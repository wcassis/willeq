/**
 * generate_surface_map - Creates a pre-computed surface type map for a zone
 *
 * This tool processes zone WLD data to create a grid-based surface type map
 * that can be loaded at runtime for fast detail placement decisions.
 *
 * Usage: generate_surface_map <eq_client_path> <zone_name> [output_path]
 * Example: generate_surface_map /path/to/EverQuest qeynos2
 */

#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <algorithm>
#include <cmath>
#include <chrono>
#include <filesystem>

#include "client/graphics/eq/s3d_loader.h"
#include "client/graphics/eq/wld_loader.h"
#include "client/graphics/eq/pfs.h"

namespace fs = std::filesystem;

// Surface types matching detail_types.h and surface_map.h RawSurfaceType
// NOTE: Values must match RawSurfaceType in surface_map.h for binary compatibility
enum class SurfaceType : uint8_t {
    Unknown     = 0,
    Grass       = 1,
    Dirt        = 2,
    Stone       = 3,
    Brick       = 4,
    Wood        = 5,
    Sand        = 6,
    Snow        = 7,
    Water       = 8,
    Lava        = 9,
    Jungle      = 10,  // Kunark tropical vegetation
    Swamp       = 11,  // Wetlands, marshes
    Rock        = 12   // Natural rocky terrain (not man-made)
};

const char* surfaceTypeName(SurfaceType type) {
    switch (type) {
        case SurfaceType::Unknown: return "Unknown";
        case SurfaceType::Grass:   return "Grass";
        case SurfaceType::Dirt:    return "Dirt";
        case SurfaceType::Stone:   return "Stone";
        case SurfaceType::Brick:   return "Brick";
        case SurfaceType::Wood:    return "Wood";
        case SurfaceType::Sand:    return "Sand";
        case SurfaceType::Snow:    return "Snow";
        case SurfaceType::Water:   return "Water";
        case SurfaceType::Lava:    return "Lava";
        case SurfaceType::Jungle:  return "Jungle";
        case SurfaceType::Swamp:   return "Swamp";
        case SurfaceType::Rock:    return "Rock";
        default: return "Unknown";
    }
}

