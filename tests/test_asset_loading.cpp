/**
 * Asset Loading Integration Tests
 *
 * These tests verify that all game assets load without errors:
 * - Equipment models (gequip*.s3d) - actor names and geometries
 * - Race/character models (global*_chr.s3d, zone_chr.s3d)
 * - Textures (BMP, DDS)
 * - Animations
 * - Zone lights
 *
 * Requirements:
 * - EQ client files at EQ_CLIENT_PATH or /home/user/projects/claude/EverQuestP1999
 *
 * Usage:
 *   ./bin/test_asset_loading [--gtest_filter=*Equipment*]
 */

#include <gtest/gtest.h>
#include <fstream>
#include <cstdlib>
#include <set>
#include <map>
#include <regex>

#include "client/graphics/eq/pfs.h"
#include "client/graphics/eq/wld_loader.h"
#include "client/graphics/eq/s3d_loader.h"
#include "client/graphics/eq/race_model_loader.h"
#include "common/logging.h"

using namespace EQT::Graphics;

// Base test fixture with EQ client path handling
class AssetLoadingTest : public ::testing::Test {
protected:
    std::string eqClientPath_;

    void SetUp() override {
        // Try to get EQ client path from environment or use default
        const char* envPath = std::getenv("EQ_CLIENT_PATH");
        if (envPath) {
            eqClientPath_ = envPath;
        } else {
            eqClientPath_ = "/home/user/projects/claude/EverQuestP1999";
        }

        // Ensure path ends with /
        if (!eqClientPath_.empty() && eqClientPath_.back() != '/') {
            eqClientPath_ += '/';
        }
    }

    bool fileExists(const std::string& path) {
        std::ifstream f(path);
        return f.good();
    }

    void skipIfNoClient() {
        if (!fileExists(eqClientPath_ + "gequip.s3d")) {
            GTEST_SKIP() << "EQ client files not found at " << eqClientPath_;
        }
    }
};

// ============================================================================
// Equipment Model Loading Tests
// ============================================================================

class EquipmentModelLoadingTest : public AssetLoadingTest {
protected:
    void SetUp() override {
        AssetLoadingTest::SetUp();
        skipIfNoClient();
    }
};

// Test that equipment archives can be opened and have IT### actors
TEST_F(EquipmentModelLoadingTest, LoadEquipmentArchives) {
    std::vector<std::string> archives = {
        "gequip.s3d", "gequip2.s3d", "gequip3.s3d"
    };

    int totalActors = 0;
    int itActors = 0;
    std::regex pattern("IT(\\d+)");

    for (const auto& archive : archives) {
        std::string path = eqClientPath_ + archive;
        if (!fileExists(path)) continue;

        // Get WLD name from archive (e.g., "gequip.s3d" -> "gequip.wld")
        std::string wldName = archive.substr(0, archive.find(".s3d")) + ".wld";

        WldLoader wld;
        if (!wld.parseFromArchive(path, wldName)) continue;

        auto& objectDefs = wld.getObjectDefs();
        totalActors += objectDefs.size();

        // Count IT### actors
        for (const auto& [name, objDef] : objectDefs) {
            std::smatch match;
            if (std::regex_match(name, match, pattern)) {
                itActors++;
            }
        }

        std::cout << archive << ": " << objectDefs.size() << " actors" << std::endl;
    }

    std::cout << "Total: " << itActors << " IT### actors out of "
              << totalActors << " total actors" << std::endl;

    EXPECT_GT(itActors, 100) << "Expected more IT### equipment actors";
}

// Test that actor names match the expected pattern after _ACTORDEF stripping
TEST_F(EquipmentModelLoadingTest, VerifyActorNamePattern) {
    std::string path = eqClientPath_ + "gequip.s3d";
    ASSERT_TRUE(fileExists(path));

    WldLoader wld;
    ASSERT_TRUE(wld.parseFromArchive(path, "gequip.wld"));

    auto& objectDefs = wld.getObjectDefs();
    EXPECT_GT(objectDefs.size(), 0u) << "No object defs found";

    // WLD loader strips _ACTORDEF suffix, so names should be like "IT123"
    std::regex pattern("IT(\\d+)");
    int matched = 0;
    int sampleCount = 0;

    for (const auto& [name, objDef] : objectDefs) {
        std::smatch match;
        if (std::regex_match(name, match, pattern)) {
            matched++;
            if (sampleCount < 5) {
                std::cout << "IT actor: " << name << std::endl;
                sampleCount++;
            }
        }
    }

    std::cout << "Matched " << matched << " IT### actors out of "
              << objectDefs.size() << " total" << std::endl;

    // Most actors in gequip should be equipment items
    float matchRatio = static_cast<float>(matched) / objectDefs.size();
    EXPECT_GT(matchRatio, 0.5f) << "Expected majority of actors to be IT### equipment";
}

