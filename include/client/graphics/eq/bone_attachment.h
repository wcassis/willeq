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

// T472 (board B588) -- THE ONE TRANSFORM FOR AN ITEM HELD AT A POINT BONE
// (R_POINT, L_POINT, SHIELD_POINT), for every item and every slot.
//
// Returns the matrix that takes a vertex of an equipment mesh -- Irrlicht
// coordinates, as EquipmentModelLoader::buildMeshFromGeometry() writes them,
// with the item's mesh centre already added (equipment_actor_geometry.cpp) --
// into the entity's MODEL frame (Irrlicht, before the scene node's own
// transform). Rotation and translation; no scale.
//
// In EQ coordinates it is  B * C,  where B is the bone's world transform and
//   C = diag(-1, 1, -1)   -- a half turn about the ITEM's Y axis.
// The item's origin lands exactly on the bone: once the centre is added, an
// item's origin IS its grip (IT1, a sword: pommel at x -0.51, crossguard at
// +0.29), so there is no grip offset to guess.
//
// WHY C, measured from the data (T472 Review, tests/test_held_item_attachment.cpp):
//   - Human male idle ("o01"): R_POINT's X axis is (0.28, 0.03, 0.96) in EQ
//     space -- UP the forearm -- and its Y axis (0.07, 1.00, 0.01) points to
//     the character's left; L_POINT's are the same to within 0.15; and
//     SHIELD_POINT's Y axis points AWAY from the chest.
//   - An item's long axis is its +X (IT1's blade runs pommel -0.51 -> tip
//     2.24; IT48's lantern body, after its own rest DAG, the same way). With C
//     = identity -- which is what the pre-T472 (180, 0, 0) amounted to, since a
//     half turn about X leaves X alone -- the blade stands UP out of the fist,
//     exactly T466's frame. X must be negated.
//   - Every shield's painted face (IT200-IT203: qguard-front, kiteshield-m,
//     shield round-metal, shield round-wood; their backs are the *back.bmp
//     textures) faces the item's +Y, by winding AND by vertex normals. For the
//     painted face to point away from the body, Y must be KEPT.
//   A proper rotation negating X and keeping Y is exactly C.
//
// Converting to Irrlicht (x, z, y) on both sides gives S * B * C * S, laid out
// below column by column (Irrlicht matrix4 is column-major like BoneMat4:
// transformVect's X = M[0]*x + M[4]*y + M[8]*z + M[12]).
inline irr::core::matrix4 computeHeldItemTransform(const BoneMat4& bone) {
    // Columns of B * C in EQ space: C negates B's X and Z columns.
    const float cx[3] = {-bone.m[0], -bone.m[1], -bone.m[2]};
    const float cy[3] = {bone.m[4], bone.m[5], bone.m[6]};
    const float cz[3] = {-bone.m[8], -bone.m[9], -bone.m[10]};

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
