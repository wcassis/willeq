// zone_bench_regions.cpp — BSP-region spatial analysis: portals and surface seeds
//
// T387: extracted verbatim from tools/hwcaps/zone_bench_loader.cpp, which was
// 3,280 lines (the 1,500 ceiling M00 enforces tree-wide, and which
// check_file_sizes.sh did not yet scan under tools/ -- board B167). Function
// bodies are unchanged; only the include set and the file boundary are new.
// The public declarations for everything here stay in zone_bench_loader.h,
// which remains this module's single interface header.

#include "zone_bench_loader.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <limits>
#include <cstdlib>
#include <mutex>
#include <set>
#include <unordered_map>

#include "common/logging.h"

#include "client/graphics/eq/s3d_loader.h"
#include "client/graphics/eq/wld_loader.h"
#include "client/graphics/eq/zone_geometry.h"
#include "client/graphics/zone_shader.h"
#include "client/graphics/constrained_renderer_config.h"
#include "client/graphics/constrained_texture_cache.h"
#include "client/graphics/frustum_culler.h"
#include "client/graphics/portal_system.h"
#include "client/zone_bsp.h"

#include "client/graphics/software_occlusion_culler.h"
#include "client/graphics/eq/race_model_loader.h"
#include "client/graphics/eq/animated_mesh_scene_node.h"
#include "client/graphics/eqe1_loader.h"
#include "client/graphics/eqe1_texture_index.h"
#include "client/graphics/load_mem_audit.h"

using namespace EQT::Graphics;

