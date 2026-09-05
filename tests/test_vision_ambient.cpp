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
