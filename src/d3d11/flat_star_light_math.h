#pragma once
// visual.star_light_colour's rules (flat_star_light.h): pure, no Windows API, so a rig can drive them.
#include <cmath>

namespace edvr { namespace starlight {

// The game's own full light tint, read on arrival (the light is most saturated just after the jump) at the stars flown on
// 2026-11-15 (diag.star_light_log), with each star's disc colour (the star quad's PS b2[0].yzw), all scaled so red is 1.
// The game holds one light colour per class (three K stars with disc blue/red 0.141 to 0.172 gave the same tint), and the class is
// not ordered by one number: the T dwarf's disc has the F star's blue/red but half its green. So a disc takes the tint of the
// measured star nearest to it in (log green/red, log blue/red): a lookup whose class borders fall halfway between measured stars.
//   class            disc (g, b)        tint (g, b)
//   L (main star)    (0.184, ~0)        (0.184, 0.208)   pink: its disc has no blue
//   M                (0.227, 0.052)     (0.525, 0.176)
//   K (three stars)  (0.421, 0.141)     (0.680, 0.257)
//                    (0.431, 0.152)     (0.680, 0.257)
//                    (0.448, 0.172)     (0.681, 0.260)
//   G                (0.567, 0.345)     (0.753, 0.400)
//   F                (1.125, 0.951)     (0.980, 0.761)
//   A                (1.610, 2.012)     (1.086, 1.385)
//   DA white dwarf   (1.301, 4.008)     (1.206, 1.417)
//   B                (0.227, 5.841)     (2.000, 5.500)
//   T (main star)    (0.561, 1.063)     (0.210, 0.618)   magenta
// Not measured: Y dwarfs as a main star (a Y companion, disc (1.161, 3.482), never lit the ship), O and Wolf-Rayet stars: they take
// the nearest measured star's tint.
struct Anchor { float dg, db, g, b; };
constexpr Anchor kAnchors[] = {
    {0.184f, 0.f, 0.184f, 0.208f},     {0.227f, 0.052f, 0.525f, 0.176f}, {0.421f, 0.141f, 0.680f, 0.257f}, {0.431f, 0.152f, 0.680f, 0.257f},
    {0.448f, 0.172f, 0.681f, 0.260f},  {0.567f, 0.345f, 0.753f, 0.400f}, {1.125f, 0.951f, 0.980f, 0.761f}, {1.610f, 2.012f, 1.086f, 1.385f},
    {1.301f, 4.008f, 1.206f, 1.417f},  {0.227f, 5.841f, 2.000f, 5.500f}, {0.561f, 1.063f, 0.210f, 0.618f},
};
constexpr int kAnchorCount = sizeof(kAnchors) / sizeof(kAnchors[0]);
constexpr float kFloor = 0.005f;   // a disc channel at 0 (the L dwarf's blue) is read as this much of red

// The tint for a disc colour, red 1: the nearest measured star's. False when the disc is no colour.
inline bool tintFor(const float disc[3], float out[3]) {
    for (int k = 0; k < 3; ++k) if (!std::isfinite(disc[k]) || disc[k] < 0.f) return false;
    if (!(disc[0] > 0.f)) return false;
    const float lg = std::log(std::fmax(disc[1] / disc[0], kFloor)), lb = std::log(std::fmax(disc[2] / disc[0], kFloor));
    int best = 0;
    float bestD = 1e30f;
    for (int i = 0; i < kAnchorCount; ++i) {
        const float dg = lg - std::log(std::fmax(kAnchors[i].dg, kFloor)), db = lb - std::log(std::fmax(kAnchors[i].db, kFloor));
        const float d = dg * dg + db * db;
        if (d < bestD) { bestD = d; best = i; }
    }
    out[0] = 1.f;
    out[1] = kAnchors[best].g;
    out[2] = kAnchors[best].b;
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
