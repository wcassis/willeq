// S3DLoader -- bone transforms, vertex skinning and animated skeletons.
//
// T451: split out of the 1,336-line src/client/graphics/eq/s3d_loader.cpp.
// Bodies are contiguous line ranges of that file, moved verbatim.

#include "client/graphics/eq/s3d_loader.h"
#include "common/logging.h"
#include <algorithm>
#include <iostream>
#include <limits>
#include <cmath>
#include <cctype>
#include <filesystem>
#include <functional>
#include <map>
#include <set>

namespace EQT {
namespace Graphics {


// 4x4 matrix structure for bone transforms (column-major order)
struct Mat4 {
    float m[16];

    Mat4() {
        // Identity matrix
        for (int i = 0; i < 16; i++) m[i] = 0.0f;
        m[0] = m[5] = m[10] = m[15] = 1.0f;
    }

    // Matrix multiplication: result = this * other
    Mat4 operator*(const Mat4& other) const {
        Mat4 result;
        for (int i = 0; i < 4; i++) {
            for (int j = 0; j < 4; j++) {
                result.m[j * 4 + i] = 0.0f;
                for (int k = 0; k < 4; k++) {
                    result.m[j * 4 + i] += m[k * 4 + i] * other.m[j * 4 + k];
                }
            }
        }
        return result;
    }

    // Transform a point (x, y, z) by this matrix
    void transformPoint(float& x, float& y, float& z) const {
        float nx = m[0] * x + m[4] * y + m[8] * z + m[12];
        float ny = m[1] * x + m[5] * y + m[9] * z + m[13];
        float nz = m[2] * x + m[6] * y + m[10] * z + m[14];
        x = nx; y = ny; z = nz;
    }

    // Transform a normal (rotation only, no translation)
    void transformNormal(float& nx, float& ny, float& nz) const {
        float nnx = m[0] * nx + m[4] * ny + m[8] * nz;
        float nny = m[1] * nx + m[5] * ny + m[9] * nz;
        float nnz = m[2] * nx + m[6] * ny + m[10] * nz;
        nx = nnx; ny = nny; nz = nnz;
    }

    // Create translation matrix
    static Mat4 translate(float x, float y, float z) {
        Mat4 result;
        result.m[12] = x;
        result.m[13] = y;
        result.m[14] = z;
        return result;
    }

    // Create scale matrix
    static Mat4 scale(float s) {
        Mat4 result;
        result.m[0] = result.m[5] = result.m[10] = s;
        return result;
    }

