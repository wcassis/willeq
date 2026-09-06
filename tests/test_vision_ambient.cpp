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

#include <fstream>
#include <sstream>
#include <string>

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
// T405 (board B203, B252, B253): THE LIVE RE-SOLVE, with weather AND heading
// pinned. These cases pin the constants that produced the match and the
// evidence that they do, so neither the refuted values nor the refuted method
// can come back.
//
// THE TWO CONFOUNDS, AND WHY EVERY EARLIER FIT WAS UNSOUND.
//
//   * WEATHER (B211/B252). T377's whole vision window ran under server rain at
//     weatherMod 0.460. Weather multiplies the base a ratio is measured
//     against and leaves the vision floor fixed, so it INFLATES the ratio --
//     T377 read fable's Ultravision as x1.4483 where a clear-sky re-measure on
//     the SAME constants reads x1.1606 (T401). 25% too high.
//   * HEADING (B253). The official client's yaw on login is not the
//     character_data heading, so two captures at the same coordinates showed
//     different scenes: T401's oracle night still differs from T377's by RMSE
//     10.3%, against T386's 2.7% same-scene control. A vision buff MULTIPLIES
//     the scene, so a per-channel ratio only means something between frames of
//     the same thing -- and the oracle's per-channel table duly failed to
//     reproduce (R x1.94 against x1.46).
//
// T405 pinned both. `#weather 0 0` plus qeynos2's rain/snow chances zeroed for
// the session, with the modifier PROVEN 1.000 for every measurement window
// from the client's own log; and `#goto -74 428 3.751 160`, eqemu goto.cpp's
// optional 4th argument -> MovePC, the only placement that also aims. The
// control then PASSES on both clients: two independent oracle sessions agree
// to RMSE 0.389% and two fable sessions to 1.540%, against the 2.7% bar.
//
// The solve was six probe builds, each re-measured in its own live session
// (review/T405/README.md carries the search log). It is not a desk re-fit --
// the desk re-fit is what T383 did, and T386 measured it WORSE.
// ---------------------------------------------------------------------------

namespace {

// The heading-pinned oracle, mean of sessions o1 and o2, which agree to better
// than 0.2% on every channel. review/T405/README.md.
constexpr float kOracleUltraRatio[3] = {2.2531f, 1.4399f, 1.7687f};
constexpr float kOracleInfraRatio[3] = {1.8744f, 1.1956f, 1.1936f};
constexpr float kOracleUltraGrey = 1.7469f;
constexpr float kOracleInfraGrey = 1.3553f;

// fable on exactly the constants below, mean of the two confirmation sessions
// p6 and p6b.
constexpr float kFableUltraRatio[3] = {2.2662f, 1.4730f, 1.8020f};
constexpr float kFableInfraRatio[3] = {1.8679f, 1.1576f, 1.1631f};
constexpr float kFableUltraGrey = 1.87225f;
constexpr float kFableInfraGrey = 1.41730f;

// fable's own unbuffed night base on the measurement crop, per channel, from
// the same two sessions. This is what makes the grey basis differ -- see the
// grey case below.
constexpr float kFableNightBaseUltra[3] = {0.005398f, 0.004540f, 0.004472f};
constexpr float kFableNightBaseInfra[3] = {0.004926f, 0.004166f, 0.004475f};

// The night ambient the floors are raised over (IrrlichtRenderer::
// updateTimeOfDay's 20:00-05:00 arm).
constexpr float kNightAmbient[3] = {0.08f, 0.08f, 0.15f};

// The worst per-channel residual T405 reached. Six probe builds got every
// channel inside this; it is the number a future re-solve must BEAT, not a
// tolerance to relax.
constexpr float kT405WorstResidual = 0.032f;

float relErr(float a, float b) { return (a > b ? a - b : b - a) / b; }

float channel(const AmbientRGB& c, int i) {
    return i == 0 ? c.r : (i == 1 ? c.g : c.b);
}

}  // namespace

// The four scalars. T361/T394's were 0.1521 / 0.0956 and T383's 0.1601 /
// 0.1248; both are refuted and neither may come back.
TEST(VisionSolvedT405Test, GainsAreTheLiveSolvedValues) {
    EXPECT_FLOAT_EQ(EQT::Graphics::kUltravisionAmbientGain, 0.24607f);
    EXPECT_FLOAT_EQ(EQT::Graphics::kInfravisionAmbientGain, 0.16257f);
}

