#pragma once
// visual.star_light_colour (2026-11-15): the sun's light in the star's colour, far from the star too.
//
// Odyssey lights a ship in the star's colour only within a few light seconds of it; farther out the light fades to near-white
// (flights of 2026-11-15, diag.star_light_log: the colour row of the sun draw is mix(tint, white, f), f rising to ~1 by ~5 Ls).
// The sun draw (the deferred pair kFlatDeferredHdrVs/Ps) reads the light's colour times its brightness from PS b2 row 40
// (docs/sun-switch-re-2026-11-06.md, Q7). This fix rewrites those three floats as the game writes the buffer: the light keeps the
// game's luminance and takes the star's tint, predicted from the star's own disc colour (the star quad's PS b2[0].yzw,
// docs/star-glow-2026-10-22.md) by the curve fitted to the five stars flown (M, K, G, F, A: flat_star_light_math.h). Strength 0..100
// mixes the game's light with that. Nothing else in the buffer changes: rows 10..16, the camera the flat runtime reads, are untouched.
// Flat profile, owner context. One relaxed load per draw and one pointer compare per Map/Unmap/UpdateSubresource while off.
#include <atomic>
#include <cstdint>

struct ID3D11DeviceContext;
struct ID3D11Resource;

namespace edvr {

extern std::atomic<bool> g_starLightLive;          // the setting is above 0 in the flat profile
extern std::atomic<void*> g_starLightSunBuffer;    // the sun draw's PS b2 buffer, learned at the draw (null: none yet)
extern float g_starLightGameLight[3];              // the game's own light at the last write, before the fix (owner thread)
inline bool starLightLive() noexcept { return g_starLightLive.load(std::memory_order_relaxed); }
inline bool starLightIsSunBuffer(const void* res) noexcept {
    return res && res == g_starLightSunBuffer.load(std::memory_order_relaxed);
}

// At a flat draw on the owner context, before any substitution: learns the sun draw's buffer, reads the star quad's colour.
void starLightDraw(ID3D11DeviceContext* ctx, uint64_t vs, uint64_t ps);
// The game's writes of the sun buffer: Map remembers the pointer, Unmap (before the real Unmap) re-tints row 40 in place.
void starLightMapped(ID3D11Resource* res, void* data);
void starLightUnmap(ID3D11Resource* res);
// UpdateSubresource of the sun buffer: returns the data to pass on (a re-tinted copy, or `data` itself).
const void* starLightUpdate(ID3D11Resource* res, const void* data, uint32_t left, uint32_t right, bool boxed);
// The frame boundary (owner context): reads the setting, maps the star colour copy.
void starLightFrameBoundary(ID3D11DeviceContext* ctx);

}  // namespace edvr