namespace ZoneBench {

// Minimum distance from a point to a line segment (3D)
static float pointToSegmentDistSq(float px, float py, float pz,
                                   float ax, float ay, float az,
                                   float bx, float by, float bz) {
    float abx = bx - ax, aby = by - ay, abz = bz - az;
    float apx = px - ax, apy = py - ay, apz = pz - az;
    float t = (apx * abx + apy * aby + apz * abz) /
              (abx * abx + aby * aby + abz * abz + 1e-12f);
    t = std::max(0.0f, std::min(1.0f, t));
    float cx = ax + t * abx - px, cy = ay + t * aby - py, cz = az + t * abz - pz;
    return cx * cx + cy * cy + cz * cz;
}

// Minimum distance between two line segments (approximate: sample endpoints + midpoint)
static float segmentToSegmentDistSq(float a0x, float a0y, float a0z,
                                     float a1x, float a1y, float a1z,
                                     float b0x, float b0y, float b0z,
                                     float b1x, float b1y, float b1z) {
    float best = pointToSegmentDistSq(a0x, a0y, a0z, b0x, b0y, b0z, b1x, b1y, b1z);
    best = std::min(best, pointToSegmentDistSq(a1x, a1y, a1z, b0x, b0y, b0z, b1x, b1y, b1z));
    best = std::min(best, pointToSegmentDistSq(b0x, b0y, b0z, a0x, a0y, a0z, a1x, a1y, a1z));
    best = std::min(best, pointToSegmentDistSq(b1x, b1y, b1z, a0x, a0y, a0z, a1x, a1y, a1z));
    // Midpoints
    float amx = (a0x + a1x) * 0.5f, amy = (a0y + a1y) * 0.5f, amz = (a0z + a1z) * 0.5f;
    float bmx = (b0x + b1x) * 0.5f, bmy = (b0y + b1y) * 0.5f, bmz = (b0z + b1z) * 0.5f;
    best = std::min(best, pointToSegmentDistSq(amx, amy, amz, b0x, b0y, b0z, b1x, b1y, b1z));
    best = std::min(best, pointToSegmentDistSq(bmx, bmy, bmz, a0x, a0y, a0z, a1x, a1y, a1z));
    return best;
}

void analyzePortals(ZoneBenchData& data, const std::string& csvFile) {
    if (!data.portalSystem || !data.portalSystem->hasPortals()) {
        printf("No portals to analyze.\n");
        return;
    }

    auto& portals = data.portalSystem->getData().portals;
    auto wldLoader = data.zone->wldLoader;
    const float contactThreshold = 3.0f;  // EQ units — edge within 3 units of geometry
    const float contactThreshSq = contactThreshold * contactThreshold;

    printf("\n=== Portal Analysis (%zu portals) ===\n", portals.size());

    // Collect triangle edges from each region for contact testing
    // Cache per-region to avoid reloading
    struct RegionEdges {
        struct Edge { float x0, y0, z0, x1, y1, z1; };
        std::vector<Edge> edges;
    };
    std::unordered_map<size_t, RegionEdges> regionEdgeCache;

    auto getRegionEdges = [&](size_t regionIdx) -> const RegionEdges& {
        auto it = regionEdgeCache.find(regionIdx);
        if (it != regionEdgeCache.end()) return it->second;

        RegionEdges& re = regionEdgeCache[regionIdx];
        auto geom = wldLoader->getGeometryForRegion(regionIdx);
        if (!geom || geom->vertices.empty()) return re;

        for (const auto& tri : geom->triangles) {
            if (tri.v1 >= geom->vertices.size() ||
                tri.v2 >= geom->vertices.size() ||
                tri.v3 >= geom->vertices.size()) continue;

            auto mkEdge = [&](uint32_t vi0, uint32_t vi1) {
                const auto& v0 = geom->vertices[vi0];
                const auto& v1 = geom->vertices[vi1];
                re.edges.push_back({
                    geom->centerX + v0.x, geom->centerY + v0.y, geom->centerZ + v0.z,
                    geom->centerX + v1.x, geom->centerY + v1.y, geom->centerZ + v1.z});
            };
            mkEdge(tri.v1, tri.v2);
            mkEdge(tri.v2, tri.v3);
            mkEdge(tri.v3, tri.v1);
        }
        return re;
    };

    // Analyze each portal
    struct PortalStats {
        size_t idx;
        float area;
        int edgeCount;
        int contactEdges;     // Edges that touch geometry
        float contactRatio;   // contactEdges / edgeCount
        float minEdgeDist;    // Closest geometry distance across all edges
        bool hasFloorContact; // At least one edge near ground level (low Z)
    };
    std::vector<PortalStats> stats;
    stats.reserve(portals.size());

    int analyzed = 0;
    for (size_t pi = 0; pi < portals.size(); ++pi) {
        const auto& portal = portals[pi];
        int nVerts = static_cast<int>(portal.vertexCount());
        if (nVerts < 3) continue;

        const auto& edgesA = getRegionEdges(portal.regionA);
        const auto& edgesB = getRegionEdges(portal.regionB);

        PortalStats ps;
        ps.idx = pi;
        ps.area = portal.area;
        ps.edgeCount = nVerts;
        ps.contactEdges = 0;
        ps.minEdgeDist = 1e9f;
        ps.hasFloorContact = false;

        // For each portal edge, find nearest geometry edge from both adjacent regions
        for (int e = 0; e < nVerts; ++e) {
            int e2 = (e + 1) % nVerts;
            float pe0x = portal.vx(e), pe0y = portal.vy(e), pe0z = portal.vz(e);
            float pe1x = portal.vx(e2), pe1y = portal.vy(e2), pe1z = portal.vz(e2);

            float bestDistSq = 1e18f;

            // Search both adjacent regions
            for (const auto* regionEdges : {&edgesA, &edgesB}) {
                for (const auto& ge : regionEdges->edges) {
                    float d = segmentToSegmentDistSq(
                        pe0x, pe0y, pe0z, pe1x, pe1y, pe1z,
                        ge.x0, ge.y0, ge.z0, ge.x1, ge.y1, ge.z1);
                    if (d < bestDistSq) bestDistSq = d;
                    if (bestDistSq < contactThreshSq) break;  // Early out
                }
                if (bestDistSq < contactThreshSq) break;
            }

            float dist = std::sqrt(bestDistSq);
            if (dist < ps.minEdgeDist) ps.minEdgeDist = dist;
            if (bestDistSq < contactThreshSq) {
                ps.contactEdges++;
                // Check if this contact edge is near ground level
                float edgeMidZ = (pe0z + pe1z) * 0.5f;
                float regionMidZ = 0;
                auto itA = data.regions.find(portal.regionA);
                if (itA != data.regions.end())
                    regionMidZ = (itA->second.eqMinZ + itA->second.eqMaxZ) * 0.5f;
                float bottomZ = (itA != data.regions.end()) ? itA->second.eqMinZ : -999;
                if (edgeMidZ < bottomZ + 10.0f)  // Within 10 units of region bottom
                    ps.hasFloorContact = true;
            }
        }

        ps.contactRatio = (ps.edgeCount > 0) ?
            static_cast<float>(ps.contactEdges) / ps.edgeCount : 0;
        stats.push_back(ps);

        analyzed++;
        if (analyzed % 5000 == 0)
            printf("  Analyzed %d/%zu portals...\n", analyzed, portals.size());
    }

    // Distribution summary
    int bucket0 = 0, bucket25 = 0, bucket50 = 0, bucket75 = 0, bucket100 = 0;
    int withFloor = 0;
    float totalArea0 = 0, totalAreaHigh = 0;
    for (const auto& ps : stats) {
        if (ps.contactRatio < 0.01f) { bucket0++; totalArea0 += ps.area; }
        else if (ps.contactRatio < 0.25f) bucket25++;
        else if (ps.contactRatio < 0.50f) bucket50++;
        else if (ps.contactRatio < 0.75f) { bucket75++; totalAreaHigh += ps.area; }
        else { bucket100++; totalAreaHigh += ps.area; }
        if (ps.hasFloorContact) withFloor++;
    }

    printf("\n  Contact ratio distribution:\n");
    printf("    0%%  contact:   %6d portals (avg area %.0f)\n", bucket0,
           bucket0 > 0 ? totalArea0 / bucket0 : 0);
    printf("    1-24%% contact: %6d portals\n", bucket25);
    printf("    25-49%% contact:%6d portals\n", bucket50);
    printf("    50-74%% contact:%6d portals\n", bucket75);
    printf("    75-100%% contact:%5d portals (avg area %.0f)\n", bucket100,
           bucket100 > 0 ? totalAreaHigh / (bucket75 + bucket100) : 0);
    printf("    With floor:    %6d portals\n", withFloor);
    printf("\n");

    // CSV output
    if (!csvFile.empty()) {
        FILE* f = fopen(csvFile.c_str(), "w");
        if (f) {
            fprintf(f, "portal_idx,regionA,regionB,area,edge_count,contact_edges,"
                       "contact_ratio,min_edge_dist,has_floor,center_x,center_y,center_z\n");
            for (const auto& ps : stats) {
                const auto& p = portals[ps.idx];
                fprintf(f, "%zu,%zu,%zu,%.1f,%d,%d,%.3f,%.2f,%d,%.1f,%.1f,%.1f\n",
                        ps.idx, p.regionA, p.regionB, ps.area,
                        ps.edgeCount, ps.contactEdges, ps.contactRatio,
                        ps.minEdgeDist, ps.hasFloorContact ? 1 : 0,
                        p.centerX, p.centerY, p.centerZ);
            }
            fclose(f);
            printf("  Portal analysis CSV written: %s\n", csvFile.c_str());
        }
    }
}

// ============================================================================
// Occluder-based portal detection
// ============================================================================

// Small 2D rasterizer for projecting triangles onto a plane
struct PlaneGrid {
    int width, height;
    std::vector<uint8_t> pixels;  // 1 = covered by geometry
    float originU, originV;       // World-space origin of grid
    float cellSize;               // World units per pixel
    // Plane basis vectors (in EQ coords)
    float uAxisX, uAxisY, uAxisZ;
    float vAxisX, vAxisY, vAxisZ;
    // Plane normal and point
    float normalX, normalY, normalZ, planeDist;

