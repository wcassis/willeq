// WLD integration tests — full parsing of real zone/character archives.
// Split from test_wld.cpp (T11, 2026-07-29) — pure mechanical move.
// ComprehensiveZoneTest (Phase 12, all pre-Luclin zones) lives in
// test_wld_zones_comprehensive.cpp — kept separate to stay in the
// 400-800 line target (combined the two suites would be ~900 lines).

#include <gtest/gtest.h>
#include <cstdint>
#include <string>
#include <fstream>
#include <cstdlib>
#include <algorithm>
#include <cmath>
#include <iostream>

#include "client/graphics/eq/wld_loader.h"
#include "client/graphics/eq/pfs.h"

using namespace EQT::Graphics;

class IntegrationTest : public ::testing::Test {
protected:
    std::string eqClientPath_;

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
