// Offline tool to classify BSP regions for portal occlusion eligibility.
//
// Algorithm:
// 1. Extract BSP boundary portals from split planes (same as runtime PortalSystem)
// 2. For each portal, clip against zone wall geometry using exact 2D polygon
//    boolean subtraction (planar arrangement) to find actual openings
// 3. Classify regions by portal opening ratio:
//    ratio = total_opening_area / region_AABB_surface_area
//    Low ratio = mostly walled with small openings = indoor
//    High ratio = wide open = outdoor
//
// Output: JSON file with per-region classification and per-portal geometry data.
//
// Usage:
//   zone_region_classifier --eq-path /path/to/EQ --zone qeynos2 -v
//   zone_region_classifier --eq-path /path/to/EQ --all --output data/region_maps

#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <map>
#include <unordered_map>
#include <filesystem>
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <limits>
#include "client/graphics/eq/wld_loader.h"
#include "client/graphics/portal_system.h"
#include "client/graphics/portal_geometry_clipper.h"
#include "client/hc_map.h"
#include "common/logging.h"
#include <glm/glm.hpp>

using namespace EQT::Graphics;
namespace fs = std::filesystem;

struct AABB {
    float minX, minY, minZ;
    float maxX, maxY, maxZ;

    float surfaceArea() const {
        float dx = maxX - minX;
        float dy = maxY - minY;
        float dz = maxZ - minZ;
        return 2.0f * (dx * dy + dx * dz + dy * dz);
    }
};

struct PortalStats {
    size_t portalIndex;
    size_t regionA, regionB;
    float boundaryArea;
    float coveredArea;
    float openingArea;
    int coplanarTriangles;
    int openingCount;
    std::string classification;  // "open-air", "wall", "doorway", "partial"
};

struct RegionClassification {
    size_t regionIndex;
    bool indoor;
    int totalTriangles;
    float totalOpeningArea;     // Sum of opening areas from geometry-clipped portals
    float totalBoundaryArea;    // Sum of raw BSP boundary areas
    float aabbSurfaceArea;
    float portalRatio;          // totalOpeningArea / aabbSurfaceArea
    int portalCount;            // Number of portals touching this region
    int doorwayCount;           // Portals with actual openings (partial coverage)
};

struct ClassifierStats {
    size_t totalRegions = 0;
    size_t regionsWithGeometry = 0;
    size_t indoorRegions = 0;
    size_t outdoorRegions = 0;
    size_t totalBspPortals = 0;
    size_t openAirPortals = 0;      // No coplanar geometry (outdoor BSP boundaries)
    size_t fullWallPortals = 0;     // Fully covered by geometry (solid walls)
    size_t doorwayPortals = 0;      // Partially covered (actual doorways/openings)
};

static bool computeRegionAABB(size_t regionIndex, const WldLoader& loader, AABB& out) {
    auto geom = loader.getGeometryForRegion(regionIndex);
    if (!geom || geom->vertices.empty()) return false;

    out.minX = out.minY = out.minZ = std::numeric_limits<float>::max();
    out.maxX = out.maxY = out.maxZ = std::numeric_limits<float>::lowest();

    for (const auto& v : geom->vertices) {
        float wx = geom->centerX + v.x;
        float wy = geom->centerY + v.y;
        float wz = geom->centerZ + v.z;
        if (wx < out.minX) out.minX = wx;
        if (wy < out.minY) out.minY = wy;
        if (wz < out.minZ) out.minZ = wz;
        if (wx > out.maxX) out.maxX = wx;
        if (wy > out.maxY) out.maxY = wy;
        if (wz > out.maxZ) out.maxZ = wz;
    }
    return true;
}

struct DebugArea {
    bool enabled = false;
    float x1, y1, x2, y2;  // EQ world XY bounds
};

