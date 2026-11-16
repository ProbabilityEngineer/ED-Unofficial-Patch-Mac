// TEMPORARY: diag.star_light_log. flat_star_light_diag.h says what and why.
#include "flat_star_light_diag.h"

#include "flat_compute_readback.h"   // FlatComputeInternalScope: EDVR's own copies pass the hooks straight through
#include "flat_deferred_camera.h"    // kFlatDeferredHdrVs/Ps: the sun pair
#include "flat_star_glow_math.h"     // kFlatStarQuadBVs/Ps: the star quad whose PS b2[0].yzw is the star's colour
#include "flat_star_light.h"         // g_starLightGameLight: the game's light before visual.star_light_colour

#include "../common/config.h"
#include "../common/hotkey.h"
#include "../common/log.h"
#include "../common/runtime_profile.h"

#include <windows.h>
#include <d3d11_1.h>
#include <wrl/client.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_map>
#include <unordered_set>
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
// The game's light as the diagnostic knows it: the fix's pre-fix copy while the fix runs; while it is off (0%, the hooks idle and that
// copy frozen), the last sample's sun row 40, which is then the game's own.
float g_sampledLight[3] = {0, 0, 0};
float g_sampledStar[3] = {0, 0, 0};    // the last star disc colour a sample saw (the star quad's b2[0].yzw)
const float* gameLight() { return starLightLive() ? g_starLightGameLight : g_sampledLight; }

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
        if (finite3(row + 1) && row[1] + row[2] + row[3] > 0.f) { std::memcpy(star, row + 1, 12); haveStar = true; std::memcpy(g_sampledStar, star, 12); break; }
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
            if (g.c->offset / 16u + r == 40u && finite3(v)) std::memcpy(g_sampledLight, v, sizeof(g_sampledLight));
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
    const float* game = gameLight();
    const float gr = game[0] > 0.f ? game[0] : 1.f;
    Log::get().note("star light: sample %llu: game light before the fix (%.1f,%.1f,%.1f) chroma (1,%.3f,%.3f) | star colour %s (%.4f,%.4f,%.4f) | sun draws %u, buffers read %u, positive rows %u, "
                    "with the star's chromaticity %u:%s | other positive rows:%s",
                    static_cast<unsigned long long>(g_sample), game[0], game[1], game[2], game[1] / gr, game[2] / gr,
                    haveStar ? "" : "(no star quad drawn: the star off screen)",
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

// The copy scan (2026-11-16): one frame's draws, every VS and PS constant buffer (slots 0..13) and every buffer bound as a shader resource
// (VS t0..15, PS t0..31: structured light lists, the tiled lights' buffers), each resource copied once, searched for three colours: the game's
// light now, the system's full tint (a held copy, like b1 row 26, can carry the star's colour while the light has faded) and the star's
// disc colour. Floats at every 4 bytes and half floats at every 2. Each match names the resource, the byte and the draws (shader pair,
// draw index in the frame) that read it. The same frame lists the cube maps the draws read (the ambient and reflection probes), so the
// draw that shows the ring haze can be matched to one or the other. The compute passes are not seen: the proxy has no Dispatch hook.
constexpr unsigned kScanCbSlots = 14, kScanVsSrv = 16, kScanPsSrv = 32;
constexpr uint32_t kScanCbBytes = 65536, kScanSrvBytes = 4u << 20;
constexpr uint64_t kScanBudget = 96ull << 20;
constexpr unsigned kScanMaxResources = 1500;
struct Reader { uint64_t vs, ps; uint32_t draw; uint8_t stage, kind, slot; };   // stage 0 VS 1 PS; kind 0 cbuffer 1 srv buffer 2 cube
struct ScanRes {
    const void* source = nullptr;
    uint32_t offset = 0, bytes = 0;     // the copied window of the resource
    uint8_t kind = 0;
    std::vector<Reader> readers;        // the first 4
    unsigned readerCount = 0;
    ComPtr<ID3D11Buffer> staging;
    uint32_t cubeSize = 0, cubeMips = 0, cubeFormat = 0, cubeArray = 0;
};
std::vector<ScanRes> g_scan;
std::unordered_map<uint64_t, size_t> g_scanIndex;   // (resource, window offset) -> g_scan
uint64_t g_scanBytes = 0;
bool g_scanFrame = false, g_scanInFlight = false;
unsigned g_scanWaited = 0, g_scanDraws = 0, g_scanSkipped = 0;
float g_scanTarget[3] = {1, 1, 1};
float g_scanTargets[3][3] = {};
bool g_scanHave[3] = {};
ULONGLONG g_nextScan = 0;      // the periodic scan (the ring may not be in view on arrival)
const char* g_scanWhy = "";
bool g_whiteNoted = false;
const char* const kTargetName[3] = {"light now", "system tint", "star disc"};

void addReader(ScanRes& r, uint64_t vs, uint64_t ps, uint8_t stage, uint8_t kind, uint8_t slot) {
    if (r.readers.size() < 4) r.readers.push_back(Reader{vs, ps, g_scanDraws, stage, kind, slot});
    ++r.readerCount;
}

// The resource's window, copied once per scan; later draws only add themselves as readers.
void scanBuffer(ID3D11DeviceContext* ctx, ID3D11Device* dev, ID3D11Buffer* b, uint32_t offset, uint32_t bytes, uint8_t kind, uint64_t vs,
                uint64_t ps, uint8_t stage, uint8_t slot) {
    const uint64_t key = reinterpret_cast<uintptr_t>(b) * 31ull + offset;
    auto it = g_scanIndex.find(key);
    if (it != g_scanIndex.end()) { addReader(g_scan[it->second], vs, ps, stage, kind, slot); return; }
    D3D11_BUFFER_DESC d{};
    b->GetDesc(&d);
    if (offset >= d.ByteWidth || d.Usage == D3D11_USAGE_STAGING) return;
    if (offset + bytes > d.ByteWidth) bytes = d.ByteWidth - offset;
    const uint32_t cap = kind == 0 ? kScanCbBytes : kScanSrvBytes;
    if (bytes > cap) bytes = cap;
    if (g_scan.size() >= kScanMaxResources || g_scanBytes + bytes > kScanBudget) { ++g_scanSkipped; return; }
    D3D11_BUFFER_DESC sd{};
    sd.ByteWidth = (bytes + 15u) & ~15u;
    sd.Usage = D3D11_USAGE_STAGING;
    sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ScanRes r;
    r.source = b; r.offset = offset; r.bytes = bytes; r.kind = kind;
    if (FAILED(dev->CreateBuffer(&sd, nullptr, &r.staging))) return;
    const D3D11_BOX box{offset, 0, 0, offset + bytes, 1, 1};
    ctx->CopySubresourceRegion(r.staging.Get(), 0, 0, 0, 0, b, 0, &box);
    g_scanBytes += bytes;
    addReader(r, vs, ps, stage, kind, slot);
    g_scanIndex.emplace(key, g_scan.size());
    g_scan.push_back(std::move(r));
}

void scanStage(ID3D11DeviceContext* ctx, uint64_t vs, uint64_t ps, uint8_t stage) {
    ComPtr<ID3D11DeviceContext1> c1;
    ctx->QueryInterface(IID_PPV_ARGS(&c1));
    ComPtr<ID3D11Device> dev;
    ctx->GetDevice(&dev);
    if (!dev) return;
    FlatComputeInternalScope internal;
    ID3D11Buffer* cbs[kScanCbSlots] = {};
    UINT first[kScanCbSlots] = {}, num[kScanCbSlots] = {};
    if (stage == 0) { if (c1) c1->VSGetConstantBuffers1(0, kScanCbSlots, cbs, first, num); else ctx->VSGetConstantBuffers(0, kScanCbSlots, cbs); }
    else            { if (c1) c1->PSGetConstantBuffers1(0, kScanCbSlots, cbs, first, num); else ctx->PSGetConstantBuffers(0, kScanCbSlots, cbs); }
    for (uint32_t i = 0; i < kScanCbSlots; ++i) {
        if (!cbs[i]) continue;
        const uint32_t offset = c1 ? first[i] * 16u : 0u, bytes = (c1 && num[i]) ? num[i] * 16u : kScanCbBytes;
        scanBuffer(ctx, dev.Get(), cbs[i], offset, bytes, 0, vs, ps, stage, static_cast<uint8_t>(i));
        cbs[i]->Release();
    }
    ID3D11ShaderResourceView* srvs[kScanPsSrv] = {};
    const unsigned n = stage == 0 ? kScanVsSrv : kScanPsSrv;
    if (stage == 0) ctx->VSGetShaderResources(0, n, srvs); else ctx->PSGetShaderResources(0, n, srvs);
    for (unsigned i = 0; i < n; ++i) {
        ID3D11ShaderResourceView* v = srvs[i];
        if (!v) continue;
        D3D11_SHADER_RESOURCE_VIEW_DESC vd{};
        v->GetDesc(&vd);
        ComPtr<ID3D11Resource> res;
        v->GetResource(&res);
        if (res && (vd.ViewDimension == D3D11_SRV_DIMENSION_BUFFER || vd.ViewDimension == D3D11_SRV_DIMENSION_BUFFEREX)) {
            ComPtr<ID3D11Buffer> b;
            if (SUCCEEDED(res.As(&b))) scanBuffer(ctx, dev.Get(), b.Get(), 0, kScanSrvBytes, 1, vs, ps, stage, static_cast<uint8_t>(i));
        } else if (res && (vd.ViewDimension == D3D11_SRV_DIMENSION_TEXTURECUBE || vd.ViewDimension == D3D11_SRV_DIMENSION_TEXTURECUBEARRAY)) {
            const uint64_t key = reinterpret_cast<uintptr_t>(res.Get()) * 31ull + 0xC0BEull;
            auto it = g_scanIndex.find(key);
            if (it != g_scanIndex.end()) { addReader(g_scan[it->second], vs, ps, stage, 2, static_cast<uint8_t>(i)); }
            else {
                ScanRes r;
                r.source = res.Get(); r.kind = 2;
                ComPtr<ID3D11Texture2D> t;
                if (SUCCEEDED(res.As(&t))) {
                    D3D11_TEXTURE2D_DESC td{};
                    t->GetDesc(&td);
                    r.cubeSize = td.Width; r.cubeMips = td.MipLevels; r.cubeFormat = td.Format; r.cubeArray = td.ArraySize / 6u;
                }
                addReader(r, vs, ps, stage, 2, static_cast<uint8_t>(i));
                g_scanIndex.emplace(key, g_scan.size());
                g_scan.push_back(std::move(r));
            }
        }
        v->Release();
    }
}

float halfToFloat(uint16_t h) {
    const uint32_t sign = (h >> 15) & 1u, exp = (h >> 10) & 0x1Fu, man = h & 0x3FFu;
    float f;
    if (exp == 0) f = std::ldexp(static_cast<float>(man), -24);
    else if (exp == 31) f = man ? NAN : INFINITY;
    else f = std::ldexp(static_cast<float>(man | 0x400u), static_cast<int>(exp) - 25);
    return sign ? -f : f;
}

// Which target (0..2) the colour v has, within tol of its chroma (peak-normalised), or -1.
int matchTarget(const float v[3], float tol) {
    if (!finite3(v) || !(v[0] > 0.f && v[1] > 0.f && v[2] > 0.f)) return -1;
    const float vp = std::fmax(v[0], std::fmax(v[1], v[2]));
    if (!(vp > 1e-4f)) return -1;
    for (int t = 0; t < 3; ++t) {
        if (!g_scanHave[t]) continue;
        const float* c = g_scanTargets[t];
        const float cp = std::fmax(c[0], std::fmax(c[1], c[2]));
        float d = 0.f;
        for (int k = 0; k < 3; ++k) d = std::fmax(d, std::fabs(v[k] / vp - c[k] / cp));
        if (d < tol) return t;
    }
    return -1;
}

const char* stageName(uint8_t s) { return s ? "PS" : "VS"; }
const char* kindName(uint8_t k) { return k == 0 ? "cb" : k == 1 ? "t(buffer)" : "t(cube)"; }

std::string readersText(const ScanRes& r) {
    std::string out;
    char item[96];
    for (const Reader& d : r.readers) {
        std::snprintf(item, sizeof(item), "%sdraw %u %016llX/%016llX %s %s%u", out.empty() ? "" : ", ", d.draw, static_cast<unsigned long long>(d.vs),
                      static_cast<unsigned long long>(d.ps), stageName(d.stage), d.kind == 0 ? "b" : "t", d.slot);
        out += item;
    }
    if (r.readerCount > r.readers.size()) out += ", ...";
    return out;
}

void finishScan(ID3D11DeviceContext* ctx) {
    // Per resource: the matches of each target, floats exact (chroma within 0.002: a copy of the same numbers) and close (0.02), halves (0.004).
    struct Hit { size_t res; uint32_t byte; int target; bool half; float v[3]; };
    std::vector<Hit> hits;
    struct Count { unsigned exact = 0, close = 0, half = 0; };
    std::vector<Count> counts(g_scan.size());
    unsigned mapped = 0, cubes = 0;
    for (size_t ri = 0; ri < g_scan.size(); ++ri) {
        ScanRes& r = g_scan[ri];
        if (r.kind == 2) { ++cubes; continue; }
        D3D11_MAPPED_SUBRESOURCE m{};
        if (FAILED(ctx->Map(r.staging.Get(), 0, D3D11_MAP_READ, 0, &m)) || !m.pData) continue;
        ++mapped;
        std::vector<uint8_t> bytes(r.bytes);
        std::memcpy(bytes.data(), m.pData, r.bytes);
        ctx->Unmap(r.staging.Get(), 0);
        for (uint32_t o = 0; o + 12u <= r.bytes; o += 4u) {
            float v[3];
            std::memcpy(v, bytes.data() + o, 12);
            int t = matchTarget(v, 0.002f);
            const bool exact = t >= 0;
            if (!exact) t = matchTarget(v, 0.02f);
            if (t < 0) continue;
            if (exact) ++counts[ri].exact; else ++counts[ri].close;
            if (hits.size() < 20000) hits.push_back(Hit{ri, r.offset + o, t, false, {v[0], v[1], v[2]}});
        }
        for (uint32_t o = 0; o + 6u <= r.bytes; o += 2u) {
            uint16_t h[3];
            std::memcpy(h, bytes.data() + o, 6);
            const float v[3] = {halfToFloat(h[0]), halfToFloat(h[1]), halfToFloat(h[2])};
            if (!(v[0] > 1e-3f && v[1] > 1e-3f && v[2] > 1e-3f)) continue;
            const int t = matchTarget(v, 0.004f);
            if (t < 0) continue;
            ++counts[ri].half;
            if (hits.size() < 20000) hits.push_back(Hit{ri, r.offset + o, t, true, {v[0], v[1], v[2]}});
        }
    }
    for (int t = 0; t < 3; ++t)
        if (g_scanHave[t])
            Log::get().note("star light scan: target %d (%s) = (%.3f,%.3f,%.3f)", t, kTargetName[t], g_scanTargets[t][0], g_scanTargets[t][1], g_scanTargets[t][2]);
    // The resources with matches, most-read first. A resource with hundreds of scattered matches is noise (vertex-like data); a light copy is a few.
    std::vector<size_t> withHits;
    for (size_t ri = 0; ri < g_scan.size(); ++ri) if (counts[ri].exact + counts[ri].close + counts[ri].half) withHits.push_back(ri);
    std::sort(withHits.begin(), withHits.end(), [](size_t a, size_t b) { return g_scan[a].readerCount > g_scan[b].readerCount; });
    unsigned logged = 0;
    for (size_t ri : withHits) {
        if (logged >= 40) break;
        ++logged;
        const ScanRes& r = g_scan[ri];
        const Count& c = counts[ri];
        std::string where;
        unsigned shown = 0;
        for (const Hit& h : hits) {
            if (h.res != ri || shown >= 4) continue;
            char item[160];
            std::snprintf(item, sizeof(item), "%s%s byte %u%s = (%.5g,%.5g,%.5g)", shown ? "; " : "", kTargetName[h.target], h.byte, h.half ? " half" : "",
                          h.v[0], h.v[1], h.v[2]);
            where += item;
            ++shown;
        }
        Log::get().note("star light scan: %s %p (%u bytes copied): %u exact, %u close, %u half matches: %s | read by %u: %s", kindName(r.kind), r.source,
                        r.bytes, c.exact, c.close, c.half, where.c_str(), r.readerCount, readersText(r).c_str());
    }
    // The cube maps, most-read first.
    std::vector<size_t> cubeList;
    for (size_t ri = 0; ri < g_scan.size(); ++ri) if (g_scan[ri].kind == 2) cubeList.push_back(ri);
    std::sort(cubeList.begin(), cubeList.end(), [](size_t a, size_t b) { return g_scan[a].readerCount > g_scan[b].readerCount; });
    unsigned cubeLogged = 0;
    for (size_t ri : cubeList) {
        if (cubeLogged >= 20) break;
        ++cubeLogged;
        const ScanRes& r = g_scan[ri];
        Log::get().note("star light scan: cube %p %ux%u, %u mips, format %u, %u cube(s) | read by %u: %s", r.source, r.cubeSize, r.cubeSize, r.cubeMips,
                        r.cubeFormat, r.cubeArray, r.readerCount, readersText(r).c_str());
    }
    Log::get().note("star light scan (%s): %u draws; %u buffers copied (%u mapped, %.1f MB, %u not copied: budget), %u cube maps; %u resources with matches "
                    "(%u logged), %u cubes logged",
                    g_scanWhy, g_scanDraws, static_cast<unsigned>(g_scan.size()) - cubes, mapped, g_scanBytes / 1048576.0, g_scanSkipped, cubes,
                    static_cast<unsigned>(withHits.size()), logged, cubeLogged);
    g_scan.clear();
    g_scanIndex.clear();
    g_scanBytes = 0;
}

}  // namespace