// Classify texture name to surface type
// CONSERVATIVE approach: only classify textures we're confident are GROUND surfaces
// Unknown textures will be skipped by the detail system
SurfaceType classifyTexture(const std::string& textureName, const std::string& zoneName = "") {
    if (textureName.empty()) {
        // Empty textures are typically outdoor terrain mesh - default to grass
        return SurfaceType::Grass;
    }

    // Convert to lowercase
    std::string name = textureName;
    std::transform(name.begin(), name.end(), name.begin(), ::tolower);

    // Helper to check if name starts with a prefix
    auto startsWith = [&name](const char* prefix) {
        return name.rfind(prefix, 0) == 0;
    };

    // === EXCLUSIONS FIRST - textures that are NOT walkable ground ===

    // Water/lava - exclude from detail placement
    if (name.find("water") != std::string::npos ||
        name.find("falls") != std::string::npos ||
        name.find("fount") != std::string::npos ||
        name.find("agua") != std::string::npos) {
        return SurfaceType::Water;
    }
    if (name.find("lava") != std::string::npos ||
        name.find("magma") != std::string::npos) {
        return SurfaceType::Lava;
    }

    // Skip non-ground textures (walls, ceilings, windows, roofs, signs, etc.)
    // These should be filtered by slope anyway, but mark them Unknown to be safe
    if (name.find("wall") != std::string::npos ||
        name.find("waal") != std::string::npos ||
        name.find("wail") != std::string::npos ||
        name.find("wafl") != std::string::npos ||
        name.find("ceil") != std::string::npos ||
        name.find("roof") != std::string::npos ||
        name.find("win") != std::string::npos ||    // windows
        name.find("sign") != std::string::npos ||
        name.find("door") != std::string::npos ||
        name.find("cyw") != std::string::npos ||    // city wall decorations
        name.find("leav") != std::string::npos ||   // leaves/eaves on buildings
        name.find("eave") != std::string::npos ||
        name.find("side") != std::string::npos ||   // building sides
        name.find("bar") != std::string::npos) {    // bar walls/decorations
        return SurfaceType::Unknown;
    }

    // === BIOME-SPECIFIC GROUND TEXTURES ===
    // Check these BEFORE generic grass to properly categorize expansion content

    // Swamp/marsh textures (Innothule, Feerrott, Kunark swamps)
    // Check before grass since swamp areas shouldn't have temperate grass
    if (name.find("swamp") != std::string::npos ||
        name.find("marsh") != std::string::npos ||
        name.find("bog") != std::string::npos ||
        name.find("muck") != std::string::npos ||
        name.find("slime") != std::string::npos ||
        name.find("sludge") != std::string::npos) {
        return SurfaceType::Swamp;
    }

    // Jungle textures (Kunark tropical zones)
    // Check before grass since jungle shouldn't have temperate grass
    if (name.find("jungle") != std::string::npos ||
        name.find("fern") != std::string::npos ||
        name.find("palm") != std::string::npos ||
        name.find("tropical") != std::string::npos ||
        startsWith("ej") ||           // Emerald Jungle prefix
        startsWith("sbjung")) {       // Stonebrunt jungle
        return SurfaceType::Jungle;
    }

    // Firiona Vie grass is tropical (Kunark) - check for "fir" prefix but not "fire"
    if ((startsWith("fir") || name.find("firgrass") != std::string::npos) &&
        name.find("fire") == std::string::npos) {
        return SurfaceType::Jungle;
    }

    // Snow/ice textures (Velious zones)
    // Expanded patterns for Velious zone prefixes
    if (name.find("snow") != std::string::npos ||
        name.find("ice") != std::string::npos ||
        name.find("frost") != std::string::npos ||
        name.find("frozen") != std::string::npos ||
        name.find("icsnow") != std::string::npos ||
        startsWith("gdr") ||          // Great Divide prefix (gdrocksnow, etc.)
        startsWith("vel") ||          // Velketor prefix
        startsWith("wice") ||         // Velious water ice
        startsWith("thu")) {          // Thurgadin prefix
        return SurfaceType::Snow;
    }

    // === GENERIC GROUND TEXTURES ===

    // Grass - explicit grass textures (includes xgrasdir = grass/dirt transition)
    if (name.find("grass") != std::string::npos ||
        name.find("gras") != std::string::npos ||   // catches xgrasdir, kwgras1, etc.
        name.find("lawn") != std::string::npos ||
        name.find("turf") != std::string::npos) {
        return SurfaceType::Grass;
    }

    // Natural rock/cliff terrain (NOT man-made stone floors)
    // Check for rock WITHOUT floor/tile indicators
    if ((name.find("rock") != std::string::npos ||
         name.find("cliff") != std::string::npos ||
         name.find("boulder") != std::string::npos ||
         name.find("mountain") != std::string::npos ||
         name.find("crag") != std::string::npos) &&
        name.find("floor") == std::string::npos &&
        name.find("flor") == std::string::npos &&
        name.find("tile") == std::string::npos) {
        return SurfaceType::Rock;
    }

    // Cobblestone/paved streets - the main walkable stone in cities
    if (name.find("coble") != std::string::npos ||
        name.find("cobble") != std::string::npos ||
        name.find("pave") != std::string::npos) {
        return SurfaceType::Stone;
    }

    // Explicit floor textures (man-made)
    if (name.find("floor") != std::string::npos ||
        name.find("flor") != std::string::npos ||
        name.find("flr") != std::string::npos) {
        return SurfaceType::Stone;
    }

    // Tile textures (indoor floors)
    if (name.find("tile") != std::string::npos ||
        name.find("undrtil") != std::string::npos) {  // underground tile
        return SurfaceType::Stone;
    }

    // Dirt/mud paths
    if (name.find("dirt") != std::string::npos ||
        name.find("xdrt") != std::string::npos ||
        name.find("mud") != std::string::npos ||
        name.find("ground") != std::string::npos) {
        return SurfaceType::Dirt;
    }

    // Wood floors/decks/jambs
    if (name.find("deck") != std::string::npos ||
        name.find("wdfloor") != std::string::npos ||
        name.find("wood") != std::string::npos ||
        name.find("jam") != std::string::npos) {    // door jamb/frame
        return SurfaceType::Wood;
    }

    // Sand/beach
    if (name.find("sand") != std::string::npos ||
        name.find("beach") != std::string::npos ||
        name.find("desert") != std::string::npos ||
        name.find("dune") != std::string::npos) {
        return SurfaceType::Sand;
    }

    // Brick explicitly named (rare as ground, but possible)
    if (name.find("brick") != std::string::npos) {
        return SurfaceType::Brick;
    }

    // === KUNARK ZONE PREFIXES ===
    // Many Kunark textures use zone-specific prefixes
    // These are outdoor ground textures that should get appropriate biomes

    // Burning Woods - volcanic/ash ground
    if (startsWith("bw") && (name.find("ground") != std::string::npos ||
                             name.find("grass") != std::string::npos)) {
        return SurfaceType::Dirt;  // Ash/volcanic dirt
    }

    // Dreadlands - barren rock
    if (startsWith("dread") || startsWith("drd")) {
        return SurfaceType::Rock;
    }

    // Field of Bone - bone/dirt ground
    if (startsWith("fob") || name.find("bone") != std::string::npos) {
        return SurfaceType::Dirt;
    }

    // === UNKNOWN - don't assume anything about unrecognized textures ===
    // This is IMPORTANT - we'd rather skip a spot than place grass on stone
    return SurfaceType::Unknown;
}

