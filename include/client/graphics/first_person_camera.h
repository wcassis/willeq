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
 * NOTHING IN THIS HEADER HIDES THE PLAYER. T300 claimed the down-pitch clamp
 * did; T301 Leg B and T307 both refuted it (the measured clamp reaches PAST
 * the nadir, and the frustum plainly contains the body there). Since T309 the
 * body is hidden by the measured DRAW RULE --
 * include/client/graphics/first_person_body_draw.h, driven per frame by
 * EntityRenderer::updateFirstPersonBodyDrawRule() -- which withholds the
 * model from the render list except while an attack animation is playing.
 * firstPersonMaxDownPitchDegrees() below is a CONTROL-FEEL limit: how far
 * down the official client lets you look. The camera placement formula is
 * untouched by either.
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

#include "client/graphics/first_person_eye.h"
#include "client/graphics/first_person_tunables.h"

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

// T310 Leg A: kFirstPersonPitchDownDegrees (3.65) and
// kFirstPersonHorizontalFovDegrees (62.8) MOVED, values and comments
// unchanged, to include/client/graphics/first_person_tunables.h -- which this
// header includes, so every existing spelling still resolves. They moved only
// so the runtime tunable table can name them as its defaults without a
// circular include. The formula below now reads the RUNTIME value
// (fpTunable(...)), which equals the constant unless /fpcam or --fpcam has
// overridden it. CHANGE THE CONSTANT, NOT THE READ.

// THE DOWN-PITCH CLAMP IS THE ONE LEVER THAT HIDES THE PLAYER'S BODY, and it
// is a SEPARATE, NAMED, MEASURED CONSTANT living in first_person_eye.h:
// kFirstPersonMaxDownPitchDegrees.
//
// *** IT IS NOT DERIVED FROM THE FOV, AND MUST NOT BE. *** T300 shipped
//
//     maxDownPitch = 90 degrees - vFOV/2
//
// on the reasoning that this is the angle at which the frustum's lower edge
// reaches the nadir. T301 REFUTED it: that is by construction the condition
// under which the frustum ADMITS the cone of directions the body occupies,
// not one that excludes it. At that clamp the frustum spans 40.8-90.0 deg
// below the horizon and a Human male's body occupies exactly that cone (nose
// 51.1 deg, feet 90 deg) -- see review/T300/T300_C_12, which is a screenshot
// of the player's own face. It agreed with Leg A's 55-75 bracket by
// coincidence and was adopted without checking which side of the inequality
// it fell on.
//
// Its claimed race-invariance was false too: the angle from a crown-mounted
// eye to the nearest own geometry measured ~27 deg for a Human male and ~61
// for a Troll, because that angle is a property of mesh PROPORTIONS, not of
// size. The FOV argument has been REMOVED from this function and from
// computeFirstPersonCamera() so the coupling cannot be reintroduced by
// accident.
inline float firstPersonMaxDownPitchDegrees() {
    // T310 Leg A: the RUNTIME value, which is kFirstPersonMaxDownPitchDegrees
    // until /fpcam or --fpcam overrides it.
    return fpTunable(FpTunable::MaxDownPitchDegrees);
}

// The clamp applies to the camera's TOTAL down-pitch -- the mouse-look angle
// PLUS the fixed kFirstPersonPitchDownDegrees bias, since it is the resulting
// frustum that must not pass the nadir. This is the corresponding floor for
// the STORED mouse-look value (playerPitch_, + = up), so the accumulator and
// the camera formula clamp the same thing in the same place in the chain.
inline float firstPersonMinLookPitchDegrees() {
    return -(firstPersonMaxDownPitchDegrees() - fpTunable(FpTunable::PitchDownDegrees));
}

// T310 Leg A: kFirstPersonMaxUpPitchDegrees (89) and kWorldNearPlaneUnits
// (1.0) also moved to first_person_tunables.h, same reason, same values.

// Irrlicht's ICameraSceneNode default vertical FOV (PI/2.5), restored when
// leaving first person so third person renders exactly as it did before
// T300 -- the visual gate's goldens depend on it.
constexpr float kWorldVerticalFovRadians = 3.14159265358979323846f / 2.5f;

constexpr float kFirstPersonPi = 3.14159265358979323846f;

// Irrlicht's setFOV() takes the VERTICAL angle; the measurement above is
// horizontal, so convert through the camera's aspect ratio.
inline float firstPersonVerticalFovRadians(float aspect) {
    if (!(aspect > 0.0f)) aspect = 4.0f / 3.0f;
    const float halfH = fpTunable(FpTunable::HorizontalFovDegrees) * 0.5f *
                        kFirstPersonPi / 180.0f;
    return 2.0f * std::atan(std::tan(halfH) / aspect);
}

