// The cockpit HUD's history refusal (flat, 2026-11-22): the HUD panels skip the temporal upscaler's history.
//
// WHY. Since the flat edition dropped fix.ui_quality (30ed0621), the cockpit HUD -- the holo panels and their text, the comms panel,
// the flight HUD, the target sprite and the radar's holograms -- stays in the picture the temporal upscaler (MetalFX, FSR) is handed.
// Those draws write no motion of their own, so each HUD pixel carries the motion of whatever lies behind it: the camera's in a turn,
// a rotating station's when one turns behind the panel. The upscaler then fetches the text's history from where the background came
// from, and the text breaks up while anything behind it moves, lighter or darker with what is underneath. The owner's flight
// (2026-11-22): F7 (anti-aliasing off) cures it -- the text "bright, solid and well defined" --, solid HUD off does not, SS 1.0 only
// improves it. ui_quality avoided it by taking these draws out of the scene into a layer added after the upscale, at about a quarter
// of the frame rate.
//
// WHAT. Each draw of those families is issued once more, just before the game's own issue, into a private R8 mask at the size of the
// target the game draws into: the same vertex stage, input and raster, the game's depth-stencil state made read-only (so it covers
// exactly the fragments the game's draw will, and leaves the depth and stencil the game's draw then tests unchanged), a white pixel
// shader, no blend. The mask is cleared at the first such draw of a frame. The resolver's prep (flat_mono_shader_source.h, debug.w
// bit 64, t19) then refuses the history of every marked pixel, so the finish gives it the plain picture instead of the upscaler's --
// what anti-aliasing off shows there -- while the scene around the HUD keeps the upscaler. The scene seen THROUGH a translucent panel
// takes the plain picture too, inside the panel's area.
//
// The families are the ones ui_quality took (flat_ui_layer.cpp's family table): kHolo, kFlightHud, kSprite and kHoloGeneric. The frosted
// plate behind the panels (vs 2CECEC30 ps A4D03619) stays out: ui_quality left it in the scene too, and it is the blurred scene itself.
// Only a draw into a single render target with no output-merger UAVs is marked; any state that cannot be made leaves the draw alone.
// Owner context only, while flat anti-aliasing is configured. Header-only: vscreen.cpp issues, flat_mono_resolve.cpp binds.
#pragma once

#include <d3d11.h>

#include <cstdint>

#include "../common/config.h"
#include "../common/log.h"
#include "../common/runtime_profile.h"
#include "../common/temporal_mode.h"
#include "binding_shadow.h"
#include "flat_solid_hud.h"          // solid_hud::detail::readOnlyClone, the same read-only depth-stencil clone
#include "flat_ui_layer_math.h"
#include "temporal_shader_bytecode.h"  // kConcourseWhiteBytecode: float4 main() : SV_Target { return 1; }
#include "vscreen.h"