// Surface map file format header
struct SurfaceMapHeader {
    char magic[4] = {'S', 'M', 'A', 'P'};
    uint32_t version = 1;
    float cellSize;
    float minX, minY, minZ;
    float maxX, maxY, maxZ;
    uint32_t gridWidth;
    uint32_t gridHeight;
    uint32_t cellCount;
};

// Point-in-triangle test (2D, ignoring height)
bool pointInTriangle2D(float px, float py,
                       float ax, float ay, float bx, float by, float cx, float cy) {
    float v0x = cx - ax, v0y = cy - ay;
    float v1x = bx - ax, v1y = by - ay;
    float v2x = px - ax, v2y = py - ay;

    float dot00 = v0x * v0x + v0y * v0y;
    float dot01 = v0x * v1x + v0y * v1y;
    float dot02 = v0x * v2x + v0y * v2y;
    float dot11 = v1x * v1x + v1y * v1y;
    float dot12 = v1x * v2x + v1y * v2y;

    float invDenom = 1.0f / (dot00 * dot11 - dot01 * dot01);
    float u = (dot11 * dot02 - dot01 * dot12) * invDenom;
    float v = (dot00 * dot12 - dot01 * dot02) * invDenom;

    return (u >= 0) && (v >= 0) && (u + v <= 1);
}

