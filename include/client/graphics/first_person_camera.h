#pragma once

/**
 * T300 -- the first-person camera, as constants plus one pure function.
 *
 * THE MECHANISM (T299 + T300 Leg A, measured in the official client; do not
 * re-litigate it from source alone):
 *
 *   First person is the player's ORDINARY world model, drawn normally, with
 *   the camera AT THE EYES. **The client clips nothing** -- world and NPC
 *   geometry draws at point-blank range (Leg A teleported the camera onto a
 *   fixed NPC in 0.5-unit steps; at d=0 its head and shoulders render sharply
 *   with no cut), so the near plane is under 1 unit. What hides the player is
 *   that **first-person pitch is CLAMPED on the down side**: the body hangs
 *   directly below the eye, and the frustum's lower edge is never allowed to
 *   pass the nadir, so you can never look far enough down to see yourself.
 *   A swing rotates the arm and weapon UP across a frame EDGE into view --
 *   Leg A's halberd frame shows the arm, hand and shoulder running off the
 *   bottom-RIGHT CORNER, and no frame in the session has a straight cut
 *   across it.
 *
 * A NEAR PLANE WAS THE EARLIER HYPOTHESIS AND IS REFUTED. Leg A's size sweep
 * killed both variants at once: at #size 30 (5x), 417 rendered frames of
 * emotes with the UI hidden show zero player geometry while the ground 34
 * units below is still drawn. A fixed ~7-unit plane would be poked through by
 * a 5x body; a size-scaled plane (~35 units) would have erased that ground.
 * Do not reintroduce near-plane hiding: first person must clip exactly as
 * third person does.
 *
 * There is no viewmodel pass (T296's, retired here), no arm-bone geometry
 * filter (T74's, deleted here), no standoff (T76's, removed here), and no
 * near-plane or size-derived hiding of any kind.
 *
 * THE HIDING MECHANISM IS DELIBERATELY ISOLATED. Everything that hides the
 * player lives behind two names: firstPersonMaxDownPitchDegrees() here, and
 * IrrlichtRenderer::applyFirstPersonPitchClamp() (its only production
 * caller). Swapping the mechanism again is those two bodies; the camera
 * placement formula below is untouched by it.
 *
 * WHY THIS IS A HEADER, AND WHY THE MATH IS PURE: the first-person camera
 * formula has been duplicated four times across this codebase (T74, T75,
 * T76, T295 each found another copy), and every duplicate silently discarded
 * some fix. Keeping the constants and the placement math in one header that
 * one .cpp reads lets tests/test_first_person_camera_single_formula.cpp
 * assert BOTH that the numbers live in exactly one place and that they are
 * what they claim to be, numerically, headlessly.
 */

#include <cmath>

