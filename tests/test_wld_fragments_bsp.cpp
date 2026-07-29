// WLD Fragment 0x21/0x22/0x29 (BSP Tree and Region) tests.
// Split from test_wld.cpp (T11, 2026-07-29) — pure mechanical move.
// WldBspFragmentTest is independent (derives from ::testing::Test
// directly, not from the shared WldHeaderTest/WldIntegrationTest
// fixtures), so it stays self-contained in this TU.

#include <gtest/gtest.h>
#include <cstdint>
#include <cstring>
#include <vector>
#include <string>
#include <fstream>
#include <cstdlib>
#include <algorithm>
#include <tuple>
#include <cmath>
#include <iostream>

#include "client/graphics/eq/wld_loader.h"
#include "client/graphics/eq/pfs.h"

using namespace EQT::Graphics;

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
            eqClientPath_ = EQ_CLIENT_DATA_PATH;
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