    // Create rotation matrix from normalized quaternion
    static Mat4 fromQuaternion(float qx, float qy, float qz, float qw) {
        Mat4 result;

        float xx = qx * qx, yy = qy * qy, zz = qz * qz;
        float xy = qx * qy, xz = qx * qz, yz = qy * qz;
        float wx = qw * qx, wy = qw * qy, wz = qw * qz;

        result.m[0]  = 1.0f - 2.0f * (yy + zz);
        result.m[1]  = 2.0f * (xy + wz);
        result.m[2]  = 2.0f * (xz - wy);
        result.m[3]  = 0.0f;

        result.m[4]  = 2.0f * (xy - wz);
        result.m[5]  = 1.0f - 2.0f * (xx + zz);
        result.m[6]  = 2.0f * (yz + wx);
        result.m[7]  = 0.0f;

        result.m[8]  = 2.0f * (xz + wy);
        result.m[9]  = 2.0f * (yz - wx);
        result.m[10] = 1.0f - 2.0f * (xx + yy);
        result.m[11] = 0.0f;

        result.m[12] = 0.0f;
        result.m[13] = 0.0f;
        result.m[14] = 0.0f;
        result.m[15] = 1.0f;

        return result;
    }
};

void S3DLoader::applySkinning(std::shared_ptr<ZoneGeometry>& mesh,
                               const std::shared_ptr<SkeletonTrack>& skeleton) {
    if (!mesh || !skeleton || mesh->vertexPieces.empty()) {
        return;
    }

    // Use allBones from skeleton which preserves original file order
    // This is the order that vertex piece bone indices reference
    const auto& allBones = skeleton->allBones;
    const auto& parentIndices = skeleton->parentIndices;

    if (allBones.empty()) {
        LOG_TRACE(MOD_GRAPHICS, "applySkinning: No bones in skeleton");
        return;
    }

    LOG_TRACE(MOD_GRAPHICS, "applySkinning: Skeleton has {} bones total", allBones.size());

    // Build bone world matrices indexed by original bone order
    std::vector<Mat4> boneMatrices(allBones.size());

    // Compute world transforms for each bone using proper matrix multiplication
    // Process in order (0 to N-1) - parents are guaranteed to come before children
    for (size_t i = 0; i < allBones.size(); ++i) {
        const auto& bone = allBones[i];
        int parentIdx = (i < parentIndices.size()) ? parentIndices[i] : -1;

        // Get local transform components
        float qx = 0, qy = 0, qz = 0, qw = 1;  // Identity quaternion
        float tx = 0, ty = 0, tz = 0;          // No translation
        float scale = 1.0f;                     // Unit scale

        if (bone && bone->orientation) {
            qx = bone->orientation->quatX;
            qy = bone->orientation->quatY;
            qz = bone->orientation->quatZ;
            qw = bone->orientation->quatW;
            tx = bone->orientation->shiftX;
            ty = bone->orientation->shiftY;
            tz = bone->orientation->shiftZ;
            scale = bone->orientation->scale;
        }

        // Build local transform matrix: T * R * S (Translation * Rotation * Scale)
        Mat4 scaleMat = Mat4::scale(scale);
        Mat4 rotMat = Mat4::fromQuaternion(qx, qy, qz, qw);
        Mat4 transMat = Mat4::translate(tx, ty, tz);

        // Local matrix = T * R * S
        Mat4 localMat = transMat * rotMat * scaleMat;

        // World matrix = parent world matrix * local matrix
        if (parentIdx >= 0 && static_cast<size_t>(parentIdx) < boneMatrices.size()) {
            boneMatrices[i] = boneMatrices[parentIdx] * localMat;
        } else {
            boneMatrices[i] = localMat;
        }

        if (i < 10 || (i >= 40 && i <= 55)) {
            std::string boneName = bone ? bone->name : "(null)";
            LOG_TRACE(MOD_GRAPHICS, "  Bone[{}] '{}' parent={} trans=({},{},{}) quat=({},{},{},{}) scale={}",
                i, boneName, parentIdx, tx, ty, tz, qx, qy, qz, qw, scale);
        }
    }

    // Find max bone index from vertex pieces to validate
    int maxBoneIndex = 0;
    for (const auto& piece : mesh->vertexPieces) {
        maxBoneIndex = std::max(maxBoneIndex, static_cast<int>(piece.boneIndex));
    }

    LOG_TRACE(MOD_GRAPHICS, "applySkinning: {} vertex pieces, max bone index {}, {} bone matrices",
        mesh->vertexPieces.size(), maxBoneIndex, boneMatrices.size());

    // Apply bone transforms to vertices based on vertex pieces
    size_t vertexStart = 0;
    for (const auto& piece : mesh->vertexPieces) {
        int boneIdx = piece.boneIndex;
        if (boneIdx < 0 || boneIdx >= static_cast<int>(boneMatrices.size())) {
            vertexStart += piece.count;
            continue;
        }

        const Mat4& boneMat = boneMatrices[boneIdx];

        // Apply transform to each vertex in this piece
        for (int i = 0; i < piece.count; ++i) {
            size_t vIdx = vertexStart + i;
            if (vIdx >= mesh->vertices.size()) break;

            Vertex3D& v = mesh->vertices[vIdx];

            // Transform position by full bone matrix
            boneMat.transformPoint(v.x, v.y, v.z);

            // Transform normal (rotation only, no translation)
            boneMat.transformNormal(v.nx, v.ny, v.nz);

            // Renormalize normal
            float nlen = std::sqrt(v.nx*v.nx + v.ny*v.ny + v.nz*v.nz);
            if (nlen > 0.0001f) {
                v.nx /= nlen;
                v.ny /= nlen;
                v.nz /= nlen;
            }
        }

        vertexStart += piece.count;
    }

    // Recalculate bounding box
    mesh->minX = mesh->minY = mesh->minZ = std::numeric_limits<float>::max();
    mesh->maxX = mesh->maxY = mesh->maxZ = std::numeric_limits<float>::lowest();
    for (const auto& v : mesh->vertices) {
        mesh->minX = std::min(mesh->minX, v.x);
        mesh->minY = std::min(mesh->minY, v.y);
        mesh->minZ = std::min(mesh->minZ, v.z);
        mesh->maxX = std::max(mesh->maxX, v.x);
        mesh->maxY = std::max(mesh->maxY, v.y);
        mesh->maxZ = std::max(mesh->maxZ, v.z);
    }

    LOG_TRACE(MOD_GRAPHICS, "applySkinning: bounds after skinning X[{},{}] Y[{},{}] Z[{},{}]",
        mesh->minX, mesh->maxX, mesh->minY, mesh->maxY, mesh->minZ, mesh->maxZ);
}

std::shared_ptr<CharacterSkeleton> S3DLoader::buildAnimatedSkeleton(
    const std::string& modelCode,
    const std::shared_ptr<SkeletonTrack>& skeleton,
    WldLoader& wldLoader) {

    if (!skeleton || skeleton->allBones.empty()) {
        return nullptr;
    }

    auto animSkel = std::make_shared<CharacterSkeleton>();
    animSkel->modelCode = modelCode;

    // Build bone list from skeleton
    animSkel->bones.resize(skeleton->allBones.size());
    for (size_t i = 0; i < skeleton->allBones.size(); ++i) {
        const auto& srcBone = skeleton->allBones[i];
        AnimatedBone& dstBone = animSkel->bones[i];

        dstBone.name = srcBone ? srcBone->name : "";
        dstBone.parentIndex = (i < skeleton->parentIndices.size()) ? skeleton->parentIndices[i] : -1;

        // Copy the bone's orientation as the default pose track
        // This is critical - the pose data comes from the skeleton hierarchy (Fragment 0x10),
        // not from track refs. Without this, bones use identity transforms.
        if (srcBone && srcBone->orientation) {
            auto poseTrack = std::make_shared<TrackDef>();
            poseTrack->name = dstBone.name + "_pose";
            poseTrack->frames.push_back(*srcBone->orientation);  // Single-frame pose
            dstBone.poseTrack = poseTrack;
        }

        // Convert bone name to lowercase for matching
        std::string lowerName = dstBone.name;
        std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(),
                      [](unsigned char c) { return std::tolower(c); });

        // Remove common suffixes for cleaner matching
        size_t suffixPos = lowerName.find("_dag");
        if (suffixPos != std::string::npos) {
            lowerName = lowerName.substr(0, suffixPos);
        }
        dstBone.name = lowerName;

        // Fill in child indices
        for (size_t j = 0; j < skeleton->allBones.size(); ++j) {
            if (j < skeleton->parentIndices.size() && skeleton->parentIndices[j] == static_cast<int>(i)) {
                dstBone.childIndices.push_back(static_cast<int>(j));
            }
        }
    }

