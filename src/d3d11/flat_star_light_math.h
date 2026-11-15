#pragma once
// visual.star_light_colour's rules (flat_star_light.h): pure, no Windows API, so a rig can drive them.
#include <cmath>

namespace edvr { namespace starlight {

// The game's own full light tint, measured on arrival at five stars (2026-11-15, diag.star_light_log), against the star's disc colour's
// blue / red ratio, which orders the classes by temperature. Tints are scaled so red is 1.
//   class  disc b/r  tint (r, g, b)
//   M      0.052     (1, 0.525, 0.176)
//   K      0.172     (1, 0.681, 0.260)   measured at 3.5 Ls, not on arrival: may be a little too pale
//   G      0.345     (1, 0.753, 0.400)
//   F      0.950     (1, 0.980, 0.761)
//   A      2.012     (1, 1.086, 1.385)
struct Anchor { float ratio, g, b; };
constexpr Anchor kAnchors[] = {
    {0.052f, 0.525f, 0.176f}, {0.172f, 0.681f, 0.260f}, {0.345f, 0.753f, 0.400f}, {0.950f, 0.980f, 0.761f}, {2.012f, 1.086f, 1.385f},
};
constexpr int kAnchorCount = sizeof(kAnchors) / sizeof(kAnchors[0]);

// The tint for a disc colour, red 1, interpolated in log(ratio) and held at the ends. False when the disc is no colour.
inline bool tintFor(const float disc[3], float out[3]) {
    if (!(disc[0] > 0.f) || !(disc[2] >= 0.f) || !std::isfinite(disc[0]) || !std::isfinite(disc[2])) return false;
    const float x = std::log(std::fmax(disc[2] / disc[0], 1e-4f));
    out[0] = 1.f;
    if (x <= std::log(kAnchors[0].ratio)) { out[1] = kAnchors[0].g; out[2] = kAnchors[0].b; return true; }
    for (int i = 1; i < kAnchorCount; ++i) {
        const float x1 = std::log(kAnchors[i].ratio);
        if (x <= x1) {
            const float x0 = std::log(kAnchors[i - 1].ratio), t = (x - x0) / (x1 - x0);
            out[1] = kAnchors[i - 1].g + t * (kAnchors[i].g - kAnchors[i - 1].g);
            out[2] = kAnchors[i - 1].b + t * (kAnchors[i].b - kAnchors[i - 1].b);
            return true;
        }
    }
    out[1] = kAnchors[kAnchorCount - 1].g;
    out[2] = kAnchors[kAnchorCount - 1].b;
    return true;
}

inline float luminance(const float c[3]) { return 0.2126f * c[0] + 0.7152f * c[1] + 0.0722f * c[2]; }

// The light the sun draw gets: the game's light `light` (colour times brightness) with its luminance kept and its colour moved
// `strength` (0..1) of the way to the tint. A light that is not a positive finite colour, or no tint, is left as it is.
inline void apply(float light[3], const float tint[3], float strength) {
    if (!(strength > 0.f)) return;
    for (int k = 0; k < 3; ++k) if (!std::isfinite(light[k]) || light[k] < 0.f) return;
    const float lum = luminance(light), tl = luminance(tint);
    if (!(lum > 0.f) || !(tl > 0.f)) return;
    const float s = std::fmin(strength, 1.f);
    for (int k = 0; k < 3; ++k) light[k] = light[k] + s * (tint[k] * (lum / tl) - light[k]);
}

// The game's light far from the star: the tint mixed toward this white (2026-11-15 flights; red 1).
constexpr float kWhite[3] = {1.f, 0.969f, 0.973f};

// How well `tint` explains the game's light: the light's colour (red 1) lies on the line from the tint toward white when that star
// is the light's source, at any distance. The distance from that segment; a large number when the light is no colour.
inline float misfit(const float light[3], const float tint[3]) {
    if (!(light[0] > 0.f)) return 1e9f;
    const float c[3] = {1.f, light[1] / light[0], light[2] / light[0]};
    float dw[3], dc[3], dd = 0.f, dot = 0.f;
    for (int k = 0; k < 3; ++k) { dw[k] = kWhite[k] - tint[k]; dc[k] = c[k] - tint[k]; dd += dw[k] * dw[k]; dot += dc[k] * dw[k]; }
    const float f = dd > 0.f ? std::fmin(std::fmax(dot / dd, 0.f), 1.f) : 0.f;
    float e = 0.f;
    for (int k = 0; k < 3; ++k) { const float r = dc[k] - f * dw[k]; e += r * r; }
    return std::sqrt(e);
}

}}  // namespace edvr::starlight
