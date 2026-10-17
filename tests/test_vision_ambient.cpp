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

// The two ambients the day/night curve computes. T428 (board B280) moved both
// out of IrrlichtRenderer::updateTimeOfDay() into vision_ambient.h and solved
// the NIGHT one live against the official client -- it used to be an unmeasured
// (0.08, 0.08, 0.15). Named here rather than copied, so this file cannot drift
// from the production value the way its old private copy could.
constexpr AmbientRGB kNight{EQT::Graphics::kNightAmbient[0],
                            EQT::Graphics::kNightAmbient[1],
                            EQT::Graphics::kNightAmbient[2]};
constexpr AmbientRGB kDay{EQT::Graphics::kDayAmbient[0],
                          EQT::Graphics::kDayAmbient[1],
                          EQT::Graphics::kDayAmbient[2]};

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

    // Oracle: Infravision's added light is strongly red -- that survives every
    // re-measurement (its floor is R 0.448 against B 0.364).
    EXPECT_GT(infra.r, infra.b);
    // T428 REVERSED the Ultravision clause, on a measurement. "Near-neutral
    // and if anything slightly blue" came from the 700x420+40+40 world crop,
    // which at this spot is half SKY -- and the sky does not respond to a
    // vision buff, so it drags every channel toward the sky's own colour. On
    // T408's geometry-only band the oracle's Ultravision is R x2.7995 against
    // B x2.4946 in three independent sessions: RED-leaning, like Infravision
    // but much less so. The floor follows the measurement.
    EXPECT_GT(ultra.r, ultra.b);
    EXPECT_LT(ultra.r / ultra.b, infra.r / infra.b)
        << "Ultravision leans red LESS than Infravision does";
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
// T428 MOVED THIS, and the deviation is deliberate and measured. Matching the
// official client's night ratio (x2.7995 R on the geometry crop) needs an
// Ultravision floor of (0.51320, 0.51411, 0.49219), which is 2.6% and 2.8%
// ABOVE the day ambient on R and G -- so daylight is no longer strictly above
// every floor and Ultravision is no longer exactly invisible at noon. Measured
// live on the committed constants (session conf2): daylight under Ultravision
// reads R x1.0239 G x1.0259 B x0.9414, i.e. a couple of percent. Infravision
// IS still invisible by day, on every channel.
//
// Whether the official client lifts daylight under Ultravision is NOT settled:
// T428's oracle day block read x1.028 / x1.022 / x1.030, but that session's own
// unbuffed day control drifted 30-48% over the same four minutes, so the
// signal is inside the drift. Board B447.
TEST(VisionAmbientTest, DaylightIsUntouchedByInfravisionAndBarelyMovedByUltravision) {
    for (VisionType v : {VisionType::Normal, VisionType::Infravision}) {
        const AmbientRGB out = applyVisionAmbientFloor(v, kDay);
        EXPECT_FLOAT_EQ(out.r, kDay.r);
        EXPECT_FLOAT_EQ(out.g, kDay.g);
        EXPECT_FLOAT_EQ(out.b, kDay.b);
    }
    for (VisionType v : {VisionType::Ultravision, VisionType::FroglokVision}) {
        const AmbientRGB out = applyVisionAmbientFloor(v, kDay);
        EXPECT_GE(out.r, kDay.r);
        EXPECT_LE(out.r, kDay.r * 1.03f);
        EXPECT_GE(out.g, kDay.g);
        EXPECT_LE(out.g, kDay.g * 1.03f);
        EXPECT_FLOAT_EQ(out.b, kDay.b) << "no floor clears the day ambient on B";
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

// T428 SETTLED THE LAST ROW (board B060 CLOSED, on Will's ruling that Froglok
// gets a fourth level). T361 saw it as a luma (63.49, between Infravision's
// 60.18 and Ultravision's 65.00) and could not express it; T428 measured it
// per channel on the same official client -- x2.6803 R, x1.9478 G, x2.1753 B
// over that session's own night base, against Ultravision's x2.7995 / x1.9517
// / x2.4946 and Infravision's x2.2483 / x1.4121 / x1.3561 -- and
// VisionType::FroglokVision carries it. Froglok is the only race that has it,
// exactly as the Dark Elf is the only Ultravision race.
TEST(RacialVisionTest, FroglokIsTheOnlyRaceOnTheFourthLevel) {
    EXPECT_EQ(racialBaseVision(330), VisionType::FroglokVision);
    for (uint16_t race : {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 128, 130}) {
        EXPECT_NE(racialBaseVision(race), VisionType::FroglokVision)
            << "race " << race << " must not be on the Froglok level";
    }
}

// THE ENUMERATOR VALUES ARE WIRE VALUES and the brightness order is NOT the
// enumerator order. `BuffManager::computeVisionType()` literally returns 2 for
// an UltraVision effect and SimulationInput carries the enum as a raw uint8_t,
// so T428 had to APPEND FroglokVision as 3 -- which makes it compare GREATER
// than Ultravision. `PlayerCameraComponent::setVisionType()`'s rule is "a buff
// raises vision, never lowers it"; written on the enum, an Ultravision buff
// would have been a silent no-op on a Froglok. visionRank() is the measured
// order and is what that comparison uses.
TEST(RacialVisionTest, VisionRankIsBrightnessOrderNotEnumeratorOrder) {
    using EQT::Graphics::visionRank;
    EXPECT_GT(visionRank(VisionType::Ultravision),
              visionRank(VisionType::FroglokVision));
    EXPECT_GT(visionRank(VisionType::FroglokVision),
              visionRank(VisionType::Infravision));
    EXPECT_GT(visionRank(VisionType::Infravision), visionRank(VisionType::Normal));
    // The enumerator order says the opposite for the top pair -- that is the
    // trap this function exists to keep out of the comparison.
    EXPECT_GT(static_cast<int>(VisionType::FroglokVision),
              static_cast<int>(VisionType::Ultravision));
    // And the wire values nothing may renumber.
    EXPECT_EQ(static_cast<int>(VisionType::Normal), 0);
    EXPECT_EQ(static_cast<int>(VisionType::Infravision), 1);
    EXPECT_EQ(static_cast<int>(VisionType::Ultravision), 2);
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

// THE ORACLE, RE-TAKEN BY T428 ON THE GEOMETRY CROP -- and the crop is the
// correction, not a detail. T405's numbers (Ultravision x2.2531 x1.4399
// x1.7687; Infravision x1.8744 x1.1956 x1.1936) were measured on the
// 700x420+40+40 world crop, which at this spot is about HALF SKY -- and a
// vision buff does not touch the sky (T361 measured x1.02 on a sky-only crop).
// So that crop dilutes every ratio by a factor set by each client's own camera
// height, which is exactly the kind of thing that is not comparable ACROSS
// clients. T408 had already moved to a geometry-only band for the same reason.
// Re-measuring T405's own two oracle videos on 560x110+200+350, plus T428's
// new session o3, gives three sessions agreeing to under 1%:
//
//     session       Ultra R   G        B     |  Infra R   G        B
//     T405 o1       2.7965  1.9569  2.4841   |  2.2447  1.4121  1.3529
//     T405 o2       2.7989  1.9560  2.5070   |  2.2505  1.4119  1.3587
//     T428 o3       2.8032  1.9422  2.4926   |  2.2496  1.4122  1.3566
constexpr float kOracleUltraRatio[3] = {2.7995f, 1.9517f, 2.4946f};
constexpr float kOracleInfraRatio[3] = {2.2483f, 1.4121f, 1.3561f};
constexpr float kOracleUltraGrey = 2.3294f;
constexpr float kOracleInfraGrey = 1.5936f;

// fable on exactly the constants below, session conf2, each buff window
// divided by the mean of the two unbuffed windows that bracket it.
constexpr float kFableUltraRatio[3] = {2.8244f, 1.9746f, 2.3957f};
constexpr float kFableInfraRatio[3] = {2.3157f, 1.4348f, 1.3965f};
constexpr float kFableUltraGrey = 2.3382f;
constexpr float kFableInfraGrey = 1.7149f;

// fable's own unbuffed night base on the measurement crop, per channel, from
// the same session. This is what makes the grey basis differ -- see the grey
// case below.
constexpr float kFableNightBaseUltra[3] = {0.011560f, 0.016597f, 0.007210f};
constexpr float kFableNightBaseInfra[3] = {0.011551f, 0.016587f, 0.007199f};

// The worst per-channel residual T428's live search reached, on the corrected
// crop and the moved night base. It is the number a future re-solve must BEAT,
// not a tolerance to relax. (T405's was 0.032 on the diluted world crop.)
constexpr float kT428WorstResidual = 0.045f;

float relErr(float a, float b) { return (a > b ? a - b : b - a) / b; }

float channel(const AmbientRGB& c, int i) {
    return i == 0 ? c.r : (i == 1 ? c.g : c.b);
}

}  // namespace

// The four scalars. T361/T394's were 0.1521 / 0.0956 and T383's 0.1601 /
// 0.1248; both are refuted and neither may come back.
TEST(VisionSolvedT405Test, GainsAreTheLiveSolvedValues) {
    // T428 re-solved these: the night base they are floors OVER moved by 3.3x
    // (board B280), so T405's values sat BELOW it on two of three channels and
    // the buff would have done nothing there. Same method, same rig, one
    // session -- and on the corrected geometry crop (see the oracle block).
    EXPECT_FLOAT_EQ(EQT::Graphics::kUltravisionAmbientGain, 0.50650f);
    EXPECT_FLOAT_EQ(EQT::Graphics::kFroglokVisionAmbientGain, 0.49566f);
    EXPECT_FLOAT_EQ(EQT::Graphics::kInfravisionAmbientGain, 0.41469f);
}

// The chromaticities. T361/T394's were (1.009, 0.848, 1.143) and
// (1.539, 0.843, 0.617) -- the oracle's measured ADDED-LIGHT colours, which
// T405 showed cannot work, because the three channels are coupled.
TEST(VisionSolvedT405Test, TintsAreTheLiveSolvedChromaticities) {
    EXPECT_FLOAT_EQ(EQT::Graphics::kUltravisionTint[0], 1.01323f);
    EXPECT_FLOAT_EQ(EQT::Graphics::kUltravisionTint[1], 1.01502f);
    EXPECT_FLOAT_EQ(EQT::Graphics::kUltravisionTint[2], 0.97175f);
    EXPECT_FLOAT_EQ(EQT::Graphics::kFroglokVisionTint[0], 1.02056f);
    EXPECT_FLOAT_EQ(EQT::Graphics::kFroglokVisionTint[1], 1.03579f);
    EXPECT_FLOAT_EQ(EQT::Graphics::kFroglokVisionTint[2], 0.94365f);
    EXPECT_FLOAT_EQ(EQT::Graphics::kInfravisionTint[0], 1.08065f);
    EXPECT_FLOAT_EQ(EQT::Graphics::kInfravisionTint[1], 1.04177f);
    EXPECT_FLOAT_EQ(EQT::Graphics::kInfravisionTint[2], 0.87758f);
}

// What the split actually composes to -- the floors probe 6 measured. Pinning
// the product as well as its two factors is what makes a "harmless"
// re-normalisation of gain against tint a visible edit.
TEST(VisionSolvedT405Test, FloorsAreProbeSixsMeasuredValues) {
    const AmbientRGB ultra = visionAmbientFloor(VisionType::Ultravision);
    EXPECT_NEAR(ultra.r, 0.51320f, 0.0001f);
    EXPECT_NEAR(ultra.g, 0.51411f, 0.0001f);
    EXPECT_NEAR(ultra.b, 0.49219f, 0.0001f);
    const AmbientRGB frog = visionAmbientFloor(VisionType::FroglokVision);
    EXPECT_NEAR(frog.r, 0.50585f, 0.0001f);
    EXPECT_NEAR(frog.g, 0.51340f, 0.0001f);
    EXPECT_NEAR(frog.b, 0.46773f, 0.0001f);
    const AmbientRGB infra = visionAmbientFloor(VisionType::Infravision);
    EXPECT_NEAR(infra.r, 0.44813f, 0.0001f);
    EXPECT_NEAR(infra.g, 0.43201f, 0.0001f);
    EXPECT_NEAR(infra.b, 0.36392f, 0.0001f);
    // The measured ORDER: Froglok sits between the other two on every channel,
    // which is what makes it a fourth level rather than a rounding of either.
    EXPECT_LT(frog.r, ultra.r);
    EXPECT_GT(frog.r, infra.r);
    EXPECT_LT(frog.b, ultra.b);
    EXPECT_GT(frog.b, infra.b);
}

// THE STRUCTURAL DEFECT T405 FOUND IN THE OLD NUMBERS. A floor only does
// anything on a channel it is ABOVE. The pre-T405 Infravision floor was
// (0.147, 0.081, 0.059) against a night base of (0.08, 0.08, 0.15): G cleared
// the base by 1%, and B was BELOW it -- so two of the three channels could not
// be moved by the buff at all, and every Infravision fit before T405 was in
// truth solved from one usable channel. Both floors must now clear the base
// everywhere.
TEST(VisionSolvedT405Test, EveryFloorChannelClearsTheNightBase) {
    for (VisionType v : {VisionType::Ultravision, VisionType::FroglokVision,
                         VisionType::Infravision}) {
        const AmbientRGB f = visionAmbientFloor(v);
        for (int i = 0; i < 3; ++i) {
            EXPECT_GT(channel(f, i), EQT::Graphics::kNightAmbient[i] * 1.2f)
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
                  kT428WorstResidual)
            << "Ultravision channel " << i;
        EXPECT_LE(relErr(kFableInfraRatio[i], kOracleInfraRatio[i]),
                  kT428WorstResidual)
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
        // Against the reachable grey, fable is inside 3%. That is the whole
        // claim: the arithmetic, not the gap.
        EXPECT_LE(relErr(r.fableGrey, reachableGrey), 0.03f);
    }
}

// T428's addendum, and it is the clearest single piece of evidence that B280
// is closed. The gap above USED to be 7.18% (Ultravision) and 4.57%
// (Infravision) against the oracle's own grey, purely because the two clients'
// night bases were different COLOURS -- fable's red-heavy (R .00540 G .00453
// B .00445), the oracle's green-heavy (R .01900 G .03333 B .02797). With the
// night base solved, fable's base is green-heavy too and the Ultravision greys
// now agree to better than 1% without anyone aiming at the grey. Infravision's
// gap survives at ~7%, because the two clients' DAY chromaticity still differs
// (different cameras see different fractions of the same scene) and the grey
// is base-weighted -- so the arithmetic above is still the rule, and this case
// records that one of its two rows collapsed when the base moved.
TEST(VisionSolvedT405Test, MatchingTheNightBaseCollapsedTheUltravisionGreyGap) {
    EXPECT_LE(relErr(kFableUltraGrey, kOracleUltraGrey), 0.01f);
    EXPECT_GT(relErr(kFableInfraGrey, kOracleInfraGrey), 0.04f);
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
    // T428's fourth level INHERITS Ultravision's pair rather than inventing
    // one: what T428 measured is the AMBIENT floor, and whether vision belongs
    // on the zone's data-only point lights at all is still board B067. The
    // inheritance is deliberate and is pinned here so it reads as a decision.
    const auto frog = EQT::Graphics::visionLightModifiers(VisionType::FroglokVision);
    EXPECT_FLOAT_EQ(frog.intensity, ultra.intensity);
    EXPECT_FLOAT_EQ(frog.redShift, ultra.redShift);
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
    // T405's own, superseded by T428 for two reasons at once: they were solved
    // over a night base 3.3x too dark (board B280) and against oracle ratios
    // diluted by a half-sky crop. Both are measurements, not opinions.
    EXPECT_NE(EQT::Graphics::kUltravisionAmbientGain, 0.24607f);
    EXPECT_NE(EQT::Graphics::kInfravisionAmbientGain, 0.16257f);
}

// ---------------------------------------------------------------------------
// T396 (board B211): THE COMPOSITION ORDER.
//
// updateTimeOfDay() builds the scene ambient from three things (four until
// T424 dropped the dead user multiplier -- board B380), and until T396 the
// order lived only in a comment. B211 is the reason it matters: the
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
        composeSceneAmbient(kNight, 1.0f, VisionType::Normal);
    EXPECT_FLOAT_EQ(out.r, kNight.r);
    EXPECT_FLOAT_EQ(out.g, kNight.g);
    EXPECT_FLOAT_EQ(out.b, kNight.b);
}

TEST(VisionAmbientCompositionT396Test, WeatherMultipliesTheUnbuffedNightAmbient) {
    const AmbientRGB out =
        composeSceneAmbient(kNight, kT377WeatherMod, VisionType::Normal);
    EXPECT_FLOAT_EQ(out.r, kNight.r * kT377WeatherMod);
    EXPECT_FLOAT_EQ(out.g, kNight.g * kT377WeatherMod);
    EXPECT_FLOAT_EQ(out.b, kNight.b * kT377WeatherMod);
}

// The order pin. Under the CORRECT order the buff is untouched by the storm;
// under the inverted order every channel would come out 0.46x lower.
TEST(VisionAmbientCompositionT396Test, TheVisionFloorClampsTheFinalAmbientAfterWeather) {
    const AmbientRGB floor = visionAmbientFloor(VisionType::Ultravision);
    const AmbientRGB out =
        composeSceneAmbient(kNight, kT377WeatherMod, VisionType::Ultravision);

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
    const float clearBase = mean(composeSceneAmbient(kNight, 1.0f, VisionType::Normal));
    const float clearBuff = mean(composeSceneAmbient(kNight, 1.0f, VisionType::Ultravision));
    const float rainBase = mean(composeSceneAmbient(kNight, kT377WeatherMod, VisionType::Normal));
    const float rainBuff = mean(composeSceneAmbient(kNight, kT377WeatherMod, VisionType::Ultravision));

    EXPECT_FLOAT_EQ(clearBuff, rainBuff) << "the floor is weather-independent";
    EXPECT_GT(rainBuff / rainBase, (clearBuff / clearBase) * 1.9f)
        << "T377's rain more than doubled the apparent Ultravision ratio";
}

// Daylight sits at or barely under the Ultravision floor, so a vision buff is
// nearly -- not exactly -- invisible at noon. See the long note on
// VisionAmbientTest.DaylightIsUntouchedByInfravisionAndBarelyMovedByUltravision
// for why T428 had to give this up, and board B447 for the oracle question it
// leaves open.
TEST(VisionAmbientCompositionT396Test, DaylightIsAtMostBarelyMovedByAFloor) {
    const AmbientRGB plain =
        composeSceneAmbient(kDay, 1.0f, VisionType::Ultravision);
    EXPECT_NEAR(plain.r, kDay.r, kDay.r * 0.03f);
    EXPECT_NEAR(plain.g, kDay.g, kDay.g * 0.03f);
    EXPECT_FLOAT_EQ(plain.b, kDay.b);
    const AmbientRGB infra =
        composeSceneAmbient(kDay, 1.0f, VisionType::Infravision);
    EXPECT_FLOAT_EQ(infra.r, kDay.r);
    EXPECT_FLOAT_EQ(infra.g, kDay.g);
    EXPECT_FLOAT_EQ(infra.b, kDay.b);
}

// T424 (board B380). The fourth term this composition used to carry -- a
// "user ambient multiplier", clamped per channel, sitting between the
// day/night curve and the weather modifier -- is GONE, and this test is what
// keeps it gone. Its only writer was a Page Up / Page Down handler in the
// renderer's ADMIN mode, deleted with that whole mode in cac31166 ("Remove
// Admin and Repair renderer modes, keeping only Player mode"); from then until
// T424 the field was a constant 1.0 that every frame multiplied by, and T423
// found it while enumerating the inputs its ambient dirty flag has to watch.
// A term nothing can move is not an input. If a user-adjustable ambient is
// ever wanted, it arrives as a key binding or a slash command WITH a writer
// and WITH markSceneAmbientDirty(), not as a resurrected field.
TEST(VisionAmbientCompositionT396Test, NoUserAmbientMultiplierRemains) {
    // The declaration is gone (the header keeps a prose tombstone naming it,
    // so this looks for the DECLARATION, not the word) ...
    {
        const std::string path = std::string(WILLEQ_REPO_ROOT) +
                                 "/include/client/graphics/irrlicht_renderer.h";
        std::ifstream in(path);
        ASSERT_TRUE(in.good()) << "cannot open " << path;
        std::stringstream ss;
        ss << in.rdbuf();
        EXPECT_EQ(ss.str().find("float ambientMultiplier_"), std::string::npos)
            << path << " declares ambientMultiplier_ again. The field had no "
               "writer from cac31166 until T424 deleted it (board B380); "
               "re-adding it needs a writer and a markSceneAmbientDirty() "
               "call, not a default.";
    }
    // ... and no code names it at all.
    for (const char* rel : {"/include/client/graphics/vision_ambient.h",
                            "/src/client/graphics/vision_ambient.cpp",
                            "/src/client/graphics/renderer/irrlicht_renderer.cpp"}) {
        const std::string path = std::string(WILLEQ_REPO_ROOT) + rel;
        std::ifstream in(path);
        ASSERT_TRUE(in.good()) << "cannot open " << path;
        std::stringstream ss;
        ss << in.rdbuf();
        EXPECT_EQ(ss.str().find("ambientMultiplier"), std::string::npos)
            << path << " names the user ambient multiplier again (board B380)";
    }
    // And the signature is three arguments, so no caller can pass one.
    const std::string hdr = std::string(WILLEQ_REPO_ROOT) +
                            "/include/client/graphics/vision_ambient.h";
    std::ifstream in(hdr);
    ASSERT_TRUE(in.good());
    std::stringstream ss;
    ss << in.rdbuf();
    EXPECT_NE(ss.str().find("composeSceneAmbient(AmbientRGB dayNight, float weatherModifier,"),
              std::string::npos)
        << "composeSceneAmbient() no longer takes exactly (dayNight, "
           "weatherModifier, vision)";
}

// The production site calls it -- so the order above is the one that ships.
TEST(VisionAmbientCompositionT396Test, UpdateTimeOfDayUsesTheComposition) {
    const char* kRendererTU =
        WILLEQ_REPO_PATH("src/client/graphics/renderer/irrlicht_renderer.cpp");
    std::ifstream in(kRendererTU);
    ASSERT_TRUE(in.good()) << "cannot open " << kRendererTU;
    std::stringstream ss;
    ss << in.rdbuf();
    const std::string src = ss.str();
    EXPECT_NE(src.find("composeSceneAmbient("), std::string::npos)
        << "updateTimeOfDay() open-codes the ambient composition again; the "
           "order is then pinned by nothing";
}
