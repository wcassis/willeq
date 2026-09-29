// B875 (issue #219): ChaseRigPinned -- the device-free half of the Chase
// camera pin. The renderer half (the camera a real headless IrrlichtRenderer
// places in Chase mode) is ChaseRigPinnedE2E in tests/test_pose_log_e2e.cpp;
// both read the committed expected values from tests/chase_rig_expected.h.
//
// WHAT THIS PINS, AND WHAT IT DOES NOT. The expected values are fable's
// CURRENT constants, corroborated at rest by one live fable run -- NOT a
// measurement of the official client (the provenance is written out in
// chase_rig_expected.h and refactor/B875/README.md). A red test here means
// someone moved fable's chase camera; it says nothing about oracle parity.
// The official-client fit is still owed (refactor/B875/FIT_PLAN.md).
//
// It supersedes MR !31's ChaseConstantsFitPinned, which read a JSON golden,
// checked only its structure and never compared a constant to kChaseRig (it
// stayed green with the distance at 4.001).
//
// The closed-form pose below is written out from the rig's documented meaning
// (camera_rig.h: azimuth 0 = straight behind the heading, pitch above
// horizontal, distance from the look-at, look-at `height` above the feet,
// forward = (sin h, cos h) with h = heading / 512 * 2pi) in double precision,
// from the EXPECTED literals -- never from kChaseRig and never by calling
// computeCameraRigPose() for the expectation -- so neither a constant nor the
// function can move without a red test.

#include <gtest/gtest.h>

#include <cmath>

#include "chase_rig_expected.h"
#include "client/graphics/first_person_tunables.h"
#include "client/graphics/player/camera_mode.h"
#include "client/graphics/player/camera_rig.h"

using namespace EQT::Graphics;
namespace ex = chase_rig_expected;

namespace {

constexpr double kPi = 3.14159265358979323846;

struct Pose {
    double camX, camY, camZ, tgtX, tgtY, tgtZ;
};

// The closed form, from the expected literals, for an eye `eye` above the
// feet (the race scale: distance and height by eye / kFitEyeAboveFeet).
Pose ExpectedChasePose(double x, double y, double feetZ, double headingEq512, double eye) {
    const double s = eye / ex::kFitEyeAboveFeet;
    const double dist = ex::kDistance * s;
    const double height = ex::kHeight * s;
    const double angle = headingEq512 / 512.0 * 2.0 * kPi + ex::kAzimuthDeg * kPi / 180.0;
    const double pitch = ex::kPitchDeg * kPi / 180.0;
    const double horiz = dist * std::cos(pitch);
    Pose p;
    p.tgtX = x;
    p.tgtY = y;
    p.tgtZ = feetZ + height;
    p.camX = x - std::sin(angle) * horiz;
    p.camY = y - std::cos(angle) * horiz;
    p.camZ = p.tgtZ + dist * std::sin(pitch);
    return p;
}

// float arithmetic in the production path at coordinates up to a few hundred
// units: ~3e-5 of rounding. 1e-4 still turns a 0.001 change of distance or
// height, or a 0.01 degree change of pitch or azimuth, red (the RED proofs
// in refactor/B875/README.md).
constexpr double kPoseTolerance = 1e-4;

}  // namespace

// (a) Each constant against its committed value.
TEST(ChaseRigPinned, EachConstantEqualsItsCommittedValue) {
    EXPECT_NEAR(kChaseRig.azimuthDeg, ex::kAzimuthDeg, ex::kConstantTolerance) << "kChaseRig.azimuthDeg";
    EXPECT_NEAR(kChaseRig.pitchDeg, ex::kPitchDeg, ex::kConstantTolerance) << "kChaseRig.pitchDeg";
    EXPECT_NEAR(kChaseRig.distance, ex::kDistance, ex::kConstantTolerance) << "kChaseRig.distance";
    EXPECT_NEAR(kChaseRig.height, ex::kHeight, ex::kConstantTolerance) << "kChaseRig.height";
    EXPECT_NEAR(kChaseFitEyeAboveFeet, ex::kFitEyeAboveFeet, ex::kConstantTolerance)
        << "kChaseFitEyeAboveFeet (the eye at which the rig is unscaled)";
    EXPECT_NEAR(kFirstPersonHorizontalFovDegrees, ex::kHorizontalFovDeg, ex::kConstantTolerance)
        << "kFirstPersonHorizontalFovDegrees (Chase renders with it too, T466)";
}

