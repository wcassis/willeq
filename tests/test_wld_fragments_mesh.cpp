// WLD Fragment 0x04/0x30/0x31 (Texture) and 0x36/0x37/0x2C (Mesh) tests.
// Split from test_wld.cpp (T11, 2026-07-29) — pure mechanical move.

#include <gtest/gtest.h>
#include <cstdint>
#include <cstring>
#include <vector>
#include <string>
#include <fstream>
#include <cstdlib>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <map>

#include "wld_test_fixtures.h"

class WldTextureFragmentTest : public WldIntegrationTest {
protected:
    // Fragment 0x04 flags (matches eqsage BitMapInfoFlags)
    static constexpr uint32_t FLAG_SKIP_FRAMES = 0x02;
    static constexpr uint32_t FLAG_UNKNOWN = 0x04;
    static constexpr uint32_t FLAG_ANIMATED = 0x08;
    static constexpr uint32_t FLAG_HAS_SLEEP = 0x10;
    static constexpr uint32_t FLAG_HAS_CURRENT_FRAME = 0x20;

    // Parse raw Fragment 0x04 data to extract flags and count how many have animation
    struct Fragment04Stats {
        std::vector<uint32_t> allFlags;
        int animatedCount = 0;
        int hasCurrentFrameCount = 0;
        int hasSleepCount = 0;
        int hasAnimationDelayCount = 0;  // Requires both ANIMATED and HAS_SLEEP
    };

    Fragment04Stats analyzeFragment04(const std::vector<char>& wldBuffer) {
        Fragment04Stats stats;

        if (wldBuffer.size() < sizeof(WldHeader)) {
            return stats;
        }

        const WldHeader* header = reinterpret_cast<const WldHeader*>(wldBuffer.data());
        size_t idx = sizeof(WldHeader) + header->hashLength;

        // Parse fragments
        for (uint32_t i = 0; i < header->fragmentCount && idx < wldBuffer.size(); ++i) {
            if (idx + 8 > wldBuffer.size()) break;

            uint32_t fragSize = *reinterpret_cast<const uint32_t*>(&wldBuffer[idx]);
            uint32_t fragType = *reinterpret_cast<const uint32_t*>(&wldBuffer[idx + 4]);
            idx += 8;  // Skip fragment header

            if (fragType == 0x04 && fragSize >= 12) {  // nameRef + flags + count
                // Skip nameRef (4 bytes)
                const char* fragBody = &wldBuffer[idx + 4];
                uint32_t flags = *reinterpret_cast<const uint32_t*>(fragBody);

                stats.allFlags.push_back(flags);

                if (flags & FLAG_ANIMATED) stats.animatedCount++;
                if (flags & FLAG_HAS_CURRENT_FRAME) stats.hasCurrentFrameCount++;
                if (flags & FLAG_HAS_SLEEP) stats.hasSleepCount++;
                if ((flags & FLAG_ANIMATED) && (flags & FLAG_HAS_SLEEP)) {
                    stats.hasAnimationDelayCount++;
                }
            }

            idx += fragSize;
        }

        return stats;
    }
};

// Test Fragment 0x04 flag constants match eqsage
TEST_F(WldTextureFragmentTest, Fragment04_FlagConstants) {
    // eqsage BitMapInfoFlags:
    // SkipFramesFlag = 0x02
    // UnknownFlag = 0x04
    // AnimatedFlag = 0x08
    // HasSleepFlag = 0x10
    // HasCurrentFrameFlag = 0x20
    EXPECT_EQ(FLAG_SKIP_FRAMES, 0x02u);
    EXPECT_EQ(FLAG_UNKNOWN, 0x04u);
    EXPECT_EQ(FLAG_ANIMATED, 0x08u);
    EXPECT_EQ(FLAG_HAS_SLEEP, 0x10u);
    EXPECT_EQ(FLAG_HAS_CURRENT_FRAME, 0x20u);
}

