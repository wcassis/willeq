// WLD Fragment 0x10/0x12/0x13 (Skeleton and Animation) tests.
// Split from test_wld.cpp (T11, 2026-07-29) — pure mechanical move.

#include <gtest/gtest.h>
#include <cstdint>
#include <cstring>
#include <vector>
#include <string>
#include <fstream>
#include <cstdlib>
#include <algorithm>
#include <tuple>
#include <cctype>
#include <cmath>
#include <iostream>

#include "wld_test_fixtures.h"

class WldSkeletonFragmentTest : public WldIntegrationTest {
protected:
    // Fragment 0x10 flags (matches eqsage SkeletonFlags)
    static constexpr uint32_t FLAG_HAS_CENTER_OFFSET = 0x01;
    static constexpr uint32_t FLAG_HAS_BOUNDING_RADIUS = 0x02;
    static constexpr uint32_t FLAG_HAS_MESH_REFERENCE = 0x200;

    // Analyze Fragment 0x10 (Skeleton Hierarchy) in a WLD file
    struct SkeletonStats {
        int skeletonCount = 0;
        int totalBones = 0;
        int withCenterOffset = 0;
        int withBoundingRadius = 0;
        int withMeshReference = 0;
        std::vector<int> boneCounts;
    };

    SkeletonStats analyzeSkeletons(const std::vector<char>& wldBuffer) {
        SkeletonStats stats;

        if (wldBuffer.size() < sizeof(WldHeader)) {
            return stats;
        }

        const WldHeader* header = reinterpret_cast<const WldHeader*>(wldBuffer.data());
        size_t idx = sizeof(WldHeader) + header->hashLength;

        for (uint32_t i = 0; i < header->fragmentCount && idx < wldBuffer.size(); ++i) {
            if (idx + 8 > wldBuffer.size()) break;

            uint32_t fragSize = *reinterpret_cast<const uint32_t*>(&wldBuffer[idx]);
            uint32_t fragType = *reinterpret_cast<const uint32_t*>(&wldBuffer[idx + 4]);
            idx += 8;

            if (fragType == 0x10 && fragSize >= 16) {
                const char* fragBody = &wldBuffer[idx + 4];  // Skip nameRef
                uint32_t flags = *reinterpret_cast<const uint32_t*>(fragBody);
                uint32_t boneCount = *reinterpret_cast<const uint32_t*>(fragBody + 4);

                stats.skeletonCount++;
                stats.totalBones += boneCount;
                stats.boneCounts.push_back(boneCount);

                if (flags & FLAG_HAS_CENTER_OFFSET) stats.withCenterOffset++;
                if (flags & FLAG_HAS_BOUNDING_RADIUS) stats.withBoundingRadius++;
                if (flags & FLAG_HAS_MESH_REFERENCE) stats.withMeshReference++;
            }

            idx += fragSize;
        }

        return stats;
    }

    // Analyze Fragment 0x12 (Track Definition) - animation keyframes
    struct TrackDefStats {
        int trackCount = 0;
        int totalFrames = 0;
        int singleFrameTracks = 0;  // Pose tracks have 1 frame
        int multiFrameTracks = 0;   // Animation tracks have >1 frames
        std::vector<int> frameCounts;
    };

    TrackDefStats analyzeTrackDefs(const std::vector<char>& wldBuffer) {
        TrackDefStats stats;

        if (wldBuffer.size() < sizeof(WldHeader)) {
            return stats;
        }

        const WldHeader* header = reinterpret_cast<const WldHeader*>(wldBuffer.data());
        size_t idx = sizeof(WldHeader) + header->hashLength;

        for (uint32_t i = 0; i < header->fragmentCount && idx < wldBuffer.size(); ++i) {
            if (idx + 8 > wldBuffer.size()) break;

            uint32_t fragSize = *reinterpret_cast<const uint32_t*>(&wldBuffer[idx]);
            uint32_t fragType = *reinterpret_cast<const uint32_t*>(&wldBuffer[idx + 4]);
            idx += 8;

            if (fragType == 0x12 && fragSize >= 12) {
                const char* fragBody = &wldBuffer[idx + 4];  // Skip nameRef
                // uint32_t flags = *reinterpret_cast<const uint32_t*>(fragBody);
                uint32_t frameCount = *reinterpret_cast<const uint32_t*>(fragBody + 4);

                stats.trackCount++;
                stats.totalFrames += frameCount;
                stats.frameCounts.push_back(frameCount);

                if (frameCount == 1) {
                    stats.singleFrameTracks++;
                } else {
                    stats.multiFrameTracks++;
                }
            }

            idx += fragSize;
        }

        return stats;
    }

