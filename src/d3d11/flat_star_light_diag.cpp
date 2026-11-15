// TEMPORARY: diag.star_light_log. flat_star_light_diag.h says what and why.
#include "flat_star_light_diag.h"

#include "flat_compute_readback.h"   // FlatComputeInternalScope: EDVR's own copies pass the hooks straight through
#include "flat_deferred_camera.h"    // kFlatDeferredHdrVs/Ps: the sun pair
#include "flat_star_glow_math.h"     // kFlatStarQuadBVs/Ps: the star quad whose PS b2[0].yzw is the star's colour

#include "../common/config.h"
#include "../common/log.h"
#include "../common/runtime_profile.h"

#include <windows.h>
#include <d3d11_1.h>
#include <wrl/client.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace edvr {

std::atomic<bool> g_starLightDiagFrame{false};

namespace {

using Microsoft::WRL::ComPtr;

constexpr unsigned kSlots = 8;          // PS b0..b7 of the sun draw
constexpr unsigned kMaxBytes = 4096;    // per buffer copied
constexpr unsigned kMaxSunDraws = 4;    // one draw per batch of two lights; a sample keeps the frame's first four
constexpr unsigned kMaxStarDraws = 4;
constexpr unsigned kWaitFrames = 3;     // frames between the copies and the map
constexpr unsigned kMaxLoggedRows = 12;

enum Kind : uint32_t { kSun = 0, kStar = 1 };
struct Copy {
    uint32_t kind = 0, draw = 0, slot = 0, offset = 0, bytes = 0;
    ComPtr<ID3D11Buffer> staging;
};

std::vector<Copy> g_pending;
unsigned g_waited = 0;
bool g_inFlight = false;   // a sample frame was armed and its results are not logged yet
unsigned g_sunDraws = 0, g_starDraws = 0;
uint32_t g_gen = 0;
bool g_genRead = false;
int g_interval = 0;
ULONGLONG g_next = 0;
uint64_t g_sample = 0;
FILE* g_file = nullptr;
bool g_fileFailed = false;

void queue(ID3D11DeviceContext* ctx, Kind kind, unsigned draw, unsigned firstSlot, unsigned count) {
    ComPtr<ID3D11DeviceContext1> c1;
    ctx->QueryInterface(IID_PPV_ARGS(&c1));
    ID3D11Buffer* buffers[kSlots] = {};
    UINT first[kSlots] = {}, num[kSlots] = {};
    if (c1) c1->PSGetConstantBuffers1(firstSlot, count, buffers, first, num);
    else ctx->PSGetConstantBuffers(firstSlot, count, buffers);
    ComPtr<ID3D11Device> dev;
    ctx->GetDevice(&dev);
    FlatComputeInternalScope internal;
    for (unsigned i = 0; i < count; ++i) {
        ID3D11Buffer* b = buffers[i];
        if (!b) continue;
        D3D11_BUFFER_DESC d{};
        b->GetDesc(&d);
        unsigned offset = c1 ? first[i] * 16u : 0u;
        unsigned bytes = (c1 && num[i]) ? num[i] * 16u : d.ByteWidth;
        if (dev && offset < d.ByteWidth) {
            if (offset + bytes > d.ByteWidth) bytes = d.ByteWidth - offset;
            if (bytes > kMaxBytes) bytes = kMaxBytes;
            D3D11_BUFFER_DESC sd{};
            sd.ByteWidth = (bytes + 15u) & ~15u;
            sd.Usage = D3D11_USAGE_STAGING;
            sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            Copy c;
            c.kind = kind; c.draw = draw; c.slot = firstSlot + i; c.offset = offset; c.bytes = bytes;
            if (SUCCEEDED(dev->CreateBuffer(&sd, nullptr, &c.staging))) {
                const D3D11_BOX box{offset, 0, 0, offset + bytes, 1, 1};
                ctx->CopySubresourceRegion(c.staging.Get(), 0, 0, 0, 0, b, 0, &box);
                g_pending.push_back(std::move(c));
            }
        }
        b->Release();
    }
}

bool finite3(const float* v) { return std::isfinite(v[0]) && std::isfinite(v[1]) && std::isfinite(v[2]); }

// Peak-normalised chromaticity distance: the largest per-channel difference once each colour is divided by its own peak.
float chromaDistance(const float* a, const float* b) {
    const float pa = std::fmax(a[0], std::fmax(a[1], a[2])), pb = std::fmax(b[0], std::fmax(b[1], b[2]));
    if (!(pa > 0.f) || !(pb > 0.f)) return 1e9f;
    float d = 0.f;
    for (int k = 0; k < 3; ++k) d = std::fmax(d, std::fabs(a[k] / pa - b[k] / pb));
    return d;
}

void openFile() {
    if (g_file || g_fileFailed) return;
    SYSTEMTIME t;
    GetLocalTime(&t);
    wchar_t name[64];
    _snwprintf_s(name, _TRUNCATE, L"\\star_light_%04u%02u%02u_%02u%02u%02u.bin", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
    const std::wstring path = Log::get().dir() + name;
    if (_wfopen_s(&g_file, path.c_str(), L"wb") != 0) g_file = nullptr;
    if (!g_file) {
        g_fileFailed = true;
        Log::get().note("star light: could not open the dump file; samples are logged but not saved");
    }
}

// The copies have run (kWaitFrames later): map, log, append, release.
void finish(ID3D11DeviceContext* ctx) {
    struct Got { Copy* c; std::vector<uint8_t> data; };
    std::vector<Got> got;
    for (Copy& c : g_pending) {
        D3D11_MAPPED_SUBRESOURCE m{};
        if (FAILED(ctx->Map(c.staging.Get(), 0, D3D11_MAP_READ, 0, &m)) || !m.pData) continue;
        Got g{&c, std::vector<uint8_t>(c.bytes)};
        std::memcpy(g.data.data(), m.pData, c.bytes);
        ctx->Unmap(c.staging.Get(), 0);
        got.push_back(std::move(g));
    }
    // The star's colour: the first star quad's b2[0].yzw.
    float star[3] = {0, 0, 0};
    bool haveStar = false;
    for (const Got& g : got) {
        if (g.c->kind != kStar || g.c->bytes < 16) continue;
        float row[4];
        std::memcpy(row, g.data.data(), 16);
        if (finite3(row + 1) && row[1] + row[2] + row[3] > 0.f) { std::memcpy(star, row + 1, 12); haveStar = true; break; }
    }
    // The sun draws' rows with a positive colour-like xyz: those with the star's chromaticity first, then the others (up to the cap).
    std::string matches, others;
    unsigned shown = 0, matched = 0, positive = 0;
    for (const Got& g : got) {
        if (g.c->kind != kSun) continue;
        const unsigned rows = g.c->bytes / 16u;
        for (unsigned r = 0; r < rows; ++r) {
            float v[4];
            std::memcpy(v, g.data.data() + r * 16u, 16);
            if (!finite3(v) || !(v[0] > 0.f && v[1] > 0.f && v[2] > 0.f)) continue;
            ++positive;
            const bool match = haveStar && chromaDistance(v, star) < 0.08f;
            if (match) ++matched;
            if (shown >= kMaxLoggedRows) continue;
            char item[160];
            std::snprintf(item, sizeof(item), " d%u.b%u[%u]=(%.5g,%.5g,%.5g,%.5g)", g.c->draw, g.c->slot, g.c->offset / 16u + r, v[0], v[1], v[2], v[3]);
            (match ? matches : others) += item;
            ++shown;
        }
    }
    const float peak = std::fmax(star[0], std::fmax(star[1], star[2]));
    Log::get().note("star light: sample %llu: star colour %s (%.4f,%.4f,%.4f) | sun draws %u, buffers read %u, positive rows %u, "
                    "with the star's chromaticity %u:%s | other positive rows:%s",
                    static_cast<unsigned long long>(g_sample), haveStar ? "" : "(no star quad drawn: the star off screen)",
                    peak > 0.f ? star[0] / peak : 0.f, peak > 0.f ? star[1] / peak : 0.f, peak > 0.f ? star[2] / peak : 0.f,
                    g_sunDraws, static_cast<unsigned>(got.size()), positive, matched, matches.empty() ? " none" : matches.c_str(),
                    others.empty() ? " none" : others.c_str());
    openFile();
    if (g_file) {
        // Record: 'SLD1', sample, tick, count; then per buffer: kind, draw, slot, offset, bytes, the bytes.
        const uint32_t magic = 0x31444C53u, count = static_cast<uint32_t>(got.size());
        const uint64_t tick = GetTickCount64();
        std::fwrite(&magic, 4, 1, g_file);
        std::fwrite(&g_sample, 8, 1, g_file);
        std::fwrite(&tick, 8, 1, g_file);
        std::fwrite(&count, 4, 1, g_file);
        for (const Got& g : got) {
            const uint32_t head[5] = {g.c->kind, g.c->draw, g.c->slot, g.c->offset, g.c->bytes};
            std::fwrite(head, 4, 5, g_file);
            std::fwrite(g.data.data(), 1, g.data.size(), g_file);
        }
        std::fflush(g_file);
    }
    g_pending.clear();
}

}  // namespace

void starLightDiagDraw(ID3D11DeviceContext* ctx, uint64_t vs, uint64_t ps) {
    if (!ctx) return;
    if (vs == kFlatDeferredHdrVs && ps == kFlatDeferredHdrPs && g_sunDraws < kMaxSunDraws) queue(ctx, kSun, g_sunDraws++, 0, kSlots);
    else if (vs == kFlatStarQuadBVs && ps == kFlatStarQuadBPs && g_starDraws < kMaxStarDraws) queue(ctx, kStar, g_starDraws++, 2, 1);
}

void starLightDiagFrameBoundary(ID3D11DeviceContext* ctx) {
    const uint32_t gen = Config::get().generation();
    if (!g_genRead || gen != g_gen) {
        g_genRead = true;
        g_gen = gen;
        const int v = runtimeFlatProfile() ? Config::get().getIntInRange("diag.star_light_log", 0, 0, 600) : 0;
        if (v != g_interval) {
            g_interval = v;
            g_next = 0;
            if (v) Log::get().note("star light: diag.star_light_log = %d: the sun draw's constants and the star's colour are sampled every %d s (TEMPORARY)", v, v);
            else Log::get().note("star light: diag.star_light_log off");
        }
    }
    g_starLightDiagFrame.store(false, std::memory_order_relaxed);
    if (!ctx) return;
    if (g_inFlight) {
        if (++g_waited >= kWaitFrames) {
            finish(ctx);
            g_sunDraws = g_starDraws = 0;
            g_inFlight = false;
        }
        return;
    }
    if (g_interval <= 0) return;
    const ULONGLONG now = GetTickCount64();
    if (now < g_next) return;
    g_next = now + static_cast<ULONGLONG>(g_interval) * 1000ull;
    g_waited = 0;
    g_inFlight = true;
    ++g_sample;
    g_starLightDiagFrame.store(true, std::memory_order_relaxed);
}

}  // namespace edvr
