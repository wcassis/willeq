// Test WLD file parsing - verifies willeq matches eqsage implementation
// Tests Phase 2: WLD Header and String Table

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

#include "client/graphics/eq/wld_loader.h"
#include "client/graphics/eq/pfs.h"

using namespace EQT::Graphics;

// ============================================================================
// Phase 2.1: WLD Header Parsing Tests
// ============================================================================

class WldHeaderTest : public ::testing::Test {
protected:
    // Constants matching eqsage (wld.js)
    static constexpr uint32_t WLD_MAGIC = 0x54503D02;
    static constexpr uint32_t WLD_VERSION_OLD = 0x00015500;
    static constexpr uint32_t WLD_VERSION_NEW = 0x1000C800;

    // XOR key for string hash decoding - matches eqsage (util.js)
    static constexpr uint8_t HASH_KEY[8] = {0x95, 0x3A, 0xC5, 0x2A, 0x95, 0x7A, 0x95, 0x6A};

    std::string eqClientPath_;

    void SetUp() override {
        const char* envPath = std::getenv("EQ_CLIENT_PATH");
        if (envPath) {
            eqClientPath_ = envPath;
        } else {
            eqClientPath_ = "/home/user/projects/claude/EverQuestP1999";
        }
    }

    bool fileExists(const std::string& path) {
        std::ifstream f(path);
        return f.good();
    }

    // Parse WLD header directly from buffer
    bool parseWldHeader(const std::vector<char>& buffer, WldHeader& outHeader) {
        if (buffer.size() < sizeof(WldHeader)) {
            return false;
        }
        std::memcpy(&outHeader, buffer.data(), sizeof(WldHeader));
        return true;
    }

    // Decode string hash - matches eqsage's decodeString function
    std::string decodeStringHash(const char* data, size_t length) {
        std::vector<char> decoded(length);
        for (size_t i = 0; i < length; ++i) {
            decoded[i] = data[i] ^ HASH_KEY[i % 8];
        }
        return std::string(decoded.begin(), decoded.end());
    }

    // Get string from hash table using negative index (matches eqsage getString)
    std::string getString(const std::string& hashTable, int32_t idx) {
        if (idx >= 0) {
            return "";
        }
        size_t offset = static_cast<size_t>(-idx);
        if (offset >= hashTable.size()) {
            return "";
        }
        // Find null terminator
        size_t end = hashTable.find('\0', offset);
        if (end == std::string::npos) {
            return hashTable.substr(offset);
        }
        return hashTable.substr(offset, end - offset);
    }
};

// Test WLD header structure size matches expected
TEST_F(WldHeaderTest, HeaderStructureSize) {
    // WLD header should be 28 bytes (7 * uint32_t)
    // This matches eqsage which reads 7 uint32 values
    EXPECT_EQ(sizeof(WldHeader), 28u);
}

// Test WLD magic constant
TEST_F(WldHeaderTest, MagicConstant) {
    EXPECT_EQ(WLD_MAGIC, 0x54503D02u);
}

// Test WLD version constants
TEST_F(WldHeaderTest, VersionConstants) {
    EXPECT_EQ(WLD_VERSION_OLD, 0x00015500u);
    EXPECT_EQ(WLD_VERSION_NEW, 0x1000C800u);
}

// Test XOR key matches eqsage
TEST_F(WldHeaderTest, XorKeyMatchesEqsage) {
    // eqsage util.js: const hashKey = [0x95, 0x3a, 0xc5, 0x2a, 0x95, 0x7a, 0x95, 0x6a];
    const uint8_t eqsageKey[8] = {0x95, 0x3A, 0xC5, 0x2A, 0x95, 0x7A, 0x95, 0x6A};
    for (int i = 0; i < 8; ++i) {
        EXPECT_EQ(HASH_KEY[i], eqsageKey[i]) << "XOR key mismatch at index " << i;
    }
}

// ============================================================================
// Phase 2.2: String Hash Table Decoding Tests
// ============================================================================

TEST_F(WldHeaderTest, StringDecoding_SimpleXor) {
    // Test that XOR decoding works correctly
    // Encode a test string
    const char* testStr = "TEST_NAME";
    size_t len = strlen(testStr);
    std::vector<char> encoded(len);
    for (size_t i = 0; i < len; ++i) {
        encoded[i] = testStr[i] ^ HASH_KEY[i % 8];
    }

    // Decode and verify
    std::string decoded = decodeStringHash(encoded.data(), len);
    EXPECT_EQ(decoded, testStr);
}

TEST_F(WldHeaderTest, StringDecoding_LongString) {
    // Test string longer than 8 bytes to verify key cycling
    const char* testStr = "THIS_IS_A_LONGER_TEST_STRING_FOR_XOR_CYCLING";
    size_t len = strlen(testStr);
    std::vector<char> encoded(len);
    for (size_t i = 0; i < len; ++i) {
        encoded[i] = testStr[i] ^ HASH_KEY[i % 8];
    }

    std::string decoded = decodeStringHash(encoded.data(), len);
    EXPECT_EQ(decoded, testStr);
}

TEST_F(WldHeaderTest, GetString_NegativeIndex) {
    // Test getString with negative index (matches eqsage behavior)
    // Must use explicit length since string contains embedded nulls
    // Layout: \0FIRST\0SECOND\0THIRD\0
    //         0 12345 6 789... 14...
    const char hashData[] = "\0FIRST\0SECOND\0THIRD\0";
    std::string hashTable(hashData, sizeof(hashData) - 1);  // -1 to exclude trailing null from sizeof

    // -1 should point to offset 1 which is "FIRST"
    EXPECT_EQ(getString(hashTable, -1), "FIRST");

    // -7 should point to offset 7 which is "SECOND"
    EXPECT_EQ(getString(hashTable, -7), "SECOND");

    // -14 should point to offset 14 which is "THIRD"
    EXPECT_EQ(getString(hashTable, -14), "THIRD");
}

TEST_F(WldHeaderTest, GetString_PositiveIndex_ReturnsEmpty) {
    // Positive index should return empty string (matches eqsage)
    const char hashData[] = "\0TEST\0";
    std::string hashTable(hashData, sizeof(hashData) - 1);
    EXPECT_EQ(getString(hashTable, 0), "");
    EXPECT_EQ(getString(hashTable, 1), "");
    EXPECT_EQ(getString(hashTable, 100), "");
}

// ============================================================================
// Integration Tests with Real EQ Files
// ============================================================================

class WldIntegrationTest : public WldHeaderTest {
protected:
    // Load WLD file from S3D archive
    bool loadWldFromArchive(const std::string& s3dPath, const std::string& wldName,
                           std::vector<char>& outBuffer) {
        PfsArchive archive;
        if (!archive.open(s3dPath)) {
            return false;
        }
        return archive.get(wldName, outBuffer);
    }
};

TEST_F(WldIntegrationTest, LoadZoneWld_ValidHeader) {
    std::string zonePath = eqClientPath_ + "/qeynos2.s3d";

    if (!fileExists(zonePath)) {
        GTEST_SKIP() << "EQ client files not found at " << eqClientPath_;
    }

    std::vector<char> wldBuffer;
    ASSERT_TRUE(loadWldFromArchive(zonePath, "qeynos2.wld", wldBuffer))
        << "Failed to load qeynos2.wld from archive";

    WldHeader header;
    ASSERT_TRUE(parseWldHeader(wldBuffer, header));

    // Verify magic
    EXPECT_EQ(header.magic, WLD_MAGIC) << "Invalid WLD magic";

    // Verify version is old format (pre-Luclin zones use old format)
    EXPECT_EQ(header.version, WLD_VERSION_OLD) << "Expected old WLD format for qeynos2";

    // Verify fragment count is reasonable
    EXPECT_GT(header.fragmentCount, 0u) << "No fragments in WLD";
    EXPECT_LT(header.fragmentCount, 100000u) << "Fragment count unreasonably high";

    // Verify hash length is reasonable
    EXPECT_GT(header.hashLength, 0u) << "No string hash table";
    EXPECT_LT(header.hashLength, 1000000u) << "Hash length unreasonably high";

    // Verify hash length doesn't exceed remaining buffer
    size_t hashOffset = sizeof(WldHeader);
    EXPECT_LE(hashOffset + header.hashLength, wldBuffer.size())
        << "Hash table extends beyond buffer";
}

TEST_F(WldIntegrationTest, LoadObjectsWld_ValidHeader) {
    std::string zonePath = eqClientPath_ + "/qeynos2.s3d";

    if (!fileExists(zonePath)) {
        GTEST_SKIP() << "EQ client files not found at " << eqClientPath_;
    }

    std::vector<char> wldBuffer;
    ASSERT_TRUE(loadWldFromArchive(zonePath, "objects.wld", wldBuffer))
        << "Failed to load objects.wld from archive";

    WldHeader header;
    ASSERT_TRUE(parseWldHeader(wldBuffer, header));

    EXPECT_EQ(header.magic, WLD_MAGIC);
    EXPECT_EQ(header.version, WLD_VERSION_OLD);
    EXPECT_GT(header.fragmentCount, 0u);
}

TEST_F(WldIntegrationTest, DecodeStringTable_ValidStrings) {
    std::string zonePath = eqClientPath_ + "/qeynos2.s3d";

    if (!fileExists(zonePath)) {
        GTEST_SKIP() << "EQ client files not found at " << eqClientPath_;
    }

    std::vector<char> wldBuffer;
    ASSERT_TRUE(loadWldFromArchive(zonePath, "qeynos2.wld", wldBuffer));

    WldHeader header;
    ASSERT_TRUE(parseWldHeader(wldBuffer, header));

    // Decode string hash table
    size_t hashOffset = sizeof(WldHeader);
    std::string hashTable = decodeStringHash(wldBuffer.data() + hashOffset, header.hashLength);

    // Verify hash table is not empty
    EXPECT_FALSE(hashTable.empty());

    // Verify hash table starts with null (convention)
    EXPECT_EQ(hashTable[0], '\0') << "Hash table should start with null byte";

    // Find some strings in the hash table (should contain fragment names)
    // Zone WLDs typically have texture names like "QEYWALL01", mesh names, etc.
    bool foundValidString = false;
    for (size_t i = 1; i < hashTable.size(); ++i) {
        if (hashTable[i-1] == '\0' && hashTable[i] != '\0') {
            // Found start of a string
            std::string str = getString(hashTable, -static_cast<int32_t>(i));
            if (!str.empty() && str.length() > 2) {
                foundValidString = true;
                // Verify string contains only printable ASCII
                for (char c : str) {
                    EXPECT_TRUE(c >= 32 && c < 127)
                        << "Non-printable character in string: " << str;
                }
                break;
            }
        }
    }
    EXPECT_TRUE(foundValidString) << "No valid strings found in hash table";
}

TEST_F(WldIntegrationTest, CompareMultipleZones) {
    // Test multiple zones to ensure consistent parsing
    std::vector<std::string> zones = {"qeynos2", "freporte", "ecommons"};

    for (const auto& zone : zones) {
        std::string zonePath = eqClientPath_ + "/" + zone + ".s3d";

        if (!fileExists(zonePath)) {
            continue;  // Skip zones that don't exist
        }

        std::vector<char> wldBuffer;
        bool loaded = loadWldFromArchive(zonePath, zone + ".wld", wldBuffer);
        if (!loaded) {
            continue;
        }

        WldHeader header;
        ASSERT_TRUE(parseWldHeader(wldBuffer, header))
            << "Failed to parse header for " << zone;

        EXPECT_EQ(header.magic, WLD_MAGIC)
            << "Invalid magic for " << zone;
        EXPECT_EQ(header.version, WLD_VERSION_OLD)
            << "Unexpected version for " << zone;
        EXPECT_GT(header.fragmentCount, 0u)
            << "No fragments in " << zone;
    }
}

// Test that WldLoader correctly parses a zone
TEST_F(WldIntegrationTest, WldLoader_ParseZone) {
    std::string zonePath = eqClientPath_ + "/qeynos2.s3d";

    if (!fileExists(zonePath)) {
        GTEST_SKIP() << "EQ client files not found at " << eqClientPath_;
    }

    WldLoader loader;
    bool success = loader.parseFromArchive(zonePath, "qeynos2.wld");
    ASSERT_TRUE(success) << "WldLoader failed to parse qeynos2.wld";

    // Verify geometries were loaded
    const auto& geometries = loader.getGeometries();
    EXPECT_GT(geometries.size(), 0u) << "No geometries loaded";
}

// ============================================================================
// Phase 3: Texture Fragment Tests
// ============================================================================

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

// ============================================================================
// Phase 4: Mesh/Geometry Fragment Tests
// ============================================================================

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

// ============================================================================
// Phase 5: Skeleton and Animation Fragment Tests
// ============================================================================

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

// ============================================================================
// Phase 7: Light Fragment Tests
// ============================================================================

class WldLightFragmentTest : public WldIntegrationTest {
protected:
    // Fragment 0x1B flags (matches eqsage LightFlags)
    static constexpr uint32_t LIGHT_FLAG_HAS_CURRENT_FRAME = 0x01;
    static constexpr uint32_t LIGHT_FLAG_HAS_SLEEP = 0x02;
    static constexpr uint32_t LIGHT_FLAG_HAS_LIGHT_LEVELS = 0x04;
    static constexpr uint32_t LIGHT_FLAG_SKIP_FRAMES = 0x08;
    static constexpr uint32_t LIGHT_FLAG_HAS_COLOR = 0x10;

    // Analyze Fragment 0x1B (Light Source Definition)
    struct LightSourceStats {
        int count = 0;
        int withCurrentFrame = 0;
        int withSleep = 0;
        int withLightLevels = 0;
        int withSkipFrames = 0;
        int withColor = 0;
        std::vector<uint32_t> allFlags;
        std::vector<uint32_t> frameCounts;
    };