namespace EQT {
namespace Graphics {

// -------------------------------------------------------------------------
// The measured target (T298 Leg A, official Titanium client, ring-ladder and
// marker-pair fits, 2000-sample Monte Carlo error bars):
//   forward offset  0.00 +/- 0.4  EQ units   (no standoff, in any direction)
//   lateral offset  0.00 +/- 0.05 EQ units
//   eye height      6.75 +/- 0.20 EQ units above the player's server z
//   pitch           3.65 +/- 0.15 degrees DOWN
//   horizontal FOV  62.8 +/- 0.3 degrees  (vertical 49.3 at 4:3)
// -------------------------------------------------------------------------

// Fixed downward bias applied to the look ray, on top of the user's
// mouse-look pitch. T75 shipped 12 degrees; that number was chosen to tip
// the frustum onto in-world arm geometry, which is not what first person is
// for. 3.65 is the official client's measured value.
constexpr float kFirstPersonPitchDownDegrees = 3.65f;

// Horizontal field of view. fable never called setFOV(), so the world camera
// ran at Irrlicht's default 72 degrees VERTICAL (PI/2.5), which at 4:3 is
// ~88 degrees horizontal -- far wider than the official client's 62.8.
constexpr float kFirstPersonHorizontalFovDegrees = 62.8f;

// THE DOWN-PITCH CLAMP IS THE ONE LEVER THAT HIDES THE PLAYER'S BODY.
//
//     maxDownPitch = 90 degrees - vFOV/2
//
// i.e. exactly the angle at which the frustum's LOWER EDGE reaches straight
// down (the nadir). Below that the edge swings behind the vertical and the
// body -- which hangs directly under the eye -- comes into frame.
//
// WHY DERIVED AND NOT HARDCODED: this form is automatically scale-invariant,
// race-invariant and immune to shrink/grow, because the body's ANGULAR
// position relative to the eye does not depend on how big the body is. It
// needs no per-race table and no runtime size term at all -- which is
// precisely the requirement a fixed or size-derived near plane could not meet.
// It also tracks the FOV: widen the lens and the clamp tightens by itself.
//
// *** 90 - vFOV/2 IS AN INFERENCE CONSISTENT WITH THE MEASUREMENT, NOT ITSELF
// A MEASURED ANGLE. *** Leg A measured max down-pitch at roughly 65 degrees,
// bracket 55-75, and independently re-derived hFOV 62.71 (+/- 0.25
// statistical, 0.9 systematic) == vFOV 49.1 at 1024x768. 90 - 49.1/2 = 65.45
// lands inside that bracket and matches Leg A's own description of the lower
// edge sitting at the nadir -- which is why this form was chosen over the
// bracket's midpoint. **A dedicated measurement of the clamp itself is the
// outstanding follow-up**, and nothing here has been judged on a screen.
inline float firstPersonMaxDownPitchDegrees(float verticalFovRadians) {
    constexpr float kPi = 3.14159265358979323846f;
    const float halfFovDeg = verticalFovRadians * 0.5f * 180.0f / kPi;
    return 90.0f - halfFovDeg;
}

// The clamp applies to the camera's TOTAL down-pitch -- the mouse-look angle
// PLUS the fixed kFirstPersonPitchDownDegrees bias, since it is the resulting
// frustum that must not pass the nadir. This is the corresponding floor for
// the STORED mouse-look value (playerPitch_, + = up), so the accumulator and
// the camera formula clamp the same thing in the same place in the chain.
inline float firstPersonMinLookPitchDegrees(float verticalFovRadians) {
    return -(firstPersonMaxDownPitchDegrees(verticalFovRadians) -
             kFirstPersonPitchDownDegrees);
}

// The gimbal clamp on the UP side is NOT part of the hiding mechanism and is
// unchanged at +89: the body hangs BELOW the eye, so looking up moves it
// further out of frame, and Leg A measured only the down side. 89 exists to
// keep the look-at camera from degenerating when the target sits exactly
// above the eye. Named here so the two clamps are not confused for each other.
constexpr float kFirstPersonMaxUpPitchDegrees = 89.0f;

// The world (non-first-person) near plane, restored when leaving first
// person. Matches setupCamera()'s original setNearValue(1.0f).
constexpr float kWorldNearPlaneUnits = 1.0f;

// Irrlicht's ICameraSceneNode default vertical FOV (PI/2.5), restored when
// leaving first person so third person renders exactly as it did before
// T300 -- the visual gate's goldens depend on it.
constexpr float kWorldVerticalFovRadians = 3.14159265358979323846f / 2.5f;

constexpr float kFirstPersonPi = 3.14159265358979323846f;

// Irrlicht's setFOV() takes the VERTICAL angle; the measurement above is
// horizontal, so convert through the camera's aspect ratio.
inline float firstPersonVerticalFovRadians(float aspect) {
    if (!(aspect > 0.0f)) aspect = 4.0f / 3.0f;
    const float halfH = kFirstPersonHorizontalFovDegrees * 0.5f * kFirstPersonPi / 180.0f;
    return 2.0f * std::atan(std::tan(halfH) / aspect);
}

// The camera's placement, in Irrlicht world space (EQ x -> Irr X,
// EQ z -> Irr Y, EQ y -> Irr Z), as the renderer's own convention has it.
struct FirstPersonCameraPlacement {
    float posX = 0.0f, posY = 0.0f, posZ = 0.0f;
    float targetX = 0.0f, targetY = 0.0f, targetZ = 0.0f;
};

/**
 * The ONE first-person camera formula.
 *
 * @param playerX,playerY   the renderer's playerX_/playerY_ (EQ XY, already
 *                          in the renderer's swapped convention).
 * @param playerZ           the renderer's playerZ_. **This is the player NODE
 *                          ORIGIN, i.e. the server's Z, which sits INSIDE the
 *                          body** -- createEntity() places the model centred
 *                          on it ("Server Z is the geometric MODEL CENTER"),
 *                          with the Human male mesh spanning Y=[-3.05,+3.65]
 *                          about it. It is NOT the feet, whatever the old
 *                          name getPlayerEyeHeightFromFeet() implied.
 * @param eyeAboveNodeOrigin
 *                          EntityRenderer::getPlayerEyeHeightAboveNodeOrigin(),
 *                          the base mesh's bbox.MaxEdge.Y (3.65 for a Human
 *                          male) -- the crown of the head, ~6.70 above the
 *                          mesh's own feet. THAT is the official client's
 *                          measured 6.75-above-server-z eye, expressed in
 *                          fable's frame. Adding 6.75 to this is a 3.05-unit
 *                          regression; see the header comment on
 *                          getPlayerEyeHeightAboveNodeOrigin().
 * @param eyeAdjustUnits    the user's +/- eye-height nudge (playerConfig_.eyeHeight).
 * @param headingUnits      EQ heading, 0..512.
 * @param lookPitchDegrees  the user's mouse-look pitch, + = up. Clamped here:
 *                          see firstPersonMaxDownPitchDegrees().
 * @param verticalFovRadians
 *                          the camera's vertical FOV -- the clamp is derived
 *                          from it (90 - vFOV/2), which is what makes the
 *                          hiding scale- and race-invariant.
 * @param targetDistance    how far along the look ray to put the look target;
 *                          a look-at camera does not care about the value.
 *
 * There is NO forward/backward standoff: the camera sits at the player's own
 * XY, which is what T298 measured (0.00 +/- 0.4) and what T76's 0.3 backward
 * standoff -- introduced to clear in-world arm geometry that no longer exists
 * as a separate pass -- got wrong in sign as well as purpose.
 */
inline FirstPersonCameraPlacement computeFirstPersonCamera(
    float playerX, float playerY, float playerZ,
    float eyeAboveNodeOrigin, float eyeAdjustUnits,
    float headingUnits, float lookPitchDegrees,
    float verticalFovRadians,
    float targetDistance = 100.0f)
{
    FirstPersonCameraPlacement out;

    const float headingRad = headingUnits / 512.0f * 2.0f * kFirstPersonPi;

    // THE HIDING MECHANISM, applied at the point of use so no caller can get
    // around it: the TOTAL down-pitch is clamped so the frustum's lower edge
    // never passes the nadir. The accumulator (renderer_player_physics.cpp)
    // clamps the stored value too -- that is what keeps the control from
    // feeling dead against an invisible wall -- but this is the guarantee.
    const float maxDownDeg = firstPersonMaxDownPitchDegrees(verticalFovRadians);
    float totalPitchDeg = lookPitchDegrees - kFirstPersonPitchDownDegrees;
    if (totalPitchDeg < -maxDownDeg) totalPitchDeg = -maxDownDeg;
    if (totalPitchDeg > kFirstPersonMaxUpPitchDegrees) totalPitchDeg = kFirstPersonMaxUpPitchDegrees;
    const float pitchRad = totalPitchDeg * kFirstPersonPi / 180.0f;
    const float cosPitch = std::cos(pitchRad);
    const float sinPitch = std::sin(pitchRad);

    const float camZ = playerZ + eyeAboveNodeOrigin + eyeAdjustUnits;

    out.posX = playerX;
    out.posY = camZ;
    out.posZ = playerY;

    out.targetX = playerX + std::sin(headingRad) * cosPitch * targetDistance;
    out.targetY = camZ + sinPitch * targetDistance;
    out.targetZ = playerY + std::cos(headingRad) * cosPitch * targetDistance;

    return out;
}

}  // namespace Graphics
}  // namespace EQT