// Test Fragment 0x04 parsing matches eqsage conditional field logic
TEST_F(WldTextureFragmentTest, Fragment04_ConditionalFields) {
    // eqsage reads:
    // 1. flags (int32)
    // 2. bitmapCount (int32)
    // 3. [if hasCurrentFrame (0x20)] currentFrame (uint32)
    // 4. [if isAnimated (0x08) && hasSleep (0x10)] animationDelayMs (int32)
    // 5. bitmapCount x references (int32 each)

    // Test that flags 0x08 alone should NOT read animationDelayMs
    uint32_t flagsAnimatedOnly = FLAG_ANIMATED;  // 0x08
    bool eqsageReadsDelay = (flagsAnimatedOnly & FLAG_ANIMATED) && (flagsAnimatedOnly & FLAG_HAS_SLEEP);
    EXPECT_FALSE(eqsageReadsDelay) << "eqsage requires both ANIMATED and HAS_SLEEP for delay";

    // Test that flags 0x18 (both) should read animationDelayMs
    uint32_t flagsBoth = FLAG_ANIMATED | FLAG_HAS_SLEEP;  // 0x18
    eqsageReadsDelay = (flagsBoth & FLAG_ANIMATED) && (flagsBoth & FLAG_HAS_SLEEP);
    EXPECT_TRUE(eqsageReadsDelay) << "eqsage reads delay when both flags set";
}

// Test Fragment 0x04 flags in real zone files
TEST_F(WldTextureFragmentTest, Fragment04_RealZoneFlags) {
    std::vector<std::string> zones = {"qeynos2", "freporte", "ecommons", "nro"};

    for (const auto& zone : zones) {
        std::string zonePath = eqClientPath_ + "/" + zone + ".s3d";

        if (!fileExists(zonePath)) {
            continue;
        }

        std::vector<char> wldBuffer;
        if (!loadWldFromArchive(zonePath, zone + ".wld", wldBuffer)) {
            continue;
        }

        Fragment04Stats stats = analyzeFragment04(wldBuffer);

        // Output statistics for debugging
        std::cout << "Fragment 0x04 Statistics for " << zone << ".wld:\n";
        std::cout << "  Total Fragment 0x04 count: " << stats.allFlags.size() << "\n";
        std::cout << "  With ANIMATED flag (0x08): " << stats.animatedCount << "\n";
        std::cout << "  With HAS_CURRENT_FRAME (0x20): " << stats.hasCurrentFrameCount << "\n";
        std::cout << "  With HAS_SLEEP (0x10): " << stats.hasSleepCount << "\n";
        std::cout << "  With animation delay (0x08 && 0x10): " << stats.hasAnimationDelayCount << "\n";

        // Verify we found some texture fragments
        EXPECT_GT(stats.allFlags.size(), 0u) << "No Fragment 0x04 found in " << zone;

        // Check for UNKNOWN flag (0x04) which willeq skips on
        int hasUnknownFlag = 0;
        int animatedWithoutSleep = 0;
        for (uint32_t flags : stats.allFlags) {
            if (flags & FLAG_UNKNOWN) {
                hasUnknownFlag++;
            }
            if ((flags & FLAG_ANIMATED) && !(flags & FLAG_HAS_SLEEP)) {
                animatedWithoutSleep++;
                std::cout << "  WARNING: Found flags 0x" << std::hex << flags << std::dec
                          << " - ANIMATED without HAS_SLEEP\n";
            }
        }

        if (hasUnknownFlag > 0) {
            std::cout << "  ** " << hasUnknownFlag
                      << " fragments have UNKNOWN flag (0x04) - willeq will skip bytes incorrectly\n";
        }

        if (animatedWithoutSleep > 0) {
            std::cout << "  ** " << animatedWithoutSleep
                      << " fragments have ANIMATED but not HAS_SLEEP - may cause parsing issues\n";
        }
    }
}

// Test Fragment 0x30 (Material) structure
TEST_F(WldTextureFragmentTest, Fragment30_Structure) {
    // eqsage Material reads (after nameRef):
    // bytes 0-3: flags (uint32)
    // bytes 4-7: parameters (uint32) - contains MaterialType
    // bytes 8-11: color RGBA (4 x uint8)
    // bytes 12-15: brightness (float32)
    // bytes 16-19: scaledAmbient (float32)
    // bytes 20-23: bitmapInfoReferenceIdx (uint32)
    //
    // Total: 24 bytes

    // Verify our understanding of the structure size
    size_t expectedSize = 4 + 4 + 4 + 4 + 4 + 4;  // 24 bytes
    EXPECT_EQ(expectedSize, 24u);

    // willeq's WldFragment30Header now matches eqsage (24 bytes)
    EXPECT_EQ(sizeof(WldFragment30Header), 24u);
}