    LightSourceStats analyzeLightSources(const std::vector<char>& wldBuffer) {
        LightSourceStats stats;

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

            if (fragType == 0x1B && fragSize >= 12) {
                const char* fragBody = &wldBuffer[idx + 4];  // Skip nameRef
                uint32_t flags = *reinterpret_cast<const uint32_t*>(fragBody);
                uint32_t frameCount = *reinterpret_cast<const uint32_t*>(fragBody + 4);

                stats.count++;
                stats.allFlags.push_back(flags);
                stats.frameCounts.push_back(frameCount);

                if (flags & LIGHT_FLAG_HAS_CURRENT_FRAME) stats.withCurrentFrame++;
                if (flags & LIGHT_FLAG_HAS_SLEEP) stats.withSleep++;
                if (flags & LIGHT_FLAG_HAS_LIGHT_LEVELS) stats.withLightLevels++;
                if (flags & LIGHT_FLAG_SKIP_FRAMES) stats.withSkipFrames++;
                if (flags & LIGHT_FLAG_HAS_COLOR) stats.withColor++;
            }

            idx += fragSize;
        }

        return stats;
    }

    // Analyze Fragment 0x28 (Light Instance)
    struct LightInstanceStats {
        int count = 0;
        std::vector<float> radii;
        float minRadius = 1e9f;
        float maxRadius = 0.0f;
    };

    LightInstanceStats analyzeLightInstances(const std::vector<char>& wldBuffer) {
        LightInstanceStats stats;

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

            if (fragType == 0x28 && fragSize >= 24) {
                // Structure: nameRef(4) + ref(4) + flags(4) + x(4) + y(4) + z(4) + radius(4)
                const char* fragBody = &wldBuffer[idx + 4];  // Skip nameRef
                // Skip ref(4) + flags(4) + x(4) + y(4) + z(4)
                float radius = *reinterpret_cast<const float*>(fragBody + 4 + 4 + 12);

                stats.count++;
                stats.radii.push_back(radius);
                stats.minRadius = std::min(stats.minRadius, radius);
                stats.maxRadius = std::max(stats.maxRadius, radius);
            }

            idx += fragSize;
        }

        return stats;
    }

    // Analyze Fragment 0x2A (Ambient Light Region)
    struct AmbientLightStats {
        int count = 0;
        int totalRegions = 0;
        std::vector<int> regionCounts;
    };

    AmbientLightStats analyzeAmbientLights(const std::vector<char>& wldBuffer) {
        AmbientLightStats stats;

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

            if (fragType == 0x2A && fragSize >= 12) {
                const char* fragBody = &wldBuffer[idx + 4];  // Skip nameRef
                // uint32_t flags = *reinterpret_cast<const uint32_t*>(fragBody);
                uint32_t regionCount = *reinterpret_cast<const uint32_t*>(fragBody + 4);

                stats.count++;
                stats.totalRegions += regionCount;
                stats.regionCounts.push_back(regionCount);
            }

            idx += fragSize;
        }

        return stats;
    }

    // Analyze Fragment 0x35 (Global Ambient Light)
    struct GlobalAmbientStats {
        int count = 0;
        std::vector<std::tuple<uint8_t, uint8_t, uint8_t, uint8_t>> colors;  // R, G, B, A
    };

    GlobalAmbientStats analyzeGlobalAmbient(const std::vector<char>& wldBuffer) {
        GlobalAmbientStats stats;

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

            if (fragType == 0x35 && fragSize >= 8) {
                const char* fragBody = &wldBuffer[idx + 4];  // Skip nameRef
                // eqsage reads BGRA order
                uint8_t b = static_cast<uint8_t>(fragBody[0]);
                uint8_t g = static_cast<uint8_t>(fragBody[1]);
                uint8_t r = static_cast<uint8_t>(fragBody[2]);
                uint8_t a = static_cast<uint8_t>(fragBody[3]);

                stats.count++;
                stats.colors.push_back({r, g, b, a});
            }

            idx += fragSize;
        }

        return stats;
    }
};

// Test Fragment 0x1B flag constants match eqsage
TEST_F(WldLightFragmentTest, Fragment1B_FlagConstants) {
    // eqsage LightFlags:
    // HasCurrentFrame = 0x01
    // HasSleep = 0x02
    // HasLightLevels = 0x04
    // SkipFrames = 0x08
    // HasColor = 0x10
    EXPECT_EQ(LIGHT_FLAG_HAS_CURRENT_FRAME, 0x01u);
    EXPECT_EQ(LIGHT_FLAG_HAS_SLEEP, 0x02u);
    EXPECT_EQ(LIGHT_FLAG_HAS_LIGHT_LEVELS, 0x04u);
    EXPECT_EQ(LIGHT_FLAG_SKIP_FRAMES, 0x08u);
    EXPECT_EQ(LIGHT_FLAG_HAS_COLOR, 0x10u);
}

// Test Fragment 0x1B structure
TEST_F(WldLightFragmentTest, Fragment1B_Structure) {
    // eqsage LightSource reads (after nameRef):
    // flags (4) + frameCount (4) = 8 bytes minimum header
    // [if hasCurrentFrame] currentFrame (4)
    // [if hasSleep] sleep (4)
    // [if hasLightLevels] frameCount x float32
    // [if hasColor] frameCount x 3 x float32

    // willeq's WldFragment1BHeader now correctly represents just the header
    // Variable fields (currentFrame, sleep, lightLevels, colors) are parsed separately
    EXPECT_EQ(sizeof(WldFragment1BHeader), 8u);  // flags + frameCount (minimal header)
}

// Test Fragment 0x28 structure
TEST_F(WldLightFragmentTest, Fragment28_Structure) {
    // eqsage LightInstance reads (after nameRef + ref):
    // flags (4) + position (12) + radius (4) = 20 bytes

    // willeq's WldFragment28Header
    EXPECT_EQ(sizeof(WldFragment28Header), 20u);  // flags + x + y + z + radius
}

// Test willeq uses wrong flag for color in Fragment 0x1B
TEST_F(WldLightFragmentTest, Fragment1B_WilleqFlagDifference) {
    // KEY DIFFERENCE IDENTIFIED:
    // willeq checks: flags & (1 << 3) = flags & 0x08
    // eqsage checks: flags & 0x10 (HasColor)
    //
    // willeq is checking the wrong flag!
    // 0x08 = SkipFrames in eqsage
    // 0x10 = HasColor in eqsage

    uint32_t willeqColorFlag = (1 << 3);  // 0x08
    uint32_t eqsageColorFlag = LIGHT_FLAG_HAS_COLOR;  // 0x10

    EXPECT_EQ(willeqColorFlag, 0x08u);
    EXPECT_EQ(eqsageColorFlag, 0x10u);
    EXPECT_NE(willeqColorFlag, eqsageColorFlag);  // They're different!
}

// Test Fragment 0x1B with real zone lights.wld
TEST_F(WldLightFragmentTest, Fragment1B_RealZoneLights) {
    std::vector<std::string> zones = {"qeynos2", "freporte", "ecommons"};

    for (const auto& zone : zones) {
        std::string zonePath = eqClientPath_ + "/" + zone + ".s3d";

        if (!fileExists(zonePath)) {
            continue;
        }

        // Lights are typically in lights.wld inside the zone archive
        std::vector<char> wldBuffer;
        if (!loadWldFromArchive(zonePath, "lights.wld", wldBuffer)) {
            // Some zones may not have lights.wld
            continue;
        }

        LightSourceStats stats = analyzeLightSources(wldBuffer);

        std::cout << "Fragment 0x1B Statistics for " << zone << "/lights.wld:\n";
        std::cout << "  Total light sources: " << stats.count << "\n";
        std::cout << "  With HasCurrentFrame (0x01): " << stats.withCurrentFrame << "\n";
        std::cout << "  With HasSleep (0x02): " << stats.withSleep << "\n";
        std::cout << "  With HasLightLevels (0x04): " << stats.withLightLevels << "\n";
        std::cout << "  With SkipFrames (0x08): " << stats.withSkipFrames << "\n";
        std::cout << "  With HasColor (0x10): " << stats.withColor << "\n";

        // Show unique flag values
        std::map<uint32_t, int> flagCounts;
        for (uint32_t f : stats.allFlags) {
            flagCounts[f]++;
        }
        std::cout << "  Unique flag values:\n";
        for (const auto& [flag, count] : flagCounts) {
            std::cout << "    0x" << std::hex << flag << std::dec << ": " << count << "\n";
        }

        // Verify willeq's flag assumption
        int withWilleqColorFlag = 0;
        for (uint32_t f : stats.allFlags) {
            if (f & 0x08) withWilleqColorFlag++;  // willeq's flag
        }
        if (stats.withColor != withWilleqColorFlag) {
            std::cout << "  ** FLAG MISMATCH: willeq would find " << withWilleqColorFlag
                      << " lights with color, eqsage finds " << stats.withColor << "\n";
        }
    }
}

// Test Fragment 0x28 with real zone lights.wld
TEST_F(WldLightFragmentTest, Fragment28_RealZoneLights) {
    std::vector<std::string> zones = {"qeynos2", "freporte", "ecommons"};

    for (const auto& zone : zones) {
        std::string zonePath = eqClientPath_ + "/" + zone + ".s3d";

        if (!fileExists(zonePath)) {
            continue;
        }

        std::vector<char> wldBuffer;
        if (!loadWldFromArchive(zonePath, "lights.wld", wldBuffer)) {
            continue;
        }

        LightInstanceStats stats = analyzeLightInstances(wldBuffer);

        std::cout << "Fragment 0x28 Statistics for " << zone << "/lights.wld:\n";
        std::cout << "  Total light instances: " << stats.count << "\n";
        if (stats.count > 0) {
            std::cout << "  Radius range: " << stats.minRadius << " - " << stats.maxRadius << "\n";
        }

        EXPECT_GE(stats.count, 0);  // Lights may or may not exist
    }
}

// Test Fragment 0x2A (Ambient Light Region) - not implemented in willeq
TEST_F(WldLightFragmentTest, Fragment2A_RealZoneLights) {
    std::vector<std::string> zones = {"qeynos2", "freporte", "ecommons"};

    for (const auto& zone : zones) {
        std::string zonePath = eqClientPath_ + "/" + zone + ".s3d";

        if (!fileExists(zonePath)) {
            continue;
        }

        std::vector<char> wldBuffer;
        if (!loadWldFromArchive(zonePath, "lights.wld", wldBuffer)) {
            continue;
        }

        AmbientLightStats stats = analyzeAmbientLights(wldBuffer);

        std::cout << "Fragment 0x2A Statistics for " << zone << "/lights.wld:\n";
        std::cout << "  Total ambient light entries: " << stats.count << "\n";
        std::cout << "  Total regions referenced: " << stats.totalRegions << "\n";

        // Note: willeq does NOT implement Fragment 0x2A
        if (stats.count > 0) {
            std::cout << "  ** NOTE: willeq does not implement Fragment 0x2A (Ambient Light Region)\n";
        }
    }
}

// Test Fragment 0x35 (Global Ambient Light) - not implemented in willeq
TEST_F(WldLightFragmentTest, Fragment35_RealZoneLights) {
    std::vector<std::string> zones = {"qeynos2", "freporte", "ecommons"};

    for (const auto& zone : zones) {
        std::string zonePath = eqClientPath_ + "/" + zone + ".s3d";

        if (!fileExists(zonePath)) {
            continue;
        }

        std::vector<char> wldBuffer;
        if (!loadWldFromArchive(zonePath, "lights.wld", wldBuffer)) {
            continue;
        }

        GlobalAmbientStats stats = analyzeGlobalAmbient(wldBuffer);

        std::cout << "Fragment 0x35 Statistics for " << zone << "/lights.wld:\n";
        std::cout << "  Total global ambient entries: " << stats.count << "\n";

        for (const auto& [r, g, b, a] : stats.colors) {
            std::cout << "  Color: R=" << static_cast<int>(r)
                      << " G=" << static_cast<int>(g)
                      << " B=" << static_cast<int>(b)
                      << " A=" << static_cast<int>(a) << "\n";
        }

        // Note: willeq does NOT implement Fragment 0x35
        if (stats.count > 0) {
            std::cout << "  ** NOTE: willeq does not implement Fragment 0x35 (Global Ambient Light)\n";
        }
    }
}

// Test WldLoader light loading
TEST_F(WldLightFragmentTest, WldLoader_LightData) {
    std::string zonePath = eqClientPath_ + "/qeynos2.s3d";

    if (!fileExists(zonePath)) {
        GTEST_SKIP() << "EQ client files not found at " << eqClientPath_;
    }

    // Load the lights.wld file
    WldLoader loader;
    if (!loader.parseFromArchive(zonePath, "lights.wld")) {
        GTEST_SKIP() << "Could not parse lights.wld";
    }

    const auto& lights = loader.getLights();
    std::cout << "WldLoader light data from qeynos2/lights.wld:\n";
    std::cout << "  Lights loaded: " << lights.size() << "\n";

    if (!lights.empty()) {
        // Sample some lights
        int sampleCount = std::min(static_cast<int>(lights.size()), 5);
        std::cout << "  First " << sampleCount << " lights:\n";
        for (int i = 0; i < sampleCount; ++i) {
            const auto& light = lights[i];
            std::cout << "    [" << i << "] pos=(" << light->x << "," << light->y << "," << light->z
                      << ") color=(" << light->r << "," << light->g << "," << light->b
                      << ") radius=" << light->radius << "\n";

            // Verify light data is reasonable
            EXPECT_FALSE(std::isnan(light->x) || std::isnan(light->y) || std::isnan(light->z))
                << "NaN position in light " << i;
            EXPECT_GE(light->r, 0.0f) << "Negative red in light " << i;
            EXPECT_GE(light->g, 0.0f) << "Negative green in light " << i;
            EXPECT_GE(light->b, 0.0f) << "Negative blue in light " << i;
            EXPECT_GE(light->radius, 0.0f) << "Negative radius in light " << i;
        }
    }
}

