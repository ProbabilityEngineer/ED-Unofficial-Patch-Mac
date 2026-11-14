// Stage 2b stand-in (ed-metalfx, 2026-11-14): the VR camera census and the native temporal channel's head and eye
// readers are gone with the VR runtime. Each answers as it did in a flat session (no census, no VR frame).
#pragma once

#include <cstdint>

struct ID3D11DeviceContext;

namespace edvr {

inline bool vrCameraCensusWanted() { return false; }
inline void vrCameraCensusFrameBoundary() {}
inline void vrCameraCensusEyeDraw(ID3D11DeviceContext*, uint32_t) {}
inline bool nativeTemporalEyeGeometry(uint32_t, uint64_t*, float[4], float[2]) { return false; }
inline bool nativeTemporalHeadPose(uint64_t*, float[12], float[2][12]) { return false; }
inline uint64_t nativeTemporalFrameSequence() { return 0; }
inline bool nativeTemporalEyeSeparation(float*) { return false; }

}  // namespace edvr