// Diagnostic: for portals in the debug area, dump detailed info about
// the portal plane vs actual geometry to verify alignment.
static void debugPortalsInArea(const DebugArea& area, const ZonePortalData& portalData,
                                const WldLoader& loader, float coplanarTolerance) {
    float minX = std::min(area.x1, area.x2);
    float maxX = std::max(area.x1, area.x2);
    float minY = std::min(area.y1, area.y2);
    float maxY = std::max(area.y1, area.y2);

    std::cout << "\n=== DEBUG AREA: X[" << minX << "," << maxX
              << "] Y[" << minY << "," << maxY << "] ===\n\n";

    int portalCount = 0;
    for (size_t pi = 0; pi < portalData.portals.size(); ++pi) {
        const auto& portal = portalData.portals[pi];
        // Check if portal center is within the debug area (XY only)
        if (portal.centerX < minX || portal.centerX > maxX) continue;
        if (portal.centerY < minY || portal.centerY > maxY) continue;

        portalCount++;
        std::cout << "--- Portal " << pi << ": R" << portal.regionA << " <-> R" << portal.regionB << " ---\n";
        std::cout << "  Center: (" << portal.centerX << ", " << portal.centerY << ", " << portal.centerZ << ")\n";
        std::cout << "  Normal: (" << portal.normalX << ", " << portal.normalY << ", " << portal.normalZ << ")\n";
        std::cout << "  Area: " << portal.area << "  Vertices: " << portal.vertexCount() << "\n";

        // Print portal polygon vertices
        std::cout << "  Polygon:\n";
        for (size_t i = 0; i < portal.vertexCount(); ++i) {
            std::cout << "    v" << i << ": (" << portal.vx(i) << ", " << portal.vy(i) << ", " << portal.vz(i) << ")\n";
        }

        // For each adjacent region, find wall-oriented triangles and measure their
        // distance to the portal plane
        auto examineRegion = [&](size_t regionIdx, const char* label) {
            auto geom = loader.getGeometryForRegion(regionIdx);
            if (!geom) {
                std::cout << "  " << label << " R" << regionIdx << ": no geometry\n";
                return;
            }

            std::cout << "  " << label << " R" << regionIdx << ": center=("
                      << geom->centerX << "," << geom->centerY << "," << geom->centerZ
                      << ") verts=" << geom->vertices.size()
                      << " tris=" << geom->triangles.size() << "\n";

            // Check plane equation: dot(normal, point) + splitDistance
            // Portal center should satisfy this
            // But we don't have splitDistance directly — we use the plane defined by
            // (normal, portal center): dist_to_plane = dot(normal, point - center)

            int wallAligned = 0;       // Triangle normal parallel to portal normal
            int coplanarCount = 0;     // Wall-aligned AND within distance tolerance
            int nearMiss = 0;          // Wall-aligned, distance 1-5
            int farCount = 0;          // Wall-aligned, distance > 5

            // Distance histogram for wall-aligned triangles
            struct WallTri {
                float dist0, dist1, dist2;  // Vertex distances to portal plane
                float maxDist;
                float normalDot;
            };
            std::vector<WallTri> wallTris;

            for (const auto& tri : geom->triangles) {
                if (tri.v1 >= geom->vertices.size() ||
                    tri.v2 >= geom->vertices.size() ||
                    tri.v3 >= geom->vertices.size()) continue;

                float wx0 = geom->centerX + geom->vertices[tri.v1].x;
                float wy0 = geom->centerY + geom->vertices[tri.v1].y;
                float wz0 = geom->centerZ + geom->vertices[tri.v1].z;
                float wx1 = geom->centerX + geom->vertices[tri.v2].x;
                float wy1 = geom->centerY + geom->vertices[tri.v2].y;
                float wz1 = geom->centerZ + geom->vertices[tri.v2].z;
                float wx2 = geom->centerX + geom->vertices[tri.v3].x;
                float wy2 = geom->centerY + geom->vertices[tri.v3].y;
                float wz2 = geom->centerZ + geom->vertices[tri.v3].z;

                // Compute triangle normal
                float e1x = wx1-wx0, e1y = wy1-wy0, e1z = wz1-wz0;
                float e2x = wx2-wx0, e2y = wy2-wy0, e2z = wz2-wz0;
                float tnx = e1y*e2z - e1z*e2y;
                float tny = e1z*e2x - e1x*e2z;
                float tnz = e1x*e2y - e1y*e2x;
                float tnLen = std::sqrt(tnx*tnx + tny*tny + tnz*tnz);
                if (tnLen < 1e-6f) continue;
                tnx /= tnLen; tny /= tnLen; tnz /= tnLen;

                // Check if triangle normal is parallel to portal normal
                float normalDot = std::fabs(tnx * portal.normalX + tny * portal.normalY + tnz * portal.normalZ);
                if (normalDot < 0.7f) continue;  // Not wall-aligned

                wallAligned++;

                // Compute vertex distances to portal plane
                float d0 = (wx0 - portal.centerX) * portal.normalX +
                            (wy0 - portal.centerY) * portal.normalY +
                            (wz0 - portal.centerZ) * portal.normalZ;
                float d1 = (wx1 - portal.centerX) * portal.normalX +
                            (wy1 - portal.centerY) * portal.normalY +
                            (wz1 - portal.centerZ) * portal.normalZ;
                float d2 = (wx2 - portal.centerX) * portal.normalX +
                            (wy2 - portal.centerY) * portal.normalY +
                            (wz2 - portal.centerZ) * portal.normalZ;

                float maxD = std::max({std::fabs(d0), std::fabs(d1), std::fabs(d2)});

                if (maxD <= coplanarTolerance) coplanarCount++;
                else if (maxD <= 5.0f) nearMiss++;
                else farCount++;

                wallTris.push_back({d0, d1, d2, maxD, normalDot});
            }

            std::cout << "    Wall-aligned triangles: " << wallAligned
                      << "  (coplanar=" << coplanarCount
                      << "  near[1-5]=" << nearMiss
                      << "  far[>5]=" << farCount << ")\n";

            // Sort by distance and show closest 5
            std::sort(wallTris.begin(), wallTris.end(),
                      [](const auto& a, const auto& b) { return a.maxDist < b.maxDist; });
            int showCount = std::min(5, (int)wallTris.size());
            for (int i = 0; i < showCount; ++i) {
                const auto& wt = wallTris[i];
                std::cout << "    closest[" << i << "]: dists=("
                          << std::fixed << std::setprecision(2)
                          << wt.dist0 << ", " << wt.dist1 << ", " << wt.dist2
                          << ")  maxDist=" << wt.maxDist
                          << "  normalDot=" << wt.normalDot << "\n";
            }
        };

        examineRegion(portal.regionA, "RegionA");
        examineRegion(portal.regionB, "RegionB");
        std::cout << "\n";
    }

    std::cout << "=== " << portalCount << " portals in debug area ===\n\n";
}

