// T361 (board B067): the vision type -> global ambient mapping.
//
// Before T361 `visionType` had exactly two consumers tree-wide, both scaling
// the zone's data-only point lights; `ZoneShader::setAmbientColor` was never
// called from anything vision related. T358 measured the live consequence: a
// full Ultravision buff at forced 22:00 moved a world crop by +4.9% while the
// day/night cycle moved it 4x. The official Titanium client, measured at the
// same spot in the same zone at the same forced time (T361 Leg A,
// review/T361/README.md), lifts the same crop x1.554 in linear light for
// Ultravision and x1.253 for Infravision, with Infravision's lift red-biased.
//
// These cases pin the shape of that mapping, not the pixels: Normal changes
// nothing, Ultravision imposes the largest floor, Infravision a smaller and
// red-biased one, the floor never DARKENS anything, and daylight is above
// every floor so a vision buff is invisible at noon.

#include <gtest/gtest.h>

#include "client/graphics/vision_ambient.h"
#include "client/graphics/vision_type.h"

using EQT::Graphics::AmbientRGB;
using EQT::Graphics::VisionType;
using EQT::Graphics::applyVisionAmbientFloor;
using EQT::Graphics::racialBaseVision;
using EQT::Graphics::visionAmbientFloor;
using EQT::Graphics::visionFrameRatios;

namespace {

// The two ambients IrrlichtRenderer::updateTimeOfDay() computes.
constexpr AmbientRGB kNight{0.08f, 0.08f, 0.15f};
constexpr AmbientRGB kDay{0.5f, 0.5f, 0.5f};

float mean(const AmbientRGB& a) { return (a.r + a.g + a.b) / 3.0f; }

}  // namespace

TEST(VisionAmbientTest, NormalImposesNoFloor) {
    const AmbientRGB floor = visionAmbientFloor(VisionType::Normal);
    EXPECT_FLOAT_EQ(floor.r, 0.0f);
    EXPECT_FLOAT_EQ(floor.g, 0.0f);
    EXPECT_FLOAT_EQ(floor.b, 0.0f);

    const AmbientRGB out = applyVisionAmbientFloor(VisionType::Normal, kNight);
    EXPECT_FLOAT_EQ(out.r, kNight.r);
    EXPECT_FLOAT_EQ(out.g, kNight.g);
    EXPECT_FLOAT_EQ(out.b, kNight.b);
}

// The defect B067 names: vision must reach the global ambient at all.
TEST(VisionAmbientTest, UltravisionLiftsTheNightAmbient) {
    const AmbientRGB out = applyVisionAmbientFloor(VisionType::Ultravision, kNight);
    EXPECT_GT(out.r, kNight.r);
    EXPECT_GT(out.g, kNight.g);
    EXPECT_GT(out.b, kNight.b);
    // Not a token lift: the oracle's Ultravision is worth x1.554 on the
    // frame, and fable's frame answers the mean ambient as a power law with
    // exponent 1.14 (measured, T361 probe 1), so the ambient itself has to
    // move about x1.47. The +4.9% T358 measured is nowhere near this.
    EXPECT_GT(mean(out), 1.40f * mean(kNight));
}

TEST(VisionAmbientTest, InfravisionLiftsLessThanUltravisionAndIsRedBiased) {
    const AmbientRGB ultra = visionAmbientFloor(VisionType::Ultravision);
    const AmbientRGB infra = visionAmbientFloor(VisionType::Infravision);

    EXPECT_GT(mean(ultra), mean(infra));
    EXPECT_GT(mean(infra), 0.0f);

    // T383: the bias is a statement about the FRAME RATIOS the oracle
    // measured, not about the floor's raw channel order. fable's night ambient
    // is blue-dominant (0.08/0.08/0.15) while the oracle's night FRAME is
    // red-dominant, so a floor that reproduces the oracle's ratios need not be
    // red-dominant itself. Oracle Infravision R x1.328 > B x1.208 (red-biased
    // lift); oracle Ultravision R x1.458 < B x1.851 (blue-lifted).
    const AmbientRGB ir = visionFrameRatios(VisionType::Infravision, kNight);
    const AmbientRGB ur = visionFrameRatios(VisionType::Ultravision, kNight);
    EXPECT_GT(ir.r, ir.b);
    EXPECT_LT(ur.r, ur.b);
}

TEST(VisionAmbientTest, OrderingIsNormalBelowInfraBelowUltra) {
    const float normal = mean(applyVisionAmbientFloor(VisionType::Normal, kNight));
    const float infra = mean(applyVisionAmbientFloor(VisionType::Infravision, kNight));
    const float ultra = mean(applyVisionAmbientFloor(VisionType::Ultravision, kNight));
    EXPECT_LT(normal, infra);
    EXPECT_LT(infra, ultra);
}

// A floor, not a multiply or an add: it can only ever raise a channel.
TEST(VisionAmbientTest, FloorNeverDarkensAChannel) {
    for (VisionType v : {VisionType::Normal, VisionType::Infravision,
                         VisionType::Ultravision}) {
        const AmbientRGB out = applyVisionAmbientFloor(v, kNight);
        EXPECT_GE(out.r, kNight.r);
        EXPECT_GE(out.g, kNight.g);
        EXPECT_GE(out.b, kNight.b);
    }
}