    // Get track references and definitions from WLD loader
    const auto& trackRefs = wldLoader.getTrackRefs();
    const auto& trackDefs = wldLoader.getTrackDefs();

    // Lower-case model code for matching
    std::string lowerModelCode = modelCode;
    std::transform(lowerModelCode.begin(), lowerModelCode.end(), lowerModelCode.begin(),
                  [](unsigned char c) { return std::tolower(c); });

    // Collect animations by matching model code
    std::map<std::string, std::shared_ptr<Animation>> animations;
    int poseTracksFound = 0;
    int animTracksFound = 0;

    for (const auto& [fragIdx, trackRef] : trackRefs) {
        if (!trackRef || !trackRef->isNameParsed) continue;

        // Check if this track belongs to our model
        if (trackRef->modelCode != lowerModelCode) continue;

        // Get the track definition (keyframe data)
        auto trackDef = wldLoader.getTrackDef(trackRef->trackDefRef);
        if (!trackDef) continue;

        // Find the bone this track applies to
        std::string boneName = trackRef->boneName;
        int boneIdx = animSkel->getBoneIndex(boneName);

        // Try alternate name formats if not found
        if (boneIdx < 0 && !boneName.empty() && boneName[0] == '_') {
            boneIdx = animSkel->getBoneIndex(boneName.substr(1));
        }
        if (boneIdx < 0) {
            // Try with lowercase conversion
            std::string lowerBone = boneName;
            std::transform(lowerBone.begin(), lowerBone.end(), lowerBone.begin(),
                          [](unsigned char c) { return std::tolower(c); });
            boneIdx = animSkel->getBoneIndex(lowerBone);
        }
        // Try with model prefix (e.g., "pe" -> "elepe" for model "ele")
        if (boneIdx < 0 && !boneName.empty()) {
            std::string prefixedName = lowerModelCode + boneName;
            boneIdx = animSkel->getBoneIndex(prefixedName);
        }
        // Root bone special case
        if (boneIdx < 0 && (boneName == "root" || boneName.empty())) {
            boneIdx = animSkel->getBoneIndex(lowerModelCode);  // e.g., "ele" for ELE model
        }

        if (boneIdx < 0) {
            // Bone not found - skip this track
            continue;
        }

        AnimatedBone& bone = animSkel->bones[boneIdx];

        if (trackRef->isPoseAnimation || trackRef->animCode == "pos") {
            // This is the default pose
            bone.poseTrack = trackDef;
            poseTracksFound++;
        } else {
            // This is an action animation
            bone.animationTracks[trackRef->animCode] = trackDef;
            animTracksFound++;

            // Create or update animation entry
            auto& anim = animations[trackRef->animCode];
            if (!anim) {
                anim = std::make_shared<Animation>();
                anim->name = trackRef->animCode;
                anim->modelCode = lowerModelCode;
                anim->frameCount = 0;
                anim->animationTimeMs = 0;

                // Determine if this animation should loop
                static const std::set<std::string> loopedAnims = {
                    "pos", "p01", "p03", "p06", "p07", "p08",  // Standing/idle poses
                    "l01", "l02",                               // Walk/run
                    "l05", "l06", "l07", "l09",                 // Falling, crouch walk, climbing, swim
                    "o01", "o02", "o03"                         // Idle animations
                };
                anim->isLooped = (loopedAnims.find(trackRef->animCode) != loopedAnims.end());
            }

            // Update frame count
            if (static_cast<int>(trackDef->frames.size()) > anim->frameCount) {
                anim->frameCount = static_cast<int>(trackDef->frames.size());
            }

            // Update animation duration
            int trackTimeMs = (trackRef->frameMs > 0)
                ? trackRef->frameMs * static_cast<int>(trackDef->frames.size())
                : 100 * static_cast<int>(trackDef->frames.size());  // Default 100ms per frame
            if (trackTimeMs > anim->animationTimeMs) {
                anim->animationTimeMs = trackTimeMs;
            }

            // Store track reference in animation
            anim->tracks[boneName] = trackRef;
        }
    }

    animSkel->animations = animations;

    if (poseTracksFound > 0 || animTracksFound > 0) {
        LOG_TRACE(MOD_GRAPHICS, "S3DLoader: Built animated skeleton for '{}' with {} bones, {} pose tracks, {} animation tracks, {} unique animations",
            modelCode, animSkel->bones.size(), poseTracksFound, animTracksFound, animations.size());

        // List animations found
        for (const auto& [animCode, anim] : animations) {
            LOG_TRACE(MOD_GRAPHICS, "  Animation '{}': {} frames, {}ms{}",
                animCode, anim->frameCount, anim->animationTimeMs, (anim->isLooped ? " (looped)" : ""));
        }
    }

    return animSkel;
}


} // namespace Graphics
} // namespace EQT
