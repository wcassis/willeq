#include "client/graphics/portal_system.h"
#include "client/graphics/eq/wld_loader.h"
#include "common/logging.h"
#include <algorithm>
#include <cmath>

namespace EQT {
namespace Graphics {

// ============================================================================
// Convex polygon (winding) operations for BSP portal extraction
// ============================================================================

struct Winding {
    std::vector<float> verts;  // packed xyz
    size_t count() const { return verts.size() / 3; }
    float x(size_t i) const { return verts[i * 3 + 0]; }
    float y(size_t i) const { return verts[i * 3 + 1]; }
    float z(size_t i) const { return verts[i * 3 + 2]; }
    void push(float px, float py, float pz) {
        verts.push_back(px); verts.push_back(py); verts.push_back(pz);
    }
    bool valid() const { return count() >= 3; }
};

// Clip plane: dot(normal, point) + dist.  keepFront=true keeps dot >= 0 side.
struct ClipPlane {
    float nx, ny, nz, dist;
    bool keepFront;
};

static float planeDot(float nx, float ny, float nz, float d, float px, float py, float pz) {
    return nx * px + ny * py + nz * pz + d;
}

// Create a large winding on the split plane of a BSP node.
// The winding is a rectangle on the plane, sized to span the zone bounds.
static Winding makeBaseWinding(float nx, float ny, float nz, float dist,
                                float bMinX, float bMinY, float bMinZ,
                                float bMaxX, float bMaxY, float bMaxZ) {
    // Find the major axis of the normal to choose basis vectors
    float ax = std::fabs(nx), ay = std::fabs(ny), az = std::fabs(nz);

    // Choose two tangent vectors perpendicular to the normal
    float ux, uy, uz, vx, vy, vz;
    if (ax >= ay && ax >= az) {
        // Normal is mostly X — use Y and Z as tangents
        ux = 0; uy = 1; uz = 0;
        vx = 0; vy = 0; vz = 1;
    } else if (ay >= ax && ay >= az) {
        // Normal is mostly Y — use X and Z as tangents
        ux = 1; uy = 0; uz = 0;
        vx = 0; vy = 0; vz = 1;
    } else {
        // Normal is mostly Z — use X and Y as tangents
        ux = 1; uy = 0; uz = 0;
        vx = 0; vy = 1; vz = 0;
    }

    // Gram-Schmidt: make u perpendicular to normal
    float udotn = ux * nx + uy * ny + uz * nz;
    ux -= udotn * nx; uy -= udotn * ny; uz -= udotn * nz;
    float ulen = std::sqrt(ux * ux + uy * uy + uz * uz);
    if (ulen < 1e-8f) return {};
    ux /= ulen; uy /= ulen; uz /= ulen;

    // v = cross(normal, u)
    vx = ny * uz - nz * uy;
    vy = nz * ux - nx * uz;
    vz = nx * uy - ny * ux;

    // Find a point on the plane: p = normal * (-dist)
    // (since dot(normal, p) + dist = 0 => dot(normal, normal*t) = t => t = -dist)
    float cx = nx * (-dist);
    float cy = ny * (-dist);
    float cz = nz * (-dist);

    // Zone extent for winding size
    float dx = bMaxX - bMinX, dy = bMaxY - bMinY, dz = bMaxZ - bMinZ;
    float extent = std::sqrt(dx * dx + dy * dy + dz * dz) * 0.6f;

    Winding w;
    w.push(cx - extent * ux - extent * vx, cy - extent * uy - extent * vy, cz - extent * uz - extent * vz);
    w.push(cx + extent * ux - extent * vx, cy + extent * uy - extent * vy, cz + extent * uz - extent * vz);
    w.push(cx + extent * ux + extent * vx, cy + extent * uy + extent * vy, cz + extent * uz + extent * vz);
    w.push(cx - extent * ux + extent * vx, cy - extent * uy + extent * vy, cz - extent * uz + extent * vz);
    return w;
}

// Clip a winding by a plane. keepFront=true keeps the side where dot >= 0.
static Winding clipWinding(const Winding& w, float pnx, float pny, float pnz, float pdist, bool keepFront) {
    if (!w.valid()) return {};
    size_t n = w.count();

    const float EPSILON = 0.01f;

    // Classify each vertex
    std::vector<float> dots(n);
    for (size_t i = 0; i < n; ++i) {
        dots[i] = planeDot(pnx, pny, pnz, pdist, w.x(i), w.y(i), w.z(i));
        if (!keepFront) dots[i] = -dots[i];
    }

    Winding out;
    for (size_t i = 0; i < n; ++i) {
        size_t j = (i + 1) % n;

        bool iInside = dots[i] >= -EPSILON;
        bool jInside = dots[j] >= -EPSILON;

        if (iInside) {
            out.push(w.x(i), w.y(i), w.z(i));
        }

        if (iInside != jInside) {
            // Edge crosses plane — compute intersection
            float d1 = dots[i], d2 = dots[j];
            float t = d1 / (d1 - d2);
            if (t < 0.0f) t = 0.0f;
            if (t > 1.0f) t = 1.0f;
            out.push(w.x(i) + t * (w.x(j) - w.x(i)),
                     w.y(i) + t * (w.y(j) - w.y(i)),
                     w.z(i) + t * (w.z(j) - w.z(i)));
        }
    }

    return out;
}

// Compute area of a convex polygon using cross product accumulation
static float windingArea(const Winding& w) {
    if (w.count() < 3) return 0.0f;
    float ax = 0, ay = 0, az = 0;
    for (size_t i = 1; i + 1 < w.count(); ++i) {
        float e1x = w.x(i) - w.x(0), e1y = w.y(i) - w.y(0), e1z = w.z(i) - w.z(0);
        float e2x = w.x(i+1) - w.x(0), e2y = w.y(i+1) - w.y(0), e2z = w.z(i+1) - w.z(0);
        ax += e1y * e2z - e1z * e2y;
        ay += e1z * e2x - e1x * e2z;
        az += e1x * e2y - e1y * e2x;
    }
    return 0.5f * std::sqrt(ax * ax + ay * ay + az * az);
}

static void windingCenter(const Winding& w, float& cx, float& cy, float& cz) {
    cx = cy = cz = 0;
    for (size_t i = 0; i < w.count(); ++i) {
        cx += w.x(i); cy += w.y(i); cz += w.z(i);
    }
    float inv = 1.0f / static_cast<float>(w.count());
    cx *= inv; cy *= inv; cz *= inv;
}

// ============================================================================
// BSP portal extraction
// ============================================================================

// Push a portal winding down through a BSP subtree to find which leaf regions it reaches.
// Each leaf gets the winding clipped to its cell.
static void pushPortalToLeaves(int nodeIdx, const Winding& portal, const BspTree& bsp,
                                std::vector<std::pair<size_t, Winding>>& leafPortals) {
    if (nodeIdx < 0 || static_cast<size_t>(nodeIdx) >= bsp.nodes.size()) return;
    const auto& node = bsp.nodes[nodeIdx];

    // Leaf node: associate portal with this region
    if (node.regionId > 0) {
        size_t regionIdx = static_cast<size_t>(node.regionId - 1);
        if (portal.valid()) {
            leafPortals.push_back({regionIdx, portal});
        }
        return;
    }

    // Internal node: clip portal by this node's split plane and recurse
    // EQ BSP: dot >= 0 -> left (front), dot < 0 -> right (back)
    Winding front = clipWinding(portal, node.normalX, node.normalY, node.normalZ, node.splitDistance, true);
    Winding back = clipWinding(portal, node.normalX, node.normalY, node.normalZ, node.splitDistance, false);

    if (front.valid() && node.left >= 0)
        pushPortalToLeaves(node.left, front, bsp, leafPortals);
    if (back.valid() && node.right >= 0)
        pushPortalToLeaves(node.right, back, bsp, leafPortals);
}

// Recursive BSP portal generation.
// At each internal node, creates a portal on the split plane, clips it against
// ancestor planes, then pushes it through both subtrees to find leaf connections.
static void generatePortalsRecursive(int nodeIdx, const BspTree& bsp,
                                      float bMinX, float bMinY, float bMinZ,
                                      float bMaxX, float bMaxY, float bMaxZ,
                                      std::vector<ClipPlane>& ancestorPlanes,
                                      std::vector<Portal>& outPortals) {
    if (nodeIdx < 0 || static_cast<size_t>(nodeIdx) >= bsp.nodes.size()) return;
    const auto& node = bsp.nodes[nodeIdx];

    // Leaf: no portals to generate
    if (node.regionId > 0) return;

    // Generate a large winding on this node's split plane
    Winding winding = makeBaseWinding(node.normalX, node.normalY, node.normalZ, node.splitDistance,
                                       bMinX, bMinY, bMinZ, bMaxX, bMaxY, bMaxZ);

    // Clip against all ancestor planes
    for (const auto& ap : ancestorPlanes) {
        if (!winding.valid()) break;
        winding = clipWinding(winding, ap.nx, ap.ny, ap.nz, ap.dist, ap.keepFront);
    }

    if (winding.valid()) {
        // Push through left subtree (front side) and right subtree (back side)
        std::vector<std::pair<size_t, Winding>> leftLeaves, rightLeaves;

        if (node.left >= 0)
            pushPortalToLeaves(node.left, winding, bsp, leftLeaves);
        if (node.right >= 0)
            pushPortalToLeaves(node.right, winding, bsp, rightLeaves);

        // Create portals for each leaf pair
        for (const auto& [regionA, polyA] : leftLeaves) {
            for (const auto& [regionB, polyB] : rightLeaves) {
                // Use polyA (the winding clipped to regionA's cell) as the portal shape.
                // Both polyA and polyB represent the same split plane clipped to different
                // leaf cells — take the smaller one (should be similar).
                float areaA = windingArea(polyA);
                float areaB = windingArea(polyB);
                const Winding& poly = (areaA <= areaB) ? polyA : polyB;
                float area = std::min(areaA, areaB);

                if (area < 1.0f) continue;  // Skip degenerate portals

                Portal p;
                p.regionA = regionA;
                p.regionB = regionB;
                p.vertices = poly.verts;
                p.normalX = node.normalX;
                p.normalY = node.normalY;
                p.normalZ = node.normalZ;
                p.area = area;
                windingCenter(poly, p.centerX, p.centerY, p.centerZ);

                outPortals.push_back(std::move(p));
            }
        }
    }

    // Recurse into left child (front side: dot >= 0)
    if (node.left >= 0) {
        ancestorPlanes.push_back({node.normalX, node.normalY, node.normalZ, node.splitDistance, true});
        generatePortalsRecursive(node.left, bsp, bMinX, bMinY, bMinZ, bMaxX, bMaxY, bMaxZ,
                                  ancestorPlanes, outPortals);
        ancestorPlanes.pop_back();
    }

    // Recurse into right child (back side: dot < 0)
    if (node.right >= 0) {
        ancestorPlanes.push_back({node.normalX, node.normalY, node.normalZ, node.splitDistance, false});
        generatePortalsRecursive(node.right, bsp, bMinX, bMinY, bMinZ, bMaxX, bMaxY, bMaxZ,
                                  ancestorPlanes, outPortals);
        ancestorPlanes.pop_back();
    }
}

// ============================================================================
// PortalSystem public interface
// ============================================================================

const std::vector<size_t>& PortalSystem::getPortalsForRegion(size_t regionIdx) const {
    auto it = data_.regionPortals.find(regionIdx);
    if (it != data_.regionPortals.end()) {
        return it->second;
    }
    return emptyList_;
}

size_t PortalSystem::getOtherRegion(size_t portalIdx, size_t fromRegion) const {
    if (portalIdx >= data_.portals.size()) return SIZE_MAX;
    const auto& p = data_.portals[portalIdx];
    if (p.regionA == fromRegion) return p.regionB;
    if (p.regionB == fromRegion) return p.regionA;
    return SIZE_MAX;
}

void PortalSystem::buildFromBsp(const BspTree& bsp,
                                 float boundsMinX, float boundsMinY, float boundsMinZ,
                                 float boundsMaxX, float boundsMaxY, float boundsMaxZ) {
    data_.portals.clear();
    data_.regionPortals.clear();

    if (bsp.nodes.empty() || bsp.regions.empty()) {
        return;
    }

    std::vector<ClipPlane> ancestorPlanes;
    std::vector<Portal> rawPortals;

    generatePortalsRecursive(0, bsp, boundsMinX, boundsMinY, boundsMinZ,
                              boundsMaxX, boundsMaxY, boundsMaxZ,
                              ancestorPlanes, rawPortals);

    // Filter: only keep portals between regions that have geometry (containsPolygons)
    // and that have mutual PVS visibility
    for (auto& p : rawPortals) {
        if (p.regionA >= bsp.regions.size() || p.regionB >= bsp.regions.size()) continue;
        const auto& regA = bsp.regions[p.regionA];
        const auto& regB = bsp.regions[p.regionB];
        if (!regA || !regB) continue;
        if (!regA->containsPolygons || !regB->containsPolygons) continue;

        // Check mutual PVS visibility
        bool aSeesB = (p.regionB < regA->visibleRegions.size()) && regA->visibleRegions[p.regionB];
        bool bSeesA = (p.regionA < regB->visibleRegions.size()) && regB->visibleRegions[p.regionA];
        if (!aSeesB || !bSeesA) continue;

        size_t portalIdx = data_.portals.size();
        data_.portals.push_back(std::move(p));
        data_.regionPortals[data_.portals.back().regionA].push_back(portalIdx);
        data_.regionPortals[data_.portals.back().regionB].push_back(portalIdx);
    }

    LOG_INFO(MOD_GRAPHICS, "Portal system: extracted {} portals from BSP split planes ({} raw, {} regions)",
             data_.portals.size(), rawPortals.size(), bsp.regions.size());
}

} // namespace Graphics
} // namespace EQT
