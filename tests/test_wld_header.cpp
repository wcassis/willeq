// WLD header parsing tests + WldIntegrationTest tests.
// Split from test_wld.cpp (T11, 2026-07-29) — pure mechanical move.
// Fixtures (WldHeaderTest, WldIntegrationTest) live in wld_test_fixtures.h.

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

    // ------------------------------------------------------------------
    // CHARACTERIZATION ASSERTIONS (T10, 2026-07-29, zone=qeynos2/qeynos2.wld)
    //
    // These pin the parser's CURRENT observed output over a real archive.
    // They are not a spec derived independently of the code — if the
    // parser has a latent bug, these assertions pin the buggy value too.
    // That's an accepted trade for regression protection (see
    // refactor/T10_wld_kill_rate.md Part B). Values below were sanity
    // checked where an independent check was possible; each check says so.
    // ------------------------------------------------------------------

    // Exact mesh/vertex/triangle counts. Cross-validated two ways: the
    // WldMeshFragmentTest.WldLoader_ZoneGeometry test (below) independently
    // re-parses the raw Fragment 0x36 records for this same archive and
    // reports identical totals (1915 meshes / 36218 vertices / 19470
    // polygons) — so this is not just "whatever the loader emits," it
    // agrees with a second, independent parse of the raw bytes.
    size_t totalVertices = 0, totalTriangles = 0;
    for (const auto& g : geometries) {
        totalVertices += g->vertices.size();
        totalTriangles += g->triangles.size();
    }
    EXPECT_EQ(geometries.size(), 1915u) << "qeynos2 mesh (Fragment 0x36) count changed";
    EXPECT_EQ(totalVertices, 36218u) << "qeynos2 total vertex count changed";
    EXPECT_EQ(totalTriangles, 19470u) << "qeynos2 total triangle count changed";

    // World-space bounding box (via getCombinedGeometry(), which adds each
    // mesh's center back onto its locally-stored vertices — see D20f1).
    // Sanity check: qeynos2 ("Qeynos Hills") is an outdoor zone connecting
    // the two Qeynos city zones; a ~2250 x 2200 x 470 unit footprint is
    // plausible for that role and in line with sibling zones' BSP/mesh
    // scale reported by ComprehensiveZoneTest. Not implausible.
    auto combined = loader.getCombinedGeometry();
    ASSERT_NE(combined, nullptr);
    EXPECT_NEAR(combined->minX, -258.906f, 1.0f);
    EXPECT_NEAR(combined->minY, -1099.56f, 1.0f);
    EXPECT_NEAR(combined->minZ, -390.844f, 1.0f);
    EXPECT_NEAR(combined->maxX, 1999.19f, 1.0f);
    EXPECT_NEAR(combined->maxY, 1099.56f, 1.0f);
    EXPECT_NEAR(combined->maxZ, 79.9688f, 1.0f);

    // Vertex data sanity: no NaNs, and every combined (world-space) vertex
    // falls inside the combined bounding box we just pinned above. This is
    // a real invariant (not characterization) — it must hold regardless of
    // the exact values, since the bbox is computed as min/max over these
    // same vertices.
    bool anyNaN = false;
    bool anyOutsideBbox = false;
    const float bboxTol = 0.01f;
    for (const auto& v : combined->vertices) {
        if (std::isnan(v.x) || std::isnan(v.y) || std::isnan(v.z)) anyNaN = true;
        if (v.x < combined->minX - bboxTol || v.x > combined->maxX + bboxTol ||
            v.y < combined->minY - bboxTol || v.y > combined->maxY + bboxTol ||
            v.z < combined->minZ - bboxTol || v.z > combined->maxZ + bboxTol) {
            anyOutsideBbox = true;
        }
    }
    EXPECT_FALSE(anyNaN) << "Combined geometry contains NaN vertex coordinates";
    EXPECT_FALSE(anyOutsideBbox) << "Combined geometry has a vertex outside its own bounding box";

    // Distinct resolved texture count, and one specific known name. All 128
    // names in this archive are lowercase 8.3-style basenames ending
    // ".bmp" — consistent with real Titanium client texture naming
    // (confirmed independently against the sibling Fragment 0x30/0x31 raw
    // fragment counts: 136 Fragment 0x30 materials resolve down to 128
    // distinct texture names here, i.e. some materials share a texture).
    EXPECT_EQ(combined->textureNames().size(), 128u) << "qeynos2 distinct texture count changed";
    bool foundKnownTexture = false;
    for (const auto& texName : combined->textureNames()) {
        if (texName == "wawall1.bmp") {  // Qeynos-wall texture; plausible EQ Titanium name
            foundKnownTexture = true;
            break;
        }
    }
    EXPECT_TRUE(foundKnownTexture) << "Expected known texture 'wawall1.bmp' not found in qeynos2 texture list";

    // BSP/PVS region counts (D20f1 BSP tree, shared with game state).
    // 1898/2074 regions (91.5%) map back to a mesh via getGeometryForRegion()
    // — plausible: some regions are solid/void space with no polygon data,
    // which is expected in a BSP built over an outdoor zone with terrain.
    EXPECT_TRUE(loader.hasPvsData()) << "qeynos2 should have PVS visibility data";
    auto bspTree = loader.getBspTree();
    ASSERT_NE(bspTree, nullptr);
    EXPECT_EQ(bspTree->regions.size(), 2074u) << "qeynos2 BSP region count changed";
    size_t regionsWithGeometry = 0;
    for (size_t i = 0; i < bspTree->regions.size(); ++i) {
        if (loader.getGeometryForRegion(i)) {
            regionsWithGeometry++;
        }
    }
    EXPECT_EQ(regionsWithGeometry, 1898u) << "qeynos2 region-to-geometry mapping count changed";
}
