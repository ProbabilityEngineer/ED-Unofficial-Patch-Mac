#pragma once
// visual.star_light_colour's rules (flat_star_light.h): pure, no Windows API, so a rig can drive them.
#include <cmath>
#include <cstdint>

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

// ---- Option 1 (2026-11-16): about the same amount of visible colour from every star at one setting. ----
// The owner's preferences in one evening: about 65% for a K star, about 55% for Taygeta (B). Measured as Oklab chroma (a perceptual
// colour space) a full B tint is 1.7 times as colourful as a full K tint, yet the preferred B amount was only a little less: a partial
// equalisation, strength * (reference chroma / the tint's chroma)^0.3, fits both, with the K tint as the reference (K stars take the setting
// as it is; the bluest and reddest stars a little less, the pale F and A stars more, capped at the full tint). A first fit to two points.
constexpr float kChromaRef = 0.116f;        // the K tint's Oklab chroma (mixed toward kWhite at the white's luminance)
constexpr float kChromaPower = 0.3f;

inline float oklabChroma(const float c[3]) {
    const float l = 0.4122214708f * c[0] + 0.5363325363f * c[1] + 0.0514459929f * c[2];
    const float m = 0.2119034982f * c[0] + 0.6806995451f * c[1] + 0.1073969566f * c[2];
    const float s = 0.0883024619f * c[0] + 0.2817188376f * c[1] + 0.6299787005f * c[2];
    const float l3 = std::cbrt(std::fmax(l, 0.f)), m3 = std::cbrt(std::fmax(m, 0.f)), s3 = std::cbrt(std::fmax(s, 0.f));
    const float a = 1.9779984951f * l3 - 2.4285922050f * m3 + 0.4505937099f * s3;
    const float b = 0.0259040371f * l3 + 0.7827717662f * m3 - 0.8086757660f * s3;
    return std::sqrt(a * a + b * b);
}

// The tint's own chroma: the tint at the white's luminance.
inline float tintChroma(const float tint[3]) {
    const float tl = luminance(tint), wl = luminance(kWhite);
    if (!(tl > 0.f)) return 0.f;
    const float c[3] = {tint[0] * wl / tl, tint[1] * wl / tl, tint[2] * wl / tl};
    return oklabChroma(c);
}

// The setting (0..1) as the strength apply() takes for this tint.
inline float equalStrength(const float tint[3], float setting) {
    if (!(setting > 0.f)) return 0.f;
    const float c = tintChroma(tint);
    if (!(c > 1e-4f)) return std::fmin(setting, 1.f);
    return std::fmin(setting * std::pow(kChromaRef / c, kChromaPower), 1.f);
}

// ---- DXGI_FORMAT_R11G11B10_FLOAT, for the ring dust's ambient cube (flat_star_light.h). No sign; 5-bit exponent, bias 15. ----
inline float decodeSmallFloat(uint32_t v, int mantBits) {
    const uint32_t e = (v >> mantBits) & 0x1Fu, m = v & ((1u << mantBits) - 1u);
    if (e == 0) return std::ldexp(static_cast<float>(m), -14 - mantBits);
    if (e == 31) return m ? 0.f : 65000.f;   // NaN reads as 0, infinity as large
    return std::ldexp(static_cast<float>(m | (1u << mantBits)), static_cast<int>(e) - 15 - mantBits);
}
inline uint32_t encodeSmallFloat(float f, int mantBits) {
    if (!(f > 0.f)) return 0u;                                  // negatives and NaN to 0
    const float maxV = std::ldexp(static_cast<float>((2u << mantBits) - 1u), 15 - mantBits);   // largest finite
    if (f >= maxV) return (30u << mantBits) | ((1u << mantBits) - 1u);
    int e = 0;
    const float fr = std::frexp(f, &e);                          // f = fr * 2^e, fr in [0.5, 1)
    int be = e - 1 + 15;                                         // biased exponent of 1.xxx * 2^(e-1)
    if (be <= 0) {                                               // denormal
        const uint32_t m = static_cast<uint32_t>(std::lround(std::ldexp(f, 14 + mantBits)));
        return m >= (1u << mantBits) ? (1u << mantBits) : m;   // rounding up to the smallest normal
    }
    uint32_t m = static_cast<uint32_t>(std::lround((fr * 2.f - 1.f) * static_cast<float>(1u << mantBits)));
    if (m >= (1u << mantBits)) { m = 0; ++be; }
    if (be >= 31) return (30u << mantBits) | ((1u << mantBits) - 1u);
    return (static_cast<uint32_t>(be) << mantBits) | m;
}
inline void decodeR11G11B10(uint32_t p, float out[3]) {
    out[0] = decodeSmallFloat(p & 0x7FFu, 6);
    out[1] = decodeSmallFloat((p >> 11) & 0x7FFu, 6);
    out[2] = decodeSmallFloat((p >> 22) & 0x3FFu, 5);
}
inline uint32_t encodeR11G11B10(const float c[3]) {
    return encodeSmallFloat(c[0], 6) | (encodeSmallFloat(c[1], 6) << 11) | (encodeSmallFloat(c[2], 5) << 22);
}

}}  // namespace edvr::starlight
