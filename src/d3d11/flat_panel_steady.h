// fix.steady_panels: the cockpit panels drawn in the scene, made steady for the temporal upscaler (flat only; off by default; live).
//
// WHY. With the panels in the scene (fix.ui_quality off, or this key on), their pixels are upscaled with the rest of the frame, and the
// motion each pixel is handed is the motion of what lies behind the panel: the sky through the canopy, a planet, a station. The panels
// do not move with any of that (they ride the cockpit), so the upscaler drags their text along the background's motion, and the text
// shimmers wherever the background changes. The UI quality layer avoids it by taking the panels out of the scene altogether; this is the
// other way, which keeps them in the scene (one tonemap, one bloom, the game's own look) and tells the upscaler the truth about them.
//
// WHAT. Each cockpit-panel draw the game issues into the scene's HDR target H is issued once more right after the game's own issue, into a
// layer P of its own (R16G16B16A16_FLOAT at H's size, cleared to (0, 0, 0, 1) at the frame's first such draw): the same depth view with a
// depth-stencil state whose writes are off, the game's shaders, constants and jitter, and the UI layer's blend conversion
// (ui_layer_math.h uiLayerConvertBlend), so P holds the panels' own premultiplied light in rgb and their transmittance in alpha, exactly
// as the UI layer would. The resolve's prep (flat_mono_shader_source.h, debug.w value 64) reads P beside the scene's colour: a pixel whose
// light is at least half the panels' (the text, the glyphs' glow), or whose background the panels hide by three quarters or more, is a
// panel pixel, and its motion is zero with its history kept. Every other pixel keeps the motion it had. Nothing in the picture changes:
// P is read by the prep alone.
//
// WHICH DRAWS. The cockpit HUD families the UI layer would take (flat_ui_layer_math.h flatUiLayerTakesFamily): the holo panels, the flight
// HUD, the target sprite and the radar's holograms. Not the panels' frosted backing (it only dims what lies behind, so it is not ink), and
// not the overlay extras (supercruise bars, dust, orbit lines, weapons fire, the maps), which stay where UI quality puts them. With this key
// on the UI layer never takes the panel families (flatUiLayerDecide), whatever UI quality says; the extras are unaffected.
//
// PORTABLE. No backend is named: the prep's motion is what FSR, MetalFX and EDVR's TAA all read. (MetalFX's reactive mask would be a further,
// Mac-only step; it is not here.)
//
// THREADS. The flat runtime's owner thread only (its draw scope, its resolve and its frame boundary).
#pragma once

#include <cstdint>

struct ID3D11DeviceContext;
struct ID3D11ShaderResourceView;

namespace edvr {

enum class UiLayerFamily : uint8_t;

// One relaxed load: fix.steady_panels is on in the flat profile (read at each frame boundary, so live).
bool flatPanelSteadyOn();

// The HDR route (SS 1.0 and above) asked for the mask within the last second (flatPanelSteadyView). Below SS 1.0 the resolve runs after the
// tonemap (the copy route), where the panels' light cannot be compared with the scene's, so nothing is marked and the UI layer keeps the panel
// families as UI quality says: the key acts only while this is true.
bool flatPanelSteadyRouteLive();

// The families it marks (see WHICH DRAWS). The backing pair is the caller's to leave out (flatUiPanelBackingPair).
bool flatPanelSteadyFamily(UiLayerFamily f);

// Once a frame from the flat runtime's Present path: re-reads the key, starts the frame's state, prints the 30 s line while on (and one line
// when it turns off).
void flatPanelSteadyFrame();

// The scope's re-issue bracket (FlatRuntimeDrawScope::uiReissue), after the game's own issue: binds P (width x height, H's size) with the
// converted blend and a no-write depth state. False: nothing was changed (counted by reason), and End must not be called.
bool flatPanelSteadyBegin(ID3D11DeviceContext* ctx, uint32_t width, uint32_t height);
void flatPanelSteadyEnd(ID3D11DeviceContext* ctx);

// The resolve, on the HDR route: P's view when this frame marked panels at width x height, else null. After this call the frame's P is the
// prep's: a later panel draw this frame is refused (counted as late), since the prep has already read P.
ID3D11ShaderResourceView* flatPanelSteadyView(uint32_t width, uint32_t height);

// The resolve says the prep ran with P bound (true) or refused it (false: the route, a reset, a size), for the 30 s line.
void flatPanelSteadyNoteResolve(bool bound);

}  // namespace edvr
