#include "client/graphics/eq/wld_loader.h"
#include "client/graphics/eq/pfs.h"
#include "common/logging.h"
#include <cstring>
#include <cmath>
#include <algorithm>
#include <set>
#include <vector>
#include <iostream>
#include <iomanip>
#include "client/graphics/eq/wld_internal.h"

namespace EQT {
namespace Graphics {

void WldLoader::parseFragment10(const char* fragBuffer, uint32_t fragLength, uint32_t fragIndex,
                                int32_t nameRef, const char* hash, bool oldFormat,
                                const std::vector<std::pair<size_t, WldFragmentHeader>>& fragments,
                                const std::vector<char>& buffer) {
    WldFragment10Header header = read_val<WldFragment10Header>(fragBuffer);
    const char* ptr = fragBuffer + sizeof(WldFragment10Header);

    auto track = std::make_shared<SkeletonTrack>();

    if (nameRef != 0) {
        int32_t nameOffset = -nameRef;
        // The hash can be much larger than 65536 for big archives like global_chr.s3d
        if (nameOffset > 0) {
            track->name = std::string(&hash[nameOffset]);
        }
    }

    LOG_TRACE(MOD_GRAPHICS, "Frag10[{}]: SkeletonHierarchy '{}' flags=0x{:x} trackRefCount={} polygonAnimFrag={}",
              fragIndex, track->name, header.flags, header.trackRefCount, header.polygonAnimFrag);

    if (header.flags & 1) ptr += sizeof(int32_t) * 3;
    if (header.flags & 2) ptr += sizeof(float);

    std::vector<std::shared_ptr<SkeletonBone>> allBones;
    allBones.reserve(header.trackRefCount);

    // Collect tree relationships: (parent_bone_index, child_bone_index)
    std::vector<std::pair<int, int>> treeRelations;

    for (uint32_t i = 0; i < header.trackRefCount; ++i) {
        if (ptr + sizeof(WldFragment10BoneEntry) > fragBuffer + fragLength) {
            LOG_WARN(MOD_GRAPHICS, "Frag10[{}] '{}': bone entry {} overflows fragment", fragIndex, track->name, i);
            break;
        }

        WldFragment10BoneEntry entry = read_val<WldFragment10BoneEntry>(ptr);
        ptr += sizeof(WldFragment10BoneEntry);

        auto bone = std::make_shared<SkeletonBone>();

        if (entry.nameRef < 0) {
            int32_t nameOffset = -entry.nameRef;
            if (nameOffset > 0 ) {
                bone->name = std::string(&hash[nameOffset]);
            }
        }

        if (entry.orientationRef > 0) {
            auto it13 = boneOrientationRefs_.find(entry.orientationRef);
            if (it13 != boneOrientationRefs_.end()) {
                auto it12 = boneOrientations_.find(it13->second);
                if (it12 != boneOrientations_.end()) {
                    bone->orientation = it12->second;
                } else {
                    LOG_TRACE(MOD_GRAPHICS, "  Frag10[{}] bone[{}] '{}': orientationRef={} -> trackDefRef={} NOT FOUND in boneOrientations_",
                              fragIndex, i, bone->name, entry.orientationRef, it13->second);
                }
            } else {
                LOG_TRACE(MOD_GRAPHICS, "  Frag10[{}] bone[{}] '{}': orientationRef={} NOT FOUND in boneOrientationRefs_",
                          fragIndex, i, bone->name, entry.orientationRef);
            }
        }

        if (entry.modelRef > 0) {
            bone->modelRef = entry.modelRef;
            LOG_TRACE(MOD_GRAPHICS, "  Frag10[{}] bone[{}] '{}': modelRef={} flags=0x{:x} childCount={}",
                      fragIndex, i, bone->name, entry.modelRef, entry.flags, entry.childCount);
        } else {
            LOG_TRACE(MOD_GRAPHICS, "  Frag10[{}] bone[{}] '{}': no modelRef flags=0x{:x} childCount={}",
                      fragIndex, i, bone->name, entry.flags, entry.childCount);
        }

        allBones.push_back(bone);

        // Read child indices - store for later processing
        for (uint32_t j = 0; j < entry.childCount; ++j) {
            if (ptr + sizeof(int32_t) > fragBuffer + fragLength) break;
            int32_t childIdx = read_val<int32_t>(ptr);
            ptr += sizeof(int32_t);
            // Store relationship: current bone (i) has child at childIdx
            treeRelations.push_back(std::make_pair(static_cast<int>(i), childIdx));
        }
    }

    // Build parent index array (initialized to -1 for roots)
    track->parentIndices.resize(allBones.size(), -1);

    // Now establish parent-child relationships after all bones are created
    for (const auto& relation : treeRelations) {
        int parentIdx = relation.first;
        int childIdx = relation.second;
        if (parentIdx >= 0 && static_cast<size_t>(parentIdx) < allBones.size() &&
            childIdx >= 0 && static_cast<size_t>(childIdx) < allBones.size()) {
            allBones[parentIdx]->children.push_back(allBones[childIdx]);
            track->parentIndices[childIdx] = parentIdx;
        }
    }

    // Store all bones in original file order
    track->allBones = allBones;

    // Find root bones (bones that are not children of any other bone)
    std::set<std::shared_ptr<SkeletonBone>> childBones;
    for (const auto& bone : allBones) {
        for (const auto& child : bone->children) {
            childBones.insert(child);
        }
    }

    for (const auto& bone : allBones) {
        if (childBones.find(bone) == childBones.end()) {
            track->bones.push_back(bone);
        }
    }

    skeletonTracks_[fragIndex] = track;

    // Count bones with modelRefs
    int bonesWithModelRef = 0;
    for (const auto& bone : allBones) {
        if (bone->modelRef > 0) bonesWithModelRef++;
    }
    LOG_TRACE(MOD_GRAPHICS, "Frag10[{}] '{}': {} total bones, {} roots, {} with modelRef",
              fragIndex, track->name, allBones.size(), track->bones.size(), bonesWithModelRef);
}

void WldLoader::parseFragment11(const char* fragBuffer, uint32_t fragLength, uint32_t fragIndex) {
    WldFragment11Header header = read_val<WldFragment11Header>(fragBuffer);
    if (header.ref > 0) {
        skeletonRefs_[fragIndex] = header.ref;
        LOG_TRACE(MOD_GRAPHICS, "Frag11[{}]: SkeletonRef -> {}", fragIndex, header.ref);
    }
}

void WldLoader::parseFragment12(const char* fragBuffer, uint32_t fragLength, uint32_t fragIndex,
                                int32_t nameRef, const char* hash) {
    // Fragment 0x12 - TrackDef - Contains animation keyframe data
    // Structure:
    //   int32_t nameRef (already in header)
    //   int32_t flags
    //   int32_t frameCount
    //   For each frame:
    //     int16_t rotDenom (quaternion W)
    //     int16_t rotX, rotY, rotZ (quaternion X, Y, Z)
    //     int16_t shiftX, shiftY, shiftZ (translation)
    //     int16_t shiftDenom (scale)

    const char* ptr = fragBuffer;

    // Read flags
    uint32_t flags = read_val<uint32_t>(ptr);
    ptr += sizeof(uint32_t);

    // Read frame count
    int32_t frameCount = read_val<int32_t>(ptr);
    ptr += sizeof(int32_t);

    auto trackDef = std::make_shared<TrackDef>();
    trackDef->fragIndex = fragIndex;

    // Get name from hash
    if (nameRef != 0) {
        int32_t nameOffset = -nameRef;
        if (nameOffset > 0) {
            trackDef->name = std::string(&hash[nameOffset]);
        }
    }

    // Parse all frames
    trackDef->frames.reserve(frameCount);

    for (int32_t i = 0; i < frameCount; ++i) {
        BoneTransform frame;

        int16_t rotDenom = read_val<int16_t>(ptr); ptr += 2;
        int16_t rotX = read_val<int16_t>(ptr); ptr += 2;
        int16_t rotY = read_val<int16_t>(ptr); ptr += 2;
        int16_t rotZ = read_val<int16_t>(ptr); ptr += 2;
        int16_t shiftX = read_val<int16_t>(ptr); ptr += 2;
        int16_t shiftY = read_val<int16_t>(ptr); ptr += 2;
        int16_t shiftZ = read_val<int16_t>(ptr); ptr += 2;
        int16_t shiftDenom = read_val<int16_t>(ptr); ptr += 2;

        // EQ stores rotation as quaternion components (w, x, y, z) as int16 values
        float qw = static_cast<float>(rotDenom);
        float qx = static_cast<float>(rotX);
        float qy = static_cast<float>(rotY);
        float qz = static_cast<float>(rotZ);

        // Normalize the quaternion
        float len = std::sqrt(qx*qx + qy*qy + qz*qz + qw*qw);
        if (len > 0.0001f) {
            frame.quatX = qx / len;
            frame.quatY = qy / len;
            frame.quatZ = qz / len;
            frame.quatW = qw / len;
        } else {
            // Identity quaternion if zero length
            frame.quatX = 0.0f;
            frame.quatY = 0.0f;
            frame.quatZ = 0.0f;
            frame.quatW = 1.0f;
        }

        // Translation is divided by 256 (fixed point)
        frame.shiftX = static_cast<float>(shiftX) / 256.0f;
        frame.shiftY = static_cast<float>(shiftY) / 256.0f;
        frame.shiftZ = static_cast<float>(shiftZ) / 256.0f;

        // Scale is shiftDenom / 256 (defaults to 1.0 if zero)
        frame.scale = (shiftDenom != 0) ? static_cast<float>(shiftDenom) / 256.0f : 1.0f;

        trackDef->frames.push_back(frame);
    }

    trackDefs_[fragIndex] = trackDef;

    // Also store first frame as BoneOrientation for backwards compatibility
    if (!trackDef->frames.empty()) {
        auto orientation = std::make_shared<BoneOrientation>(trackDef->frames[0]);
        boneOrientations_[fragIndex] = orientation;
    }
}

void WldLoader::parseFragment13(const char* fragBuffer, uint32_t fragLength, uint32_t fragIndex,
                                int32_t nameRef, const char* hash) {
    // Fragment 0x13 - TrackRef - References a TrackDef and contains metadata
    // Structure:
    //   int32_t nameRef (already in header)
    //   int32_t trackDefRef
    //   int32_t flags
    //   [optional] int32_t frameMs (if flags bit 0 is set)

    const char* ptr = fragBuffer;

    int32_t trackDefRef = read_val<int32_t>(ptr);
    ptr += sizeof(int32_t);

    int32_t flags = read_val<int32_t>(ptr);
    ptr += sizeof(int32_t);

    auto trackRef = std::make_shared<TrackRef>();

    // Get name from hash
    if (nameRef != 0) {
        int32_t nameOffset = -nameRef;
        if (nameOffset > 0) {
            trackRef->name = std::string(&hash[nameOffset]);
        }
    }

    trackRef->trackDefRef = (trackDefRef > 0) ? static_cast<uint32_t>(trackDefRef) : 0;
    trackRef->isPoseAnimation = false;
    trackRef->isNameParsed = false;

    // Read frameMs if flags bit 0 is set
    if (flags & 1) {
        trackRef->frameMs = read_val<int32_t>(ptr);
        ptr += sizeof(int32_t);
    } else {
        trackRef->frameMs = 0;
    }

    // Parse the track name to extract animation code, model code, and bone name
    // Track names follow the pattern: {ANIM_CODE}{MODEL_CODE}{BONE_NAME}_TRACK
    // e.g., "C01HUF_ROOT_TRACK" -> animCode="c01", modelCode="huf", boneName="root"
    // or "HUF_ROOT_TRACK" for pose animation -> animCode="pos", modelCode="huf", boneName="root"
    if (!trackRef->name.empty()) {
        std::string cleanedName = trackRef->name;

        // Remove _TRACK suffix if present
        size_t trackPos = cleanedName.find("_TRACK");
        if (trackPos != std::string::npos) {
            cleanedName = cleanedName.substr(0, trackPos);
        }

        // Convert to lowercase for consistent comparison
        std::transform(cleanedName.begin(), cleanedName.end(), cleanedName.begin(),
                      [](unsigned char c) { return std::tolower(c); });

        // Check if first 3 chars look like an animation code (letter + 2 digits)
        bool hasAnimCode = false;
        if (cleanedName.length() >= 6 &&
            std::isalpha(cleanedName[0]) &&
            std::isdigit(cleanedName[1]) &&
            std::isdigit(cleanedName[2])) {
            hasAnimCode = true;
        }

        if (hasAnimCode) {
            // Pattern: {ANIM_CODE}{MODEL_CODE}{BONE_NAME}
            // e.g., "l01humpe" -> animCode="l01", modelCode="hum", boneName="pe"
            trackRef->animCode = cleanedName.substr(0, 3);
            trackRef->modelCode = cleanedName.substr(3, 3);
            trackRef->boneName = cleanedName.substr(6);
            trackRef->isNameParsed = true;
        } else if (cleanedName.length() >= 4) {
            // Pattern: {MODEL_CODE}{BONE_NAME} (pose animation)
            // e.g., "humpe" -> animCode="pos", modelCode="hum", boneName="pe"
            trackRef->animCode = "pos";
            trackRef->modelCode = cleanedName.substr(0, 3);
            trackRef->boneName = cleanedName.substr(3);
            trackRef->isPoseAnimation = true;
            trackRef->isNameParsed = true;
        } else if (cleanedName.length() == 3) {
            // Just model code (root bone pose)
            trackRef->animCode = "pos";
            trackRef->modelCode = cleanedName;
            trackRef->boneName = "root";
            trackRef->isPoseAnimation = true;
            trackRef->isNameParsed = true;
        }

        // Clean up bone name - remove leading underscore if present
        if (!trackRef->boneName.empty() && trackRef->boneName[0] == '_') {
            trackRef->boneName = trackRef->boneName.substr(1);
        }
    }

    trackRefs_[fragIndex] = trackRef;

    // Also store reference for backwards compatibility
    if (trackRef->trackDefRef > 0) {
        boneOrientationRefs_[fragIndex] = trackRef->trackDefRef;
    }
}

} // namespace Graphics
} // namespace EQT