// Why a FLOOR was chosen: EQ's vision buffs do nothing you can see at noon.
TEST(VisionAmbientTest, DaylightIsAboveEveryFloor) {
    for (VisionType v : {VisionType::Normal, VisionType::Infravision,
                         VisionType::Ultravision}) {
        const AmbientRGB out = applyVisionAmbientFloor(v, kDay);
        EXPECT_FLOAT_EQ(out.r, kDay.r);
        EXPECT_FLOAT_EQ(out.g, kDay.g);
        EXPECT_FLOAT_EQ(out.b, kDay.b);
    }
}

// ---------------------------------------------------------------------------
// The racial vision floor (board B060), measured T361 against the official
// Titanium client: two sessions, `#race N` at a fixed camera at forced 22:00,
// each carrying a cast-Ultravision positive control, read from the session
// video. Three levels appeared and only three; race 6 landed exactly on the
// control both times.
// ---------------------------------------------------------------------------

TEST(RacialVisionTest, DarkElfIsTheOnlyUltravisionRace) {
    EXPECT_EQ(racialBaseVision(6), VisionType::Ultravision);
    for (uint16_t race : {1, 2, 3, 4, 5, 7, 8, 9, 10, 11, 12, 128, 130, 330}) {
        EXPECT_NE(racialBaseVision(race), VisionType::Ultravision)
            << "race " << race << " must not be Ultravision";
    }
}

TEST(RacialVisionTest, MeasuredInfravisionRaces) {
    // The four the pre-T361 table had wrong (4, 5, 9, 128) and the one it
    // omitted (130) are in here.
    for (uint16_t race : {4, 5, 7, 8, 9, 10, 11, 12, 128, 130}) {
        EXPECT_EQ(racialBaseVision(race), VisionType::Infravision)
            << "race " << race;
    }
}

TEST(RacialVisionTest, MeasuredNormalRaces) {
    for (uint16_t race : {1, 2, 3}) {
        EXPECT_EQ(racialBaseVision(race), VisionType::Normal) << "race " << race;
    }
}

// Froglok measured a fourth level (63.49, between Infravision's 60.18 and
// Ultravision's 65.00) that VisionType cannot express. It is deliberately NOT
// rounded to a neighbour; B060 carries the measurement for Will's call. This
// case exists so that a future change to it is a deliberate one.
TEST(RacialVisionTest, FroglokIsUnsettledAndLeftNormal) {
    EXPECT_EQ(racialBaseVision(330), VisionType::Normal);
}

// Anything the oracle was not asked about falls to Normal, as before.
TEST(RacialVisionTest, UnknownRaceIsNormal) {
    EXPECT_EQ(racialBaseVision(0), VisionType::Normal);
    EXPECT_EQ(racialBaseVision(9999), VisionType::Normal);
}

// ---------------------------------------------------------------------------
// T383 (board B171): the PER-CHANNEL gains, solved against the oracle table.
//
// T361 solved the two SCALAR gains and got them close (fable 1.449 / 1.237
// against the oracle's 1.560 / 1.252), but the per-channel shape was never
// fitted: it was carried over from T361's near-wall added-light deltas. T377
// measured both clients in one session, same spot, same crop, video-stepped on
// both sides, and the channels came out wrong -- fable's Ultravision green
// lagged its red by 12.5 points (a magenta cast where the oracle's lift is
// near-neutral), and fable's Infravision drove BLUE DOWN to x0.821, the only
// channel in either table that moved the wrong way.
//
// THE ORACLE TABLE (review/T377/README.md section 2, linear light, 700x420
// crop, qeynos2 -74/428/3.75 h=160, forced 22:00, Oraclealpha -- a Human, so
// no racial floor confounds the buff):
//
//     Ultravision  R x1.4581  G x1.4633  B x1.8507   grey x1.5612
//     Infravision  R x1.3284  G x1.2111  B x1.2079   grey x1.2555
//
// These cases pin the mapping against that table through visionFrameRatios(),
// the power-law response T361 measured on fable's own frames (exponent 1.1405,
// given independently by its two probe points as 1.1402 and 1.1409). The bar
// is 2%; T377's still-to-still noise floor is 1.3%.
// ---------------------------------------------------------------------------

namespace {

// review/T377/README.md section 2, oracle rows.
constexpr float kOracleUltraRatio[3] = {1.4581f, 1.4633f, 1.8507f};
constexpr float kOracleInfraRatio[3] = {1.3284f, 1.2111f, 1.2079f};
constexpr float kOracleUltraGrey = 1.5612f;
constexpr float kOracleInfraGrey = 1.2555f;

// fable's own measured night channel levels on that crop (T377, the two
// "night, no buff" windows that bracket each landing). Used only to weight the
// per-channel ratios into a whole-frame grey ratio the same way the oracle
// table's grey column is formed.
constexpr float kFableNightBeforeUltra[3] = {0.004443f, 0.003763f, 0.002664f};
constexpr float kFableNightBeforeInfra[3] = {0.004435f, 0.003757f, 0.002663f};

float greyRatio(const AmbientRGB& r, const float (&base)[3]) {
    const float lit = r.r * base[0] + r.g * base[1] + r.b * base[2];
    return lit / (base[0] + base[1] + base[2]);
}

}  // namespace

