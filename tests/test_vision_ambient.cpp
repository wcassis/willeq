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
//
// T394 (board B203): T383 re-solved all six floor channels plus the zone-light
// arm's Infravision redShift against the oracle table and its forward model was
// REFUTED by the live re-measure (T386 section 4) -- both scalar ratios got
// WORSE. The constants are back to T361's live-matched values and the last
// section of this file pins them, with the refuted table written down.

#include <gtest/gtest.h>

#include "client/graphics/vision_ambient.h"
#include "client/graphics/vision_type.h"

using EQT::Graphics::AmbientRGB;
using EQT::Graphics::VisionType;
using EQT::Graphics::applyVisionAmbientFloor;
using EQT::Graphics::racialBaseVision;
using EQT::Graphics::visionAmbientFloor;

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

    // Oracle: Infravision's added light is strongly red (R/B measured 2.49),
    // Ultravision's is near-neutral and if anything slightly blue.
    EXPECT_GT(infra.r, infra.b);
    EXPECT_LT(ultra.r, ultra.b);
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
// T394 (board B203): T383's per-channel re-solve was REFUTED by the live
// re-measure it was waiting for, and the constants are back to T361's
// live-matched values. These cases pin the RESTORED numbers so the refuted
// ones cannot come back by accident.
//
// WHAT WAS REFUTED. T383 re-solved all six floor channels (and the zone-light
// arm's Infravision redShift) by inverting T361's measured power law
// `floor_c = night_c * oracle_ratio_c ^ (1/1.1405)` PER CHANNEL, and
// forward-checked the prediction to within 0.11% of the oracle table. T386 §4
// then repeated T377 §2 exactly -- same character, spot, forced 22:00,
// 700x420+40+40 crop, linear light, video-stepped over 882 frames, with the
// two sessions' night stills agreeing to RMSE 2.7% -- and measured the frame:
//
//   | condition                  |      R |      G |      B |   grey |
//   |----------------------------|--------|--------|--------|--------|
//   | oracle Ultravision         | 1.4581 | 1.4633 | 1.8507 | 1.5612 |
//   | T383 PREDICTED             | 1.4569 | 1.4640 | 1.8503 | 1.5561 |
//   | fable pre-T383  (T377)     | 1.4609 | 1.2814 | 1.6629 | 1.4483 |
//   | fable post-T383 (T386)     | 0.9674 | 0.9723 | 1.6377 | 1.1686 |
//   | oracle Infravision         | 1.3284 | 1.2111 | 1.2079 | 1.2555 |
//   | T383 PREDICTED             | 1.3298 | 1.2125 | 1.2085 | 1.2582 |
//   | fable pre-T383  (T377)     | 1.6002 | 1.1264 | 0.8209 | 1.2450 |
//   | fable post-T383 (T386)     | 1.0688 | 1.0345 | 1.1274 | 1.0757 |
//
// Raising the floor's R and G from 0.08 to ~0.111 was predicted to move the
// frame x1.457 / x1.464 and moved it x0.967 / x0.972 -- very slightly DARKER.
// The `ambient^1.1405` power law does not hold per channel at this operating
// point, so the forward model is wrong, and both scalar ratios regressed
// (Ultravision 1.4483 -> 1.1686 against 1.5612; Infravision 1.2450 -> 1.0757
// against 1.2555, having been within 1% before). T386 also answered T383's
// open question: the `B x0.8209` anomaly did NOT reproduce.
//
// A refuted model is reverted, not patched. The per-channel fit is OWED as a
// LIVE A/B search -- one parameter at a time, oracle frame beside fable frame,
// T361's method -- and the pre-T383 row above is the baseline it starts from.
// No constant here may be moved by argument again.
// ---------------------------------------------------------------------------

namespace {

// review/T377/README.md section 2 and review/T386/README.md section 4.
constexpr float kOracleUltraRatio[3] = {1.4581f, 1.4633f, 1.8507f};
constexpr float kOracleInfraRatio[3] = {1.3284f, 1.2111f, 1.2079f};
constexpr float kOracleUltraGrey = 1.5612f;
constexpr float kOracleInfraGrey = 1.2555f;

// The LIVE-MEASURED fable rows. Pre-T383 is what the restored constants
// produce on a real frame and is the baseline for the next re-solve; post-T383
// is what the refuted constants produced.
constexpr float kFableUltraRatioPreT383[3] = {1.4609f, 1.2814f, 1.6629f};
constexpr float kFableInfraRatioPreT383[3] = {1.6002f, 1.1264f, 0.8209f};
constexpr float kFableUltraGreyPreT383 = 1.4483f;
constexpr float kFableInfraGreyPreT383 = 1.2450f;
constexpr float kFableUltraGreyPostT383 = 1.1686f;
constexpr float kFableInfraGreyPostT383 = 1.0757f;

float absErr(float a, float b) { return a > b ? a - b : b - a; }

}  // namespace

