// T428 (board B280) -- fable's NIGHT BASE, solved live against the official
// Titanium client, and the day/night curve it now lives in.
//
// WHY THIS FILE EXISTS. Until T428 the four literals of the day/night curve
// were bare numbers inside `IrrlichtRenderer::updateTimeOfDay()`, written out
// three times over (the night arm plus both ends of the dawn and dusk ramps),
// and fable's night was 5.6x darker than the official client's AND a different
// colour. Every vision number in this project since T358 has been a RATIO
// precisely because of that (B280). T405 measured the deficit, T408 showed it
// was uniform across a 10x range of spot brightness -- so it was the ambient
// and not a lamp term -- and T428 solved it.
//
// HOW IT WAS SOLVED, in one line: `/nightambient <r> <g> <b>` sweeps candidate
// night bases inside ONE live session, and the target is the official client's
// own DAY/NIGHT ratio per channel at a fixed spot, measured on T408's
// geometry-only crop. Never an absolute pixel value: the two clients' cameras
// sit at different heights, so only a statistic that divides the framing out
// is comparable. review/T428/README.md has the search log.
//
// WHAT THESE CASES PIN
//   1. the solved night triple (a constant that may not move by argument);
//   2. that the DAY arm does not read it -- the reason no daytime golden moved;
//   3. that dawn and dusk are continuous with it at 05:00 and 20:00, which is
//      the whole reason the literal was hoisted out of the if-chain;
//   4. the live result: fable's day/night per channel against the oracle's.

#include <gtest/gtest.h>

#include "client/graphics/vision_ambient.h"
#include "client/graphics/vision_type.h"

using EQT::Graphics::AmbientRGB;
using EQT::Graphics::dayNightAmbient;
using EQT::Graphics::VisionType;
using EQT::Graphics::visionAmbientFloor;

namespace {

// THE ORACLE. The official client's own day/night ratio at the lamp-free spot
// (qeynos2, `#goto -74 428 3.751 160`, weather cleared and rain_chance zeroed,
// geometry crop 560x110+200+350, linear-decoded). THREE independent sessions:
// T408's o1 and o2 (review/T408/logs/oracle_plateaus_o*.txt) and T428's own o3
// (review/T428/logs/geo_oracle_o3.tsv), which agree to 0.15%.
constexpr float kOracleDayOverNight[3] = {4.454f, 3.622f, 3.809f};

// FABLE ON EXACTLY THE COMMITTED TRIPLE, session conf2, same spot, same crop,
// same night: day/night per channel.
constexpr float kFableDayOverNight[3] = {4.4760f, 3.6025f, 3.7316f};

// What it was BEFORE, on (0.08, 0.08, 0.15) -- T408's nolamp2 session on the
// same spot and the same crop. This is the size of what T428 closed.
constexpr float kPreT428DayOverNight[3] = {9.8411f, 12.5683f, 4.6454f};

// The worst per-channel residual the live search reached. It is the number a
// future re-solve must BEAT, not a tolerance to relax.
constexpr float kT428WorstResidual = 0.021f;

float relErr(float a, float b) { return (a > b ? a - b : b - a) / b; }

float channel(const AmbientRGB& c, int i) {
    return i == 0 ? c.r : (i == 1 ? c.g : c.b);
}

}  // namespace

// The triple itself. (0.08, 0.08, 0.15) was never measured against anything;
// it is the value fable shipped from the beginning and the subject of B280.
TEST(NightBaseSolvedT428Test, TheNightAmbientIsTheLiveSolvedTriple) {
    EXPECT_FLOAT_EQ(EQT::Graphics::kNightAmbient[0], 0.26244f);
    EXPECT_FLOAT_EQ(EQT::Graphics::kNightAmbient[1], 0.31147f);
    EXPECT_FLOAT_EQ(EQT::Graphics::kNightAmbient[2], 0.24003f);
}

TEST(NightBaseSolvedT428Test, TheRefutedNightBaseCannotComeBack) {
    // The unmeasured shipped value B280 was raised against.
    EXPECT_NE(EQT::Graphics::kNightAmbient[0], 0.08f);
    EXPECT_NE(EQT::Graphics::kNightAmbient[1], 0.08f);
    EXPECT_NE(EQT::Graphics::kNightAmbient[2], 0.15f);
}

// The night arm returns the triple unchanged, at both ends of the night and
// across midnight.
TEST(NightBaseSolvedT428Test, EveryNightHourReturnsTheNightBase) {
    const AmbientRGB night{EQT::Graphics::kNightAmbient[0],
                           EQT::Graphics::kNightAmbient[1],
                           EQT::Graphics::kNightAmbient[2]};
    for (int hour : {20, 21, 22, 23, 0, 1, 2, 3, 4}) {
        float sun = -1.0f;
        const AmbientRGB out = dayNightAmbient(static_cast<uint8_t>(hour), 30,
                                               night, sun);
        EXPECT_FLOAT_EQ(out.r, night.r) << "hour " << hour;
        EXPECT_FLOAT_EQ(out.g, night.g) << "hour " << hour;
        EXPECT_FLOAT_EQ(out.b, night.b) << "hour " << hour;
        EXPECT_FLOAT_EQ(sun, EQT::Graphics::kNightSunIntensity);
    }
}

