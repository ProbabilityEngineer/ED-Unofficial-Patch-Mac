// visual.star_light_colour: flat_star_light.h says what and why; the rules are flat_star_light_math.h's.
#include "flat_star_light.h"

#include "flat_compute_readback.h"   // FlatComputeInternalScope: EDVR's own copies pass the hooks straight through
#include "flat_deferred_camera.h"    // kFlatDeferredHdrVs/Ps: the sun pair
#include "flat_star_glow_math.h"     // kFlatStarQuadBVs/Ps: the star quad
#include "flat_star_light_math.h"

#include "../common/config.h"
#include "../common/log.h"
#include "../common/runtime_profile.h"

#include <windows.h>
#include <d3d11_1.h>
#include <wrl/client.h>

#include <cmath>
#include <cstring>
#include <vector>

namespace edvr {

std::atomic<bool> g_starLightLive{false};
std::atomic<void*> g_starLightSunBuffer{nullptr};
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

ComPtr<ID3D11Buffer> g_staging[kMaxStars];    // one read's copies in flight: the star quads of one frame
unsigned g_stagingCount = 0;                  // copies queued this read
unsigned g_stagingAge = 0;
bool g_stagingPending = false;
bool g_readingFrame = false;                  // this frame's star quads are being copied
unsigned g_framesSinceRead = kReadEveryFrames;

uint64_t g_writes = 0, g_tinted = 0;
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
        // No light: the jump, or no star. The next system's star must be seen before it tints anything.
        g_haveTint = false;
        g_havePick = false;
        g_starCount = 0;
        return;
    }
    if (!g_haveTint || g_strength <= 0) return;
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
    starlight::apply(light, g_tint, g_strength / 100.f);
    std::memcpy(row, light, sizeof(light));
    ++g_tinted;
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
    }
}

void starLightMapped(ID3D11Resource* res, void* data) {
    if (res == g_sunBuffer.Get()) g_mapped = data;
}

void starLightUnmap(ID3D11Resource* res) {
    if (res != g_sunBuffer.Get() || !g_mapped) return;
    tint(g_mapped, 0, 0);
    g_mapped = nullptr;
}

const void* starLightUpdate(ID3D11Resource* res, const void* data, uint32_t left, uint32_t right, bool boxed) {
    if (res != g_sunBuffer.Get() || !data) return data;
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

void starLightFrameBoundary(ID3D11DeviceContext* ctx) {
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
            g_haveTint = false;
            g_havePick = false;
            g_starCount = 0;
        }
    }
    if (!ctx || g_strength <= 0) return;
    ++g_framesSinceRead;
    finishStar(ctx);
    const ULONGLONG now = GetTickCount64();
    if (!g_windowStart) g_windowStart = now;
    if (now - g_windowStart >= 30000) {
        Log::get().note("star light: this window: sun buffer writes %llu, tinted %llu; tint %s (%.3f,%.3f,%.3f), strength %d%%",
                        static_cast<unsigned long long>(g_writes), static_cast<unsigned long long>(g_tinted),
                        g_haveTint ? "from the star" : "none yet (the star not seen since the jump)", g_tint[0], g_tint[1], g_tint[2], g_strength);
        g_writes = g_tinted = 0;
        g_windowStart = now;
    }
}

}  // namespace edvr