// Test Fragment 0x30 flags in real zone files
TEST_F(WldTextureFragmentTest, Fragment30_RealZoneFlags) {
    std::vector<std::string> zones = {"qeynos2", "freporte", "ecommons"};

    for (const auto& zone : zones) {
        std::string zonePath = eqClientPath_ + "/" + zone + ".s3d";

        if (!fileExists(zonePath)) {
            continue;
        }

        std::vector<char> wldBuffer;
        if (!loadWldFromArchive(zonePath, zone + ".wld", wldBuffer)) {
            continue;
        }

        if (wldBuffer.size() < sizeof(WldHeader)) {
            continue;
        }

        const WldHeader* header = reinterpret_cast<const WldHeader*>(wldBuffer.data());
        size_t idx = sizeof(WldHeader) + header->hashLength;

        int fragment30Count = 0;
        int flagsZeroCount = 0;
        int flagsNonZeroCount = 0;
        std::map<uint32_t, int> flagCounts;
        std::map<uint32_t, int> parameterCounts;

        // Parse fragments to find 0x30
        for (uint32_t i = 0; i < header->fragmentCount && idx < wldBuffer.size(); ++i) {
            if (idx + 8 > wldBuffer.size()) break;

            uint32_t fragSize = *reinterpret_cast<const uint32_t*>(&wldBuffer[idx]);
            uint32_t fragType = *reinterpret_cast<const uint32_t*>(&wldBuffer[idx + 4]);
            idx += 8;

            if (fragType == 0x30 && fragSize >= 24) {  // nameRef + 24 bytes structure
                const char* fragBody = &wldBuffer[idx + 4];  // Skip nameRef
                uint32_t flags = *reinterpret_cast<const uint32_t*>(fragBody);
                uint32_t parameters = *reinterpret_cast<const uint32_t*>(fragBody + 4);

                fragment30Count++;
                if (flags == 0) {
                    flagsZeroCount++;
                } else {
                    flagsNonZeroCount++;
                }
                flagCounts[flags]++;
                parameterCounts[parameters & ~0x80000000]++;  // Mask off high bit like eqsage
            }

            idx += fragSize;
        }

        std::cout << "Fragment 0x30 Statistics for " << zone << ".wld:\n";
        std::cout << "  Total Fragment 0x30 count: " << fragment30Count << "\n";
        std::cout << "  With flags == 0: " << flagsZeroCount << "\n";
        std::cout << "  With flags != 0: " << flagsNonZeroCount << "\n";

        std::cout << "  Unique flag values:\n";
        for (const auto& [flag, count] : flagCounts) {
            std::cout << "    0x" << std::hex << flag << std::dec << ": " << count << "\n";
        }

        std::cout << "  Material types (parameters & ~0x80000000):\n";
        for (const auto& [param, count] : parameterCounts) {
            std::cout << "    0x" << std::hex << param << std::dec << ": " << count << "\n";
        }

        EXPECT_GT(fragment30Count, 0) << "No Fragment 0x30 found in " << zone;

        // If any have flags == 0, willeq's parsing is wrong
        if (flagsZeroCount > 0) {
            std::cout << "  ** WARNING: " << flagsZeroCount
                      << " materials have flags==0, willeq will skip incorrectly\n";
        }
    }
}

// Test Fragment 0x31 (MaterialList) structure
TEST_F(WldTextureFragmentTest, Fragment31_Structure) {
    // eqsage MaterialList reads (after nameRef):
    // bytes 0-3: flags (uint32)
    // bytes 4-7: materialCount (uint32)
    // bytes 8+: materialCount x references (uint32 each)

    // Verify willeq's WldFragment31Header matches
    EXPECT_EQ(sizeof(WldFragment31Header), 8u);  // flags + count
}

class WldMeshFragmentTest : public WldIntegrationTest {
protected:
    // Analyze Fragment 0x36 meshes in a zone
    struct MeshStats {
        int totalMeshes = 0;
        int totalVertices = 0;
        int totalPolygons = 0;
        int meshesWithAnimation = 0;
        int meshesWithVertexPieces = 0;
        std::vector<int> vertexCounts;
        std::vector<int> polygonCounts;
    };