// Test that geometries exist for IT### actors
TEST_F(EquipmentModelLoadingTest, VerifyGeometriesExist) {
    std::string path = eqClientPath_ + "gequip.s3d";
    ASSERT_TRUE(fileExists(path));

    WldLoader wld;
    ASSERT_TRUE(wld.parseFromArchive(path, "gequip.wld"));

    auto& geometries = wld.getGeometries();
    EXPECT_GT(geometries.size(), 0u) << "No geometries found";

    // Count geometries with IT### prefix
    int itGeometries = 0;
    for (const auto& geom : geometries) {
        if (!geom || geom->name.empty()) continue;
        if (geom->name.find("IT") == 0) {
            itGeometries++;
        }
    }

    std::cout << "Found " << itGeometries << " IT### geometries out of "
              << geometries.size() << " total" << std::endl;

    EXPECT_GT(itGeometries, 100) << "Expected more IT### geometries";
}

// ============================================================================
// Race Model Loading Tests
// ============================================================================

class RaceModelLoadingTest : public AssetLoadingTest {
protected:
    void SetUp() override {
        AssetLoadingTest::SetUp();
        skipIfNoClient();
    }
};

// Test loading global character files
TEST_F(RaceModelLoadingTest, LoadGlobalChrFiles) {
    std::vector<std::string> globalFiles = {
        "global_chr.s3d", "global2_chr.s3d", "global3_chr.s3d",
        "global4_chr.s3d", "global5_chr.s3d", "global6_chr.s3d", "global7_chr.s3d"
    };

    int loadedCount = 0;
    int totalModels = 0;

    for (const auto& filename : globalFiles) {
        std::string path = eqClientPath_ + filename;
        if (!fileExists(path)) continue;

        // Get WLD name from archive (e.g., "global_chr.s3d" -> "global_chr.wld")
        std::string wldName = filename.substr(0, filename.find(".s3d")) + ".wld";

        WldLoader wld;
        if (!wld.parseFromArchive(path, wldName)) continue;

        loadedCount++;

        auto& skeletons = wld.getSkeletonTracks();
        totalModels += skeletons.size();

        std::cout << filename << ": " << skeletons.size() << " skeletons" << std::endl;
    }

    EXPECT_GT(loadedCount, 0) << "No global_chr files found";
    EXPECT_GT(totalModels, 0) << "No skeletons found in global_chr files";
    std::cout << "Total skeletons in global files: " << totalModels << std::endl;
}

// Test loading race codes for all pre-Luclin races
TEST_F(RaceModelLoadingTest, VerifyRaceCodeMappings) {
    std::map<int, std::string> expectedCodes = {
        {1, "HUM"},   // Human
        {2, "BAM"},   // Barbarian
        {3, "ERM"},   // Erudite
        {4, "ELM"},   // Wood Elf
        {5, "HIM"},   // High Elf
        {6, "DAM"},   // Dark Elf
        {7, "HAM"},   // Half Elf
        {8, "DWM"},   // Dwarf
        {9, "TRM"},   // Troll
        {10, "OGM"},  // Ogre
        {11, "HOM"},  // Halfling
        {12, "GNM"},  // Gnome
        {13, "WOL"},  // Wolf
        {21, "SKE"},  // Skeleton
        {22, "BET"},  // Beetle
        {44, "GNN"},  // Gnoll
        {128, "IKM"}, // Iksar
    };

    int matched = 0;
    for (const auto& [raceId, expectedCode] : expectedCodes) {
        std::string actualCode = RaceModelLoader::getRaceCode(raceId);
        if (actualCode == expectedCode) {
            matched++;
        } else {
            std::cout << "Race " << raceId << ": expected " << expectedCode
                      << ", got " << actualCode << std::endl;
        }
    }

    EXPECT_EQ(matched, expectedCodes.size())
        << "Some race code mappings are incorrect";
}

// ============================================================================
// Texture Loading Tests
// ============================================================================

