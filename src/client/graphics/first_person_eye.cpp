// T301 Leg B -- head-anchor resolution and eye derivation. See
// include/client/graphics/first_person_eye.h for the evidence behind every
// rule in this file; it is all measured off the shipped client tree.

#include "client/graphics/first_person_eye.h"

#include <algorithm>
#include <cctype>

namespace EQT {
namespace Graphics {

namespace {

std::string toLower(const std::string& s) {
    std::string out(s);
    for (char& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

}  // namespace

bool isAttachmentPointBoneName(const std::string& boneName) {
    static const std::string kSuffix = "_point";
    const std::string lower = toLower(boneName);
    return lower.size() > kSuffix.size() &&
           lower.compare(lower.size() - kSuffix.size(), kSuffix.size(), kSuffix) == 0;
}

int findHeadBoneIndex(const std::string& modelCode,
                      const std::vector<std::string>& boneNames) {
    const std::string code = toLower(modelCode);

    int best = -1;
    std::size_t bestRemainder = std::string::npos;

    for (std::size_t i = 0; i < boneNames.size(); ++i) {
        // `humhead_point` / `frmhehead_point` also begin with `<code>he`.
        // They hang helms; they are not the head.
        if (isAttachmentPointBoneName(boneNames[i])) continue;

        std::string remainder = toLower(boneNames[i]);
        if (!code.empty() && remainder.size() >= code.size() &&
            remainder.compare(0, code.size(), code) == 0) {
            remainder = remainder.substr(code.size());
        }
        if (remainder.compare(0, 2, "he") != 0) continue;

        // `bamhe` beats `bamhe2`; `frmhehead` is taken because nothing
        // shorter survives.
        if (bestRemainder == std::string::npos || remainder.size() < bestRemainder) {
            bestRemainder = remainder.size();
            best = static_cast<int>(i);
        }
    }
    return best;
}

HeadVertexCluster accumulateHeadCluster(const std::vector<BoneVertexRun>& runs,
                                        int headBoneIndex,
                                        const float* positions,
                                        std::size_t vertexCount,
                                        std::size_t strideFloats) {
    HeadVertexCluster cluster;
    if (!positions || strideFloats < 3 || headBoneIndex < 0) return cluster;

    double sumX = 0.0, sumY = 0.0;
    std::size_t index = 0;
    for (const auto& run : runs) {
        for (unsigned i = 0; i < run.count && index < vertexCount; ++i, ++index) {
            if (run.boneIndex != headBoneIndex) continue;
            const float* p = positions + index * strideFloats;
            if (cluster.count == 0) {
                cluster.minX = cluster.maxX = p[0];
                cluster.minY = cluster.maxY = p[1];
                cluster.minZ = cluster.maxZ = p[2];
            } else {
                cluster.minX = std::min(cluster.minX, p[0]);
                cluster.maxX = std::max(cluster.maxX, p[0]);
                cluster.minY = std::min(cluster.minY, p[1]);
                cluster.maxY = std::max(cluster.maxY, p[1]);
                cluster.minZ = std::min(cluster.minZ, p[2]);
                cluster.maxZ = std::max(cluster.maxZ, p[2]);
            }
            sumX += p[0];
            sumY += p[1];
            ++cluster.count;
        }
    }
    if (cluster.count > 0) {
        cluster.centroidX = static_cast<float>(sumX / cluster.count);
        cluster.centroidY = static_cast<float>(sumY / cluster.count);
    }
    return cluster;
}

float skullLowerBoundLocalZ(const HeadVertexCluster& cluster) {
    // The head JOINT is local Z 0 by construction and a skull reaches about
    // as far below it as above it. Anything further down is beard or hair
    // skinned to the same bone (Dwarf male: crown +0.85, beard -2.29).
    return std::max(cluster.minZ, -cluster.maxZ);
}

EyeLocalPoint computeEyeInHeadBoneSpace(const HeadVertexCluster& cluster) {
    EyeLocalPoint eye;
    if (!cluster.valid()) return eye;

    const float low = skullLowerBoundLocalZ(cluster);
    const float high = cluster.maxZ;
    eye.x = cluster.centroidX;
    eye.y = cluster.centroidY;
    eye.z = low + kEyeHeightFractionOfSkull * (high - low);
    return eye;
}

}  // namespace Graphics
}  // namespace EQT