// ---- The haze hunt (flat_star_light_diag.h) ----
namespace {
enum class Hunt { Off, Collect, Step, Done };
Hunt g_hunt = Hunt::Off;
constexpr unsigned kHuntCollectFrames = 3;
unsigned g_huntFrames = 0;
bool g_huntSunSeen = false;
std::vector<std::pair<uint64_t, uint64_t>> g_huntCand;   // (vs, ps), in the order first drawn
std::unordered_set<uint64_t> g_huntCandSet, g_huntHidden;
size_t g_huntLo = 0, g_huntHi = 0;
unsigned g_huntStep = 0;
bool g_huntKeysMade = false;
Hotkey g_keyGone, g_keyThere, g_keyRestart;

uint64_t huntKey(uint64_t vs, uint64_t ps) { return vs * 0x9E3779B97F4A7C15ull ^ (ps + 0x632BE59BD9B4E019ull); }

void huntHide() {
    g_huntHidden.clear();
    if (g_hunt == Hunt::Done) {
        g_huntHidden.insert(huntKey(g_huntCand[g_huntLo].first, g_huntCand[g_huntLo].second));
        return;
    }
    const size_t mid = (g_huntLo + g_huntHi) / 2;
    for (size_t i = g_huntLo; i < mid; ++i) g_huntHidden.insert(huntKey(g_huntCand[i].first, g_huntCand[i].second));
    std::string list;
    char item[48];
    for (size_t i = g_huntLo; i < mid && i < g_huntLo + 6; ++i) {
        std::snprintf(item, sizeof(item), "%s%016llX/%016llX", list.empty() ? "" : ", ", static_cast<unsigned long long>(g_huntCand[i].first),
                      static_cast<unsigned long long>(g_huntCand[i].second));
        list += item;
    }
    Log::get().note("haze hunt: step %u: hiding candidates %zu..%zu (%zu of the %zu left): %s%s. Look at the haze: Ctrl+F9 if it is gone, Ctrl+F11 if it is still there",
                    g_huntStep, g_huntLo, mid - 1, mid - g_huntLo, g_huntHi - g_huntLo, list.c_str(), mid - g_huntLo > 6 ? ", ..." : "");
}

void huntStart() {
    g_hunt = Hunt::Collect;
    g_huntFrames = 0;
    g_huntCand.clear();
    g_huntCandSet.clear();
    g_huntHidden.clear();
    g_huntStep = 0;
    Log::get().note("haze hunt: collecting the shader pairs drawn after the sun's lighting pass for %u frames (keep the haze in view)", kHuntCollectFrames);
}

void huntFrameBoundary() {
    const bool want = runtimeFlatProfile() && Config::get().getBool("diag.haze_hunt", false);
    if (!want) {
        if (g_hunt != Hunt::Off) Log::get().note("haze hunt: off; nothing hidden");
        g_hunt = Hunt::Off;
        g_huntHidden.clear();
        g_hazeHuntActive.store(false, std::memory_order_relaxed);
        return;
    }
    if (!g_huntKeysMade) {
        g_huntKeysMade = true;
        g_keyGone.setKey(VK_F9, kHotkeyCtrl);
        g_keyThere.setKey(VK_F11, kHotkeyCtrl);
        g_keyRestart.setKey(VK_F12, kHotkeyCtrl);
    }
    g_hazeHuntActive.store(true, std::memory_order_relaxed);
    const bool gone = g_keyGone.pressed(), there = g_keyThere.pressed(), restart = g_keyRestart.pressed();
    if (g_hunt == Hunt::Off || restart) { huntStart(); return; }
    if (g_hunt == Hunt::Collect) {
        g_huntSunSeen = false;
        if (++g_huntFrames < kHuntCollectFrames) return;
        if (g_huntCand.size() < 2) {
            Log::get().note("haze hunt: only %zu candidate pair(s) drawn after the lighting pass; collecting again", g_huntCand.size());
            huntStart();
            return;
        }
        Log::get().note("haze hunt: %zu candidate pairs (about %u steps)", g_huntCand.size(),
                        static_cast<unsigned>(std::ceil(std::log2(static_cast<double>(g_huntCand.size())))));
        g_hunt = Hunt::Step;
        g_huntLo = 0;
        g_huntHi = g_huntCand.size();
        g_huntStep = 1;
        huntHide();
        return;
    }
    if (!gone && !there) return;
    if (g_hunt == Hunt::Done) {
        Log::get().note("haze hunt: confirmation: with only %016llX/%016llX hidden the haze is %s", static_cast<unsigned long long>(g_huntCand[g_huntLo].first),
                        static_cast<unsigned long long>(g_huntCand[g_huntLo].second), gone ? "GONE" : "still there");
        return;
    }
    const size_t mid = (g_huntLo + g_huntHi) / 2;
    Log::get().note("haze hunt: step %u answer: %s", g_huntStep, gone ? "gone (it is among the hidden)" : "still there (it is among the shown)");
    if (gone) g_huntHi = mid; else g_huntLo = mid;
    ++g_huntStep;
    if (g_huntHi - g_huntLo <= 1) {
        g_hunt = Hunt::Done;
        Log::get().note("haze hunt: RESULT: %016llX/%016llX (candidate %zu); it alone is hidden now: Ctrl+F9 if the haze is gone, Ctrl+F11 if not",
                        static_cast<unsigned long long>(g_huntCand[g_huntLo].first), static_cast<unsigned long long>(g_huntCand[g_huntLo].second), g_huntLo);
    }
    huntHide();
}
}  // namespace

