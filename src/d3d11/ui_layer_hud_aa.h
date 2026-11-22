// The HUD layer's edge smoothing mode (fix.hud_antialiasing, off | light | sharp; docs/design-flat-ui-quality-2026-10-05.md,
// "2026-10-24: edge smoothing for the HUD layer"). The words: off, light is FXAA (ui_layer_fxaa.h, one pass), sharp is SMAA 1x
// (ui_layer_smaa.h, three passes). Older spellings are still read: fxaa means light; smaa, on, 1, true and yes mean sharp.
// Pure (no D3D, no globals), so the menu and a rig read exactly what the layer does.
#pragma once

#include <cstdint>

namespace edvr {

enum class HudAaMode : uint8_t { kOff = 0, kFxaa = 1, kSmaa = 2 };

inline HudAaMode hudAaParse(const char* raw) {
    if (!raw) return HudAaMode::kOff;
    while (*raw == ' ' || *raw == '\t') ++raw;
    auto eq = [](const char* a, const char* b) {
        while (*a && *b) {
            const char ca = *a >= 'A' && *a <= 'Z' ? static_cast<char>(*a - 'A' + 'a') : *a;
            if (ca != *b) return false;
            ++a;
            ++b;
        }
        while (*a == ' ' || *a == '\t') ++a;
        return *a == '\0' && *b == '\0';
    };
    if (eq(raw, "light") || eq(raw, "fxaa")) return HudAaMode::kFxaa;
    if (eq(raw, "sharp") || eq(raw, "smaa") || eq(raw, "on") || eq(raw, "1") || eq(raw, "true") || eq(raw, "yes")) return HudAaMode::kSmaa;
    return HudAaMode::kOff;
}

// The word the file keeps for a mode: the choice's own value ("light", "sharp"), off for anything else.
inline const char* hudAaWord(HudAaMode mode) {
    switch (mode) {
        case HudAaMode::kFxaa: return "light";
        case HudAaMode::kSmaa: return "sharp";
        case HudAaMode::kOff:
        default: return "off";
    }
}

// Whether a mode is one the ini's choice lists (the menu steps over those, so a stored older spelling steps to a real one).
inline bool hudAaIsChoiceWord(HudAaMode mode) { return mode != HudAaMode::kOff; }

}  // namespace edvr
