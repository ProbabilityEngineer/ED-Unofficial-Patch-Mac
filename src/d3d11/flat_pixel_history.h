// The flat profile's pixel history under the mouse cursor (diagnostic only; 2026-11-21, the nav tag left bright over the comms panel with AA on).
//
// On the census key (hotkey.dump_draws, F6), for the census's first frame only: the point under the mouse cursor, as a fraction of the game
// window's client area, is watched in every colour target a draw writes whose shape is the screen's (aspect within 2% of the client's, at
// least 640 wide) -- the game's scene target H, and, with fix.ui_quality live, the HDR HUD layer the taken draws are bound to instead.
//   * Colour: a 16x16 window around the point is copied GPU-side (into one staging atlas per format) before the first draw into each such
//     target and after every draw into it. At the next frame boundary each copy is diffed against the one before it for the same target, and
//     every draw that changed the window is logged with the census line's own index (DCO 0 #n), its target, and the window's centre texel
//     and mean before and after, decoded to floats.
//   * Depth-stencil: before each draw of that frame whose stencil test reads any bit (read mask not 0), the bound depth-stencil is copied
//     whole and mapped at once (a stall, on that one frame), and the depth and stencil at the point are logged with the draw's test.
//     Capped at kMaxStencilReads a frame.
// Nothing in the frame is changed. With the key unpressed the cost is one load of a false bool per flat draw.
//
// The point is the cursor's when F6 is pressed and the game window is the foreground window; otherwise the screen's centre, and the log says
// which. Header-only, included by vscreen.cpp alone, owner context only.
#pragma once

#include <d3d11.h>
#include <windows.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

#include "../common/log.h"