// Möller–Trumbore ray-triangle intersection.
// Ray: origin + t*dir, t in [0, maxT].
// Returns true if intersection found, sets tOut to the intersection t.
static bool rayTriangleIntersect(float ox, float oy, float oz,
                                  float dx, float dy, float dz, float maxT,
                                  float v0x, float v0y, float v0z,
                                  float v1x, float v1y, float v1z,
                                  float v2x, float v2y, float v2z) {
    const float EPSILON = 1e-6f;
    float e1x = v1x-v0x, e1y = v1y-v0y, e1z = v1z-v0z;
    float e2x = v2x-v0x, e2y = v2y-v0y, e2z = v2z-v0z;
    float hx = dy*e2z - dz*e2y, hy = dz*e2x - dx*e2z, hz = dx*e2y - dy*e2x;
    float a = e1x*hx + e1y*hy + e1z*hz;
    if (a > -EPSILON && a < EPSILON) return false;
    float f = 1.0f / a;
    float sx = ox-v0x, sy = oy-v0y, sz = oz-v0z;
    float u = f * (sx*hx + sy*hy + sz*hz);
    if (u < 0.0f || u > 1.0f) return false;
    float qx = sy*e1z - sz*e1y, qy = sz*e1x - sx*e1z, qz = sx*e1y - sy*e1x;
    float v = f * (dx*qx + dy*qy + dz*qz);
    if (v < 0.0f || u + v > 1.0f) return false;
    float t = f * (e2x*qx + e2y*qy + e2z*qz);
    return (t >= 0.0f && t <= maxT);
}

// Check if a ray from origin along dir (length maxT) hits any triangle in the geometry.
static bool rayHitsGeometry(float ox, float oy, float oz,
                             float dx, float dy, float dz, float maxT,
                             const ZoneGeometry* geom) {
    if (!geom) return false;
    for (const auto& tri : geom->triangles) {
        if (tri.v1 >= geom->vertices.size() ||
            tri.v2 >= geom->vertices.size() ||
            tri.v3 >= geom->vertices.size()) continue;
        float wx0 = geom->centerX + geom->vertices[tri.v1].x;
        float wy0 = geom->centerY + geom->vertices[tri.v1].y;
        float wz0 = geom->centerZ + geom->vertices[tri.v1].z;
        float wx1 = geom->centerX + geom->vertices[tri.v2].x;
        float wy1 = geom->centerY + geom->vertices[tri.v2].y;
        float wz1 = geom->centerZ + geom->vertices[tri.v2].z;
        float wx2 = geom->centerX + geom->vertices[tri.v3].x;
        float wy2 = geom->centerY + geom->vertices[tri.v3].y;
        float wz2 = geom->centerZ + geom->vertices[tri.v3].z;
        if (rayTriangleIntersect(ox, oy, oz, dx, dy, dz, maxT,
                                  wx0, wy0, wz0, wx1, wy1, wz1, wx2, wy2, wz2))
            return true;
    }
    return false;
}