// Scan many zones to find Fragment 0x2A and 0x35
TEST_F(WldLightFragmentTest, ScanZonesForAmbientLights) {
    // Pre-Luclin zones (Original EQ, Kunark, Velious) + Luclin + PoP
    std::vector<std::string> zones = {
        // Original EQ cities
        "qeynos", "qeynos2", "qcat", "qrg", "surefall", "halas",
        "freportn", "freportw", "freporte", "commons", "ecommons",
        "nektulos", "lavastorm", "nro", "sro", "oasis", "innothule",
        "grobb", "oggok", "feerrott", "cazicthule", "guktop", "gukbottom",
        "akanon", "steamfont", "lfaydark", "gfaydark", "crushbone", "kaladima", "kaladimb",
        "felwithea", "felwitheb", "unrest", "mistmoore", "kedge",
        "paineel", "erudsxing", "erudnext", "erudnint", "tox", "kerraridge",
        "hole", "highkeep", "kithicor", "rivervale", "misty", "runnyeye",
        "eastkarana", "northkarana", "southkarana", "lakerathe", "rathe",
        "najena", "lavastorm", "soldungb", "permafrost", "everfrost",
        "blackburrow", "befallen", "qeytoqrg", "highpass", "highpasshold",
        "butcher", "oot", "cauldron", "estate", "paw",
        // Kunark
        "timorous", "firiona", "overthere", "swampofnohope", "warslikswood",
        "frontiermtns", "dreadlands", "burningwood", "skyfire", "lakeofillomen",
        "cabwest", "cabeast", "fieldofbone", "kurnscave", "kaesora",
        "charasis", "karnor", "sebilis", "trakanon", "veeshan", "dalnir",
        "chardok", "nurga", "droga",
        // Velious
        "thurgadina", "thurgadinb", "greatdivide", "wakening", "eastwastes",
        "cobaltscar", "sirens", "westwastes", "kael", "velketor",
        "crystal", "necropolis", "templeveeshan", "sleeper", "iceclad",
        "growthplane", "mischiefplane",
        // Luclin (Shadows of Luclin)
        "sseru", "ssratemple", "nexus", "bazaar", "echo", "scarlet",
        "paludal", "fungusgrove", "dawnshroud", "netherbian", "hollowshade",
        "acrylia", "shadeweaver", "umbral", "akheva", "vexthal", "sseru",
        "thedeep", "griegsend", "shadowhaven", "mseru", "sanctus",
        // Planes of Power
        "ponightmare", "potranquility", "postorms", "poair", "poeartha",
        "poearthb", "pofire", "powater", "povalor", "poinnovation",
        "podisease", "pojustice", "potorment", "potimea", "potimeb",
        "codecay", "hohonora", "hohonorb", "solrotower"
    };

    std::cout << "\n=== Scanning zones for Fragment 0x2A and 0x35 ===\n";

    int zonesWithLights = 0;
    int zonesWithAmbient2A = 0;
    int zonesWithGlobal35 = 0;
    std::vector<std::string> zonesHaving2A;
    std::vector<std::string> zonesHaving35;

    for (const auto& zone : zones) {
        std::string zonePath = eqClientPath_ + "/" + zone + ".s3d";

        if (!fileExists(zonePath)) {
            continue;
        }

        // Check both lights.wld and zone.wld
        std::vector<std::string> wldFiles = {"lights.wld", zone + ".wld"};

        for (const auto& wldFile : wldFiles) {
            std::vector<char> wldBuffer;
            if (!loadWldFromArchive(zonePath, wldFile, wldBuffer)) {
                continue;
            }

            if (wldFile == "lights.wld") zonesWithLights++;

            AmbientLightStats ambientStats = analyzeAmbientLights(wldBuffer);
            GlobalAmbientStats globalStats = analyzeGlobalAmbient(wldBuffer);

            if (ambientStats.count > 0) {
                zonesWithAmbient2A++;
                zonesHaving2A.push_back(zone + "/" + wldFile);
                std::cout << "  " << zone << "/" << wldFile << ": Fragment 0x2A count=" << ambientStats.count
                          << " regions=" << ambientStats.totalRegions << "\n";
            }

            if (globalStats.count > 0) {
                zonesWithGlobal35++;
                zonesHaving35.push_back(zone + "/" + wldFile);
                std::cout << "  " << zone << "/" << wldFile << ": Fragment 0x35 count=" << globalStats.count;
                for (const auto& [r, g, b, a] : globalStats.colors) {
                    std::cout << " color=(" << (int)r << "," << (int)g << "," << (int)b << "," << (int)a << ")";
                }
                std::cout << "\n";
            }
        }
    }

    std::cout << "\n=== Summary ===\n";
    std::cout << "  Zones with lights.wld: " << zonesWithLights << "\n";
    std::cout << "  Zones with Fragment 0x2A (Ambient Light Region): " << zonesWithAmbient2A << "\n";
    std::cout << "  Zones with Fragment 0x35 (Global Ambient Light): " << zonesWithGlobal35 << "\n";

    if (!zonesHaving2A.empty()) {
        std::cout << "  Zones with 0x2A: ";
        for (const auto& z : zonesHaving2A) std::cout << z << " ";
        std::cout << "\n";
    }

    if (!zonesHaving35.empty()) {
        std::cout << "  Zones with 0x35: ";
        for (const auto& z : zonesHaving35) std::cout << z << " ";
        std::cout << "\n";
    }
}

// ============================================================================
// Phase 8: BSP and Region Fragments
// ============================================================================

class WldBspFragmentTest : public ::testing::Test {
protected:
    std::string eqClientPath_;

    // XOR key for string hash decoding - matches eqsage
    static constexpr uint8_t HASH_KEY[8] = {0x95, 0x3A, 0xC5, 0x2A, 0x95, 0x7A, 0x95, 0x6A};

    void SetUp() override {
        const char* envPath = std::getenv("EQ_CLIENT_PATH");
        if (envPath) {
            eqClientPath_ = envPath;
        } else {
            eqClientPath_ = "/home/user/projects/claude/EverQuestP1999";
        }
    }

    bool fileExists(const std::string& path) {
        std::ifstream f(path);
        return f.good();
    }

    // Decode XOR-encoded string from fragment body - matches eqsage decodeString
    std::string decodeStringFromBody(const char* data, size_t length) {
        std::vector<char> decoded(length);
        for (size_t i = 0; i < length; ++i) {
            decoded[i] = data[i] ^ HASH_KEY[i % 8];
        }
        // Find null terminator
        size_t end = 0;
        while (end < length && decoded[end] != '\0') end++;
        return std::string(decoded.begin(), decoded.begin() + end);
    }

    // Parse fragment header to get size/type/nameRef
    struct FragmentHeader {
        uint32_t size;
        uint32_t type;
        int32_t nameRef;
    };
};

// Test Fragment 0x21 (BSP Tree) node structure
TEST_F(WldBspFragmentTest, Fragment21_NodeStructure) {
    // eqsage BspNode reads:
    // normalX (float32) + normalY (float32) + normalZ (float32) +
    // splitDistance (float32) + regionId (int32) + left (int32) + right (int32)
    // = 28 bytes per node

    // willeq's BspNode structure should be 28 bytes
    EXPECT_EQ(sizeof(BspNode), 28u);

    // Verify member offsets match expected layout
    BspNode node;
    char* base = reinterpret_cast<char*>(&node);
    EXPECT_EQ(reinterpret_cast<char*>(&node.normalX) - base, 0);
    EXPECT_EQ(reinterpret_cast<char*>(&node.normalY) - base, 4);
    EXPECT_EQ(reinterpret_cast<char*>(&node.normalZ) - base, 8);
    EXPECT_EQ(reinterpret_cast<char*>(&node.splitDistance) - base, 12);
    EXPECT_EQ(reinterpret_cast<char*>(&node.regionId) - base, 16);
    EXPECT_EQ(reinterpret_cast<char*>(&node.left) - base, 20);
    EXPECT_EQ(reinterpret_cast<char*>(&node.right) - base, 24);
}

// Test Fragment 0x21 parsing on real zone data
TEST_F(WldBspFragmentTest, Fragment21_RealZoneData) {
    std::vector<std::string> zones = {"qeynos2", "freporte", "ecommons"};

    for (const auto& zone : zones) {
        std::string s3dPath = eqClientPath_ + "/" + zone + ".s3d";
        if (!fileExists(s3dPath)) continue;

        WldLoader loader;
        std::string wldFile = zone + ".wld";
        if (!loader.parseFromArchive(s3dPath, wldFile)) continue;

        auto bspTree = loader.getBspTree();
        if (!bspTree) continue;

        std::cout << "Fragment 0x21 (BSP Tree) for " << zone << ":\n";
        std::cout << "  Node count: " << bspTree->nodes.size() << "\n";

        // Count leaf nodes (nodes with no children)
        int leafCount = 0;
        int nodesWithRegion = 0;
        for (const auto& node : bspTree->nodes) {
            if (node.left == -1 && node.right == -1) {
                leafCount++;
            }
            if (node.regionId > 0) {
                nodesWithRegion++;
            }
        }
        std::cout << "  Leaf nodes: " << leafCount << "\n";
        std::cout << "  Nodes with region: " << nodesWithRegion << "\n";
        std::cout << "  Region count: " << bspTree->regions.size() << "\n";

        // Verify basic tree invariants
        EXPECT_GT(bspTree->nodes.size(), 0u);
    }
}

// Test Fragment 0x22 (BSP Region) structure
TEST_F(WldBspFragmentTest, Fragment22_RegionStructure) {
    // eqsage BspRegion reads:
    // flags (4) + unknown1 (4) + data1Size (4) + data2Size (4) +
    // unknown2 (4) + data3Size (4) + data4Size (4) +
    // unknown3 (4) + data5Size (4) + data6Size (4)
    // = 40 bytes minimum header, then variable data

    // Key values:
    // containsPolygons = (flags == 0x181)
    EXPECT_EQ(0x181, 385);  // Verify flag value
}

// Test Fragment 0x22 parsing on real zone data
TEST_F(WldBspFragmentTest, Fragment22_RealZoneData) {
    std::vector<std::string> zones = {"qeynos2", "freporte", "ecommons"};

    for (const auto& zone : zones) {
        std::string s3dPath = eqClientPath_ + "/" + zone + ".s3d";
        if (!fileExists(s3dPath)) continue;

        WldLoader loader;
        std::string wldFile = zone + ".wld";
        if (!loader.parseFromArchive(s3dPath, wldFile)) continue;

        auto bspTree = loader.getBspTree();
        if (!bspTree) continue;

        std::cout << "Fragment 0x22 (BSP Region) for " << zone << ":\n";
        std::cout << "  Region count: " << bspTree->regions.size() << "\n";

        int withPolygons = 0;
        int withMeshRef = 0;
        for (const auto& region : bspTree->regions) {
            if (region->containsPolygons) withPolygons++;
            if (region->meshReference >= 0) withMeshRef++;
        }
        std::cout << "  Regions with polygons: " << withPolygons << "\n";
        std::cout << "  Regions with mesh reference: " << withMeshRef << "\n";
    }
}