    void init(int w, int h, float cs,
              float ou, float ov,
              float ux, float uy, float uz,
              float vx, float vy, float vz,
              float nx, float ny, float nz, float pd) {
        width = w; height = h; cellSize = cs;
        originU = ou; originV = ov;
        uAxisX = ux; uAxisY = uy; uAxisZ = uz;
        vAxisX = vx; vAxisY = vy; vAxisZ = vz;
        normalX = nx; normalY = ny; normalZ = nz; planeDist = pd;
        pixels.assign(w * h, 0);
    }

    // Project a world-space point onto grid coordinates
    void project(float wx, float wy, float wz, float& gu, float& gv) const {
        gu = (wx * uAxisX + wy * uAxisY + wz * uAxisZ - originU) / cellSize;
        gv = (wx * vAxisX + wy * vAxisY + wz * vAxisZ - originV) / cellSize;
    }

    // Distance from point to the plane
    float planeDotPoint(float wx, float wy, float wz) const {
        return normalX * wx + normalY * wy + normalZ * wz + planeDist;
    }

    // Rasterize a triangle projected onto the grid (scanline fill)
    void rasterizeTriangle(float u0, float v0, float u1, float v1, float u2, float v2) {
        // Sort by v
        if (v0 > v1) { std::swap(u0, u1); std::swap(v0, v1); }
        if (v0 > v2) { std::swap(u0, u2); std::swap(v0, v2); }
        if (v1 > v2) { std::swap(u1, u2); std::swap(v1, v2); }

        auto scanline = [&](int y, float leftU, float rightU) {
            if (y < 0 || y >= height) return;
            int x0 = std::max(0, static_cast<int>(std::floor(std::min(leftU, rightU))));
            int x1 = std::min(width - 1, static_cast<int>(std::ceil(std::max(leftU, rightU))));
            for (int x = x0; x <= x1; ++x)
                pixels[y * width + x] = 1;
        };

        float totalH = v2 - v0;
        if (totalH < 0.001f) return;

        for (int y = std::max(0, static_cast<int>(std::floor(v0)));
             y <= std::min(height - 1, static_cast<int>(std::ceil(v2))); ++y) {
            float fy = static_cast<float>(y) + 0.5f;
            float t = (fy - v0) / totalH;
            float uLong = u0 + t * (u2 - u0);

            float uShort;
            if (fy < v1) {
                float shortH = v1 - v0;
                if (shortH < 0.001f) uShort = u0;
                else uShort = u0 + (fy - v0) / shortH * (u1 - u0);
            } else {
                float shortH = v2 - v1;
                if (shortH < 0.001f) uShort = u1;
                else uShort = u1 + (fy - v1) / shortH * (u2 - u1);
            }

            scanline(y, uLong, uShort);
        }
    }

