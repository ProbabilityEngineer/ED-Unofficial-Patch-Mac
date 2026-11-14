// Stage 2b stand-in (ed-metalfx, 2026-11-14): visual.star_glow in the headset is gone with VR. It armed only in the
// VR profile; the flat star glow is flat_star_glow.h.
#pragma once

struct ID3D11DeviceContext;

namespace edvr {

inline bool vrStarGlowWanted() { return false; }
inline void vrStarGlowFrameBoundary() {}
inline void vrStarGlowBeforeDraw(ID3D11DeviceContext*) {}
inline bool vrStarGlowAfterDraw(ID3D11DeviceContext*, bool) { return false; }
inline bool vrStarGlowAfterMayAct() { return false; }
inline void vrStarGlowReplayEnd(ID3D11DeviceContext*) {}

}  // namespace edvr