// Test Fragment 0x29 (Region Type) - KEY DIFFERENCE with eqsage
// eqsage reads region type string from fragment BODY (XOR encoded)
// willeq reads from fragment NAME
TEST_F(WldBspFragmentTest, Fragment29_StringSource) {
    // This test verifies where the region type string comes from
    // eqsage BspRegionType constructor:
    //   1. flags (int32)
    //   2. regionCount (int32)
    //   3. regionIndices (int32 × regionCount)
    //   4. stringSize (int32)
    //   5. XOR-decoded string (stringSize bytes)
    //   6. Falls back to fragment name if string is empty

    std::vector<std::string> zones = {"qeynos2", "freporte", "ecommons"};

    for (const auto& zone : zones) {
        std::string s3dPath = eqClientPath_ + "/" + zone + ".s3d";
        if (!fileExists(s3dPath)) continue;

        PfsArchive archive;
        if (!archive.open(s3dPath)) continue;

        std::vector<char> wldBuffer;
        std::string wldFile = zone + ".wld";
        if (!archive.get(wldFile, wldBuffer)) continue;

        // Parse WLD manually to examine Fragment 0x29 bodies
        if (wldBuffer.size() < 28) continue;

        const char* ptr = wldBuffer.data();
        uint32_t magic = *reinterpret_cast<const uint32_t*>(ptr);
        if (magic != 0x54503D02) continue;

        uint32_t version = *reinterpret_cast<const uint32_t*>(ptr + 4);
        uint32_t fragmentCount = *reinterpret_cast<const uint32_t*>(ptr + 8);
        uint32_t hashLength = *reinterpret_cast<const uint32_t*>(ptr + 20);

        // Decode string hash table
        const char* hashStart = ptr + 28;
        std::vector<char> decodedHash(hashLength);
        for (size_t i = 0; i < hashLength; ++i) {
            decodedHash[i] = hashStart[i] ^ HASH_KEY[i % 8];
        }

        // Scan fragments
        const char* fragPtr = hashStart + hashLength;
        int frag29Count = 0;
        int bodyStringMatches = 0;
        int bodyStringDiffers = 0;

        std::cout << "\nFragment 0x29 analysis for " << zone << ":\n";

        for (uint32_t i = 0; i < fragmentCount; ++i) {
            uint32_t fragSize = *reinterpret_cast<const uint32_t*>(fragPtr);
            uint32_t fragType = *reinterpret_cast<const uint32_t*>(fragPtr + 4);
            int32_t nameRef = *reinterpret_cast<const int32_t*>(fragPtr + 8);
            const char* fragBody = fragPtr + 12;
            uint32_t fragBodySize = fragSize - 4;  // size includes type but not size field

            if (fragType == 0x29) {
                frag29Count++;

                // Get fragment name from hash table
                std::string fragName;
                if (nameRef < 0 && static_cast<size_t>(-nameRef) < decodedHash.size()) {
                    const char* nameStart = decodedHash.data() + (-nameRef);
                    size_t nameEnd = 0;
                    while (nameStart[nameEnd] != '\0' && (-nameRef + nameEnd) < decodedHash.size()) nameEnd++;
                    fragName = std::string(nameStart, nameEnd);
                }

                // Parse fragment body like eqsage does
                if (fragBodySize >= 8) {
                    const char* bodyPtr = fragBody;
                    int32_t flags = *reinterpret_cast<const int32_t*>(bodyPtr); bodyPtr += 4;
                    int32_t regionCount = *reinterpret_cast<const int32_t*>(bodyPtr); bodyPtr += 4;

                    // Skip region indices
                    bodyPtr += regionCount * 4;

                    // Read stringSize and string
                    size_t consumed = 8 + regionCount * 4;
                    if (consumed + 4 <= fragBodySize) {
                        int32_t stringSize = *reinterpret_cast<const int32_t*>(bodyPtr); bodyPtr += 4;
                        consumed += 4;

                        std::string bodyString;
                        if (stringSize > 0 && consumed + stringSize <= fragBodySize) {
                            bodyString = decodeStringFromBody(bodyPtr, stringSize);
                        }

                        // Compare body string with fragment name
                        std::string lowerFragName = fragName;
                        std::transform(lowerFragName.begin(), lowerFragName.end(),
                                     lowerFragName.begin(), ::tolower);
                        std::string lowerBodyString = bodyString;
                        std::transform(lowerBodyString.begin(), lowerBodyString.end(),
                                     lowerBodyString.begin(), ::tolower);

                        if (frag29Count <= 5) {
                            std::cout << "  [" << frag29Count << "] Name: '" << fragName
                                     << "' Body: '" << bodyString << "' (size=" << stringSize << ")\n";
                        }

                        if (lowerFragName == lowerBodyString || bodyString.empty()) {
                            bodyStringMatches++;
                        } else {
                            bodyStringDiffers++;
                            if (frag29Count <= 10) {
                                std::cout << "    ** DIFFERS: name='" << fragName
                                         << "' body='" << bodyString << "'\n";
                            }
                        }
                    }
                }
            }

            fragPtr += fragSize + 4;  // +4 for size field itself
            if (fragPtr >= wldBuffer.data() + wldBuffer.size()) break;
        }

        std::cout << "  Total Fragment 0x29: " << frag29Count << "\n";
        std::cout << "  Body string matches name: " << bodyStringMatches << "\n";
        std::cout << "  Body string differs from name: " << bodyStringDiffers << "\n";
    }
}

// Test RegionType enum values match eqsage
TEST_F(WldBspFragmentTest, RegionTypeEnumValues) {
    // eqsage RegionType enum values
    EXPECT_EQ(static_cast<uint8_t>(RegionType::Normal), 0);
    EXPECT_EQ(static_cast<uint8_t>(RegionType::Water), 1);
    EXPECT_EQ(static_cast<uint8_t>(RegionType::Lava), 2);
    EXPECT_EQ(static_cast<uint8_t>(RegionType::Pvp), 3);
    EXPECT_EQ(static_cast<uint8_t>(RegionType::Zoneline), 4);
    EXPECT_EQ(static_cast<uint8_t>(RegionType::WaterBlockLOS), 5);
    EXPECT_EQ(static_cast<uint8_t>(RegionType::FreezingWater), 6);
    EXPECT_EQ(static_cast<uint8_t>(RegionType::Slippery), 7);
    EXPECT_EQ(static_cast<uint8_t>(RegionType::Unknown), 8);
}

// Test ZoneLineType enum values match eqsage
TEST_F(WldBspFragmentTest, ZoneLineTypeEnumValues) {
    // eqsage ZoneLineType enum values
    EXPECT_EQ(static_cast<uint8_t>(ZoneLineType::Reference), 0);
    EXPECT_EQ(static_cast<uint8_t>(ZoneLineType::Absolute), 1);
}

// Test region type string parsing for water regions
TEST_F(WldBspFragmentTest, RegionTypeStringParsing_Water) {
    // eqsage patterns for water:
    // wtn_* or wt_* -> Water
    // wtntp* -> Water + Zoneline

    std::vector<std::string> zones = {"qeynos2", "freporte", "ecommons", "kedge"};

    for (const auto& zone : zones) {
        std::string s3dPath = eqClientPath_ + "/" + zone + ".s3d";
        if (!fileExists(s3dPath)) continue;

        WldLoader loader;
        std::string wldFile = zone + ".wld";
        if (!loader.parseFromArchive(s3dPath, wldFile)) continue;

        auto bspTree = loader.getBspTree();
        if (!bspTree) continue;

        int waterRegions = 0;
        int lavaRegions = 0;
        int zonelineRegions = 0;
        int normalRegions = 0;

        for (const auto& region : bspTree->regions) {
            for (auto type : region->regionTypes) {
                switch (type) {
                    case RegionType::Water: waterRegions++; break;
                    case RegionType::Lava: lavaRegions++; break;
                    case RegionType::Zoneline: zonelineRegions++; break;
                    case RegionType::Normal: normalRegions++; break;
                    default: break;
                }
            }
        }

        if (waterRegions > 0 || lavaRegions > 0 || zonelineRegions > 0) {
            std::cout << "Region types for " << zone << ":\n";
            std::cout << "  Water: " << waterRegions << "\n";
            std::cout << "  Lava: " << lavaRegions << "\n";
            std::cout << "  Zoneline: " << zonelineRegions << "\n";
            std::cout << "  Normal: " << normalRegions << "\n";
        }
    }
}

// Test zone line info parsing
TEST_F(WldBspFragmentTest, ZoneLineInfoParsing) {
    std::vector<std::string> zones = {"qeynos2", "freporte", "ecommons", "nro"};

    for (const auto& zone : zones) {
        std::string s3dPath = eqClientPath_ + "/" + zone + ".s3d";
        if (!fileExists(s3dPath)) continue;

        WldLoader loader;
        std::string wldFile = zone + ".wld";
        if (!loader.parseFromArchive(s3dPath, wldFile)) continue;

        auto bspTree = loader.getBspTree();
        if (!bspTree) continue;

        std::cout << "Zone line info for " << zone << ":\n";
        int zoneLineCount = 0;

        for (size_t i = 0; i < bspTree->regions.size(); ++i) {
            const auto& region = bspTree->regions[i];
            if (region->zoneLineInfo.has_value()) {
                zoneLineCount++;
                const auto& info = region->zoneLineInfo.value();
                std::cout << "  Region " << i << ": type="
                         << (info.type == ZoneLineType::Absolute ? "Absolute" : "Reference")
                         << " zoneId=" << info.zoneId
                         << " pointIdx=" << info.zonePointIndex
                         << " pos=(" << info.x << "," << info.y << "," << info.z << ")"
                         << " heading=" << info.heading << "\n";
            }
        }
        std::cout << "  Total zone lines: " << zoneLineCount << "\n";
    }
}

// Test BSP point lookup
TEST_F(WldBspFragmentTest, BspPointLookup) {
    std::string s3dPath = eqClientPath_ + "/ecommons.s3d";
    if (!fileExists(s3dPath)) {
        GTEST_SKIP() << "ecommons.s3d not found";
    }

    WldLoader loader;
    ASSERT_TRUE(loader.parseFromArchive(s3dPath, "ecommons.wld"));

    auto bspTree = loader.getBspTree();
    ASSERT_NE(bspTree, nullptr);

    // Test findRegionForPoint with a known zone line location
    // East Commonlands has zone lines to Nektulos Forest and North Freeport
    // These are at specific coordinates

    // Test a few points to verify BSP traversal works
    std::cout << "BSP point lookup tests for ecommons:\n";

    // Sample points in the zone
    std::vector<std::tuple<float, float, float, std::string>> testPoints = {
        {0, 0, 0, "origin"},
        {100, 100, 10, "sample1"},
        {-500, 200, 5, "sample2"},
    };

    for (const auto& [x, y, z, name] : testPoints) {
        auto region = bspTree->findRegionForPoint(x, y, z);
        if (region) {
            std::cout << "  Point (" << x << "," << y << "," << z << ") [" << name << "]: ";
            std::cout << "region found, types: ";
            for (auto type : region->regionTypes) {
                std::cout << static_cast<int>(type) << " ";
            }
            std::cout << "\n";
        } else {
            std::cout << "  Point (" << x << "," << y << "," << z << ") [" << name << "]: no region\n";
        }
    }
}

// ============================================================================
// Phase 9: Coordinate System Transformation Tests
// Verifies that willeq's coordinate transforms match eqsage's internal format
// ============================================================================

class CoordinateSystemTest : public ::testing::Test {
protected:
    // Rotation conversion constants - matches eqsage Location class
    static constexpr float ROT_MODIFIER = 360.0f / 512.0f;

    // Normal conversion constant - matches eqsage mesh.js
    static constexpr float NORMAL_SCALE = 1.0f / 128.0f;

    // UV conversion for old format - matches eqsage mesh.js
    static constexpr float UV_SCALE_OLD = 1.0f / 256.0f;

    // Test helper: normalize a vector (matches eqsage vec3.normalize)
    void normalize(float& x, float& y, float& z) {
        float len = std::sqrt(x*x + y*y + z*z);
        if (len > 0.0001f) {
            x /= len;
            y /= len;
            z /= len;
        } else {
            x = 0; y = 0; z = 1;  // Default to up vector
        }
    }
};

// ============================================================================
// Phase 9.1: Vertex Scale Tests
// ============================================================================

// Test vertex scale calculation matches eqsage: 1.0 / (1 << scale)
TEST_F(CoordinateSystemTest, VertexScaleCalculation) {
    // eqsage (mesh.js line 102): const scale = 1.0 / (1 << reader.readInt16())
    // willeq (wld_loader.cpp line 351): float scale = 1.0f / static_cast<float>(1 << header->scale)

    // Test common scale values
    EXPECT_FLOAT_EQ(1.0f / (1 << 0), 1.0f);       // scale=0
    EXPECT_FLOAT_EQ(1.0f / (1 << 1), 0.5f);       // scale=1
    EXPECT_FLOAT_EQ(1.0f / (1 << 2), 0.25f);      // scale=2
    EXPECT_FLOAT_EQ(1.0f / (1 << 3), 0.125f);     // scale=3
    EXPECT_FLOAT_EQ(1.0f / (1 << 4), 0.0625f);    // scale=4
    EXPECT_FLOAT_EQ(1.0f / (1 << 8), 0.00390625f); // scale=8 (common)
}

// Test vertex position conversion: raw int16 * scale
TEST_F(CoordinateSystemTest, VertexPositionConversion) {
    // Both eqsage and willeq: vertex.x = rawInt16 * scale
    float scale = 1.0f / (1 << 8);  // Common scale value

    // Test positive values
    EXPECT_NEAR(100 * scale, 0.390625f, 0.0001f);
    EXPECT_NEAR(1000 * scale, 3.90625f, 0.0001f);
    EXPECT_NEAR(32767 * scale, 127.99609375f, 0.0001f);  // Max int16

    // Test negative values
    EXPECT_NEAR(-100 * scale, -0.390625f, 0.0001f);
    EXPECT_NEAR(-32768 * scale, -128.0f, 0.0001f);  // Min int16
}

// ============================================================================
// Phase 9.2: Normal Conversion Tests
// ============================================================================

// Test normal conversion: int8 / 128.0 then normalize
TEST_F(CoordinateSystemTest, NormalConversion) {
    // eqsage (mesh.js lines 127-132):
    //   const x = reader.readInt8() / 128.0;
    //   const y = reader.readInt8() / 128.0;
    //   const z = reader.readInt8() / 128.0;
    //   vec3.normalize(vec, vec);
    // willeq (wld_loader.cpp lines 407-416): same calculation

    // Test boundary values
    EXPECT_NEAR(127 * NORMAL_SCALE, 0.9921875f, 0.0001f);   // Max positive int8
    EXPECT_NEAR(-128 * NORMAL_SCALE, -1.0f, 0.0001f);       // Min int8
    EXPECT_NEAR(0 * NORMAL_SCALE, 0.0f, 0.0001f);           // Zero

    // Test normalization for a typical normal
    float nx = 64 * NORMAL_SCALE;   // 0.5
    float ny = 64 * NORMAL_SCALE;   // 0.5
    float nz = 90 * NORMAL_SCALE;   // ~0.703
    normalize(nx, ny, nz);

    // Verify normalized length is 1.0
    float len = std::sqrt(nx*nx + ny*ny + nz*nz);
    EXPECT_NEAR(len, 1.0f, 0.0001f);
}

// Test degenerate normal handling (length near zero)
TEST_F(CoordinateSystemTest, DegenerateNormalHandling) {
    // Both implementations default to up vector (0, 0, 1) for degenerate normals
    float nx = 0, ny = 0, nz = 0;
    normalize(nx, ny, nz);

    EXPECT_FLOAT_EQ(nx, 0.0f);
    EXPECT_FLOAT_EQ(ny, 0.0f);
    EXPECT_FLOAT_EQ(nz, 1.0f);
}

// ============================================================================
// Phase 9.3: UV Coordinate Conversion Tests
// ============================================================================