namespace edvr::flat_pixel_history {

inline bool g_active = false;   // this frame is the probed one

namespace detail {

constexpr uint32_t kWindow = 16;
constexpr uint32_t kMaxTargets = 6;
constexpr uint32_t kMaxDraws = 4096;
constexpr uint32_t kAtlasCols = 64;
constexpr uint32_t kAtlasRows = (kMaxDraws + kMaxTargets + kAtlasCols - 1) / kAtlasCols;
constexpr uint32_t kMaxAtlases = 4;
constexpr uint32_t kMaxLogged = 600;
constexpr uint32_t kMaxStencilReads = 24;

struct Atlas {
    DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
    ID3D11Texture2D* tex = nullptr;
    uint32_t next = 0;   // the next free slot
    uint32_t bpt = 0;
};
struct Target {
    ID3D11Resource* res = nullptr;   // identity only, never dereferenced after the draw
    uint32_t w = 0, h = 0;
    DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
    uint32_t atlas = 0;
    uint32_t baseline = UINT32_MAX;   // the slot copied before its first draw
    uint32_t firstOrdinal = 0;
};
struct Record {
    uint32_t ordinal = 0;   // the census's DCO index for this draw
    uint32_t target = 0;
    uint32_t slot = 0;
    uint64_t vs = 0, ps = 0;
};

inline bool g_done = false;      // taken for the running census
inline float g_nx = 0.5f, g_ny = 0.5f;
inline Atlas g_atlas[kMaxAtlases];
inline Target g_target[kMaxTargets];
inline uint32_t g_targets = 0;
inline std::vector<Record> g_records;
inline float g_clientAspect = 0.0f;
inline uint32_t g_dropped = 0, g_skippedShape = 0, g_stencilReads = 0;
// The draw in flight (between before and after).
inline int g_cur = -1;
inline uint32_t g_curOrdinal = 0;
inline uint64_t g_curVs = 0, g_curPs = 0;
// The one depth-stencil staging copy, kept for the frame (re-made if the bound depth-stencil's shape changes).
inline ID3D11Texture2D* g_dsStage = nullptr;
inline D3D11_TEXTURE2D_DESC g_dsDesc{};

inline uint32_t colourBpt(DXGI_FORMAT f) {
    switch (f) {
        case DXGI_FORMAT_R11G11B10_FLOAT: case DXGI_FORMAT_R8G8B8A8_UNORM: case DXGI_FORMAT_R8G8B8A8_TYPELESS:
        case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB: case DXGI_FORMAT_B8G8R8A8_UNORM: case DXGI_FORMAT_B8G8R8A8_TYPELESS:
        case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB: case DXGI_FORMAT_R10G10B10A2_UNORM: case DXGI_FORMAT_R10G10B10A2_TYPELESS:
            return 4;
        case DXGI_FORMAT_R16G16B16A16_FLOAT: case DXGI_FORMAT_R16G16B16A16_TYPELESS:
            return 8;
        default:
            return 0;
    }
}

inline float halfToFloat(uint16_t h) {
    const uint32_t s = (h >> 15) & 1, e = (h >> 10) & 31, m = h & 1023;
    float v;
    if (e == 0) v = std::ldexp(static_cast<float>(m), -24);
    else if (e == 31) v = m ? NAN : INFINITY;
    else v = std::ldexp(static_cast<float>(m | 1024), static_cast<int>(e) - 25);
    return s ? -v : v;
}
// An unsigned small float of `mb` mantissa bits and a 5-bit exponent (R11G11B10's channels).
inline float smallFloat(uint32_t bits, uint32_t mb) {
    const uint32_t e = bits >> mb, m = bits & ((1u << mb) - 1);
    if (e == 0) return std::ldexp(static_cast<float>(m), -14 - static_cast<int>(mb));
    if (e == 31) return m ? NAN : INFINITY;
    return std::ldexp(static_cast<float>(m | (1u << mb)), static_cast<int>(e) - 15 - static_cast<int>(mb));
}
inline void decode(DXGI_FORMAT f, const uint8_t* p, float out[4]) {
    switch (f) {
        case DXGI_FORMAT_R11G11B10_FLOAT: {
            uint32_t v;
            std::memcpy(&v, p, 4);
            out[0] = smallFloat(v & 0x7FF, 6);
            out[1] = smallFloat((v >> 11) & 0x7FF, 6);
            out[2] = smallFloat((v >> 22) & 0x3FF, 5);
            out[3] = 1.0f;
            return;
        }
        case DXGI_FORMAT_R16G16B16A16_FLOAT: case DXGI_FORMAT_R16G16B16A16_TYPELESS: {
            uint16_t h[4];
            std::memcpy(h, p, 8);
            for (int i = 0; i < 4; ++i) out[i] = halfToFloat(h[i]);
            return;
        }
        case DXGI_FORMAT_R10G10B10A2_UNORM: case DXGI_FORMAT_R10G10B10A2_TYPELESS: {
            uint32_t v;
            std::memcpy(&v, p, 4);
            out[0] = (v & 1023) / 1023.0f;
            out[1] = ((v >> 10) & 1023) / 1023.0f;
            out[2] = ((v >> 20) & 1023) / 1023.0f;
            out[3] = (v >> 30) / 3.0f;
            return;
        }
        case DXGI_FORMAT_B8G8R8A8_UNORM: case DXGI_FORMAT_B8G8R8A8_TYPELESS: case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
            out[0] = p[2] / 255.0f; out[1] = p[1] / 255.0f; out[2] = p[0] / 255.0f; out[3] = p[3] / 255.0f;
            return;
        default:
            for (int i = 0; i < 4; ++i) out[i] = p[i] / 255.0f;
            return;
    }
}

inline void windowOrigin(uint32_t w, uint32_t h, uint32_t& x0, uint32_t& y0) {
    int32_t x = static_cast<int32_t>(g_nx * static_cast<float>(w)) - static_cast<int32_t>(kWindow / 2);
    int32_t y = static_cast<int32_t>(g_ny * static_cast<float>(h)) - static_cast<int32_t>(kWindow / 2);
    const int32_t mx = static_cast<int32_t>(w - kWindow), my = static_cast<int32_t>(h - kWindow);
    x = x < 0 ? 0 : (x > mx ? mx : x);
    y = y < 0 ? 0 : (y > my ? my : y);
    x0 = static_cast<uint32_t>(x);
    y0 = static_cast<uint32_t>(y);
}

inline int atlasFor(ID3D11DeviceContext* ctx, DXGI_FORMAT f) {
    for (uint32_t i = 0; i < kMaxAtlases; ++i)
        if (g_atlas[i].tex && g_atlas[i].format == f) return static_cast<int>(i);
    for (uint32_t i = 0; i < kMaxAtlases; ++i) {
        if (g_atlas[i].tex) continue;
        ID3D11Device* dev = nullptr;
        ctx->GetDevice(&dev);
        if (!dev) return -1;
        D3D11_TEXTURE2D_DESC d{};
        d.Width = kAtlasCols * kWindow;
        d.Height = kAtlasRows * kWindow;
        d.MipLevels = d.ArraySize = 1;
        d.SampleDesc.Count = 1;
        d.Format = f;
        d.Usage = D3D11_USAGE_STAGING;
        d.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        const HRESULT hr = dev->CreateTexture2D(&d, nullptr, &g_atlas[i].tex);
        dev->Release();
        if (FAILED(hr) || !g_atlas[i].tex) { g_atlas[i].tex = nullptr; return -1; }
        g_atlas[i].format = f;
        g_atlas[i].bpt = colourBpt(f);
        g_atlas[i].next = 0;
        return static_cast<int>(i);
    }
    return -1;
}

inline bool copyWindow(ID3D11DeviceContext* ctx, const Target& t, uint32_t& slotOut) {
    Atlas& a = g_atlas[t.atlas];
    if (a.next >= kAtlasCols * kAtlasRows) return false;
    const uint32_t slot = a.next++;
    uint32_t x0, y0;
    windowOrigin(t.w, t.h, x0, y0);
    const D3D11_BOX box{x0, y0, 0, x0 + kWindow, y0 + kWindow, 1};
    ctx->CopySubresourceRegion(a.tex, 0, (slot % kAtlasCols) * kWindow, (slot / kAtlasCols) * kWindow, 0, t.res, 0, &box);
    slotOut = slot;
    return true;
}

// The bound colour target 0 as a watched target: its index, or -1 (not a screen-shaped 2D texture of a format this decodes, or full).
inline int targetOf(ID3D11DeviceContext* ctx, ID3D11RenderTargetView* rtv) {
    if (!rtv) return -1;
    ID3D11Resource* res = nullptr;
    rtv->GetResource(&res);
    if (!res) return -1;
    for (uint32_t i = 0; i < g_targets; ++i)
        if (g_target[i].res == res) { res->Release(); return static_cast<int>(i); }
    ID3D11Texture2D* tex = nullptr;
    D3D11_TEXTURE2D_DESC d{};
    if (SUCCEEDED(res->QueryInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&tex))) && tex) {
        tex->GetDesc(&d);
        tex->Release();
    }
    res->Release();   // identity only from here
    if (!d.Width || d.ArraySize != 1 || d.SampleDesc.Count != 1 || d.Width < 640 || d.Height < kWindow || !colourBpt(d.Format)) {
        ++g_skippedShape;
        return -1;
    }
    const float aspect = static_cast<float>(d.Width) / static_cast<float>(d.Height);
    if (g_clientAspect > 0.0f && std::fabs(aspect / g_clientAspect - 1.0f) > 0.02f) { ++g_skippedShape; return -1; }
    if (g_targets >= kMaxTargets) { ++g_dropped; return -1; }
    const int a = atlasFor(ctx, d.Format);
    if (a < 0) { ++g_dropped; return -1; }
    Target& t = g_target[g_targets];
    t = Target{};
    t.res = res;
    t.w = d.Width;
    t.h = d.Height;
    t.format = d.Format;
    t.atlas = static_cast<uint32_t>(a);
    t.firstOrdinal = g_curOrdinal;
    uint32_t slot = 0;
    if (copyWindow(ctx, t, slot)) t.baseline = slot;
    return static_cast<int>(g_targets++);
}