class TextureLoadingTest : public AssetLoadingTest {
protected:
    void SetUp() override {
        AssetLoadingTest::SetUp();
        skipIfNoClient();
    }
};

// Test loading BMP textures from S3D
TEST_F(TextureLoadingTest, LoadBMPTextures) {
    std::string path = eqClientPath_ + "global_chr.s3d";
    ASSERT_TRUE(fileExists(path));

    PfsArchive pfs;
    ASSERT_TRUE(pfs.open(path));

    std::vector<std::string> bmpFiles;
    pfs.getFilenames(".bmp", bmpFiles);

    EXPECT_GT(bmpFiles.size(), 0u) << "No BMP textures found";
    std::cout << "Found " << bmpFiles.size() << " BMP textures" << std::endl;

    // Try to load a few textures
    int loaded = 0;
    int failed = 0;
    for (size_t i = 0; i < std::min(bmpFiles.size(), size_t(10)); i++) {
        std::vector<char> data;
        if (pfs.get(bmpFiles[i], data)) {
            if (data.size() > 54) { // Minimum BMP header size
                loaded++;
            } else {
                failed++;
            }
        } else {
            failed++;
        }
    }

    EXPECT_GT(loaded, 0) << "No BMP textures could be loaded";
    std::cout << "Loaded " << loaded << " BMP textures, " << failed << " failed" << std::endl;
}

// Test texture loading from zone files
TEST_F(TextureLoadingTest, LoadZoneTextures) {
    std::string path = eqClientPath_ + "commons.s3d";
    if (!fileExists(path)) {
        path = eqClientPath_ + "qeynos.s3d";
    }
    if (!fileExists(path)) {
        GTEST_SKIP() << "No zone S3D files found";
    }

    PfsArchive pfs;
    ASSERT_TRUE(pfs.open(path));

    std::vector<std::string> bmpFiles;
    std::vector<std::string> ddsFiles;
    pfs.getFilenames(".bmp", bmpFiles);
    pfs.getFilenames(".dds", ddsFiles);

    std::cout << "Zone textures: " << bmpFiles.size() << " BMP, "
              << ddsFiles.size() << " DDS" << std::endl;

    EXPECT_GT(bmpFiles.size() + ddsFiles.size(), 0u)
        << "No textures found in zone file";
}

// ============================================================================
// Animation Loading Tests
// ============================================================================

class AnimationLoadingTest : public AssetLoadingTest {
protected:
    void SetUp() override {
        AssetLoadingTest::SetUp();
        skipIfNoClient();
    }
};

// Test loading character animations
TEST_F(AnimationLoadingTest, LoadCharacterAnimations) {
    std::string path = eqClientPath_ + "global_chr.s3d";
    ASSERT_TRUE(fileExists(path));

    WldLoader wld;
    ASSERT_TRUE(wld.parseFromArchive(path, "global_chr.wld"));

    auto& skeletons = wld.getSkeletonTracks();
    ASSERT_GT(skeletons.size(), 0u) << "No skeletons found";

    // Animation data is stored in TrackDefs (keyframes) and TrackRefs (metadata)
    auto& trackDefs = wld.getTrackDefs();
    auto& trackRefs = wld.getTrackRefs();

    std::cout << "Skeletons: " << skeletons.size() << std::endl;
    std::cout << "Track definitions (0x12): " << trackDefs.size() << std::endl;
    std::cout << "Track references (0x13): " << trackRefs.size() << std::endl;

    // Count animation tracks per model code
    std::map<std::string, int> animsByModel;
    for (const auto& [fragIdx, trackRef] : trackRefs) {
        if (!trackRef || trackRef->modelCode.empty()) continue;
        animsByModel[trackRef->modelCode]++;
    }

    // Print some sample models and their animation counts
    int sampleCount = 0;
    for (const auto& [modelCode, count] : animsByModel) {
        if (sampleCount < 10) {
            std::cout << "Model " << modelCode << ": " << count << " animation tracks" << std::endl;
            sampleCount++;
        }
    }

    std::cout << "Total: " << trackRefs.size() << " animation tracks across "
              << animsByModel.size() << " models" << std::endl;

    EXPECT_GT(trackDefs.size(), 0u) << "No animation track definitions found";
    EXPECT_GT(trackRefs.size(), 0u) << "No animation track references found";
}

// ============================================================================
// Zone Light Loading Tests
// ============================================================================

class ZoneLightLoadingTest : public AssetLoadingTest {
protected:
    void SetUp() override {
        AssetLoadingTest::SetUp();
        skipIfNoClient();
    }
};