TEST(VisionOracleChannelsTest, UltravisionChannelRatiosMatchTheOracle) {
    const AmbientRGB r = visionFrameRatios(VisionType::Ultravision, kNight);
    EXPECT_NEAR(r.r, kOracleUltraRatio[0], 0.02f * kOracleUltraRatio[0]);
    EXPECT_NEAR(r.g, kOracleUltraRatio[1], 0.02f * kOracleUltraRatio[1]);
    EXPECT_NEAR(r.b, kOracleUltraRatio[2], 0.02f * kOracleUltraRatio[2]);
}

TEST(VisionOracleChannelsTest, InfravisionChannelRatiosMatchTheOracle) {
    const AmbientRGB r = visionFrameRatios(VisionType::Infravision, kNight);
    EXPECT_NEAR(r.r, kOracleInfraRatio[0], 0.02f * kOracleInfraRatio[0]);
    EXPECT_NEAR(r.g, kOracleInfraRatio[1], 0.02f * kOracleInfraRatio[1]);
    EXPECT_NEAR(r.b, kOracleInfraRatio[2], 0.02f * kOracleInfraRatio[2]);
}

// The plan's second half of the STOP condition: the whole-frame ratio has to
// come out right at the same time as the channels, not instead of them.
TEST(VisionOracleChannelsTest, WholeFrameGreyRatiosMatchTheOracle) {
    const float ultra = greyRatio(visionFrameRatios(VisionType::Ultravision, kNight),
                                  kFableNightBeforeUltra);
    const float infra = greyRatio(visionFrameRatios(VisionType::Infravision, kNight),
                                  kFableNightBeforeInfra);
    EXPECT_NEAR(ultra, kOracleUltraGrey, 0.02f * kOracleUltraGrey);
    EXPECT_NEAR(infra, kOracleInfraGrey, 0.02f * kOracleInfraGrey);
}

// The headline defect of B171: no vision may move a channel the wrong way.
// T377 measured fable's Infravision at B x0.821.
TEST(VisionOracleChannelsTest, NoVisionLowersAnyChannel) {
    for (VisionType v : {VisionType::Normal, VisionType::Infravision,
                         VisionType::Ultravision}) {
        const AmbientRGB r = visionFrameRatios(v, kNight);
        EXPECT_GE(r.r, 1.0f);
        EXPECT_GE(r.g, 1.0f);
        EXPECT_GE(r.b, 1.0f);
    }
}

// Ultravision's lift is near-neutral in R and G (the oracle's two agree to
// 0.4%) with blue on top. fable's green lagged its red by 12.5 points.
TEST(VisionOracleChannelsTest, UltravisionRedAndGreenAreNeutralToEachOther) {
    const AmbientRGB r = visionFrameRatios(VisionType::Ultravision, kNight);
    EXPECT_NEAR(r.g / r.r, kOracleUltraRatio[1] / kOracleUltraRatio[0], 0.02f);
}

// ---------------------------------------------------------------------------
// The second parameter (T383): the zone-light arm's Infravision tint.
//
// visionLightModifiers() applies r*(1+s), g*(1-0.5s), b*(1-s) to every zone
// light. At s = 0.3 that is a 1.857:1 spread between red and blue on the
// lamps; the oracle's Infravision spread is 1.328/1.208 = 1.0998. s was
// re-solved by least squares on the log of the oracle's three Infravision
// channel ratios (see the plan's Review): s = 0.0508, residuals -0.4% / +1.4%
// / -1.0%. Note the light arm was NEVER the cause of fable's B x0.821 -- it
// cannot be, since Infravision's blue multiplier (0.75 * (1 - s)) is above
// Normal's flat 0.25 for every s < 0.667 -- but its shape was wrong against
// the oracle by 24 points on blue and it is fitted here for that reason.
// ---------------------------------------------------------------------------

TEST(VisionLightTintTest, InfravisionLampSpreadMatchesTheOracle) {
    const auto infra = EQT::Graphics::visionLightModifiers(VisionType::Infravision);
    const float red = 1.0f + infra.redShift;
    const float blue = 1.0f - infra.redShift;
    EXPECT_NEAR(red / blue, kOracleInfraRatio[0] / kOracleInfraRatio[2], 0.02f);
}

TEST(VisionLightTintTest, InfravisionRaisesEveryLampChannelAgainstNormal) {
    const auto normal = EQT::Graphics::visionLightModifiers(VisionType::Normal);
    const auto infra = EQT::Graphics::visionLightModifiers(VisionType::Infravision);
    const float s = infra.redShift;
    EXPECT_GT(infra.intensity * (1.0f + s), normal.intensity);
    EXPECT_GT(infra.intensity * (1.0f - 0.5f * s), normal.intensity);
    EXPECT_GT(infra.intensity * (1.0f - s), normal.intensity);
}
