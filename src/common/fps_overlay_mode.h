// menu.fps_overlay: off | compact | detailed (docs\design-f8-menu-unification-2026-11-15.md, "The FPS readout"). Pure, so tools\native_perf_history_test can pin it.
//
// The key was a switch (on | off) before. The old words still read: on, true, yes and 1 are compact; off, false, no and 0 are off. An empty or unknown value is the
// default, off. The menu writes the new words.
#pragma once

#include <cctype>
#include <cstring>

namespace edvr {

enum class FpsOverlayMode { Off, Compact, Detailed, Full };

inline FpsOverlayMode parseFpsOverlay(const char* text) {
  if (!text) return FpsOverlayMode::Off;
  char word[16] = {};
  size_t n = 0;
  const char* p = text;
  while (*p == ' ' || *p == '\t') ++p;
  for (; *p && n + 1 < sizeof(word); ++p) word[n++] = static_cast<char>(std::tolower(static_cast<unsigned char>(*p)));
  while (n && (word[n - 1] == ' ' || word[n - 1] == '\t' || word[n - 1] == '\r')) word[--n] = 0;
  if (!std::strcmp(word, "compact") || !std::strcmp(word, "on") || !std::strcmp(word, "true") || !std::strcmp(word, "yes") || !std::strcmp(word, "1"))
    return FpsOverlayMode::Compact;
  if (!std::strcmp(word, "detailed")) return FpsOverlayMode::Detailed;
  // full (ed-metalfx): the flat monitor panel; the headset shows it as detailed.
  if (!std::strcmp(word, "full")) return FpsOverlayMode::Full;
  return FpsOverlayMode::Off;
}

inline const char* fpsOverlayWord(FpsOverlayMode m) {
  return m == FpsOverlayMode::Compact ? "compact" : m == FpsOverlayMode::Detailed ? "detailed" : m == FpsOverlayMode::Full ? "full" : "off";
}

// The readout key's step (ed-metalfx, hotkey.monitor_panel): off -> compact -> detailed -> full -> off.
inline FpsOverlayMode nextFpsOverlay(FpsOverlayMode m) {
  return m == FpsOverlayMode::Off ? FpsOverlayMode::Compact
       : m == FpsOverlayMode::Compact ? FpsOverlayMode::Detailed
       : m == FpsOverlayMode::Detailed ? FpsOverlayMode::Full
       : FpsOverlayMode::Off;
}

}  // namespace edvr
