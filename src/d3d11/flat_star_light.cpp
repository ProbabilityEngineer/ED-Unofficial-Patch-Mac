// visual.star_light_colour: flat_star_light.h says what and why; the rules are flat_star_light_math.h's.
#include "flat_star_light.h"

#include "flat_compute_readback.h"   // FlatComputeInternalScope: EDVR's own copies pass the hooks straight through
#include "flat_deferred_camera.h"    // kFlatDeferredHdrVs/Ps: the sun pair
#include "flat_star_glow_math.h"     // kFlatStarQuadBVs/Ps: the star quad
#include "flat_star_light_math.h"

#include "../common/config.h"
#include "../common/hotkey.h"
#include "../common/log.h"
#include "../common/runtime_profile.h"
#include "journal_watch.h"         // the in-flight test for the ring dust's ambient

#include <windows.h>
#include <d3d11_1.h>
#include <wrl/client.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iterator>
#include <vector>

namespace edvr {

std::atomic<bool> g_starLightLive{false};
std::atomic<void*> g_starLightSunBuffer{nullptr};
std::atomic<void*> g_starLightLightBuffer[kStarLightLightBuffers] = {};
std::atomic<void*> g_starLightProbeBuffer{nullptr};
std::atomic<bool> g_hazeProbeOn{false};
float g_starLightGameLight[3] = {0, 0, 0};   // the game's own light, before the fix (the diagnostic logs it)

namespace {

using Microsoft::WRL::ComPtr;

constexpr unsigned kRow = 40;                 // the light's colour times brightness, in the sun draw's b2 window
constexpr unsigned kReadEveryFrames = 30;     // the star colour is read at most this often (it changes only with the system)
constexpr unsigned kWaitFrames = 3;           // frames between a colour copy and its map

int g_strength = 0;                           // 0..100
uint32_t g_gen = 0;
bool g_genRead = false;

ComPtr<ID3D11Buffer> g_sunBuffer;             // held, so its address cannot be reused by another buffer while it is watched
uint32_t g_rowOffset = kRow * 16u;            // bytes from the buffer's start: the window's first constant * 16 + row 40
void* g_mapped = nullptr;                     // the game's pointer of its current Map of the sun buffer

// The stars seen (a system can show two: Kpelidoog's K star and its M companion). Each write of the light takes the star whose tint
// explains the game's light best (starlight::misfit): the light lies between its source's tint and white at any distance.
constexpr unsigned kMaxStars = 4;
struct Star { float disc[3]; float tint[3]; float peak; };   // peak: the disc colour's largest channel, how bright the star looks
Star g_stars[kMaxStars];
unsigned g_starCount = 0;
bool g_haveTint = false;                      // g_starCount > 0
float g_tint[3] = {1, 1, 1};                  // the last one used (the window line)
bool g_havePick = false;                      // a confident choice was made: g_tint is its tint
int g_lastLogged = -1;

// The system's own tint, learned on arrival. Right after a jump the game's light is its star's full, unwhitened tint (every arrival
// flown on 2026-11-15, at 1.7 to 8.7 Ls), and the game picks it by class: a late F star with a G star's disc colour was lit with
// the F tint. So the most saturated light of the first seconds after a jump is the tint used in that system; the prediction from
// the disc colour is the fallback, for a session that starts away from an arrival (at a station).
constexpr ULONGLONG kArrivalMs = 12000;
bool g_arriving = false;                      // a jump was seen (the light went to zero); learning until g_arrivalEnd
ULONGLONG g_arrivalEnd = 0;                   // 0: the first light after the jump has not come yet
bool g_learned = false;
float g_learnedTint[3] = {1, 1, 1};
float g_learnedSat = 0.f;                     // its distance from the game's white (red 1)
ULONGLONG g_arrivalStart = 0;                 // the first light after the jump
// A jump the light does not go dark for (Taygeta from a K star, 2026-11-16: the light went from the K star's straight to Taygeta's, and the
// K tint held there turned the system yellow). The held tint is dropped when the game's light stops fitting it (its colour off the line
// from that tint to white, starlight::misfit) for this many writes in a row; the tint is then predicted from the star in view.
constexpr float kForeignMisfit = 0.15f;
constexpr unsigned kForeignWrites = 60;
unsigned g_foreignWrites = 0;
bool g_scanRequested = false, g_scanTaken = true;   // TEMPORARY: the copy scan's request for this arrival

ComPtr<ID3D11Buffer> g_staging[kMaxStars];    // one read's copies in flight: the star quads of one frame
unsigned g_stagingCount = 0;                  // copies queued this read
unsigned g_stagingAge = 0;
bool g_stagingPending = false;
bool g_readingFrame = false;                  // this frame's star quads are being copied
unsigned g_framesSinceRead = kReadEveryFrames;

uint64_t g_writes = 0, g_tinted = 0;

// TEST: the light buffers (flat_star_light.h). Learned from PS t1/t2 of the pixel shaders that read them in the scans of 2026-11-16
// (the ring at Taygeta, an atmospheric planet at 4000 Ls); a write is re-tinted only where a light's colour (byte 0 of each 48 bytes) has
// the colour of the game's light at the last sun write.
constexpr uint64_t kLightReaderPs[] = {
    0x15D1067AC3DBE44Cull, 0x4E4FF61E8A08FC7Eull, 0x6B8C26FAC5558C6Cull, 0x702C3974A260DE14ull, 0x80D2471DFF02654Full,
    0x8717694A527EC745ull, 0x9343961582C81D86ull, 0xD0B9213C1F248335ull, 0xED91F94EC94FA5C2ull,
};
constexpr unsigned kLightStride = 48, kLightMaxBytes = 1024, kLightLookupsPerFrame = 8;
struct LightBuffer { ComPtr<ID3D11Buffer> buffer; uint32_t bytes = 0; void* mapped = nullptr; uint64_t lastUse = 0; uint64_t ps = 0; };
LightBuffer g_light[kStarLightLightBuffers];
unsigned g_lightLookups = 0;
uint64_t g_frameCounter = 0;
bool g_factorValid = false;                   // the last sun write was re-tinted: g_factor took its light to the tinted one
float g_factor[3] = {1, 1, 1};
float g_factorLight[3] = {0, 0, 0};           // that write's light before the fix
uint64_t g_lightMaps = 0, g_lightUpdates = 0, g_lightTinted = 0, g_lightRefused = 0;

void noteFactor(const float* row, const float after[3]) {
    for (int k = 0; k < 3; ++k) {
        if (!(row[k] > 0.f)) { g_factorValid = false; return; }
        g_factor[k] = after[k] / row[k];
        g_factorLight[k] = row[k];
    }
    g_factorValid = true;
}
ULONGLONG g_windowStart = 0;

void setSunBuffer(ID3D11Buffer* b, uint32_t offset) {
    if (g_sunBuffer.Get() != b) {
        g_sunBuffer = b;
        g_mapped = nullptr;
        g_starLightSunBuffer.store(b, std::memory_order_relaxed);
    }
    g_rowOffset = offset;
}

void tint(void* data, uint32_t bytesFromStart, uint32_t windowStart) {
    // `data` holds the buffer's bytes from `windowStart` on; the row sits at g_rowOffset from the buffer's start.
    if (!data || g_rowOffset < windowStart) return;
    float* row = reinterpret_cast<float*>(static_cast<uint8_t*>(data) + (g_rowOffset - windowStart));
    (void)bytesFromStart;
    ++g_writes;
    float light[3];
    std::memcpy(light, row, sizeof(light));
    std::memcpy(g_starLightGameLight, light, sizeof(light));
    if (light[0] == 0.f && light[1] == 0.f && light[2] == 0.f) {
        // No light: the jump, or no star. The next system's star must be seen before it tints anything, and its arrival is learned.
        g_factorValid = false;
        g_haveTint = false;
        g_havePick = false;
        g_starCount = 0;
        g_learned = false;
        g_learnedSat = 0.f;
        g_arriving = true;
        g_arrivalEnd = 0;
        return;
    }
    if (g_arriving && light[0] > 0.f) {
        const ULONGLONG now = GetTickCount64();
        if (!g_arrivalEnd) { g_arrivalEnd = now + kArrivalMs; g_arrivalStart = now; g_scanTaken = false; }
        const float c[3] = {1.f, light[1] / light[0], light[2] / light[0]};
        const float dg = c[1] - starlight::kWhite[1], db = c[2] - starlight::kWhite[2];
        const float sat = std::sqrt(dg * dg + db * db);
        if (sat > g_learnedSat) { g_learnedSat = sat; std::memcpy(g_learnedTint, c, sizeof(c)); g_learned = true; }
        if (!g_scanTaken && g_learned && now - g_arrivalStart >= 1500) { g_scanTaken = true; g_scanRequested = true; }
        if (now >= g_arrivalEnd) {
            g_arriving = false;
            if (g_learned)
                Log::get().note("star light: arrival: the game's own tint here is (%.3f,%.3f,%.3f); it is used in this system (the disc colour's prediction is the fallback)",
                                g_learnedTint[0], g_learnedTint[1], g_learnedTint[2]);
        }
    }
    g_factorValid = false;
    if (g_learned && !g_arriving) {
        if (starlight::misfit(light, g_learnedTint) > kForeignMisfit) {
            if (++g_foreignWrites >= kForeignWrites) {
                Log::get().note("star light: the game's light (%.3f,%.3f,%.3f, red 1) no longer fits the tint learned on arrival (%.3f,%.3f,%.3f): "
                                "a jump the light did not go dark for; the tint is predicted from the star in view until the next arrival",
                                1.f, light[1] / light[0], light[2] / light[0], g_learnedTint[0], g_learnedTint[1], g_learnedTint[2]);
                g_learned = false;
                g_learnedSat = 0.f;
                g_havePick = false;
                g_starCount = 0;
                g_haveTint = false;
                g_foreignWrites = 0;
            }
        } else {
            g_foreignWrites = 0;
        }
    }
    if (g_strength <= 0) return;
    if (g_learned) {
        std::memcpy(g_tint, g_learnedTint, sizeof(g_tint));
        starlight::apply(light, g_tint, starlight::equalStrength(g_tint, g_strength / 100.f));
        noteFactor(row, light);
        std::memcpy(row, light, sizeof(light));
        ++g_tinted;
        return;
    }
    if (!g_haveTint) return;
    int best = 0;
    float bestFit = 1e30f, second = 1e30f;
    for (unsigned i = 0; i < g_starCount; ++i) {
        const float fit = starlight::misfit(light, g_stars[i].tint);
        if (fit < bestFit) { second = bestFit; bestFit = fit; best = static_cast<int>(i); }
        else if (fit < second) second = fit;
    }
    // A light that fits two stars about equally (a white one, far out, fits every star) keeps the last confident choice: the light is
    // tinted on arrival, so the source is chosen there and held.
    const bool confident = g_starCount < 2 || second - bestFit >= 0.005f;
    if (!confident && g_havePick) {
        // the star whose tint is the held one's (the list is read again every half second, in the frame's draw order)
        float nearest = 1e30f;
        for (unsigned i = 0; i < g_starCount; ++i) {
            const float d = std::fabs(g_stars[i].tint[1] - g_tint[1]) + std::fabs(g_stars[i].tint[2] - g_tint[2]);
            if (d < nearest) { nearest = d; best = static_cast<int>(i); }
        }
    } else if (!confident) {
        // No tinted light has chosen yet (a flight that starts far out, at a station): the star that looks brightest, the system's
        // own (Kpelidoog from Hipparchus Terminal: the K star's disc peaks at 51.9, its M companion's at 0.6).
        float brightest = -1.f;
        for (unsigned i = 0; i < g_starCount; ++i)
            if (g_stars[i].peak > brightest) { brightest = g_stars[i].peak; best = static_cast<int>(i); }
    }
    std::memcpy(g_tint, g_stars[best].tint, sizeof(g_tint));
    if (confident) g_havePick = true;
    if (best != g_lastLogged && g_starCount > 1) {
        g_lastLogged = best;
        Log::get().note("star light: %u stars in view; the light (%.0f,%.0f,%.0f) fits star %d's tint (%.3f,%.3f,%.3f) best (misfit %.3f)",
                        g_starCount, light[0], light[1], light[2], best, g_tint[0], g_tint[1], g_tint[2], bestFit);
    }
    starlight::apply(light, g_tint, starlight::equalStrength(g_tint, g_strength / 100.f));
    noteFactor(row, light);
    std::memcpy(row, light, sizeof(light));
    ++g_tinted;
}

// A light buffer write: `data` holds bytes [start, end) of the buffer. Each light's colour that has the game's light colour takes the factor.
void tintLights(void* data, uint32_t start, uint32_t end) {
    if (!data || !g_factorValid || g_strength <= 0) return;
    const float lp = std::fmax(g_factorLight[0], std::fmax(g_factorLight[1], g_factorLight[2]));
    if (!(lp > 0.f)) return;
    bool any = false, refused = false;
    for (uint32_t at = 0; at + 12u <= kLightMaxBytes; at += kLightStride) {
        if (at < start || at + 12u > end) continue;
        float* c = reinterpret_cast<float*>(static_cast<uint8_t*>(data) + (at - start));
        float v[3];
        std::memcpy(v, c, sizeof(v));
        bool ok = true;
        for (int k = 0; k < 3 && ok; ++k) ok = std::isfinite(v[k]) && v[k] > 0.f;
        const float vp = ok ? std::fmax(v[0], std::fmax(v[1], v[2])) : 0.f;
        for (int k = 0; k < 3 && ok; ++k) ok = std::fabs(v[k] / vp - g_factorLight[k] / lp) < 0.02f;
        if (!ok) { refused = true; continue; }
        for (int k = 0; k < 3; ++k) v[k] *= g_factor[k];
        std::memcpy(c, v, sizeof(v));
        any = true;
    }
    if (any) ++g_lightTinted;
    else if (refused) ++g_lightRefused;
}

int lightIndex(const void* res) {
    if (!res) return -1;
    for (unsigned i = 0; i < kStarLightLightBuffers; ++i) if (g_light[i].buffer.Get() == res) return static_cast<int>(i);
    return -1;
}

void learnLightBuffers(ID3D11DeviceContext* ctx, uint64_t ps) {
    if (g_lightLookups >= kLightLookupsPerFrame) return;
    if (std::find(std::begin(kLightReaderPs), std::end(kLightReaderPs), ps) == std::end(kLightReaderPs)) return;
    ++g_lightLookups;
    ID3D11ShaderResourceView* views[2] = {};
    ctx->PSGetShaderResources(1, 2, views);
    for (unsigned s = 0; s < 2; ++s) {
        ID3D11ShaderResourceView* v = views[s];
        if (!v) continue;
        D3D11_SHADER_RESOURCE_VIEW_DESC vd{};
        v->GetDesc(&vd);
        ComPtr<ID3D11Resource> res;
        v->GetResource(&res);
        ComPtr<ID3D11Buffer> b;
        D3D11_BUFFER_DESC d{};
        if ((vd.ViewDimension == D3D11_SRV_DIMENSION_BUFFER || vd.ViewDimension == D3D11_SRV_DIMENSION_BUFFEREX) && res && SUCCEEDED(res.As(&b)))
            b->GetDesc(&d);
        if (b && d.ByteWidth >= 12u && d.ByteWidth <= kLightMaxBytes) {
            const int known = lightIndex(b.Get());
            if (known >= 0) g_light[known].lastUse = g_frameCounter;
            else {
                unsigned slot = 0;   // an empty slot, else the one unused longest
                for (unsigned i = 0; i < kStarLightLightBuffers; ++i) {
                    if (!g_light[i].buffer) { slot = i; break; }
                    if (g_light[i].lastUse < g_light[slot].lastUse) slot = i;
                }
                LightBuffer& lb = g_light[slot];
                lb = LightBuffer{};
                lb.buffer = b;
                lb.bytes = d.ByteWidth;
                lb.lastUse = g_frameCounter;
                lb.ps = ps;
                g_starLightLightBuffer[slot].store(b.Get(), std::memory_order_relaxed);
                Log::get().note("star light: light buffer %u watched (PS t%u of ps %016llX, %u bytes, usage %u, cpu access %u, misc %u)", slot, s + 1,
                                static_cast<unsigned long long>(ps), d.ByteWidth, static_cast<unsigned>(d.Usage), d.CPUAccessFlags, d.MiscFlags);
            }
        }
        v->Release();
    }
}

void dropLightBuffers() {
    for (unsigned i = 0; i < kStarLightLightBuffers; ++i) {
        g_starLightLightBuffer[i].store(nullptr, std::memory_order_relaxed);
        g_light[i] = LightBuffer{};
    }
    g_factorValid = false;
}

void readStar(ID3D11DeviceContext* ctx) {
    if (!g_readingFrame) {
        if (g_stagingPending || g_framesSinceRead < kReadEveryFrames) return;
        g_readingFrame = true;   // this frame's star quads, all of them (up to kMaxStars), make the read
        g_stagingCount = 0;
    }
    if (g_stagingCount >= kMaxStars) return;
    ComPtr<ID3D11DeviceContext1> c1;
    ctx->QueryInterface(IID_PPV_ARGS(&c1));
    ID3D11Buffer* b = nullptr;
    UINT first = 0, num = 0;
    if (c1) c1->PSGetConstantBuffers1(2, 1, &b, &first, &num);
    else ctx->PSGetConstantBuffers(2, 1, &b);
    if (!b) return;
    D3D11_BUFFER_DESC d{};
    b->GetDesc(&d);
    const uint32_t offset = first * 16u;
    if (offset + 16u <= d.ByteWidth) {
        ComPtr<ID3D11Buffer>& slot = g_staging[g_stagingCount];
        if (!slot) {
            ComPtr<ID3D11Device> dev;
            ctx->GetDevice(&dev);
            D3D11_BUFFER_DESC sd{};
            sd.ByteWidth = 16;
            sd.Usage = D3D11_USAGE_STAGING;
            sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            if (dev) dev->CreateBuffer(&sd, nullptr, &slot);
        }
        if (slot) {
            FlatComputeInternalScope internal;
            const D3D11_BOX box{offset, 0, 0, offset + 16u, 1, 1};
            ctx->CopySubresourceRegion(slot.Get(), 0, 0, 0, 0, b, 0, &box);
            ++g_stagingCount;
        }
    }
    b->Release();
}

void finishStar(ID3D11DeviceContext* ctx) {
    if (g_readingFrame) {   // the read frame just ended: its copies are in flight
        g_readingFrame = false;
        g_stagingPending = g_stagingCount > 0;
        g_stagingAge = 0;
        g_framesSinceRead = 0;
        return;
    }
    if (!g_stagingPending || ++g_stagingAge < kWaitFrames) return;
    FlatComputeInternalScope internal;
    Star got[kMaxStars];
    unsigned n = 0;
    for (unsigned i = 0; i < g_stagingCount; ++i) {
        D3D11_MAPPED_SUBRESOURCE m{};
        const HRESULT hr = ctx->Map(g_staging[i].Get(), 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &m);
        if (hr == DXGI_ERROR_WAS_STILL_DRAWING) return;   // all of them next frame
        if (FAILED(hr) || !m.pData) continue;
        float row[4];
        std::memcpy(row, m.pData, sizeof(row));
        ctx->Unmap(g_staging[i].Get(), 0);
        Star st{{row[1], row[2], row[3]}, {0, 0, 0}, std::fmax(row[1], std::fmax(row[2], row[3]))};
        if (!starlight::tintFor(st.disc, st.tint)) continue;
        bool dup = false;   // the same star drawn twice in the frame
        for (unsigned j = 0; j < n && !dup; ++j)
            dup = std::fabs(got[j].disc[0] - st.disc[0]) + std::fabs(got[j].disc[1] - st.disc[1]) + std::fabs(got[j].disc[2] - st.disc[2]) <
                  1e-3f * (st.disc[0] + st.disc[1] + st.disc[2]);
        if (!dup) got[n++] = st;
    }
    g_stagingPending = false;
    if (!n) return;
    bool changed = n != g_starCount;
    for (unsigned i = 0; i < n && !changed; ++i)
        changed = std::fabs(got[i].tint[1] - g_stars[i].tint[1]) + std::fabs(got[i].tint[2] - g_stars[i].tint[2]) > 1e-3f;
    for (unsigned i = 0; i < n; ++i) g_stars[i] = got[i];
    g_starCount = n;
    g_haveTint = true;
    if (changed) {
        g_lastLogged = -1;
        for (unsigned i = 0; i < n; ++i) {
            const float* d = got[i].disc;
            const float peak = std::fmax(d[0], std::fmax(d[1], d[2]));
            Log::get().note("star light: star %u of %u in view: colour (%.3f,%.3f,%.3f) (blue/red %.3f) gives the light tint (%.3f,%.3f,%.3f); strength %d%%",
                            i, n, d[0] / peak, d[1] / peak, d[2] / peak, d[2] / d[0], got[i].tint[0], got[i].tint[1], got[i].tint[2], g_strength);
        }
    }
}

}  // namespace

namespace {

// ---- The ring dust's light (flat_star_light.h; the haze hunt and probe of 2026-11-16) ----
// The ring's dust is drawn by the volume pair 203DF517/EEAAC839 and the ribbon pair B12F7A61/42AC0CAC. The probe showed its light colour is
// row 293 of the shared constants it reads at b1 (made red, the dust went red; rows 26 and 159 did nothing; blanking its ambient cube did
// not change its colour). b1 is rewritten by the game for nearly every draw, so its bytes are kept from each write (the Map hooks), and for
// those two draws only, in flight, a copy with row 293 times the sun write's per-channel factor (g_factor) is bound at VS and PS b1.
constexpr uint64_t kHazeVolumeVs = 0x203DF51758AADC4Dull, kHazeVolumePs = 0xEEAAC839A9F09448ull;
constexpr uint64_t kHazeRibbonVs = 0xB12F7A618E1BDE98ull, kHazeRibbonPs = 0x42AC0CACC9CDF72Bull;
constexpr unsigned kHazeRow = 293;
bool g_inFlight = false;
uint64_t g_hazeBound = 0, g_hazeSkipped = 0, g_hazeRefused = 0;
ComPtr<ID3D11Buffer> g_hazeB1, g_hazeCopy;          // the game's b1 (watched), and the copy bound for the dust
std::vector<uint8_t> g_hazeShadow;                   // b1's bytes at its last write
bool g_hazeShadowOk = false;
void* g_hazeMapped = nullptr;

void hazeWatchB1(ID3D11DeviceContext* ctx) {
    if (g_hazeB1) return;
    ID3D11Buffer* b = nullptr;
    ctx->PSGetConstantBuffers(1, 1, &b);
    if (!b) return;
    D3D11_BUFFER_DESC d{};
    b->GetDesc(&d);
    if (d.ByteWidth >= (kHazeRow + 1u) * 16u) {
        g_hazeB1 = b;
        g_starLightProbeBuffer.store(b, std::memory_order_relaxed);
        Log::get().note("star light: ring dust: b1 %p watched (%u bytes, usage %u); its row %u is the dust's light", static_cast<void*>(b), d.ByteWidth,
                        static_cast<unsigned>(d.Usage), kHazeRow);
    }
    b->Release();
}

void hazeDropB1() {
    g_starLightProbeBuffer.store(nullptr, std::memory_order_relaxed);
    g_hazeB1.Reset();
    g_hazeShadowOk = false;
    g_hazeMapped = nullptr;
}

// b1 with `edit` applied to the shadow, bound at VS and PS b1 through `copy` (made on first use). False: nothing bound.
template <class Edit>
bool hazeBindCopy(ID3D11DeviceContext* ctx, ComPtr<ID3D11Buffer>& copy, HazeProbeRestore* r, Edit edit) {
    if (!g_hazeB1 || !g_hazeShadowOk) return false;
    ID3D11Buffer* curPs = nullptr;
    ctx->PSGetConstantBuffers(1, 1, &curPs);
    if (curPs != g_hazeB1.Get()) { if (curPs) curPs->Release(); return false; }   // bound with another buffer (or an offset view): leave it
    if (!copy) {
        ComPtr<ID3D11Device> dev;
        ctx->GetDevice(&dev);
        D3D11_BUFFER_DESC d{};
        d.ByteWidth = static_cast<UINT>((g_hazeShadow.size() + 15u) & ~size_t(15));
        d.Usage = D3D11_USAGE_DYNAMIC;
        d.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        d.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        if (!dev || FAILED(dev->CreateBuffer(&d, nullptr, &copy))) { curPs->Release(); return false; }
    }
    {
        FlatComputeInternalScope internal;
        D3D11_MAPPED_SUBRESOURCE m{};
        if (FAILED(ctx->Map(copy.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &m)) || !m.pData) { curPs->Release(); return false; }
        std::memcpy(m.pData, g_hazeShadow.data(), g_hazeShadow.size());
        if (!edit(static_cast<uint8_t*>(m.pData))) { ctx->Unmap(copy.Get(), 0); curPs->Release(); return false; }
        ctx->Unmap(copy.Get(), 0);
    }
    ID3D11Buffer* curVs = nullptr;
    ctx->VSGetConstantBuffers(1, 1, &curVs);
    ID3D11Buffer* mine = copy.Get();
    ctx->PSSetConstantBuffers(1, 1, &mine);
    ctx->VSSetConstantBuffers(1, 1, &mine);
    r->kind = 3; r->slot = 1; r->ps = curPs; r->vs = curVs;
    return true;
}
}  // namespace

void starLightHazeBind(ID3D11DeviceContext* ctx, uint64_t vs, uint64_t ps, HazeProbeRestore* r) {
    if (!(vs == kHazeVolumeVs && ps == kHazeVolumePs) && !(vs == kHazeRibbonVs && ps == kHazeRibbonPs)) return;
    hazeWatchB1(ctx);
    if (!g_inFlight || !g_factorValid || g_strength <= 0) { ++g_hazeSkipped; return; }
    const float lp = std::fmax(g_factorLight[0], std::fmax(g_factorLight[1], g_factorLight[2]));
    const bool bound = hazeBindCopy(ctx, g_hazeCopy, r, [lp](uint8_t* data) {
        float* v = reinterpret_cast<float*>(data + kHazeRow * 16u);
        bool ok = lp > 0.f;
        for (int k = 0; k < 3 && ok; ++k) ok = std::isfinite(v[k]) && v[k] > 0.f;
        const float vp = ok ? std::fmax(v[0], std::fmax(v[1], v[2])) : 0.f;
        for (int k = 0; k < 3 && ok; ++k) ok = std::fabs(v[k] / vp - g_factorLight[k] / lp) < 0.02f;   // the row has the game's light colour
        if (!ok) { ++g_hazeRefused; return false; }
        for (int k = 0; k < 3; ++k) v[k] *= g_factor[k];
        return true;
    });
    if (bound) ++g_hazeBound; else ++g_hazeSkipped;
}

// ---- TEMPORARY: the haze probe (flat_star_light.h) ----
namespace {
constexpr unsigned kProbeStates = 17;   // 0 none; 1..8 PS t0..t7 blank; 9..13 PS b0..b4 blank; 14..16 b1 row 26 / 159 / 293 red
const char* probeName(unsigned st, char* buf, size_t n) {
    if (st == 0) std::snprintf(buf, n, "nothing changed");
    else if (st <= 8) std::snprintf(buf, n, "PS t%u blank", st - 1);
    else if (st <= 13) std::snprintf(buf, n, "PS b%u blank", st - 9);
    else std::snprintf(buf, n, "b1 row %u pure red", st == 14 ? 26u : st == 15 ? 159u : 293u);
    return buf;
}
unsigned g_probeState = 0;
bool g_probeDescribed = false;
bool g_probeKeysMade = false;
Hotkey g_probeNext, g_probePrev, g_probeReset;
ComPtr<ID3D11Buffer> g_probeCopy;
uint64_t g_probeDraws = 0;

void probeFrameBoundary() {
    const bool on = runtimeFlatProfile() && Config::get().getBool("diag.haze_probe", false);
    if (on != g_hazeProbeOn.load(std::memory_order_relaxed)) {
        Log::get().note("haze probe: %s", on ? "on: Ctrl+F9 next input, Ctrl+F11 back, Ctrl+F12 none; only the ring dust's volume draw changes" : "off");
        g_hazeProbeOn.store(on, std::memory_order_relaxed);
        g_probeState = 0;
        if (!on && g_strength <= 0) hazeDropB1();
    }
    if (!on) return;
    if (!g_probeKeysMade) {
        g_probeKeysMade = true;
        g_probeNext.setKey(VK_F9, kHotkeyCtrl);
        g_probePrev.setKey(VK_F11, kHotkeyCtrl);
        g_probeReset.setKey(VK_F12, kHotkeyCtrl);
    }
    unsigned st = g_probeState;
    if (g_probeNext.pressed()) st = (st + 1) % kProbeStates;
    if (g_probePrev.pressed()) st = (st + kProbeStates - 1) % kProbeStates;
    if (g_probeReset.pressed()) st = 0;
    if (st != g_probeState) {
        g_probeState = st;
        g_probeDescribed = false;
        char b[48];
        Log::get().note("haze probe: step %u: %s (the volume draw was seen %llu times since the last step)", st, probeName(st, b, sizeof(b)),
                        static_cast<unsigned long long>(g_probeDraws));
        g_probeDraws = 0;
    }
}

void hazeMapped(void* data) { g_hazeMapped = data; }
void hazeUnmap() {
    if (!g_hazeMapped || !g_hazeB1) return;
    D3D11_BUFFER_DESC d{};
    g_hazeB1->GetDesc(&d);
    g_hazeShadow.assign(static_cast<const uint8_t*>(g_hazeMapped), static_cast<const uint8_t*>(g_hazeMapped) + d.ByteWidth);
    g_hazeShadowOk = true;
    g_hazeMapped = nullptr;
}
void hazeUpdate(const void* data, uint32_t left, uint32_t right, bool boxed) {
    if (!data || !g_hazeB1) return;
    D3D11_BUFFER_DESC d{};
    g_hazeB1->GetDesc(&d);
    if (g_hazeShadow.size() != d.ByteWidth) { g_hazeShadow.assign(d.ByteWidth, 0); if (boxed) return; }
    const uint32_t a = boxed ? left : 0u, e = boxed ? std::min(right, d.ByteWidth) : d.ByteWidth;
    if (e > a) std::memcpy(g_hazeShadow.data() + a, data, e - a);
    if (!boxed) g_hazeShadowOk = true;
}
}  // namespace

void starLightHazeProbeBind(ID3D11DeviceContext* ctx, uint64_t vs, uint64_t ps, HazeProbeRestore* r) {
    if (!(vs == kHazeVolumeVs && ps == kHazeVolumePs)) return;
    ++g_probeDraws;
    hazeWatchB1(ctx);
    const unsigned st = g_probeState;
    if (st == 0) return;
    char name[48];
    probeName(st, name, sizeof(name));
    if (st <= 8) {
        const UINT slot = st - 1;
        ID3D11ShaderResourceView* game = nullptr;
        ctx->PSGetShaderResources(slot, 1, &game);
        if (!g_probeDescribed) {
            g_probeDescribed = true;
            if (!game) Log::get().note("haze probe: %s: nothing is bound there", name);
            else {
                D3D11_SHADER_RESOURCE_VIEW_DESC vd{};
                game->GetDesc(&vd);
                ComPtr<ID3D11Resource> res;
                game->GetResource(&res);
                ComPtr<ID3D11Texture2D> t2;
                ComPtr<ID3D11Texture3D> t3;
                ComPtr<ID3D11Buffer> bf;
                if (res && SUCCEEDED(res.As(&t2))) {
                    D3D11_TEXTURE2D_DESC d{}; t2->GetDesc(&d);
                    Log::get().note("haze probe: %s: view dimension %u, 2D %ux%u x%u, %u mips, format %u, bind 0x%X, misc 0x%X", name, vd.ViewDimension, d.Width, d.Height,
                                    d.ArraySize, d.MipLevels, d.Format, d.BindFlags, d.MiscFlags);
                } else if (res && SUCCEEDED(res.As(&t3))) {
                    D3D11_TEXTURE3D_DESC d{}; t3->GetDesc(&d);
                    Log::get().note("haze probe: %s: view dimension %u, 3D %ux%ux%u, %u mips, format %u, bind 0x%X", name, vd.ViewDimension, d.Width, d.Height, d.Depth,
                                    d.MipLevels, d.Format, d.BindFlags);
                } else if (res && SUCCEEDED(res.As(&bf))) {
                    D3D11_BUFFER_DESC d{}; bf->GetDesc(&d);
                    Log::get().note("haze probe: %s: buffer, %u bytes, stride %u, usage %u", name, d.ByteWidth, d.StructureByteStride, static_cast<unsigned>(d.Usage));
                }
            }
        }
        if (!game) return;
        ID3D11ShaderResourceView* none = nullptr;
        ctx->PSSetShaderResources(slot, 1, &none);
        r->kind = 1; r->slot = slot; r->srv = game;
        return;
    }
    if (st <= 13) {
        const UINT slot = st - 9;
        ID3D11Buffer* game = nullptr;
        ctx->PSGetConstantBuffers(slot, 1, &game);
        if (!g_probeDescribed) {
            g_probeDescribed = true;
            D3D11_BUFFER_DESC d{};
            if (game) game->GetDesc(&d);
            Log::get().note("haze probe: %s: %s%u bytes", name, game ? "" : "nothing bound, ", game ? d.ByteWidth : 0u);
        }
        if (!game) return;
        ID3D11Buffer* none = nullptr;
        ctx->PSSetConstantBuffers(slot, 1, &none);
        r->kind = 2; r->slot = slot; r->ps = game;
        return;
    }
    // b1 with one light row pure red (at the row's luminance), bound at VS and PS b1 for this draw.
    const unsigned row = st == 14 ? 26u : st == 15 ? 159u : 293u;
    const bool bound = hazeBindCopy(ctx, g_probeCopy, r, [&](uint8_t* data) {
        float* v = reinterpret_cast<float*>(data + row * 16u);
        const float lum = 0.2126f * v[0] + 0.7152f * v[1] + 0.0722f * v[2];
        if (!g_probeDescribed) { g_probeDescribed = true; Log::get().note("haze probe: %s: the row was (%.5g,%.5g,%.5g,%.5g)", name, v[0], v[1], v[2], v[3]); }
        v[0] = lum > 0.f ? lum / 0.2126f : 1.f; v[1] = 0.f; v[2] = 0.f;
        return true;
    });
    if (!bound && !g_probeDescribed) { g_probeDescribed = true; Log::get().note("haze probe: %s: b1 not ready (no write of it seen yet)", name); }
}

void starLightHazeProbeRestore(ID3D11DeviceContext* ctx, HazeProbeRestore* r) {
    if (r->kind == 1) { ctx->PSSetShaderResources(r->slot, 1, &r->srv); r->srv->Release(); }
    else if (r->kind == 2) { ctx->PSSetConstantBuffers(r->slot, 1, &r->ps); r->ps->Release(); }
    else if (r->kind == 3) {
        ctx->PSSetConstantBuffers(1, 1, &r->ps);
        ctx->VSSetConstantBuffers(1, 1, &r->vs);
        if (r->ps) r->ps->Release();
        if (r->vs) r->vs->Release();
    }
    r->kind = 0;
}

void starLightDraw(ID3D11DeviceContext* ctx, uint64_t vs, uint64_t ps) {
    if (!ctx) return;
    if (vs == kFlatDeferredHdrVs && ps == kFlatDeferredHdrPs) {
        ComPtr<ID3D11DeviceContext1> c1;
        ctx->QueryInterface(IID_PPV_ARGS(&c1));
        ID3D11Buffer* b = nullptr;
        UINT first = 0, num = 0;
        if (c1) c1->PSGetConstantBuffers1(2, 1, &b, &first, &num);
        else ctx->PSGetConstantBuffers(2, 1, &b);
        if (!b) return;
        D3D11_BUFFER_DESC d{};
        b->GetDesc(&d);
        const uint32_t offset = first * 16u + kRow * 16u;
        if (offset + 12u <= d.ByteWidth) setSunBuffer(b, offset);
        b->Release();
    } else if (vs == kFlatStarQuadBVs && ps == kFlatStarQuadBPs) {
        readStar(ctx);
    } else {
        learnLightBuffers(ctx, ps);
    }
}

void starLightMapped(ID3D11Resource* res, void* data) {
    if (res == g_sunBuffer.Get()) { g_mapped = data; return; }
    if (g_hazeB1 && res == g_hazeB1.Get()) { hazeMapped(data); return; }
    const int i = lightIndex(res);
    if (i >= 0) g_light[i].mapped = data;
}

void starLightUnmap(ID3D11Resource* res) {
    if (g_hazeB1 && res == g_hazeB1.Get()) { hazeUnmap(); return; }
    if (res == g_sunBuffer.Get()) {
        if (!g_mapped) return;
        tint(g_mapped, 0, 0);
        g_mapped = nullptr;
        return;
    }
    const int i = lightIndex(res);
    if (i < 0 || !g_light[i].mapped) return;
    ++g_lightMaps;
    tintLights(g_light[i].mapped, 0, g_light[i].bytes);
    g_light[i].mapped = nullptr;
}

const void* starLightUpdate(ID3D11Resource* res, const void* data, uint32_t left, uint32_t right, bool boxed) {
    if (!data) return data;
    if (g_hazeB1 && res == g_hazeB1.Get()) { hazeUpdate(data, left, right, boxed); return data; }
    const int li = lightIndex(res);
    if (li >= 0) {
        ++g_lightUpdates;
        const uint32_t start = boxed ? left : 0u, end = boxed ? std::min(right, g_light[li].bytes) : g_light[li].bytes;
        if (end <= start) return data;
        static std::vector<uint8_t> lightCopy;
        lightCopy.assign(static_cast<const uint8_t*>(data), static_cast<const uint8_t*>(data) + (end - start));
        tintLights(lightCopy.data(), start, end);
        return lightCopy.data();
    }
    if (res != g_sunBuffer.Get()) return data;
    const uint32_t start = boxed ? left : 0u;
    if (boxed && (g_rowOffset < left || g_rowOffset + 12u > right)) return data;   // the write does not cover the row
    static std::vector<uint8_t> copy;
    D3D11_BUFFER_DESC d{};
    g_sunBuffer->GetDesc(&d);
    const uint32_t bytes = boxed ? right - left : d.ByteWidth;
    copy.assign(static_cast<const uint8_t*>(data), static_cast<const uint8_t*>(data) + bytes);
    tint(copy.data(), bytes, start);
    return copy.data();
}

bool starLightTakeScanRequest(float chroma[3]) noexcept {
    if (!g_scanRequested) return false;
    g_scanRequested = false;
    std::memcpy(chroma, g_learnedTint, sizeof(g_learnedTint));
    return true;
}

bool starLightSystemTint(float tint[3]) noexcept {
    if (g_learned) { std::memcpy(tint, g_learnedTint, sizeof(g_learnedTint)); return true; }
    if (g_haveTint) { std::memcpy(tint, g_tint, sizeof(g_tint)); return true; }
    return false;
}

void starLightFrameBoundary(ID3D11DeviceContext* ctx) {
    probeFrameBoundary();   // TEMPORARY
    const uint32_t gen = Config::get().generation();
    if (!g_genRead || gen != g_gen) {
        g_genRead = true;
        g_gen = gen;
        const int v = runtimeFlatProfile() ? Config::get().getIntInRange("visual.star_light_colour", 100, 0, 100) : 0;
        if (v != g_strength) {
            Log::get().note("star light: visual.star_light_colour = %d%%%s", v, v ? "" : " (off: the game's light as it ships)");
            g_strength = v;
        }
        g_starLightLive.store(v > 0, std::memory_order_relaxed);
        if (v <= 0) {
            setSunBuffer(nullptr, kRow * 16u);
            dropLightBuffers();
            if (!g_hazeProbeOn.load(std::memory_order_relaxed)) hazeDropB1();
            g_haveTint = false;
            g_havePick = false;
            g_starCount = 0;
        }
    }
    if (!ctx || g_strength <= 0) return;
    ++g_framesSinceRead;
    ++g_frameCounter;
    g_lightLookups = 0;
    // In flight: in a ship, a fighter or an SRV, not docked, not on foot (Status.json). Unknown is not in flight.
    g_inFlight = journalStatusSeatedKnown() && journalStatusDockedKnown() && !journalStatusDocked() &&
                 (journalStatusFlags() & kStatusFlagsSeated) != 0 && (journalStatusFlags2() & 1u) == 0;

    finishStar(ctx);
    const ULONGLONG now = GetTickCount64();
    if (!g_windowStart) g_windowStart = now;
    if (now - g_windowStart >= 30000) {
        Log::get().note("star light: this window: sun buffer writes %llu, tinted %llu; tint %s (%.3f,%.3f,%.3f), setting %d%%, this star's strength %.0f%% "
                        "(equal visible colour, flat_star_light_math.h equalStrength)",
                        static_cast<unsigned long long>(g_writes), static_cast<unsigned long long>(g_tinted),
                        g_learned ? "learned on arrival" : g_haveTint ? "predicted from the star's colour" : "none yet (no arrival seen, the star not seen)", g_tint[0], g_tint[1], g_tint[2], g_strength,
                        100.f * starlight::equalStrength(g_tint, g_strength / 100.f));
        Log::get().note("star light: this window: ring dust (in flight %s): draws re-tinted %llu, left as the game's %llu (of which row %u not the game's light colour %llu)",
                        g_inFlight ? "yes" : "no", static_cast<unsigned long long>(g_hazeBound), static_cast<unsigned long long>(g_hazeSkipped), kHazeRow,
                        static_cast<unsigned long long>(g_hazeRefused));
        g_hazeBound = g_hazeSkipped = g_hazeRefused = 0;
        unsigned watched = 0;
        for (const LightBuffer& lb : g_light) watched += lb.buffer ? 1u : 0u;
        Log::get().note("star light: this window: light buffers watched %u; writes by Map %llu, by UpdateSubresource %llu; tinted %llu, left alone %llu "
                        "(no light of the game's light colour). Watched with no writes: the game fills them some other way (a copy), which the fix does not see",
                        watched, static_cast<unsigned long long>(g_lightMaps), static_cast<unsigned long long>(g_lightUpdates),
                        static_cast<unsigned long long>(g_lightTinted), static_cast<unsigned long long>(g_lightRefused));
        g_writes = g_tinted = 0;
        g_lightMaps = g_lightUpdates = g_lightTinted = g_lightRefused = 0;
        g_windowStart = now;
    }
}

}  // namespace edvr
