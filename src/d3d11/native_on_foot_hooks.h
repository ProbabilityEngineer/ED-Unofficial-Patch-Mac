// native_on_foot_hooks.h -- ed-metalfx stand-in (strip stage 2a). The VR on-foot avatar hooks
// (native_on_foot_hooks.cpp) are removed; device_hook.cpp still calls these two at the frame
// boundary and at shutdown, where there is now nothing to do.
#pragma once

namespace edvr {
inline void nativeOnFootHooksFrameBoundary() {}
inline void nativeOnFootHooksShutdown() {}
}  // namespace edvr
