/*
 * Shared skeleton-bone lookup + weapon/shield attachment math.
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
 * This header is the ONE shared implementation, extracted verbatim from
 * `entity_equipment.cpp` (the production/in-world copy) to guarantee
 * `EntityRenderer::updateEquipmentTransforms()` — the code path that
 * determines in-world weapon placement — is behaviour-identical before
 * and after convergence. `entity_equipment.cpp`, `tools/model_viewer.cpp`,
 * and the restored `CharacterModelView` all consume this header; none of
 * them may keep a local copy of `findBoneIndex`/`extractBoneRotation`/the
 * grip-offset formula.
 *
 * Fixing the held-item misalignment itself is explicitly out of scope for
 * T60 — this header preserves the existing (known-imperfect) heuristic.
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

// Extract a Euler rotation (degrees) from a bone's world transform, for
// orienting attached equipment. Swaps Y/Z axes for EQ->Irrlicht coordinate
// conversion; translation is intentionally discarded (callers compute
// position separately). Verbatim from entity_equipment.cpp.
inline irr::core::vector3df extractBoneRotation(const BoneMat4& m) {
    irr::core::matrix4 irrMat;
    irrMat[0] = m.m[0];   irrMat[1] = m.m[2];   irrMat[2] = m.m[1];   irrMat[3] = 0;
    irrMat[4] = m.m[8];   irrMat[5] = m.m[10];  irrMat[6] = m.m[9];   irrMat[7] = 0;
    irrMat[8] = m.m[4];   irrMat[9] = m.m[6];   irrMat[10] = m.m[5];  irrMat[11] = 0;
    irrMat[12] = 0;       irrMat[13] = 0;       irrMat[14] = 0;       irrMat[15] = 1;

    return irrMat.getRotationDegrees();
}

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

// Grip/attachment offset along a weapon or shield mesh's local X axis
// (blade/handle direction), derived from its bounding box. Two-handed
// weapons (length > 3 units) grip at ~1/4 from center; one-handed weapons
// grip near the hilt (80% toward the MinEdge end). Verbatim formula from
// entity_equipment.cpp's primary/secondary weapon branches.
inline float computeWeaponGripOffset(const irr::core::aabbox3df& bbox) {
    float length = bbox.MaxEdge.X - bbox.MinEdge.X;
    bool isTwoHanded = (length > 3.0f);
    return isTwoHanded ? length * 0.25f : -bbox.MinEdge.X * 0.8f;
}

// Shield grip offset. entity_equipment.cpp (production) uses a flat
// constant here, NOT a bbox-derived fraction — this is a documented
// divergence found while converging the three historical copies (T60):
// the deleted CharacterModelView and tools/model_viewer.cpp's *initial*
// placement both used `bbox width * 0.175`, but entity_equipment.cpp's
// per-frame update (the code path that actually determines in-world
// placement, since it runs every frame after the initial placement) uses
// a flat 0.3. The flat constant is kept here — behaviour-preservation for
// in-world entities is mandatory, so production's actual formula wins.
inline float computeShieldGripOffset() {
    return 0.3f;
}

// Base rotation offset (Euler degrees, applied on top of the bone's own
// world rotation via extractBoneRotation()) for an item attached at the
// secondary/off-hand slot (L_POINT or SHIELD_POINT).
//
// T65: before this function existed, `entity_equipment.cpp` (mirrored in
// the paperdoll and `tools/model_viewer.cpp`) applied (180, 0, 180) for
// shields but only (180, 0, 0) for a plain secondary weapon -- the extra
// Z rotation was believed shield-specific. That split was the root cause
// of a defect where an equipped, mesh-built, texture-loaded, bone-resolved
// plain secondary weapon rendered nothing: the missing Z rotation pointed
// the grip-offset axis (computeWeaponGripOffset()'s return value, applied
// along local +X) into/behind the torso instead of outward, hiding it
// behind body geometry rather than actually placing it off-model.
//
// Confirmed via a controlled A/B, not assumed: forcing the shield's
// (180, 0, 180) rotation onto a plain secondary weapon (IT5 dagger, Dark
// Elf female skeleton -- the exact item/race T63's live session used and
// found invisible) made the weapon visible, gripped correctly, in the
// same run where the original (180, 0, 0) rotation produced nothing. See
// refactor/T65_secondary_weapon_invisible.md's Review for the before/after
// capture pair. The (isShield ? 180 : 0) split is removed: both item
// kinds in this slot need the same base rotation, they were never a
// legitimate divergence. Primary weapons (R_POINT, `(180, 0, 0)`) are a
// separate bone/slot and are NOT changed by this function -- they were
// already rendering correctly and this fix must not move them.
inline irr::core::vector3df computeSecondaryItemBaseRotation() {
    return irr::core::vector3df(180.0f, 0.0f, 180.0f);
}

} // namespace Graphics
} // namespace EQT
