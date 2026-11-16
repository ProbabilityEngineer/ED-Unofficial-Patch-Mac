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
struct ID3D11ShaderResourceView;
struct ID3D11Buffer;
typedef unsigned int UINT;

namespace edvr {

extern std::atomic<bool> g_starLightLive;          // the setting is above 0 in the flat profile
extern std::atomic<void*> g_starLightSunBuffer;    // the sun draw's PS b2 buffer, learned at the draw (null: none yet)
extern float g_starLightGameLight[3];              // the game's own light at the last write, before the fix (owner thread)
inline bool starLightLive() noexcept { return g_starLightLive.load(std::memory_order_relaxed); }
// TEST (2026-11-16, the copy scan at Taygeta): the forward and atmosphere draws read the sun's light from a small structured buffer (48 or
// 96 bytes, PS t1/t2, the light's colour times brightness at byte 0 of each 48-byte light) that the sun draw's row 40 does not reach.
// Up to kStarLightLightBuffers of them are watched and re-tinted by the same per-channel factor.
constexpr unsigned kStarLightLightBuffers = 8;
extern std::atomic<void*> g_starLightLightBuffer[kStarLightLightBuffers];
// The sun buffer or a watched light buffer: a write the fix re-tints.
// TEMPORARY (the haze probe, below): the shared frame constants the ring dust reads at b1, watched so a copy can be altered for its draw.
extern std::atomic<void*> g_starLightProbeBuffer;
extern std::atomic<bool> g_hazeProbeOn;
inline bool hazeProbeOn() noexcept { return g_hazeProbeOn.load(std::memory_order_relaxed); }
inline bool starLightIsSunBuffer(const void* res) noexcept {
    if (!res) return false;
    if (res == g_starLightSunBuffer.load(std::memory_order_relaxed)) return true;
    if (res == g_starLightProbeBuffer.load(std::memory_order_relaxed)) return true;
    for (const auto& b : g_starLightLightBuffer) if (res == b.load(std::memory_order_relaxed)) return true;
    return false;
}

// At a flat draw on the owner context, before any substitution: learns the sun draw's buffer, reads the star quad's colour.
void starLightDraw(ID3D11DeviceContext* ctx, uint64_t vs, uint64_t ps);
// The game's writes of the sun buffer: Map remembers the pointer, Unmap (before the real Unmap) re-tints row 40 in place.
void starLightMapped(ID3D11Resource* res, void* data);
void starLightUnmap(ID3D11Resource* res);
// UpdateSubresource of the sun buffer: returns the data to pass on (a re-tinted copy, or `data` itself).
const void* starLightUpdate(ID3D11Resource* res, const void* data, uint32_t left, uint32_t right, bool boxed);
// TEMPORARY (the copy scan, flat_star_light_diag.h): once per arrival, ~1.5 s after the first light, the game's own (unwhitened) light
// colour, red 1, for the diagnostic to look for in every draw's buffers. True once per arrival.
bool starLightTakeScanRequest(float chroma[3]) noexcept;
// TEMPORARY (the copy scan): the system's full tint as the fix holds it now (learned on arrival, else predicted), red 1. False: none.
bool starLightSystemTint(float tint[3]) noexcept;
// TEMPORARY (2026-11-16): the haze probe, diag.haze_probe = on. For the ring dust's volume draw only, one input at a time is blanked (PS t0..t7,
// PS b0..b4) or the shared frame constants' light rows 26, 159 or 293 are made pure red; Ctrl+F9 next, Ctrl+F11 back, Ctrl+F12 none.
struct HazeProbeRestore {
    int kind = 0;                                  // 0 nothing, 1 a PS view, 2 a PS constant buffer, 3 b1 at VS and PS
    UINT slot = 0;
    ID3D11ShaderResourceView* srv = nullptr;      // held references to put back
    ID3D11Buffer* ps = nullptr;
    ID3D11Buffer* vs = nullptr;
};
void starLightHazeProbeBind(ID3D11DeviceContext* ctx, uint64_t vs, uint64_t ps, HazeProbeRestore* r);
// The ring dust's light (2026-11-16): the dust (volume 203DF517/EEAAC839, ribbon B12F7A61/42AC0CAC) takes its light colour from row 293 of the
// shared constants at b1, which nothing else of the fix reaches. In flight (Status.json: seated, not docked, not on foot) its two draws get a
// copy of b1 with that row given the sun light's per-channel change. Restored by starLightHazeProbeRestore.
void starLightHazeBind(ID3D11DeviceContext* ctx, uint64_t vs, uint64_t ps, HazeProbeRestore* r);
void starLightHazeProbeRestore(ID3D11DeviceContext* ctx, HazeProbeRestore* r);
class StarLightHazeScope {
public:
    StarLightHazeScope(ID3D11DeviceContext* ctx, bool armed, uint64_t vs, uint64_t ps) : ctx_(ctx) {
        if (!armed) return;
        starLightHazeBind(ctx, vs, ps, &tint_);
        if (hazeProbeOn()) starLightHazeProbeBind(ctx, vs, ps, &probe_);
    }
    ~StarLightHazeScope() {
        if (probe_.kind) starLightHazeProbeRestore(ctx_, &probe_);
        if (tint_.kind) starLightHazeProbeRestore(ctx_, &tint_);
    }
    StarLightHazeScope(const StarLightHazeScope&) = delete;
    StarLightHazeScope& operator=(const StarLightHazeScope&) = delete;
private:
    ID3D11DeviceContext* ctx_;
    HazeProbeRestore tint_, probe_;
};
// The frame boundary (owner context): reads the setting, maps the star colour copy.
void starLightFrameBoundary(ID3D11DeviceContext* ctx);

}  // namespace edvr
