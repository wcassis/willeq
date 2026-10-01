#pragma once
// The per-pixel player light: the GLSL lines every player-light program
// shares, and the attenuation LUT they sample (B967 #285, B968 #286).
//
// ONE definition, used by the production shaders
// (src/client/graphics/zone_shader_sources_gles2.h, zone_shader.cpp) and by the
// probe that pins them (tools/hwcaps/gles2_player_light_probe.cpp), so the
// probe draws the shipped math, not a copy of it.
//
// The light vector is formed in the VERTEX shader (B967). Mali-400's fragment
// ALU is FP16 whatever the precision qualifier says (B965), and so is the
// varying a fragment shader reads (B969). The old FS formed
//   pLv = uPlayerLightPos - vWorldPos
// from a world-coordinate varying: at |x| ~ 7,000-9,500 one FP16 step is 4-8
// world units, so the light moved in visible bands on BOTH GPUs, and the two
// GPUs put the band edges in different places (Pi vs x86 timorous_f2: 3,175 px
// > 32). The VS (FP32) now subtracts the two world positions and passes the
// small, RELATIVE difference, pre-scaled by kPlayerLightVecScale:
//   vPlayerLightVec = (uPlayerLightPos - worldPos) / 64
// so the FS squares d/64 instead of d. FP16 holds d^2/4096 up to d = 16,384
// world units (65,504 = FP16's max), far past the LUT's end (<= 4,096 units).
//
// The light's shape (B968): the term is the per-vertex zone-light family
// (the VS loop over uLightPos[1..7], and slot 0 in the lightweight programs):
//   pLight = color * max(N . L^, 0) * A(d),  A(d) = 1 / (c + l*d + q*d^2 + 1e-4)
// - N . L^ = dot(N, v) * inversesqrt(dot(v, v)), exact, no LUT (the old LUT's
//   "1/d" channel stopped at d = 64 while |pLv| kept growing, so a wall facing
//   the light got BRIGHTER with distance: 17 at 64 units, 105 at 400).
// - A(d) comes from the LUT, now indexed LINEARLY IN d (coordinate d / R), so
//   one 256-texel table covers the whole range R of the light at ~1.4-unit
//   steps: A is smooth in d (flat at d = 0), unlike 1/d, which was why the old
//   table had to be indexed by d^2 and stop at 64 units.
// - R is where A falls to 1/255 (c + l*R + q*R^2 = 255), the same threshold
//   P07d's radius demotion uses. The last texel is 0, so past R the light is
//   exactly 0 and the coordinate's clamp (B965) returns that 0.
// So pLight is monotonic non-increasing in d on any surface whose N . L^ does
// not grow with distance, and it is 0 from R on.
//
// FP16 guards in the FS (B965's Mali rule: never hand texture2D() a value that
// can reach inf, and nothing in the FS may rely on more than FP16):
// - inversesqrt(clamp(d2, 2^-14, 60000)): 2^-14 is FP16's smallest normal
//   (d = 0.5 world unit), so no 1/0; 60,000 keeps an infinite d2 (only past
//   16,384 units) finite there, and clamp(inf * x, 0, 1) is the last texel (0).
// - clamp(d2 * invD * uLUTScale, 0, 1) on the LUT coordinate (B965).

#include <cmath>
#include <cstdint>
#include <cstring>

namespace EQT {
namespace Graphics {
namespace PlayerLight {

// VS pre-scale of the light vector (1/64): see the header comment.
constexpr float kVecScale = 1.0f / 64.0f;
// The LUT's range R is clamped to this interval (units). The lightstone's own
// R is 73..366 units (radius 20..100, q = 19/radius^2: R = radius * sqrt(254/19)).
constexpr float kMinRange = 64.0f;
constexpr float kMaxRange = 4096.0f;
constexpr int kLutSize = 256;
// Attenuation at R: the light ends where it falls below one 8-bit step.
constexpr float kRangeAttenDenominator = 255.0f;

// The GLSL lines. VS: needs `uniform vec3 uPlayerLightPos;`, `vec3 worldPos`
// in scope and `varying vec3 vPlayerLightVec;`. FS: needs the varying,
// `uAttenLUT`, `uLUTScale`, `uPlayerLightColor`; NORMAL names the normal
// (vWorldNormal in production, a uniform in the probe). Defines `lut`, `pLn`
// and `pLight` for the caller.
#define EQT_PLAYER_LIGHT_VS_LINE \
    "    vPlayerLightVec = (uPlayerLightPos - worldPos) * 0.015625;\n"

#define EQT_PLAYER_LIGHT_FS_TERM(NORMAL)                                                       \
    "    float d2 = dot(vPlayerLightVec, vPlayerLightVec);\n"                                 \
    "    float invD = inversesqrt(clamp(d2, 0.00006103515625, 60000.0));\n"                  \
    "    vec4 lut = texture2D(uAttenLUT, vec2(clamp(d2 * invD * uLUTScale, 0.0, 1.0), 0.5));\n" \
    "    float pLn = max(dot(" #NORMAL ", vPlayerLightVec), 0.0) * invD;\n"                    \
    "    vec3 pLight = uPlayerLightColor * (pLn * lut.a);\n"

// The light's range R: the largest d with c + l*d + q*d^2 <= 255, clamped to
// [kMinRange, kMaxRange] (a light with no falloff gets kMaxRange).
inline float lutRange(float c, float l, float q) {
    const float target = kRangeAttenDenominator - c;
    float r = kMaxRange;
    if (target <= 0.0f) {
        r = kMinRange;
    } else if (q > 0.0f) {
        r = (-l + std::sqrt(l * l + 4.0f * q * target)) / (2.0f * q);
    } else if (l > 0.0f) {
        r = target / l;
    }
    if (!(r >= kMinRange)) r = kMinRange;  // also catches NaN
    if (r > kMaxRange) r = kMaxRange;
    return r;
}

// The uniform uLUTScale: the FS forms d2 * invD = d / 64, the LUT is indexed by
// d / R.
inline float lutScale(float range) { return (1.0f / kVecScale) / range; }

// Attenuation at texel i of a table of range R: texel centres sit at
// d = R * (i + 0.5) / 256; the last texel is 0.
inline float lutAtten(int i, float c, float l, float q, float range) {
    if (i >= kLutSize - 1) return 0.0f;
    const float d = range * (static_cast<float>(i) + 0.5f) / static_cast<float>(kLutSize);
    return 1.0f / (c + l * d + q * d * d + 0.0001f);
}

inline uint16_t floatToHalf(float f) {
    uint32_t bits;
    std::memcpy(&bits, &f, sizeof(bits));
    const uint32_t sign = (bits >> 16) & 0x8000;
    const int32_t exp = static_cast<int32_t>((bits >> 23) & 0xFF) - 127;
    const uint32_t mant = bits & 0x7FFFFF;
    if (exp > 15) return static_cast<uint16_t>(sign | 0x7C00);  // inf
    if (exp < -14) return static_cast<uint16_t>(sign);          // zero (denormals flushed)
    return static_cast<uint16_t>(sign | ((exp + 15) << 10) | (mant >> 13));
}

// Fill a 256x1 LUMINANCE_ALPHA half-float table: L = A = attenuation (L is
// read only by /shaderdebug 3).
inline void fillLut(uint16_t* data, float c, float l, float q, float range) {
    for (int i = 0; i < kLutSize; ++i) {
        const uint16_t h = floatToHalf(lutAtten(i, c, l, q, range));
        data[i * 2] = h;
        data[i * 2 + 1] = h;
    }
}

}  // namespace PlayerLight
}  // namespace Graphics
}  // namespace EQT
