// B875 (issue #219) -- the COMMITTED expected values of fable's Chase camera,
// shared by the two halves of the ChaseRigPinned pin:
//
//   tests/test_chase_rig_pinned.cpp  device-free: each constant, the eye-height
//                                    race scale, and the closed-form pose;
//   tests/test_pose_log_e2e.cpp      the camera a REAL headless renderer
//                                    places in Chase mode (ChaseRigPinnedE2E).
//
// PROVENANCE -- read this before citing these numbers anywhere.
//
// These are fable's CURRENT constants (include/client/graphics/player/
// camera_rig.h kChaseRig and kChaseFitEyeAboveFeet; first_person_tunables.h
// kFirstPersonHorizontalFovDegrees, which Chase shares since T466), copied here
// as literals on 2026-09-29. They were chosen by T466/T474's image-similarity
// sweep of fable's own renders against official-client stills, a fit T478
// (board B721) found depends on the scoring crop and left NOT SETTLED. They
// are corroborated AT REST by one real zoned-in fable run
// (refactor/B874/live/, merged to fable from task/pose-live-capture; run
// run-20260929T022314Z: pitch 6.0000, azimuth 0.0001 deg, distance 4.0002
// against kChaseRig over 129 stationary rows) -- which measured FABLE against
// its own constants, not the official client.
//
// They are NOT a measurement of the official client's chase camera. No
// camera-pose measurement of the official client exists yet; the one-time
// fit B875 owes is designed in refactor/B875/FIT_PLAN.md. This pin catches
// accidental drift of fable's camera; it does NOT prove oracle parity. When
// the owed fit lands, a change to these values is a product call on its own
// card (docs/decisions.md 2026-09-23), made by editing THIS file and
// camera_rig.h in the same commit.

#pragma once

namespace chase_rig_expected {

// kChaseRig, field by field (azimuth and pitch in degrees; distance and
// look-at height in EQ units, the height measured above the player's feet).
constexpr double kAzimuthDeg = 0.0;
constexpr double kPitchDeg = 6.0;
constexpr double kDistance = 4.0;
constexpr double kHeight = 7.0;

// kChaseFitEyeAboveFeet: the eye height at which the rig is placed unscaled.
// Every other eye scales distance and height by eye / kFitEyeAboveFeet.
constexpr double kFitEyeAboveFeet = 6.023;

// The horizontal field of view every third-person mode renders with (T466:
// the same as first person), degrees.
constexpr double kHorizontalFovDeg = 62.8;

// How far a constant may sit from its expected value. Tight on purpose: a
// 0.001 change to any one of them is a red test (the RED proofs are recorded
// in refactor/B875/README.md).
constexpr double kConstantTolerance = 1e-5;

}  // namespace chase_rig_expected
