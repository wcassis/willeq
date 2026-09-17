/*
 * Shared skeleton-bone lookup + held-item attachment math.
 *
 * T60: before this file, three copies of this logic existed —
 * `entity_equipment.cpp` (production, in-world 3rd-person rendering),
 * `tools/model_viewer.cpp` (the debug tool), and the deleted
 * `character_model_view.cpp`'s own copy. All three implemented the same
 * fragile bone-name fallback chain and weapon-offset heuristic
 * independently, which is why debugging the held-item misalignment in the
 * viewer did not reliably predict production behaviour (see
 * refactor/T60_paperdoll_restore.md).
 *
 * This header is the ONE shared implementation. `entity_equipment.cpp`,
 * `tools/model_viewer/model_viewer_equipment.cpp` and `CharacterModelView`
 * all consume it; none of them may keep a local copy of `findBoneIndex` or of
 * the held-item transform.
 *
 * T472 (board B588) replaced T60's "known-imperfect heuristic" -- a
 * per-slot Euler offset added to an Euler angle extracted from the bone, plus
 * a bounding-box grip offset -- with computeHeldItemTransform(), derived from
 * the S3D data. extractBoneRotation(), computeWeaponGripOffset(),
 * computeShieldGripOffset() and computeSecondaryItemBaseRotation() are gone.
 */
#pragma once

#include <irrlicht.h>
#include <algorithm>
#include <cctype>
#include <memory>
#include <string>
#include <vector>

#include "client/graphics/eq/skeletal_animator.h"