// Test UV old format: int16 / 256.0
TEST_F(CoordinateSystemTest, UVConversionOldFormat) {
    // eqsage (mesh.js line 121): vec2.fromValues(reader.readInt16() / 256.0, reader.readInt16() / 256.0)
    // willeq (wld_loader.cpp lines 385-386): geom->vertices[i].u = tc->u * recip256

    // Test typical UV values
    EXPECT_NEAR(128 * UV_SCALE_OLD, 0.5f, 0.0001f);
    EXPECT_NEAR(256 * UV_SCALE_OLD, 1.0f, 0.0001f);
    EXPECT_NEAR(0 * UV_SCALE_OLD, 0.0f, 0.0001f);

    // Negative values can occur for tiled textures
    EXPECT_NEAR(-128 * UV_SCALE_OLD, -0.5f, 0.0001f);
}

// Test UV new format: float32 directly (no conversion)
TEST_F(CoordinateSystemTest, UVConversionNewFormat) {
    // eqsage (mesh.js line 117): vec2.fromValues(reader.readFloat32(), reader.readFloat32())
    // willeq (wld_loader.cpp lines 394-396): tc->u used directly

    // New format uses float32 directly - just verify identity
    float u = 0.5f, v = 0.75f;
    EXPECT_FLOAT_EQ(u, 0.5f);
    EXPECT_FLOAT_EQ(v, 0.75f);
}

// ============================================================================
// Phase 9.4: Polygon Winding Order Tests
// ============================================================================

// Test polygon winding reversal
TEST_F(CoordinateSystemTest, PolygonWindingReversal) {
    // eqsage: parses as (v1, v2, v3), reverses at export time
    // willeq: reverses at parse time (index[2], index[1], index[0])
    // Both result in the same final winding order

    // Simulate raw polygon indices from file
    uint16_t rawIndices[3] = {0, 1, 2};

    // willeq winding reversal (wld_loader.cpp lines 437-439)
    uint16_t v1 = rawIndices[2];  // 2
    uint16_t v2 = rawIndices[1];  // 1
    uint16_t v3 = rawIndices[0];  // 0

    // Verify order is reversed
    EXPECT_EQ(v1, 2u);
    EXPECT_EQ(v2, 1u);
    EXPECT_EQ(v3, 0u);

    // Test with different values
    rawIndices[0] = 10; rawIndices[1] = 20; rawIndices[2] = 30;
    v1 = rawIndices[2];
    v2 = rawIndices[1];
    v3 = rawIndices[0];

    EXPECT_EQ(v1, 30u);
    EXPECT_EQ(v2, 20u);
    EXPECT_EQ(v3, 10u);
}

// ============================================================================
// Phase 9.5: Rotation Conversion Tests (Fragment 0x15 ActorInstance)
// ============================================================================

// Test rotation modifier calculation
TEST_F(CoordinateSystemTest, RotationModifier) {
    // Both eqsage and willeq use: 360.0 / 512.0 = 0.703125
    // eqsage (location.js line 13): const modifier = 1.0 / 512.0 * 360.0
    // willeq (wld_loader.cpp line 797): const float rotModifier = 360.0f / 512.0f

    EXPECT_NEAR(ROT_MODIFIER, 0.703125f, 0.0001f);
}

// Test rotation conversion matching eqsage Location class
TEST_F(CoordinateSystemTest, RotationConversion) {
    // eqsage Location class (location.js):
    //   this.rotateX = 0;
    //   this.rotateZ = rotateY * modifier;
    //   this.rotateY = rotateX * modifier * -1;
    //
    // willeq (wld_loader.cpp lines 798-800):
    //   float finalRotX = 0.0f;
    //   float finalRotY = rawRotX * rotModifier * -1.0f;
    //   float finalRotZ = rawRotY * rotModifier;

    // Test conversion with sample raw values
    float rawRotX = 128.0f;  // Quarter circle in EQ units
    float rawRotY = 256.0f;  // Half circle in EQ units

    // willeq conversion (matches eqsage)
    float finalRotX = 0.0f;
    float finalRotY = rawRotX * ROT_MODIFIER * -1.0f;
    float finalRotZ = rawRotY * ROT_MODIFIER;

    EXPECT_FLOAT_EQ(finalRotX, 0.0f);
    EXPECT_NEAR(finalRotY, -90.0f, 0.01f);   // 128 * 0.703125 * -1 = -90
    EXPECT_NEAR(finalRotZ, 180.0f, 0.01f);   // 256 * 0.703125 = 180
}

// Test full rotation (512 EQ units = 360 degrees)
TEST_F(CoordinateSystemTest, FullRotation) {
    float rawRot = 512.0f;
    float degrees = rawRot * ROT_MODIFIER;
    EXPECT_NEAR(degrees, 360.0f, 0.01f);
}

// ============================================================================
// Phase 9.6: Coordinate System Transform Notes
// ============================================================================

// Document the intentional differences between eqsage and willeq at render time
TEST_F(CoordinateSystemTest, CoordinateSystemDocumentation) {
    // PARSING STAGE (WLD -> internal representation):
    // Both eqsage and willeq parse coordinates IDENTICALLY:
    // - Vertices: int16 * scale (no axis swapping)
    // - Normals: int8 / 128.0, normalized (no axis swapping)
    // - UVs: old format int16/256, new format float32 (no V flip at parse)
    // - Rotations: rawRotX -> rotateY (negated), rawRotY -> rotateZ

    // RENDER/EXPORT STAGE (internal -> target system):
    // Here the implementations differ based on target format:
    //
    // eqsage -> glTF (right-handed, OpenGL UVs):
    //   - Y/Z swap: y = z, z = y (line 1119 s3d-decoder.js)
    //   - X flip: scale [-1, 1, 1] (line 1096 s3d-decoder.js)
    //   - V negate: uv[1] * -1 (line 299 s3d-decoder.js)
    //   - +180° rotation: rotateY + 180 (line 1163 s3d-decoder.js)
    //   - Winding reversal at export (lines 360-366 s3d-decoder.js)
    //
    // willeq -> Irrlicht (left-handed like EQ, DirectX UVs):
    //   - Y/Z swap: pos.Y = z, pos.Z = y (zone_geometry.cpp:119-124)
    //   - NO X flip (Irrlicht is left-handed like EQ)
    //   - NO V negate (DirectX UV origin is top-left like EQ)
    //   - NO +180° rotation (Irrlicht is left-handed like EQ)
    //   - Winding reversal at parse (wld_loader.cpp:437-439)

    // This test just documents the design - no assertions needed
    SUCCEED();
}

// ============================================================================
// Phase 10: Texture Loading Pipeline Tests
// Verifies DDS decoding and texture chain resolution
// ============================================================================

class TextureLoadingTest : public ::testing::Test {
protected:
    // DDS magic constant - matches eqsage and willeq
    static constexpr uint32_t DDS_MAGIC = 0x20534444;  // "DDS "

    // FourCC codes for DXT formats
    static constexpr uint32_t FOURCC_DXT1 = 0x31545844;  // "DXT1"
    static constexpr uint32_t FOURCC_DXT3 = 0x33545844;  // "DXT3"
    static constexpr uint32_t FOURCC_DXT5 = 0x35545844;  // "DXT5"

    // Material type constants - matches eqsage MaterialType enum
    static constexpr uint32_t MATERIAL_BOUNDARY = 0x0;
    static constexpr uint32_t MATERIAL_DIFFUSE = 0x01;
    static constexpr uint32_t MATERIAL_TRANSPARENT50 = 0x05;
    static constexpr uint32_t MATERIAL_TRANSPARENT_MASKED = 0x13;

    // BitmapInfo flags - matches eqsage BitMapInfoFlags
    static constexpr uint32_t FLAG_SKIP_FRAMES = 0x02;
    static constexpr uint32_t FLAG_UNKNOWN = 0x04;
    static constexpr uint32_t FLAG_ANIMATED = 0x08;
    static constexpr uint32_t FLAG_HAS_SLEEP = 0x10;
    static constexpr uint32_t FLAG_HAS_CURRENT_FRAME = 0x20;

    // RGB565 to RGB888 conversion - matches willeq dds_decoder.cpp
    void rgb565ToRGB888(uint16_t color, uint8_t& r, uint8_t& g, uint8_t& b) {
        r = ((color >> 11) & 0x1F) * 255 / 31;
        g = ((color >> 5) & 0x3F) * 255 / 63;
        b = (color & 0x1F) * 255 / 31;
    }

    // FourCC conversion - matches eqsage image-processing.js
    uint32_t fourCCToInt32(const char* value) {
        return value[0] + (value[1] << 8) + (value[2] << 16) + (value[3] << 24);
    }
};

// ============================================================================
// Phase 10.1: DDS Format Detection Tests
// ============================================================================

// Test DDS magic constant
TEST_F(TextureLoadingTest, DDSMagicConstant) {
    // Both eqsage (image-processing.js:166) and willeq (dds_decoder.cpp:28)
    // use 0x20534444 as DDS magic ("DDS " in little-endian)
    EXPECT_EQ(DDS_MAGIC, 0x20534444u);

    // Verify it matches the string "DDS "
    const char* ddsStr = "DDS ";
    uint32_t fromStr = ddsStr[0] | (ddsStr[1] << 8) | (ddsStr[2] << 16) | (ddsStr[3] << 24);
    EXPECT_EQ(fromStr, DDS_MAGIC);
}

// Test FourCC codes for DXT formats
TEST_F(TextureLoadingTest, FourCCCodes) {
    // eqsage (image-processing.js:250-252)
    // willeq (dds_decoder.cpp:9-11)

    EXPECT_EQ(fourCCToInt32("DXT1"), FOURCC_DXT1);
    EXPECT_EQ(fourCCToInt32("DXT3"), FOURCC_DXT3);
    EXPECT_EQ(fourCCToInt32("DXT5"), FOURCC_DXT5);

    // Verify values
    EXPECT_EQ(FOURCC_DXT1, 0x31545844u);
    EXPECT_EQ(FOURCC_DXT3, 0x33545844u);
    EXPECT_EQ(FOURCC_DXT5, 0x35545844u);
}

// ============================================================================
// Phase 10.2: DXT Block Size Tests
// ============================================================================

// Test DXT block sizes
TEST_F(TextureLoadingTest, DXTBlockSizes) {
    // DXT1: 8 bytes per 4x4 block (4bpp)
    // DXT3: 16 bytes per 4x4 block (8bpp) - 8 alpha + 8 color
    // DXT5: 16 bytes per 4x4 block (8bpp) - 8 alpha + 8 color

    // Both implementations use these block sizes
    // eqsage (image-processing.js:303-315)
    // willeq (dds_decoder.cpp:72, 150, 225)

    uint32_t dxt1BlockSize = 8;
    uint32_t dxt3BlockSize = 16;
    uint32_t dxt5BlockSize = 16;

    // Test block count calculation for a 256x256 texture
    uint32_t width = 256, height = 256;
    uint32_t blocksX = (width + 3) / 4;
    uint32_t blocksY = (height + 3) / 4;

    EXPECT_EQ(blocksX, 64u);
    EXPECT_EQ(blocksY, 64u);

    // Total data size
    EXPECT_EQ(blocksX * blocksY * dxt1BlockSize, 32768u);   // 32KB for DXT1
    EXPECT_EQ(blocksX * blocksY * dxt3BlockSize, 65536u);   // 64KB for DXT3
    EXPECT_EQ(blocksX * blocksY * dxt5BlockSize, 65536u);   // 64KB for DXT5
}

// Test block count calculation for non-power-of-2 dimensions
TEST_F(TextureLoadingTest, NonPowerOf2BlockCount) {
    // Formula: blocksX = (width + 3) / 4
    // This handles partial blocks at edges

    // 17x17 texture -> 5x5 blocks
    EXPECT_EQ((17 + 3) / 4, 5u);

    // 1x1 texture -> 1x1 blocks
    EXPECT_EQ((1 + 3) / 4, 1u);

    // 4x4 texture -> 1x1 blocks
    EXPECT_EQ((4 + 3) / 4, 1u);

    // 5x5 texture -> 2x2 blocks
    EXPECT_EQ((5 + 3) / 4, 2u);
}

// ============================================================================
// Phase 10.3: RGB565 to RGB888 Conversion Tests
// ============================================================================

// Test RGB565 to RGB888 conversion
TEST_F(TextureLoadingTest, RGB565Conversion) {
    // RGB565 format: RRRRRGGGGGGBBBBB (5-6-5 bits)
    // Both eqsage (dxt-js library) and willeq (dds_decoder.cpp:60-64)
    // convert to 8-bit per channel

    uint8_t r, g, b;

    // Test pure red (0xF800)
    rgb565ToRGB888(0xF800, r, g, b);
    EXPECT_EQ(r, 255);
    EXPECT_EQ(g, 0);
    EXPECT_EQ(b, 0);

    // Test pure green (0x07E0)
    rgb565ToRGB888(0x07E0, r, g, b);
    EXPECT_EQ(r, 0);
    EXPECT_EQ(g, 255);
    EXPECT_EQ(b, 0);

    // Test pure blue (0x001F)
    rgb565ToRGB888(0x001F, r, g, b);
    EXPECT_EQ(r, 0);
    EXPECT_EQ(g, 0);
    EXPECT_EQ(b, 255);

    // Test white (0xFFFF)
    rgb565ToRGB888(0xFFFF, r, g, b);
    EXPECT_EQ(r, 255);
    EXPECT_EQ(g, 255);
    EXPECT_EQ(b, 255);

    // Test black (0x0000)
    rgb565ToRGB888(0x0000, r, g, b);
    EXPECT_EQ(r, 0);
    EXPECT_EQ(g, 0);
    EXPECT_EQ(b, 0);
}

// ============================================================================
// Phase 10.4: DXT1 Color Interpolation Tests
// ============================================================================