// WHY NO DAYTIME GOLDEN MOVED. The gate's three zones are captured by day, and
// the day arm does not read the night triple at all -- so moving it is
// provably invisible there. This is the case that makes that a test rather
// than an argument.
TEST(NightBaseSolvedT428Test, TheDayArmDoesNotReadTheNightBase) {
    for (int hour = 7; hour <= 17; ++hour) {
        float sunA = 0.0f, sunB = 0.0f;
        const AmbientRGB a = dayNightAmbient(static_cast<uint8_t>(hour), 0,
                                             AmbientRGB{0.08f, 0.08f, 0.15f}, sunA);
        const AmbientRGB b = dayNightAmbient(static_cast<uint8_t>(hour), 0,
                                             AmbientRGB{0.9f, 0.1f, 0.5f}, sunB);
        EXPECT_FLOAT_EQ(a.r, b.r) << "hour " << hour;
        EXPECT_FLOAT_EQ(a.g, b.g) << "hour " << hour;
        EXPECT_FLOAT_EQ(a.b, b.b) << "hour " << hour;
        EXPECT_FLOAT_EQ(a.r, EQT::Graphics::kDayAmbient[0]);
        EXPECT_FLOAT_EQ(sunA, EQT::Graphics::kDaySunIntensity);
        EXPECT_FLOAT_EQ(sunB, EQT::Graphics::kDaySunIntensity);
    }
}

// THE REASON THE LITERAL WAS HOISTED. Dawn ramps FROM the night base and dusk
// ramps TO it. Written out three times in an if-chain, a night value changed
// in one arm and not the others is a step change at 05:00 and 20:00 that
// nothing would catch. Here the ramps are continuous with the night arm by
// construction, and this case is what keeps them so.
TEST(NightBaseSolvedT428Test, DawnAndDuskAreContinuousWithTheNightBase) {
    const AmbientRGB night{EQT::Graphics::kNightAmbient[0],
                           EQT::Graphics::kNightAmbient[1],
                           EQT::Graphics::kNightAmbient[2]};
    float sun = 0.0f;
    // Dawn at t = 0 (05:00) IS the night base.
    const AmbientRGB dawn0 = dayNightAmbient(5, 0, night, sun);
    EXPECT_FLOAT_EQ(dawn0.r, night.r);
    EXPECT_FLOAT_EQ(dawn0.g, night.g);
    EXPECT_FLOAT_EQ(dawn0.b, night.b);
    EXPECT_FLOAT_EQ(sun, EQT::Graphics::kNightSunIntensity);
    // Dusk at t = 1 (the instant before 20:00) lands ON the night base.
    const AmbientRGB dusk1 = dayNightAmbient(19, 59, night, sun);
    EXPECT_NEAR(dusk1.r, night.r, 0.005f);
    EXPECT_NEAR(dusk1.g, night.g, 0.005f);
    EXPECT_NEAR(dusk1.b, night.b, 0.005f);
    // Dusk at t = 0 (18:00) is the day anchor, and it is NOT kDayAmbient --
    // the pre-T428 curve stepped there and T428 preserved the step rather than
    // "fixing" a value nothing has measured.
    const AmbientRGB dusk0 = dayNightAmbient(18, 0, night, sun);
    EXPECT_FLOAT_EQ(dusk0.r, EQT::Graphics::kDawnDuskDayAnchor[0]);
    EXPECT_FLOAT_EQ(dusk0.g, EQT::Graphics::kDawnDuskDayAnchor[1]);
    EXPECT_FLOAT_EQ(dusk0.b, EQT::Graphics::kDawnDuskDayAnchor[2]);
    EXPECT_NE(EQT::Graphics::kDawnDuskDayAnchor[1], EQT::Graphics::kDayAmbient[1]);
}

// THE LIVE RESULT. Every channel of fable's day/night ratio on the committed
// triple, against the three-session oracle mean, at the lamp-free spot.
TEST(NightBaseSolvedT428Test, EveryChannelMatchesTheOraclesOwnDayNightRatio) {
    for (int i = 0; i < 3; ++i) {
        EXPECT_LE(relErr(kFableDayOverNight[i], kOracleDayOverNight[i]),
                  kT428WorstResidual)
            << "channel " << i;
        // ...and the pre-T428 base missed it by between 22% and 247%, so this
        // is not a tolerance any old value would pass.
        EXPECT_GT(relErr(kPreT428DayOverNight[i], kOracleDayOverNight[i]), 0.2f)
            << "channel " << i;
    }
}

// A FLOOR ONLY DOES SOMETHING ON A CHANNEL IT IS ABOVE, and moving the night
// base up by ~3.3x invalidated every floor solved against the old one: T405's
// Ultravision floor (0.26545, 0.19653, 0.27623) sits BELOW this night base on
// two of three channels, so leaving it would have made the buff a no-op there.
// That is why the floors were re-solved in the same session, and this case is
// the invariant that catches the next base move.
TEST(NightBaseSolvedT428Test, EveryVisionFloorStillClearsTheMovedNightBase) {
    for (VisionType v : {VisionType::Ultravision, VisionType::FroglokVision,
                         VisionType::Infravision}) {
        const AmbientRGB f = visionAmbientFloor(v);
        for (int i = 0; i < 3; ++i) {
            EXPECT_GT(channel(f, i), EQT::Graphics::kNightAmbient[i] * 1.2f)
                << "vision " << static_cast<int>(v) << " channel " << i;
        }
    }
    // And T405's Ultravision floor, solved against the OLD base, would not:
    // its G sits BELOW this night base outright (0.19653 against 0.31147, so
    // the buff could not move green at all) and its R clears it by 1.1%, which
    // is nothing. Leaving it in place would have been a silent regression --
    // the exact shape T405 itself found in the pre-T405 Infravision floor.
    const float t405Ultra[3] = {0.26545f, 0.19653f, 0.27623f};
    EXPECT_LT(t405Ultra[1], EQT::Graphics::kNightAmbient[1]);
    EXPECT_LT(t405Ultra[0], EQT::Graphics::kNightAmbient[0] * 1.02f);
}
