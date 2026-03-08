// Tool to audit exact polygon counts visible from a specific BSP region
// Used to validate that the renderer is culling correctly

#include <iostream>
#include <iomanip>
#include <map>
#include <set>
#include <algorithm>
#include <cmath>
#include "client/graphics/eq/wld_loader.h"
#include "client/graphics/eq/placeable.h"
#include "common/logging.h"

using namespace EQT::Graphics;

// Compute AABB for a region's geometry
struct AABB {
    float minX = 1e30f, minY = 1e30f, minZ = 1e30f;
    float maxX = -1e30f, maxY = -1e30f, maxZ = -1e30f;
    void expand(float x, float y, float z) {
        minX = std::min(minX, x); minY = std::min(minY, y); minZ = std::min(minZ, z);
        maxX = std::max(maxX, x); maxY = std::max(maxY, y); maxZ = std::max(maxZ, z);
    }
    bool valid() const { return minX < maxX; }
};

int main(int argc, char** argv) {
    if (argc < 4) {
        std::cerr << "Usage: " << argv[0] << " <s3d_file> <region_index> <eq_client_path>" << std::endl;
        std::cerr << "  region_index: 0-based BSP region index (e.g. 463)" << std::endl;
        std::cerr << "Example: " << argv[0] << " /path/to/qeynos2.s3d 463 /path/to/EverQuest" << std::endl;
        return 1;
    }

    std::string archivePath = argv[1];
    size_t targetRegion = std::stoul(argv[2]);
    std::string eqPath = argv[3];

    // --- PART 1: Zone geometry ---
    std::cout << "=== ZONE GEOMETRY AUDIT ===" << std::endl;

    WldLoader loader;
    if (!loader.parseFromArchive(archivePath, "qeynos2.wld")) {
        std::cerr << "Failed to parse zone WLD" << std::endl;
        return 1;
    }

    auto bspTree = loader.getBspTree();
    if (!bspTree) {
        std::cerr << "No BSP tree" << std::endl;
        return 1;
    }

    std::cout << "Total BSP regions: " << bspTree->regions.size() << std::endl;
    std::cout << "Total geometries: " << loader.getGeometries().size() << std::endl;

    // Count total zone polys
    size_t totalZoneVerts = 0, totalZoneTris = 0;
    for (const auto& geom : loader.getGeometries()) {
        totalZoneVerts += geom->vertices.size();
        totalZoneTris += geom->triangles.size();
    }
    std::cout << "Total zone vertices: " << totalZoneVerts << std::endl;
    std::cout << "Total zone triangles: " << totalZoneTris << std::endl;

    // Check target region
    if (targetRegion >= bspTree->regions.size()) {
        std::cerr << "Region " << targetRegion << " out of range (max " << bspTree->regions.size() - 1 << ")" << std::endl;
        return 1;
    }

    const auto& playerRegion = bspTree->regions[targetRegion];
    std::cout << "\n--- Player Region " << targetRegion << " ---" << std::endl;
    std::cout << "  containsPolygons: " << playerRegion->containsPolygons << std::endl;
    std::cout << "  meshReference: " << playerRegion->meshReference << std::endl;
    std::cout << "  PVS size: " << playerRegion->visibleRegions.size() << std::endl;

    auto playerGeom = loader.getGeometryForRegion(targetRegion);
    if (playerGeom) {
        std::cout << "  vertices: " << playerGeom->vertices.size() << std::endl;
        std::cout << "  triangles: " << playerGeom->triangles.size() << std::endl;

        // Compute AABB (in WLD Z-up coords)
        AABB box;
        for (const auto& v : playerGeom->vertices) {
            box.expand(v.x, v.y, v.z);
        }
        if (box.valid()) {
            std::cout << "  AABB: (" << box.minX << "," << box.minY << "," << box.minZ
                      << ") to (" << box.maxX << "," << box.maxY << "," << box.maxZ << ")" << std::endl;
        }
    } else {
        std::cout << "  (no geometry)" << std::endl;
    }

    // PVS-visible regions
    size_t pvsVisibleCount = 0;
    size_t pvsVisibleWithGeom = 0;
    size_t pvsVisibleVerts = 0;
    size_t pvsVisibleTris = 0;
    std::vector<std::pair<size_t, size_t>> regionTriCounts; // (region_idx, tri_count)

    for (size_t i = 0; i < playerRegion->visibleRegions.size(); ++i) {
        if (!playerRegion->visibleRegions[i]) continue;
        pvsVisibleCount++;

        auto geom = loader.getGeometryForRegion(i);
        if (geom && !geom->triangles.empty()) {
            pvsVisibleWithGeom++;
            pvsVisibleVerts += geom->vertices.size();
            pvsVisibleTris += geom->triangles.size();
            regionTriCounts.push_back({i, geom->triangles.size()});
        }
    }

    // Sort by tri count descending
    std::sort(regionTriCounts.begin(), regionTriCounts.end(),
              [](const auto& a, const auto& b) { return a.second > b.second; });

    std::cout << "\n--- PVS Visibility from Region " << targetRegion << " ---" << std::endl;
    std::cout << "  PVS-visible regions: " << pvsVisibleCount << " / " << bspTree->regions.size() << std::endl;
    std::cout << "  PVS-visible with geometry: " << pvsVisibleWithGeom << std::endl;
    std::cout << "  PVS-visible total vertices: " << pvsVisibleVerts << std::endl;
    std::cout << "  PVS-visible total triangles: " << pvsVisibleTris << std::endl;
    std::cout << "  Culling ratio: " << std::fixed << std::setprecision(1)
              << (1.0 - (double)pvsVisibleTris / totalZoneTris) * 100.0 << "% culled by PVS alone" << std::endl;

    // Top 20 biggest PVS-visible regions
    std::cout << "\n--- Top 20 PVS-Visible Regions by Triangle Count ---" << std::endl;
    for (size_t i = 0; i < std::min(regionTriCounts.size(), (size_t)20); ++i) {
        auto geom = loader.getGeometryForRegion(regionTriCounts[i].first);
        AABB box;
        if (geom) {
            for (const auto& v : geom->vertices) {
                box.expand(v.x, v.y, v.z);
            }
        }
        std::cout << "  R" << regionTriCounts[i].first
                  << ": " << regionTriCounts[i].second << " tris";
        if (box.valid()) {
            std::cout << "  AABB (" << std::fixed << std::setprecision(0)
                      << box.minX << "," << box.minY << "," << box.minZ
                      << ")-(" << box.maxX << "," << box.maxY << "," << box.maxZ << ")";
        }
        std::cout << std::endl;
    }

    // Find all nearby regions (within 50 units of player position)
    // Player client pos: (262.16, 293.53, 17.05) — in EQ Z-up coords
    float playerX = 262.16f, playerY = 293.53f, playerZ = 17.05f;
    std::cout << "\n--- Regions Near Player (EQ coords " << playerX << "," << playerY << "," << playerZ << ") ---" << std::endl;

    // Use BSP tree to find player's region
    size_t foundRegion = bspTree->findRegionIndexForPoint(playerX, playerY, playerZ);
    std::cout << "  BSP tree lookup for player position: region " << foundRegion << std::endl;

    // Also check nearby regions by AABB proximity
    std::cout << "\n--- All Regions with Geometry Near Player (AABB within 30 units) ---" << std::endl;
    size_t nearbyTotalTris = 0;
    for (size_t i = 0; i < bspTree->regions.size(); ++i) {
        auto geom = loader.getGeometryForRegion(i);
        if (!geom || geom->triangles.empty()) continue;

        AABB box;
        for (const auto& v : geom->vertices) {
            box.expand(v.x, v.y, v.z);
        }

        // Check if AABB is within 30 units of player (EQ Z-up coords)
        float dx = std::max(0.0f, std::max(box.minX - playerX, playerX - box.maxX));
        float dy = std::max(0.0f, std::max(box.minY - playerY, playerY - box.maxY));
        float dz = std::max(0.0f, std::max(box.minZ - playerZ, playerZ - box.maxZ));
        float dist = std::sqrt(dx*dx + dy*dy + dz*dz);

        if (dist < 30.0f) {
            bool pvsVisible = (i < playerRegion->visibleRegions.size() &&
                               playerRegion->visibleRegions[i]);
            std::cout << "  R" << i << ": " << geom->triangles.size() << " tris, "
                      << geom->vertices.size() << " verts, dist=" << std::fixed << std::setprecision(1) << dist
                      << " PVS=" << (pvsVisible ? "YES" : "NO")
                      << " AABB (" << std::setprecision(0)
                      << box.minX << "," << box.minY << "," << box.minZ
                      << ")-(" << box.maxX << "," << box.maxY << "," << box.maxZ << ")"
                      << std::endl;
            nearbyTotalTris += geom->triangles.size();
        }
    }
    std::cout << "  Total nearby triangles: " << nearbyTotalTris << std::endl;

    // --- PART 2: Entity model poly counts ---
    std::cout << "\n=== ENTITY MODEL POLY COUNTS ===" << std::endl;

    // Player: race 1 (human), gender 1 (female)
    // Pet: Gabann000 (magician earth pet, level 41)
    // NPCs: "human male" and "qeynos citizen" — race 71 (Shenro_Kazpur000 built first as race 71)

    struct ModelInfo { std::string name; int race; std::string archiveHint; };
    std::vector<ModelInfo> models = {
        {"Human Female (player, race 1)", 1, "globalhum_chr.s3d"},
        {"Human Male NPC (race 71)", 71, "globalhum_chr.s3d"},
    };

    // Common pet races for magician earth elemental
    // Race 75 = earth elemental, race 209 = air elemental, etc.
    // Try common pet S3D archives
    std::vector<std::pair<std::string, std::string>> petArchives = {
        {"Earth Elemental (race 75)", "globaleet_chr.s3d"},
        {"Air Elemental (race 209)", "globalaet_chr.s3d"},
        {"Water Elemental (race 76)", "globalwet_chr.s3d"},
        {"Fire Elemental (race 212)", "globalfet_chr.s3d"},
        {"Warder/Pet (race 120)", "globalwer_chr.s3d"},
    };

    for (const auto& model : models) {
        std::string archName = eqPath + "/" + model.archiveHint;
        WldLoader charLoader;
        std::string wldName;
        size_t slashPos = archName.rfind('/');
        std::string baseName = (slashPos != std::string::npos) ? archName.substr(slashPos + 1) : archName;
        size_t dotPos = baseName.rfind(".s3d");
        if (dotPos != std::string::npos) {
            wldName = baseName.substr(0, dotPos) + ".wld";
        }

        if (charLoader.parseFromArchive(archName, wldName)) {
            size_t totalVerts = 0, totalTris = 0;
            for (const auto& geom : charLoader.getGeometries()) {
                totalVerts += geom->vertices.size();
                totalTris += geom->triangles.size();
            }
            std::cout << "  " << model.name << ": "
                      << totalVerts << " vertices, " << totalTris << " triangles"
                      << " [" << charLoader.getGeometries().size() << " meshes]" << std::endl;
        } else {
            std::cout << "  " << model.name << ": FAILED to load " << archName << std::endl;
        }
    }

    // Try pet archives
    for (const auto& pet : petArchives) {
        std::string archName = eqPath + "/" + pet.second;
        WldLoader charLoader;
        std::string wldName;
        size_t slashPos = archName.rfind('/');
        std::string baseName = (slashPos != std::string::npos) ? archName.substr(slashPos + 1) : archName;
        size_t dotPos = baseName.rfind(".s3d");
        if (dotPos != std::string::npos) {
            wldName = baseName.substr(0, dotPos) + ".wld";
        }

        if (charLoader.parseFromArchive(archName, wldName)) {
            size_t totalVerts = 0, totalTris = 0;
            for (const auto& geom : charLoader.getGeometries()) {
                totalVerts += geom->vertices.size();
                totalTris += geom->triangles.size();
            }
            std::cout << "  " << pet.first << ": "
                      << totalVerts << " vertices, " << totalTris << " triangles"
                      << " [" << charLoader.getGeometries().size() << " meshes]" << std::endl;
        }
    }

    // Objects in zone
    std::cout << "\n=== ZONE OBJECTS (from objects.wld) ===" << std::endl;
    WldLoader objLoader;
    if (objLoader.parseFromArchive(archivePath, "objects.wld")) {
        auto placeables = objLoader.getPlaceables();
        std::cout << "Total placeables: " << placeables.size() << std::endl;

        // Find placeables near player position
        size_t nearbyObjTris = 0;
        for (const auto& p : placeables) {
            float dx = p->getX() - playerX;
            float dy = p->getY() - playerY;
            float dz = p->getZ() - playerZ;
            float dist = std::sqrt(dx*dx + dy*dy + dz*dz);
            if (dist < 50.0f) {
                std::cout << "  " << p->getName() << " at (" << std::fixed << std::setprecision(1)
                          << p->getX() << "," << p->getY() << "," << p->getZ() << ") dist=" << dist << std::endl;
                auto defs = objLoader.getObjectDefs();
                auto it = defs.find(p->getName());
                if (it != defs.end()) {
                    for (auto ref : it->second.meshRefs) {
                        auto geom = objLoader.getGeometryByFragmentIndex(ref);
                        if (geom) {
                            std::cout << "    mesh: " << geom->triangles.size() << " tris, "
                                      << geom->vertices.size() << " verts" << std::endl;
                            nearbyObjTris += geom->triangles.size();
                        }
                    }
                }
            }
        }
        std::cout << "  Total nearby object triangles: " << nearbyObjTris << std::endl;
    }

    // --- PART 3: Fallback mesh ---
    std::cout << "\n=== FALLBACK MESH ===" << std::endl;
    size_t fallbackVerts = 0, fallbackTris = 0;
    size_t regionsWithGeom = 0;
    std::set<uint32_t> referencedFrags;
    for (size_t i = 0; i < bspTree->regions.size(); ++i) {
        const auto& r = bspTree->regions[i];
        if (r->containsPolygons && r->meshReference >= 0) {
            referencedFrags.insert(static_cast<uint32_t>(r->meshReference));
            regionsWithGeom++;
        }
    }

    for (const auto& geom : loader.getGeometries()) {
        bool isReferenced = false;
        for (auto frag : referencedFrags) {
            auto g = loader.getGeometryByFragmentIndex(frag);
            if (g == geom) {
                isReferenced = true;
                break;
            }
        }
        if (!isReferenced && !geom->triangles.empty()) {
            fallbackVerts += geom->vertices.size();
            fallbackTris += geom->triangles.size();
            AABB box;
            for (const auto& v : geom->vertices) {
                box.expand(v.x, v.y, v.z);
            }
            std::cout << "  Unreferenced: " << geom->name << " — " << geom->triangles.size() << " tris, "
                      << geom->vertices.size() << " verts";
            if (box.valid()) {
                std::cout << " AABB (" << std::fixed << std::setprecision(0)
                          << box.minX << "," << box.minY << "," << box.minZ
                          << ")-(" << box.maxX << "," << box.maxY << "," << box.maxZ << ")";
            }
            std::cout << std::endl;
        }
    }
    std::cout << "  Total fallback mesh: " << fallbackTris << " triangles, " << fallbackVerts << " vertices" << std::endl;
    std::cout << "  Regions with geometry: " << regionsWithGeom << " / " << bspTree->regions.size() << std::endl;

    std::cout << "\n=== SUMMARY ===" << std::endl;
    std::cout << "If player is in region " << targetRegion << ":" << std::endl;
    std::cout << "  PVS-visible zone triangles: " << pvsVisibleTris << std::endl;
    std::cout << "  Fallback mesh triangles (always drawn): " << fallbackTris << std::endl;
    std::cout << "  Nearby region triangles (<30 units): " << nearbyTotalTris << std::endl;
    std::cout << "  Expected ZONE ONLY total: " << pvsVisibleTris + fallbackTris << " tris" << std::endl;
    std::cout << "  Observed in log: 9413 polys (includes entities, doors, objects)" << std::endl;

    return 0;
}