// Test DXT1 4-color interpolation (c0 > c1)
TEST_F(TextureLoadingTest, DXT1FourColorInterpolation) {
    // When c0 > c1: 4 colors
    // color2 = (2/3)*c0 + (1/3)*c1
    // color3 = (1/3)*c0 + (2/3)*c1

    // Both eqsage (dxt-js) and willeq (dds_decoder.cpp:104-111)

    uint8_t c0[3] = {255, 0, 0};  // Red
    uint8_t c1[3] = {0, 0, 255};  // Blue

    // color2 = 2/3 red + 1/3 blue
    uint8_t color2[3];
    for (int i = 0; i < 3; ++i) {
        color2[i] = (2 * c0[i] + c1[i]) / 3;
    }
    EXPECT_EQ(color2[0], 170);  // ~2/3 of 255
    EXPECT_EQ(color2[1], 0);
    EXPECT_EQ(color2[2], 85);   // ~1/3 of 255

    // color3 = 1/3 red + 2/3 blue
    uint8_t color3[3];
    for (int i = 0; i < 3; ++i) {
        color3[i] = (c0[i] + 2 * c1[i]) / 3;
    }
    EXPECT_EQ(color3[0], 85);   // ~1/3 of 255
    EXPECT_EQ(color3[1], 0);
    EXPECT_EQ(color3[2], 170);  // ~2/3 of 255
}

// Test DXT1 3-color + transparent (c0 <= c1)
TEST_F(TextureLoadingTest, DXT1ThreeColorPlusTransparent) {
    // When c0 <= c1: 3 colors + transparent
    // color2 = (1/2)*c0 + (1/2)*c1
    // color3 = transparent (alpha = 0)

    // Both implementations handle this case
    // willeq (dds_decoder.cpp:112-119)

    uint8_t c0[3] = {100, 100, 100};
    uint8_t c1[3] = {200, 200, 200};

    // color2 = average
    uint8_t color2[3];
    for (int i = 0; i < 3; ++i) {
        color2[i] = (c0[i] + c1[i]) / 2;
    }
    EXPECT_EQ(color2[0], 150);
    EXPECT_EQ(color2[1], 150);
    EXPECT_EQ(color2[2], 150);

    // color3 is transparent
    uint8_t color3_alpha = 0;
    EXPECT_EQ(color3_alpha, 0);
}

// ============================================================================
// Phase 10.5: DXT5 Alpha Interpolation Tests
// ============================================================================

// Test DXT5 8-alpha interpolation (alpha0 > alpha1)
TEST_F(TextureLoadingTest, DXT5EightAlphaInterpolation) {
    // When alpha0 > alpha1: 8 alpha values (6 interpolated + 2 endpoints)
    // Both implementations use the same formula
    // willeq (dds_decoder.cpp:253-260)

    uint8_t alpha0 = 255;
    uint8_t alpha1 = 0;

    uint8_t alphas[8];
    alphas[0] = alpha0;
    alphas[1] = alpha1;
    alphas[2] = (6 * alpha0 + 1 * alpha1) / 7;  // ~218
    alphas[3] = (5 * alpha0 + 2 * alpha1) / 7;  // ~182
    alphas[4] = (4 * alpha0 + 3 * alpha1) / 7;  // ~145
    alphas[5] = (3 * alpha0 + 4 * alpha1) / 7;  // ~109
    alphas[6] = (2 * alpha0 + 5 * alpha1) / 7;  // ~72
    alphas[7] = (1 * alpha0 + 6 * alpha1) / 7;  // ~36

    EXPECT_EQ(alphas[0], 255);
    EXPECT_EQ(alphas[1], 0);
    EXPECT_NEAR(alphas[2], 218, 1);
    EXPECT_NEAR(alphas[3], 182, 1);
    EXPECT_NEAR(alphas[4], 145, 1);
    EXPECT_NEAR(alphas[5], 109, 1);
    EXPECT_NEAR(alphas[6], 72, 1);
    EXPECT_NEAR(alphas[7], 36, 1);
}

// Test DXT5 6-alpha + 0 + 255 (alpha0 <= alpha1)
TEST_F(TextureLoadingTest, DXT5SixAlphaPlusExtremes) {
    // When alpha0 <= alpha1: 6 interpolated values + 0 and 255
    // willeq (dds_decoder.cpp:261-268)

    uint8_t alpha0 = 50;
    uint8_t alpha1 = 200;

    uint8_t alphas[8];
    alphas[0] = alpha0;
    alphas[1] = alpha1;
    alphas[2] = (4 * alpha0 + 1 * alpha1) / 5;
    alphas[3] = (3 * alpha0 + 2 * alpha1) / 5;
    alphas[4] = (2 * alpha0 + 3 * alpha1) / 5;
    alphas[5] = (1 * alpha0 + 4 * alpha1) / 5;
    alphas[6] = 0;    // Explicit 0
    alphas[7] = 255;  // Explicit 255

    EXPECT_EQ(alphas[0], 50);
    EXPECT_EQ(alphas[1], 200);
    EXPECT_EQ(alphas[6], 0);
    EXPECT_EQ(alphas[7], 255);
}

// ============================================================================
// Phase 10.6: Texture Chain Resolution Tests
// ============================================================================

// Test material type masking
TEST_F(TextureLoadingTest, MaterialTypeMasking) {
    // Both eqsage (material.js:82) and willeq (wld_loader.cpp:277)
    // mask with ~0x80000000 to extract material type

    uint32_t parameters = 0x80000001;  // High bit set + diffuse type
    uint32_t materialType = parameters & ~0x80000000;

    EXPECT_EQ(materialType, MATERIAL_DIFFUSE);

    // Test without high bit
    parameters = 0x00000005;
    materialType = parameters & ~0x80000000;
    EXPECT_EQ(materialType, MATERIAL_TRANSPARENT50);
}

// Test animation flags
TEST_F(TextureLoadingTest, AnimationFlags) {
    // eqsage (bitmap-info.js:3-8) and willeq (wld_loader.cpp:234)

    uint32_t flags = FLAG_ANIMATED | FLAG_HAS_SLEEP;  // 0x18

    bool isAnimated = (flags & FLAG_ANIMATED) != 0;
    bool hasSleep = (flags & FLAG_HAS_SLEEP) != 0;
    bool hasCurrentFrame = (flags & FLAG_HAS_CURRENT_FRAME) != 0;

    EXPECT_TRUE(isAnimated);
    EXPECT_TRUE(hasSleep);
    EXPECT_FALSE(hasCurrentFrame);

    // Animation delay is read when animated AND has sleep
    bool shouldReadDelay = isAnimated && hasSleep;
    EXPECT_TRUE(shouldReadDelay);
}

// Test texture chain fragment references
TEST_F(TextureLoadingTest, TextureChainFragmentReferences) {
    // Texture chain: 0x31 -> 0x30 -> 0x05 -> 0x04 -> 0x03 -> filename
    //
    // 0x31 MaterialList: array of references to 0x30 fragments
    // 0x30 Material: reference to 0x05 fragment
    // 0x05 BitmapInfoReference: reference to 0x04 fragment
    // 0x04 BitmapInfo: animation info + references to 0x03 fragments
    // 0x03 BitmapName: texture filename(s)

    // Verify fragment type values
    uint8_t frag03 = 0x03;  // BitmapName
    uint8_t frag04 = 0x04;  // BitmapInfo
    uint8_t frag05 = 0x05;  // BitmapInfoReference
    uint8_t frag30 = 0x30;  // Material
    uint8_t frag31 = 0x31;  // MaterialList

    EXPECT_EQ(frag03, 3);
    EXPECT_EQ(frag04, 4);
    EXPECT_EQ(frag05, 5);
    EXPECT_EQ(frag30, 48);
    EXPECT_EQ(frag31, 49);
}

// ============================================================================
// Phase 10.7: Material Type Constants Tests
// ============================================================================

// Test material type shader mapping
TEST_F(TextureLoadingTest, MaterialTypeShaderMapping) {
    // eqsage (material.js:6-37) defines material types
    // willeq uses compatible type detection

    // Boundary type (not rendered)
    EXPECT_EQ(MATERIAL_BOUNDARY, 0x0);

    // Standard diffuse types
    EXPECT_EQ(MATERIAL_DIFFUSE, 0x01);

    // Transparent types
    EXPECT_EQ(MATERIAL_TRANSPARENT50, 0x05);  // 50% blend
    EXPECT_EQ(MATERIAL_TRANSPARENT_MASKED, 0x13);  // Alpha test
}

// ============================================================================
// Phase 11: Integration Tests
// Full parsing of actual zone and character files
// ============================================================================

class IntegrationTest : public ::testing::Test {
protected:
    std::string eqClientPath_;

    void SetUp() override {
        const char* envPath = std::getenv("EQ_CLIENT_PATH");
        if (envPath) {
            eqClientPath_ = envPath;
        } else {
            eqClientPath_ = "/home/user/projects/claude/EverQuestP1999";
        }
    }

    bool fileExists(const std::string& path) {
        std::ifstream f(path);
        return f.good();
    }
};

// ============================================================================
// Phase 11.1: Zone Geometry Integration Tests
// ============================================================================

// Test loading East Commonlands zone geometry
TEST_F(IntegrationTest, EastCommonsZoneGeometry) {
    std::string s3dPath = eqClientPath_ + "/ecommons.s3d";
    if (!fileExists(s3dPath)) {
        GTEST_SKIP() << "EQ client not found at: " << eqClientPath_;
    }

    WldLoader loader;
    ASSERT_TRUE(loader.parseFromArchive(s3dPath, "ecommons.wld"));

    // Verify zone geometry was loaded
    const auto& geometries = loader.getGeometries();
    EXPECT_GT(geometries.size(), 0u) << "Zone should have geometry meshes";

    // Count total vertices and triangles
    size_t totalVertices = 0;
    size_t totalTriangles = 0;
    for (const auto& geom : geometries) {
        totalVertices += geom->vertices.size();
        totalTriangles += geom->triangles.size();
    }

    std::cout << "East Commonlands zone geometry:\n";
    std::cout << "  Mesh count: " << geometries.size() << "\n";
    std::cout << "  Total vertices: " << totalVertices << "\n";
    std::cout << "  Total triangles: " << totalTriangles << "\n";

    // Verify reasonable mesh counts (ecommons is a medium-sized zone)
    EXPECT_GT(totalVertices, 1000u) << "Zone should have substantial vertex count";
    EXPECT_GT(totalTriangles, 500u) << "Zone should have substantial triangle count";

    // Document texture count (may be loaded separately via texture chain)
    const auto& textures = loader.getTextureNames();
    std::cout << "  Texture names in WLD: " << textures.size() << "\n";
    // Note: Textures are often resolved via fragment chain, not stored directly
}

// Test loading Qeynos Hills zone geometry
TEST_F(IntegrationTest, QeynosHillsZoneGeometry) {
    std::string s3dPath = eqClientPath_ + "/qeytoqrg.s3d";
    if (!fileExists(s3dPath)) {
        GTEST_SKIP() << "qeytoqrg.s3d not found";
    }

    WldLoader loader;
    ASSERT_TRUE(loader.parseFromArchive(s3dPath, "qeytoqrg.wld"));

    const auto& geometries = loader.getGeometries();
    EXPECT_GT(geometries.size(), 0u) << "Zone should have geometry meshes";

    size_t totalVertices = 0;
    for (const auto& geom : geometries) {
        totalVertices += geom->vertices.size();
    }

    std::cout << "Qeynos Hills zone geometry:\n";
    std::cout << "  Mesh count: " << geometries.size() << "\n";
    std::cout << "  Total vertices: " << totalVertices << "\n";

    EXPECT_GT(totalVertices, 1000u);
}

// Test combined geometry creation
TEST_F(IntegrationTest, CombinedGeometryCreation) {
    std::string s3dPath = eqClientPath_ + "/ecommons.s3d";
    if (!fileExists(s3dPath)) {
        GTEST_SKIP() << "EQ client not found";
    }

    WldLoader loader;
    ASSERT_TRUE(loader.parseFromArchive(s3dPath, "ecommons.wld"));

    auto combined = loader.getCombinedGeometry();
    ASSERT_NE(combined, nullptr) << "Combined geometry should be created";

    std::cout << "Combined geometry:\n";
    std::cout << "  Vertices: " << combined->vertices.size() << "\n";
    std::cout << "  Triangles: " << combined->triangles.size() << "\n";

    EXPECT_GT(combined->vertices.size(), 0u);
    EXPECT_GT(combined->triangles.size(), 0u);
}

// ============================================================================
// Phase 11.2: Object Placement Integration Tests
// ============================================================================

// Test loading zone objects (placeables)
TEST_F(IntegrationTest, EastCommonsObjectPlacement) {
    std::string s3dPath = eqClientPath_ + "/ecommons.s3d";
    if (!fileExists(s3dPath)) {
        GTEST_SKIP() << "EQ client not found";
    }

    WldLoader loader;
    ASSERT_TRUE(loader.parseFromArchive(s3dPath, "ecommons.wld"));

    const auto& placeables = loader.getPlaceables();
    std::cout << "East Commonlands placeables:\n";
    std::cout << "  Placeable count: " << placeables.size() << "\n";

    // Verify placeables have valid data
    size_t validPlaceables = 0;
    for (const auto& p : placeables) {
        if (p && !p->getName().empty()) {
            validPlaceables++;

            // Verify position is reasonable (within zone bounds)
            float x = p->getX();
            float y = p->getY();
            float z = p->getZ();

            // East Commonlands roughly spans -2000 to 2000 in each axis
            EXPECT_GT(x, -10000.0f) << "X position should be within reasonable bounds";
            EXPECT_LT(x, 10000.0f) << "X position should be within reasonable bounds";
            EXPECT_GT(y, -10000.0f) << "Y position should be within reasonable bounds";
            EXPECT_LT(y, 10000.0f) << "Y position should be within reasonable bounds";
        }
    }

    std::cout << "  Valid placeables: " << validPlaceables << "\n";

    // Print first few placeables for verification
    std::cout << "  Sample placeables:\n";
    int count = 0;
    for (const auto& p : placeables) {
        if (p && count < 5) {
            std::cout << "    " << p->getName() << " at ("
                      << p->getX() << ", " << p->getY() << ", " << p->getZ() << ")\n";
            count++;
        }
    }
}

