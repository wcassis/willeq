/**
 * Tests for door loading functionality
 *
 * These tests verify that:
 * 1. Zone S3D files contain door object geometries (objectGeometries map)
 * 2. DoorManager can find door meshes from zone data
 * 3. Door creation works correctly with and without graphics
 *
 * Unit tests run without requiring a server or display.
 * Graphics tests require DISPLAY environment variable.
 */

#include <gtest/gtest.h>
#include <cstdlib>
#include <string>
#include <fstream>

#include "client/graphics/eq/s3d_loader.h"
#include "client/graphics/eq/pfs.h"

#ifdef EQT_HAS_GRAPHICS
#include <irrlicht.h>
#include "client/graphics/door_manager.h"
#include "client/graphics/eq/zone_geometry.h"
#endif

// Stub for EverQuest::s_debug_level (used by door_manager.cpp logging)
namespace {
    int s_test_debug_level = 0;
}

// Define the EverQuest stubs needed by door_manager.cpp
class EverQuest {
public:
    static int s_debug_level;
    static int GetDebugLevel() { return s_debug_level; }
};
int EverQuest::s_debug_level = 0;

using namespace EQT::Graphics;

// ============================================================================
// Unit Tests: S3D Zone Loading (no graphics required)
// ============================================================================

class DoorLoadingUnitTest : public ::testing::Test {
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

// Test that S3DLoader loads objectGeometries from zone _obj.s3d files
TEST_F(DoorLoadingUnitTest, S3DLoaderLoadsObjectGeometries) {
    std::string zonePath = eqClientPath_ + "/qeynos2.s3d";
    if (!fileExists(zonePath)) {
        GTEST_SKIP() << "Zone file not found: " << zonePath;
    }

    S3DLoader loader;
    ASSERT_TRUE(loader.loadZone(zonePath)) << "Failed to load zone: " << loader.getError();

    auto zone = loader.getZone();
    ASSERT_NE(zone, nullptr) << "Zone is null after loading";

    // qeynos2 should have object geometries from qeynos2_obj.s3d
    EXPECT_GT(zone->objectGeometries.size(), 0)
        << "No object geometries loaded - door meshes won't be available";

    std::cout << "Loaded " << zone->objectGeometries.size() << " object geometries\n";
}

// Test that common door mesh names exist in objectGeometries
TEST_F(DoorLoadingUnitTest, ObjectGeometriesContainDoorMeshes) {
    std::string zonePath = eqClientPath_ + "/qeynos2.s3d";
    if (!fileExists(zonePath)) {
        GTEST_SKIP() << "Zone file not found: " << zonePath;
    }

    S3DLoader loader;
    ASSERT_TRUE(loader.loadZone(zonePath)) << "Failed to load zone: " << loader.getError();

    auto zone = loader.getZone();
    ASSERT_NE(zone, nullptr);

    // qeynos2 has doors named DOOR1, DOOR2, HHCELL, etc.
    // Check for at least one door mesh
    bool hasDoorMesh = false;
    std::vector<std::string> doorNames;

    for (const auto& [name, geom] : zone->objectGeometries) {
        // Door-like object names typically contain "DOOR" or are specific door models
        if (name.find("DOOR") != std::string::npos ||
            name == "HHCELL" || name == "SPEARDOWN") {
            hasDoorMesh = true;
            doorNames.push_back(name);

            // Verify the geometry has actual data
            ASSERT_NE(geom, nullptr) << "Door geometry '" << name << "' is null";
            EXPECT_GT(geom->vertices.size(), 0)
                << "Door '" << name << "' has no vertices";
            EXPECT_GT(geom->triangles.size(), 0)
                << "Door '" << name << "' has no triangles";
        }
    }

    EXPECT_TRUE(hasDoorMesh) << "No door meshes found in objectGeometries";

    std::cout << "Found " << doorNames.size() << " door meshes: ";
    for (const auto& name : doorNames) {
        std::cout << name << " ";
    }
    std::cout << "\n";
}

// Test that door geometries have proper bounding data for collision
TEST_F(DoorLoadingUnitTest, DoorGeometriesHaveValidBounds) {
    std::string zonePath = eqClientPath_ + "/qeynos2.s3d";
    if (!fileExists(zonePath)) {
        GTEST_SKIP() << "Zone file not found: " << zonePath;
    }

    S3DLoader loader;
    ASSERT_TRUE(loader.loadZone(zonePath)) << "Failed to load zone: " << loader.getError();

    auto zone = loader.getZone();
    ASSERT_NE(zone, nullptr);

    // Check DOOR1 specifically if it exists
    auto it = zone->objectGeometries.find("DOOR1");
    if (it != zone->objectGeometries.end() && it->second) {
        auto& geom = it->second;

        // Calculate bounds from vertices
        float minX = std::numeric_limits<float>::max();
        float maxX = std::numeric_limits<float>::lowest();
        float minY = std::numeric_limits<float>::max();
        float maxY = std::numeric_limits<float>::lowest();
        float minZ = std::numeric_limits<float>::max();
        float maxZ = std::numeric_limits<float>::lowest();

        for (const auto& vert : geom->vertices) {
            minX = std::min(minX, vert.x);
            maxX = std::max(maxX, vert.x);
            minY = std::min(minY, vert.y);
            maxY = std::max(maxY, vert.y);
            minZ = std::min(minZ, vert.z);
            maxZ = std::max(maxZ, vert.z);
        }

        float width = maxX - minX;
        float height = maxZ - minZ;  // Z is up in EQ
        float depth = maxY - minY;

        std::cout << "DOOR1 bounds: " << width << " x " << depth << " x " << height << "\n";

        // A door should have reasonable dimensions (not zero, not huge)
        EXPECT_GT(width, 0.1f) << "Door width too small";
        EXPECT_GT(height, 1.0f) << "Door height too small (should be human-height)";
        EXPECT_LT(width, 50.0f) << "Door width too large";
        EXPECT_LT(height, 50.0f) << "Door height too large";
    }
}

// Test loading multiple zones to ensure objectGeometries is populated consistently
TEST_F(DoorLoadingUnitTest, MultipleZonesHaveObjectGeometries) {
    std::vector<std::string> testZones = {"qeynos2", "qeynos", "freportn"};
    int zonesWithDoors = 0;

    for (const auto& zoneName : testZones) {
        std::string zonePath = eqClientPath_ + "/" + zoneName + ".s3d";
        if (!fileExists(zonePath)) {
            std::cout << "Skipping " << zoneName << " (file not found)\n";
            continue;
        }

        S3DLoader loader;
        if (!loader.loadZone(zonePath)) {
            std::cout << "Failed to load " << zoneName << ": " << loader.getError() << "\n";
            continue;
        }

        auto zone = loader.getZone();
        if (!zone) continue;

        // Count door-like objects
        int doorCount = 0;
        for (const auto& [name, geom] : zone->objectGeometries) {
            if (name.find("DOOR") != std::string::npos) {
                doorCount++;
            }
        }

        if (doorCount > 0) {
            zonesWithDoors++;
            std::cout << zoneName << ": " << zone->objectGeometries.size()
                      << " object geometries, " << doorCount << " doors\n";
        }
    }

    // At least one zone should have doors
    EXPECT_GT(zonesWithDoors, 0) << "No zones had door objects";
}

// ============================================================================
// Graphics Tests: DoorManager (requires DISPLAY)
// ============================================================================

#ifdef EQT_HAS_GRAPHICS

class DoorManagerGraphicsTest : public ::testing::Test {
protected:
    std::string eqClientPath_;
    irr::IrrlichtDevice* device_ = nullptr;
    irr::scene::ISceneManager* smgr_ = nullptr;
    irr::video::IVideoDriver* driver_ = nullptr;

