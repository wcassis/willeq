#pragma once

/**
 * T301 Leg B -- WHERE THE EYE IS, derived per race from the mesh.
 *
 * THE DEFECT THIS REPLACES. Until T301 the first-person eye was
 * `EntityRenderer::getPlayerEyeHeightAboveNodeOrigin()` == the base mesh's
 * WHOLE-BODY `bbox.MaxEdge.Y` -- 3.6527 for a Human male, i.e. the crown of
 * the skull. That is not an eye, it is a hat. Two live failures follow from
 * it and neither is fixable with a clamp value (T300 Leg C):
 *
 *   - the player's own face sits FORWARD OF and BELOW that point (51 deg
 *     depression for a Human male) and so is inside any plausible frustum;
 *   - the run cycle lifts the ANIMATED head ABOVE the STATIC crown, so the
 *     head bobs through frame at LEVEL pitch. (T301 Leg A later measured that
 *     the OFFICIAL client has this same theoretical bob and does not care,
 *     because it never draws the body at all -- see kFirstPersonEyeSource.)
 *
 * THE ANCHOR (established from the shipped client tree, not assumed). Every
 * playable skeleton names its head bone `<modelCode>he...`:
 *
 *     HUM humhe   HUF hufhe   BAM bamhe   ERM ermhe   ELM elmhe   HIM himhe
 *     DAM damhe   HAM hamhe   DWM dwmhe   TRM trmhe   OGM ogmhe   HOM homhe
 *     GNM gnmhe   IKM ikmhe   KEM kemhe   FRM frmhehead  (+ every female)
 *
 * 30 of 30 race/gender combinations resolve, and every one of them has real
 * skinned geometry on that bone (58..916 vertices). `getBoneIndexByName("HEAD")`
 * -- the lever T76 added and T295 logged as `source=BBOX_FALLBACK
 * headBoneIdx=-1` in every live session -- never fired because no EQ skeleton
 * has ever contained a bone called "HEAD". It looked for a name that does not
 * exist rather than for the name that does.
 *
 * TWO TRAPS in the name match, both real in the shipped data:
 *   - `<code>head_point` is an ATTACHMENT POINT (where a helm hangs), not the
 *     head, and it also starts with `<code>he`. So does `frmhehead_point`.
 *     Every `*_point` name is rejected.
 *   - `bamhe2` (Barbarian male) is a second head part. The SHORTEST surviving
 *     remainder wins, so plain `bamhe` is taken.
 *
 * WHY A VERTEX CLUSTER AND NOT THE BONE ORIGIN: the head bone's origin is the
 * NECK JOINT, at the base of the skull -- as wrong in the other direction as
 * the crown was. The eye is derived from the head bone's OWN VERTICES, which
 * is a per-race property of the mesh: no hand-written table exists here and
 * none may be added.
 *
 * WHY THE SKULL IS MEASURED SYMMETRICALLY ABOUT THE JOINT: the raw head
 * cluster's vertical span is NOT the skull for every race. The Dwarf male's
 * beard is skinned to `dwmhe` and hangs to local Z -2.29 against a crown at
 * +0.85; a naive min..max fraction puts the "eye" in his chest (model Z 1.48,
 * 2.27 below the crown, versus 0.63 for a Human male). Long hair does the
 * same. The head JOINT is at local Z 0 by construction, and a skull extends
 * about as far below the joint as above it, so the lower bound is taken as
 * `max(minZ, -maxZ)`: for a Dwarf that discards the beard, and for every race
 * whose cluster is already tighter than that (Troll, Iksar, Froglok) it
 * changes nothing.
 *
 * NOTHING HERE HAS BEEN RASTERIZED. The tests in
 * tests/test_first_person_eye_anatomy.cpp are geometry over the real shipped
 * meshes; they do not draw a single pixel and CANNOT settle whether the
 * player sees his own nose. T301 Leg E is the live gate, and it decides.
 */

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace EQT {
namespace Graphics {

// -------------------------------------------------------------------------
// T301 LEG A'S MEASUREMENTS, against the official Titanium client, two-client
// protocol (Testchar as actor, a second Human male as observer twin;
// feet-ladder fit residuals rms 0.19 px over 8 rungs). These are measured
// values, not the interim placeholders this leg first shipped, except where
// a comment says otherwise.
// -------------------------------------------------------------------------

// MEASURED: the eye sits at 0.925 +/- 0.004 of the model's own standing
// height. Leg A's advice is to key off a fraction rather than an absolute,
// because it does not depend on the frame "height" is measured in.
constexpr float kEyeHeightFractionOfStandingHeight = 0.925f;

// MEASURED: the crown sits 0.60 above the eye -- and 0.75 BEHIND it. The
// bbox-crown placement T300 shipped was therefore wrong in BOTH axes, not
// just in height.
//
// *** THE SKULL FRACTION IS KEYED TO THIS, NOT TO 0.925 H. *** The two
// disagree: 0.925 H puts a Human male's eye at model Z 3.17, crown - 0.60
// puts it at 3.07. The difference is the FRAME. fable stands the model
// CENTRED on the server z and the official client stands it FEET-ON-z -- a
// ~3-unit disagreement about the model's placement (T300; see
// getPlayerFirstPersonEyeOffset()) -- so "standing height" does not name the
// same span in the two clients. Leg A's own figures show the seam: the sit
// drop is 0.925 - 0.47 = 0.455 of H, reported as 3.6 units, which implies
// H = 7.9 where this mesh's own crown-to-feet span is 6.67.
//
// crown-minus-eye is measured INSIDE the head and is immune to that question,
// so it is what the fraction is solved against. 0.925 H is asserted as a
// per-race CROSS-CHECK instead, with the disagreement reported rather than
// averaged away (tests/test_first_person_eye_anatomy.cpp).
constexpr float kCrownAboveEyeUnits = 0.60f;

// Where the eye sits up the skull: a fraction from the underside of the skull
// (chin/jaw line) to the crown. Solved so a Human male's eye lands at
// crown - kCrownAboveEyeUnits. Every other race's value then follows from ITS
// OWN head cluster, so this stays ONE global constant and never a table.
constexpr float kEyeHeightFractionOfSkull = 0.574f;

// MEASURED: the eye sits +0.9 +/- 0.25 units FORWARD of the model's own
// vertical axis (about 0.11 H).
//
// *** THIS DISAGREES WITH T298's 0.00 +/- 0.4 AT ABOUT 1.9 SIGMA, and the
// rotation-parallax test that would settle it could not be run. *** Leg A's
// instruction was to treat the forward offset as a RANGE, 0 to 1.0, and pick
// the value that keeps the face out of frame.
//
// 1.0 IS THE TOP OF THAT RANGE AND IS CHOSEN DELIBERATELY, FOR THAT REASON.
// Moving the eye forward carries it toward and then through the face plane,
// so the further forward it sits the more of the player's own face falls
// BEHIND the camera, where it cannot be drawn at all. The failure being
// avoided -- review/T300/T300_C_12, a screenshot of the player's own face --
// is caused by an eye too far BACK, so the far end of the range is the safe
// end and the near end (0.0, T298's value) reintroduces the risk.
//
// Applied along the LOOK RAY, on top of the head cluster's own forward
// position, so it needs no knowledge of any mesh's baked axes -- the trap
// that broke T300 Leg B's per-race table. For a Human male the head centroid
// is 0.17 BEHIND the origin, so the total is +0.83 from the vertical axis:
// inside Leg A's range, within 0.07 of its central +0.9. Hunched races carry
// their own anatomy on top (Iksar +0.84, Froglok +1.48).
constexpr float kEyeForwardOffsetUnits = 1.0f;

// -------------------------------------------------------------------------
// THE DOWN-PITCH CLAMP -- MEASURED, AND IT IS NOT THE HIDING MECHANISM.
// -------------------------------------------------------------------------
//
// MEASURED: 94.9 +/- 0.5 degrees below horizontal -- about 5 degrees PAST THE
// NADIR -- and verified genuinely clamped (4 further seconds of input changed
// 0.08% of pixels). T300's 55-75 bracket is REFUTED, and so is its
// `90 - vFOV/2` derivation (that expression is by construction the angle at
// which the frustum's lower edge reaches the nadir, so it is the condition
// that ADMITS the body's cone of directions, not one that excludes it).
//
// *** A CLAMP THAT REACHES PAST VERTICAL CANNOT BE WHAT HIDES THE BODY. ***
// This leg's own headless measurement had already said so: no clamp value can
// empty the down-frustum band, because from any eye inside the head the body
// spans depression angles from near 0 (the hands) to near 90 (the feet). For
// a Human male, hiding the shoulders needs C > 49.1 and hiding the feet needs
// C < 48.4 -- the interval is EMPTY by about 0.7 degrees. Leg A's control
// settles it: an identical Human-male model teleported onto the actor's own
// coordinates, at the same clamp, fills 17.8% of the frame with shoulders,
// arms, hands, belt and hair. THE FRUSTUM CONTAINS THE BODY; THE CLIENT
// SIMPLY DOES NOT DRAW IT.
//
// The draw rule is a SEPARATE LEG's measurement and is deliberately NOT
// implemented here. The hiding seam stays exactly where T300 put it -- this
// constant and IrrlichtRenderer::applyFirstPersonPitchClamp() -- so swapping
// the mechanism stays two function bodies. But do not mistake this clamp for
// the mechanism any longer: it is a control-feel limit now.
//
// CONSEQUENCE FOR THE CAMERA, AND IT IS NOT OPTIONAL: 94.9 passes straight
// down, where a look-at camera's default (0,1,0) up vector degenerates and
// then flips through the nadir. The camera formula therefore computes an
// EXPLICIT up vector -- see FirstPersonCameraPlacement in
// first_person_camera.h. Without it this measured value cannot be used at all.
constexpr float kFirstPersonMaxDownPitchDegrees = 94.9f;

// -------------------------------------------------------------------------
// WHERE THE EYE'S POSE COMES FROM -- the unit's central design decision,
// settled by Leg A's Q3 MEASUREMENT rather than by argument.
//
// The actor pinned against a wall holding forward produced ONE distinct
// rendered frame in 12 seconds, with ZERO pixel change in his own view, while
// the observing client watched his crown oscillate between rows 372 and 389
// over about 10 gait cycles -- a 0.16-unit head bob. The official eye
// therefore does NOT follow the skeleton. But it IS posture-aware: sitting
// drops it 3.6 units, to 0.47 H.
//
// That is a THIRD option, and it is what fable now implements.
//
// AnimatedPose -- this leg's first choice, argued from "an eye inside the
// skull cannot be bobbed through" -- is REFUTED. Do not restore it on that
// argument: the official client accepts the theoretical bob-through and
// simply never draws the body, which makes the argument moot. RestPose is
// stable but ignores posture and so cannot reproduce the sit drop.
// -------------------------------------------------------------------------
enum class EyePoseSource : uint8_t {
    RestPose = 0,        // bind pose only; posture ignored
    PostureDriven = 1,   // bind pose + a per-posture offset  <-- MEASURED
    AnimatedPose = 2,    // rides the animated skeleton; REFUTED by Leg A Q3
};
constexpr EyePoseSource kFirstPersonEyeSource = EyePoseSource::PostureDriven;

// The DISCRETE posture states the eye is driven from. These mirror
// EntityVisual::PoseState / eqt::state::EntityPoseState, which arrive on
// SpawnAppearance and are deliberately never touched by movement updates --
// exactly the "discrete posture state, never the animated pose" Leg A's
// measurement calls for.
enum class EyePosture : uint8_t {
    Standing = 0,
    Sitting = 1,
    Crouching = 2,
    Lying = 3,
};

// MEASURED: sitting drops the eye from 0.925 H to 0.47 H.
constexpr float kSitEyeDropFractionOfHeight =
    kEyeHeightFractionOfStandingHeight - 0.47f;   // 0.455

// *** CROUCHING AND LYING ARE UNMEASURED. *** Leg A measured standing and
// sitting only. Rather than invent two numbers, both take the SIT drop: it is
// the only measured non-standing value, it is in the right direction, and an
// honest reuse is far easier to find later than a fabricated constant that
// reads as measured. Measuring these is an outstanding follow-up -- a prone
// eye in particular is certainly lower than a seated one.
inline float eyeDropFractionForPosture(EyePosture posture) {
    switch (posture) {
        case EyePosture::Standing:  return 0.0f;
        case EyePosture::Sitting:   return kSitEyeDropFractionOfHeight;
        case EyePosture::Crouching: return kSitEyeDropFractionOfHeight;  // UNMEASURED
        case EyePosture::Lying:     return kSitEyeDropFractionOfHeight;  // UNMEASURED
    }
    return 0.0f;
}

// -------------------------------------------------------------------------
// Head-bone name resolution.
// -------------------------------------------------------------------------

// True for the `*_point` attachment-point names (`humhead_point`,
// `frmhehead_point`, ...). They are helm/hair hangers, not the head, and they
// carry no geometry of their own.
bool isAttachmentPointBoneName(const std::string& boneName);

// The index of the head bone in `boneNames`, or -1. `modelCode` is the
// skeleton's own code ("HUM", "FRM", ...); matching is case-insensitive and
// falls back to an unprefixed "he"/"head" name if the prefix is absent.
int findHeadBoneIndex(const std::string& modelCode,
                      const std::vector<std::string>& boneNames);

// -------------------------------------------------------------------------
// Eye derivation from the head bone's vertex cluster.
// -------------------------------------------------------------------------

// Extents of the vertices skinned to the head bone, in that BONE'S OWN local
// (bind) space -- which is the space the raw mesh vertices are stored in, and
// therefore the space in which the bone's world matrix is the only thing
// needed to place a point. EQ axes: +Z up, +X forward, +Y left.
struct HeadVertexCluster {
    std::size_t count = 0;
    float minX = 0.0f, maxX = 0.0f;
    float minY = 0.0f, maxY = 0.0f;
    float minZ = 0.0f, maxZ = 0.0f;
    float centroidX = 0.0f, centroidY = 0.0f;
    bool valid() const { return count > 0 && maxZ > minZ; }
};

// One run of the EQ vertex-piece table: `count` consecutive vertices all
// skinned to `boneIndex`.
struct BoneVertexRun {
    int boneIndex = 0;
    unsigned count = 0;
};

// Accumulate the head bone's cluster over a positions array. Stride is in
// FLOATS and the position must be the first three floats of each element,
// which is true of both irr::video::S3DVertex (9 floats) and the loader's
// Vertex3D (8) -- so the runtime path and the tests share ONE implementation
// of this scan rather than each growing their own.
HeadVertexCluster accumulateHeadCluster(const std::vector<BoneVertexRun>& runs,
                                        int headBoneIndex,
                                        const float* positions,
                                        std::size_t vertexCount,
                                        std::size_t strideFloats);

// A point in the head bone's local space.
struct EyeLocalPoint {
    float x = 0.0f, y = 0.0f, z = 0.0f;
};

// The eye, in the head bone's local space. Horizontally the cluster's own
// centroid (so a snout carries the eye forward and a flat human face does
// not); vertically kEyeHeightFractionOfSkull up the SYMMETRISED skull span --
// see the file header for why the raw minimum is not used.
EyeLocalPoint computeEyeInHeadBoneSpace(const HeadVertexCluster& cluster);

// The lower bound of the skull used by computeEyeInHeadBoneSpace(), exposed
// so tests can assert the beard rejection directly rather than infer it.
float skullLowerBoundLocalZ(const HeadVertexCluster& cluster);

}  // namespace Graphics
}  // namespace EQT