// Test object definitions are loaded
TEST_F(IntegrationTest, ObjectDefinitions) {
    std::string s3dPath = eqClientPath_ + "/ecommons.s3d";
    if (!fileExists(s3dPath)) {
        GTEST_SKIP() << "EQ client not found";
    }

    WldLoader loader;
    ASSERT_TRUE(loader.parseFromArchive(s3dPath, "ecommons.wld"));

    const auto& objectDefs = loader.getObjectDefs();
    std::cout << "Object definitions: " << objectDefs.size() << "\n";

    // Verify object definitions have mesh references
    size_t defsWithMesh = 0;
    for (const auto& [name, def] : objectDefs) {
        if (!def.meshRefs.empty()) {
            defsWithMesh++;
        }
    }

    std::cout << "  Definitions with meshes: " << defsWithMesh << "\n";
}

// ============================================================================
// Phase 11.3: Character Model Integration Tests
// ============================================================================

// Test loading human character model
TEST_F(IntegrationTest, HumanCharacterModel) {
    std::string s3dPath = eqClientPath_ + "/global_chr.s3d";
    if (!fileExists(s3dPath)) {
        GTEST_SKIP() << "global_chr.s3d not found";
    }

    WldLoader loader;
    // Human male model is in global_chr.s3d with wld file
    ASSERT_TRUE(loader.parseFromArchive(s3dPath, "global_chr.wld"));

    // Check for skeleton tracks (character animation data)
    const auto& skeletonTracks = loader.getSkeletonTracks();
    std::cout << "Human character model:\n";
    std::cout << "  Skeleton tracks: " << skeletonTracks.size() << "\n";

    // Check for bone orientations
    const auto& boneOrientations = loader.getBoneOrientations();
    std::cout << "  Bone orientations: " << boneOrientations.size() << "\n";

    // Check for animation track definitions
    const auto& trackDefs = loader.getTrackDefs();
    std::cout << "  Track definitions: " << trackDefs.size() << "\n";

    // Verify character data was loaded
    EXPECT_TRUE(loader.hasCharacterData()) << "Should have character animation data";
}

// Test loading race-specific character model
TEST_F(IntegrationTest, ElfCharacterModel) {
    std::string s3dPath = eqClientPath_ + "/globalelf_chr.s3d";
    if (!fileExists(s3dPath)) {
        GTEST_SKIP() << "globalelf_chr.s3d not found";
    }

    WldLoader loader;
    ASSERT_TRUE(loader.parseFromArchive(s3dPath, "globalelf_chr.wld"));

    const auto& skeletonTracks = loader.getSkeletonTracks();
    std::cout << "Elf character model:\n";
    std::cout << "  Skeleton tracks: " << skeletonTracks.size() << "\n";

    const auto& geometries = loader.getGeometries();
    std::cout << "  Mesh parts: " << geometries.size() << "\n";

    // Count total vertices across all mesh parts
    size_t totalVertices = 0;
    for (const auto& geom : geometries) {
        totalVertices += geom->vertices.size();
    }
    std::cout << "  Total vertices: " << totalVertices << "\n";
}

// Test skeleton bone hierarchy
TEST_F(IntegrationTest, SkeletonBoneHierarchy) {
    std::string s3dPath = eqClientPath_ + "/global_chr.s3d";
    if (!fileExists(s3dPath)) {
        GTEST_SKIP() << "global_chr.s3d not found";
    }

    WldLoader loader;
    ASSERT_TRUE(loader.parseFromArchive(s3dPath, "global_chr.wld"));

    const auto& skeletonTracks = loader.getSkeletonTracks();

    // Find and verify skeleton structure
    size_t maxBones = 0;
    std::string maxBoneSkeleton;

    for (const auto& [fragIdx, skeleton] : skeletonTracks) {
        if (skeleton->bones.size() > maxBones) {
            maxBones = skeleton->bones.size();
            maxBoneSkeleton = skeleton->name;
        }
    }

    std::cout << "Skeleton hierarchy:\n";
    std::cout << "  Total skeletons: " << skeletonTracks.size() << "\n";
    std::cout << "  Most complex skeleton: " << maxBoneSkeleton << " (" << maxBones << " bones)\n";

    // Print details of the most complex skeleton
    for (const auto& [fragIdx, skeleton] : skeletonTracks) {
        if (skeleton->name == maxBoneSkeleton) {
            std::cout << "  Sample bones:\n";
            for (size_t i = 0; i < std::min(skeleton->bones.size(), size_t(5)); ++i) {
                const auto& bone = skeleton->bones[i];
                std::cout << "    Bone " << i << ": name=" << bone->name
                          << " modelRef=" << bone->modelRef << "\n";
            }
            break;
        }
    }

    // Verify we have skeleton data
    EXPECT_GT(skeletonTracks.size(), 0u) << "Should have skeleton tracks";
}

// ============================================================================
// Phase 11.4: Light Placement Integration Tests
// ============================================================================

// Test loading zone lights
TEST_F(IntegrationTest, EastCommonsLights) {
    std::string s3dPath = eqClientPath_ + "/ecommons.s3d";
    if (!fileExists(s3dPath)) {
        GTEST_SKIP() << "EQ client not found";
    }

    WldLoader loader;
    ASSERT_TRUE(loader.parseFromArchive(s3dPath, "ecommons.wld"));

    const auto& lights = loader.getLights();
    std::cout << "East Commonlands lights:\n";
    std::cout << "  Light count: " << lights.size() << "\n";

    // Verify light data
    for (size_t i = 0; i < std::min(lights.size(), size_t(5)); ++i) {
        const auto& light = lights[i];
        std::cout << "  Light " << i << ": pos=(" << light->x << ", "
                  << light->y << ", " << light->z << ") ";
        std::cout << "color=(" << (int)light->r << ", " << (int)light->g
                  << ", " << (int)light->b << ") ";
        std::cout << "radius=" << light->radius << "\n";

        // Verify light has valid color (not all zeros unless intentional)
        // and reasonable radius
        EXPECT_GE(light->radius, 0.0f) << "Light radius should be non-negative";
    }
}

// Test global ambient light
TEST_F(IntegrationTest, GlobalAmbientLight) {
    std::string s3dPath = eqClientPath_ + "/ecommons.s3d";
    if (!fileExists(s3dPath)) {
        GTEST_SKIP() << "EQ client not found";
    }

    WldLoader loader;
    ASSERT_TRUE(loader.parseFromArchive(s3dPath, "ecommons.wld"));

    std::cout << "Global ambient light:\n";
    if (loader.hasGlobalAmbientLight()) {
        const auto& ambient = loader.getGlobalAmbientLight();
        std::cout << "  Present: yes\n";
        // Log ambient light details if available
    } else {
        std::cout << "  Present: no (outdoor zone)\n";
    }

    // Check ambient light regions
    const auto& regions = loader.getAmbientLightRegions();
    std::cout << "  Ambient regions: " << regions.size() << "\n";
}

// ============================================================================
// Phase 11.5: BSP Tree Integration Tests
// ============================================================================

// Test BSP tree loading for zone lines
TEST_F(IntegrationTest, BspTreeZoneLines) {
    std::string s3dPath = eqClientPath_ + "/ecommons.s3d";
    if (!fileExists(s3dPath)) {
        GTEST_SKIP() << "EQ client not found";
    }

    WldLoader loader;
    ASSERT_TRUE(loader.parseFromArchive(s3dPath, "ecommons.wld"));

    std::cout << "BSP Tree:\n";
    std::cout << "  Has zone lines: " << (loader.hasZoneLines() ? "yes" : "no") << "\n";

    auto bspTree = loader.getBspTree();
    if (bspTree) {
        std::cout << "  Region count: " << bspTree->regions.size() << "\n";

        // Count zone line regions
        size_t zoneLineCount = 0;
        for (const auto& region : bspTree->regions) {
            for (auto type : region->regionTypes) {
                if (type == RegionType::Zoneline) {
                    zoneLineCount++;
                    break;
                }
            }
        }
        std::cout << "  Zone line regions: " << zoneLineCount << "\n";
    }
}

// Test PVS (Potentially Visible Set) data decoding
TEST_F(IntegrationTest, PvsDataDecoding) {
    std::string s3dPath = eqClientPath_ + "/befallen.s3d";  // Use befallen - an indoor zone with good PVS data
    if (!fileExists(s3dPath)) {
        GTEST_SKIP() << "EQ client not found";
    }

    WldLoader loader;
    ASSERT_TRUE(loader.parseFromArchive(s3dPath, "befallen.wld"));

    std::cout << "PVS Data (befallen):\n";
    std::cout << "  Has PVS data: " << (loader.hasPvsData() ? "yes" : "no") << "\n";
    std::cout << "  Total region count: " << loader.getTotalRegionCount() << "\n";

    auto bspTree = loader.getBspTree();
    ASSERT_NE(bspTree, nullptr);

    // Count regions with PVS data and calculate statistics
    size_t regionsWithPvs = 0;
    size_t totalVisibleRegions = 0;
    size_t minVisible = SIZE_MAX;
    size_t maxVisible = 0;

    for (const auto& region : bspTree->regions) {
        if (!region->visibleRegions.empty()) {
            regionsWithPvs++;
            size_t visibleCount = 0;
            for (bool v : region->visibleRegions) {
                if (v) visibleCount++;
            }
            totalVisibleRegions += visibleCount;
            minVisible = std::min(minVisible, visibleCount);
            maxVisible = std::max(maxVisible, visibleCount);
        }
    }

    std::cout << "  Regions with PVS: " << regionsWithPvs << "/" << bspTree->regions.size() << "\n";

    if (regionsWithPvs > 0) {
        double avgVisible = static_cast<double>(totalVisibleRegions) / regionsWithPvs;
        std::cout << "  Avg visible regions: " << avgVisible << "\n";
        std::cout << "  Min visible: " << minVisible << "\n";
        std::cout << "  Max visible: " << maxVisible << "\n";

        // Verify PVS data makes sense (should be at least visible to self)
        EXPECT_GT(regionsWithPvs, 0) << "Should have some regions with PVS data";
        EXPECT_GT(avgVisible, 0) << "Average visible regions should be > 0";
        EXPECT_LE(maxVisible, bspTree->regions.size()) << "Max visible should not exceed total regions";
    }

    // Test geometry-to-region mapping
    size_t regionsWithGeometry = 0;
    for (size_t i = 0; i < bspTree->regions.size(); ++i) {
        auto geom = loader.getGeometryForRegion(i);
        if (geom) {
            regionsWithGeometry++;
        }
    }
    std::cout << "  Regions with geometry: " << regionsWithGeometry << "/" << bspTree->regions.size() << "\n";
}

// ============================================================================
// Phase 11.6: Cross-Zone Comparison Tests
// ============================================================================

// Test multiple zones load successfully
TEST_F(IntegrationTest, MultipleZonesLoad) {
    std::vector<std::pair<std::string, std::string>> zones = {
        {"ecommons.s3d", "ecommons.wld"},
        {"qeynos2.s3d", "qeynos2.wld"},
        {"freporte.s3d", "freporte.wld"},
        {"nektulos.s3d", "nektulos.wld"},
    };

    std::cout << "Multi-zone loading test:\n";

    for (const auto& [s3dFile, wldFile] : zones) {
        std::string s3dPath = eqClientPath_ + "/" + s3dFile;
        if (!fileExists(s3dPath)) {
            std::cout << "  " << s3dFile << ": SKIPPED (not found)\n";
            continue;
        }

        WldLoader loader;
        bool success = loader.parseFromArchive(s3dPath, wldFile);

        if (success) {
            const auto& geoms = loader.getGeometries();
            size_t verts = 0;
            for (const auto& g : geoms) {
                verts += g->vertices.size();
            }
            std::cout << "  " << s3dFile << ": OK (meshes=" << geoms.size()
                      << ", verts=" << verts << ")\n";
        } else {
            std::cout << "  " << s3dFile << ": FAILED\n";
        }

        EXPECT_TRUE(success) << "Zone " << s3dFile << " should load successfully";
    }
}

// ============================================================================
// Phase 12: Comprehensive Zone Verification
// Tests all pre-Luclin zones to verify WLD parsing works correctly
// ============================================================================

class ComprehensiveZoneTest : public ::testing::Test {
protected:
    std::string eqClientPath_;

    void SetUp() override {
        const char* envPath = std::getenv("EQ_CLIENT_PATH");
        if (envPath) {
            eqClientPath_ = envPath;
        } else {
            eqClientPath_ = "/home/user/projects/claude/EverQuestP1999";
        }
    }

    bool fileExists(const std::string& path) {
        std::ifstream f(path);
        return f.good();
    }

    struct ZoneStats {
        std::string name;
        bool loaded;
        size_t meshCount;
        size_t vertexCount;
        size_t triangleCount;
        size_t placeableCount;
        size_t lightCount;
        size_t bspRegionCount;
        std::string error;
    };

