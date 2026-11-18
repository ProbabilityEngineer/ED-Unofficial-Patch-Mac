#pragma once
// ed-metalfx: XeSS Frame Generation is not part of this fork (no XeSS, no D3D12 bridge). Upstream's shared F8 menu and
// frame-rate readout ask the bridge for its state; this header answers every question as "frame generation off", so the
// menu code stays as upstream wrote it and future menu updates merge cleanly. The XeSS rows themselves are not in
// edvr.ini, so the row helpers below never match a row.

#include <cstdint>

namespace edvr {

inline bool xessFgBridged() { return false; }
inline bool xessFgExclusiveFullscreen() { return false; }
inline uint32_t xessFgMaxGeneratedFrames() { return 0; }
inline const char* xessFgProblem() { return nullptr; }
inline bool xessFgWanted() { return false; }
inline bool xessFgBridgeThisSession() { return false; }
inline uint32_t xessFgActiveGeneratedFrames() { return 0; }
inline bool xessFgPresentStats(uint64_t*, uint64_t*, const char** source) {
    if (source) *source = nullptr;
    return false;
}
inline const char* xessFgPresentUnavailableWhy() { return "no frame generation in this build"; }

}  // namespace edvr

// The menu's row helpers (upstream keeps them in menu_flat_rows.h beside the XeSS rows).
inline bool xessFgToggleRowDim(bool flatProfile, bool antiAliasingIsXess) { return flatProfile && !antiAliasingIsXess; }
inline bool xessFgFramesRowDim(bool flatProfile, bool antiAliasingIsXess, bool frameGenerationOn) {
    return xessFgToggleRowDim(flatProfile, antiAliasingIsXess) || !frameGenerationOn;
}
inline int xessFgFramesShown(int stored, unsigned maxFrames) {
    int v = stored < 1 ? 1 : stored > 3 ? 3 : stored;
    if (maxFrames != 0 && static_cast<unsigned>(v) > maxFrames) v = static_cast<int>(maxFrames);
    return v;
}
