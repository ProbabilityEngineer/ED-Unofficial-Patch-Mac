// The HUD layer's FXAA edge smoothing (fix.hud_antialiasing = light; ported from upstream ff7f383c): single-pass
// premultiplied-alpha-aware FXAA 3.11 over the layer's finished 8-bit content, so the composite that downsamples it reads
// smoothed edges. One R8G8B8A8_UNORM output texture (4 BPP, against SMAA's ~10) and one linear-clamp sampler; mirrors
// UiLayerSmaa's lifecycle and stand-down contract (ui_layer_smaa.h). The shader is kUiLayerFxaaHlsl (fixed_shader_source.h),
// built offline (tools/temporal_shader_build) into kUiLayerFxaaBytecode. Nothing here logs: ui_layer.cpp reads the state
// and the failure this returns and says it.
//
// One UiLayerFxaa belongs to one eye's layer. uiLayerFxaaEnsure makes what is missing at the layer's size (the target is
// re-made when the size moves), and a creation that failed at a size is remembered for that size: every later call there
// answers kStoodDown without trying again. uiLayerFxaaRun is one dispatch.
#pragma once

#include "ui_layer_smaa.h"   // UiLayerSmaaFail, UiLayerSmaaState, uilayersmaa_detail's formatStores and makeTarget

#include "temporal_shader_bytecode.h"

#include <d3d11.h>
#include <wrl/client.h>

#include <cstdint>

namespace edvr {

struct UiLayerFxaa {
    template <class T>
    using Ptr = Microsoft::WRL::ComPtr<T>;
    Ptr<ID3D11ComputeShader> fxaaCs;
    Ptr<ID3D11SamplerState> linear;
    Ptr<ID3D11Buffer> metrics;
    Ptr<ID3D11Texture2D> out;
    Ptr<ID3D11UnorderedAccessView> outUav;
    Ptr<ID3D11ShaderResourceView> outSrv;
    uint32_t w = 0, h = 0;
    uint32_t failedW = 0, failedH = 0;
    // The output's format follows the layer's, as SMAA's does: R16G16B16A16_FLOAT when the flat layer is (an fp16 door frame, e.g. HDR
    // output), so HUD values above 1 are not clipped to 8 bits; R8G8B8A8_UNORM otherwise.
    DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN, failedFormat = DXGI_FORMAT_UNKNOWN;

    void releaseTargets() {
        metrics.Reset();
        out.Reset();
        outUav.Reset();
        outSrv.Reset();
        w = h = 0;
        format = DXGI_FORMAT_UNKNOWN;
    }
    void releaseAll() {
        releaseTargets();
        fxaaCs.Reset();
        linear.Reset();
        failedW = failedH = 0;
        failedFormat = DXGI_FORMAT_UNKNOWN;
    }
    bool ready() const { return outSrv && outUav && metrics && fxaaCs && linear && w && h; }
};

inline UiLayerSmaaState uiLayerFxaaEnsure(UiLayerFxaa& s, ID3D11Device* dev, uint32_t w, uint32_t h, UiLayerSmaaFail* fail,
                                         DXGI_FORMAT outputFormat = DXGI_FORMAT_R8G8B8A8_UNORM) {
    using namespace uilayersmaa_detail;
    if (s.ready() && s.w == w && s.h == h && s.format == outputFormat) return UiLayerSmaaState::kReady;
    if (s.failedW == w && s.failedH == h && s.failedFormat == outputFormat && w && h) return UiLayerSmaaState::kStoodDown;
    s.releaseTargets();
    HRESULT hr = S_OK;
    const auto stand = [&](const char* what) {
        s.releaseTargets();
        s.failedW = w;
        s.failedH = h;
        s.failedFormat = outputFormat;
        if (fail) {
            fail->what = what;
            fail->hr = hr;
        }
        return UiLayerSmaaState::kFailed;
    };
    if (!dev || !w || !h) {
        hr = E_INVALIDARG;
        return stand("no device or an empty layer");
    }
    if (outputFormat != DXGI_FORMAT_R8G8B8A8_UNORM && outputFormat != DXGI_FORMAT_R16G16B16A16_FLOAT) {
        hr = E_INVALIDARG;
        return stand("an output format other than R8G8B8A8_UNORM or R16G16B16A16_FLOAT");
    }
    if (!formatStores(dev, outputFormat, &hr))
        return stand(outputFormat == DXGI_FORMAT_R16G16B16A16_FLOAT ? "no typed unordered-access store for R16G16B16A16_FLOAT on this GPU"
                                                                     : "no typed unordered-access store for R8G8B8A8_UNORM on this GPU");
    if (!s.fxaaCs && FAILED(hr = dev->CreateComputeShader(kUiLayerFxaaBytecode, sizeof(kUiLayerFxaaBytecode), nullptr, s.fxaaCs.GetAddressOf())))
        return stand("the FXAA shader could not be created");
    if (!s.linear) {
        D3D11_SAMPLER_DESC sd{};
        sd.Filter = D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT;
        sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        sd.MaxLOD = D3D11_FLOAT32_MAX;
        if (FAILED(hr = dev->CreateSamplerState(&sd, s.linear.GetAddressOf()))) return stand("the linear sampler could not be created");
    }
    {
        const float metrics[4] = {1.0f / static_cast<float>(w), 1.0f / static_cast<float>(h), static_cast<float>(w), static_cast<float>(h)};
        D3D11_BUFFER_DESC bd{};
        bd.ByteWidth = sizeof(metrics);
        bd.Usage = D3D11_USAGE_IMMUTABLE;
        bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        D3D11_SUBRESOURCE_DATA init{};
        init.pSysMem = metrics;
        if (FAILED(hr = dev->CreateBuffer(&bd, &init, s.metrics.GetAddressOf()))) return stand("the metrics constant buffer could not be created");
    }
    if (!makeTarget(dev, w, h, outputFormat, s.out, s.outUav, s.outSrv, &hr))
        return stand("the smoothed output could not be created");
    s.w = w;
    s.h = h;
    s.format = outputFormat;
    return UiLayerSmaaState::kMade;
}

inline ID3D11ShaderResourceView* uiLayerFxaaRun(UiLayerFxaa& s, ID3D11DeviceContext* ctx, ID3D11ShaderResourceView* layer) {
    if (!ctx || !layer || !s.ready()) return nullptr;
    ID3D11SamplerState* savedSampler = nullptr;
    ctx->CSGetSamplers(0, 1, &savedSampler);
    ID3D11SamplerState* linear = s.linear.Get();
    ID3D11Buffer* metrics = s.metrics.Get();
    ID3D11ShaderResourceView* in = layer;
    ID3D11UnorderedAccessView* target = s.outUav.Get();
    ID3D11ShaderResourceView* noSrv = nullptr;
    ID3D11UnorderedAccessView* noUav = nullptr;

    ctx->CSSetSamplers(0, 1, &linear);
    ctx->CSSetConstantBuffers(0, 1, &metrics);
    ctx->CSSetShader(s.fxaaCs.Get(), nullptr, 0);
    ctx->CSSetShaderResources(0, 1, &in);
    ctx->CSSetUnorderedAccessViews(0, 1, &target, nullptr);
    ctx->Dispatch((s.w + 31u) / 32u, (s.h + 7u) / 8u, 1u);
    ctx->CSSetShaderResources(0, 1, &noSrv);
    ctx->CSSetUnorderedAccessViews(0, 1, &noUav, nullptr);

    ctx->CSSetSamplers(0, 1, &savedSampler);
    if (savedSampler) savedSampler->Release();
    return s.outSrv.Get();
}

}  // namespace edvr
