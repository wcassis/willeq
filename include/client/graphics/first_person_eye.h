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
 *     head bobs through frame at LEVEL pitch.
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
#include <string>
#include <vector>

namespace EQT {
namespace Graphics {

// -------------------------------------------------------------------------
// T301 LEG A LANDS AS THESE THREE NUMBERS. They are the whole interface
// between Leg A's measurement of the official client and this code: setting
// them is the change, and no formula has to move.
// -------------------------------------------------------------------------

// Where the eye sits up the skull, as a fraction from the underside of the
// skull (chin/jaw line) to the crown. 0.0 = chin, 1.0 = crown.
//
// *** INTERIM. T301 Leg A measures the official client's eye as a fraction
// of the model's own standing height and this is where that lands. *** 0.55
// is the human-anatomy midpoint (the eye line sits at about half of
// crown-to-chin) and it is NOT a measurement. Cross-check that it is not
// absurd: for a Human male it yields model Z 3.04, which is 0.63 below the
// crown the old code used and within 0.06 of the 3.10 T76's HEAD-bone
// preference produced by an unrelated route.
constexpr float kEyeHeightFractionOfSkull = 0.55f;

// Extra forward offset along the LOOK RAY, in EQ units, on top of the head
// cluster's own forward position. *** INTERIM: 0.0 until Leg A reports. ***
// T298 measured the official eye's forward offset from the player's XY as
// 0.00 +/- 0.4, and the head cluster already carries each race's real forward
// position (a Human male's head centroid is 0.17 BEHIND the origin; an Iksar's
// is 0.84 ahead, a Froglok's 1.48 -- those are hunched models, not errors).
// This constant is only for a residual Leg A finds on top of that.
constexpr float kEyeForwardOffsetUnits = 0.0f;

// THE DOWN-PITCH CLAMP. A SEPARATE, NAMED, MEASURED CONSTANT.
//
// *** DO NOT RE-DERIVE THIS FROM THE FOV. *** T300 shipped
// `90 - vFOV/2` and T301 REFUTED it: that expression is by construction the
// angle at which the frustum's LOWER EDGE REACHES THE NADIR, so it is the
// condition that ADMITS the cone of directions the body occupies, not one
// that excludes it. It agreed with Leg A's 55-75 bracket by coincidence and
// was adopted without checking which side of the inequality it fell on. Its
// claimed race-invariance is false as well: the angle from a crown-mounted
// eye to the nearest own geometry was ~27 deg for a Human male and ~61 deg
// for a Troll, because that angle is a property of mesh PROPORTIONS.
//
// *** INTERIM: 65.0, the centre of T300 Leg A's 55-75 bracket. T301 Leg A
// measures the clamp itself and this is where that number lands. ***
constexpr float kFirstPersonMaxDownPitchDegrees = 65.0f;

// -------------------------------------------------------------------------
// THE ANIMATION-FOLLOW SWITCH -- the unit's central design decision, made
// once, here, and reversible by one bool.
//
// TRUE  = the eye rides the head bone's CURRENT animated transform. The eye
//         is inside the skull in every frame of every animation BY
//         CONSTRUCTION, so the head can never bob through the camera: the
//         defect is closed structurally rather than tuned away.
// FALSE = the eye rides the head bone's REST transform. Rock steady, but the
//         run cycle then lifts the animated head past a fixed eye -- which is
//         exactly the T300_C_20/21 defect, merely moved 0.6 units down.
//
// NOTE WHAT IS *NOT* FOLLOWED: only the head bone's POSITION is taken. Its
// ORIENTATION is discarded, so the look direction stays entirely under the
// mouse and the run cycle's head yaw/roll never shakes the view. That is the
// distinction between "view bob" (which the official client has) and "the
// camera is parented to the skull" (which would be unplayable).
//
// T301 Leg A Q3 measures what the official client does. If it reports a
// rest-pose eye, flip this to false -- both branches are implemented and
// tested; nothing else moves.
// -------------------------------------------------------------------------
constexpr bool kFirstPersonEyeFollowsAnimation = true;

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