// The chromaticities. T361/T394's were (1.009, 0.848, 1.143) and
// (1.539, 0.843, 0.617) -- the oracle's measured ADDED-LIGHT colours, which
// T405 showed cannot work, because the three channels are coupled.
TEST(VisionSolvedT405Test, TintsAreTheLiveSolvedChromaticities) {
    EXPECT_FLOAT_EQ(EQT::Graphics::kUltravisionTint[0], 1.07877f);
    EXPECT_FLOAT_EQ(EQT::Graphics::kUltravisionTint[1], 0.79866f);
    EXPECT_FLOAT_EQ(EQT::Graphics::kUltravisionTint[2], 1.12256f);
    EXPECT_FLOAT_EQ(EQT::Graphics::kInfravisionTint[0], 1.15398f);
    EXPECT_FLOAT_EQ(EQT::Graphics::kInfravisionTint[1], 0.70739f);
    EXPECT_FLOAT_EQ(EQT::Graphics::kInfravisionTint[2], 1.13864f);
}

// What the split actually composes to -- the floors probe 6 measured. Pinning
// the product as well as its two factors is what makes a "harmless"
// re-normalisation of gain against tint a visible edit.
TEST(VisionSolvedT405Test, FloorsAreProbeSixsMeasuredValues) {
    const AmbientRGB ultra = visionAmbientFloor(VisionType::Ultravision);
    EXPECT_NEAR(ultra.r, 0.26545f, 0.0001f);
    EXPECT_NEAR(ultra.g, 0.19653f, 0.0001f);
    EXPECT_NEAR(ultra.b, 0.27623f, 0.0001f);
    const AmbientRGB infra = visionAmbientFloor(VisionType::Infravision);
    EXPECT_NEAR(infra.r, 0.18760f, 0.0001f);
    EXPECT_NEAR(infra.g, 0.11500f, 0.0001f);
    EXPECT_NEAR(infra.b, 0.18511f, 0.0001f);
}

// THE STRUCTURAL DEFECT T405 FOUND IN THE OLD NUMBERS. A floor only does
// anything on a channel it is ABOVE. The pre-T405 Infravision floor was
// (0.147, 0.081, 0.059) against a night base of (0.08, 0.08, 0.15): G cleared
// the base by 1%, and B was BELOW it -- so two of the three channels could not
// be moved by the buff at all, and every Infravision fit before T405 was in
// truth solved from one usable channel. Both floors must now clear the base
// everywhere.
TEST(VisionSolvedT405Test, EveryFloorChannelClearsTheNightBase) {
    for (VisionType v : {VisionType::Ultravision, VisionType::Infravision}) {
        const AmbientRGB f = visionAmbientFloor(v);
        for (int i = 0; i < 3; ++i) {
            EXPECT_GT(channel(f, i), kNightAmbient[i] * 1.2f)
                << "vision " << static_cast<int>(v) << " channel " << i
                << " does not clear the night base by a usable margin";
        }
    }
}

// The match itself: every channel of both visions inside the residual the
// search reached, measured live on these constants against the heading-pinned
// oracle.
TEST(VisionSolvedT405Test, EveryChannelMatchesTheHeadingPinnedOracle) {
    for (int i = 0; i < 3; ++i) {
        EXPECT_LE(relErr(kFableUltraRatio[i], kOracleUltraRatio[i]),
                  kT405WorstResidual)
            << "Ultravision channel " << i;
        EXPECT_LE(relErr(kFableInfraRatio[i], kOracleInfraRatio[i]),
                  kT405WorstResidual)
            << "Infravision channel " << i;
    }
}

