#pragma once
// TEMPORARY (star-light-colour arc, 2026-11-15; .agents/design-slim-mac-flat-2026-11-13.md, "star-coloured light far from the star").
//
// diag.star_light_log = N (seconds, 0 off): every N seconds, for one frame, the flat profile copies the pixel-shader constant buffers of the
// game's sun draw (the deferred directional-light pair kFlatDeferredHdrVs/Ps, slots 0..7; docs/sun-switch-re-2026-11-06.md, Q7: the light
// colour lives in that draw's buffers) and the star quad's PS b2 (kFlatStarQuadBVs/Ps, b2[0].yzw: the star's own peak-normalised colour,
// docs/star-glow-2026-10-22.md). A few frames later it maps them, logs the star colour and every sun row whose colour has the star's chromaticity,
// and appends all the bytes to edvr_logs/star_light_<stamp>.bin for the offline scan. Read only: the game's state and output are untouched.
// Owner context, flat profile only. One relaxed load per draw while off.
#include <atomic>
#include <cstdint>

struct ID3D11DeviceContext;

namespace edvr {

extern std::atomic<bool> g_starLightDiagFrame;   // this frame is a sample frame
inline bool starLightDiagCapturing() noexcept { return g_starLightDiagFrame.load(std::memory_order_relaxed); }

// At a flat draw on the owner context, before any substitution, with the game's shader hashes.
void starLightDiagDraw(ID3D11DeviceContext* ctx, uint64_t vs, uint64_t ps);
// The frame boundary (owner context): reads the key, maps a sample once its copies have run, starts the next sample when due.
void starLightDiagFrameBoundary(ID3D11DeviceContext* ctx);

}  // namespace edvr