// Raycast-based portal diagnostic.
// For each portal in the area, creates a grid on the portal plane and casts
// rays perpendicular to the portal to detect blocking geometry.
static void debugRaycastInArea(const DebugArea& area, const ZonePortalData& portalData,
                                const WldLoader& loader, float rayLen, float gridSpacing) {
    float minX = std::min(area.x1, area.x2);
    float maxX = std::max(area.x1, area.x2);
    float minY = std::min(area.y1, area.y2);
    float maxY = std::max(area.y1, area.y2);

    std::cout << "\n=== RAYCAST DEBUG: X[" << minX << "," << maxX
              << "] Y[" << minY << "," << maxY
              << "] rayLen=" << rayLen << " grid=" << gridSpacing << " ===\n\n";

    int portalCount = 0;
    for (size_t pi = 0; pi < portalData.portals.size(); ++pi) {
        const auto& portal = portalData.portals[pi];
        if (portal.centerX < minX || portal.centerX > maxX) continue;
        if (portal.centerY < minY || portal.centerY > maxY) continue;

        size_t nv = portal.vertexCount();
        if (nv < 3) continue;

        portalCount++;

        // Build 2D projection basis (same approach as portal_geometry_clipper)
        float nx = portal.normalX, ny = portal.normalY, nz = portal.normalZ;
        float cx = portal.centerX, cy = portal.centerY, cz = portal.centerZ;

        // Gram-Schmidt tangent basis
        float ax = std::fabs(nx), ay = std::fabs(ny), az = std::fabs(nz);
        float tux, tuy, tuz;
        if (ax >= ay && ax >= az) { tux=0; tuy=1; tuz=0; }
        else if (ay >= ax && ay >= az) { tux=1; tuy=0; tuz=0; }
        else { tux=1; tuy=0; tuz=0; }
        float d = tux*nx + tuy*ny + tuz*nz;
        tux -= d*nx; tuy -= d*ny; tuz -= d*nz;
        float ulen = std::sqrt(tux*tux + tuy*tuy + tuz*tuz);
        tux /= ulen; tuy /= ulen; tuz /= ulen;
        float tvx = ny*tuz - nz*tuy, tvy = nz*tux - nx*tuz, tvz = nx*tuy - ny*tux;

        // Project portal vertices to 2D
        std::vector<std::pair<float,float>> poly2D(nv);
        float uMin = 1e30f, uMax = -1e30f, vMin = 1e30f, vMax = -1e30f;
        for (size_t i = 0; i < nv; ++i) {
            float dx = portal.vx(i)-cx, dy = portal.vy(i)-cy, dz = portal.vz(i)-cz;
            float u = dx*tux + dy*tuy + dz*tuz;
            float v = dx*tvx + dy*tvy + dz*tvz;
            poly2D[i] = {u, v};
            uMin = std::min(uMin, u); uMax = std::max(uMax, u);
            vMin = std::min(vMin, v); vMax = std::max(vMax, v);
        }

        // Point-in-polygon test (2D winding number)
        auto pointInPoly = [&](float pu, float pv) -> bool {
            int winding = 0;
            for (size_t i = 0; i < nv; ++i) {
                size_t j = (i+1) % nv;
                float ay = poly2D[i].second, by = poly2D[j].second;
                if (ay <= pv) {
                    if (by > pv) {
                        float cross = (poly2D[j].first - poly2D[i].first) * (pv - poly2D[i].second)
                                    - (pu - poly2D[i].first) * (poly2D[j].second - poly2D[i].second);
                        if (cross > 0) ++winding;
                    }
                } else {
                    if (by <= pv) {
                        float cross = (poly2D[j].first - poly2D[i].first) * (pv - poly2D[i].second)
                                    - (pu - poly2D[i].first) * (poly2D[j].second - poly2D[i].second);
                        if (cross < 0) --winding;
                    }
                }
            }
            return winding != 0;
        };

        // Get geometry for both regions
        auto gA = loader.getGeometryForRegion(portal.regionA);
        auto gB = loader.getGeometryForRegion(portal.regionB);

        // Create grid and raycast
        int gridW = static_cast<int>((uMax - uMin) / gridSpacing) + 1;
        int gridH = static_cast<int>((vMax - vMin) / gridSpacing) + 1;

        // Cap grid size for sanity
        if (gridW > 120) { gridSpacing = (uMax - uMin) / 119.0f; gridW = 120; }
        if (gridH > 60) { gridSpacing = (vMax - vMin) / 59.0f; gridH = 60; }

        int blocked = 0, open = 0, outside = 0;
        std::vector<char> grid(gridW * gridH, ' ');

        for (int gv = 0; gv < gridH; ++gv) {
            float pv = vMin + (gv + 0.5f) * (vMax - vMin) / gridH;
            for (int gu = 0; gu < gridW; ++gu) {
                float pu = uMin + (gu + 0.5f) * (uMax - uMin) / gridW;

                if (!pointInPoly(pu, pv)) {
                    grid[gv * gridW + gu] = ' ';
                    outside++;
                    continue;
                }

                // Unproject to 3D
                float px = cx + pu*tux + pv*tvx;
                float py = cy + pu*tuy + pv*tvy;
                float pz = cz + pu*tuz + pv*tvz;

                // Cast rays in both directions along the normal
                bool hit = rayHitsGeometry(px, py, pz, nx, ny, nz, rayLen, gA.get()) ||
                           rayHitsGeometry(px, py, pz, -nx, -ny, -nz, rayLen, gA.get()) ||
                           rayHitsGeometry(px, py, pz, nx, ny, nz, rayLen, gB.get()) ||
                           rayHitsGeometry(px, py, pz, -nx, -ny, -nz, rayLen, gB.get());

                if (hit) {
                    grid[gv * gridW + gu] = '#';
                    blocked++;
                } else {
                    grid[gv * gridW + gu] = '.';
                    open++;
                }
            }
        }

        // Print results
        std::cout << "--- Portal " << pi << ": R" << portal.regionA << " <-> R" << portal.regionB << " ---\n";
        std::cout << "  Center: (" << cx << ", " << cy << ", " << cz << ")\n";
        std::cout << "  Normal: (" << nx << ", " << ny << ", " << nz << ")\n";
        std::cout << "  Area: " << portal.area << "  Grid: " << gridW << "x" << gridH
                  << "  Blocked: " << blocked << "  Open: " << open << "\n";

        // Print ASCII grid (v axis top-to-bottom)
        for (int gv = gridH - 1; gv >= 0; --gv) {
            std::cout << "  |";
            for (int gu = 0; gu < gridW; ++gu) {
                std::cout << grid[gv * gridW + gu];
            }
            std::cout << "|\n";
        }
        std::cout << "\n";
    }

    std::cout << "=== " << portalCount << " portals raycast ===\n\n";
}