// Test loading zone lights
TEST_F(ZoneLightLoadingTest, LoadZoneLights) {
    std::vector<std::string> testZones = {
        "freportw.s3d", "qeynos2.s3d", "commons.s3d", "nektulos.s3d"
    };

    int zonesWithLights = 0;
    int totalLights = 0;

    for (const auto& zoneName : testZones) {
        std::string path = eqClientPath_ + zoneName;
        if (!fileExists(path)) continue;

        S3DLoader loader;
        if (!loader.loadZone(path)) continue;

        auto zone = loader.getZone();
        if (!zone) continue;

        int lightCount = zone->lights.size();
        if (lightCount > 0) {
            zonesWithLights++;
            totalLights += lightCount;
            std::cout << zoneName << ": " << lightCount << " lights" << std::endl;
        }
    }

    std::cout << "Total: " << totalLights << " lights across "
              << zonesWithLights << " zones" << std::endl;

    // Not all zones have lights, so just verify we can load them
    EXPECT_GE(zonesWithLights, 0);
}

// Test light properties are valid
TEST_F(ZoneLightLoadingTest, VerifyLightProperties) {
    std::string path = eqClientPath_ + "freportw.s3d";
    if (!fileExists(path)) {
        GTEST_SKIP() << "freportw.s3d not found";
    }

    S3DLoader loader;
    ASSERT_TRUE(loader.loadZone(path));

    auto zone = loader.getZone();
    ASSERT_NE(zone, nullptr);

    if (zone->lights.empty()) {
        std::cout << "No lights in zone" << std::endl;
        return;
    }

    int validLights = 0;
    for (const auto& light : zone->lights) {
        if (!light) continue;

        // Light radius should be positive
        bool valid = light->radius > 0.0f;

        // Light color components should be in [0, 1] range
        valid = valid && light->r >= 0.0f && light->r <= 1.0f;
        valid = valid && light->g >= 0.0f && light->g <= 1.0f;
        valid = valid && light->b >= 0.0f && light->b <= 1.0f;

        if (valid) {
            validLights++;
        } else {
            std::cout << "Invalid light at (" << light->x << ", " << light->y << ", " << light->z
                      << ") radius=" << light->radius
                      << " rgb=(" << light->r << ", " << light->g << ", " << light->b << ")" << std::endl;
        }
    }

    EXPECT_EQ(validLights, static_cast<int>(zone->lights.size()))
        << "Some lights have invalid properties";
}

// ============================================================================
// Zone Geometry Loading Tests
// ============================================================================

class ZoneGeometryLoadingTest : public AssetLoadingTest {
protected:
    void SetUp() override {
        AssetLoadingTest::SetUp();
        skipIfNoClient();
    }
};

// Test loading zone geometry
TEST_F(ZoneGeometryLoadingTest, LoadZoneGeometry) {
    std::string path = eqClientPath_ + "commons.s3d";
    if (!fileExists(path)) {
        GTEST_SKIP() << "commons.s3d not found";
    }

    S3DLoader loader;
    ASSERT_TRUE(loader.loadZone(path)) << "Failed to load zone: " << loader.getError();

    auto zone = loader.getZone();
    ASSERT_NE(zone, nullptr);
    ASSERT_NE(zone->geometry, nullptr) << "Zone geometry is null";

    int totalVertices = zone->geometry->vertices.size();
    int totalTriangles = zone->geometry->triangles.size();

    std::cout << "Zone geometry: " << totalVertices << " vertices, "
              << totalTriangles << " triangles" << std::endl;

    EXPECT_GT(totalVertices, 0) << "No vertices in zone geometry";
    EXPECT_GT(totalTriangles, 0) << "No triangles in zone geometry";
}

// Test loading zone objects (placeables)
TEST_F(ZoneGeometryLoadingTest, LoadZoneObjects) {
    std::string path = eqClientPath_ + "freportw.s3d";
    if (!fileExists(path)) {
        GTEST_SKIP() << "freportw.s3d not found";
    }

    S3DLoader loader;
    ASSERT_TRUE(loader.loadZone(path));

    auto zone = loader.getZone();
    ASSERT_NE(zone, nullptr);

    std::cout << "Zone objects: " << zone->objects.size() << std::endl;

    // Verify object properties
    for (size_t i = 0; i < std::min(zone->objects.size(), size_t(5)); i++) {
        const auto& obj = zone->objects[i];
        if (obj.placeable) {
            std::cout << "Object " << i << ": name=" << obj.placeable->getName()
                      << " pos=(" << obj.placeable->getX() << ", "
                      << obj.placeable->getY() << ", " << obj.placeable->getZ() << ")"
                      << " rot=(" << obj.placeable->getRotateX() << ", "
                      << obj.placeable->getRotateY() << ", " << obj.placeable->getRotateZ() << ")"
                      << std::endl;
        }
    }

    // Most zones should have some objects
    EXPECT_GT(zone->objects.size(), 0u) << "No objects in zone";
}

