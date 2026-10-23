// EQAnimatedMeshSceneNode -- bone world queries and hand-bone lookup.
//
// T451: split out of the 1,287-line
// src/client/graphics/eq/animated_mesh_scene_node.cpp. Bodies are contiguous
// line ranges of that file, moved verbatim.

#include "client/graphics/eq/animated_mesh_scene_node.h"
#include "common/simd_detect.h"
#include "common/logging.h"
#include <iostream>
#include <cmath>
#include <algorithm>
#include <set>

#include "client/graphics/eq/animated_mesh_skinning.h"
#include "client/graphics/first_person_eye.h"

namespace EQT {
namespace Graphics {

size_t EQAnimatedMeshSceneNode::getInstanceMeshIndexCount() const {
    if (!instanceMesh_) return 0;
    size_t total = 0;
    for (irr::u32 b = 0; b < instanceMesh_->getMeshBufferCount(); ++b) {
        irr::scene::IMeshBuffer* mb = instanceMesh_->getMeshBuffer(b);
        if (mb) total += mb->getIndexCount();
    }
    return total;
}

bool EQAnimatedMeshSceneNode::getBoneWorldPosition(int boneIndex, irr::core::vector3df& outPosition) const {
    const auto& boneStates = animator_.getBoneStates();
    if (boneIndex < 0 || boneIndex >= static_cast<int>(boneStates.size())) {
        return false;
    }

    const BoneMat4& worldXform = boneStates[boneIndex].worldTransform;
    float eqX = worldXform.m[12];
    float eqY = worldXform.m[13];
    float eqZ = worldXform.m[14];

    // Apply node scale and convert EQ(x,y,z) to Irrlicht(x,z,y)
    irr::core::vector3df nodePos = getAbsolutePosition();
    irr::core::vector3df nodeScale = getScale();
    irr::core::vector3df nodeRot = getRotation();

    float localX = eqX * nodeScale.X;
    float localY = eqZ * nodeScale.Z;  // Height: EQ Z -> Irrlicht Y
    float localZ = eqY * nodeScale.Y;  // Depth:  EQ Y -> Irrlicht Z

    // Apply Y-axis rotation (model facing direction)
    float rotY = nodeRot.Y * 3.14159265f / 180.0f;
    float cosY = std::cos(rotY);
    float sinY = std::sin(rotY);
    float rotatedX = localX * cosY + localZ * sinY;
    float rotatedZ = -localX * sinY + localZ * cosY;

    outPosition.X = rotatedX + nodePos.X;
    outPosition.Y = localY + nodePos.Y;
    outPosition.Z = rotatedZ + nodePos.Z;

    return true;
}

bool EQAnimatedMeshSceneNode::getBoneWorldDirection(int boneIndex, irr::core::vector3df& outDirection) const {
    if (!eqMesh_) return false;
    auto skeleton = eqMesh_->getSkeleton();
    if (!skeleton) return false;

    const auto& boneStates = animator_.getBoneStates();
    if (boneIndex < 0 || boneIndex >= static_cast<int>(boneStates.size())) {
        return false;
    }

    // Get this bone's world position
    irr::core::vector3df bonePos;
    if (!getBoneWorldPosition(boneIndex, bonePos)) return false;

    // Walk up the bone hierarchy to find an ancestor with meaningful spatial separation.
    // Attachment points (R_POINT, L_POINT) are very close to their parent (hand bone),
    // so parent→child gives a tiny noisy vector. Walk up to forearm/upper arm level
    // where we get actual arm direction.
    static const float MIN_SEPARATION_SQ = 0.5f * 0.5f;  // Need at least 0.5 units of separation
    int ancestorIdx = skeleton->bones[boneIndex].parentIndex;
    int maxWalk = 4;  // Don't walk more than 4 levels
    while (ancestorIdx >= 0 && ancestorIdx < static_cast<int>(boneStates.size()) && maxWalk > 0) {
        irr::core::vector3df ancestorPos;
        if (getBoneWorldPosition(ancestorIdx, ancestorPos)) {
            irr::core::vector3df diff = bonePos - ancestorPos;
            float lenSq = diff.X * diff.X + diff.Y * diff.Y + diff.Z * diff.Z;
            if (lenSq >= MIN_SEPARATION_SQ) {
                float len = std::sqrt(lenSq);
                outDirection = diff / len;
                return true;
            }
        }
        ancestorIdx = skeleton->bones[ancestorIdx].parentIndex;
        --maxWalk;
    }

    // Fallback: direction from character center to bone (outward from body)
    irr::core::vector3df nodePos = getAbsolutePosition();
    outDirection = bonePos - nodePos;
    float len = outDirection.getLength();
    if (len > 0.0001f) {
        outDirection /= len;
    }
    return true;
}

int EQAnimatedMeshSceneNode::getBoneIndexByName(const std::string& boneName) const {
    if (!eqMesh_) return -1;
    auto skeleton = eqMesh_->getSkeleton();
    if (!skeleton) return -1;
    return skeleton->getBoneIndex(boneName);
}

int EQAnimatedMeshSceneNode::findRightHandBoneIndex() const {
    if (!eqMesh_) return -1;
    auto skeleton = eqMesh_->getSkeleton();
    if (!skeleton) return -1;

    // Get race code from skeleton's modelCode
    std::string raceCode = skeleton->modelCode;

    // Common right hand bone name patterns
    std::vector<std::string> suffixes = {
        "r_point", "R_POINT", "R_POINT_DAG", "BO_R_DAG", "TO_R_DAG",
        "_r_point", "_R_POINT"
    };

    // Try with race code prefix
    for (const auto& suffix : suffixes) {
        int idx = skeleton->getBoneIndex(raceCode + suffix);
        if (idx >= 0) return idx;

        // Try lowercase race code
        std::string lowerRace = raceCode;
        std::transform(lowerRace.begin(), lowerRace.end(), lowerRace.begin(),
                       [](unsigned char c) { return std::tolower(c); });
        idx = skeleton->getBoneIndex(lowerRace + suffix);
        if (idx >= 0) return idx;
    }

    // Try without race code prefix
    for (const auto& suffix : suffixes) {
        int idx = skeleton->getBoneIndex(suffix);
        if (idx >= 0) return idx;
    }

    return -1;
}

int EQAnimatedMeshSceneNode::findHeadBoneIndexForFx() const {
    if (!eqMesh_) return -1;
    auto skeleton = eqMesh_->getSkeleton();
    if (!skeleton) return -1;
    std::vector<std::string> names;
    names.reserve(skeleton->bones.size());
    for (const auto& bone : skeleton->bones) names.push_back(bone.name);
    return findHeadBoneIndex(skeleton->modelCode, names);
}

int EQAnimatedMeshSceneNode::findLeftHandBoneIndex() const {
    if (!eqMesh_) return -1;
    auto skeleton = eqMesh_->getSkeleton();
    if (!skeleton) return -1;

    // Get race code from skeleton's modelCode
    std::string raceCode = skeleton->modelCode;

    // Common left hand bone name patterns
    std::vector<std::string> suffixes = {
        "l_point", "L_POINT", "L_POINT_DAG", "BO_L_DAG", "TO_L_DAG",
        "shield_point", "SHIELD_POINT", "_l_point", "_L_POINT"
    };

    // Try with race code prefix
    for (const auto& suffix : suffixes) {
        int idx = skeleton->getBoneIndex(raceCode + suffix);
        if (idx >= 0) return idx;

        // Try lowercase race code
        std::string lowerRace = raceCode;
        std::transform(lowerRace.begin(), lowerRace.end(), lowerRace.begin(),
                       [](unsigned char c) { return std::tolower(c); });
        idx = skeleton->getBoneIndex(lowerRace + suffix);
        if (idx >= 0) return idx;
    }

    // Try without race code prefix
    for (const auto& suffix : suffixes) {
        int idx = skeleton->getBoneIndex(suffix);
        if (idx >= 0) return idx;
    }

    return -1;
}

// T300: setFirstPersonMode(), applyFirstPersonIndexFilter() and
// buildArmBoneIndices() were DELETED here. T74 added them so first person
// could draw arm-bone triangles only (and hide the player entirely when a
// skeleton yielded no arm bones); T296 built the viewmodel pass on top of
// them. T299 measured the actual mechanism in two client versions: the
// official client draws the ORDINARY, unfiltered player model from an
// eye-point camera and hides it with a CLAMPED DOWN-PITCH (T300 Leg A: the
// client clips nothing) -- geometry reappears whenever an animation rotates
// it up into frame. There is
// therefore no geometry to select and no fallback to fall back to. Do not
// reintroduce a first-person triangle filter.
//
// T309: the rest of that sentence used to read "if the body shows, the
// down-pitch clamp is wrong". It is not the clamp. The body shows exactly
// while an ATTACK animation plays (first_person_body_draw.h), enforced by
// EntityRenderer::updateFirstPersonBodyDrawRule() withholding this node from
// the render list -- see setWithheldFromRenderList().


} // namespace Graphics
} // namespace EQT