// HCMap-based raycast debug — same grid approach but using collision mesh
static void debugHCMapRaycastInArea(const DebugArea& area, const ZonePortalData& portalData,
                                     const HCMap& hcmap, float rayLen, float gridSpacing,
                                     float zMin = -1e30f, float zMax = 1e30f) {
    float minX = std::min(area.x1, area.x2);
    float maxX = std::max(area.x1, area.x2);
    float minY = std::min(area.y1, area.y2);
    float maxY = std::max(area.y1, area.y2);

    std::cout << "\n=== HCMAP RAYCAST DEBUG: X[" << minX << "," << maxX
              << "] Y[" << minY << "," << maxY
              << "] Z[" << zMin << "," << zMax
              << "] rayLen=" << rayLen << " grid=" << gridSpacing << " ===\n\n";

    int portalCount = 0;
    for (size_t pi = 0; pi < portalData.portals.size(); ++pi) {
        const auto& portal = portalData.portals[pi];
        if (portal.centerX < minX || portal.centerX > maxX) continue;
        if (portal.centerY < minY || portal.centerY > maxY) continue;
        if (portal.centerZ < zMin || portal.centerZ > zMax) continue;

        size_t nv = portal.vertexCount();
        if (nv < 3) continue;

        portalCount++;

        float nx = portal.normalX, ny = portal.normalY, nz = portal.normalZ;
        float cx = portal.centerX, cy = portal.centerY, cz = portal.centerZ;

        // Gram-Schmidt tangent basis
        float ax = std::fabs(nx), ay = std::fabs(ny), az = std::fabs(nz);
        float tux, tuy, tuz;
        if (ax >= ay && ax >= az) { tux = 0; tuy = 1; tuz = 0; }
        else { tux = 1; tuy = 0; tuz = 0; }
        float d = tux*nx + tuy*ny + tuz*nz;
        tux -= d*nx; tuy -= d*ny; tuz -= d*nz;
        float len = std::sqrt(tux*tux + tuy*tuy + tuz*tuz);
        tux /= len; tuy /= len; tuz /= len;
        float tvx = ny*tuz - nz*tuy;
        float tvy = nz*tux - nx*tuz;
        float tvz = nx*tuy - ny*tux;

        // Project portal vertices to 2D and get bounds
        float uMin = 1e30f, uMax = -1e30f, vMin = 1e30f, vMax = -1e30f;
        std::vector<float> pU(nv), pV(nv);
        for (size_t i = 0; i < nv; ++i) {
            float dx = portal.vx(i) - cx, dy = portal.vy(i) - cy, dz = portal.vz(i) - cz;
            pU[i] = dx*tux + dy*tuy + dz*tuz;
            pV[i] = dx*tvx + dy*tvy + dz*tvz;
            uMin = std::min(uMin, pU[i]); uMax = std::max(uMax, pU[i]);
            vMin = std::min(vMin, pV[i]); vMax = std::max(vMax, pV[i]);
        }

        auto pointInPoly = [&](float u, float v) -> bool {
            int crossings = 0;
            for (size_t i = 0, j = nv - 1; i < nv; j = i++) {
                float yi = pV[i], yj = pV[j], xi = pU[i], xj = pU[j];
                if (((yi <= v && yj > v) || (yj <= v && yi > v))) {
                    float t = (v - yi) / (yj - yi);
                    if (u < xi + t * (xj - xi)) crossings++;
                }
            }
            return (crossings & 1) != 0;
        };

        int gridW = static_cast<int>((uMax - uMin) / gridSpacing) + 1;
        int gridH = static_cast<int>((vMax - vMin) / gridSpacing) + 1;
        if (gridW > 120) gridW = 120;
        if (gridH > 60) gridH = 60;

        int blocked = 0, open = 0;
        std::vector<char> grid(gridW * gridH, ' ');

        for (int gv = 0; gv < gridH; ++gv) {
            float pv = vMin + (gv + 0.5f) * (vMax - vMin) / gridH;
            for (int gu = 0; gu < gridW; ++gu) {
                float pu = uMin + (gu + 0.5f) * (uMax - uMin) / gridW;

                if (!pointInPoly(pu, pv)) continue;

                float px = cx + pu*tux + pv*tvx;
                float py = cy + pu*tuy + pv*tvy;
                float pz = cz + pu*tuz + pv*tvz;

                // CheckLOS: true = clear, false = blocked
                glm::vec3 origin(px, py, pz);
                glm::vec3 endA(px + nx*rayLen, py + ny*rayLen, pz + nz*rayLen);
                glm::vec3 endB(px - nx*rayLen, py - ny*rayLen, pz - nz*rayLen);

                bool hit = !hcmap.CheckLOS(origin, endA) || !hcmap.CheckLOS(origin, endB);

                if (hit) {
                    grid[gv * gridW + gu] = '#';
                    blocked++;
                } else {
                    grid[gv * gridW + gu] = '.';
                    open++;
                }
            }
        }

        std::cout << "--- Portal " << pi << ": R" << portal.regionA << " <-> R" << portal.regionB << " ---\n";
        std::cout << "  Center: (" << cx << ", " << cy << ", " << cz << ")\n";
        std::cout << "  Normal: (" << nx << ", " << ny << ", " << nz << ")\n";
        std::cout << "  Area: " << portal.area << "  Grid: " << gridW << "x" << gridH
                  << "  Blocked: " << blocked << "  Open: " << open << "\n";

        for (int gv = gridH - 1; gv >= 0; --gv) {
            std::cout << "  |";
            for (int gu = 0; gu < gridW; ++gu) {
                std::cout << grid[gv * gridW + gu];
            }
            std::cout << "|\n";
        }
        std::cout << "\n";
    }

    std::cout << "=== " << portalCount << " portals in area ===\n\n";
}

