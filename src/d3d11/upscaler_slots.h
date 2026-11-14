// The upscaler feature slots the trained backends (fsr3_engine, metal_fx_engine) keep their per-slot state in
// (moved here from dlaa.h when ed-metalfx dropped NGX, stage 2b 2026-11-14). Each slot has its own feature, key and
// history, so two callers never share an accumulation. The flat profile uses slot 0; slots 1 and 2 were EDVR's
// second eye and its VR world route, and are kept so the engines' tables and their log lines stay as they were.
#pragma once

#include <cstdint>

namespace edvr {

constexpr uint32_t kUpscalerSlots = 3;

// How a slot is named in a log line.
inline const char* upscalerSlotLabel(int slot) {
    switch (slot) {
        case 0:  return "eye 0";
        case 1:  return "eye 1";
        case 2:  return "upscaler slot 2";
        default: return "an unknown upscaler slot";
    }
}

}  // namespace edvr
