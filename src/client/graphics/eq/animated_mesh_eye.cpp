// T301 Leg B -- EQAnimatedMeshSceneNode::getFirstPersonEyeOffset().
//
// Split out of animated_mesh_scene_node.cpp deliberately: that TU is already
// large and this is substantial new code (refactor/check_file_sizes.sh).
//
// WHAT THIS COMPUTES: the offset, in Irrlicht WORLD axes, from this node's
// origin to the player's eye -- derived from the HEAD BONE's own vertex
// cluster, per race, from the mesh. See include/client/graphics/
// first_person_eye.h for the anchor survey and the beard trap.
//
// THE 90-DEGREE OFFSET IS LOAD-BEARING AND IS REPRODUCED HERE ON PURPOSE.
// The PLAYER node bakes its Y rotation into the vertices with a +90 degree
// offset (applyAnimation(), "the model's back faces the camera") and then
// renders with a POSITION-ONLY world transform. getBoneWorldPosition() below
// does NOT do that -- it applies the raw nodeRot.Y -- so it is off by 90
// degrees for the player node specifically, and could not be reused here.
// Players and NPCs genuinely do not share a default rotation; this is not
// drift to harmonize.

#include "client/graphics/eq/animated_mesh_scene_node.h"
#include "client/graphics/actor_scale.h"
#include "client/graphics/first_person_eye.h"

#include <cmath>

