// See temporal_pass.h: the flat profile's share of the former VR eye temporal pass.
#include "temporal_pass.h"

#include <string>

#include "../common/config.h"
#include "../common/runtime_profile.h"
#include "../common/temporal_mode.h"
#include "celestial_motion.h"
#include "engine_velocity.h"
#include "fsr3_engine.h"
#include "scheduler_stack_probe.h"
#include "sky_ring.h"

namespace edvr {

namespace detail {
bool g_temporalPassWantedFssChrome = false;
}  // namespace detail

void temporalPassConfigure(Config& cfg) {
    detail::g_temporalPassWantedFssChrome = temporalModeEnabled(cfg.getString("fix.temporal_aa", "off"));
    skyRingConfigure(cfg.getString("diag.sky_ring", "off") == "on");
    // Engine-record velocity (docs/kinematic-motion-injection-2026-09-19.md) and planet patch motion (celestial_motion.h)
    // are part of fix.temporal_aa, with no key of their own: they arm and disarm with the mode, live. The flat resolve
    // reads both (flat_mono_resolve, fetched by the flat runtime).
    const bool engineMotionOn = runtimeFlatProfile() ? temporalModeEnabled(cfg.requestedTemporalMode())
                                                     : detail::g_temporalPassWantedFssChrome;
    engineVelocityConfigure(engineMotionOn);
    celestialMotionConfigure(engineMotionOn);
    // The scheduler stack-capture probe observes the engine, not the renderer: its own key.
    schedulerStackProbeConfigure(cfg.getBool("advanced.scheduler_probe", false));
    // The engine-motion census ran only during a VR eye dump run.
    engineVelocityDiagnostics(false);
}

void temporalPassShutdown() {
    skyRingShutdown();
    fsr3Shutdown();
}

}  // namespace edvr
