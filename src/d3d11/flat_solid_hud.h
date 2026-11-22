// visual.solid_hud (flat, 2026-11-22): the cockpit's holo panels let less of the scene through.
//
// REVISED THE SAME DAY: the first cut darkened the frosted plates (vs 2CECEC30 ps A4D03619) once more. Flown (edvr_gfx_20261122_043503):
// patchy, shadow-like, strongest where the scene behind is lighter -- the plate's coverage follows the blurred scene it samples, so
// squaring it squares that pattern -- and nothing behind DISEMBARK, where F6 showed the plate changing 64 of 256 texels and the holo panel
// draw (vs 81216C77 ps A2965EC2) taking the scene from 0.336 to 0.109. The removed fix.ui_quality never squared the plate (it stayed in
// the scene); its gamma blend raised the HOLO PANELS' transmittance to about the 2.2th power. So the draws darkened twice are the holo
// panel family (flat_ui_layer_math.h flatUiFamilyOf == kHolo: the panels and the comms panel's glass), not the plate. The paragraphs
// below were written for the plate; the mechanism is the same.
//
// WHY. The plates behind the cockpit's panel text (the station's launch menu, the comms panel, the other panels' label plates) are
// one draw each, vs 2CECEC3065EF0D4A ps A4D03619D631B186 (flat_ui_layer_math.h kFlatUiPanelBacking*): premultiplied over, so the scene
// behind keeps (1 - a) of its light. F6's pixel history under the station's AUTO LAUNCH plate (edvr_gfx_20261122_034112, 03:53:05):
// 0.205 -> 0.075, about 37% let through; under the comms panel (03:53:20) 0.048 -> 0.0038, about 8%. That is the game's look (the GPTK
// reference). The removed fix.ui_quality showed those plates darker -- its gamma blend squared roughly what they let through -- and the
// owner liked that; this gives it on purpose, as a choice, at the cost of one extra draw per plate (up to about 15 a frame).
//
// WHAT. Right before the game's own issue of a plate draw, the same draw is issued once more with a darken-only blend -- colour ZERO,
// INV_SRC_ALPHA: the target times (1 - a), the plate's own light not added -- and with the game's depth-stencil state made read-only
// (depth and stencil writes off, the tests as the game set them), so it covers exactly the pixels the game's draw will and leaves the
// depth and stencil the game's draw then tests unchanged. The game's draw follows as always: the scene behind the plate keeps
// (1 - a)^2 of its light, the plate's tint is the game's. Only a draw into a single render target is touched (a second target, such as
// a motion target, would be darkened too). Any state that cannot be made leaves the draw alone. Owner context only, header-only,
// included by vscreen.cpp alone.
#pragma once

#include <d3d11.h>

#include <cstdint>

#include "../common/config.h"
#include "../common/log.h"
#include "../common/runtime_profile.h"
#include "binding_shadow.h"
#include "flat_ui_layer_math.h"
#include "vscreen.h"

