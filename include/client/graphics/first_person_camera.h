#pragma once

/**
 * T300 -- the first-person camera, as constants plus one pure function.
 *
 * THE MECHANISM (settled by T299 against two client versions, classic and
 * Titanium; do not re-litigate it from source alone):
 *
 *   First person is the player's ORDINARY world model, drawn normally, with
 *   the camera AT THE EYES and a near clip plane large enough to swallow the
 *   body. Nothing of the player renders at rest -- at any pitch, including
 *   aimed straight down at his own feet -- yet the SAME leg reappears the
 *   instant a kick extends it, and the main-hand blade appears on every
 *   swing, always truncated at a clean cut with the hand and elbow never
 *   drawn. Geometry that reappears was CLIPPED, not omitted; geometry cut
 *   cleanly with no attachment point was cut by a plane.
 *
 * There is no viewmodel pass (T296's, retired here), no arm-bone geometry
 * filter (T74's, deleted here), and no standoff (T76's, removed here).
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

// The near clip plane, in EQ units from the eye. THIS IS THE ONE LEVER THAT
// HIDES THE PLAYER'S BODY; there is no geometry filter and no hidden node
// behind it.
//
// *** PLACEHOLDER, NOT A MEASUREMENT. *** T300 Leg A is measuring this live
// off the truncation cut across a two-hander's swing arc. Until that number
// lands, this is the smallest value the T299 evidence certainly supports:
//
//   LOWER BOUND, hard: > 6.75. Nothing of the body renders with the camera
//   aimed straight down at feet T298 measured 6.75 units below the eye, so
//   the plane is beyond the far side of the standing body.
//   UPPER BOUND, soft: less than the reach of an extended weapon or kick,
//   because those DO render. T299 could not size that reach; a halberd's
//   blade is the longest lever and Leg A is solving for it.
//
// 7.0 is chosen as a documented placeholder just above the hard lower bound.
// Replace it with Leg A's measured value; do not tune it by eye.
//
// Cost, stated honestly: a 7-unit near plane is large (a Human male is 6.70
// units tall), so world geometry within 7 units of the eye -- a wall the
// player stands against, the ground when looking steeply down -- will clip
// away. That is a property of the mechanism the official client uses, not a
// defect introduced here, but the live gate (Leg C) is what judges it. It is
// applied ONLY in first person; Follow/Free keep the world's 1.0 plane.
constexpr float kFirstPersonNearPlaneUnits = 7.0f;

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
 * @param lookPitchDegrees  the user's mouse-look pitch, + = up.
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
    float targetDistance = 100.0f)
{
    FirstPersonCameraPlacement out;

    const float headingRad = headingUnits / 512.0f * 2.0f * kFirstPersonPi;
    const float pitchRad = lookPitchDegrees * kFirstPersonPi / 180.0f -
                           kFirstPersonPitchDownDegrees * kFirstPersonPi / 180.0f;
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