// This is the case that stops the next unit chasing a phantom. "grey" is the
// unweighted mean of the three channel means, so a grey RATIO is a
// base-weighted average of the three channel ratios -- weighted by the
// client's OWN night base. fable's night base is red-heavy and the oracle's is
// green-heavy, so two clients whose per-channel ratios agree EXACTLY still
// report different greys. The reachable grey is the one fable would report at
// exact per-channel agreement, computed here from fable's own measured base.
TEST(VisionSolvedT405Test, GreyIsBaseWeightedSoTheOracleGreyIsNotATarget) {
    struct Row {
        const float* base;
        const float* oracleRatio;
        float fableGrey;
        float oracleGrey;
    };
    const Row rows[2] = {
        {kFableNightBaseUltra, kOracleUltraRatio, kFableUltraGrey, kOracleUltraGrey},
        {kFableNightBaseInfra, kOracleInfraRatio, kFableInfraGrey, kOracleInfraGrey},
    };
    for (const Row& r : rows) {
        float num = 0.0f, den = 0.0f;
        for (int i = 0; i < 3; ++i) {
            num += r.base[i] * r.oracleRatio[i];
            den += r.base[i];
        }
        const float reachableGrey = num / den;
        // Against the reachable grey, fable is inside 2%...
        EXPECT_LE(relErr(r.fableGrey, reachableGrey), 0.02f);
        // ...while against the oracle's own grey it is more than 4% away, and
        // that gap is the base chromaticity, not a defect.
        EXPECT_GT(relErr(r.fableGrey, r.oracleGrey), 0.04f);
    }
}

// T405 did NOT move the zone-light arm. A probe build set Infravision's lamp
// modifiers equal to Ultravision's so a single session could sweep one ambient
// channel with the lamps held fixed; that change was probe-only and reverted,
// and T360's literals stand.
TEST(VisionSolvedT405Test, LampModifiersAreStillT360sLiterals) {
    const auto infra = EQT::Graphics::visionLightModifiers(VisionType::Infravision);
    EXPECT_FLOAT_EQ(infra.intensity, 0.75f);
    EXPECT_FLOAT_EQ(infra.redShift, 0.3f);
    const auto ultra = EQT::Graphics::visionLightModifiers(VisionType::Ultravision);
    EXPECT_FLOAT_EQ(ultra.intensity, 1.0f);
    EXPECT_FLOAT_EQ(ultra.redShift, 0.0f);
}

// The tombstone. Every superseded value in one place, so a revert to any of
// them is a red test naming the unit that refuted it.
TEST(VisionSolvedT405Test, NoRefutedConstantSetCanComeBack) {
    // T361/T394's, refuted by T405: 75% of the oracle's Ultravision under a
    // proven-clear sky (T401 §4).
    EXPECT_NE(EQT::Graphics::kUltravisionAmbientGain, 0.1521f);
    EXPECT_NE(EQT::Graphics::kInfravisionAmbientGain, 0.0956f);
    EXPECT_NE(EQT::Graphics::kUltravisionTint[0], 1.009f);
    EXPECT_NE(EQT::Graphics::kInfravisionTint[0], 1.539f);
    // T383's, refuted by T386's live re-measure and reverted by T394.
    EXPECT_NE(EQT::Graphics::kUltravisionAmbientGain, 0.1601f);
    EXPECT_NE(EQT::Graphics::kInfravisionAmbientGain, 0.1248f);
}

// ---------------------------------------------------------------------------
// T396 (board B211): THE COMPOSITION ORDER.
//
// updateTimeOfDay() builds the scene ambient from four things, and until T396
// the order lived only in a comment. B211 is the reason it matters: the
// weather modifier multiplies the ambient a vision ratio is measured against
// while leaving the floor fixed, so an uncontrolled weather state inflates any
// measured vision ratio in proportion to the weather at the time. T377's
// session ran at weatherMod 0.460 (server rain, intensity 9) throughout.
//
// The floor is applied LAST, on purpose: it is a floor on WHAT THE PLAYER
// SEES. `composeSceneAmbient()` IS the production composition, so these cases
// pin the order at the site rather than restating it.
// ---------------------------------------------------------------------------

using EQT::Graphics::composeSceneAmbient;

namespace {
// The exact modifier T377's session ran under: 1 - 0.6*9/10.
constexpr float kT377WeatherMod = 0.46f;
}  // namespace

TEST(VisionAmbientCompositionT396Test, ClearWeatherAndNoUserBoostChangeNothing) {
    const AmbientRGB out =
        composeSceneAmbient(kNight, 1.0f, 1.0f, VisionType::Normal);
    EXPECT_FLOAT_EQ(out.r, kNight.r);
    EXPECT_FLOAT_EQ(out.g, kNight.g);
    EXPECT_FLOAT_EQ(out.b, kNight.b);
}

