#pragma once
// visual.star_light_colour (2026-11-15): the sun's light in the star's colour, far from the star too.
// 2026-11-16: the same change also reaches the light buffers of the forward and atmosphere draws and the planetary ring dust's light (below),
// and the setting can be scaled per star toward equal visible colour (flat_star_light_math.h equalStrength; off since 2026-11-16, kEqualVisibleColour).
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
// The light buffers (2026-11-16, found by a scan of every draw's buffers at Taygeta): the forward and atmosphere draws (planet skies among
// them) read the sun's light from a small structured buffer (48 or 96 bytes, PS t1/t2, the light's colour times brightness at byte 0 of each
// 48-byte light) that the sun draw's row 40 does not reach. Up to kStarLightLightBuffers of them are watched and re-tinted by the same
// per-channel factor as row 40.
constexpr unsigned kStarLightLightBuffers = 8;
extern std::atomic<void*> g_starLightLightBuffer[kStarLightLightBuffers];
// The sun buffer, a watched light buffer or the dust's b1: a write the fix re-tints or keeps.
// The shared constants the ring dust reads at b1 (below), watched so its writes can be kept for the dust's tinted copy.
extern std::atomic<void*> g_starLightDustBuffer;
inline bool starLightIsSunBuffer(const void* res) noexcept {
    if (!res) return false;
    if (res == g_starLightSunBuffer.load(std::memory_order_relaxed)) return true;
    if (res == g_starLightDustBuffer.load(std::memory_order_relaxed)) return true;
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
// The ring dust's light (2026-11-16): the dust (volume 203DF517/EEAAC839, ribbon B12F7A61/42AC0CAC) takes its light colour from row 293 of the
// shared constants at b1, which nothing else of the fix reaches (found by a halving search of the draws and a probe of the dust draw's
// inputs). In flight (Status.json: seated, not docked, not on foot) its two draws get a copy of b1 with that row given the sun light's
// per-channel change; the game's b1 is put back after the draw.
struct StarLightHazeRestore {
    bool bound = false;
    ID3D11Buffer* ps = nullptr;   // held references to put back at VS and PS b1
    ID3D11Buffer* vs = nullptr;
};
void starLightHazeBind(ID3D11DeviceContext* ctx, uint64_t vs, uint64_t ps, StarLightHazeRestore* r);
void starLightHazeRestore(ID3D11DeviceContext* ctx, StarLightHazeRestore* r);
class StarLightHazeScope {
public:
    StarLightHazeScope(ID3D11DeviceContext* ctx, bool armed, uint64_t vs, uint64_t ps) : ctx_(ctx) {
        if (armed) starLightHazeBind(ctx, vs, ps, &restore_);
    }
    ~StarLightHazeScope() { if (restore_.bound) starLightHazeRestore(ctx_, &restore_); }
    StarLightHazeScope(const StarLightHazeScope&) = delete;
    StarLightHazeScope& operator=(const StarLightHazeScope&) = delete;
private:
    ID3D11DeviceContext* ctx_;
    StarLightHazeRestore restore_;
};
// The frame boundary (owner context): reads the setting, maps the star colour copy.
void starLightFrameBoundary(ID3D11DeviceContext* ctx);

}  // namespace edvr