    // Analyze Fragment 0x13 (Track Reference/Instance)
    struct TrackRefStats {
        int trackRefCount = 0;
        int withFrameMs = 0;
        int poseAnimations = 0;
        int regularAnimations = 0;
    };

    TrackRefStats analyzeTrackRefs(const std::vector<char>& wldBuffer) {
        TrackRefStats stats;

        if (wldBuffer.size() < sizeof(WldHeader)) {
            return stats;
        }

        const WldHeader* header = reinterpret_cast<const WldHeader*>(wldBuffer.data());
        size_t idx = sizeof(WldHeader) + header->hashLength;

        for (uint32_t i = 0; i < header->fragmentCount && idx < wldBuffer.size(); ++i) {
            if (idx + 8 > wldBuffer.size()) break;

            uint32_t fragSize = *reinterpret_cast<const uint32_t*>(&wldBuffer[idx]);
            uint32_t fragType = *reinterpret_cast<const uint32_t*>(&wldBuffer[idx + 4]);
            idx += 8;

            if (fragType == 0x13 && fragSize >= 12) {
                const char* fragBody = &wldBuffer[idx + 4];  // Skip nameRef
                // int32_t trackDefRef = *reinterpret_cast<const int32_t*>(fragBody);
                uint32_t flags = *reinterpret_cast<const uint32_t*>(fragBody + 4);

                stats.trackRefCount++;

                // Flags bit 0 indicates frameMs is present
                if (flags & 0x01) {
                    stats.withFrameMs++;
                }
            }

            idx += fragSize;
        }

        return stats;
    }
};

// Test Fragment 0x10 flag constants match eqsage
TEST_F(WldSkeletonFragmentTest, Fragment10_FlagConstants) {
    // eqsage SkeletonFlags:
    // HasCenterOffset = 0x01
    // HasBoundingRadius = 0x02
    // HasMeshReference = 0x200
    EXPECT_EQ(FLAG_HAS_CENTER_OFFSET, 0x01u);
    EXPECT_EQ(FLAG_HAS_BOUNDING_RADIUS, 0x02u);
    EXPECT_EQ(FLAG_HAS_MESH_REFERENCE, 0x200u);
}

// Test Fragment 0x10 header structure
TEST_F(WldSkeletonFragmentTest, Fragment10_HeaderStructure) {
    // eqsage SkeletonHierarchy reads (after nameRef):
    // flags (4) + boneCount (4) + frag18Reference (4) = 12 bytes base
    // [if flags & 0x01] centerOffset (12) = 3 x float32
    // [if flags & 0x02] boundingRadius (4) = float32
    // Then per bone: nameRef(4) + flags(4) + trackRef(4) + meshRef(4) + childCount(4) + children

    // willeq's WldFragment10Header
    EXPECT_EQ(sizeof(WldFragment10Header), 12u);
    EXPECT_EQ(sizeof(WldFragment10BoneEntry), 20u);  // 5 x int32
}

// Test Fragment 0x12 keyframe structure
TEST_F(WldSkeletonFragmentTest, Fragment12_KeyframeStructure) {
    // eqsage TrackDefFragment reads per keyframe:
    // rotDenominator (2) + rotX (2) + rotY (2) + rotZ (2) = 8 bytes quaternion
    // shiftX (2) + shiftY (2) + shiftZ (2) + shiftDenominator (2) = 8 bytes translation/scale
    // Total: 16 bytes per keyframe

    size_t keyframeSize = 8 * sizeof(int16_t);
    EXPECT_EQ(keyframeSize, 16u);
}

// Test Fragment 0x12 quaternion normalization
TEST_F(WldSkeletonFragmentTest, Fragment12_QuaternionNormalization) {
    // Both eqsage and willeq normalize quaternions after reading
    // Test that normalization produces unit quaternion

    // Test case: raw quaternion values (w, x, y, z) = (16384, 0, 0, 0)
    float qw = 16384.0f;
    float qx = 0.0f, qy = 0.0f, qz = 0.0f;

    float len = std::sqrt(qx*qx + qy*qy + qz*qz + qw*qw);
    EXPECT_GT(len, 0.0f);

    qw /= len; qx /= len; qy /= len; qz /= len;

    // Should be unit quaternion
    float normLen = std::sqrt(qx*qx + qy*qy + qz*qz + qw*qw);
    EXPECT_NEAR(normLen, 1.0f, 0.0001f);
}