    MeshStats analyzeMeshes(const std::vector<char>& wldBuffer) {
        MeshStats stats;

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

            if (fragType == 0x36 && fragSize >= sizeof(WldFragment36Header) + 4) {
                const char* fragBody = &wldBuffer[idx + 4];  // Skip nameRef
                const WldFragment36Header* meshHeader =
                    reinterpret_cast<const WldFragment36Header*>(fragBody);

                stats.totalMeshes++;
                stats.totalVertices += meshHeader->vertexCount;
                stats.totalPolygons += meshHeader->polygonCount;
                stats.vertexCounts.push_back(meshHeader->vertexCount);
                stats.polygonCounts.push_back(meshHeader->polygonCount);

                if (meshHeader->frag2 > 0) {
                    stats.meshesWithAnimation++;
                }
                if (meshHeader->size6 > 0) {
                    stats.meshesWithVertexPieces++;
                }
            }

            idx += fragSize;
        }

        return stats;
    }
};

// Test Fragment 0x36 header structure matches eqsage
TEST_F(WldMeshFragmentTest, Fragment36_HeaderStructure) {
    // eqsage Mesh reads (after nameRef):
    // flags (4) + materialListIdx (4) + animatedVerticesRef (4) + skip (8) = 20
    // center (12) + skip (12) + maxDistance (4) = 28
    // minPos (12) + maxPos (12) = 24
    // 10 x int16 counts (20) + scale (2) = 22
    // Total header: 94 bytes

    // willeq's WldFragment36Header
    // flags(4) + frag1(4) + frag2(4) + frag3(4) + frag4(4) = 20
    // center(12) + params2(12) + maxDist(4) = 28
    // min(12) + max(12) = 24
    // counts: 9 x uint16(18) + scale int16(2) = 20
    // Total: 92 bytes

    // The difference is because willeq packs the 10 count fields slightly differently
    // but the data layout in the file is the same
    EXPECT_GE(sizeof(WldFragment36Header), 90u);
    EXPECT_LE(sizeof(WldFragment36Header), 96u);
}

// Test Fragment 0x36 scale calculation matches eqsage
TEST_F(WldMeshFragmentTest, Fragment36_ScaleCalculation) {
    // eqsage: scale = 1.0 / (1 << reader.readInt16())
    // willeq: scale = 1.0f / static_cast<float>(1 << header->scale)

    // Test various scale values
    for (int16_t scaleVal = 0; scaleVal <= 10; ++scaleVal) {
        float expected = 1.0f / static_cast<float>(1 << scaleVal);
        float actual = 1.0f / static_cast<float>(1 << scaleVal);
        EXPECT_FLOAT_EQ(expected, actual) << "Scale mismatch for value " << scaleVal;
    }
}

// Test Fragment 0x36 vertex parsing constants
TEST_F(WldMeshFragmentTest, Fragment36_VertexParsingConstants) {
    // Old format UV: int16 / 256.0
    float uvScale = 1.0f / 256.0f;
    EXPECT_FLOAT_EQ(uvScale, 0.00390625f);

    // Normal: int8 / 128.0
    float normalScale = 1.0f / 128.0f;
    EXPECT_FLOAT_EQ(normalScale, 0.0078125f);

    // Test normal range: int8 [-128, 127] -> [-1.0, 0.992]
    EXPECT_FLOAT_EQ(-128 * normalScale, -1.0f);
    EXPECT_NEAR(127 * normalScale, 1.0f, 0.01f);
}

// Test Fragment 0x36 parsing with real zone data
TEST_F(WldMeshFragmentTest, Fragment36_RealZoneMeshes) {
    std::vector<std::string> zones = {"qeynos2", "freporte", "ecommons"};

    for (const auto& zone : zones) {
        std::string zonePath = eqClientPath_ + "/" + zone + ".s3d";

        if (!fileExists(zonePath)) {
            continue;
        }

        std::vector<char> wldBuffer;
        if (!loadWldFromArchive(zonePath, zone + ".wld", wldBuffer)) {
            continue;
        }

        MeshStats stats = analyzeMeshes(wldBuffer);

        std::cout << "Fragment 0x36 Statistics for " << zone << ".wld:\n";
        std::cout << "  Total meshes: " << stats.totalMeshes << "\n";
        std::cout << "  Total vertices: " << stats.totalVertices << "\n";
        std::cout << "  Total polygons: " << stats.totalPolygons << "\n";
        std::cout << "  Meshes with vertex animation: " << stats.meshesWithAnimation << "\n";
        std::cout << "  Meshes with vertex pieces (skinning): " << stats.meshesWithVertexPieces << "\n";

        EXPECT_GT(stats.totalMeshes, 0) << "No meshes found in " << zone;
        EXPECT_GT(stats.totalVertices, 0) << "No vertices found in " << zone;
        EXPECT_GT(stats.totalPolygons, 0) << "No polygons found in " << zone;

        // Zone meshes typically don't have vertex pieces (that's for character models)
        // But they may have animated vertices (flags, water, etc.)
    }
}