static bool processZone(const std::string& eqPath, const std::string& zoneName,
                         const std::string& outputDir, bool verbose, float threshold,
                         const DebugArea& debugArea = {},
                         bool debugRaycast = false, float rayLen = 15.0f, float gridSpacing = 0.5f,
                         const std::string& mapsPath = "",
                         float zMin = -1e30f, float zMax = 1e30f) {
    std::string s3dPath = eqPath + "/" + zoneName + ".s3d";
    std::string wldName = zoneName + ".wld";

    if (!fs::exists(s3dPath)) {
        std::cerr << "S3D file not found: " << s3dPath << std::endl;
        return false;
    }

    WldLoader loader;
    if (!loader.parseFromArchive(s3dPath, wldName)) {
        std::cerr << "Failed to parse WLD: " << s3dPath << " / " << wldName << std::endl;
        return false;
    }

    auto bspTree = loader.getBspTree();
    if (!bspTree) {
        std::cerr << "No BSP tree in " << zoneName << std::endl;
        return false;
    }

    if (!loader.hasPvsData()) {
        std::cerr << "No PVS data in " << zoneName << " (needed for portal extraction)" << std::endl;
        return false;
    }

    ClassifierStats stats;
    stats.totalRegions = bspTree->regions.size();

    // Step 1: Compute region AABBs and zone bounds
    std::map<size_t, AABB> regionAABBs;
    float boundsMinX = std::numeric_limits<float>::max();
    float boundsMinY = std::numeric_limits<float>::max();
    float boundsMinZ = std::numeric_limits<float>::max();
    float boundsMaxX = std::numeric_limits<float>::lowest();
    float boundsMaxY = std::numeric_limits<float>::lowest();
    float boundsMaxZ = std::numeric_limits<float>::lowest();

    for (size_t i = 0; i < stats.totalRegions; ++i) {
        AABB aabb;
        if (computeRegionAABB(i, loader, aabb)) {
            regionAABBs[i] = aabb;
            boundsMinX = std::min(boundsMinX, aabb.minX);
            boundsMinY = std::min(boundsMinY, aabb.minY);
            boundsMinZ = std::min(boundsMinZ, aabb.minZ);
            boundsMaxX = std::max(boundsMaxX, aabb.maxX);
            boundsMaxY = std::max(boundsMaxY, aabb.maxY);
            boundsMaxZ = std::max(boundsMaxZ, aabb.maxZ);
        }
    }

    std::cout << zoneName << ": " << stats.totalRegions << " BSP regions, "
              << regionAABBs.size() << " with geometry" << std::endl;

    // Step 2: Extract BSP boundary portals (same algorithm as runtime PortalSystem)
    PortalSystem portalSystem;
    portalSystem.buildFromBsp(*bspTree, boundsMinX, boundsMinY, boundsMinZ,
                               boundsMaxX, boundsMaxY, boundsMaxZ);

    const auto& portalData = portalSystem.getData();
    stats.totalBspPortals = portalData.portals.size();

    std::cout << "  BSP portals extracted: " << stats.totalBspPortals << std::endl;

    // Debug area diagnostic (if requested)
    if (debugArea.enabled && !debugRaycast) {
        debugPortalsInArea(debugArea, portalData, loader, 1.0f);
        return true;
    }
    if (debugArea.enabled && debugRaycast && !mapsPath.empty()) {
        // HCMap-based raycast debug
        std::unique_ptr<HCMap> hcmap(HCMap::LoadMapFile(zoneName, mapsPath));
        if (!hcmap || !hcmap->IsLoaded()) {
            std::cerr << "Failed to load HCMap from " << mapsPath << "/" << zoneName << ".map" << std::endl;
            return false;
        }
        auto ms = hcmap->GetMemoryStats();
        std::cout << "  HCMap loaded: " << ms.vertexCount << " verts, " << ms.faceCount << " faces" << std::endl;
        debugHCMapRaycastInArea(debugArea, portalData, *hcmap, rayLen, gridSpacing, zMin, zMax);
        return true;
    }
    if (debugArea.enabled && debugRaycast) {
        debugRaycastInArea(debugArea, portalData, loader, rayLen, gridSpacing);
        return true;
    }

    // Step 3: Clip each portal against zone geometry to find actual openings
    std::vector<PortalStats> portalStatsList;
    portalStatsList.reserve(portalData.portals.size());

    // Per-region accumulators
    std::unordered_map<size_t, float> regionOpeningArea;
    std::unordered_map<size_t, float> regionBoundaryArea;
    std::unordered_map<size_t, int> regionPortalCount;
    std::unordered_map<size_t, int> regionDoorwayCount;

    int processedCount = 0;
    int progressInterval = std::max(1, static_cast<int>(portalData.portals.size()) / 20);

    for (size_t pi = 0; pi < portalData.portals.size(); ++pi) {
        const auto& portal = portalData.portals[pi];

        // Clip portal against HCMap collision mesh
        // For non-debug full processing, HCMap must be loaded
        // TODO: Load HCMap once at top of processZone for full pipeline
        ClippedPortalResult clipped;
        clipped.regionA = portal.regionA;
        clipped.regionB = portal.regionB;
        clipped.totalBoundaryArea = portal.area;
        clipped.wallCoveredArea = 0;
        clipped.totalOpeningArea = portal.area;
        clipped.blockedCellCount = 0;

        // Classify this portal
        PortalStats ps;
        ps.portalIndex = pi;
        ps.regionA = portal.regionA;
        ps.regionB = portal.regionB;
        ps.boundaryArea = clipped.totalBoundaryArea;
        ps.coveredArea = clipped.wallCoveredArea;
        ps.openingArea = clipped.totalOpeningArea;
        ps.coplanarTriangles = clipped.blockedCellCount;
        ps.openingCount = static_cast<int>(clipped.openings.size());

        if (clipped.isFullyOpen()) {
            ps.classification = "open-air";
            stats.openAirPortals++;
        } else if (clipped.isFullyCovered()) {
            ps.classification = "wall";
            stats.fullWallPortals++;
        } else {
            ps.classification = "doorway";
            stats.doorwayPortals++;
        }

        portalStatsList.push_back(ps);

        // Accumulate per-region opening areas
        // Only count actual openings (doorway portals), not open-air boundaries
        if (clipped.isPartial()) {
            regionOpeningArea[portal.regionA] += clipped.totalOpeningArea;
            regionOpeningArea[portal.regionB] += clipped.totalOpeningArea;
            regionDoorwayCount[portal.regionA]++;
            regionDoorwayCount[portal.regionB]++;
        }
        regionBoundaryArea[portal.regionA] += clipped.totalBoundaryArea;
        regionBoundaryArea[portal.regionB] += clipped.totalBoundaryArea;
        regionPortalCount[portal.regionA]++;
        regionPortalCount[portal.regionB]++;

        processedCount++;
        if (processedCount % progressInterval == 0) {
            std::cout << "  Clipping portals: " << processedCount << " / "
                      << portalData.portals.size() << std::endl;
        }
    }

    std::cout << "  Portal classification: "
              << stats.openAirPortals << " open-air, "
              << stats.fullWallPortals << " wall, "
              << stats.doorwayPortals << " doorway" << std::endl;

    // Step 4: Classify each region by opening ratio
    std::vector<RegionClassification> classifications;
    classifications.reserve(stats.totalRegions);

    for (size_t i = 0; i < stats.totalRegions; ++i) {
        RegionClassification c{};
        c.regionIndex = i;
        c.indoor = false;
        c.totalTriangles = 0;
        c.totalOpeningArea = 0;
        c.totalBoundaryArea = 0;
        c.aabbSurfaceArea = 0;
        c.portalRatio = 999.0f;
        c.portalCount = 0;
        c.doorwayCount = 0;

        auto geom = loader.getGeometryForRegion(i);
        if (geom && !geom->triangles.empty()) {
            c.totalTriangles = static_cast<int>(geom->triangles.size());
        }

        auto aabbIt = regionAABBs.find(i);
        if (aabbIt != regionAABBs.end() && c.totalTriangles > 0) {
            stats.regionsWithGeometry++;

            c.aabbSurfaceArea = aabbIt->second.surfaceArea();
            c.totalOpeningArea = regionOpeningArea[i];
            c.totalBoundaryArea = regionBoundaryArea[i];
            c.portalCount = regionPortalCount[i];
            c.doorwayCount = regionDoorwayCount[i];

            if (c.aabbSurfaceArea > 0.0f) {
                c.portalRatio = c.totalOpeningArea / c.aabbSurfaceArea;
            }

            // Indoor = has doorway portals with small opening ratio
            // Must have at least one actual doorway portal (not just open-air boundaries)
            c.indoor = (c.portalRatio <= threshold && c.doorwayCount > 0);

            if (c.indoor) {
                stats.indoorRegions++;
            } else {
                stats.outdoorRegions++;
            }
        }

        classifications.push_back(c);
    }

    // Write JSON output
    fs::create_directories(outputDir);
    std::string outputPath = outputDir + "/" + zoneName + ".json";
    std::ofstream out(outputPath);
    if (!out.is_open()) {
        std::cerr << "Failed to open output file: " << outputPath << std::endl;
        return false;
    }

    out << "{\n";
    out << "  \"zone\": \"" << zoneName << "\",\n";
    out << "  \"totalRegions\": " << stats.totalRegions << ",\n";
    out << "  \"regionsWithGeometry\": " << stats.regionsWithGeometry << ",\n";
    out << "  \"indoorRegions\": " << stats.indoorRegions << ",\n";
    out << "  \"outdoorRegions\": " << stats.outdoorRegions << ",\n";
    out << "  \"totalBspPortals\": " << stats.totalBspPortals << ",\n";
    out << "  \"openAirPortals\": " << stats.openAirPortals << ",\n";
    out << "  \"fullWallPortals\": " << stats.fullWallPortals << ",\n";
    out << "  \"doorwayPortals\": " << stats.doorwayPortals << ",\n";
    out << "  \"rayLength\": " << std::fixed << std::setprecision(1) << rayLen << ",\n";
    out << "  \"gridSpacing\": " << std::setprecision(1) << gridSpacing << ",\n";
    out << "  \"portalRatioThreshold\": " << std::setprecision(2) << threshold << ",\n";
    out << "  \"regions\": [\n";

    bool first = true;
    for (const auto& c : classifications) {
        if (c.totalTriangles == 0) continue;

        if (!first) out << ",\n";
        first = false;

        out << "    { \"region\": " << c.regionIndex
            << ", \"indoor\": " << (c.indoor ? "true" : "false")
            << ", \"triangles\": " << c.totalTriangles
            << ", \"portalRatio\": " << std::fixed << std::setprecision(3) << c.portalRatio
            << ", \"openingArea\": " << std::setprecision(1) << c.totalOpeningArea
            << ", \"aabbArea\": " << c.aabbSurfaceArea
            << ", \"portals\": " << c.portalCount
            << ", \"doorways\": " << c.doorwayCount
            << " }";
    }
    out << "\n  ]\n}\n";
    out.close();

    std::cout << zoneName << ": " << stats.regionsWithGeometry << " regions with geometry, "
              << stats.indoorRegions << " indoor, " << stats.outdoorRegions << " outdoor"
              << " -> " << outputPath << std::endl;

    if (verbose) {
        std::cout << "\n--- Portal Summary ---\n";
        // Show some example doorway portals
        int doorwayShown = 0;
        for (const auto& ps : portalStatsList) {
            if (ps.classification != "doorway") continue;
            if (doorwayShown >= 20) break;
            std::cout << "  Portal " << ps.portalIndex
                      << ": R" << ps.regionA << "<->R" << ps.regionB
                      << "  boundary=" << std::fixed << std::setprecision(0) << ps.boundaryArea
                      << "  covered=" << ps.coveredArea
                      << "  opening=" << ps.openingArea
                      << "  openings=" << ps.openingCount
                      << "  coplanarTris=" << ps.coplanarTriangles
                      << "  [" << ps.classification << "]"
                      << std::endl;
            doorwayShown++;
        }
        if (stats.doorwayPortals > 20) {
            std::cout << "  ... (" << (stats.doorwayPortals - 20) << " more doorway portals)\n";
        }

        std::cout << "\n--- Region Classification (sorted by ratio) ---\n";
        auto sorted = classifications;
        std::sort(sorted.begin(), sorted.end(), [](const auto& a, const auto& b) {
            return a.portalRatio < b.portalRatio;
        });
        for (const auto& c : sorted) {
            if (c.totalTriangles == 0) continue;
            std::cout << "  R" << std::setw(4) << c.regionIndex
                      << ": " << (c.indoor ? "INDOOR " : "outdoor")
                      << "  ratio=" << std::fixed << std::setprecision(3) << c.portalRatio
                      << "  openArea=" << std::setprecision(0) << c.totalOpeningArea
                      << "  aabbArea=" << c.aabbSurfaceArea
                      << "  portals=" << c.portalCount
                      << "  doorways=" << c.doorwayCount
                      << "  tris=" << c.totalTriangles
                      << std::endl;
        }
    }

    return true;
}