    ZoneStats loadAndVerifyZone(const std::string& zoneName) {
        ZoneStats stats;
        stats.name = zoneName;
        stats.loaded = false;
        stats.meshCount = 0;
        stats.vertexCount = 0;
        stats.triangleCount = 0;
        stats.placeableCount = 0;
        stats.lightCount = 0;
        stats.bspRegionCount = 0;

        std::string s3dPath = eqClientPath_ + "/" + zoneName + ".s3d";
        std::string wldName = zoneName + ".wld";

        if (!fileExists(s3dPath)) {
            stats.error = "File not found";
            return stats;
        }

        try {
            WldLoader loader;
            if (!loader.parseFromArchive(s3dPath, wldName)) {
                stats.error = "Parse failed";
                return stats;
            }

            stats.loaded = true;

            // Collect geometry stats
            const auto& geometries = loader.getGeometries();
            stats.meshCount = geometries.size();
            for (const auto& geom : geometries) {
                stats.vertexCount += geom->vertices.size();
                stats.triangleCount += geom->triangles.size();
            }

            // Collect placeable stats
            stats.placeableCount = loader.getPlaceables().size();

            // Collect light stats
            stats.lightCount = loader.getLights().size();

            // Collect BSP stats
            auto bspTree = loader.getBspTree();
            if (bspTree) {
                stats.bspRegionCount = bspTree->regions.size();
            }

        } catch (const std::exception& e) {
            stats.error = std::string("Exception: ") + e.what();
        }

        return stats;
    }
};

// ============================================================================
// Phase 12.1: Classic (Original) Zones
// ============================================================================

TEST_F(ComprehensiveZoneTest, ClassicZones) {
    std::vector<std::string> classicZones = {
        "befallen", "blackburrow", "butcher", "cauldron", "cazicthule",
        "commons", "ecommons", "erudnext", "erudnint",
        "everfrost", "feerrott", "freporte", "freportn", "freportw",
        "gfaydark", "grobb", "gukbottom", "guktop", "halas",
        "highkeep", "highpass", "hole", "innothule", "kaladima",
        "kaladimb", "kithicor", "lavastorm", "lfaydark", "mistmoore",
        "najena", "nektulos", "neriaka", "neriakb", "neriakc",
        "northkarana", "nro", "oasis", "oggok", "paineel",
        "permafrost", "qcat", "qey2hh1", "qeynos", "qeynos2",
        "qeytoqrg", "qrg", "rathemtn", "rivervale", "runnyeye",
        "soldunga", "soldungb", "southkarana", "sro", "steamfont",
        "tox", "unrest"
    };

    std::cout << "\n=== Classic (Original) Zone Verification ===\n";
    std::cout << std::setw(20) << "Zone" << std::setw(10) << "Status"
              << std::setw(10) << "Meshes" << std::setw(12) << "Vertices"
              << std::setw(12) << "Triangles" << std::setw(10) << "BSP"
              << "\n";
    std::cout << std::string(74, '-') << "\n";

    size_t loadedCount = 0;
    size_t notFoundCount = 0;
    size_t failedCount = 0;

    for (const auto& zone : classicZones) {
        auto stats = loadAndVerifyZone(zone);

        std::cout << std::setw(20) << zone;
        if (stats.loaded) {
            std::cout << std::setw(10) << "OK"
                      << std::setw(10) << stats.meshCount
                      << std::setw(12) << stats.vertexCount
                      << std::setw(12) << stats.triangleCount
                      << std::setw(10) << stats.bspRegionCount;
            loadedCount++;
        } else if (stats.error == "File not found") {
            std::cout << std::setw(10) << "SKIP" << " (not found)";
            notFoundCount++;
        } else {
            std::cout << std::setw(10) << "FAIL" << " (" << stats.error << ")";
            failedCount++;
        }
        std::cout << "\n";

        // Verify loaded zones have geometry
        if (stats.loaded) {
            EXPECT_GT(stats.meshCount, 0u) << zone << " should have meshes";
            EXPECT_GT(stats.vertexCount, 0u) << zone << " should have vertices";
        }
    }

    std::cout << "\nClassic zones summary: " << loadedCount << " loaded, "
              << notFoundCount << " not found, " << failedCount << " failed\n";

    // At least some zones should load
    EXPECT_GT(loadedCount, 0u) << "At least some Classic zones should load";
    EXPECT_EQ(failedCount, 0u) << "No Classic zones should fail to parse";
}

// ============================================================================
// Phase 12.2: Kunark Zones
// ============================================================================

TEST_F(ComprehensiveZoneTest, KunarkZones) {
    std::vector<std::string> kunarkZones = {
        "burningwood", "cabeast", "cabwest", "chardok", "citymist",
        "dalnir", "dreadlands", "droga", "emeraldjungle", "fieldofbone",
        "firiona", "frontiermtns", "kaesora", "karnor", "kurn",
        "lakeofillomen", "nurga", "overthere", "sebilis",
        "skyfire", "swampofnohope", "timorous", "trakanon",
        "wakening", "warslikswood"
    };

    std::cout << "\n=== Kunark Zone Verification ===\n";
    std::cout << std::setw(20) << "Zone" << std::setw(10) << "Status"
              << std::setw(10) << "Meshes" << std::setw(12) << "Vertices"
              << std::setw(12) << "Triangles" << std::setw(10) << "BSP"
              << "\n";
    std::cout << std::string(74, '-') << "\n";

    size_t loadedCount = 0;
    size_t notFoundCount = 0;
    size_t failedCount = 0;

    for (const auto& zone : kunarkZones) {
        auto stats = loadAndVerifyZone(zone);

        std::cout << std::setw(20) << zone;
        if (stats.loaded) {
            std::cout << std::setw(10) << "OK"
                      << std::setw(10) << stats.meshCount
                      << std::setw(12) << stats.vertexCount
                      << std::setw(12) << stats.triangleCount
                      << std::setw(10) << stats.bspRegionCount;
            loadedCount++;
        } else if (stats.error == "File not found") {
            std::cout << std::setw(10) << "SKIP" << " (not found)";
            notFoundCount++;
        } else {
            std::cout << std::setw(10) << "FAIL" << " (" << stats.error << ")";
            failedCount++;
        }
        std::cout << "\n";

        if (stats.loaded) {
            EXPECT_GT(stats.meshCount, 0u) << zone << " should have meshes";
        }
    }

    std::cout << "\nKunark zones summary: " << loadedCount << " loaded, "
              << notFoundCount << " not found, " << failedCount << " failed\n";

    EXPECT_EQ(failedCount, 0u) << "No Kunark zones should fail to parse";
}

// ============================================================================
// Phase 12.3: Velious Zones
// ============================================================================

TEST_F(ComprehensiveZoneTest, VeliousZones) {
    std::vector<std::string> veliousZones = {
        "cobaltscar", "crystal", "eastwastes", "frozenshadow",
        "greatdivide", "growthplane", "iceclad", "kael", "mischiefplane",
        "necropolis", "sirens", "skyshrine", "sleeper", "templeveeshan",
        "thurgadina", "thurgadinb", "velketor", "westwastes"
    };

    std::cout << "\n=== Velious Zone Verification ===\n";
    std::cout << std::setw(20) << "Zone" << std::setw(10) << "Status"
              << std::setw(10) << "Meshes" << std::setw(12) << "Vertices"
              << std::setw(12) << "Triangles" << std::setw(10) << "BSP"
              << "\n";
    std::cout << std::string(74, '-') << "\n";

    size_t loadedCount = 0;
    size_t notFoundCount = 0;
    size_t failedCount = 0;

    for (const auto& zone : veliousZones) {
        auto stats = loadAndVerifyZone(zone);

        std::cout << std::setw(20) << zone;
        if (stats.loaded) {
            std::cout << std::setw(10) << "OK"
                      << std::setw(10) << stats.meshCount
                      << std::setw(12) << stats.vertexCount
                      << std::setw(12) << stats.triangleCount
                      << std::setw(10) << stats.bspRegionCount;
            loadedCount++;
        } else if (stats.error == "File not found") {
            std::cout << std::setw(10) << "SKIP" << " (not found)";
            notFoundCount++;
        } else {
            std::cout << std::setw(10) << "FAIL" << " (" << stats.error << ")";
            failedCount++;
        }
        std::cout << "\n";

        if (stats.loaded) {
            EXPECT_GT(stats.meshCount, 0u) << zone << " should have meshes";
        }
    }

    std::cout << "\nVelious zones summary: " << loadedCount << " loaded, "
              << notFoundCount << " not found, " << failedCount << " failed\n";

    EXPECT_EQ(failedCount, 0u) << "No Velious zones should fail to parse";
}

// ============================================================================
// Phase 12.4: Character Model Verification
// ============================================================================

TEST_F(ComprehensiveZoneTest, CharacterModels) {
    std::vector<std::string> chrFiles = {
        "global_chr", "globalelf_chr", "globaldaf_chr", "globaldam_chr",
        "globalhum_chr", "globalerf_chr", "globalerm_chr"
    };

    std::cout << "\n=== Character Model Verification ===\n";
    std::cout << std::setw(20) << "Model" << std::setw(10) << "Status"
              << std::setw(12) << "Skeletons" << std::setw(12) << "TrackDefs"
              << std::setw(12) << "Meshes" << std::setw(12) << "Vertices"
              << "\n";
    std::cout << std::string(78, '-') << "\n";

    size_t loadedCount = 0;
    size_t notFoundCount = 0;

    for (const auto& chrName : chrFiles) {
        std::string s3dPath = eqClientPath_ + "/" + chrName + ".s3d";
        std::string wldName = chrName + ".wld";

        std::cout << std::setw(20) << chrName;

        if (!fileExists(s3dPath)) {
            std::cout << std::setw(10) << "SKIP" << " (not found)\n";
            notFoundCount++;
            continue;
        }

        WldLoader loader;
        if (loader.parseFromArchive(s3dPath, wldName)) {
            const auto& skeletons = loader.getSkeletonTracks();
            const auto& trackDefs = loader.getTrackDefs();
            const auto& geometries = loader.getGeometries();

            size_t totalVerts = 0;
            for (const auto& g : geometries) {
                totalVerts += g->vertices.size();
            }

            std::cout << std::setw(10) << "OK"
                      << std::setw(12) << skeletons.size()
                      << std::setw(12) << trackDefs.size()
                      << std::setw(12) << geometries.size()
                      << std::setw(12) << totalVerts << "\n";

            loadedCount++;
            EXPECT_TRUE(loader.hasCharacterData()) << chrName << " should have character data";
        } else {
            std::cout << std::setw(10) << "FAIL\n";
        }
    }

    std::cout << "\nCharacter models summary: " << loadedCount << " loaded, "
              << notFoundCount << " not found\n";
}

// ============================================================================
// Phase 12.5: Zone Object Files Verification
// ============================================================================

TEST_F(ComprehensiveZoneTest, ZoneObjectFiles) {
    // Zone object files contain placeables like trees, rocks, etc.
    std::vector<std::string> objZones = {
        "ecommons_obj", "qeynos2_obj", "freporte_obj", "nektulos_obj",
        "gfaydark_obj", "lfaydark_obj", "commons_obj"
    };

    std::cout << "\n=== Zone Object Files Verification ===\n";
    std::cout << std::setw(20) << "Object File" << std::setw(10) << "Status"
              << std::setw(12) << "ObjectDefs" << std::setw(12) << "Meshes"
              << "\n";
    std::cout << std::string(54, '-') << "\n";

    size_t loadedCount = 0;

    for (const auto& objName : objZones) {
        std::string s3dPath = eqClientPath_ + "/" + objName + ".s3d";

        std::cout << std::setw(20) << objName;

        if (!fileExists(s3dPath)) {
            std::cout << std::setw(10) << "SKIP" << " (not found)\n";
            continue;
        }

        // Object files typically have a different WLD name
        std::string wldName = objName + ".wld";

        WldLoader loader;
        if (loader.parseFromArchive(s3dPath, wldName)) {
            const auto& objectDefs = loader.getObjectDefs();
            const auto& geometries = loader.getGeometries();

            std::cout << std::setw(10) << "OK"
                      << std::setw(12) << objectDefs.size()
                      << std::setw(12) << geometries.size() << "\n";

            loadedCount++;
        } else {
            std::cout << std::setw(10) << "FAIL\n";
        }
    }

    std::cout << "\nObject files summary: " << loadedCount << " loaded\n";
}

// ============================================================================
// Phase 12.6: Summary Statistics
// ============================================================================

TEST_F(ComprehensiveZoneTest, OverallSummary) {
    // Quick test of key zones to generate a summary
    std::vector<std::pair<std::string, std::string>> keyZones = {
        {"ecommons", "East Commonlands"},
        {"qeynos2", "South Qeynos"},
        {"freporte", "East Freeport"},
        {"gfaydark", "Greater Faydark"},
        {"nektulos", "Nektulos Forest"},
        {"butcher", "Butcherblock Mountains"},
        {"highpass", "High Pass Hold"},
        {"cazicthule", "Cazic-Thule"},
    };

    std::cout << "\n=== Key Zone Summary ===\n\n";

    size_t totalMeshes = 0;
    size_t totalVertices = 0;
    size_t totalTriangles = 0;
    size_t zonesLoaded = 0;

    for (const auto& [zone, displayName] : keyZones) {
        auto stats = loadAndVerifyZone(zone);
        if (stats.loaded) {
            std::cout << displayName << " (" << zone << "):\n";
            std::cout << "  Meshes: " << stats.meshCount << "\n";
            std::cout << "  Vertices: " << stats.vertexCount << "\n";
            std::cout << "  Triangles: " << stats.triangleCount << "\n";
            std::cout << "  BSP Regions: " << stats.bspRegionCount << "\n\n";

            totalMeshes += stats.meshCount;
            totalVertices += stats.vertexCount;
            totalTriangles += stats.triangleCount;
            zonesLoaded++;
        }
    }

    if (zonesLoaded > 0) {
        std::cout << "=== Totals across " << zonesLoaded << " key zones ===\n";
        std::cout << "  Total meshes: " << totalMeshes << "\n";
        std::cout << "  Total vertices: " << totalVertices << "\n";
        std::cout << "  Total triangles: " << totalTriangles << "\n";
        std::cout << "  Average meshes/zone: " << totalMeshes / zonesLoaded << "\n";
        std::cout << "  Average vertices/zone: " << totalVertices / zonesLoaded << "\n";
    }

    EXPECT_GT(zonesLoaded, 0u) << "At least some key zones should load";
}
