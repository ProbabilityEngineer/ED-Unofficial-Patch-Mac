// native_on_foot.h -- ed-metalfx stand-in (strip stage 2a).
//
// EDVR's VR on-foot stereo system (native_on_foot*.cpp/.h, about 73,000 lines) is removed. Its
// "active" flag was only ever set when the game ran in headset mode (native_on_foot.cpp set it
// from the last forwarded HMD render mode and the journal's on-foot state), so in the flat
// profile every call below already took its "not on foot" path. This header keeps the types and
// signatures the remaining code names, with the flat answers: inactive, nothing captured, nothing
// drawn. The types are copied verbatim from the original header because callers read their fields.
#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>

#include "native_on_foot_optic_capture.h"   // nofoot::optic_capture::RowDescriptor (kept: pure types)
#include "native_on_foot_f116.h"            // nofoot::f116 (kept: pure types used by ui_depth.cpp)

struct ID3D11DeviceContext;
struct ID3D11Resource;

namespace edvr {

namespace nofoot {
// The game's two cinema render modes (from native_on_foot_core.h): the flat screen shown in the headset.
constexpr uint32_t kModeCinemaA = 5;
constexpr uint32_t kModeCinemaB = 6;
inline bool isCinemaMode(uint32_t mode) { return mode == kModeCinemaA || mode == kModeCinemaB; }
namespace f100 { constexpr uint32_t kTeeClockStride = 64; }   // vscreen.cpp's marker-tee clock stride
namespace f157 { inline bool countsMatch(uint32_t, uint32_t) noexcept { return false; } }   // the coat pack draw is never live
}  // namespace nofoot

struct NativeOnFootAirborneSnapshot {
    uint64_t generation = 0, sampleQpc = 0;
    uint32_t sourceFrame = 0;
    double root[3] = {}, verticalVelocityMps = 0.0, rawVerticalVelocityMps = 0.0;
    bool valid = false, airborne = false, takeoffEdge = false;
};
inline bool nativeOnFootAirborneSnapshot(NativeOnFootAirborneSnapshot*) noexcept { return false; }

enum class NativeOnFootGate : uint8_t {
    Frames = 0,       // frame boundaries while the flag is set
    WorldRoute,       // the VR world route owned the frame (native_temporal begin) or took an eye's screen draw (layer-only treat)
    UiLayer,          // the UI layer's world-screen gate held (on foot and a world screen seen)
    FootSplit,        // footSplitNow: the on-foot path (no ship split) was chosen
    DeferredPacing,   // the deferred frame pacing (weapon stability) was on
    PanelShape,       // vScreen saw a 16:9 target taken as the on-foot panel
    TransitionFlash,  // the transition flash's camera-mode gate (game mode 2 with a reset mailbox)
    Count
};
inline void nativeOnFootCountGate(NativeOnFootGate) {}

inline std::atomic<bool> g_nativeOnFootActive{false};
inline bool nativeOnFootActive() { return false; }
inline std::atomic<uint32_t> g_nativeOnFootCamMode{0};
inline std::atomic<uint32_t> g_nativeLastEffective{0};   // the render mode the game was last given; mode_watch/desktop_window keep their own reads
inline std::atomic<bool> g_nativeOnFootSharpHudKey{false};
inline std::atomic<uint32_t> g_nativeModeEdges{0};   // the render-mode setter's edges (only the on-foot system counted them)
inline uint64_t g_nativeOnFootTeeClockTicks = 0, g_nativeOnFootTeeClockCalls = 0;

inline void nativeOnFootFrameBoundary(uint32_t) {}
inline void nativeOnFootShutdown() {}
inline bool nativeOnFootBoardedEarly() { return false; }
inline void nativeOnFootHudProbe(uint64_t) {}
inline bool nativeOnFootBodyCullWanted() { return false; }
inline bool nativeOnFootBodyCullBegin(ID3D11DeviceContext*, uint32_t, uint32_t) { return false; }
inline void nativeOnFootBodyCullEnd(ID3D11DeviceContext*) {}
inline void nativeOnFootBodyCullShutdown() {}
inline bool nativeOnFootCoatPackLive() { return false; }
inline bool nativeOnFootCoatPackSkips(ID3D11DeviceContext*, uint32_t, uint32_t, bool, const void*) { return false; }
inline bool nativeOnFootB3Counting() { return false; }
inline void nativeOnFootB3NoteEyeDraw() {}
inline bool nativeOnFootSpawnLive() { return false; }
inline void nativeOnFootSpawnDraw(ID3D11DeviceContext*, uint32_t, uint32_t, uint32_t, int32_t, const void*) {}
inline uint32_t nativeOnFootEdges() { return 0; }
inline bool nativeOnFootSharpHudWanted() { return false; }
inline bool nativeOnFootMarkerProbeLive() { return false; }
inline bool nativeOnFootTeeFast(uint64_t) { return false; }
inline void nativeOnFootMarkerProbeMap(ID3D11Resource*, void*, int) {}
inline void nativeOnFootMarkerProbeUnmap(const ID3D11Resource*) {}
inline void nativeOnFootMarkerProbeDraw(ID3D11DeviceContext*, uint32_t, uint32_t, char, uint32_t = 0, uint32_t = 0, int32_t = 0) {}
inline void nativeOnFootMarkerProbeUpdate(const ID3D11Resource*, const void*, bool, uint32_t, uint32_t) {}
inline void nativeOnFootCb1Copied(const ID3D11Resource*) {}
inline void nativeOnFootMarkerProbeShutdown() {}
inline bool nativeOnFootMarkerFixLive() { return false; }
inline bool nativeOnFootMarkerFixPending() { return false; }
inline bool nativeOnFootMarkerFixBegin(ID3D11DeviceContext*) { return false; }
inline void nativeOnFootMarkerFixEnd(ID3D11DeviceContext*) {}
inline uint32_t nativeOnFootMarkerFrameNow() { return 0; }
inline uint32_t nativeOnFootRenderFrame() { return 0; }
struct MarkerCropNdc {
    std::atomic<float> p[2];
    std::atomic<float> drawn[2];
    std::atomic<uint64_t> pMs, drawnMs;
    std::atomic<bool> sHave;
    std::atomic<float> sNdc[2], sW, sHead;
    std::atomic<uint32_t> sFrame, sKey, drawnFrame;
    std::atomic<uint32_t> patchFrame, patchKey, patchDraws;
    std::atomic<float> patchDx, patchDy;
    std::atomic<bool> patchNewest;
};
inline MarkerCropNdc g_markerCropNdc[2];   // zero: nothing publishes the marker series any more
inline bool nativeOnFootMarkerFrameCounts(uint32_t, uint32_t decided[2], uint32_t bound[2]) { decided[0]=decided[1]=bound[0]=bound[1]=0; return false; }
inline size_t nativeOnFootLabelDrawsJson(uint32_t, char* buf, size_t cap) { if (buf && cap) buf[0]=0; return 0; }
inline void nativeOnFootLabelIssue(ID3D11DeviceContext*, bool) {}
inline bool nativeOnFootLabelPending() { return false; }
struct NativeOnFootPoseStamp {
    bool valid = false, writePresent = false, matricesValid = false, sourceFresh = false;
    uint32_t renderFrame = 0, fpFrame = 0, fpSequence = 0, gGameFrame = 0, itemSerial = 0, rigGeneration = 0;
    uint64_t rig = 0, qpc = 0;
    uint16_t armJoints[2][3] = {}; // U/F/H indices for left and right.
};
inline bool nativeOnFootPoseStamp(uint32_t, NativeOnFootPoseStamp*) { return false; }
struct NativeWeaponUiSubmitIdentity {
    uint32_t renderFrame = 0, itemSerial = 0, ordinal = ~0u, submitThread = 0;
    uint64_t submitQpc = 0, renderer = 0, part = 0, instance = 0;
    uint16_t nameId = 0;
    uint8_t facing = 0;
    int32_t space = 0;
    bool itemRead = false, fieldsValid = false, instanceValid = false;
};
inline bool nativeOnFootWeaponUiCurrentSubmit(uintptr_t, NativeWeaponUiSubmitIdentity*) noexcept { return false; }
struct NativeOnFootCameraEvidence {
    enum : uint32_t { kMatricesValid = 1, kSourcesExact = 2, kTimesFresh = 4, kPinTargetsValid = 8, kPoseSourcesFresh = 16, kM3pCanonicalized = 32 };
    uint32_t status = 0; // 0 unavailable, 1 completed F81 row for the requested hook frame, 2 cached IkG (never an exact draw claim)
    uint32_t validFlags = 0, requestedFrame = 0, sourceFrame = 0, fpFrame = 0, rootReadRenderFrame = 0, gGameFrame = 0, itemSerial = 0;
    uint64_t sampleQpc = 0, qpc = 0, fpQpc = 0, rootQpc = 0, ms = 0, rootMs = 0, qpcFrequency = 0;
    // ms/rootMs exist only in cached IkG; F81 has QPC stamps instead. kTimesFresh means all available source stamps are non-future and <=100 ms old.
    float froot[16] = {}, m3p[16] = {}, G[16] = {}, bodyOffset[3] = {}, pinEyeTarget[3] = {}, pinTarget[3] = {};
};
inline bool nativeOnFootCameraEvidence(uint32_t, NativeOnFootCameraEvidence*) { return false; }
inline void nativeOnFootMenuInputArm(const wchar_t*, uint32_t, uint32_t) {}
inline void nativeOnFootMenuInputBoundary(uint32_t) {}
inline std::string nativeOnFootMenuInputEnd() { return std::string(); }
inline std::string nativeOnFootMenuInputDrawBridge(uint32_t, uint32_t, uint64_t) { return std::string(); }
inline void nativeOnFootMenuInputDiscard() {}
inline bool nativeOnFootMarkerResidual(uint32_t, int, float out[4]) { out[0]=out[1]=out[2]=out[3]=0; return false; }
inline void nativeOnFootOpticCaptureBegin(const wchar_t*) {}
inline void nativeOnFootOpticCaptureSample(uint32_t, uint32_t, uint32_t, uint32_t, uint64_t, const char*, bool, bool, bool, bool, int32_t,
                                           uint32_t, uint64_t, const float[12]) {}
inline void nativeOnFootOpticCaptureEnd(const char*, const nofoot::optic_capture::RowDescriptor*, uint32_t, uint32_t) {}

}  // namespace edvr