static std::vector<std::string> findAllZones(const std::string& eqPath) {
    std::vector<std::string> zones;
    for (const auto& entry : fs::directory_iterator(eqPath)) {
        if (!entry.is_regular_file()) continue;
        std::string filename = entry.path().filename().string();
        if (filename.size() < 5) continue;

        std::string ext = filename.substr(filename.size() - 4);
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        if (ext != ".s3d") continue;

        std::string base = filename.substr(0, filename.size() - 4);
        if (base.size() > 4 && base.substr(base.size() - 4) == "_chr") continue;
        if (base.size() > 4 && base.substr(base.size() - 4) == "_obj") continue;
        if (base.size() > 5 && base.substr(base.size() - 5) == "_chr2") continue;
        if (base.find("global") == 0) continue;

        zones.push_back(base);
    }
    std::sort(zones.begin(), zones.end());
    return zones;
}

static void printUsage(const char* progName) {
    std::cerr << "Usage: " << progName << " [options]\n"
              << "\nRequired:\n"
              << "  --eq-path <path>       Path to EQ client directory (contains .s3d files)\n"
              << "  --zone <name>          Process a single zone (e.g., qeynos2)\n"
              << "  --all                  Process all zones found in eq-path\n"
              << "\nOptional:\n"
              << "  --output <path>        Output directory (default: data/region_maps)\n"
              << "  --threshold <val>      Portal ratio threshold for indoor (default: 0.3)\n"
              << "  --tolerance <val>      Coplanarity tolerance in EQ units (default: 1.0)\n"
              << "  -v, --verbose          Show per-portal and per-region details\n"
              << "  -h, --help             Show this help\n"
              << "\nAlgorithm:\n"
              << "  1. Extract BSP boundary portals from split planes\n"
              << "  2. Clip each portal against wall geometry (exact 2D polygon boolean)\n"
              << "  3. Classify portals: open-air (no walls), wall (fully blocked), doorway (opening)\n"
              << "  4. Classify regions by opening ratio = opening_area / AABB_surface_area\n"
              << "\nExamples:\n"
              << "  " << progName << " --eq-path /path/to/EQ --zone qeynos2 -v\n"
              << "  " << progName << " --eq-path /path/to/EQ --zone befallen -v --tolerance 2.0\n"
              << "  " << progName << " --eq-path /path/to/EQ --all --output data/region_maps\n";
}

