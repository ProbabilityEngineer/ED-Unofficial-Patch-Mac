// Stage 2b stand-in (ed-metalfx, 2026-11-14): the VR world route's mipped screen is gone with the route.
#pragma once

#include <cstdint>

struct ID3D11Device;
struct ID3D11DeviceContext;
struct ID3D11SamplerState;
struct ID3D11ShaderResourceView;
struct ID3D11Texture2D;
struct D3D11_SAMPLER_DESC;

namespace edvr {

inline ID3D11ShaderResourceView* vrWorldMipsScreen(ID3D11DeviceContext*, ID3D11Texture2D*, uint64_t) { return nullptr; }
inline ID3D11SamplerState* vrWorldMipsSampler(ID3D11Device*, const D3D11_SAMPLER_DESC&) { return nullptr; }

}  // namespace edvr