std::atomic<bool> g_hazeHuntActive{false};

void hazeHuntDraw(uint64_t vs, uint64_t ps) {
    if (g_hunt != Hunt::Collect) return;
    if (vs == kFlatDeferredHdrVs && ps == kFlatDeferredHdrPs) { g_huntSunSeen = true; return; }
    if (!g_huntSunSeen || ps == 0) return;
    if (g_huntCandSet.insert(huntKey(vs, ps)).second) g_huntCand.emplace_back(vs, ps);
}

bool hazeHuntHides(uint64_t vs, uint64_t ps) {
    return !g_huntHidden.empty() && g_huntHidden.count(huntKey(vs, ps)) != 0;
}

void starLightDiagDraw(ID3D11DeviceContext* ctx, uint64_t vs, uint64_t ps) {
    if (!ctx) return;
    if (g_scanFrame) {
        scanStage(ctx, vs, ps, 0);
        scanStage(ctx, vs, ps, 1);
        ++g_scanDraws;
        return;
    }
    if (vs == kFlatDeferredHdrVs && ps == kFlatDeferredHdrPs && g_sunDraws < kMaxSunDraws) queue(ctx, kSun, g_sunDraws++, 0, kSlots);
    else if (vs == kFlatStarQuadBVs && ps == kFlatStarQuadBPs && g_starDraws < kMaxStarDraws) queue(ctx, kStar, g_starDraws++, 2, 1);
}