// The bound depth-stencil at the point, for a draw whose stencil test reads: copied whole, mapped at once.
inline void stencilRead(ID3D11DeviceContext* ctx, ID3D11DepthStencilView* dsv) {
    ID3D11DepthStencilState* st = nullptr;
    UINT ref = 0;
    ctx->OMGetDepthStencilState(&st, &ref);
    D3D11_DEPTH_STENCIL_DESC sd{};
    if (st) { st->GetDesc(&sd); st->Release(); }
    if (!st || !sd.StencilEnable || !sd.StencilReadMask) return;
    if (g_stencilReads >= kMaxStencilReads) { ++g_stencilReads; return; }
    ++g_stencilReads;
    const char* why = nullptr;
    ID3D11Resource* res = nullptr;
    if (dsv) dsv->GetResource(&res);
    ID3D11Texture2D* tex = nullptr;
    if (res) { res->QueryInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&tex)); res->Release(); }
    D3D11_TEXTURE2D_DESC d{};
    if (tex) tex->GetDesc(&d);
    float depth = 0.0f;
    uint32_t stencil = 0, px = 0, py = 0;
    if (!tex) why = "no depth-stencil bound";
    else if (d.SampleDesc.Count != 1 || d.ArraySize != 1) why = "a multisampled or array depth-stencil";
    else {
        if (g_dsStage && (g_dsDesc.Width != d.Width || g_dsDesc.Height != d.Height || g_dsDesc.Format != d.Format)) {
            g_dsStage->Release();
            g_dsStage = nullptr;
        }
        if (!g_dsStage) {
            D3D11_TEXTURE2D_DESC s = d;
            s.MipLevels = 1;
            s.Usage = D3D11_USAGE_STAGING;
            s.BindFlags = 0;
            s.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            s.MiscFlags = 0;
            ID3D11Device* dev = nullptr;
            ctx->GetDevice(&dev);
            if (dev) {
                if (FAILED(dev->CreateTexture2D(&s, nullptr, &g_dsStage))) g_dsStage = nullptr;
                dev->Release();
            }
            g_dsDesc = d;
        }
        if (!g_dsStage) why = "the staging copy could not be made";
        else {
            ctx->CopyResource(g_dsStage, tex);
            D3D11_MAPPED_SUBRESOURCE m{};
            if (FAILED(ctx->Map(g_dsStage, 0, D3D11_MAP_READ, 0, &m)) || !m.pData) why = "the staging copy could not be mapped";
            else {
                px = static_cast<uint32_t>(g_nx * static_cast<float>(d.Width));
                py = static_cast<uint32_t>(g_ny * static_cast<float>(d.Height));
                if (px >= d.Width) px = d.Width - 1;
                if (py >= d.Height) py = d.Height - 1;
                const uint8_t* row = static_cast<const uint8_t*>(m.pData) + static_cast<size_t>(py) * m.RowPitch;
                switch (d.Format) {
                    case DXGI_FORMAT_R32G8X24_TYPELESS: case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:
                        std::memcpy(&depth, row + px * 8, 4);
                        stencil = row[px * 8 + 4];
                        break;
                    case DXGI_FORMAT_R24G8_TYPELESS: case DXGI_FORMAT_D24_UNORM_S8_UINT: {
                        uint32_t v;
                        std::memcpy(&v, row + px * 4, 4);
                        depth = (v & 0xFFFFFF) / 16777215.0f;
                        stencil = v >> 24;
                        break;
                    }
                    default:
                        why = "a depth-stencil format this does not read";
                }
                ctx->Unmap(g_dsStage, 0);
            }
        }
    }
    if (tex) tex->Release();
    if (why)
        Log::get().note("flat pixel history: stencil before DCO 0 #%u vs %016llX ps %016llX: not read (%s).", g_curOrdinal,
                        static_cast<unsigned long long>(g_curVs), static_cast<unsigned long long>(g_curPs), why);
    else
        Log::get().note("flat pixel history: stencil before DCO 0 #%u vs %016llX ps %016llX: depth-stencil %ux%u f%u at (%u,%u): depth %.6f "
                        "stencil 0x%02X; the draw tests (stencil & 0x%02X) func %u against ref 0x%02X (%s), writes mask 0x%02X; depth %s func %u.",
                        g_curOrdinal, static_cast<unsigned long long>(g_curVs), static_cast<unsigned long long>(g_curPs), d.Width, d.Height,
                        static_cast<unsigned>(d.Format), px, py, static_cast<double>(depth), stencil, sd.StencilReadMask,
                        static_cast<unsigned>(sd.FrontFace.StencilFunc), ref & 0xFF,
                        sd.FrontFace.StencilFunc == D3D11_COMPARISON_EQUAL
                            ? (((stencil & sd.StencilReadMask) == (ref & sd.StencilReadMask)) ? "passes here" : "FAILS here")
                            : "see func", sd.StencilWriteMask, sd.DepthEnable ? "on" : "off", static_cast<unsigned>(sd.DepthFunc));
}