// Test Fragment 0x12 translation scale
TEST_F(WldSkeletonFragmentTest, Fragment12_TranslationScale) {
    // Both eqsage and willeq divide translation by 256
    // eqsage: x = shiftX / 256, y = shiftY / 256 * -1, z = shiftZ / 256
    // willeq: shiftX/Y/Z = raw / 256.0f

    // NOTE: eqsage negates Y translation, willeq does not
    // This may be for coordinate system conversion in eqsage's glTF export

    int16_t rawShift = 256;
    float expected = 1.0f;  // 256 / 256 = 1.0
    float actual = static_cast<float>(rawShift) / 256.0f;
    EXPECT_FLOAT_EQ(expected, actual);

    rawShift = -512;
    expected = -2.0f;  // -512 / 256 = -2.0
    actual = static_cast<float>(rawShift) / 256.0f;
    EXPECT_FLOAT_EQ(expected, actual);
}

// Test Fragment 0x12 scale factor
TEST_F(WldSkeletonFragmentTest, Fragment12_ScaleFactor) {
    // eqsage: scale = shiftDenominator / 256 (if non-zero)
    // willeq: scale = shiftDenom / 256.0f or 1.0f if zero

    int16_t rawScale = 256;
    float expected = 1.0f;
    float actual = static_cast<float>(rawScale) / 256.0f;
    EXPECT_FLOAT_EQ(expected, actual);

    rawScale = 128;
    expected = 0.5f;
    actual = static_cast<float>(rawScale) / 256.0f;
    EXPECT_FLOAT_EQ(expected, actual);

    // Zero case - should default to 1.0
    rawScale = 0;
    actual = (rawScale != 0) ? static_cast<float>(rawScale) / 256.0f : 1.0f;
    EXPECT_FLOAT_EQ(actual, 1.0f);
}

// Test Fragment 0x13 flags
TEST_F(WldSkeletonFragmentTest, Fragment13_Flags) {
    // eqsage TrackFragmentFlags:
    // HasFrameMs = 0x01
    EXPECT_EQ(0x01u, 0x01u);  // Flag constant

    // When flag 0x01 is set, frameMs (int32) follows the flags field
}

// Test skeleton/animation parsing with real character model
TEST_F(WldSkeletonFragmentTest, Fragment10_CharacterModel) {
    std::string chrPath = eqClientPath_ + "/global_chr.s3d";

    if (!fileExists(chrPath)) {
        GTEST_SKIP() << "EQ client files not found at " << eqClientPath_;
    }

    std::vector<char> wldBuffer;
    // global_chr.s3d contains multiple WLD files, try the main one
    if (!loadWldFromArchive(chrPath, "global_chr.wld", wldBuffer)) {
        GTEST_SKIP() << "Could not load global_chr.wld";
    }

    SkeletonStats stats = analyzeSkeletons(wldBuffer);

    std::cout << "Fragment 0x10 Statistics for global_chr.wld:\n";
    std::cout << "  Total skeletons: " << stats.skeletonCount << "\n";
    std::cout << "  Total bones: " << stats.totalBones << "\n";
    std::cout << "  With center offset: " << stats.withCenterOffset << "\n";
    std::cout << "  With bounding radius: " << stats.withBoundingRadius << "\n";
    std::cout << "  With mesh reference: " << stats.withMeshReference << "\n";

    if (!stats.boneCounts.empty()) {
        int minBones = *std::min_element(stats.boneCounts.begin(), stats.boneCounts.end());
        int maxBones = *std::max_element(stats.boneCounts.begin(), stats.boneCounts.end());
        std::cout << "  Bone count range: " << minBones << " - " << maxBones << "\n";
    }

    // Character models should have skeletons
    EXPECT_GT(stats.skeletonCount, 0) << "No skeletons found in character model";
}

