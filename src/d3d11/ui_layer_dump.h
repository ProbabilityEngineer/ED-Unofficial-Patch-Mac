// The UI layer's one-frame dump (diagnostic only; 2026-11-21, the nav tag left bright over the comms panel with AA on).
//
// The census key (hotkey.dump_draws, F6) also copies, in the first census frame that tonemaps the HUD layer, the left eye's HDR HUD
// layer as the tonemap re-issue reads it (premultiplied radiance, transmittance in alpha) and the 8-bit layer after the re-issue and
// the coverage pass (what the door composites: colour, transmittance in alpha). Each is copied GPU-side into a staging texture at that
// moment and written at the next frame boundary, next to the log, as
//     ui_layer_<time>_<what>_<w>x<h>_f<dxgi format>.bin
// a 32-byte header ("EDVRLYR1", then width, height, DXGI format, bytes per texel, row bytes, 0 as uint32) followed by the rows,
// tightly packed. Nothing is changed in the frame; with the key unpressed the cost is one branch at each tonemap re-issue.
//
// Header-only and stateless apart from the one pending capture per slot, so a rig could drive it; the caller owns the context and runs
// it on the render thread only.
#pragma once

#include <d3d11.h>
#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <string>

namespace edvr::ui_layer_dump {

inline uint32_t bytesPerTexel(DXGI_FORMAT f) {
    switch (f) {
        case DXGI_FORMAT_R16G16B16A16_FLOAT:
        case DXGI_FORMAT_R16G16B16A16_UNORM:
            return 8;
        case DXGI_FORMAT_R8G8B8A8_UNORM:
        case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
        case DXGI_FORMAT_B8G8R8A8_UNORM:
        case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
        case DXGI_FORMAT_R10G10B10A2_UNORM:
        case DXGI_FORMAT_R11G11B10_FLOAT:
            return 4;
        case DXGI_FORMAT_R32G32B32A32_FLOAT:
            return 16;
        default:
            return 0;
    }
}

struct Slot {
    ID3D11Texture2D* staging = nullptr;
    uint32_t w = 0, h = 0, bpt = 0;
    DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
    char what[24] = "";
    void reset() {
        if (staging) staging->Release();
        staging = nullptr;
        w = h = bpt = 0;
        format = DXGI_FORMAT_UNKNOWN;
        what[0] = 0;
    }
};

// Copy `src` (a single-sample, single-mip 2D texture) into a new staging texture. False, with `why` set, when it cannot.
inline bool capture(ID3D11DeviceContext* ctx, ID3D11Texture2D* src, const char* what, Slot& slot, const char** why) {
    slot.reset();
    if (!ctx || !src) { *why = "no texture"; return false; }
    D3D11_TEXTURE2D_DESC d{};
    src->GetDesc(&d);
    const uint32_t bpt = bytesPerTexel(d.Format);
    if (!bpt) { *why = "a format the dump does not write"; return false; }
    if (d.SampleDesc.Count != 1 || d.ArraySize != 1) { *why = "a multisampled or array texture"; return false; }
    D3D11_TEXTURE2D_DESC s = d;
    s.MipLevels = 1;
    s.Usage = D3D11_USAGE_STAGING;
    s.BindFlags = 0;
    s.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    s.MiscFlags = 0;
    ID3D11Device* dev = nullptr;
    ctx->GetDevice(&dev);
    if (!dev) { *why = "no device"; return false; }
    const HRESULT hr = dev->CreateTexture2D(&s, nullptr, &slot.staging);
    dev->Release();
    if (FAILED(hr) || !slot.staging) { slot.staging = nullptr; *why = "the staging texture could not be made"; return false; }
    ctx->CopySubresourceRegion(slot.staging, 0, 0, 0, 0, src, 0, nullptr);
    slot.w = d.Width;
    slot.h = d.Height;
    slot.bpt = bpt;
    slot.format = d.Format;
    std::snprintf(slot.what, sizeof(slot.what), "%s", what);
    return true;
}

// Map the slot (waiting for the GPU: once, on a key press) and write it. Returns the file's name in `out`, or the reason in `why`.
inline bool write(ID3D11DeviceContext* ctx, Slot& slot, const std::wstring& dir, const char* stamp, std::string& out, const char** why) {
    if (!slot.staging) { *why = "nothing captured"; return false; }
    D3D11_MAPPED_SUBRESOURCE m{};
    if (FAILED(ctx->Map(slot.staging, 0, D3D11_MAP_READ, 0, &m)) || !m.pData) {
        *why = "the staging texture could not be mapped";
        slot.reset();
        return false;
    }
    char name[128];
    std::snprintf(name, sizeof(name), "ui_layer_%s_%s_%ux%u_f%u.bin", stamp, slot.what, slot.w, slot.h, static_cast<unsigned>(slot.format));
    std::wstring path = dir + L"\\";
    for (const char* c = name; *c; ++c) path += static_cast<wchar_t>(*c);
    bool ok = false;
    FILE* f = nullptr;
    if (_wfopen_s(&f, path.c_str(), L"wb") == 0 && f) {
        const uint32_t row = slot.w * slot.bpt;
        const uint32_t header[8] = {0x52564445u, 0x3152594Cu, slot.w, slot.h, static_cast<uint32_t>(slot.format), slot.bpt, row, 0};  // "EDVR" "LYR1"
        ok = std::fwrite(header, sizeof(header), 1, f) == 1;
        for (uint32_t y = 0; ok && y < slot.h; ++y)
            ok = std::fwrite(static_cast<const uint8_t*>(m.pData) + static_cast<size_t>(y) * m.RowPitch, row, 1, f) == 1;
        ok = (std::fclose(f) == 0) && ok;
    }
    ctx->Unmap(slot.staging, 0);
    slot.reset();
    if (!ok) { *why = "the file could not be written"; return false; }
    out = name;
    return true;
}

}  // namespace edvr::ui_layer_dump