inline void releaseAll() {
    for (Atlas& a : g_atlas) {
        if (a.tex) a.tex->Release();
        a = Atlas{};
    }
    if (g_dsStage) g_dsStage->Release();
    g_dsStage = nullptr;
    g_records.clear();
    g_targets = 0;
    g_cur = -1;
}

inline void meanAndCentre(const D3D11_MAPPED_SUBRESOURCE& m, const Atlas& a, uint32_t slot, float mean[4], float centre[4]) {
    const uint32_t sx = (slot % kAtlasCols) * kWindow, sy = (slot / kAtlasCols) * kWindow;
    for (int i = 0; i < 4; ++i) mean[i] = 0.0f;
    for (uint32_t y = 0; y < kWindow; ++y) {
        const uint8_t* row = static_cast<const uint8_t*>(m.pData) + static_cast<size_t>(sy + y) * m.RowPitch;
        for (uint32_t x = 0; x < kWindow; ++x) {
            float v[4];
            decode(a.format, row + (sx + x) * a.bpt, v);
            for (int i = 0; i < 4; ++i) mean[i] += v[i];
            if (x == kWindow / 2 && y == kWindow / 2)
                for (int i = 0; i < 4; ++i) centre[i] = v[i];
        }
    }
    for (int i = 0; i < 4; ++i) mean[i] /= static_cast<float>(kWindow * kWindow);
}