TEST(VisionAmbientCompositionT396Test, WeatherMultipliesTheUnbuffedNightAmbient) {
    const AmbientRGB out =
        composeSceneAmbient(kNight, 1.0f, kT377WeatherMod, VisionType::Normal);
    EXPECT_FLOAT_EQ(out.r, kNight.r * kT377WeatherMod);
    EXPECT_FLOAT_EQ(out.g, kNight.g * kT377WeatherMod);
    EXPECT_FLOAT_EQ(out.b, kNight.b * kT377WeatherMod);
}

// The order pin. Under the CORRECT order the buff is untouched by the storm;
// under the inverted order every channel would come out 0.46x lower.
TEST(VisionAmbientCompositionT396Test, TheVisionFloorClampsTheFinalAmbientAfterWeather) {
    const AmbientRGB floor = visionAmbientFloor(VisionType::Ultravision);
    const AmbientRGB out =
        composeSceneAmbient(kNight, 1.0f, kT377WeatherMod, VisionType::Ultravision);

    EXPECT_FLOAT_EQ(out.r, floor.r);
    EXPECT_FLOAT_EQ(out.g, floor.g);
    EXPECT_FLOAT_EQ(out.b, floor.b)
        << "weather multiplied the vision floor; the floor must be last";

    // And explicitly: not the other order.
    EXPECT_GT(out.r, floor.r * kT377WeatherMod);
    EXPECT_GT(mean(out), kT377WeatherMod * mean(floor) * 1.5f);
}

// The consequence B211 records, stated as a test so it cannot be forgotten:
// weather changes the base a vision ratio is measured against but NOT the
// buffed frame, so the ratio measured under rain is larger than the true one.
TEST(VisionAmbientCompositionT396Test, UncontrolledWeatherInflatesAMeasuredVisionRatio) {
    const float clearBase = mean(composeSceneAmbient(kNight, 1.0f, 1.0f, VisionType::Normal));
    const float clearBuff = mean(composeSceneAmbient(kNight, 1.0f, 1.0f, VisionType::Ultravision));
    const float rainBase = mean(composeSceneAmbient(kNight, 1.0f, kT377WeatherMod, VisionType::Normal));
    const float rainBuff = mean(composeSceneAmbient(kNight, 1.0f, kT377WeatherMod, VisionType::Ultravision));

    EXPECT_FLOAT_EQ(clearBuff, rainBuff) << "the floor is weather-independent";
    EXPECT_GT(rainBuff / rainBase, (clearBuff / clearBase) * 1.9f)
        << "T377's rain more than doubled the apparent Ultravision ratio";
}

// Daylight is above every floor and the user multiplier still clamps at 1.
TEST(VisionAmbientCompositionT396Test, DaylightIsAboveEveryFloorAndTheUserBoostClamps) {
    const AmbientRGB out =
        composeSceneAmbient(kDay, 4.0f, 1.0f, VisionType::Ultravision);
    EXPECT_FLOAT_EQ(out.r, 1.0f);
    EXPECT_FLOAT_EQ(out.g, 1.0f);
    EXPECT_FLOAT_EQ(out.b, 1.0f);

    const AmbientRGB plain =
        composeSceneAmbient(kDay, 1.0f, 1.0f, VisionType::Ultravision);
    EXPECT_FLOAT_EQ(plain.r, kDay.r);
    EXPECT_FLOAT_EQ(plain.g, kDay.g);
    EXPECT_FLOAT_EQ(plain.b, kDay.b);
}

// The production site calls it -- so the order above is the one that ships.
TEST(VisionAmbientCompositionT396Test, UpdateTimeOfDayUsesTheComposition) {
    const char* kRendererTU =
        WILLEQ_REPO_ROOT "/src/client/graphics/renderer/irrlicht_renderer.cpp";
    std::ifstream in(kRendererTU);
    ASSERT_TRUE(in.good()) << "cannot open " << kRendererTU;
    std::stringstream ss;
    ss << in.rdbuf();
    const std::string src = ss.str();
    EXPECT_NE(src.find("composeSceneAmbient("), std::string::npos)
        << "updateTimeOfDay() open-codes the ambient composition again; the "
           "order is then pinned by nothing";
}
