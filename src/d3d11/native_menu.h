#pragma once
#include <stdint.h>
// ed-metalfx: the native OpenXR menu session (edvrAcquireNativeMenu) is gone with the runtime; no session is ever open.
inline bool nativeMenuAvailable() { return false; }
inline bool nativeMenuActive() { return false; }
inline uint64_t nativeMenuRevision() { return 0; }
inline uint64_t nativeMenuSessionId() { return 0; }
inline bool nativeMenuEyePoses(float (&)[2][12]) { return false; }