    int coveredPixels() const {
        int count = 0;
        for (auto p : pixels) count += p;
        return count;
    }
};

// Find all adjacent region pairs by walking BSP tree.
// Returns pairs of (regionA, regionB, splitPlane normal+dist).
struct RegionBoundary {
    size_t regionA, regionB;
    float normalX, normalY, normalZ, splitDist;
};

static void findRegionBoundariesRecursive(
    int nodeIdx, const BspTree& bsp,
    std::vector<std::pair<float, bool>>& ancestorPlanes,  // (dot value, isLeft)
    std::vector<RegionBoundary>& boundaries) {

    if (nodeIdx < 0 || static_cast<size_t>(nodeIdx) >= bsp.nodes.size()) return;
    const auto& node = bsp.nodes[nodeIdx];

    if (node.regionId > 0) return;  // Leaf

    // Collect leaf regions reachable from left and right subtrees
    auto collectLeaves = [&](int subtreeRoot, std::vector<size_t>& leaves) {
        std::vector<int> stack = {subtreeRoot};
        while (!stack.empty()) {
            int idx = stack.back(); stack.pop_back();
            if (idx < 0 || static_cast<size_t>(idx) >= bsp.nodes.size()) continue;
            const auto& n = bsp.nodes[idx];
            if (n.regionId > 0) {
                size_t regionIdx = static_cast<size_t>(n.regionId - 1);
                if (regionIdx < bsp.regions.size() && bsp.regions[regionIdx] &&
                    bsp.regions[regionIdx]->containsPolygons) {
                    leaves.push_back(regionIdx);
                }
            } else {
                if (n.left >= 0) stack.push_back(n.left);
                if (n.right >= 0) stack.push_back(n.right);
            }
        }
    };

    std::vector<size_t> leftLeaves, rightLeaves;
    if (node.left >= 0) collectLeaves(node.left, leftLeaves);
    if (node.right >= 0) collectLeaves(node.right, rightLeaves);

    // Create boundaries for adjacent leaf pairs.
    // Limit subtree fan-out to avoid O(n^2) explosion on deep BSP nodes.
    if (leftLeaves.size() <= 10 && rightLeaves.size() <= 10) {
        for (size_t a : leftLeaves) {
            for (size_t b : rightLeaves) {
                // Check PVS mutual visibility
                if (a < bsp.regions.size() && b < bsp.regions.size()) {
                    const auto& regA = bsp.regions[a];
                    const auto& regB = bsp.regions[b];
                    bool aSeesB = regA->pvsContains(b);
                    bool bSeesA = regB->pvsContains(a);
                    if (aSeesB && bSeesA) {
                        boundaries.push_back({a, b, node.normalX, node.normalY, node.normalZ, node.splitDistance});
                    }
                }
            }
        }
    }

    // Recurse
    if (node.left >= 0) findRegionBoundariesRecursive(node.left, bsp, ancestorPlanes, boundaries);
    if (node.right >= 0) findRegionBoundariesRecursive(node.right, bsp, ancestorPlanes, boundaries);
}

void detectPortalsFromOccluders(ZoneBenchData& data, const std::string& csvFile) {
    if (!data.portalSystem || !data.portalSystem->hasPortals()) {
        printf("No portal data for detection.\n");
        return;
    }

    auto wldLoader = data.zone->wldLoader;
    auto& portals = data.portalSystem->getData().portals;
    printf("\n=== Occluder-Based Portal Detection ===\n");

    // Use existing portals (region pairs + split planes) as input.
    // Deduplicate region pairs — many BSP portals connect the same pair.
    struct RegionPairInfo {
        size_t regionA, regionB;
        float normalX, normalY, normalZ;
        // Merged portal area for this pair
        float totalPortalArea;
    };

    std::map<std::pair<size_t, size_t>, RegionPairInfo> pairMap;
    for (const auto& p : portals) {
        auto key = std::make_pair(std::min(p.regionA, p.regionB),
                                   std::max(p.regionA, p.regionB));
        auto it = pairMap.find(key);
        if (it == pairMap.end()) {
            pairMap[key] = {key.first, key.second, p.normalX, p.normalY, p.normalZ, p.area};
        } else {
            it->second.totalPortalArea += p.area;
            // Keep the normal from the largest portal for this pair
            if (p.area > it->second.totalPortalArea - p.area) {
                it->second.normalX = p.normalX;
                it->second.normalY = p.normalY;
                it->second.normalZ = p.normalZ;
            }
        }
    }

    std::vector<RegionPairInfo> uniquePairs;
    for (auto& [key, info] : pairMap)
        uniquePairs.push_back(info);

    printf("  %zu portals -> %zu unique region pairs\n", portals.size(), uniquePairs.size());

    // Step 2: For each region pair, rasterize geometry onto the boundary plane
    const float gridCellSize = 2.0f;  // 2 EQ units per pixel
    const int maxGridDim = 64;        // Max 64x64 grid per boundary
    const float nearPlaneThresh = 5.0f;  // Project triangles within 5 units of plane

    struct DetectedPortal {
        size_t regionA, regionB;
        float boundaryArea;    // Total boundary area (sq EQ units)
        float coveredArea;     // Area covered by geometry
        float gapArea;         // Uncovered area (potential portal)
        float coverageRatio;   // covered / total
        float normalX, normalY, normalZ;
        float centerX, centerY, centerZ;
        int gridPixels, coveredPixels;
    };
    std::vector<DetectedPortal> detected;

    int analyzed = 0;
    for (const auto& pair : uniquePairs) {
        // Get region bounding boxes to determine grid extent
        auto itA = data.regions.find(pair.regionA);
        auto itB = data.regions.find(pair.regionB);
        if (itA == data.regions.end() || itB == data.regions.end()) continue;

        // Compute overlap region of the two AABBs (the shared boundary area).
        // Add margin because adjacent BSP regions may touch at edges without overlapping.
        const float margin = 5.0f;
        float overlapMinX = std::max(itA->second.eqMinX, itB->second.eqMinX) - margin;
        float overlapMaxX = std::min(itA->second.eqMaxX, itB->second.eqMaxX) + margin;
        float overlapMinY = std::max(itA->second.eqMinY, itB->second.eqMinY) - margin;
        float overlapMaxY = std::min(itA->second.eqMaxY, itB->second.eqMaxY) + margin;
        float overlapMinZ = std::max(itA->second.eqMinZ, itB->second.eqMinZ) - margin;
        float overlapMaxZ = std::min(itA->second.eqMaxZ, itB->second.eqMaxZ) + margin;

        if (overlapMinX >= overlapMaxX || overlapMinY >= overlapMaxY || overlapMinZ >= overlapMaxZ)
            continue;

        // Build plane basis vectors (same as portal_system.cpp)
        float nx = pair.normalX, ny = pair.normalY, nz = pair.normalZ;
        float ax = std::fabs(nx), ay = std::fabs(ny), az = std::fabs(nz);

        float ux, uy, uz;
        if (ax >= ay && ax >= az) { ux = 0; uy = 1; uz = 0; }
        else if (ay >= ax && ay >= az) { ux = 1; uy = 0; uz = 0; }
        else { ux = 1; uy = 0; uz = 0; }

        // Gram-Schmidt
        float udotn = ux * nx + uy * ny + uz * nz;
        ux -= udotn * nx; uy -= udotn * ny; uz -= udotn * nz;
        float ulen = std::sqrt(ux * ux + uy * uy + uz * uz);
        if (ulen < 1e-8f) continue;
        ux /= ulen; uy /= ulen; uz /= ulen;

        // v = cross(normal, u)
        float vx = ny * uz - nz * uy;
        float vy = nz * ux - nx * uz;
        float vz = nx * uy - ny * ux;

        // Project overlap AABB corners onto UV plane to get grid extent
        float corners[8][3] = {
            {overlapMinX, overlapMinY, overlapMinZ}, {overlapMaxX, overlapMinY, overlapMinZ},
            {overlapMinX, overlapMaxY, overlapMinZ}, {overlapMaxX, overlapMaxY, overlapMinZ},
            {overlapMinX, overlapMinY, overlapMaxZ}, {overlapMaxX, overlapMinY, overlapMaxZ},
            {overlapMinX, overlapMaxY, overlapMaxZ}, {overlapMaxX, overlapMaxY, overlapMaxZ},
        };
        float minU = 1e9f, maxU = -1e9f, minV = 1e9f, maxV = -1e9f;
        for (auto& c : corners) {
            float u = c[0] * ux + c[1] * uy + c[2] * uz;
            float v = c[0] * vx + c[1] * vy + c[2] * vz;
            minU = std::min(minU, u); maxU = std::max(maxU, u);
            minV = std::min(minV, v); maxV = std::max(maxV, v);
        }

        float extentU = maxU - minU, extentV = maxV - minV;
        if (extentU < 1.0f || extentV < 1.0f) continue;

        int gridW = std::min(maxGridDim, std::max(4, static_cast<int>(extentU / gridCellSize)));
        int gridH = std::min(maxGridDim, std::max(4, static_cast<int>(extentV / gridCellSize)));
        float actualCellSize = std::max(extentU / gridW, extentV / gridH);

        PlaneGrid grid;
        // Compute approximate split distance from the plane normal and overlap center
        float overlapCx = (overlapMinX + overlapMaxX) * 0.5f;
        float overlapCy = (overlapMinY + overlapMaxY) * 0.5f;
        float overlapCz = (overlapMinZ + overlapMaxZ) * 0.5f;
        float splitDist = -(nx * overlapCx + ny * overlapCy + nz * overlapCz);

        grid.init(gridW, gridH, actualCellSize, minU, minV,
                  ux, uy, uz, vx, vy, vz, nx, ny, nz, splitDist);

        // Rasterize ALL triangles from both regions that are near the split plane
        for (size_t regionIdx : {pair.regionA, pair.regionB}) {
            auto geom = wldLoader->getGeometryForRegion(regionIdx);
            if (!geom) continue;

            for (const auto& tri : geom->triangles) {
                if (tri.v1 >= geom->vertices.size() ||
                    tri.v2 >= geom->vertices.size() ||
                    tri.v3 >= geom->vertices.size()) continue;

                const auto& va = geom->vertices[tri.v1];
                const auto& vb = geom->vertices[tri.v2];
                const auto& vc = geom->vertices[tri.v3];

                float w0x = geom->centerX + va.x, w0y = geom->centerY + va.y, w0z = geom->centerZ + va.z;
                float w1x = geom->centerX + vb.x, w1y = geom->centerY + vb.y, w1z = geom->centerZ + vb.z;
                float w2x = geom->centerX + vc.x, w2y = geom->centerY + vc.y, w2z = geom->centerZ + vc.z;

                // Check if triangle is near the split plane
                float d0 = grid.planeDotPoint(w0x, w0y, w0z);
                float d1 = grid.planeDotPoint(w1x, w1y, w1z);
                float d2 = grid.planeDotPoint(w2x, w2y, w2z);

                if (std::fabs(d0) > nearPlaneThresh &&
                    std::fabs(d1) > nearPlaneThresh &&
                    std::fabs(d2) > nearPlaneThresh) continue;

                // Project onto grid
                float gu0, gv0, gu1, gv1, gu2, gv2;
                grid.project(w0x, w0y, w0z, gu0, gv0);
                grid.project(w1x, w1y, w1z, gu1, gv1);
                grid.project(w2x, w2y, w2z, gu2, gv2);

                grid.rasterizeTriangle(gu0, gv0, gu1, gv1, gu2, gv2);
            }
        }

        int covered = grid.coveredPixels();
        int total = gridW * gridH;
        float coverageRatio = static_cast<float>(covered) / total;
        float boundaryArea = extentU * extentV;
        float coveredArea = coverageRatio * boundaryArea;
        float gapArea = boundaryArea - coveredArea;

        // Compute boundary center
        float bcx = (overlapMinX + overlapMaxX) * 0.5f;
        float bcy = (overlapMinY + overlapMaxY) * 0.5f;
        float bcz = (overlapMinZ + overlapMaxZ) * 0.5f;

        detected.push_back({pair.regionA, pair.regionB,
                           boundaryArea, coveredArea, gapArea, coverageRatio,
                           nx, ny, nz, bcx, bcy, bcz,
                           total, covered});

        analyzed++;
        if (analyzed % 1000 == 0)
            printf("  Analyzed %d/%zu pairs...\n", analyzed, uniquePairs.size());
    }

    // Distribution summary
    printf("\n  Analyzed %zu boundaries\n", detected.size());

    int highCover = 0, medCover = 0, lowCover = 0, noCover = 0;
    int doorCandidates = 0;

    for (const auto& d : detected) {
        if (d.coverageRatio >= 0.75f) highCover++;
        else if (d.coverageRatio >= 0.50f) medCover++;
        else if (d.coverageRatio >= 0.10f) lowCover++;
        else noCover++;

        // Candidate doorway: high wall coverage with a small gap
        if (d.coverageRatio >= 0.30f && d.gapArea > 10.0f && d.gapArea < 2000.0f) {
            doorCandidates++;
        }
    }

    printf("\n  Coverage distribution:\n");
    printf("    75-100%% covered: %d (mostly wall, small gaps = doorways)\n", highCover);
    printf("    50-74%% covered:  %d (partial wall)\n", medCover);
    printf("    10-49%% covered:  %d (sparse geometry)\n", lowCover);
    printf("    0-9%% covered:    %d (open air)\n", noCover);
    printf("\n  Doorway candidates (30%%+ coverage, 10-2000 sq gap): %d\n", doorCandidates);

    // CSV output
    if (!csvFile.empty()) {
        FILE* f = fopen(csvFile.c_str(), "w");
        if (f) {
            fprintf(f, "regionA,regionB,boundary_area,covered_area,gap_area,"
                       "coverage_ratio,grid_pixels,covered_pixels,"
                       "normal_x,normal_y,normal_z,center_x,center_y,center_z\n");
            for (const auto& d : detected) {
                fprintf(f, "%zu,%zu,%.1f,%.1f,%.1f,%.3f,%d,%d,%.3f,%.3f,%.3f,%.1f,%.1f,%.1f\n",
                        d.regionA, d.regionB, d.boundaryArea, d.coveredArea, d.gapArea,
                        d.coverageRatio, d.gridPixels, d.coveredPixels,
                        d.normalX, d.normalY, d.normalZ,
                        d.centerX, d.centerY, d.centerZ);
            }
            fclose(f);
            printf("\n  CSV written: %s\n", csvFile.c_str());
        }
    }
}

std::vector<SurfaceCandidate> findSurfaceCandidates(const ZoneBenchData& data,
                                                    size_t maxCandidates) {
    // T242: rank regions by polygon count, restricted to the upper half of
    // the zone's *polygon-weighted* Z distribution (region-center Z, weighted
    // by each region's polygon count). The pre-T242 finder used the upper
    // half of the raw vertex-extent Z range instead, which is dominated by a
    // handful of sky/far-scenery regions hundreds of units above the playable
    // area (blackburrow Z=[-228,605], crushbone Z=[-185,629], gfaydark
    // Z=[-99,802]): every playable region fell below that threshold, the
    // "surface" seed landed ~300 EQ units in the sky, navmesh sampling found
    // no polygon within its (5,100,5) search extents, and the orbit fallback
    // circled the zone at that height looking at nothing -- the Batch H
    // "blackburrow/crushbone render blank" finding.
    struct Entry { float x, y, z; int polys; };
    std::vector<Entry> entries;
    long long totalPolys = 0;
    for (const auto& [idx, rd] : data.regions) {
        if (!rd.mesh) continue;
        int polys = 0;
        for (irr::u32 i = 0; i < rd.mesh->getMeshBufferCount(); ++i) {
            auto* buf = rd.mesh->getMeshBuffer(i);
            if (buf) polys += buf->getIndexCount() / 3;
        }
        if (polys <= 0) continue;
        entries.push_back({(rd.eqMinX + rd.eqMaxX) * 0.5f,
                           (rd.eqMinY + rd.eqMaxY) * 0.5f,
                           (rd.eqMinZ + rd.eqMaxZ) * 0.5f, polys});
        totalPolys += polys;
    }

    std::vector<SurfaceCandidate> out;
    if (entries.empty()) {
        out.push_back({(data.boundsMinX + data.boundsMaxX) * 0.5f,
                       (data.boundsMinY + data.boundsMaxY) * 0.5f,
                       data.boundsMaxZ - 10.0f, 0});
        return out;
    }

    // Polygon-weighted median of region-center Z.
    std::vector<Entry> byZ = entries;
    std::sort(byZ.begin(), byZ.end(), [](const Entry& a, const Entry& b) { return a.z < b.z; });
    long long acc = 0;
    float zThreshold = byZ.front().z;
    for (const auto& e : byZ) {
        acc += e.polys;
        if (acc * 2 >= totalPolys) { zThreshold = e.z; break; }
    }

    size_t above = 0;
    for (const auto& e : entries) {
        if (e.z >= zThreshold) {
            ++above;
            out.push_back({e.x, e.y, e.z, e.polys});
        }
    }
    std::stable_sort(out.begin(), out.end(),
                     [](const SurfaceCandidate& a, const SurfaceCandidate& b) {
                         return a.polys > b.polys;
                     });
    if (out.size() > maxCandidates) out.resize(maxCandidates);

    printf("  Surface candidates: %zu regions at or above the poly-weighted median "
           "Z=%.1f (bounds Z=[%.0f,%.0f]), keeping top %zu by polygon count\n",
           above, zThreshold, data.boundsMinZ, data.boundsMaxZ, out.size());
    return out;
}

void findSurfacePosition(const ZoneBenchData& data,
                          float& outX, float& outY, float& outZ) {
    auto candidates = findSurfaceCandidates(data, 1);
    outX = candidates.front().x;
    outY = candidates.front().y;
    outZ = candidates.front().z;

    printf("  Surface position: (%.1f, %.1f, %.1f) [%d polys in region]\n",
           outX, outY, outZ, candidates.front().polys);
}

} // namespace ZoneBench
