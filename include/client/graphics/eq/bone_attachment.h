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

// B1065: held items are drawn with BACK-FACE CULLING ON. A shield is two
// skins a few hundredths apart -- IT200's painted face (qguard-front.bmp,
// winding normal +Y_item) and its back (qguard-back.bmp, winding -Y_item) --
// and the BACK skin is the one on the +Y side (centre y 0.042 vs 0.015). With
// culling off, depth alone decides and a shield whose face points away from
// the body shows its BACK from outside: the dark shape beside the guards in
// review/B1065/. The official client culls: from the guard's left it shows the
// painted face (review/B1065/oracle/side_b_guard_crop_x2.png). Measured with
// both settings on x86 (refactor/B1065/README.md).
constexpr bool kHeldItemBackfaceCulling = true;

// B1065 (#385) -- WHICH BONES THE TWO HELD ITEMS HANG FROM. Primary: R_POINT.
// Secondary: SHIELD_POINT for a shield (IT200-IT299; on every Luclin-era
// humanoid skeleton a child of the left FOREARM, e.g. QCMFO_L_DAG ->
// QCMSHIELD_POINT_DAG, docs/reference/s3d_contents_reference.md), L_POINT for
// anything else (or when the skeleton has no SHIELD_POINT).
//
// The bone names carry the SKELETON's model code, which is not always the race
// row's code: a race drawn with its fallback model (race 71 Qeynos Citizen
// "QCM" -> "HUM" where no QCM model is loaded, config/race_models.json) has
// "humr_point", never "QCMR_POINT". Before B1065 the lookup used only the race
// code, so such an entity's items were never placed. The skeleton's own
// modelCode is tried first, the race code second.
struct HeldItemBones {
    int primary = -1;
    int secondary = -1;
};

inline HeldItemBones resolveHeldItemBones(const std::shared_ptr<CharacterSkeleton>& skeleton,
                                          const std::string& raceCode, bool secondaryIsShield) {
    HeldItemBones out;
    if (!skeleton) return out;
    std::vector<std::string> codes;
    std::string own = skeleton->modelCode;
    std::transform(own.begin(), own.end(), own.begin(), [](unsigned char c) { return std::toupper(c); });
    if (!own.empty()) codes.push_back(own);
    if (!raceCode.empty() && raceCode != own) codes.push_back(raceCode);
    static const std::vector<std::string> kRight = {"R_POINT_DAG", "R_POINT", "r_point_dag", "r_point"};
    static const std::vector<std::string> kShield = {"SHIELD_POINT_DAG", "SHIELD_POINT", "shield_point_dag",
                                                     "shield_point"};
    static const std::vector<std::string> kLeft = {"L_POINT_DAG", "L_POINT", "l_point_dag", "l_point"};
    for (const auto& code : codes) {
        if (out.primary < 0) out.primary = findBoneIndex(skeleton, code, kRight);
        if (out.secondary < 0 && secondaryIsShield) out.secondary = findBoneIndex(skeleton, code, kShield);
    }
    for (const auto& code : codes) {
        if (out.secondary < 0) out.secondary = findBoneIndex(skeleton, code, kLeft);
    }
    return out;
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
// match-the-oracle rule in docs/decisions.md), and settled by WILL'S RULING on
// the sheets (2026-09-17), which is the oracle fact this rests on:
//
//   "ALL held items are wrongly oriented in both. In the after (c20) sheet the
//    item's long axis FOLLOWS the forearm; it must be PERPENDICULAR to the
//    forearm, a fist grip, so c20 is off by 90 degrees. In the before (T472)
//    sheet they are off by 180 degrees."
//
// That is a MEASURABLE criterion, and unlike the two readings it replaces it
// does not depend on which frame anything was captured at. MEASURED (the
// instrument writes review/T485/data/rpoint_frames.txt; scored by
// score_perpendicular.py there): R_POINT's -Z IS the forearm direction
// (humfo_r -> humfi_r), dot 1.001 at all 41 frames of o01. So the forearm and
// the item's long axis are both fixed in the BONE frame and the angle between
// them is the same at every frame. Per candidate, averaged over the loop:
//
//   correction                     blade (+X_item)   arrow (-Z_item)
//   c00 identity   X+X Y+Y Z+Z         89.1 deg          1.1 deg
//   c20 (T485 shipped, WRONG)            1.1 deg        90.9 deg
//   c02 (T472, WRONG)                   90.9 deg       178.9 deg
//
// c20 lays the BLADE along the forearm -- exactly what Will saw. The sign then
// comes from the official idle, which hangs the arm at the side
// (review/T478/oracle/*_idle_*): the arrow runs from the hand at the hip DOWN
// past the knee, 13 deg off vertical, so item -Z lands on bone -Z (Z -> +Z);
// the blades run FORWARD out of the fist, and forward-and-up at the hanging
// frame is bone +X (bone Y is character-left, the sideways one), so X -> +X.
// det +1 then forces Y -> +Y. That is the IDENTITY, and it is also the only
// correction consistent with Will's two numbers on their own: rotations about
// the item's Y are signed permutations only at 0/90/180/270, namely
// c00 / c20 / c02 / c22, so "c20 is 90 deg out" and "c02 is 180 deg out" meet
// at c00 alone.
//
// Consequences worth knowing: the item's +Y (every shield's painted face, by
// winding and vertex normals -- T472) is kept, so a shield still faces away
// from the body; and a 2H sword's crossguard (item Z) lands on bone +Z, which
// is vertical at the hanging frame (itemZ_up 0.917), as the official set C
// frames show.
//
// STILL NOT MEASURED, and the reason this could move again: fable's o01 and the
// official client's idle are not the same pose. fable's closest arm-hanging
// frame is 25, forearm 24.5 deg off straight down; the official arm reads
// nearer vertical. Only an official-client bone dump or a stepped video would
// close that, and neither exists (board B756).
//
// Converting to Irrlicht (x, z, y) on both sides gives S * B * C * S, laid out
// below column by column (Irrlicht matrix4 is column-major like BoneMat4:
// transformVect's X = M[0]*x + M[4]*y + M[8]*z + M[12]).
struct HeldItemCorrection {
    // c[j] is the image of the item's EQ axis j (0 = X, 1 = Y, 2 = Z), as an
    // EQ-space vector. Proper (det +1) signed permutations only.
    float c[3][3];
};

// The one correction the client ships: the IDENTITY (T485, Will's ruling).
// An item's axes go straight to the point bone's -- no correction at all.
constexpr HeldItemCorrection kHeldItemCorrection = {{{1.0f, 0.0f, 0.0f},   // item X (the long axis of a blade) -> bone +X
                                                     {0.0f, 1.0f, 0.0f},   // item Y (a shield's painted face)  -> bone +Y
                                                     {0.0f, 0.0f, 1.0f}}};  // item Z (a crossguard)             -> bone +Z

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