TEST(ChaseRigPinned, ChaseIsHeadingRelativeAndInsideTheClamps) {
    // Azimuth 0 only means "behind the player" in the PlayerHeading frame.
    EXPECT_EQ(cameraAzimuthFrame(CameraMode::Chase), CameraAzimuthFrame::PlayerHeading);
    CameraRig copy = kChaseRig;
    EXPECT_FALSE(clampCameraRig(copy)) << "a clamp would move the pinned rig at runtime";
}

// (b, device-free) The production pose function, fed the production rig and
// the production race scale, equals the closed form of the EXPECTED values --
// three headings, two positions, two eye heights.
TEST(ChaseRigPinned, ProductionPoseEqualsTheClosedFormOfTheExpectedValues) {
    struct Where { double x, y, feetZ; };
    const Where positions[] = {{428.0, -74.0, 3.119}, {-150.75, 212.5, -12.25}};
    const double headings[] = {0.0, 123.25, 256.0, 400.5};
    // The fitted character's eye (the rig unscaled), and a second size --
    // T431's oracle-solved Dwarf male eye, 3.58 above its feet, used here
    // only as a second size (the Dwarf's Chase geometry is unmeasured, B681).
    const double eyes[] = {ex::kFitEyeAboveFeet, 3.58};

    for (const Where& w : positions) {
        for (double h : headings) {
            for (double eye : eyes) {
                SCOPED_TRACE(::testing::Message() << "pos (" << w.x << ", " << w.y << ", " << w.feetZ
                                                  << ") heading " << h << " eye " << eye);
                const CameraRig placed = scaleChaseRigForEye(kChaseRig, static_cast<float>(eye));
                const CameraRigPose got = computeCameraRigPose(
                    placed, static_cast<float>(w.x), static_cast<float>(w.y),
                    static_cast<float>(w.feetZ), static_cast<float>(h),
                    cameraAzimuthFrame(CameraMode::Chase));
                const Pose want = ExpectedChasePose(w.x, w.y, w.feetZ, h, eye);
                EXPECT_NEAR(got.camX, want.camX, kPoseTolerance) << "camX";
                EXPECT_NEAR(got.camY, want.camY, kPoseTolerance) << "camY";
                EXPECT_NEAR(got.camZ, want.camZ, kPoseTolerance) << "camZ";
                EXPECT_NEAR(got.targetX, want.tgtX, kPoseTolerance) << "targetX";
                EXPECT_NEAR(got.targetY, want.tgtY, kPoseTolerance) << "targetY";
                EXPECT_NEAR(got.targetZ, want.tgtZ, kPoseTolerance) << "targetZ";
            }
        }
    }
}

// The race scale in numbers, so a change to the scale LAW (not only to its
// input) is red too: at the Dwarf eye the rig is 3.58 / 6.023 of the fit.
TEST(ChaseRigPinned, TheRaceScaleIsUniformAboutTheFeet) {
    const double eye = 3.58;
    const CameraRig r = scaleChaseRigForEye(kChaseRig, static_cast<float>(eye));
    const double s = eye / ex::kFitEyeAboveFeet;
    EXPECT_NEAR(r.azimuthDeg, ex::kAzimuthDeg, ex::kConstantTolerance);
    EXPECT_NEAR(r.pitchDeg, ex::kPitchDeg, ex::kConstantTolerance) << "a uniform scale keeps the view angle";
    EXPECT_NEAR(r.distance, ex::kDistance * s, ex::kConstantTolerance);
    EXPECT_NEAR(r.height, ex::kHeight * s, ex::kConstantTolerance);
    // At the fitted eye the scale is the identity.
    EXPECT_EQ(scaleChaseRigForEye(kChaseRig, static_cast<float>(ex::kFitEyeAboveFeet)), kChaseRig);
}