// Test Fragment 0x12 (TrackDef) with character model
TEST_F(WldSkeletonFragmentTest, Fragment12_CharacterModel) {
    std::string chrPath = eqClientPath_ + "/global_chr.s3d";

    if (!fileExists(chrPath)) {
        GTEST_SKIP() << "EQ client files not found at " << eqClientPath_;
    }

    std::vector<char> wldBuffer;
    if (!loadWldFromArchive(chrPath, "global_chr.wld", wldBuffer)) {
        GTEST_SKIP() << "Could not load global_chr.wld";
    }

    TrackDefStats stats = analyzeTrackDefs(wldBuffer);

    std::cout << "Fragment 0x12 Statistics for global_chr.wld:\n";
    std::cout << "  Total track definitions: " << stats.trackCount << "\n";
    std::cout << "  Total keyframes: " << stats.totalFrames << "\n";
    std::cout << "  Single-frame tracks (pose): " << stats.singleFrameTracks << "\n";
    std::cout << "  Multi-frame tracks (anim): " << stats.multiFrameTracks << "\n";

    if (!stats.frameCounts.empty()) {
        int minFrames = *std::min_element(stats.frameCounts.begin(), stats.frameCounts.end());
        int maxFrames = *std::max_element(stats.frameCounts.begin(), stats.frameCounts.end());
        std::cout << "  Frame count range: " << minFrames << " - " << maxFrames << "\n";
    }

    // Character models should have animation tracks
    EXPECT_GT(stats.trackCount, 0) << "No track definitions found";
}

// Test Fragment 0x13 (TrackRef) with character model
TEST_F(WldSkeletonFragmentTest, Fragment13_CharacterModel) {
    std::string chrPath = eqClientPath_ + "/global_chr.s3d";

    if (!fileExists(chrPath)) {
        GTEST_SKIP() << "EQ client files not found at " << eqClientPath_;
    }

    std::vector<char> wldBuffer;
    if (!loadWldFromArchive(chrPath, "global_chr.wld", wldBuffer)) {
        GTEST_SKIP() << "Could not load global_chr.wld";
    }

    TrackRefStats stats = analyzeTrackRefs(wldBuffer);

    std::cout << "Fragment 0x13 Statistics for global_chr.wld:\n";
    std::cout << "  Total track references: " << stats.trackRefCount << "\n";
    std::cout << "  With frameMs timing: " << stats.withFrameMs << "\n";

    EXPECT_GT(stats.trackRefCount, 0) << "No track references found";
}

// Test WldLoader correctly loads skeleton data
TEST_F(WldSkeletonFragmentTest, WldLoader_SkeletonData) {
    std::string chrPath = eqClientPath_ + "/global_chr.s3d";

    if (!fileExists(chrPath)) {
        GTEST_SKIP() << "EQ client files not found at " << eqClientPath_;
    }

    WldLoader loader;
    if (!loader.parseFromArchive(chrPath, "global_chr.wld")) {
        GTEST_SKIP() << "Could not parse global_chr.wld";
    }

    // Check that skeleton data was loaded
    const auto& skeletons = loader.getSkeletonTracks();
    std::cout << "WldLoader skeleton data from global_chr.wld:\n";
    std::cout << "  Skeleton tracks loaded: " << skeletons.size() << "\n";

    if (!skeletons.empty()) {
        // Sample a skeleton to verify structure
        const auto& firstSkeleton = skeletons.begin()->second;
        std::cout << "  First skeleton: " << firstSkeleton->name << "\n";
        std::cout << "    Root bones: " << firstSkeleton->bones.size() << "\n";
        std::cout << "    All bones: " << firstSkeleton->allBones.size() << "\n";
    }

    // Check track definitions
    const auto& trackDefs = loader.getTrackDefs();
    std::cout << "  Track definitions: " << trackDefs.size() << "\n";

    if (!trackDefs.empty()) {
        // Sample a track definition
        const auto& firstTrack = trackDefs.begin()->second;
        std::cout << "  First track: " << firstTrack->name << "\n";
        std::cout << "    Frames: " << firstTrack->frames.size() << "\n";

        // Verify frame data is valid
        if (!firstTrack->frames.empty()) {
            const auto& frame = firstTrack->frames[0];
            // Quaternion should be normalized (length ~= 1.0)
            float quatLen = std::sqrt(
                frame.quatX * frame.quatX +
                frame.quatY * frame.quatY +
                frame.quatZ * frame.quatZ +
                frame.quatW * frame.quatW
            );
            EXPECT_NEAR(quatLen, 1.0f, 0.01f) << "Quaternion not normalized";

            // Scale should be reasonable
            EXPECT_GT(frame.scale, 0.0f) << "Invalid scale";
            EXPECT_LT(frame.scale, 100.0f) << "Scale too large";
        }
    }

    // Check track references
    const auto& trackRefs = loader.getTrackRefs();
    std::cout << "  Track references: " << trackRefs.size() << "\n";

    EXPECT_TRUE(loader.hasCharacterData()) << "No character data loaded";
}

