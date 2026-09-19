// WLD comprehensive zone verification — loads all pre-Luclin zones plus
// character models and object files to verify the parser handles the
// full asset set without crashing.
// Split from test_wld.cpp (T11, 2026-07-29) — pure mechanical move.
// See test_wld_zones.cpp for IntegrationTest (the sibling suite).

#include <gtest/gtest.h>
#include <cstdint>
#include <string>
#include <fstream>
#include <cstdlib>
#include <iostream>
#include <iomanip>

#include "client/graphics/eq/wld_loader.h"
#include "client/graphics/eq/pfs.h"

#include "asset_requirement.h"

using namespace EQT::Graphics;

class ComprehensiveZoneTest : public ::testing::Test {
protected:
    std::string eqClientPath_;

    void SetUp() override {
        const char* envPath = std::getenv("EQ_CLIENT_PATH");
        if (envPath) {
            eqClientPath_ = envPath;
        } else {
            eqClientPath_ = EQ_CLIENT_DATA_PATH;
        }
        // B815: the aggregate cases (ClassicZones, OverallSummary) assert on
        // a count of loaded zones, so a missing asset root was a hard FAIL
        // while every per-zone sibling skipped cleanly. Root-level gate:
        // skip the suite (or fail under WILLEQ_REQUIRE_ASSETS=1).
        eqt::test::requireEqClientAssets(eqClientPath_);
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
