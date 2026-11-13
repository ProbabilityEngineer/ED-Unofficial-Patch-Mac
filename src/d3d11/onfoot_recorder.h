// onfoot_recorder.h -- ed-metalfx stand-in (strip stage 2a). The VR on-foot flight recorder (F213,
// onfoot_recorder.cpp) is removed with the on-foot system; the mark key's handler in device_hook.cpp
// still calls mark(), which now does nothing.
#pragma once

namespace edvr {
namespace onfootrec {
inline void mark() {}
}  // namespace onfootrec
}  // namespace edvr