namespace EQT {
namespace Graphics {

// Search a skeleton for a bone matching raceCode + one of `suffixes`, over a
// fallback chain of naming-convention variants (case, `_DAG` suffix).
// Returns -1 if none match. Verbatim from entity_equipment.cpp — this is
// the fallback chain production in-world weapon placement is proven
// against, so it must not be widened or narrowed without re-proving
// in-world placement (see the plan's STOP condition).
inline int findBoneIndex(const std::shared_ptr<CharacterSkeleton>& skeleton,
                          const std::string& raceCode,
                          const std::vector<std::string>& suffixes) {
    if (!skeleton) return -1;

    for (const auto& suffix : suffixes) {
        // Try uppercase race code
        std::string upperName = raceCode + suffix;
        int idx = skeleton->getBoneIndex(upperName);
        if (idx >= 0) return idx;

        // Try lowercase race code
        std::string lowerRace = raceCode;
        std::transform(lowerRace.begin(), lowerRace.end(), lowerRace.begin(),
                      [](unsigned char c) { return std::tolower(c); });
        std::string lowerName = lowerRace + suffix;
        idx = skeleton->getBoneIndex(lowerName);
        if (idx >= 0) return idx;

        // Try without underscore (e.g., "qcmr_point" instead of "qcm_r_point")
        if (suffix.length() > 0 && suffix[0] != '_') {
            // Already no leading underscore
        } else if (suffix.length() > 1) {
            std::string noUnderscoreSuffix = suffix.substr(1);
            idx = skeleton->getBoneIndex(lowerRace + noUnderscoreSuffix);
            if (idx >= 0) return idx;
        }

        // Try with _DAG suffix removed if present
        if (suffix.length() > 4 && suffix.substr(suffix.length() - 4) == "_DAG") {
            std::string noDAG = suffix.substr(0, suffix.length() - 4);
            idx = skeleton->getBoneIndex(raceCode + noDAG);
            if (idx >= 0) return idx;
            idx = skeleton->getBoneIndex(lowerRace + noDAG);
            if (idx >= 0) return idx;
        }
    }
    return -1;
}

// T472 (board B588) / T485 (B724, B725) -- THE ONE TRANSFORM FOR AN ITEM HELD
// AT A POINT BONE (R_POINT, L_POINT, SHIELD_POINT), for every item and every
// slot, and the one placement routine that applies it to a scene node.
//
// computeHeldItemTransform() returns the matrix that takes a vertex of an
// equipment mesh -- Irrlicht coordinates, as
// EquipmentModelLoader::buildMeshFromGeometry() writes them, with the item's
// mesh centre already added (equipment_actor_geometry.cpp) -- into the
// entity's MODEL frame (Irrlicht, before the scene node's own transform).
// Rotation and translation; no scale.
//
// In EQ coordinates it is  B * C,  where B is the bone's world transform and C
// is a constant proper signed permutation of the item's axes (a
// HeldItemCorrection: column j is the image of the item's EQ axis j). The
// item's origin lands exactly on the bone: once the centre is added, an item's
// origin IS its grip (IT1, a sword: pommel at x -0.51, crossguard at +0.29),
// so there is no grip offset to guess.
//
// WHICH C -- chosen by SEARCH against the official client, not by argument
// (T485, refactor/T485_held_item_transform_candidate_search.md, the
// match-the-oracle rule in docs/decisions.md). T472 derived
// C = diag(-1, 1, -1) from one R_POINT axis at one o01 frame plus shield
// winding; T478's fresh oracle grabs (review/T478/sheets/T478_held_*) refuted
// it on every orientation observation. T485 rendered all 24 proper signed
// permutations through this function (tools/hwcaps/held_item_candidate_search)
// and scored each against the three oracle facts:
//   - a one-handed blade (IT10649, long axis +X) points FORWARD from the
//     fist, roughly level, at idle;
//   - a two-handed blade (IT10004, long axis +X) is level in front of the
//     waist, pointing forward, its crossguard (item Z) vertical;
//   - an arrow (IT10, long axis -Z) hangs DOWN along the leg, tip near the foot.
// Exactly one candidate satisfies all three: item +X -> bone -Z, item Z ->
// bone +X, item Y kept -- a quarter turn about the item's Y (candidate c20 of
// review/T485/). Measured on the Human male at o01 frame 0 (rpoint_frames.txt
// there): the FOREARM runs (0.96, 0.07, -0.27) from elbow to fingers -- the
// elbow is bent and the hand is at the belt -- and R_POINT's -Z is that same
// line (0.958, 0.06, -0.284), while its X (0.28, 0.03, 0.96) points UP,
// perpendicular to the forearm. T472's "X runs up the forearm" was wrong, and
// that is why its C hung the blade straight down. With c20 the blade
// continues the forearm forward and the arrow's long end (-Z_item -> -X_bone)
// hangs down. With X -> -Z and Z -> +X fixed, det +1 forces Y -> +Y, so c20 is
// the UNIQUE proper candidate -- and every shield's painted face, which faces
// item +Y by winding and normals (T472), still points away from the body as a
// consequence, not as an input (the oracle has not been seen holding a shield,
// B723).
//
// The sheets score ONE frame of the o01 loop, and that loop swings R_POINT by
// ~50 deg (its -Z pitches from 16 to 67 deg below level over 41 frames), so
// the choice was re-scored over EVERY frame of the loop as well
// (review/T485/data/score_over_loop.py): c20 is first with a mean
// blade-to-forward + arrow-to-down of 80.5 deg against 103.7 for the runner-up
// (identity), so the winner does not depend on which frame was captured. What
// the correction cannot fix is the PITCH: the oracle's blade reads level to
// slightly up and c20's is 16-67 deg below level depending on the frame, and
// the oracle frames' loop phase is not known (B756).
//
// Converting to Irrlicht (x, z, y) on both sides gives S * B * C * S, laid out
// below column by column (Irrlicht matrix4 is column-major like BoneMat4:
// transformVect's X = M[0]*x + M[4]*y + M[8]*z + M[12]).
struct HeldItemCorrection {
    // c[j] is the image of the item's EQ axis j (0 = X, 1 = Y, 2 = Z), as an
    // EQ-space vector. Proper (det +1) signed permutations only.
    float c[3][3];
};

// The one correction the client ships (T485's winner).
constexpr HeldItemCorrection kHeldItemCorrection = {{{0.0f, 0.0f, -1.0f},   // item X -> bone -Z
                                                     {0.0f, 1.0f, 0.0f},    // item Y -> bone +Y
                                                     {1.0f, 0.0f, 0.0f}}};  // item Z -> bone +X

inline irr::core::matrix4 computeHeldItemTransform(const BoneMat4& bone,
                                                   const HeldItemCorrection& corr = kHeldItemCorrection) {
    // Columns of B * C in EQ space: column j of B*C = B * corr.c[j].
    float col[3][3];
    for (int j = 0; j < 3; ++j) {
        for (int r = 0; r < 3; ++r) {
            col[j][r] = bone.m[0 + r] * corr.c[j][0] + bone.m[4 + r] * corr.c[j][1] + bone.m[8 + r] * corr.c[j][2];
        }
    }
    const float* cx = col[0];
    const float* cy = col[1];
    const float* cz = col[2];

    irr::core::matrix4 m;
    // Irrlicht input x = EQ x -> EQ column X, rows swapped y<->z.
    m[0] = cx[0];  m[1] = cx[2];  m[2] = cx[1];  m[3] = 0.0f;
    // Irrlicht input y = EQ z -> EQ column Z.
    m[4] = cz[0];  m[5] = cz[2];  m[6] = cz[1];  m[7] = 0.0f;
    // Irrlicht input z = EQ y -> EQ column Y.
    m[8] = cy[0];  m[9] = cy[2];  m[10] = cy[1]; m[11] = 0.0f;
    m[12] = bone.m[12];
    m[13] = bone.m[14];
    m[14] = bone.m[13];
    m[15] = 1.0f;
    return m;
}

// T485: THE ONE PLACEMENT. Sets `node`'s rotation and position (in its
// parent's local frame) so that it coincides with the geometry the parent
// node actually DRAWS for `bone`. The three consumers -- in-world
// (entity_equipment.cpp), the paperdoll (character_model_view.cpp) and the
// model viewer (model_viewer_equipment.cpp) -- used to each carry a copy of
// the setRotation/setPosition split; they all call this now so they cannot
// drift.
//
// playerYawFix / playerScale: the T301 Leg C compensation for the PLAYER's own
// node (see playerEquipmentYawCompensationDegrees() below), whose body is
// drawn baked and translation-only. Pass nullptr / 1.0 for every other node.
// `corr` is exposed for tools/hwcaps/held_item_candidate_search only; the
// client always ships kHeldItemCorrection.
inline void placeHeldItem(irr::scene::ISceneNode* node, const BoneMat4& bone,
                          const irr::core::matrix4* playerYawFix = nullptr, float playerScale = 1.0f,
                          const HeldItemCorrection& corr = kHeldItemCorrection) {
    if (!node) return;
    irr::core::matrix4 rotation = computeHeldItemTransform(bone, corr);
    irr::core::vector3df position = rotation.getTranslation();
    rotation.setTranslation(irr::core::vector3df(0.0f, 0.0f, 0.0f));
    if (!playerYawFix) {
        node->setRotation(rotation.getRotationDegrees());
        node->setPosition(position);
        return;
    }
    // Bone position un-scaled, because the baked body is drawn without the
    // node's scale.
    if (playerScale > 0.0001f) position /= playerScale;
    playerYawFix->rotateVect(position);
    // m3 = m1 * m2 applies m2 first, then m1 (Irrlicht matrix4::operator*).
    node->setRotation(((*playerYawFix) * rotation).getRotationDegrees());
    node->setPosition(position);
}

// T301 Leg C -- THE PLAYER'S OWN NODE DRAWS ITS BODY IN A DIFFERENT FRAME
// FROM THE ONE ITS EQUIPMENT CHILDREN INHERIT, AND THIS RECONCILES THEM.
//
// EQAnimatedMeshSceneNode::applyAnimation() BAKES a Y rotation of
// (nodeRot.Y + 90) degrees into the player's per-instance vertices (it is
// applied about the EQ Z axis, which is Irrlicht's Y), and
// EQAnimatedMeshSceneNode::render() then draws that node with a
// TRANSLATION-ONLY world transform -- the node's own rotation and scale are
// deliberately dropped. NPCs do neither: their vertices are unbaked and they
// draw through the full AbsoluteTransformation.
//
// Equipment, however, is an ordinary child scene node of the animated node
// for players and NPCs alike, so it inherits the node's R(nodeRot.Y) and its
// scale in both cases. For an NPC that matches the body exactly. For the
// PLAYER it does not: the body ends up at Irrlicht yaw -(nodeRot.Y + 90)
// while its weapon sits at +nodeRot.Y, an error of (-2*nodeRot.Y - 90)
// degrees that passes through zero only near one heading (45 deg) and is
// otherwise large -- 90 degrees at heading 0. In third person the weapon is
// still on screen (wrongly oriented, beside the player rather than in his
// hand, and rotating at twice his rate); in FIRST person, where the frustum
// is only 62.8 degrees wide and centred on the body's forward, the weapon
// falls outside the frustum at almost every heading and is simply never seen.
// That is T300 Leg C's 285-frame, seven-exchange, zero-weapon negative.
//
// The compensation below is applied to the player's equipment ONLY, in
// EntityRenderer::updateEquipmentTransforms(); the NPC path is untouched.
//
// Returns the extra Irrlicht Y rotation (degrees) that a player's equipment
// child must carry so that, after the parent's own R(nodeRot.Y) is applied,
// it lands in the same frame as the baked body.
inline float playerEquipmentYawCompensationDegrees(float nodeRotYDegrees) {
    return -2.0f * nodeRotYDegrees - 90.0f;
}

} // namespace Graphics
} // namespace EQT
