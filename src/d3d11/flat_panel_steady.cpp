// fix.steady_panels' flat mechanism: flat_panel_steady.h says what and why.
#include "flat_panel_steady.h"

#include "draw_state_describe.h"    // shapeOf: the bound blend as the UI layer reads it
#include "flat_compute_readback.h"  // FlatComputeInternalScope
#include "ui_layer_math.h"          // UiLayerFamily, uiLayerConvertBlend
#include "ui_layer_shaders.h"       // uiLayerBlendDesc, kUiLayerClear
#include "vscreen.h"                // the raw OM entry points: the shadow keeps describing the game's bindings

#include "../common/config.h"
#include "../common/guard.h"
#include "../common/log.h"
#include "../common/runtime_profile.h"

#include <windows.h>
#include <d3d11.h>
#include <wrl/client.h>

#include <atomic>
#include <cstring>

namespace edvr {

namespace {

template <class T>
using Ptr = Microsoft::WRL::ComPtr<T>;

constexpr UINT kTargets = D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT;
constexpr size_t kCloneSlots = 16;
constexpr size_t kBlendSlots = 16;

std::atomic<bool> g_on{false};
bool g_configRead = false;
uint32_t g_configGeneration = 0;
bool g_wasOn = false;

// P: the panels' light (rgb) and transmittance (alpha), at H's size.
struct Layer {
    Ptr<ID3D11Texture2D> tex;
    Ptr<ID3D11RenderTargetView> rtv;
    Ptr<ID3D11ShaderResourceView> srv;
    uint32_t w = 0, h = 0;
};
Layer g_layer;

// S: H as the tone draw read it (the copy route only), H's own format and size.
struct Snapshot {
    Ptr<ID3D11Texture2D> tex;
    Ptr<ID3D11ShaderResourceView> srv;
    uint32_t w = 0, h = 0;
    DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
};
Snapshot g_snapshot;
bool g_snapped = false;   // this frame's S was taken

// This frame: P cleared (its first mark), P handed to the prep (later marks are late), P could not be made (not asked again this frame).
bool g_marked = false, g_consumed = false, g_layerFailed = false;
// The tick of the HDR route's last ask (flatPanelSteadyView), for flatPanelSteadyRouteLive.
ULONGLONG g_routeTick = 0;

// The 30 s window.
struct Window {
    uint64_t frames = 0, markedFrames = 0, draws = 0;
    uint64_t refusedShape = 0, refusedLayer = 0, refusedDepth = 0, refusedLate = 0, refusedSize = 0, refusedFault = 0, refusedTarget = 0;
    uint64_t resolvesBound = 0, resolvesRefused = 0, viewNone = 0, viewSize = 0;
    uint64_t copyBound = 0, copyRefused = 0, snapshots = 0, snapUnmarked = 0, snapRefused = 0;
    uint32_t w = 0, h = 0;
};
Window g_win;
ULONGLONG g_windowStart = 0;
FaultBudget g_budget("panelSteady.reissue", 4);

// The game's depth-stencil states, each with a clone whose writes are off (flat_star_glow.cpp's rule): the re-issue tests depth as the
// game's draw did and writes nothing.
struct CloneSlot {
    Ptr<ID3D11DepthStencilState> source, clone;
    bool used = false, trivial = false;
};
CloneSlot g_clones[kCloneSlots];
size_t g_cloneNext = 0;

struct BlendSlot {
    D3D11_BLEND_DESC desc{};
    Ptr<ID3D11BlendState> state;
};
BlendSlot g_blends[kBlendSlots];
size_t g_blendCount = 0;

struct Saved {
    Ptr<ID3D11RenderTargetView> rtvs[kTargets];
    Ptr<ID3D11DepthStencilView> dsv;
    Ptr<ID3D11DepthStencilState> ds;
    UINT stencilRef = 0;
    Ptr<ID3D11BlendState> blend;
    FLOAT factor[4] = {};
    UINT sampleMask = 0xFFFFFFFFu;
    bool active = false;
};
Saved g_saved;

void releaseSaved() { g_saved = Saved{}; }

bool ensureLayer(ID3D11DeviceContext* ctx, uint32_t w, uint32_t h) {
    if (g_layer.tex && g_layer.w == w && g_layer.h == h) return true;
    g_layer = Layer{};
    Ptr<ID3D11Device> dev;
    ctx->GetDevice(&dev);
    if (!dev || !w || !h) return false;
    D3D11_TEXTURE2D_DESC d{};
    d.Width = w;
    d.Height = h;
    d.MipLevels = 1;
    d.ArraySize = 1;
    d.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    d.SampleDesc.Count = 1;
    d.Usage = D3D11_USAGE_DEFAULT;
    d.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    Layer made;
    if (FAILED(dev->CreateTexture2D(&d, nullptr, &made.tex)) || !made.tex || FAILED(dev->CreateRenderTargetView(made.tex.Get(), nullptr, &made.rtv)) ||
        !made.rtv || FAILED(dev->CreateShaderResourceView(made.tex.Get(), nullptr, &made.srv)) || !made.srv)
        return false;
    made.w = w;
    made.h = h;
    g_layer = made;
    return true;
}

bool replayDepthState(ID3D11DeviceContext* ctx, ID3D11DepthStencilState* game, ID3D11DepthStencilState** out) {
    for (const CloneSlot& c : g_clones) {
        if (c.used && c.source.Get() == game) {
            *out = c.trivial ? game : c.clone.Get();
            return true;
        }
    }
    D3D11_DEPTH_STENCIL_DESC desc{};
    if (game) {
        game->GetDesc(&desc);
    } else {   // the API's default state
        desc.DepthEnable = TRUE;
        desc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
        desc.DepthFunc = D3D11_COMPARISON_LESS;
        desc.StencilReadMask = D3D11_DEFAULT_STENCIL_READ_MASK;
        desc.StencilWriteMask = D3D11_DEFAULT_STENCIL_WRITE_MASK;
    }
    CloneSlot& slot = g_clones[g_cloneNext];
    g_cloneNext = (g_cloneNext + 1) % kCloneSlots;
    slot = CloneSlot{};
    slot.source = game;
    const bool writesDepth = desc.DepthEnable && desc.DepthWriteMask != D3D11_DEPTH_WRITE_MASK_ZERO;
    const bool writesStencil = desc.StencilEnable && desc.StencilWriteMask != 0;
    if (!writesDepth && !writesStencil) {
        slot.trivial = true;
        slot.used = true;
        *out = game;
        return true;
    }
    desc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
    desc.StencilWriteMask = 0;
    Ptr<ID3D11Device> dev;
    ctx->GetDevice(&dev);
    if (!dev || FAILED(dev->CreateDepthStencilState(&desc, &slot.clone)) || !slot.clone) {
        slot = CloneSlot{};
        return false;
    }
    slot.used = true;
    *out = slot.clone.Get();
    return true;
}

ID3D11BlendState* convertedBlend(ID3D11DeviceContext* ctx, ID3D11BlendState* game) {
    UiBlendRt rt, conv;
    if (shapeOf(game, &rt) == UiBlendShape::kRefused || !uiLayerConvertBlend(rt, &conv)) return nullptr;
    const D3D11_BLEND_DESC want = uiLayerBlendDesc(conv);
    for (size_t i = 0; i < g_blendCount; ++i)
        if (std::memcmp(&g_blends[i].desc, &want, sizeof(want)) == 0) return g_blends[i].state.Get();
    if (g_blendCount >= kBlendSlots) return nullptr;
    Ptr<ID3D11Device> dev;
    ctx->GetDevice(&dev);
    Ptr<ID3D11BlendState> state;
    if (!dev || FAILED(dev->CreateBlendState(&want, &state)) || !state) return nullptr;
    g_blends[g_blendCount].desc = want;
    g_blends[g_blendCount].state = state;
    return g_blends[g_blendCount++].state.Get();
}

bool restoreSaved(ID3D11DeviceContext* ctx) {
    return guarded("panelSteady.restore", [&] {
        ID3D11RenderTargetView* raw[kTargets] = {};
        UINT n = 0;
        for (UINT i = 0; i < kTargets; ++i) {
            raw[i] = g_saved.rtvs[i].Get();
            if (raw[i]) n = i + 1;
        }
        vScreenSetRenderTargetsRaw(ctx, n, raw, g_saved.dsv.Get());
        vScreenOMSetDepthStencilStateRaw(ctx, g_saved.ds.Get(), g_saved.stencilRef);
        vScreenOMSetBlendStateRaw(ctx, g_saved.blend.Get(), g_saved.factor, g_saved.sampleMask);
    });
}

enum class Why { kNone, kShape, kLayer, kDepth, kTarget };

bool beginReplay(ID3D11DeviceContext* ctx, uint32_t w, uint32_t h, Why& why) {
    why = Why::kLayer;
    if (!ensureLayer(ctx, w, h)) {
        g_layerFailed = true;
        return false;
    }
    ID3D11RenderTargetView* raw[kTargets] = {};
    ID3D11DepthStencilView* dsv = nullptr;
    ctx->OMGetRenderTargets(kTargets, raw, &dsv);
    for (UINT i = 0; i < kTargets; ++i) g_saved.rtvs[i].Attach(raw[i]);
    g_saved.dsv.Attach(dsv);
    UINT ref = 0;
    ID3D11DepthStencilState* ds = nullptr;
    ctx->OMGetDepthStencilState(&ds, &ref);
    g_saved.ds.Attach(ds);
    g_saved.stencilRef = ref;
    ID3D11BlendState* bs = nullptr;
    ctx->OMGetBlendState(&bs, g_saved.factor, &g_saved.sampleMask);
    g_saved.blend.Attach(bs);
    why = Why::kTarget;
    if (!g_saved.rtvs[0]) return false;
    why = Why::kShape;
    ID3D11BlendState* blend = convertedBlend(ctx, g_saved.blend.Get());
    if (!blend) return false;
    why = Why::kDepth;
    ID3D11DepthStencilState* replay = nullptr;
    if (!replayDepthState(ctx, g_saved.ds.Get(), &replay)) return false;
    if (!g_marked) {   // the frame's first mark
        vScreenClearRenderTargetViewRaw(ctx, g_layer.rtv.Get(), kUiLayerClear);
        g_marked = true;
        ++g_win.markedFrames;
    }
    ID3D11RenderTargetView* p = g_layer.rtv.Get();
    g_saved.active = true;   // from the first call that changes the game's bindings
    vScreenSetRenderTargetsRaw(ctx, 1, &p, g_saved.dsv.Get());
    vScreenOMSetDepthStencilStateRaw(ctx, replay, ref);
    vScreenOMSetBlendStateRaw(ctx, blend, g_saved.factor, g_saved.sampleMask);
    why = Why::kNone;
    return true;
}

void logWindow() {
    const Window& w = g_win;
    Log::get().note("flat steady panels: frames=%llu marked-frames=%llu draws=%llu refused shape=%llu layer=%llu depth=%llu target=%llu late=%llu "
                    "size=%llu fault=%llu; resolves bound=%llu refused=%llu (none=%llu size=%llu), of them below SS 1.0 (the copy route) bound=%llu "
                    "refused=%llu; tone snapshots=%llu unmarked=%llu refused=%llu; layer %ux%u. A bound resolve gave the panels' pixels zero motion "
                    "(fix.steady_panels).",
                    (unsigned long long)w.frames, (unsigned long long)w.markedFrames, (unsigned long long)w.draws,
                    (unsigned long long)w.refusedShape, (unsigned long long)w.refusedLayer, (unsigned long long)w.refusedDepth,
                    (unsigned long long)w.refusedTarget, (unsigned long long)w.refusedLate, (unsigned long long)w.refusedSize,
                    (unsigned long long)w.refusedFault, (unsigned long long)w.resolvesBound, (unsigned long long)w.resolvesRefused,
                    (unsigned long long)w.viewNone, (unsigned long long)w.viewSize, (unsigned long long)w.copyBound,
                    (unsigned long long)w.copyRefused, (unsigned long long)w.snapshots, (unsigned long long)w.snapUnmarked,
                    (unsigned long long)w.snapRefused, w.w, w.h);
}

}  // namespace

bool flatPanelSteadyOn() { return g_on.load(std::memory_order_relaxed); }

bool flatPanelSteadyRouteLive() { return g_routeTick && GetTickCount64() - g_routeTick < 1000; }

bool flatPanelSteadyFamily(UiLayerFamily f) {
    return f == UiLayerFamily::kHolo || f == UiLayerFamily::kFlightHud || f == UiLayerFamily::kSprite || f == UiLayerFamily::kHoloGeneric;
}

void flatPanelSteadyFrame() {
    if (!runtimeFlatProfile()) return;
    Config& cfg = Config::get();
    const uint32_t generation = cfg.generation();
    if (!g_configRead || generation != g_configGeneration) {
        g_configRead = true;
        g_configGeneration = generation;
        g_on.store(cfg.getBool("fix.steady_panels", false), std::memory_order_relaxed);
    }
    const bool on = g_on.load(std::memory_order_relaxed);
    const ULONGLONG now = GetTickCount64();
    if (on && !g_wasOn) {
        g_win = Window{};
        g_windowStart = now;
        Log::get().note("flat steady panels: on (fix.steady_panels): the cockpit panels stay in the scene, and the upscaler is handed zero motion for "
                        "their pixels.");
    } else if (!on && g_wasOn) {
        Log::get().note("flat steady panels: off (fix.steady_panels); its layer and snapshot are released.");
        g_layer = Layer{};
        g_snapshot = Snapshot{};
    }
    g_wasOn = on;
    g_marked = g_consumed = g_layerFailed = g_snapped = false;
    if (!on) {
        g_routeTick = 0;
        return;
    }
    ++g_win.frames;
    if (now - g_windowStart >= 30000) {
        logWindow();
        g_win = Window{};
        g_windowStart = now;
    }
}

bool flatPanelSteadyBegin(ID3D11DeviceContext* ctx, uint32_t width, uint32_t height) {
    if (!ctx || g_saved.active || !flatPanelSteadyOn()) return false;
    if (g_consumed) {
        ++g_win.refusedLate;
        return false;
    }
    if (g_marked && (g_layer.w != width || g_layer.h != height)) {   // one size a frame
        ++g_win.refusedSize;
        return false;
    }
    if (g_layerFailed) {
        ++g_win.refusedLayer;
        return false;
    }
    FlatComputeInternalScope internal;
    Why why = Why::kLayer;
    bool ok = false;
    const bool ran = guardedBudget(g_budget, [&] { ok = beginReplay(ctx, width, height, why); });
    if (!ran || !ok) {
        if (!ran) ++g_win.refusedFault;
        else if (why == Why::kShape) ++g_win.refusedShape;
        else if (why == Why::kDepth) ++g_win.refusedDepth;
        else if (why == Why::kTarget) ++g_win.refusedTarget;
        else ++g_win.refusedLayer;
        if (g_saved.active) restoreSaved(ctx);
        releaseSaved();
        return false;
    }
    ++g_win.draws;
    g_win.w = width;
    g_win.h = height;
    return true;
}

void flatPanelSteadyEnd(ID3D11DeviceContext* ctx) {
    if (!g_saved.active) return;
    FlatComputeInternalScope internal;
    if (!restoreSaved(ctx)) ++g_win.refusedFault;
    releaseSaved();
}

ID3D11ShaderResourceView* flatPanelSteadyView(uint32_t width, uint32_t height) {
    if (!flatPanelSteadyOn()) return nullptr;
    g_consumed = true;
    g_routeTick = GetTickCount64();
    if (!g_marked || !g_layer.srv) {
        ++g_win.viewNone;
        return nullptr;
    }
    if (g_layer.w != width || g_layer.h != height) {
        ++g_win.viewSize;
        return nullptr;
    }
    return g_layer.srv.Get();
}

void flatPanelSteadyNoteResolve(bool bound, bool copyRoute) {
    if (bound) ++g_win.resolvesBound;
    else ++g_win.resolvesRefused;
    if (copyRoute) {
        if (bound) ++g_win.copyBound;
        else ++g_win.copyRefused;
    }
}

bool flatPanelSteadySnapshot(ID3D11DeviceContext* ctx, void* scene) {
    if (!ctx || !scene || !flatPanelSteadyOn() || g_snapped || g_consumed) return false;
    if (!g_marked || !g_layer.tex) {
        ++g_win.snapUnmarked;
        return false;
    }
    FlatComputeInternalScope internal;
    bool ok = false;
    const bool ran = guarded("panelSteady.snapshot", [&] {
        Ptr<ID3D11Texture2D> h;
        if (FAILED(static_cast<ID3D11Resource*>(scene)->QueryInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void**>(h.GetAddressOf()))) || !h) return;
        D3D11_TEXTURE2D_DESC d{};
        h->GetDesc(&d);
        // P's size, one plain typed level: the prep reads S at P's texel.
        if (d.Width != g_layer.w || d.Height != g_layer.h || d.MipLevels != 1 || d.ArraySize != 1 || d.SampleDesc.Count != 1 ||
            (d.Format != DXGI_FORMAT_R11G11B10_FLOAT && d.Format != DXGI_FORMAT_R16G16B16A16_FLOAT))
            return;
        if (!g_snapshot.tex || g_snapshot.w != d.Width || g_snapshot.h != d.Height || g_snapshot.format != d.Format) {
            g_snapshot = Snapshot{};
            Ptr<ID3D11Device> dev;
            ctx->GetDevice(&dev);
            D3D11_TEXTURE2D_DESC m{};
            m.Width = d.Width;
            m.Height = d.Height;
            m.MipLevels = 1;
            m.ArraySize = 1;
            m.Format = d.Format;
            m.SampleDesc.Count = 1;
            m.Usage = D3D11_USAGE_DEFAULT;
            m.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            Snapshot made;
            if (!dev || FAILED(dev->CreateTexture2D(&m, nullptr, &made.tex)) || !made.tex ||
                FAILED(dev->CreateShaderResourceView(made.tex.Get(), nullptr, &made.srv)) || !made.srv)
                return;
            made.w = d.Width;
            made.h = d.Height;
            made.format = d.Format;
            g_snapshot = made;
        }
        vScreenCopyResourceRaw(ctx, g_snapshot.tex.Get(), h.Get());
        ok = true;
    });
    if (!ran || !ok) {
        ++g_win.snapRefused;
        return false;
    }
    g_snapped = true;
    ++g_win.snapshots;
    return true;
}

ID3D11ShaderResourceView* flatPanelSteadySceneView(uint32_t width, uint32_t height) {
    if (!flatPanelSteadyOn() || !g_snapped || !g_snapshot.srv || g_snapshot.w != width || g_snapshot.h != height) return nullptr;
    return g_snapshot.srv.Get();
}

}  // namespace edvr
