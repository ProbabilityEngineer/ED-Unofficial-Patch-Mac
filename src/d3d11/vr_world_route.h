// Stage 2b stand-in (ed-metalfx, 2026-11-14): the VR world route is gone. In a flat session it only observed
// (state=observing, treated=0 in every flat log), so each entry point answers as it did there.
//
// Kept for real: VrWorldInternalScope and g_vrWorldInternal. Flat code (the UI layer, the depth writer probe, the
// desktop window) marks its own context calls with the scope, and the game-facing hooks (vscreen.cpp,
// exposure_fix.cpp) pass those calls straight through.
#pragma once

#include "flat_compute_readback.h"

#include <cstdint>

struct ID3D11DeviceContext;

namespace edvr {

inline thread_local bool g_vrWorldInternal = false;
struct VrWorldInternalScope {
    bool previousWorld = g_vrWorldInternal;
    FlatComputeInternalScope compute;
    VrWorldInternalScope() noexcept { g_vrWorldInternal = true; }
    ~VrWorldInternalScope() { g_vrWorldInternal = previousWorld; }
    VrWorldInternalScope(const VrWorldInternalScope&) = delete;
    VrWorldInternalScope& operator=(const VrWorldInternalScope&) = delete;
};

constexpr bool g_vrWorldWants = false;
constexpr bool g_vrWorldWatchWrites = false;
inline bool vrWorldRouteWantsDraws() { return false; }
inline void vrWorldRouteDraw(ID3D11DeviceContext*) {}
inline void vrWorldRouteNoteWrite(const void*) {}
inline void vrWorldRouteNoteRtvClear(void*) {}
inline void vrWorldRouteNoteDispatch() {}
inline void vrWorldRouteFrameBoundary() {}
inline bool vrWorldRouteEnabled() { return false; }
inline bool vrWorldRouteLayerMayTake() { return false; }
inline void vrWorldRouteNoteEyeTaken(uint32_t, uint64_t) {}
inline bool vrWorldRouteDoorLayerOnly(uint32_t, uint64_t) { return false; }

}  // namespace edvr
