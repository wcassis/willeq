// B967/B968 (GitLab #285/#286): the per-pixel player light's attenuation LUT
// and the FP16 headroom of its VS-formed vector. GL-free; the rendered curve is
// pinned by the PlayerLightProbeMonotonic ctest (gles2_player_light_probe
// --assert, llvmpipe).
#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "client/graphics/player_light_shader.h"

namespace PL = EQT::Graphics::PlayerLight;

namespace {

// The lightstone's attenuation for TypeToLevel level 1..10, exactly as
// renderer_entities.cpp sets it: radius 20 + level*8, (1, 0, 19/radius^2).
void lightstoneAtten(int level, float out[3]) {
    const float radius = 20.0f + (static_cast<float>(level) / 10.0f) * 80.0f;
    out[0] = 1.0f;
    out[1] = 0.0f;
    out[2] = 19.0f / (radius * radius);
}

float halfToFloat(uint16_t h) {
    const uint32_t sign = (h & 0x8000u) << 16;
    const uint32_t exp = (h >> 10) & 0x1F;
    const uint32_t mant = h & 0x3FF;
    uint32_t bits;
    if (exp == 0) bits = sign;  // the writer flushes denormals
    else if (exp == 31) bits = sign | 0x7F800000u | (mant << 13);
    else bits = sign | ((exp - 15 + 127) << 23) | (mant << 13);
    float f;
    std::memcpy(&f, &bits, sizeof(f));
    return f;
}

}  // namespace

// B968: the table never rises with distance, and it ends at exactly 0, so the
// clamped coordinate past R reads 0 rather than a constant glow.
TEST(PlayerLightLut, MonotonicNonIncreasingAndZeroAtTheEnd) {
    for (int level = 1; level <= 10; ++level) {
        float a[3];
        lightstoneAtten(level, a);
        const float range = PL::lutRange(a[0], a[1], a[2]);
        std::vector<uint16_t> lut(PL::kLutSize * 2);
        PL::fillLut(lut.data(), a[0], a[1], a[2], range);
        float prev = halfToFloat(lut[1]);
        EXPECT_GT(prev, 0.99f) << "level " << level;  // A(~0) = 1/c
        for (int i = 1; i < PL::kLutSize; ++i) {
            const float v = halfToFloat(lut[i * 2 + 1]);
            EXPECT_LE(v, prev) << "level " << level << " texel " << i;
            EXPECT_NEAR(halfToFloat(lut[i * 2]), (i + 0.5f) / PL::kLutSize, 1e-3f)
                << "L is the texel's coordinate d/R (/shaderdebug 3)";
            prev = v;
        }
        EXPECT_EQ(halfToFloat(lut[(PL::kLutSize - 1) * 2 + 1]), 0.0f) << "level " << level;
    }
}

// The range is where the per-vertex family falls to 1/255, the same threshold
// P07d's radius demotion uses; the lightstone's own is 73..366 units.
TEST(PlayerLightLut, RangeIsTheOneIn255PointAndInsideTheClamp) {
    for (int level = 1; level <= 10; ++level) {
        float a[3];
        lightstoneAtten(level, a);
        const float range = PL::lutRange(a[0], a[1], a[2]);
        const float radius = 20.0f + (static_cast<float>(level) / 10.0f) * 80.0f;
        EXPECT_NEAR(range, radius * std::sqrt(254.0f / 19.0f), 0.01f) << "level " << level;
        EXPECT_NEAR(a[0] + a[1] * range + a[2] * range * range, 255.0f, 0.05f) << "level " << level;
        EXPECT_GE(range, PL::kMinRange);
        EXPECT_LE(range, PL::kMaxRange);
    }
    EXPECT_EQ(PL::lutRange(1.0f, 0.0f, 0.0f), PL::kMaxRange) << "no falloff: the cap";
    EXPECT_EQ(PL::lutRange(300.0f, 0.0f, 1.0f), PL::kMinRange) << "already dim at d=0";
    EXPECT_NEAR(PL::lutRange(1.0f, 2.0f, 0.0f), 127.0f, 1e-3f) << "linear only";
    // uLUTScale maps the FS's d/64 onto d/R.
    EXPECT_FLOAT_EQ(PL::lutScale(256.0f) * (128.0f / 64.0f), 0.5f);
}

// Each texel is the per-vertex family 1/(c + l*d + q*d^2 + 1e-4) at its centre
// -- the VS point-light loop's own term -- to FP16 precision.
TEST(PlayerLightLut, TexelsAreThePerVertexFamily) {
    float a[3];
    lightstoneAtten(9, a);
    const float range = PL::lutRange(a[0], a[1], a[2]);
    std::vector<uint16_t> lut(PL::kLutSize * 2);
    PL::fillLut(lut.data(), a[0], a[1], a[2], range);
    for (int i = 0; i < PL::kLutSize - 1; ++i) {
        const float d = range * (i + 0.5f) / PL::kLutSize;
        const float want = 1.0f / (a[0] + a[1] * d + a[2] * d * d + 0.0001f);
        EXPECT_NEAR(halfToFloat(lut[i * 2 + 1]), want, want * 1e-3f + 1e-7f) << "texel " << i;
    }
}

// B967: the VS pre-scale keeps every FS quantity inside FP16 (max 65,504):
// the squared scaled vector reaches the shader's 60,000 clamp only past
// sqrt(60000) * 64 = 15,677 units, beyond the LUT's largest possible range and beyond the
// largest era zone's diagonal (refactor/B967/README.md), and the
// varying itself (|v| = d/64) cannot overflow for any world coordinate an
// EQ zone holds.
TEST(PlayerLightLut, VsPrescaleKeepsTheFsInsideFp16) {
    EXPECT_FLOAT_EQ(PL::kVecScale, 0.015625f);
    const float maxD2BeforeClamp = std::sqrt(60000.0f) / PL::kVecScale;  // world units
    EXPECT_GT(maxD2BeforeClamp, PL::kMaxRange * 3.0f);
    // The shipped GLSL carries the same 1/64 and the same guards.
    const std::string vs = EQT_PLAYER_LIGHT_VS_LINE;
    const std::string fs = EQT_PLAYER_LIGHT_FS_TERM(vWorldNormal);
    EXPECT_NE(vs.find("* 0.015625"), std::string::npos);
    EXPECT_NE(fs.find("inversesqrt(clamp(d2, 0.00006103515625, 60000.0))"), std::string::npos);
    EXPECT_NE(fs.find("clamp(d2 * invD * uLUTScale, 0.0, 1.0)"), std::string::npos) << "B965's clamp";
    EXPECT_EQ(fs.find("uPlayerLightPos"), std::string::npos) << "the FS never sees a world position";
    EXPECT_EQ(fs.find("if"), std::string::npos) << "branchless (Mali-400 FS rule)";
    // 2^-14 is FP16's smallest normal: the guard itself is representable.
    EXPECT_EQ(halfToFloat(PL::floatToHalf(0.00006103515625f)), 0.00006103515625f);
    EXPECT_EQ(halfToFloat(PL::floatToHalf(60000.0f)), 60000.0f);
}