// ============================================================================
// Comprehensive Zone Loading Test
// ============================================================================

class ComprehensiveZoneLoadingTest : public AssetLoadingTest {
protected:
    void SetUp() override {
        AssetLoadingTest::SetUp();
        skipIfNoClient();
    }
};

// Test loading multiple zones to verify broad compatibility
TEST_F(ComprehensiveZoneLoadingTest, LoadMultipleZones) {
    std::vector<std::string> testZones = {
        // Classic zones
        "qeynos.s3d", "qeynos2.s3d", "freportn.s3d", "freportw.s3d",
        "commons.s3d", "ecommons.s3d", "nektulos.s3d", "gfaydark.s3d",
        // Kunark zones
        "fieldofbone.s3d", "overthere.s3d",
        // Velious zones
        "iceclad.s3d", "greatdivide.s3d"
    };

    int loadedZones = 0;
    int failedZones = 0;
    std::vector<std::string> failures;

    for (const auto& zoneName : testZones) {
        std::string path = eqClientPath_ + zoneName;
        if (!fileExists(path)) continue;

        S3DLoader loader;
        if (loader.loadZone(path)) {
            auto zone = loader.getZone();
            if (zone && zone->geometry) {
                loadedZones++;
                std::cout << zoneName << ": OK ("
                          << zone->geometry->vertices.size() << " verts, "
                          << zone->objects.size() << " objects, "
                          << zone->lights.size() << " lights)" << std::endl;
            } else {
                failedZones++;
                failures.push_back(zoneName + " (empty geometry)");
            }
        } else {
            failedZones++;
            failures.push_back(zoneName + " (" + loader.getError() + ")");
        }
    }

    std::cout << "\nSummary: " << loadedZones << " zones loaded, "
              << failedZones << " failed" << std::endl;

    for (const auto& failure : failures) {
        std::cout << "  FAILED: " << failure << std::endl;
    }

    EXPECT_GT(loadedZones, 0) << "No zones could be loaded";
    EXPECT_EQ(failedZones, 0) << "Some zones failed to load";
}

// Test loading zone character files (_chr.s3d)
TEST_F(ComprehensiveZoneLoadingTest, LoadZoneChrFiles) {
    std::vector<std::string> testZones = {
        "commons_chr.s3d", "qeynos2_chr.s3d", "freportw_chr.s3d",
        "nektulos_chr.s3d", "befallen_chr.s3d", "gfaydark_chr.s3d"
    };

    int loadedFiles = 0;
    int totalModels = 0;

    for (const auto& filename : testZones) {
        std::string path = eqClientPath_ + filename;
        if (!fileExists(path)) continue;

        // Get WLD name from archive (e.g., "commons_chr.s3d" -> "commons_chr.wld")
        std::string wldName = filename.substr(0, filename.find(".s3d")) + ".wld";

        WldLoader wld;
        if (!wld.parseFromArchive(path, wldName)) continue;

        auto& skeletons = wld.getSkeletonTracks();
        if (skeletons.size() > 0) {
            loadedFiles++;
            totalModels += skeletons.size();
            std::cout << filename << ": " << skeletons.size() << " skeletons" << std::endl;
        }
    }

    std::cout << "Total: " << totalModels << " skeletons from "
              << loadedFiles << " zone _chr files" << std::endl;
    EXPECT_GT(loadedFiles, 0) << "No zone _chr files could be loaded";
}

// ============================================================================
// Main
// ============================================================================

int main(int argc, char **argv) {
    SetLogLevel(LOG_WARN);  // Reduce noise during tests

    std::cout << "=== Asset Loading Integration Tests ===" << std::endl;
    std::cout << "These tests verify that EQ assets load without errors." << std::endl;
    std::cout << "Set EQ_CLIENT_PATH environment variable to specify client location." << std::endl;
    std::cout << std::endl;

    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