    void SetUp() override {
        const char* display = std::getenv("DISPLAY");
        if (!display || strlen(display) == 0) {
            GTEST_SKIP() << "DISPLAY not set - skipping graphics test";
        }

        const char* envPath = std::getenv("EQ_CLIENT_PATH");
        if (envPath) {
            eqClientPath_ = envPath;
        } else {
            eqClientPath_ = "/home/user/projects/claude/EverQuestP1999";
        }

        // Create minimal Irrlicht device for testing
        irr::SIrrlichtCreationParameters params;
        params.DriverType = irr::video::EDT_NULL;  // Null driver for unit tests
        params.WindowSize = irr::core::dimension2d<irr::u32>(100, 100);

        device_ = irr::createDeviceEx(params);
        if (!device_) {
            GTEST_SKIP() << "Failed to create Irrlicht device";
        }

        smgr_ = device_->getSceneManager();
        driver_ = device_->getVideoDriver();
    }

    void TearDown() override {
        if (device_) {
            device_->drop();
            device_ = nullptr;
        }
    }

    bool fileExists(const std::string& path) {
        std::ifstream f(path);
        return f.good();
    }
};

// Test DoorManager initialization
TEST_F(DoorManagerGraphicsTest, DoorManagerInitialization) {
    DoorManager doorMgr(smgr_, driver_);

    EXPECT_EQ(doorMgr.getDoorCount(), 0) << "New DoorManager should have no doors";
    EXPECT_FALSE(doorMgr.hasDoor(1)) << "Door 1 should not exist yet";
}

// Test DoorManager with zone data set
TEST_F(DoorManagerGraphicsTest, DoorManagerWithZoneData) {
    std::string zonePath = eqClientPath_ + "/qeynos2.s3d";
    if (!fileExists(zonePath)) {
        GTEST_SKIP() << "Zone file not found: " << zonePath;
    }

    S3DLoader loader;
    ASSERT_TRUE(loader.loadZone(zonePath)) << "Failed to load zone";

    auto zone = loader.getZone();
    ASSERT_NE(zone, nullptr);
    ASSERT_GT(zone->objectGeometries.size(), 0) << "No object geometries loaded";

    DoorManager doorMgr(smgr_, driver_);
    doorMgr.setZone(zone);

    // Create a door - should find mesh from objectGeometries
    // Parameters: doorId, name, x, y, z, heading, incline, size, opentype, initiallyOpen
    bool created = doorMgr.createDoor(1, "DOOR1", 100.0f, 200.0f, 0.0f, 128.0f, 0, 100, 0, false);

    EXPECT_TRUE(created) << "Failed to create door with valid zone data";
    EXPECT_TRUE(doorMgr.hasDoor(1)) << "Door 1 should exist after creation";
    EXPECT_EQ(doorMgr.getDoorCount(), 1) << "Should have exactly 1 door";

    // Get door info
    const DoorVisual* door = doorMgr.getDoor(1);
    ASSERT_NE(door, nullptr) << "getDoor returned null for existing door";
    EXPECT_EQ(door->doorId, 1);
    EXPECT_EQ(door->modelName, "DOOR1");
    EXPECT_FLOAT_EQ(door->x, 100.0f);
    EXPECT_FLOAT_EQ(door->y, 200.0f);
}

// Test DoorManager without zone data (should use placeholder)
TEST_F(DoorManagerGraphicsTest, DoorManagerWithoutZoneData) {
    DoorManager doorMgr(smgr_, driver_);
    // Don't call setZone - zone is null

    // Create a door - should still work with placeholder mesh
    bool created = doorMgr.createDoor(1, "DOOR1", 100.0f, 200.0f, 0.0f, 128.0f, 0, 100, 0, false);

    // Door creation should succeed (with placeholder)
    EXPECT_TRUE(created) << "Door creation should succeed even without zone data (using placeholder)";
    EXPECT_TRUE(doorMgr.hasDoor(1));
}

// Test that multiple doors can be created
TEST_F(DoorManagerGraphicsTest, MultipleDoorCreation) {
    std::string zonePath = eqClientPath_ + "/qeynos2.s3d";
    if (!fileExists(zonePath)) {
        GTEST_SKIP() << "Zone file not found: " << zonePath;
    }

    S3DLoader loader;
    ASSERT_TRUE(loader.loadZone(zonePath));
    auto zone = loader.getZone();
    ASSERT_NE(zone, nullptr);

    DoorManager doorMgr(smgr_, driver_);
    doorMgr.setZone(zone);

    // Create multiple doors with different names
    EXPECT_TRUE(doorMgr.createDoor(1, "DOOR1", 0.0f, 0.0f, 0.0f, 0.0f, 0, 100, 0, false));
    EXPECT_TRUE(doorMgr.createDoor(2, "DOOR2", 10.0f, 0.0f, 0.0f, 0.0f, 0, 100, 0, false));
    EXPECT_TRUE(doorMgr.createDoor(3, "HHCELL", 20.0f, 0.0f, 0.0f, 0.0f, 0, 100, 0, false));

    EXPECT_EQ(doorMgr.getDoorCount(), 3);
    EXPECT_TRUE(doorMgr.hasDoor(1));
    EXPECT_TRUE(doorMgr.hasDoor(2));
    EXPECT_TRUE(doorMgr.hasDoor(3));
    EXPECT_FALSE(doorMgr.hasDoor(4));
}

// Test door clearing
TEST_F(DoorManagerGraphicsTest, DoorClearing) {
    DoorManager doorMgr(smgr_, driver_);

    doorMgr.createDoor(1, "DOOR1", 0.0f, 0.0f, 0.0f, 0.0f, 0, 100, 0, false);
    doorMgr.createDoor(2, "DOOR2", 0.0f, 0.0f, 0.0f, 0.0f, 0, 100, 0, false);
    ASSERT_EQ(doorMgr.getDoorCount(), 2);

    doorMgr.clearDoors();

    EXPECT_EQ(doorMgr.getDoorCount(), 0);
    EXPECT_FALSE(doorMgr.hasDoor(1));
    EXPECT_FALSE(doorMgr.hasDoor(2));
}

// Test invisible door types are skipped
TEST_F(DoorManagerGraphicsTest, InvisibleDoorsSkipped) {
    DoorManager doorMgr(smgr_, driver_);

    // opentype 50, 53, 54 should be invisible (skipped but return true)
    EXPECT_TRUE(doorMgr.createDoor(1, "INVIS1", 0.0f, 0.0f, 0.0f, 0.0f, 0, 100, 50, false));
    EXPECT_TRUE(doorMgr.createDoor(2, "INVIS2", 0.0f, 0.0f, 0.0f, 0.0f, 0, 100, 53, false));
    EXPECT_TRUE(doorMgr.createDoor(3, "INVIS3", 0.0f, 0.0f, 0.0f, 0.0f, 0, 100, 54, false));

    // Invisible doors should not be in the doors_ map
    EXPECT_EQ(doorMgr.getDoorCount(), 0) << "Invisible doors should not be counted";
    EXPECT_FALSE(doorMgr.hasDoor(1));
    EXPECT_FALSE(doorMgr.hasDoor(2));
    EXPECT_FALSE(doorMgr.hasDoor(3));
}

// Test door state changes
TEST_F(DoorManagerGraphicsTest, DoorStateChanges) {
    DoorManager doorMgr(smgr_, driver_);

    doorMgr.createDoor(1, "DOOR1", 0.0f, 0.0f, 0.0f, 0.0f, 0, 100, 0, false);

    const DoorVisual* door = doorMgr.getDoor(1);
    ASSERT_NE(door, nullptr);
    EXPECT_FALSE(door->isOpen) << "Door should start closed";

    doorMgr.setDoorState(1, true);  // Open the door
    EXPECT_TRUE(door->isOpen || door->isAnimating) << "Door should be open or animating";
}

// Test getDoorSceneNodes for collision
TEST_F(DoorManagerGraphicsTest, GetDoorSceneNodes) {
    std::string zonePath = eqClientPath_ + "/qeynos2.s3d";
    if (!fileExists(zonePath)) {
        GTEST_SKIP() << "Zone file not found: " << zonePath;
    }

    S3DLoader loader;
    ASSERT_TRUE(loader.loadZone(zonePath));
    auto zone = loader.getZone();

    DoorManager doorMgr(smgr_, driver_);
    doorMgr.setZone(zone);

    // Create doors
    doorMgr.createDoor(1, "DOOR1", 0.0f, 0.0f, 0.0f, 0.0f, 0, 100, 0, false);
    doorMgr.createDoor(2, "DOOR2", 10.0f, 0.0f, 0.0f, 0.0f, 0, 100, 0, false);

    auto nodes = doorMgr.getDoorSceneNodes();

    // Should have scene nodes for collision detection
    EXPECT_EQ(nodes.size(), 2) << "Should have 2 door scene nodes for collision";
}

#endif // EQT_HAS_GRAPHICS

// ============================================================================
// Main
// ============================================================================

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