namespace edvr::hud_mask {

inline bool g_on = false;   // flat, fix.temporal_aa configured on; read at the frame boundary

namespace detail {
inline bool g_read = false;
inline uint32_t g_generation = 0;
inline uint64_t g_frame = 1;          // vscreen's frame boundary count
inline uint64_t g_writtenFrame = 0;   // the frame the mask was last cleared and written in
inline ID3D11Texture2D* g_tex = nullptr;
inline ID3D11RenderTargetView* g_rtv = nullptr;
inline ID3D11ShaderResourceView* g_srv = nullptr;
inline UINT g_w = 0, g_h = 0;
inline ID3D11PixelShader* g_white = nullptr;
inline bool g_whiteTried = false;
inline bool g_texFailed = false;
// What begin() replaced, put back by end().
inline ID3D11RenderTargetView* g_savedRtv = nullptr;
inline ID3D11DepthStencilView* g_savedDsv = nullptr;
inline ID3D11PixelShader* g_savedPs = nullptr;
inline ID3D11ClassInstance* g_savedPsInst[256] = {};
inline UINT g_savedPsInstCount = 0;
inline ID3D11BlendState* g_savedBlend = nullptr;
inline FLOAT g_savedFactor[4] = {};
inline UINT g_savedMask = 0xFFFFFFFFu;
inline ID3D11DepthStencilState* g_savedDs = nullptr;
inline UINT g_savedRef = 0;
// The 30 s window's counts.
inline uint64_t g_marked = 0, g_skippedTargets = 0, g_bound = 0, g_sizeMismatch = 0;
inline bool g_firstMarkNoted = false, g_firstBindNoted = false, g_firstMismatchNoted = false;

inline void releaseTexture() {
    if (g_srv) g_srv->Release();
    if (g_rtv) g_rtv->Release();
    if (g_tex) g_tex->Release();
    g_srv = nullptr; g_rtv = nullptr; g_tex = nullptr; g_w = g_h = 0;
}
inline bool ensureTexture(ID3D11Device* dev, UINT w, UINT h) {
    if (g_tex && g_w == w && g_h == h) return true;
    releaseTexture();
    D3D11_TEXTURE2D_DESC d{};
    d.Width = w; d.Height = h; d.MipLevels = 1; d.ArraySize = 1; d.Format = DXGI_FORMAT_R8_UNORM;
    d.SampleDesc.Count = 1; d.Usage = D3D11_USAGE_DEFAULT; d.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    if (FAILED(dev->CreateTexture2D(&d, nullptr, &g_tex)) || FAILED(dev->CreateRenderTargetView(g_tex, nullptr, &g_rtv)) ||
        FAILED(dev->CreateShaderResourceView(g_tex, nullptr, &g_srv))) {
        releaseTexture();
        if (!g_texFailed) {
            g_texFailed = true;
            Log::get().note("hud mask: the %ux%u R8 mask could not be created; the HUD keeps the upscaler's history (said once).", w, h);
        }
        return false;
    }
    g_w = w; g_h = h;
    g_writtenFrame = 0;
    Log::get().note("hud mask: %ux%u R8 mask created (%.1f MB).", w, h, double(w) * h / 1e6);
    return true;
}
inline ID3D11PixelShader* white(ID3D11Device* dev) {
    if (g_white || g_whiteTried) return g_white;
    g_whiteTried = true;
    if (FAILED(dev->CreatePixelShader(kConcourseWhiteBytecode, sizeof(kConcourseWhiteBytecode), nullptr, &g_white))) g_white = nullptr;
    return g_white;
}
}  // namespace detail

// Once a frame (vscreen.cpp's frame boundary): the frame count, the key when the configuration changed, the 30 s line.
inline void frameBoundary() {
    using namespace detail;
    ++g_frame;
    static uint32_t frames = 0;
    if (g_on && ++frames >= 1800) {
        Log::get().note("hud mask 30s: %.1f HUD draws marked a frame; the resolver bound the mask on %llu of %u frames (%llu at another size); "
                        "%llu draws not marked (more than one render target, or output-merger UAVs bound).",
                        static_cast<double>(g_marked) / frames, static_cast<unsigned long long>(g_bound), frames,
                        static_cast<unsigned long long>(g_sizeMismatch), static_cast<unsigned long long>(g_skippedTargets));
        frames = 0;
        g_marked = g_skippedTargets = g_bound = g_sizeMismatch = 0;
    }
    Config& cfg = Config::get();
    const uint32_t generation = cfg.generation();   // read before the value (config.h)
    if (g_read && generation == g_generation) return;
    g_read = true;
    g_generation = generation;
    // requestedTemporalMode, not getString: fix.temporal_aa is not on the flat allow-list (getString answers "off" there), and this
    // honours F7's runtime override, so the comparison's AA off leaves the HUD unmarked too.
    const bool on = runtimeFlatProfile() && temporalModeEnabledFor(cfg.requestedTemporalMode(), true);
    if (on != g_on)
        Log::get().note("hud mask: %s -- the cockpit HUD's pixels %s.", on ? "on" : "off",
                        on ? "skip the temporal upscaler's history (marked as the game draws them, refused in the resolver's prep)"
                           : "are left to the scene's treatment (anti-aliasing is off)");
    g_on = on;
}

// The family test, memoised by pair (a draw's family is a function of its two hashes).
inline bool hudPair(uint64_t vs, uint64_t ps) {
    struct Memo { uint64_t vs = 0, ps = 0; bool hud = false; bool set = false; };
    static Memo memo[256];
    Memo& m = memo[(vs ^ (ps * 0x9E3779B97F4A7C15ull)) >> 56];
    if (m.set && m.vs == vs && m.ps == ps) return m.hud;
    m.vs = vs; m.ps = ps; m.set = true;
    if (flatUiPanelBackingPair(vs, ps)) { m.hud = false; return false; }
    const UiLayerFamily f = flatUiFamilyOf(vs, ps);
    m.hud = f == UiLayerFamily::kHolo || f == UiLayerFamily::kFlightHud || f == UiLayerFamily::kSprite || f == UiLayerFamily::kHoloGeneric;
    return m.hud;
}

// Before the game's issue: true when this draw is a HUD draw and the mask's state is bound (the caller issues the draw once, then end()).
inline bool begin(ID3D11DeviceContext* ctx) {
    using namespace detail;
    if (!hudPair(bindingShaderHash(BindSlot::Vs), bindingShaderHash(BindSlot::Ps))) return false;
    ID3D11RenderTargetView* rtvs[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT] = {};
    ID3D11DepthStencilView* dsv = nullptr;
    ID3D11UnorderedAccessView* uavs[D3D11_1_UAV_SLOT_COUNT] = {};
    ctx->OMGetRenderTargetsAndUnorderedAccessViews(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, rtvs, &dsv, 0, D3D11_PS_CS_UAV_REGISTER_COUNT, uavs);
    uint32_t bound = 0, uavCount = 0;
    for (ID3D11RenderTargetView* r : rtvs) if (r) ++bound;
    for (ID3D11UnorderedAccessView* u : uavs) if (u) { ++uavCount; u->Release(); }
    auto dropViews = [&] {
        for (ID3D11RenderTargetView*& r : rtvs) if (r) { r->Release(); r = nullptr; }
        if (dsv) { dsv->Release(); dsv = nullptr; }
    };
    if (bound != 1 || !rtvs[0] || uavCount) { ++g_skippedTargets; dropViews(); return false; }
    ID3D11Resource* res = nullptr;
    rtvs[0]->GetResource(&res);
    D3D11_TEXTURE2D_DESC td{};
    D3D11_RESOURCE_DIMENSION dim = D3D11_RESOURCE_DIMENSION_UNKNOWN;
    if (res) { res->GetType(&dim); if (dim == D3D11_RESOURCE_DIMENSION_TEXTURE2D) static_cast<ID3D11Texture2D*>(res)->GetDesc(&td); res->Release(); }
    if (dim != D3D11_RESOURCE_DIMENSION_TEXTURE2D || td.SampleDesc.Count != 1) { ++g_skippedTargets; dropViews(); return false; }
    ID3D11Device* dev = nullptr;
    ctx->GetDevice(&dev);
    if (!dev) { dropViews(); return false; }
    ID3D11PixelShader* ps = white(dev);
    const bool tex = ps && ensureTexture(dev, td.Width, td.Height);
    ctx->OMGetDepthStencilState(&g_savedDs, &g_savedRef);
    ID3D11DepthStencilState* ro = tex ? solid_hud::detail::readOnlyClone(dev, g_savedDs) : nullptr;
    dev->Release();
    if (!ro) {
        if (g_savedDs) g_savedDs->Release();
        g_savedDs = nullptr;
        dropViews();
        return false;
    }
    if (g_writtenFrame != g_frame) {
        const FLOAT zero[4] = {0, 0, 0, 0};
        vScreenClearRenderTargetViewRaw(ctx, g_rtv, zero);
        g_writtenFrame = g_frame;
    }
    g_savedRtv = rtvs[0];   // keeps begin's reference until end()
    g_savedDsv = dsv;
    g_savedPsInstCount = 256;
    ctx->PSGetShader(&g_savedPs, g_savedPsInst, &g_savedPsInstCount);
    ctx->OMGetBlendState(&g_savedBlend, g_savedFactor, &g_savedMask);
    vScreenSetRenderTargetsRaw(ctx, 1, &g_rtv, dsv);
    vScreenPSSetShaderRaw(ctx, ps, nullptr, 0);
    vScreenOMSetBlendStateRaw(ctx, nullptr, nullptr, 0xFFFFFFFFu);
    vScreenOMSetDepthStencilStateRaw(ctx, ro, g_savedRef);
    ++g_marked;
    if (!g_firstMarkNoted) {
        g_firstMarkNoted = true;
        Log::get().note("hud mask: the first HUD draw marked (vs %016llX ps %016llX) into the %ux%u mask, the game's target %ux%u format %d; said once.",
                        static_cast<unsigned long long>(bindingShaderHash(BindSlot::Vs)),
                        static_cast<unsigned long long>(bindingShaderHash(BindSlot::Ps)), g_w, g_h, td.Width, td.Height,
                        static_cast<int>(td.Format));
    }
    return true;
}

// After that issue: the game's targets, pixel shader, blend and depth-stencil state back, for the game's own issue.
inline void end(ID3D11DeviceContext* ctx) {
    using namespace detail;
    vScreenSetRenderTargetsRaw(ctx, 1, &g_savedRtv, g_savedDsv);
    vScreenPSSetShaderRaw(ctx, g_savedPs, g_savedPsInstCount ? g_savedPsInst : nullptr, g_savedPsInstCount);
    vScreenOMSetBlendStateRaw(ctx, g_savedBlend, g_savedFactor, g_savedMask);
    vScreenOMSetDepthStencilStateRaw(ctx, g_savedDs, g_savedRef);
    if (g_savedRtv) g_savedRtv->Release();
    if (g_savedDsv) g_savedDsv->Release();
    if (g_savedPs) g_savedPs->Release();
    for (UINT i = 0; i < g_savedPsInstCount; ++i) if (g_savedPsInst[i]) { g_savedPsInst[i]->Release(); g_savedPsInst[i] = nullptr; }
    if (g_savedBlend) g_savedBlend->Release();
    if (g_savedDs) g_savedDs->Release();
    g_savedRtv = nullptr; g_savedDsv = nullptr; g_savedPs = nullptr; g_savedPsInstCount = 0; g_savedBlend = nullptr; g_savedDs = nullptr;
}

// The resolver's prep (flat_mono_resolve.cpp): the mask when this frame marked one at the resolver's render size, else null.
inline ID3D11ShaderResourceView* viewFor(UINT width, UINT height) {
    using namespace detail;
    if (!g_on || !g_srv || g_writtenFrame != g_frame) return nullptr;
    if (g_w != width || g_h != height) {
        ++g_sizeMismatch;
        if (!g_firstMismatchNoted) {
            g_firstMismatchNoted = true;
            Log::get().note("hud mask: the mask is %ux%u but the resolver renders %ux%u; not bound (said once).", g_w, g_h, width, height);
        }
        return nullptr;
    }
    ++g_bound;
    if (!g_firstBindNoted) {
        g_firstBindNoted = true;
        Log::get().note("hud mask: first bound to the resolver's prep at %ux%u: the marked HUD pixels refuse the upscaler's history (said once).",
                        width, height);
    }
    return g_srv;
}

}  // namespace edvr::hud_mask