int main(int argc, char* argv[]) {
    if (argc < 3) {
        std::cerr << "Usage: " << argv[0] << " <eq_client_path> <zone_name> [output_path] [cell_size] [--verbose]\n";
        std::cerr << "Example: " << argv[0] << " /path/to/EverQuest qeynos2\n";
        std::cerr << "         " << argv[0] << " /path/to/EverQuest qeynos2 qeynos2.map 2.0 --verbose\n";
        std::cerr << "\nOptions:\n";
        std::cerr << "  cell_size: Grid resolution in units (default: 2.0)\n";
        std::cerr << "  --verbose: Show detailed diagnostics for Unknown cells\n";
        return 1;
    }

    std::string eqPath = argv[1];
    std::string zoneName = argv[2];
    std::string outputPath = (argc > 3) ? argv[3] : (zoneName + "_surface.map");
    float cellSize = (argc > 4) ? std::stof(argv[4]) : 2.0f;
    bool verbose = false;
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "--verbose") {
            verbose = true;
        }
    }

    std::cout << "=== Surface Map Generator ===\n";
    std::cout << "EQ Path: " << eqPath << "\n";
    std::cout << "Zone: " << zoneName << "\n";
    std::cout << "Output: " << outputPath << "\n";
    std::cout << "Cell Size: " << cellSize << " units\n\n";

    // Find zone S3D file
    std::string s3dPath = eqPath + "/" + zoneName + ".s3d";
    if (!fs::exists(s3dPath)) {
        // Try lowercase
        std::string lowerZone = zoneName;
        std::transform(lowerZone.begin(), lowerZone.end(), lowerZone.begin(), ::tolower);
        s3dPath = eqPath + "/" + lowerZone + ".s3d";
    }

    if (!fs::exists(s3dPath)) {
        std::cerr << "Error: Could not find zone file: " << s3dPath << "\n";
        return 1;
    }

    std::cout << "Loading zone from: " << s3dPath << "\n";

    auto startTime = std::chrono::high_resolution_clock::now();

    // Load zone using S3DLoader
    EQT::Graphics::S3DLoader loader;
    if (!loader.loadZone(s3dPath)) {
        std::cerr << "Error: Failed to load zone\n";
        return 1;
    }

    auto zone = loader.getZone();
    if (!zone || !zone->geometry) {
        std::cerr << "Error: Zone has no geometry\n";
        return 1;
    }

    auto& geom = *zone->geometry;
    std::cout << "Loaded geometry: " << geom.vertices.size() << " vertices, "
              << geom.triangles.size() << " triangles, "
              << geom.textureNames.size() << " textures\n";

    // Count texture index usage in triangles
    std::map<uint32_t, size_t> texUsage;
    for (const auto& tri : geom.triangles) {
        texUsage[tri.textureIndex]++;
    }

    // Print texture names and usage for debugging
    std::cout << "\nTexture names in zone geometry (sorted by usage):\n";
    std::vector<std::pair<uint32_t, size_t>> sortedUsage(texUsage.begin(), texUsage.end());
    std::sort(sortedUsage.begin(), sortedUsage.end(),
              [](const auto& a, const auto& b) { return a.second > b.second; });

    size_t emptyCount = 0;
    for (size_t i = 0; i < sortedUsage.size() && i < 30; ++i) {
        uint32_t texIdx = sortedUsage[i].first;
        size_t count = sortedUsage[i].second;
        std::string origName = (texIdx < geom.textureNames.size()) ? geom.textureNames[texIdx] : "";
        std::string displayName = origName.empty() ? "<empty>" : origName;
        if (origName.empty()) {
            emptyCount += count;
        }
        std::cout << "  [" << texIdx << "] " << displayName << " (" << count << " tris) -> "
                  << surfaceTypeName(classifyTexture(origName, zoneName)) << "\n";
    }
    std::cout << "\nTotal triangles with empty textures: " << emptyCount << "/" << geom.triangles.size()
              << " (" << (100.0 * emptyCount / geom.triangles.size()) << "%)\n\n";

    // Get BSP tree for water/lava regions
    std::shared_ptr<EQT::Graphics::BspTree> bspTree;
    if (zone->wldLoader) {
        bspTree = zone->wldLoader->getBspTree();
        if (bspTree) {
            std::cout << "BSP tree loaded with " << bspTree->regions.size() << " regions\n";
        }
    }

    // Calculate grid bounds (EQ coordinates: X, Y horizontal, Z up)
    float minX = geom.minX, maxX = geom.maxX;
    float minY = geom.minY, maxY = geom.maxY;

    // Add small margin
    minX -= cellSize;
    minY -= cellSize;
    maxX += cellSize;
    maxY += cellSize;

    uint32_t gridWidth = static_cast<uint32_t>(std::ceil((maxX - minX) / cellSize));
    uint32_t gridHeight = static_cast<uint32_t>(std::ceil((maxY - minY) / cellSize));
    uint32_t totalCells = gridWidth * gridHeight;

    std::cout << "Grid: " << gridWidth << " x " << gridHeight << " = " << totalCells << " cells\n";
    std::cout << "Bounds: X[" << minX << ", " << maxX << "] Y[" << minY << ", " << maxY << "]\n";

    // Allocate grid
    std::vector<SurfaceType> surfaceGrid(totalCells, SurfaceType::Unknown);
    std::vector<float> heightGrid(totalCells, -10000.0f);

    // Pre-compute triangle AABBs for faster lookup
    struct TriangleAABB {
        float minX, minY, maxX, maxY;
        float minZ, maxZ;
        size_t index;
    };
    std::vector<TriangleAABB> triangleAABBs;
    triangleAABBs.reserve(geom.triangles.size());

    for (size_t i = 0; i < geom.triangles.size(); ++i) {
        const auto& tri = geom.triangles[i];
        if (tri.v1 >= geom.vertices.size() ||
            tri.v2 >= geom.vertices.size() ||
            tri.v3 >= geom.vertices.size()) {
            continue;
        }

        const auto& v1 = geom.vertices[tri.v1];
        const auto& v2 = geom.vertices[tri.v2];
        const auto& v3 = geom.vertices[tri.v3];

        TriangleAABB aabb;
        aabb.minX = std::min({v1.x, v2.x, v3.x});
        aabb.maxX = std::max({v1.x, v2.x, v3.x});
        aabb.minY = std::min({v1.y, v2.y, v3.y});
        aabb.maxY = std::max({v1.y, v2.y, v3.y});
        aabb.minZ = std::min({v1.z, v2.z, v3.z});
        aabb.maxZ = std::max({v1.z, v2.z, v3.z});
        aabb.index = i;
        triangleAABBs.push_back(aabb);
    }

    std::cout << "Pre-computed " << triangleAABBs.size() << " triangle AABBs\n";

    // Process grid cells
    std::cout << "Processing grid cells...\n";
    if (verbose) {
        std::cout << "Verbose mode enabled - will show diagnostics for cells with issues\n";
    }
    size_t processedCells = 0;
    size_t foundCells = 0;

    // Statistics
    std::map<SurfaceType, size_t> surfaceCounts;

    // Diagnostic counters
    size_t noTrianglesInAABB = 0;     // No triangles at all near the cell
    size_t allFailedSlope = 0;         // Had triangles but all failed slope check
    size_t allFailedPointInTri = 0;    // Had triangles passing slope but failed point test
    size_t classifiedUnknown = 0;      // Triangle found, but texture classified as Unknown

    // For verbose mode: track samples of problematic cells
    std::vector<std::tuple<float, float, std::string, float>> slopeFailSamples;
    std::vector<std::tuple<float, float, std::string>> unknownTexSamples;
    const size_t maxSamples = 20;

    for (uint32_t gy = 0; gy < gridHeight; ++gy) {
        for (uint32_t gx = 0; gx < gridWidth; ++gx) {
            float worldX = minX + (gx + 0.5f) * cellSize;
            float worldY = minY + (gy + 0.5f) * cellSize;

            // Find triangles containing this XY point
            // Look for the highest ground triangle (to avoid underground surfaces)
            float bestZ = -10000.0f;
            size_t bestTriIdx = SIZE_MAX;

            // Diagnostic tracking
            int aabbHits = 0;
            int slopeFailCount = 0;
            int pointFailCount = 0;
            std::string lastSlopeFailTex;
            float lastSlopeFailNz = 0;

            for (const auto& aabb : triangleAABBs) {
                // Quick AABB rejection
                if (worldX < aabb.minX || worldX > aabb.maxX ||
                    worldY < aabb.minY || worldY > aabb.maxY) {
                    continue;
                }

                aabbHits++;

                const auto& tri = geom.triangles[aabb.index];
                const auto& v1 = geom.vertices[tri.v1];
                const auto& v2 = geom.vertices[tri.v2];
                const auto& v3 = geom.vertices[tri.v3];

                // Calculate triangle normal to check if it's a ground surface
                // Ground surfaces should have normal pointing mostly up (positive Z in EQ coords)
                float ux = v2.x - v1.x, uy = v2.y - v1.y, uz = v2.z - v1.z;
                float vx = v3.x - v1.x, vy = v3.y - v1.y, vz = v3.z - v1.z;
                float nx = uy * vz - uz * vy;
                float ny = uz * vx - ux * vz;
                float nz = ux * vy - uy * vx;
                float len = std::sqrt(nx*nx + ny*ny + nz*nz);
                if (len > 0.0001f) {
                    nz /= len;  // Normalize Z component
                }

                // Only consider surfaces with normal pointing mostly up (walkable ground)
                // nz > 0.5 means slope less than 60 degrees from horizontal
                if (nz < 0.5f) {
                    slopeFailCount++;
                    // Track the texture for diagnostics
                    if (tri.textureIndex < geom.textureNames.size()) {
                        lastSlopeFailTex = geom.textureNames[tri.textureIndex];
                        lastSlopeFailNz = nz;
                    }
                    continue;  // Skip walls, ceilings, steep slopes
                }

                // Point-in-triangle test (XY plane)
                if (pointInTriangle2D(worldX, worldY, v1.x, v1.y, v2.x, v2.y, v3.x, v3.y)) {
                    // Calculate Z at this XY point using barycentric interpolation
                    // Simplified: use average Z for now
                    float avgZ = (v1.z + v2.z + v3.z) / 3.0f;

                    // Take highest triangle (ground surface, not ceiling)
                    if (avgZ > bestZ) {
                        bestZ = avgZ;
                        bestTriIdx = aabb.index;
                    }
                } else {
                    pointFailCount++;
                }
            }

            size_t cellIdx = gy * gridWidth + gx;

            if (bestTriIdx != SIZE_MAX) {
                const auto& tri = geom.triangles[bestTriIdx];

                // Get texture name
                std::string texName;
                if (tri.textureIndex < geom.textureNames.size()) {
                    texName = geom.textureNames[tri.textureIndex];
                }

                // Classify surface type
                SurfaceType surfType = classifyTexture(texName, zoneName);

                // Check BSP for water/lava override
                if (bspTree) {
                    auto region = bspTree->findRegionForPoint(worldX, worldY, bestZ);
                    if (region) {
                        for (auto rtype : region->regionTypes) {
                            if (rtype == EQT::Graphics::RegionType::Water ||
                                rtype == EQT::Graphics::RegionType::WaterBlockLOS ||
                                rtype == EQT::Graphics::RegionType::FreezingWater) {
                                surfType = SurfaceType::Water;
                                break;
                            }
                            if (rtype == EQT::Graphics::RegionType::Lava) {
                                surfType = SurfaceType::Lava;
                                break;
                            }
                        }
                    }
                }

                surfaceGrid[cellIdx] = surfType;
                heightGrid[cellIdx] = bestZ;
                foundCells++;
                surfaceCounts[surfType]++;

                // Track unknown textures
                if (surfType == SurfaceType::Unknown) {
                    classifiedUnknown++;
                    if (unknownTexSamples.size() < maxSamples) {
                        unknownTexSamples.emplace_back(worldX, worldY, texName);
                    }
                }
            } else {
                // Diagnostic: why did we find no triangle?
                if (aabbHits == 0) {
                    noTrianglesInAABB++;
                } else if (slopeFailCount > 0 && pointFailCount == 0) {
                    allFailedSlope++;
                    if (verbose && slopeFailSamples.size() < maxSamples) {
                        slopeFailSamples.emplace_back(worldX, worldY, lastSlopeFailTex, lastSlopeFailNz);
                    }
                } else {
                    allFailedPointInTri++;
                }
            }

            processedCells++;
            if (processedCells % 10000 == 0) {
                std::cout << "  Processed " << processedCells << "/" << totalCells
                          << " (" << (100 * processedCells / totalCells) << "%)\r" << std::flush;
            }
        }
    }
    std::cout << "\n";

    auto endTime = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);

    std::cout << "\n=== Results ===\n";
    std::cout << "Processing time: " << duration.count() << " ms\n";
    std::cout << "Cells with ground: " << foundCells << "/" << totalCells
              << " (" << (100 * foundCells / totalCells) << "%)\n";

    std::cout << "\nSurface type distribution:\n";
    for (const auto& [type, count] : surfaceCounts) {
        float pct = 100.0f * count / foundCells;
        std::cout << "  " << surfaceTypeName(type) << ": " << count << " (" << pct << "%)\n";
    }

    // Diagnostic summary
    size_t emptyCells = totalCells - foundCells;
    std::cout << "\n=== Diagnostic Summary ===\n";
    std::cout << "Empty cells (no ground found): " << emptyCells << "\n";
    std::cout << "  - No triangles in AABB: " << noTrianglesInAABB << "\n";
    std::cout << "  - All triangles failed slope check: " << allFailedSlope << "\n";
    std::cout << "  - All triangles failed point-in-tri: " << allFailedPointInTri << "\n";
    std::cout << "Cells with Unknown texture: " << classifiedUnknown << "\n";

    if (verbose && !slopeFailSamples.empty()) {
        std::cout << "\nSample cells where all triangles failed slope check:\n";
        for (const auto& [x, y, tex, nz] : slopeFailSamples) {
            std::cout << "  (" << x << ", " << y << ") tex='" << tex << "' nz=" << nz << "\n";
        }
    }

    if (verbose && !unknownTexSamples.empty()) {
        std::cout << "\nSample cells with Unknown texture classification:\n";
        for (const auto& [x, y, tex] : unknownTexSamples) {
            std::cout << "  (" << x << ", " << y << ") tex='" << tex << "'\n";
        }
    }

    // Write output file
    std::cout << "\nWriting surface map to: " << outputPath << "\n";

    std::ofstream outFile(outputPath, std::ios::binary);
    if (!outFile) {
        std::cerr << "Error: Could not create output file\n";
        return 1;
    }

    // Write header
    SurfaceMapHeader header;
    header.cellSize = cellSize;
    header.minX = minX;
    header.minY = minY;
    header.minZ = geom.minZ;
    header.maxX = maxX;
    header.maxY = maxY;
    header.maxZ = geom.maxZ;
    header.gridWidth = gridWidth;
    header.gridHeight = gridHeight;
    header.cellCount = totalCells;

    outFile.write(reinterpret_cast<const char*>(&header), sizeof(header));

    // Write surface types (1 byte per cell)
    outFile.write(reinterpret_cast<const char*>(surfaceGrid.data()), surfaceGrid.size());

    // Write height grid (4 bytes per cell)
    outFile.write(reinterpret_cast<const char*>(heightGrid.data()), heightGrid.size() * sizeof(float));

    outFile.close();

    size_t fileSize = fs::file_size(outputPath);
    std::cout << "Output file size: " << fileSize << " bytes\n";
    std::cout << "Done!\n";

    return 0;
}