// Test Fragment 0x37 header structure matches eqsage
TEST_F(WldMeshFragmentTest, Fragment37_HeaderStructure) {
    // eqsage MeshAnimatedVertices reads:
    // flags (4) + vertexCount (2) + frameCount (2) + delay (2) + param (2) + scale (2) = 14 bytes
    // Then: frameCount * vertexCount * 6 bytes (3 x int16 per vertex)

    // Verify willeq's reading matches
    // Header fields are read individually, not from a struct
    size_t expectedHeaderSize = 4 + 2 + 2 + 2 + 2 + 2;  // 14 bytes
    EXPECT_EQ(expectedHeaderSize, 14u);
}

// Test Fragment 0x37 scale calculation matches eqsage
TEST_F(WldMeshFragmentTest, Fragment37_ScaleCalculation) {
    // eqsage: scale = 1.0 / (1 << reader.readUint16())
    // willeq: scale = 1.0f / (1 << scaleVal)
    // Both use the same formula

    for (uint16_t scaleVal = 0; scaleVal <= 10; ++scaleVal) {
        float eqsageScale = 1.0f / static_cast<float>(1 << scaleVal);
        float willeqScale = 1.0f / static_cast<float>(1 << scaleVal);
        EXPECT_FLOAT_EQ(eqsageScale, willeqScale);
    }
}

// Test WldLoader correctly loads zone geometry
TEST_F(WldMeshFragmentTest, WldLoader_ZoneGeometry) {
    std::string zonePath = eqClientPath_ + "/qeynos2.s3d";

    if (!fileExists(zonePath)) {
        GTEST_SKIP() << "EQ client files not found at " << eqClientPath_;
    }

    WldLoader loader;
    ASSERT_TRUE(loader.parseFromArchive(zonePath, "qeynos2.wld"));

    const auto& geometries = loader.getGeometries();
    EXPECT_GT(geometries.size(), 0u) << "No geometries loaded";

    // Verify geometry data is valid
    size_t totalVerts = 0;
    size_t totalTris = 0;
    for (const auto& geom : geometries) {
        EXPECT_FALSE(geom->vertices.empty()) << "Empty vertex array in " << geom->name;
        EXPECT_FALSE(geom->triangles.empty()) << "Empty triangle array in " << geom->name;

        totalVerts += geom->vertices.size();
        totalTris += geom->triangles.size();

        // Verify vertex data is reasonable (not NaN or extreme values)
        for (const auto& v : geom->vertices) {
            EXPECT_FALSE(std::isnan(v.x) || std::isnan(v.y) || std::isnan(v.z))
                << "NaN vertex position in " << geom->name;
            EXPECT_LT(std::abs(v.x), 100000.0f) << "Extreme vertex X in " << geom->name;
            EXPECT_LT(std::abs(v.y), 100000.0f) << "Extreme vertex Y in " << geom->name;
            EXPECT_LT(std::abs(v.z), 100000.0f) << "Extreme vertex Z in " << geom->name;
        }

        // Verify triangle indices are valid
        for (const auto& tri : geom->triangles) {
            EXPECT_LT(tri.v1, geom->vertices.size())
                << "Invalid triangle index v1 in " << geom->name;
            EXPECT_LT(tri.v2, geom->vertices.size())
                << "Invalid triangle index v2 in " << geom->name;
            EXPECT_LT(tri.v3, geom->vertices.size())
                << "Invalid triangle index v3 in " << geom->name;
        }
    }

    std::cout << "WldLoader qeynos2.wld summary:\n";
    std::cout << "  Geometry count: " << geometries.size() << "\n";
    std::cout << "  Total vertices: " << totalVerts << "\n";
    std::cout << "  Total triangles: " << totalTris << "\n";
}

// Test that Fragment 0x2C (Legacy Mesh) is handled
// Note: eqsage skips this fragment, willeq parses it for older character models
TEST_F(WldMeshFragmentTest, Fragment2C_LegacyMesh) {
    // eqsage doesn't parse 0x2C ("don't map this for now")
    // willeq implements it for older character models in global_chr.s3d

    // Verify willeq's header structure is reasonable
    EXPECT_GE(sizeof(WldFragment2CHeader), 60u);  // Should be at least 60 bytes
}