// The camera's placement, in Irrlicht world space (EQ x -> Irr X,
// EQ z -> Irr Y, EQ y -> Irr Z), as the renderer's own convention has it.
//
// THE UP VECTOR IS NOT DECORATION. T301 Leg A measured the official max
// down-pitch at 94.9 degrees -- about 5 degrees PAST THE NADIR. A look-at
// camera left on Irrlicht's default (0,1,0) up vector degenerates as the look
// ray approaches straight down (the ray becomes parallel to up, and the view
// basis is undefined) and then FLIPS as it crosses. Every caller must apply
// this vector, or the measured clamp cannot be used at all.
struct FirstPersonCameraPlacement {
    float posX = 0.0f, posY = 0.0f, posZ = 0.0f;
    float targetX = 0.0f, targetY = 0.0f, targetZ = 0.0f;
    float upX = 0.0f, upY = 1.0f, upZ = 0.0f;
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
 * @param eyeOffsetIrrX,eyeAboveNodeOrigin,eyeOffsetIrrZ
 *                          EntityRenderer::getPlayerFirstPersonEyeOffset(),
 *                          the offset from the node origin to the EYE in
 *                          Irrlicht world axes, derived per race from the
 *                          HEAD BONE's own vertex cluster (T301). It is
 *                          already rotated into world axes by the node, so
 *                          the heading must NOT be applied to it again.
 *
 *                          T300 passed the whole-body bbox.MaxEdge.Y here --
 *                          the CROWN OF THE SKULL -- and that is what put the
 *                          camera on top of the head. See
 *                          include/client/graphics/first_person_eye.h. The
 *                          horizontal components are non-zero for hunched
 *                          races (Iksar, Froglok) and near zero for the rest.
 * @param eyeAdjustUnits    the user's +/- eye-height nudge (playerConfig_.eyeHeight).
 * @param headingUnits      EQ heading, 0..512.
 * @param lookPitchDegrees  the user's mouse-look pitch, + = up. Clamped here:
 *                          see firstPersonMaxDownPitchDegrees().
 * @param targetDistance    how far along the look ray to put the look target;
 *                          a look-at camera does not care about the value.
 *
 * THE VERTICAL-FOV PARAMETER IS GONE. T300 used it to derive the down-pitch
 * clamp as 90 - vFOV/2; T301 refuted that (it is the condition that ADMITS
 * the body's cone, not one that excludes it) and the clamp is now the named
 * measured constant kFirstPersonMaxDownPitchDegrees. Removing the parameter
 * is deliberate: the coupling cannot come back by accident.
 *
 * There is NO forward/backward standoff beyond the head's own anatomical
 * position and kEyeForwardOffsetUnits: T298 measured the official eye at
 * 0.00 +/- 0.4 from the player's XY, and T76's 0.3 backward standoff --
 * introduced to clear in-world arm geometry that no longer exists as a
 * separate pass -- was wrong in sign as well as in purpose.
 */
inline FirstPersonCameraPlacement computeFirstPersonCamera(
    float playerX, float playerY, float playerZ,
    float eyeAboveNodeOrigin, float eyeAdjustUnits,
    float headingUnits, float lookPitchDegrees,
    float targetDistance = 100.0f,
    float eyeOffsetIrrX = 0.0f, float eyeOffsetIrrZ = 0.0f)
{
    FirstPersonCameraPlacement out;

    const float headingRad = headingUnits / 512.0f * 2.0f * kFirstPersonPi;

    // THE CONTROL-FEEL PITCH LIMIT (T309: not a hiding mechanism -- see the
    // file header), applied at the point of use so no caller can get around
    // it. The accumulator (renderer_player_physics.cpp) clamps the stored
    // value too -- that is what keeps the control from feeling dead against
    // an invisible wall -- but this is the guarantee.
    const float maxDownDeg = firstPersonMaxDownPitchDegrees();
    const float maxUpDeg = fpTunable(FpTunable::MaxUpPitchDegrees);
    float totalPitchDeg = lookPitchDegrees - fpTunable(FpTunable::PitchDownDegrees);
    if (totalPitchDeg < -maxDownDeg) totalPitchDeg = -maxDownDeg;
    if (totalPitchDeg > maxUpDeg) totalPitchDeg = maxUpDeg;
    const float pitchRad = totalPitchDeg * kFirstPersonPi / 180.0f;
    const float cosPitch = std::cos(pitchRad);
    const float sinPitch = std::sin(pitchRad);

    const float camZ = playerZ + eyeAboveNodeOrigin + eyeAdjustUnits;

    // The look ray's horizontal direction, reused for Leg A's forward nudge
    // so that constant needs no knowledge of any mesh's baked axes -- the
    // trap that broke T300 Leg B's per-race table (its error changed SIGN
    // between Human and Troll, so it was not an offset).
    const float fwdX = std::sin(headingRad);
    const float fwdZ = std::cos(headingRad);

    const float fwdOffset = fpTunable(FpTunable::EyeForwardOffsetUnits);
    out.posX = playerX + eyeOffsetIrrX + fwdX * fwdOffset;
    out.posY = camZ;
    out.posZ = playerY + eyeOffsetIrrZ + fwdZ * fwdOffset;

    out.targetX = out.posX + fwdX * cosPitch * targetDistance;
    out.targetY = camZ + sinPitch * targetDistance;
    out.targetZ = out.posZ + fwdZ * cosPitch * targetDistance;

    // The camera's own up vector, carried through and past the nadir.
    //
    // With forward F = (sinH*cosP, sinP, cosH*cosP) and the horizontal right
    // vector R = (cosH, 0, -sinH), up is F x R:
    //
    //     U = (-sinH*sinP, cosP, -cosH*sinP)
    //
    // At level pitch that is exactly (0,1,0), so nothing changes for an
    // ordinary view. At pitch -90 it becomes (sinH, 0, cosH) -- looking
    // straight down, screen-up is the direction you are facing, which is
    // correct and, more to the point, DEFINED. Past -90 its Y component goes
    // negative and the view continues smoothly over the top instead of
    // flipping. It is unit length for every pitch, so no normalisation is
    // needed and none is done.
    out.upX = -fwdX * sinPitch;
    out.upY = cosPitch;
    out.upZ = -fwdZ * sinPitch;

    return out;
}

}  // namespace Graphics
}  // namespace EQT