namespace edvr::solid_hud {

inline bool g_on = false;   // visual.solid_hud, read at the frame boundary; one load per draw

namespace detail {
inline bool g_read = false;
inline uint32_t g_generation = 0;
inline ID3D11BlendState* g_dim = nullptr;
inline bool g_dimTried = false;
struct DsClone {
    ID3D11DepthStencilState* game = nullptr;   // identity only
    ID3D11DepthStencilState* mine = nullptr;
};
constexpr int kClones = 8;
inline DsClone g_clones[kClones];
inline int g_next = 0;
// What begin() replaced, put back by end().
inline ID3D11BlendState* g_savedBlend = nullptr;
inline FLOAT g_savedFactor[4] = {};
inline UINT g_savedMask = 0xFFFFFFFFu;
inline ID3D11DepthStencilState* g_savedDs = nullptr;
inline UINT g_savedRef = 0;
inline uint64_t g_issued = 0;
inline bool g_firstNoted = false;

inline ID3D11DepthStencilState* readOnlyClone(ID3D11Device* dev, ID3D11DepthStencilState* game) {
    for (const DsClone& c : g_clones)
        if (c.mine && c.game == game) return c.mine;
    D3D11_DEPTH_STENCIL_DESC d{};
    if (game) {
        game->GetDesc(&d);
    } else {   // the default state
        d.DepthEnable = TRUE;
        d.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
        d.DepthFunc = D3D11_COMPARISON_LESS;
        d.StencilEnable = FALSE;
        d.StencilReadMask = D3D11_DEFAULT_STENCIL_READ_MASK;
        d.StencilWriteMask = D3D11_DEFAULT_STENCIL_WRITE_MASK;
        d.FrontFace.StencilFunc = d.BackFace.StencilFunc = D3D11_COMPARISON_ALWAYS;
        d.FrontFace.StencilFailOp = d.FrontFace.StencilDepthFailOp = d.FrontFace.StencilPassOp = D3D11_STENCIL_OP_KEEP;
        d.BackFace = d.FrontFace;
    }
    d.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
    d.StencilWriteMask = 0;
    ID3D11DepthStencilState* made = nullptr;
    if (FAILED(dev->CreateDepthStencilState(&d, &made)) || !made) return nullptr;
    DsClone& slot = g_clones[g_next];
    g_next = (g_next + 1) % kClones;
    if (slot.mine) slot.mine->Release();
    slot.game = game;
    slot.mine = made;
    return made;
}

inline ID3D11BlendState* dimBlend(ID3D11Device* dev) {
    if (g_dim || g_dimTried) return g_dim;
    g_dimTried = true;
    D3D11_BLEND_DESC bd{};
    bd.RenderTarget[0].BlendEnable = TRUE;
    bd.RenderTarget[0].SrcBlend = D3D11_BLEND_ZERO;
    bd.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    bd.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    bd.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ZERO;
    bd.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ONE;
    bd.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    bd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    if (FAILED(dev->CreateBlendState(&bd, &g_dim))) g_dim = nullptr;
    return g_dim;
}
}  // namespace detail

// Once a frame (vscreen.cpp's frame boundary): the key, when the configuration changed. Flat only.
inline void frameBoundary() {
    // The cost, while on: the extra draws issued, every 900 frames.
    static uint32_t frames = 0;
    static uint64_t issuedAtWindow = 0;
    if (g_on && ++frames >= 900) {
        Log::get().note("solid hud: %llu extra panel draws in the last %u frames (%.1f a frame).",
                        static_cast<unsigned long long>(detail::g_issued - issuedAtWindow), frames,
                        static_cast<double>(detail::g_issued - issuedAtWindow) / frames);
        frames = 0;
        issuedAtWindow = detail::g_issued;
    }
    Config& cfg = Config::get();
    const uint32_t generation = cfg.generation();   // read before the value (config.h)
    if (detail::g_read && generation == detail::g_generation) return;
    detail::g_read = true;
    detail::g_generation = generation;
    const bool on = runtimeFlatProfile() && cfg.getBool("visual.solid_hud", false);
    if (on != g_on)
        Log::get().note("solid hud: %s -- the cockpit's holo panels %s.", on ? "on" : "off",
                        on ? "are darkened once more before the game draws them, so the scene behind keeps (1 - a)^2 of its light"
                           : "are drawn as the game draws them");
    g_on = on;
}

// Before the game's issue: true when this draw is a plate and the darken-only state is bound (the caller issues the draw once, then end()).
// The family test, memoised by pair (a draw's family is a function of its two hashes; flatUiFamilyOf walks the shared rule).
inline bool panelPair(uint64_t vs, uint64_t ps) {
    struct Memo { uint64_t vs = 0, ps = 0; bool panel = false; bool set = false; };
    static Memo memo[256];
    Memo& m = memo[(vs ^ (ps * 0x9E3779B97F4A7C15ull)) >> 56];
    if (m.set && m.vs == vs && m.ps == ps) return m.panel;
    m.vs = vs;
    m.ps = ps;
    m.set = true;
    m.panel = !flatUiPanelBackingPair(vs, ps) && flatUiFamilyOf(vs, ps) == UiLayerFamily::kHolo;
    return m.panel;
}

inline bool begin(ID3D11DeviceContext* ctx) {
    using namespace detail;
    if (!panelPair(bindingShaderHash(BindSlot::Vs), bindingShaderHash(BindSlot::Ps))) return false;
    ID3D11RenderTargetView* rtvs[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT] = {};
    ctx->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, rtvs, nullptr);
    uint32_t bound = 0;
    for (ID3D11RenderTargetView*& r : rtvs)
        if (r) {
            ++bound;
            r->Release();
        }
    if (bound != 1 || !rtvs[0]) return false;
    ID3D11Device* dev = nullptr;
    ctx->GetDevice(&dev);
    if (!dev) return false;
    ID3D11BlendState* dim = dimBlend(dev);
    ctx->OMGetBlendState(&g_savedBlend, g_savedFactor, &g_savedMask);
    ctx->OMGetDepthStencilState(&g_savedDs, &g_savedRef);
    ID3D11DepthStencilState* ro = dim ? readOnlyClone(dev, g_savedDs) : nullptr;
    dev->Release();
    if (!dim || !ro) {
        if (g_savedBlend) g_savedBlend->Release();
        if (g_savedDs) g_savedDs->Release();
        g_savedBlend = nullptr;
        g_savedDs = nullptr;
        return false;
    }
    vScreenOMSetBlendStateRaw(ctx, dim, g_savedFactor, g_savedMask);
    vScreenOMSetDepthStencilStateRaw(ctx, ro, g_savedRef);
    ++g_issued;
    if (!g_firstNoted) {
        g_firstNoted = true;
        Log::get().note("solid hud: the first holo panel draw darkened once more (vs %016llX ps %016llX, depth-stencil state %p made read-only); "
                        "said once.", static_cast<unsigned long long>(bindingShaderHash(BindSlot::Vs)),
                        static_cast<unsigned long long>(bindingShaderHash(BindSlot::Ps)), static_cast<void*>(g_savedDs));
    }
    return true;
}

// After that issue: the game's blend and depth-stencil state back, for the game's own issue.
inline void end(ID3D11DeviceContext* ctx) {
    using namespace detail;
    vScreenOMSetBlendStateRaw(ctx, g_savedBlend, g_savedFactor, g_savedMask);
    vScreenOMSetDepthStencilStateRaw(ctx, g_savedDs, g_savedRef);
    if (g_savedBlend) g_savedBlend->Release();
    if (g_savedDs) g_savedDs->Release();
    g_savedBlend = nullptr;
    g_savedDs = nullptr;
}

}  // namespace edvr::solid_hud
