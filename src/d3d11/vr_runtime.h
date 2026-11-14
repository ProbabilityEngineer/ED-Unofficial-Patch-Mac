// Stage 2b stand-in (ed-metalfx, 2026-11-14): the VR back-end diagnosis is gone with VR. ed-metalfx is flat only,
// so no VR runtime is ever in use; the callers' VR-only advice paths keep compiling and never fire in flat.
#pragma once

namespace edvr {

enum class VrRuntime {
    NoneLoaded,
    NativeOpenXR,
    ForeignOpenvr,
    OculusNative,
};
inline VrRuntime vrRuntime() { return VrRuntime::NoneLoaded; }
inline const char* vrRuntimeShortWhy() { return "ed-metalfx runs no VR runtime"; }
inline void vrRuntimeExplainOnce() {}
inline void vrRuntimeTick() {}

}  // namespace edvr