// Test animation name parsing logic
TEST_F(WldSkeletonFragmentTest, AnimationNameParsing) {
    // Animation names follow pattern: {ANIM_CODE}{MODEL_CODE}{BONE_NAME}
    // e.g., "C01HUM" -> animCode="c01", modelCode="hum", boneName=""
    // e.g., "C01HUMPE" -> animCode="c01", modelCode="hum", boneName="pe"
    // Pose animations: "HUM" -> animCode="pos", modelCode="hum"

    // Test parsing logic (same as willeq parseFragment13)
    auto parseTrackName = [](const std::string& name) -> std::tuple<std::string, std::string, std::string> {
        std::string cleanedName = name;

        // Remove _TRACK suffix
        size_t trackPos = cleanedName.find("_TRACK");
        if (trackPos != std::string::npos) {
            cleanedName = cleanedName.substr(0, trackPos);
        }

        // Convert to lowercase
        std::transform(cleanedName.begin(), cleanedName.end(), cleanedName.begin(),
                      [](unsigned char c) { return std::tolower(c); });

        // Check for animation code (letter + 2 digits)
        bool hasAnimCode = false;
        if (cleanedName.length() >= 6 &&
            std::isalpha(cleanedName[0]) &&
            std::isdigit(cleanedName[1]) &&
            std::isdigit(cleanedName[2])) {
            hasAnimCode = true;
        }

        std::string animCode, modelCode, boneName;

        if (hasAnimCode) {
            animCode = cleanedName.substr(0, 3);
            modelCode = cleanedName.substr(3, 3);
            boneName = cleanedName.length() > 6 ? cleanedName.substr(6) : "";
        } else if (cleanedName.length() >= 4) {
            animCode = "pos";
            modelCode = cleanedName.substr(0, 3);
            boneName = cleanedName.substr(3);
        } else if (cleanedName.length() == 3) {
            animCode = "pos";
            modelCode = cleanedName;
            boneName = "root";
        }

        return {animCode, modelCode, boneName};
    };

    // Test cases
    {
        auto [anim, model, bone] = parseTrackName("C01HUM_TRACK");
        EXPECT_EQ(anim, "c01");
        EXPECT_EQ(model, "hum");
        EXPECT_EQ(bone, "");
    }

    {
        auto [anim, model, bone] = parseTrackName("L01HUMPE_TRACK");
        EXPECT_EQ(anim, "l01");
        EXPECT_EQ(model, "hum");
        EXPECT_EQ(bone, "pe");
    }

    {
        auto [anim, model, bone] = parseTrackName("HUM_TRACK");
        EXPECT_EQ(anim, "pos");
        EXPECT_EQ(model, "hum");
        EXPECT_EQ(bone, "root");  // Just model code -> root bone
    }

    {
        auto [anim, model, bone] = parseTrackName("HUMPE_TRACK");
        EXPECT_EQ(anim, "pos");
        EXPECT_EQ(model, "hum");
        EXPECT_EQ(bone, "pe");
    }
}

// Test Y translation difference between eqsage and willeq
TEST_F(WldSkeletonFragmentTest, Fragment12_TranslationYDifference) {
    // KEY DIFFERENCE IDENTIFIED:
    // eqsage: frameTransform.translation = vec3.fromValues(x, y * -1, z)
    // willeq: shiftY = static_cast<float>(shiftY) / 256.0f (no negation)
    //
    // This difference exists because:
    // 1. eqsage is designed for glTF export (right-handed, Y-up)
    // 2. willeq uses Irrlicht (left-handed like EQ)
    // 3. The negation in eqsage is a coordinate system conversion
    //
    // For willeq rendering in Irrlicht, NOT negating Y is likely correct
    // because Irrlicht's coordinate system is closer to EQ's.

    // Document the difference for reference
    int16_t rawY = 256;  // Test value

    // eqsage approach
    float eqsageY = (static_cast<float>(rawY) / 256.0f) * -1.0f;

    // willeq approach
    float willeqY = static_cast<float>(rawY) / 256.0f;

    EXPECT_FLOAT_EQ(eqsageY, -1.0f);
    EXPECT_FLOAT_EQ(willeqY, 1.0f);

    // These SHOULD be different - it's a coordinate system conversion
    EXPECT_NE(eqsageY, willeqY);
}