// T383's gains were 0.1601 / 0.1248. These are T361's, solved by an A/B
// search against the oracle on live frames.
TEST(VisionGainsRevertedT394Test,
     T383PerChannelFitRefutedByT386SoTheGainsAreT361LiveMatchedValues) {
    EXPECT_FLOAT_EQ(EQT::Graphics::kUltravisionAmbientGain, 0.1521f);
    EXPECT_FLOAT_EQ(EQT::Graphics::kInfravisionAmbientGain, 0.0956f);
}

// T383's tints were (0.695, 0.698, 1.607) and (0.823, 0.759, 1.419). These
// are the oracle's own measured added-light chromaticities (T361 Leg A).
TEST(VisionGainsRevertedT394Test,
     T383PerChannelFitRefutedByT386SoTheTintsAreOracleAddedLightChromaticities) {
    EXPECT_FLOAT_EQ(EQT::Graphics::kUltravisionTint[0], 1.009f);
    EXPECT_FLOAT_EQ(EQT::Graphics::kUltravisionTint[1], 0.848f);
    EXPECT_FLOAT_EQ(EQT::Graphics::kUltravisionTint[2], 1.143f);
    EXPECT_FLOAT_EQ(EQT::Graphics::kInfravisionTint[0], 1.539f);
    EXPECT_FLOAT_EQ(EQT::Graphics::kInfravisionTint[1], 0.843f);
    EXPECT_FLOAT_EQ(EQT::Graphics::kInfravisionTint[2], 0.617f);
}

// The shape, not just the values: T361's Infravision floor is red-dominant
// ("'s eyes glow red", R/B measured 2.49). T383's was blue-dominant, which is
// the visible signature of the refuted fit.
TEST(VisionGainsRevertedT394Test,
     T383PerChannelFitRefutedByT386SoTheInfravisionFloorIsRedDominantAgain) {
    const AmbientRGB infra = visionAmbientFloor(VisionType::Infravision);
    EXPECT_GT(infra.r, infra.g);
    EXPECT_GT(infra.r, infra.b);
    EXPECT_NEAR(infra.r / infra.b, 2.49f, 0.05f);
}

// The zone-light arm's second parameter. T383 moved it to 0.0508; T360's
// literal is 0.3 and it is restored untouched.
TEST(VisionGainsRevertedT394Test,
     T383LampRedShiftRefutedByT386SoTheInfravisionShiftIsT360sLiteral) {
    const auto infra = EQT::Graphics::visionLightModifiers(VisionType::Infravision);
    EXPECT_FLOAT_EQ(infra.intensity, 0.75f);
    EXPECT_FLOAT_EQ(infra.redShift, 0.3f);
    const auto ultra = EQT::Graphics::visionLightModifiers(VisionType::Ultravision);
    EXPECT_FLOAT_EQ(ultra.intensity, 1.0f);
    EXPECT_FLOAT_EQ(ultra.redShift, 0.0f);
}

// The baseline the next LIVE re-solve starts from: on every measured basis the
// restored constants sit closer to the oracle than T383's did. This case
// carries the numbers so a future unit does not have to re-derive them from
// two review directories -- it is the contract B203 closes against.
TEST(VisionGainsRevertedT394Test,
     PreT383LiveRatiosAreTheBaselineForTheOwedLiveResolve) {
    EXPECT_LT(absErr(kFableUltraGreyPreT383, kOracleUltraGrey),
              absErr(kFableUltraGreyPostT383, kOracleUltraGrey));
    EXPECT_LT(absErr(kFableInfraGreyPreT383, kOracleInfraGrey),
              absErr(kFableInfraGreyPostT383, kOracleInfraGrey));

    // Where the restored row is already good, and where the work remains.
    // Ultravision red is within 0.2% of the oracle; its green is 12.4% short
    // and Infravision blue is the outlier the live re-solve must explain --
    // T386 did NOT reproduce it (x1.1274), so it may not even be real.
    EXPECT_NEAR(kFableUltraRatioPreT383[0], kOracleUltraRatio[0],
                0.01f * kOracleUltraRatio[0]);
    EXPECT_LT(kFableUltraRatioPreT383[1], 0.95f * kOracleUltraRatio[1]);
    EXPECT_LT(kFableInfraRatioPreT383[2], 1.0f);
    // ...and where it overshoots: Infravision red ran 20% hot.
    EXPECT_GT(kFableInfraRatioPreT383[0], 1.15f * kOracleInfraRatio[0]);
}
