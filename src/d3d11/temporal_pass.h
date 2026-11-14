// What is left of EDVR's VR eye temporal pass in ed-metalfx (stage 2b, 2026-11-14).
//
// The VR pass itself (per-eye TAA, the trained engines' eye path, the camera-row ring, the eye dumps) is
// gone. Two jobs stay, both used by the flat profile:
//   - temporalPassConfigure: arms the per-mode pieces the flat resolve reads (engine-record velocity,
//     planet patch motion) and records whether fix.temporal_aa asks for a temporal mode;
//   - temporalPassWantsFssChrome: that answer, read by the UI layer and the remlok fix.
// The other entry points keep their signatures so callers compile unchanged; each answers as the VR pass
// did in a flat session (nothing chosen, nothing measured, nothing drawn).
#pragma once

#include <cstddef>
#include <cstdint>

struct ID3D11Device;
struct ID3D11DeviceContext;
struct ID3D11Texture2D;

namespace edvr {

class Config;

void temporalPassConfigure(Config& cfg);
void temporalPassShutdown();

namespace detail {
extern bool g_temporalPassWantedFssChrome;
}  // namespace detail
// fix.temporal_aa names a temporal mode (fsr or mfx), as of the last configure.
inline bool temporalPassWantsFssChrome() { return detail::g_temporalPassWantedFssChrome; }

struct TemporalDrawStamp {
    uint32_t sceneFrame = 0, epoch = 0, scheduledIndex = UINT32_MAX, scheduledRenderFrame = 0;
    bool sceneFrameValid = false, epochValid = false, scheduledIndexValid = false;
};
inline bool temporalPassDrawStamp(TemporalDrawStamp*) { return false; }
inline void temporalPassDumpHistory(const char*) {}
inline void temporalPassTick(ID3D11DeviceContext*) {}
inline void temporalPassNoteSceneWrite(const void*, const void*, uint32_t) {}
inline void temporalPassNoteFirstEyeDraw(ID3D11DeviceContext*) {}
inline bool temporalPassWantsRigidDraw(int) { return false; }
inline void temporalPassNoteRigidDraw(int, const void*, uint64_t) {}
inline bool temporalPassRigidDrawMayWant(int) { return false; }
inline bool temporalPassWantsDraw() { return false; }
inline void temporalPassCountDraw() {}
inline void temporalPassNoteDraw(ID3D11DeviceContext*) {}
inline void temporalPassNoteOmSet(unsigned) {}
inline void temporalPassFrameBoundary() {}
inline bool temporalPassChosenRows(float[12], bool*) { return false; }
inline bool temporalPassEyeRows(int, float[12], const void**, uint64_t*) { return false; }
inline bool temporalPassEyeTan(int, float[4]) { return false; }
inline void temporalPassArmEyeDump() {}
inline bool temporalPassTotals(uint32_t*, double*, double*, double*, double*) { return false; }
inline bool temporalPassRegistration(char*, size_t, char*, size_t, char*, size_t) { return false; }
inline void temporalPassNoteFootTotals() {}
inline bool temporalPassDlaaTotals(uint32_t*, double*, double*, uint32_t*) { return false; }
inline bool temporalPassUiResolveTotals(uint64_t*, uint64_t[3]) { return false; }
inline bool temporalPassMvDepthLayerTotals(uint64_t[4]) { return false; }
inline bool temporalPassTrainedTotals(uint32_t*, double*, double*, uint32_t*, const char**, bool*) { return false; }
inline bool temporalPassDlaaFullTotals(int, uint32_t*, double*, double*) { return false; }
inline bool temporalPassPriceWindow(double[3], double*, uint32_t*, uint32_t*) { return false; }
inline bool temporalPassPlanes(float*, float*) { return false; }
inline bool temporalPassUpscale(float*, float*) { return false; }
inline float temporalPassDepthAt(float) { return 0.0f; }

}  // namespace edvr