void starLightDiagFrameBoundary(ID3D11DeviceContext* ctx) {
    huntFrameBoundary();
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
    if (g_scanFrame) { g_scanFrame = false; g_scanInFlight = true; g_scanWaited = 0; }
    if (g_scanInFlight) {
        if (++g_scanWaited >= kWaitFrames) { finishScan(ctx); g_scanInFlight = false; }
        return;
    }
    bool scan = false;
    if (g_interval > 0 && !g_inFlight) {
        const bool arrival = starLightTakeScanRequest(g_scanTarget);
        if (arrival || GetTickCount64() >= g_nextScan) {
            // On arrival and every 30 s: the three colours (finishScan), each only while it is clearly tinted: a near-white colour
            // matches too many unrelated numbers to name anything.
            g_nextScan = GetTickCount64() + 30000;
            const float* g = gameLight();
            float cand[3][3] = {{g[0], g[1], g[2]}, {0, 0, 0}, {g_sampledStar[0], g_sampledStar[1], g_sampledStar[2]}};
            if (arrival) std::memcpy(cand[0], g_scanTarget, sizeof(cand[0]));
            starLightSystemTint(cand[1]);
            unsigned have = 0;
            for (int t = 0; t < 3; ++t) {
                g_scanHave[t] = false;
                const float* c = cand[t];
                if (!(c[0] > 0.f) || !finite3(c)) continue;
                const float dg = c[1] / c[0] - 0.969f, db = c[2] / c[0] - 0.973f;
                if (std::sqrt(dg * dg + db * db) < 0.1f) continue;
                for (int k = 0; k < 3; ++k) g_scanTargets[t][k] = c[k] / c[0];
                g_scanHave[t] = true;
                ++have;
            }
            if (have) { scan = true; g_scanWhy = arrival ? "arrival" : "periodic"; }
            else if (!g_whiteNoted) {
                g_whiteNoted = true;
                Log::get().note("star light scan: the light, the system tint and the star disc are all near-white or unknown; the scan waits");
            }
        }
    }
    if (scan) {
        g_scanFrame = true;
        g_scanDraws = g_scanSkipped = 0;
        g_scanBytes = 0;
        g_starLightDiagFrame.store(true, std::memory_order_relaxed);
        return;
    }
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