inline uint32_t changedTexels(const D3D11_MAPPED_SUBRESOURCE& m, const Atlas& a, uint32_t s0, uint32_t s1) {
    uint32_t n = 0;
    for (uint32_t y = 0; y < kWindow; ++y) {
        const uint8_t* r0 = static_cast<const uint8_t*>(m.pData) + static_cast<size_t>((s0 / kAtlasCols) * kWindow + y) * m.RowPitch +
                            (s0 % kAtlasCols) * kWindow * a.bpt;
        const uint8_t* r1 = static_cast<const uint8_t*>(m.pData) + static_cast<size_t>((s1 / kAtlasCols) * kWindow + y) * m.RowPitch +
                            (s1 % kAtlasCols) * kWindow * a.bpt;
        for (uint32_t x = 0; x < kWindow; ++x)
            if (std::memcmp(r0 + x * a.bpt, r1 + x * a.bpt, a.bpt)) ++n;
    }
    return n;
}

inline void report(ID3D11DeviceContext* ctx) {
    D3D11_MAPPED_SUBRESOURCE maps[kMaxAtlases]{};
    bool mapped[kMaxAtlases]{};
    for (uint32_t i = 0; i < kMaxAtlases; ++i)
        if (g_atlas[i].tex) mapped[i] = SUCCEEDED(ctx->Map(g_atlas[i].tex, 0, D3D11_MAP_READ, 0, &maps[i])) && maps[i].pData;
    for (uint32_t i = 0; i < g_targets; ++i) {
        const Target& t = g_target[i];
        float mean[4]{}, centre[4]{};
        if (mapped[t.atlas] && t.baseline != UINT32_MAX) meanAndCentre(maps[t.atlas], g_atlas[t.atlas], t.baseline, mean, centre);
        Log::get().note("flat pixel history: target %u is %ux%u f%u (res %p), first drawn by DCO 0 #%u; before it the centre texel was "
                        "(%.4f %.4f %.4f %.4f), the 16x16 mean (%.4f %.4f %.4f %.4f).",
                        i, t.w, t.h, static_cast<unsigned>(t.format), static_cast<void*>(t.res), t.firstOrdinal, centre[0], centre[1], centre[2],
                        centre[3], mean[0], mean[1], mean[2], mean[3]);
    }
    uint32_t last[kMaxTargets];
    for (uint32_t i = 0; i < kMaxTargets; ++i) last[i] = g_target[i].baseline;
    uint32_t logged = 0, changed = 0;
    for (const Record& r : g_records) {
        const Target& t = g_target[r.target];
        const uint32_t prev = last[r.target];
        last[r.target] = r.slot;
        if (prev == UINT32_MAX || !mapped[t.atlas]) continue;
        const uint32_t n = changedTexels(maps[t.atlas], g_atlas[t.atlas], prev, r.slot);
        if (!n) continue;
        ++changed;
        if (logged >= kMaxLogged) continue;
        ++logged;
        float m0[4], c0[4], m1[4], c1[4];
        meanAndCentre(maps[t.atlas], g_atlas[t.atlas], prev, m0, c0);
        meanAndCentre(maps[t.atlas], g_atlas[t.atlas], r.slot, m1, c1);
        Log::get().note("flat pixel history: DCO 0 #%u vs %016llX ps %016llX target %u changed %u/256: centre (%.4f %.4f %.4f %.4f) -> "
                        "(%.4f %.4f %.4f %.4f), mean (%.4f %.4f %.4f %.4f) -> (%.4f %.4f %.4f %.4f).",
                        r.ordinal, static_cast<unsigned long long>(r.vs), static_cast<unsigned long long>(r.ps), r.target, n, c0[0], c0[1],
                        c0[2], c0[3], c1[0], c1[1], c1[2], c1[3], m0[0], m0[1], m0[2], m0[3], m1[0], m1[1], m1[2], m1[3]);
    }
    for (uint32_t i = 0; i < kMaxAtlases; ++i)
        if (mapped[i]) ctx->Unmap(g_atlas[i].tex, 0);
    Log::get().note("flat pixel history: done -- %u target(s) watched, %zu draw(s) into them, %u changed the window (%u logged); "
                    "%u draw target(s) not screen-shaped or not decodable, %u dropped (full); %u stencil read(s)%s.",
                    g_targets, g_records.size(), changed, logged, g_skippedShape, g_dropped,
                    g_stencilReads > kMaxStencilReads ? kMaxStencilReads : g_stencilReads,
                    g_stencilReads > kMaxStencilReads ? " (capped)" : "");
}

}  // namespace detail

