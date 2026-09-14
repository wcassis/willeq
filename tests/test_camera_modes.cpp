// T461 -- the official camera set, the orbit rig's geometry and `/camera`'s
// grammar, all device-free (camera_rig.cpp + camera_command.cpp only).
//
// What is pinned, and against what:
//   - the set, its names and its order: eqstr_us.txt 9500..9505, quoted in
//     include/client/graphics/player/camera_mode.h;
//   - the legacy `[Camera] Mode=` mapping (0 Free, 1 Follow, 2 FirstPerson);
//   - the rig geometry for known az/pitch/dist/height/player pose, including
//     the heading convention: forward = (sin h, cos h), the vector
//     CameraController::setFollowPosition() puts the Chase camera behind, so
//     heading 0 faces +Y and heading 256 faces -Y (docs/CLAUDE.md: 0 = North
//     +Y, 256 = South -Y);
//   - every `/camera` form and every class of bad input.

#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <string>

#include "client/graphics/player/camera_mode.h"
#include "client/graphics/player/camera_rig.h"
#include "client/graphics/slash/camera_command.h"

using namespace EQT::Graphics;

namespace {

constexpr float kEps = 1e-4f;

// ---------------------------------------------------------------------------
// The set
// ---------------------------------------------------------------------------

TEST(CameraModeSet, EnumeratorsRunFromOneInEqstrOrder) {
    EXPECT_EQ(static_cast<int>(CameraMode::FirstPerson), 1);  // eqstr 9500
    EXPECT_EQ(static_cast<int>(CameraMode::Overhead), 2);     // 9501
    EXPECT_EQ(static_cast<int>(CameraMode::Chase), 3);        // 9502
    EXPECT_EQ(static_cast<int>(CameraMode::User1), 4);        // 9503
    EXPECT_EQ(static_cast<int>(CameraMode::User2), 5);        // 9504
    EXPECT_EQ(static_cast<int>(CameraMode::Tether), 6);       // 9505
}

TEST(CameraModeSet, NamesAreTheOfficialStringsVerbatim) {
    EXPECT_STREQ(cameraModeName(CameraMode::FirstPerson), "First Person Camera");
    EXPECT_STREQ(cameraModeName(CameraMode::Overhead), "Overhead Camera");
    EXPECT_STREQ(cameraModeName(CameraMode::Chase), "Chase Camera");
    EXPECT_STREQ(cameraModeName(CameraMode::User1), "User Defined Camera 1");
    EXPECT_STREQ(cameraModeName(CameraMode::User2), "User Defined Camera 2");
    EXPECT_STREQ(cameraModeName(CameraMode::Tether), "Tether Camera");
}

TEST(CameraModeSet, TheCycleWalksAllSixInOrderAndWraps) {
    CameraMode m = CameraMode::FirstPerson;
    const CameraMode order[] = {CameraMode::Overhead, CameraMode::Chase, CameraMode::User1,
                                CameraMode::User2, CameraMode::Tether, CameraMode::FirstPerson};
    for (CameraMode expected : order) {
        m = nextCameraMode(m);
        EXPECT_EQ(m, expected);
    }
}

// T471: the login camera is MEASURED, not fable's old Chase. T466 took the
// official client's first frame after entering world, before any key, and
// scored it against each direct camera: First Person 0.232, every other
// camera >= 0.897 (review/T466/logs/oracle_login_camera_scores.tsv). A saved
// `[Camera] View=` still wins (test_camera_zone_in_preference.cpp).
TEST(CameraModeSet, TheDefaultIsTheOfficialLoginCameraFirstPerson) {
    EXPECT_EQ(kDefaultCameraMode, CameraMode::FirstPerson);
}

TEST(CameraModeSet, RigAndUserModeClassification) {
    EXPECT_FALSE(isRigCameraMode(CameraMode::FirstPerson));
    EXPECT_TRUE(isRigCameraMode(CameraMode::Chase));  // T474 (B650): the fit is a rig
    EXPECT_TRUE(isRigCameraMode(CameraMode::Overhead));
    EXPECT_TRUE(isRigCameraMode(CameraMode::User1));
    EXPECT_TRUE(isRigCameraMode(CameraMode::User2));
    EXPECT_TRUE(isRigCameraMode(CameraMode::Tether));
    EXPECT_TRUE(isUserCameraMode(CameraMode::User1));
    EXPECT_TRUE(isUserCameraMode(CameraMode::User2));
    EXPECT_FALSE(isUserCameraMode(CameraMode::Overhead));
    EXPECT_FALSE(isUserCameraMode(CameraMode::Tether));
}

TEST(CameraModeSet, ParseAcceptsEveryOfficialNameAndOneShortFormEach) {
    for (int v = 1; v <= 6; ++v) {
        CameraMode in;
        ASSERT_TRUE(cameraModeFromIniValue(v, in));
        CameraMode out;
        ASSERT_TRUE(parseCameraModeName(cameraModeName(in), out)) << cameraModeName(in);
        EXPECT_EQ(out, in);
    }
    const std::pair<const char*, CameraMode> shortForms[] = {
        {"fp", CameraMode::FirstPerson}, {"firstperson", CameraMode::FirstPerson},
        {"First-Person", CameraMode::FirstPerson}, {"overhead", CameraMode::Overhead},
        {"CHASE", CameraMode::Chase}, {"user1", CameraMode::User1},
        {"user 2", CameraMode::User2}, {"tether", CameraMode::Tether}};
    for (const auto& [text, mode] : shortForms) {
        CameraMode out;
        ASSERT_TRUE(parseCameraModeName(text, out)) << text;
        EXPECT_EQ(out, mode) << text;
    }
}

TEST(CameraModeSet, ParseRejectsTheRetiredNamesAndGarbage) {
    for (const char* text : {"free", "Follow", "", "   ", "user3", "sidescroll", "camera"}) {
        CameraMode out = CameraMode::Chase;
        EXPECT_FALSE(parseCameraModeName(text, out)) << "'" << text << "'";
    }
}

TEST(CameraModeSet, IniValueAcceptsOneToSixOnly) {
    CameraMode out;
    EXPECT_FALSE(cameraModeFromIniValue(0, out));
    EXPECT_FALSE(cameraModeFromIniValue(7, out));
    EXPECT_FALSE(cameraModeFromIniValue(-1, out));
}

TEST(CameraModeSet, TheLegacyModeNumbersMapExplicitly) {
    CameraMode out;
    ASSERT_TRUE(cameraModeFromLegacyIniMode(0, out));
    EXPECT_EQ(out, CameraMode::Chase);        // Free
    ASSERT_TRUE(cameraModeFromLegacyIniMode(1, out));
    EXPECT_EQ(out, CameraMode::Chase);        // Follow
    ASSERT_TRUE(cameraModeFromLegacyIniMode(2, out));
    EXPECT_EQ(out, CameraMode::FirstPerson);  // FirstPerson
    EXPECT_FALSE(cameraModeFromLegacyIniMode(3, out));
    EXPECT_FALSE(cameraModeFromLegacyIniMode(-1, out));
}

// ---------------------------------------------------------------------------
// The rig
// ---------------------------------------------------------------------------

void ExpectPose(const CameraRigPose& p, float cx, float cy, float cz, float tx, float ty,
                float tz) {
    EXPECT_NEAR(p.camX, cx, kEps);
    EXPECT_NEAR(p.camY, cy, kEps);
    EXPECT_NEAR(p.camZ, cz, kEps);
    EXPECT_NEAR(p.targetX, tx, kEps);
    EXPECT_NEAR(p.targetY, ty, kEps);
    EXPECT_NEAR(p.targetZ, tz, kEps);
}

TEST(CameraRigGeometry, HeadingZeroAzimuthZeroSitsBehindAlongMinusY) {
    const CameraRig rig{0.0f, 0.0f, 10.0f, 5.0f};
    ExpectPose(computeCameraRigPose(rig, 100.0f, 200.0f, 3.0f, 0.0f),
               100.0f, 190.0f, 8.0f, 100.0f, 200.0f, 8.0f);
}

TEST(CameraRigGeometry, HeadingConventionMatchesDocsNorthAndSouth) {
    const CameraRig rig{0.0f, 0.0f, 10.0f, 0.0f};
    // 256 = South (-Y forward): behind the player is +Y.
    ExpectPose(computeCameraRigPose(rig, 0.0f, 0.0f, 0.0f, 256.0f),
               0.0f, 10.0f, 0.0f, 0.0f, 0.0f, 0.0f);
    // 128: the renderer's forward vector (sin h, cos h) is +X, so behind is -X.
    // This is the Chase camera's own convention (setFollowPosition), which is
    // what an azimuth of 0 must reproduce.
    ExpectPose(computeCameraRigPose(rig, 0.0f, 0.0f, 0.0f, 128.0f),
               -10.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f);
}

TEST(CameraRigGeometry, AzimuthZeroReproducesTheChaseCamerasDirectionAtEveryHeading) {
    const CameraRig rig{0.0f, 0.0f, 25.0f, 0.0f};
    for (float h = 0.0f; h < 512.0f; h += 37.0f) {
        const float rad = h / 512.0f * 2.0f * 3.14159265358979f;
        // CameraController::setFollowPosition(): camEq = eq - (sin, cos) * distance
        const CameraRigPose p = computeCameraRigPose(rig, 7.0f, -3.0f, 0.0f, h);
        EXPECT_NEAR(p.camX, 7.0f - std::sin(rad) * 25.0f, 1e-3f) << "heading " << h;
        EXPECT_NEAR(p.camY, -3.0f - std::cos(rad) * 25.0f, 1e-3f) << "heading " << h;
    }
}

TEST(CameraRigGeometry, AzimuthIsRelativeToTheHeadingInTheSameSense) {
    // 90 degrees of azimuth == 128 heading units.
    const CameraRig az90{90.0f, 0.0f, 10.0f, 0.0f};
    const CameraRig az0{0.0f, 0.0f, 10.0f, 0.0f};
    const CameraRigPose a = computeCameraRigPose(az90, 0.0f, 0.0f, 0.0f, 0.0f);
    const CameraRigPose b = computeCameraRigPose(az0, 0.0f, 0.0f, 0.0f, 128.0f);
    ExpectPose(a, b.camX, b.camY, b.camZ, b.targetX, b.targetY, b.targetZ);
    // 180 puts the camera in front of the player, looking back at it.
    ExpectPose(computeCameraRigPose(CameraRig{180.0f, 0.0f, 10.0f, 0.0f}, 0.0f, 0.0f, 0.0f, 0.0f),
               0.0f, 10.0f, 0.0f, 0.0f, 0.0f, 0.0f);
}

// T471 (board B580): User Defined 1/2 and Tether hold a WORLD azimuth -- the
// numbers a heading-0 player would read, whatever the heading.
TEST(CameraRigGeometry, AWorldAzimuthIgnoresTheHeading) {
    const CameraRig rig{90.0f, 10.0f, 20.0f, 4.0f};
    const CameraRigPose atZero = computeCameraRigPose(rig, 5.0f, 6.0f, 1.0f, 0.0f);
    for (float h = 0.0f; h < 512.0f; h += 41.0f) {
        const CameraRigPose p =
            computeCameraRigPose(rig, 5.0f, 6.0f, 1.0f, h, CameraAzimuthFrame::World);
        ExpectPose(p, atZero.camX, atZero.camY, atZero.camZ, atZero.targetX, atZero.targetY,
                   atZero.targetZ);
    }
    // World az 90: the camera on the -X side of the look-at point.
    EXPECT_LT(atZero.camX, 5.0f);
    EXPECT_NEAR(atZero.camY, 6.0f, 1e-3f);
}

TEST(CameraModeSet, TheAzimuthFrameIsTheMeasuredOne) {
    // T466: h160 vs h288 edge -- User 1 0.084, User 2 0.120, Tether 0.193 (world);
    // First Person 0.837, Overhead 0.689, Chase 0.680 (turn with the player).
    EXPECT_EQ(cameraAzimuthFrame(CameraMode::User1), CameraAzimuthFrame::World);
    EXPECT_EQ(cameraAzimuthFrame(CameraMode::User2), CameraAzimuthFrame::World);
    EXPECT_EQ(cameraAzimuthFrame(CameraMode::Tether), CameraAzimuthFrame::World);
    EXPECT_EQ(cameraAzimuthFrame(CameraMode::Overhead), CameraAzimuthFrame::PlayerHeading);
    EXPECT_EQ(cameraAzimuthFrame(CameraMode::Chase), CameraAzimuthFrame::PlayerHeading);
    EXPECT_EQ(cameraAzimuthFrame(CameraMode::FirstPerson), CameraAzimuthFrame::PlayerHeading);
    EXPECT_STREQ(cameraAzimuthFrameName(CameraAzimuthFrame::World), "world");
    EXPECT_STREQ(cameraAzimuthFrameName(CameraAzimuthFrame::PlayerHeading), "player_heading");
}

TEST(CameraRigGeometry, PitchRaisesTheCameraAndKeepsTheDistance) {
    const CameraRig rig{0.0f, 30.0f, 10.0f, 5.0f};
    const CameraRigPose p = computeCameraRigPose(rig, 0.0f, 0.0f, 1.0f, 0.0f);
    EXPECT_NEAR(p.targetZ, 6.0f, kEps);
    EXPECT_NEAR(p.camZ, 6.0f + 5.0f, kEps);                        // 10 * sin 30
    EXPECT_NEAR(p.camY, -10.0f * std::cos(30.0f * 3.14159265f / 180.0f), 1e-3f);
    const float dx = p.camX - p.targetX, dy = p.camY - p.targetY, dz = p.camZ - p.targetZ;
    EXPECT_NEAR(std::sqrt(dx * dx + dy * dy + dz * dz), 10.0f, 1e-3f);
}

TEST(CameraRigGeometry, TheLookAtHeightIsAboveTheFeet) {
    const CameraRig rig{0.0f, -20.0f, 12.0f, 4.5f};
    EXPECT_NEAR(computeCameraRigPose(rig, 0.0f, 0.0f, -7.0f, 99.0f).targetZ, -2.5f, kEps);
}

TEST(CameraRigClamp, ClampsPitchDistanceHeightAndWrapsAzimuth) {
    CameraRig r{-90.0f, 120.0f, 0.0f, 500.0f};
    EXPECT_TRUE(clampCameraRig(r));
    EXPECT_FLOAT_EQ(r.azimuthDeg, 270.0f);
    EXPECT_FLOAT_EQ(r.pitchDeg, kCameraRigMaxPitchDeg);
    EXPECT_FLOAT_EQ(r.distance, kCameraRigMinDistance);
    EXPECT_FLOAT_EQ(r.height, kCameraRigMaxHeight);

    CameraRig w{725.0f, -89.0f, 300.0f, -100.0f};
    EXPECT_FALSE(clampCameraRig(w)) << "wrapping is not clamping; the rest is in range";
    EXPECT_FLOAT_EQ(w.azimuthDeg, 5.0f);

    CameraRig big{0.0f, 0.0f, 1000.0f, 0.0f};
    EXPECT_TRUE(clampCameraRig(big));
    EXPECT_FLOAT_EQ(big.distance, kCameraRigMaxDistance);
}

TEST(CameraRigClamp, ANonFiniteValueFallsBackAndCountsAsClamped) {
    CameraRig r{std::numeric_limits<float>::quiet_NaN(), 0.0f,
                std::numeric_limits<float>::infinity(), 0.0f};
    EXPECT_TRUE(clampCameraRig(r));
    EXPECT_TRUE(std::isfinite(r.azimuthDeg));
    EXPECT_FLOAT_EQ(r.distance, CameraRig{}.distance);
}

TEST(CameraRigDefaults, UserCamerasQuoteTheOfficialIni) {
    // eqclient.ini [Options]: Camera1-Distance=30 Camera1-Height=5 Camera1-Pitch=0,
    // Camera2-Distance=82 Camera2-Height=18 Camera2-Pitch=0.
    EXPECT_FLOAT_EQ(kUser1DefaultRig.distance, 30.0f);
    EXPECT_FLOAT_EQ(kUser1DefaultRig.height, 5.0f);
    EXPECT_FLOAT_EQ(kUser1DefaultRig.pitchDeg, 0.0f);
    EXPECT_FLOAT_EQ(kUser2DefaultRig.distance, 82.0f);
    EXPECT_FLOAT_EQ(kUser2DefaultRig.height, 18.0f);
    EXPECT_FLOAT_EQ(kUser2DefaultRig.pitchDeg, 0.0f);
}

TEST(CameraRigDefaults, OverheadIsTheOracleFit) {
    // T466: fitted against the official client's Overhead frame at qeynos2
    // (-74, 428, 3.751) h160 -- minimum edge 0.774 at pitch 83, dist 45,
    // height 3, bracketed on every axis (review/T466/fable/sessions/fit4_*).
    // The old placeholder was pitch 70, dist 80; a revert to it is a red test.
    EXPECT_FLOAT_EQ(kOverheadRig.azimuthDeg, 0.0f);
    EXPECT_FLOAT_EQ(kOverheadRig.pitchDeg, 83.0f);
    EXPECT_FLOAT_EQ(kOverheadRig.distance, 45.0f);
    EXPECT_FLOAT_EQ(kOverheadRig.height, 3.0f);
    CameraRig copy = kOverheadRig;
    EXPECT_FALSE(clampCameraRig(copy)) << "the measured rig must sit inside the clamps";
}

TEST(CameraRigDefaults, ChaseIsTheOracleFit) {
    // T466: fitted against the official client's Chase frame at the same pose
    // -- minimum edge 0.7379 at pitch 6, dist 4.0, height 7.0 in fit 2 AND fit
    // 4, bracketed: dist 3.5/4.5 -> 0.767/0.839, height 6.5/7.5 -> 0.779/0.922,
    // pitch 3/9 -> 0.937/0.880 (review/T466/fable/sessions/fit4_*). Applied by
    // T474 (board B650); the pre-T474 follow camera sat 20 behind.
    EXPECT_FLOAT_EQ(kChaseRig.azimuthDeg, 0.0f);
    EXPECT_FLOAT_EQ(kChaseRig.pitchDeg, 6.0f);
    EXPECT_FLOAT_EQ(kChaseRig.distance, 4.0f);
    EXPECT_FLOAT_EQ(kChaseRig.height, 7.0f);
    CameraRig copy = kChaseRig;
    EXPECT_FALSE(clampCameraRig(copy)) << "the measured rig must sit inside the clamps";
    // T431: the fitted character's (Human male) oracle-solved eye above its feet.
    EXPECT_FLOAT_EQ(kChaseFitEyeAboveFeet, 6.023f);
}

TEST(CameraRigGeometry, TheChaseFitPutsTheCameraWhereT471ComputedIt) {
    // T471: "camera 3.978 behind and 7.418 above the feet, looking at the
    // player's axis at 7.0, pitch 6.0 down".
    const CameraRigPose p = computeCameraRigPose(kChaseRig, 0.0f, 0.0f, 0.0f, 0.0f);
    ExpectPose(p, 0.0f, -3.97809f, 7.41811f, 0.0f, 0.0f, 7.0f);
}

// T474: the race scaling (camera_rig.h scaleChaseRigForEye()). Written with the
// API it names, so it has no red-before run of its own.
TEST(ChaseRaceScale, TheFittedCharactersEyeLeavesTheFitExact) {
    EXPECT_EQ(scaleChaseRigForEye(kChaseRig, kChaseFitEyeAboveFeet), kChaseRig);
}

TEST(ChaseRaceScale, AnotherEyeScalesDistanceAndHeightUniformlyAboutTheFeet) {
    // T431's oracle-solved Dwarf male eye, 3.58 above its feet -- used here only
    // as a second size; the Dwarf's Chase geometry itself is unmeasured (B681).
    const float eye = 3.58f;
    const float s = eye / kChaseFitEyeAboveFeet;
    const CameraRig r = scaleChaseRigForEye(kChaseRig, eye);
    EXPECT_FLOAT_EQ(r.azimuthDeg, kChaseRig.azimuthDeg);
    EXPECT_FLOAT_EQ(r.pitchDeg, kChaseRig.pitchDeg) << "a uniform scale keeps the view angle";
    EXPECT_NEAR(r.distance, kChaseRig.distance * s, 1e-5f);
    EXPECT_NEAR(r.height, kChaseRig.height * s, 1e-5f);
    // So every pose coordinate above the feet scales by s.
    const CameraRigPose big = computeCameraRigPose(kChaseRig, 0.0f, 0.0f, 0.0f, 100.0f);
    const CameraRigPose small = computeCameraRigPose(r, 0.0f, 0.0f, 0.0f, 100.0f);
    EXPECT_NEAR(small.camX, big.camX * s, 1e-4f);
    EXPECT_NEAR(small.camY, big.camY * s, 1e-4f);
    EXPECT_NEAR(small.camZ, big.camZ * s, 1e-4f);
    EXPECT_NEAR(small.targetZ, big.targetZ * s, 1e-4f);
}

TEST(ChaseRaceScale, ANonPositiveOrNonFiniteEyeLeavesTheRigUnscaled) {
    for (float eye : {0.0f, -2.0f, std::numeric_limits<float>::quiet_NaN(),
                      std::numeric_limits<float>::infinity()}) {
        EXPECT_EQ(scaleChaseRigForEye(kChaseRig, eye), kChaseRig) << eye;
    }
}

// ---------------------------------------------------------------------------
// `/camera` grammar
// ---------------------------------------------------------------------------

using Kind = CameraCommand::Kind;

TEST(CameraCommandParse, TheBareFormsAndSubcommands) {
    EXPECT_EQ(ParseCameraCommand("").kind, Kind::Report);
    EXPECT_EQ(ParseCameraCommand("   ").kind, Kind::Report);
    EXPECT_EQ(ParseCameraCommand("cycle").kind, Kind::Cycle);
    EXPECT_EQ(ParseCameraCommand("GET").kind, Kind::Get);
    EXPECT_EQ(ParseCameraCommand("help").kind, Kind::Help);
    const CameraCommand m = ParseCameraCommand("User Defined Camera 2");
    EXPECT_EQ(m.kind, Kind::SetMode);
    EXPECT_EQ(m.mode, CameraMode::User2);
}

TEST(CameraCommandParse, SetTakesAllFourInAnyOrder) {
    const CameraCommand c = ParseCameraCommand("set height 6 dist 40 pitch -20 az 90.5");
    ASSERT_EQ(c.kind, Kind::Set) << c.error;
    EXPECT_TRUE(c.edit.hasAzimuth && c.edit.hasPitch && c.edit.hasDistance && c.edit.hasHeight);
    EXPECT_FLOAT_EQ(c.edit.azimuthDeg, 90.5f);
    EXPECT_FLOAT_EQ(c.edit.pitchDeg, -20.0f);
    EXPECT_FLOAT_EQ(c.edit.distance, 40.0f);
    EXPECT_FLOAT_EQ(c.edit.height, 6.0f);
}

TEST(CameraCommandParse, SetAndNudgeTakeASubset) {
    const CameraCommand s = ParseCameraCommand("set dist 12");
    ASSERT_EQ(s.kind, Kind::Set) << s.error;
    EXPECT_TRUE(s.edit.hasDistance);
    EXPECT_FALSE(s.edit.hasAzimuth || s.edit.hasPitch || s.edit.hasHeight);
    const CameraCommand n = ParseCameraCommand("NUDGE AZ -5");
    ASSERT_EQ(n.kind, Kind::Nudge) << n.error;
    EXPECT_TRUE(n.edit.hasAzimuth);
    EXPECT_FLOAT_EQ(n.edit.azimuthDeg, -5.0f);
}

TEST(CameraCommandParse, EveryClassOfBadInputIsAnErrorThatPointsAtHelp) {
    for (const char* bad : {"set", "nudge", "set az", "set az 1 pitch", "set az abc",
                            "set az 5x", "set az nan", "set az inf", "set yaw 5",
                            "set az 1 az 2", "cycle now", "get all", "help me",
                            "sidescroll", "free", "follow"}) {
        const CameraCommand c = ParseCameraCommand(bad);
        EXPECT_EQ(c.kind, Kind::Error) << "'" << bad << "'";
        EXPECT_NE(c.error.find("/camera help"), std::string::npos) << bad << ": " << c.error;
    }
}

TEST(CameraCommandParse, TheUsageStatesEveryFormAndItsUnits) {
    std::string all;
    for (const auto& line : CameraCommandUsageLines()) all += line + "\n";
    for (const char* needle : {"/camera set", "/camera nudge", "/camera get", "/camera cycle",
                               "F9", "degrees", "EQ world units", "feet", "WORLD azimuth",
                               "az_frame="}) {
        EXPECT_NE(all.find(needle), std::string::npos) << needle << "\n" << all;
    }
}

}  // namespace