int main(int argc, char** argv) {
    std::string eqPath;
    std::string zoneName;
    std::string outputDir = "data/region_maps";
    bool allZones = false;
    bool verbose = false;
    float threshold = 0.3f;
    std::string mapsPath;
    DebugArea debugArea;
    bool debugRaycast = false;
    float rayLen = 15.0f;
    float gridSpacing = 0.5f;
    float zMin = -1e30f, zMax = 1e30f;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--eq-path" && i + 1 < argc) {
            eqPath = argv[++i];
        } else if (arg == "--zone" && i + 1 < argc) {
            zoneName = argv[++i];
        } else if (arg == "--output" && i + 1 < argc) {
            outputDir = argv[++i];
        } else if (arg == "--threshold" && i + 1 < argc) {
            threshold = std::stof(argv[++i]);
        } else if (arg == "--maps-path" && i + 1 < argc) {
            mapsPath = argv[++i];
        } else if (arg == "--z-min" && i + 1 < argc) {
            zMin = std::stof(argv[++i]);
        } else if (arg == "--z-max" && i + 1 < argc) {
            zMax = std::stof(argv[++i]);
        } else if (arg == "--debug-area" && i + 4 < argc) {
            debugArea.enabled = true;
            debugArea.x1 = std::stof(argv[++i]);
            debugArea.y1 = std::stof(argv[++i]);
            debugArea.x2 = std::stof(argv[++i]);
            debugArea.y2 = std::stof(argv[++i]);
        } else if (arg == "--debug-raycast") {
            debugRaycast = true;
        } else if (arg == "--ray-len" && i + 1 < argc) {
            rayLen = std::stof(argv[++i]);
        } else if (arg == "--grid" && i + 1 < argc) {
            gridSpacing = std::stof(argv[++i]);
        } else if (arg == "--all") {
            allZones = true;
        } else if (arg == "-v" || arg == "--verbose") {
            verbose = true;
        } else if (arg == "-h" || arg == "--help") {
            printUsage(argv[0]);
            return 0;
        } else {
            std::cerr << "Unknown argument: " << arg << std::endl;
            printUsage(argv[0]);
            return 1;
        }
    }

    if (eqPath.empty()) {
        std::cerr << "Error: --eq-path is required\n";
        printUsage(argv[0]);
        return 1;
    }

    if (zoneName.empty() && !allZones) {
        std::cerr << "Error: --zone <name> or --all is required\n";
        printUsage(argv[0]);
        return 1;
    }

    if (!fs::exists(eqPath)) {
        std::cerr << "EQ path does not exist: " << eqPath << std::endl;
        return 1;
    }

    int failures = 0;

    if (allZones) {
        auto zones = findAllZones(eqPath);
        std::cout << "Found " << zones.size() << " zone S3D files" << std::endl;
        for (const auto& zone : zones) {
            if (!processZone(eqPath, zone, outputDir, verbose, threshold, debugArea, debugRaycast, rayLen, gridSpacing, mapsPath, zMin, zMax)) {
                failures++;
            }
        }
        std::cout << "\nDone: " << (zones.size() - failures) << " succeeded, "
                  << failures << " failed" << std::endl;
    } else {
        if (!processZone(eqPath, zoneName, outputDir, verbose, threshold, debugArea, debugRaycast, rayLen, gridSpacing, mapsPath, zMin, zMax)) {
            return 1;
        }
    }

    return failures > 0 ? 1 : 0;
}