// The frame edge, BEFORE the census advances: closes and reports the probed frame.
inline void frameEnd(ID3D11DeviceContext* ctx) {
    if (!g_active) return;
    g_active = false;
    if (ctx) detail::report(ctx);
    detail::releaseAll();
}

// The frame edge, AFTER the census advanced: arms the census's first frame once per census. `censusFrame0` is true when the census has
// just started (its first frame is the one about to run); `censusArmed` false lets the next census arm it again.
inline void frameStart(bool censusFrame0, bool censusArmed) {
    using namespace detail;
    if (!censusArmed) { g_done = false; return; }
    if (g_done || !censusFrame0) return;
    g_done = true;
    g_records.clear();
    g_records.reserve(1024);
    g_targets = 0;
    g_dropped = g_skippedShape = g_stencilReads = 0;
    g_cur = -1;
    // The point: the cursor over the foreground window's client area, as a fraction of it.
    const char* where = "the screen's centre (the cursor is not over the game window's client area)";
    g_nx = g_ny = 0.5f;
    g_clientAspect = 0.0f;
    POINT pt{};
    RECT rc{};
    HWND fg = GetForegroundWindow();
    if (fg && GetClientRect(fg, &rc) && rc.right > 0 && rc.bottom > 0) {
        g_clientAspect = static_cast<float>(rc.right) / static_cast<float>(rc.bottom);
        if (GetCursorPos(&pt) && ScreenToClient(fg, &pt) && pt.x >= 0 && pt.y >= 0 && pt.x < rc.right && pt.y < rc.bottom) {
            g_nx = (static_cast<float>(pt.x) + 0.5f) / static_cast<float>(rc.right);
            g_ny = (static_cast<float>(pt.y) + 0.5f) / static_cast<float>(rc.bottom);
            where = "the cursor";
        }
    }
    Log::get().note("flat pixel history: armed for the census's first frame at %s: client (%ld,%ld) of %ldx%ld, the point (%.4f, %.4f) of "
                    "each screen-shaped target; 16x16 window, depth-stencil read before each stencil-testing draw (up to %u).",
                    where, pt.x, pt.y, rc.right, rc.bottom, static_cast<double>(g_nx), static_cast<double>(g_ny), kMaxStencilReads);
    g_active = true;
}

// Before the real draw, with the bindings it will use (after the flat runtime's begin: a taken draw's are the layer's).
inline void before(ID3D11DeviceContext* ctx, uint32_t censusOrdinal, uint64_t vs, uint64_t ps) {
    using namespace detail;
    g_cur = -1;
    g_curOrdinal = censusOrdinal;
    g_curVs = vs;
    g_curPs = ps;
    ID3D11RenderTargetView* rtv = nullptr;
    ID3D11DepthStencilView* dsv = nullptr;
    ctx->OMGetRenderTargets(1, &rtv, &dsv);
    stencilRead(ctx, dsv);
    g_cur = targetOf(ctx, rtv);
    if (rtv) rtv->Release();
    if (dsv) dsv->Release();
}

// After the real draw, before the flat runtime's end.
inline void after(ID3D11DeviceContext* ctx) {
    using namespace detail;
    if (g_cur < 0) return;
    const Target& t = g_target[g_cur];
    if (g_records.size() >= kMaxDraws) { ++g_dropped; g_cur = -1; return; }
    Record r;
    r.ordinal = g_curOrdinal;
    r.target = static_cast<uint32_t>(g_cur);
    r.vs = g_curVs;
    r.ps = g_curPs;
    if (copyWindow(ctx, t, r.slot)) g_records.push_back(r);
    else ++g_dropped;
    g_cur = -1;
}

}  // namespace edvr::flat_pixel_history