namespace EQT {
namespace Graphics {

bool EQAnimatedMeshSceneNode::getFirstPersonEyeOffset(
    irr::core::vector3df& outOffset, EyePosture posture) const {
    if (!eqMesh_) return false;

    auto skeleton = eqMesh_->getSkeleton();
    if (!skeleton || skeleton->bones.empty()) return false;

    const auto& vertexPieces = eqMesh_->getVertexPieces();
    const auto& originalVertices = eqMesh_->getOriginalVertices();
    if (vertexPieces.empty() || originalVertices.empty()) return false;

    // ---- resolve + cache the head anchor and the eye's bone-local point ----
    // Both are properties of the MESH, not of this instance's pose, so they
    // are computed once. -2 means "not yet attempted"; -1 means "attempted
    // and there is no head bone", which must not be retried every frame.
    if (eyeHeadBoneIndex_ == -2) {
        std::vector<std::string> names;
        names.reserve(skeleton->bones.size());
        for (const auto& bone : skeleton->bones) names.push_back(bone.name);
        eyeHeadBoneIndex_ = findHeadBoneIndex(skeleton->modelCode, names);

        if (eyeHeadBoneIndex_ >= 0) {
            // Raw vertices are stored in the OWNING BONE's local space (the
            // bone matrix is what lifts them to model space), EQ axes, +Z up.
            std::vector<BoneVertexRun> runs;
            runs.reserve(vertexPieces.size());
            for (const auto& piece : vertexPieces) {
                runs.push_back({piece.boneIndex, piece.count});
            }
            const HeadVertexCluster cluster = accumulateHeadCluster(
                runs, eyeHeadBoneIndex_,
                &originalVertices[0].Pos.X, originalVertices.size(),
                sizeof(irr::video::S3DVertex) / sizeof(float));
            if (cluster.valid()) {
                eyeHeadCluster_ = cluster;
            } else {
                // A head bone with no geometry of its own is not an anchor.
                eyeHeadBoneIndex_ = -1;
            }
        }

        if (eyeHeadBoneIndex_ >= 0 &&
            kFirstPersonEyeSource != EyePoseSource::AnimatedPose) {
            // A private animator that is never updated holds the BIND POSE
            // (setSkeleton() calls computeBoneTransforms() and nothing
            // advances it), so this is the head bone's rest transform and it
            // never changes. Both non-animated sources ride it.
            SkeletalAnimator rest;
            rest.setSkeleton(skeleton);
            const auto& restStates = rest.getBoneStates();
            if (eyeHeadBoneIndex_ < static_cast<int>(restStates.size())) {
                eyeRestHeadTransform_ = restStates[eyeHeadBoneIndex_].worldTransform;
            }
        }
    }

    if (eyeHeadBoneIndex_ < 0) return false;

    // ---- lift the bone-local eye into model space ----
    //
    // THE POSE SOURCE, and the whole of it: which head-bone matrix is used,
    // plus a posture offset. T301 Leg A MEASURED that the official eye does
    // not follow the skeleton (one distinct frame in 12 s of running against
    // a wall, zero pixel change, while an observing client watched the crown
    // bob 0.16 units) but IS posture-aware (sitting drops it 3.6 units).
    BoneMat4 headTransform;
    if (kFirstPersonEyeSource == EyePoseSource::AnimatedPose) {
        // REFUTED by Leg A Q3; kept compiled so the comparison is real and
        // reversible, not a claim about deleted code.
        const auto& boneStates = animator_.getBoneStates();
        if (eyeHeadBoneIndex_ >= static_cast<int>(boneStates.size())) return false;
        headTransform = boneStates[eyeHeadBoneIndex_].worldTransform;
    } else {
        headTransform = eyeRestHeadTransform_;
    }

    // T310 Leg A: derived HERE, not at load, because
    // computeEyeInHeadBoneSpace() reads the runtime eyeHeightFractionOfSkull
    // tunable and a /fpcam sweep must not require a model reload. The cluster
    // scan (the expensive part) is still done once.
    const EyeLocalPoint eyeLocalPoint = computeEyeInHeadBoneSpace(eyeHeadCluster_);
    float ex = eyeLocalPoint.x;
    float ey = eyeLocalPoint.y;
    float ez = eyeLocalPoint.z;
    headTransform.transformPoint(ex, ey, ez);

    // B1176 (#495): for the PLAYER the eye HEIGHT is the client's own
    // derivation, not the head cluster's. The client's first-person camera
    // sits 0x234 - 0x238 = 6.2 - 3.125 model units above the player's z
    // (actor_scale.h, kClientEyeModelHeight / kClientROffsetModelHeight;
    // 005701c0:284496-284510 via FUN_00489bf5). The node origin IS the
    // client's z -- ground + 3.125 x s, the ROffset placement
    // (entityNodeCollisionZOffset) -- so standing eye = ground + 6.2 x s,
    // exactly as in the client. The cluster still supplies the HORIZONTAL:
    // the client's unmounted first-person camera is on the player's own
    // axis (005701c0 sets cam.x/y from the player's x/y with no offset, the
    // ini FPCOffset defaulting to 0), and fable keeps its small measured
    // head-centroid offset; the B1176 live leg measures the effect.
    if (isPlayerNode_) {
        ez = kClientEyeModelHeight - kClientROffsetModelHeight;
    }

    // POSTURE. Applied in MODEL space, on the up axis, as a fraction of this
    // model's OWN standing height -- so it scales per race from the mesh
    // rather than from a table, and so a Troll's sit drop is a Troll's.
    //
    // This is the one place the whole-body bounding box is legitimately read:
    // its HEIGHT, which is a real property of the mesh. Its MaxEdge (the
    // crown) is what T300 mistook for an eye and is not read anywhere.
    // (B1176 note: the client's camera code has no posture branch -- its
    // seated eye comes from the seated z itself -- the fable's measured
    // drop is kept, and it now rides on the client's standing height.)
    if (kFirstPersonEyeSource == EyePoseSource::PostureDriven) {
        const float drop = eyeDropFractionForPosture(posture);
        if (drop != 0.0f) {
            const irr::core::aabbox3df bbox = eqMesh_->getBoundingBox();
            const float standingHeight = bbox.MaxEdge.Y - bbox.MinEdge.Y;
            if (standingHeight > 0.0f) {
                ez -= drop * standingHeight;   // EQ Z is up in model space
            }
        }
    }

    // ---- model space -> world axes, exactly as render() draws this node ----
    const irr::core::vector3df nodeRot = getRotation();
    const irr::core::vector3df nodeScale = getScale();

    if (isPlayerNode_) {
        // Rotation baked into the vertices with the +90 offset; the world
        // matrix carries position and the uniform actor scale (B1176), so
        // the offset -- model units here, the client's 3.075 standing eye
        // for the player (above) -- is multiplied by that scale, exactly
        // like the NPC branch does.
        const float scale = nodeScale.X;
        const float rad = (nodeRot.Y + 90.0f) * 3.14159265358979323846f / 180.0f;
        const float c = std::cos(rad);
        const float s = std::sin(rad);
        const float rx = ex * c - ey * s;
        const float ry = ex * s + ey * c;
        outOffset.X = rx * scale;
        outOffset.Y = ez * scale;   // EQ Z (up) -> Irrlicht Y
        outOffset.Z = ry * scale;   // EQ Y      -> Irrlicht Z
    } else {
        // NPC path: the node's own transform carries rotation and scale.
        const float lx = ex * nodeScale.X;
        const float ly = ez * nodeScale.Z;
        const float lz = ey * nodeScale.Y;
        const float rad = nodeRot.Y * 3.14159265358979323846f / 180.0f;
        const float c = std::cos(rad);
        const float s = std::sin(rad);
        outOffset.X = lx * c + lz * s;
        outOffset.Y = ly;
        outOffset.Z = -lx * s + lz * c;
    }
    return true;
}

}  // namespace Graphics
}  // namespace EQT
