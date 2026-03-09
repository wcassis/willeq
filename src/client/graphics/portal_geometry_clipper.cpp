#include "client/graphics/portal_geometry_clipper.h"
#include "client/graphics/portal_system.h"
#include "client/hc_map.h"
#include <algorithm>
#include <cmath>
#include <vector>
#include <queue>
#include <glm/glm.hpp>

namespace EQT {
namespace Graphics {

// ============================================================================
// 2D projection basis for portal plane
// ============================================================================

struct PlaneProjection {
    double ox, oy, oz;       // Origin on plane
    double ux, uy, uz;       // Tangent u
    double vx, vy, vz;       // Tangent v
    double nx, ny, nz;       // Normal

    void project(double px, double py, double pz, double& outU, double& outV) const {
        double dx = px - ox, dy = py - oy, dz = pz - oz;
        outU = dx * ux + dy * uy + dz * uz;
        outV = dx * vx + dy * vy + dz * vz;
    }

    void unproject(double u, double v, float& outX, float& outY, float& outZ) const {
        outX = static_cast<float>(ox + u * ux + v * vx);
        outY = static_cast<float>(oy + u * uy + v * vy);
        outZ = static_cast<float>(oz + u * uz + v * vz);
    }
};

static PlaneProjection makePlaneProjection(float fnx, float fny, float fnz,
                                            float cx, float cy, float cz) {
    PlaneProjection pp;
    pp.nx = fnx; pp.ny = fny; pp.nz = fnz;
    pp.ox = cx; pp.oy = cy; pp.oz = cz;

    // Gram-Schmidt: pick a seed vector not parallel to normal
    double ax = std::fabs(fnx), ay = std::fabs(fny), az = std::fabs(fnz);
    double tux, tuy, tuz;
    if (ax >= ay && ax >= az) {
        tux = 0; tuy = 1; tuz = 0;
    } else {
        tux = 1; tuy = 0; tuz = 0;
    }

    // Subtract normal component
    double d = tux * fnx + tuy * fny + tuz * fnz;
    tux -= d * fnx; tuy -= d * fny; tuz -= d * fnz;
    double len = std::sqrt(tux * tux + tuy * tuy + tuz * tuz);
    pp.ux = tux / len; pp.uy = tuy / len; pp.uz = tuz / len;

    // v = normal x u
    pp.vx = fny * pp.uz - fnz * pp.uy;
    pp.vy = fnz * pp.ux - fnx * pp.uz;
    pp.vz = fnx * pp.uy - fny * pp.ux;

    return pp;
}

// ============================================================================
// Point-in-polygon test (2D, crossing number)
// ============================================================================

static bool pointInPolygon2D(double px, double py,
                              const std::vector<double>& polyU,
                              const std::vector<double>& polyV) {
    int crossings = 0;
    size_t n = polyU.size();
    for (size_t i = 0, j = n - 1; i < n; j = i++) {
        double yi = polyV[i], yj = polyV[j];
        double xi = polyU[i], xj = polyU[j];
        if (((yi <= py && yj > py) || (yj <= py && yi > py))) {
            double t = (py - yi) / (yj - yi);
            if (px < xi + t * (xj - xi))
                crossings++;
        }
    }
    return (crossings & 1) != 0;
}

// ============================================================================
// Flood-fill for connected components of open cells
// ============================================================================

static void floodFill(int startRow, int startCol, int rows, int cols,
                       const std::vector<bool>& blocked,
                       std::vector<int>& labels, int label) {
    std::queue<std::pair<int, int>> q;
    q.push({startRow, startCol});
    labels[startRow * cols + startCol] = label;

    while (!q.empty()) {
        auto [r, c] = q.front();
        q.pop();

        const int dr[] = {-1, 1, 0, 0};
        const int dc[] = {0, 0, -1, 1};
        for (int d = 0; d < 4; ++d) {
            int nr = r + dr[d], nc = c + dc[d];
            if (nr < 0 || nr >= rows || nc < 0 || nc >= cols) continue;
            int idx = nr * cols + nc;
            if (blocked[idx] || labels[idx] >= 0) continue;
            labels[idx] = label;
            q.push({nr, nc});
        }
    }
}

// ============================================================================
// Contour extraction — collect boundary edges between open and solid cells,
// then chain them into a closed polygon.
//
// For each open cell in the component, check its 4 neighbors. If the neighbor
// is solid (blocked, outside portal, different component), emit the shared
// edge as a directed segment (clockwise winding around the open region).
// Then chain the segments end-to-end to form the contour.
// ============================================================================

static bool cellIsSolid(int r, int c, int rows, int cols,
                         const std::vector<bool>& blocked,
                         const std::vector<bool>& insidePortal,
                         const std::vector<int>& labels, int comp) {
    if (r < 0 || r >= rows || c < 0 || c >= cols) return true;
    int idx = r * cols + c;
    if (!insidePortal[idx]) return true;
    if (blocked[idx]) return true;
    if (labels[idx] != comp) return true;
    return false;
}

struct EdgeSeg {
    double u0, v0, u1, v1; // directed segment
};

static void traceContour(int comp, int rows, int cols,
                          const std::vector<bool>& blocked,
                          const std::vector<bool>& insidePortal,
                          const std::vector<int>& labels,
                          double minU, double minV, double stepU, double stepV,
                          std::vector<double>& contourU, std::vector<double>& contourV) {
    contourU.clear();
    contourV.clear();

    // Cell (r,c) occupies the rectangle:
    //   U: [minU + (c-0.5)*stepU, minU + (c+0.5)*stepU]
    //   V: [minV + (r-0.5)*stepV, minV + (r+0.5)*stepV]

    // Collect directed boundary edges (clockwise around open region)
    // Use a map from start vertex to end vertex for chaining
    // Vertex key: (corner_col, corner_row) as integers
    // Cell (r,c) has corners at (c, r), (c+1, r), (c+1, r+1), (c, r+1)
    // (using shifted indices where corner (ic, ir) maps to U/V coords)

    std::map<std::pair<int,int>, std::pair<int,int>> edgeMap; // start -> end

    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < cols; ++c) {
            if (labels[r * cols + c] != comp) continue;

            // This cell is open. Check 4 neighbors.
            // Top neighbor (r-1, c): if solid, emit top edge right-to-left → (c+1,r) to (c,r)
            if (cellIsSolid(r-1, c, rows, cols, blocked, insidePortal, labels, comp))
                edgeMap[{c+1, r}] = {c, r};
            // Bottom (r+1, c): if solid, emit bottom edge left-to-right → (c,r+1) to (c+1,r+1)
            if (cellIsSolid(r+1, c, rows, cols, blocked, insidePortal, labels, comp))
                edgeMap[{c, r+1}] = {c+1, r+1};
            // Left (r, c-1): if solid, emit left edge top-to-bottom... wait
            // For CW winding: left edge goes (c,r) to (c,r+1)
            if (cellIsSolid(r, c-1, rows, cols, blocked, insidePortal, labels, comp))
                edgeMap[{c, r}] = {c, r+1};
            // Right (r, c+1): if solid, emit right edge (c+1,r+1) to (c+1,r)
            if (cellIsSolid(r, c+1, rows, cols, blocked, insidePortal, labels, comp))
                edgeMap[{c+1, r+1}] = {c+1, r};
        }
    }

    if (edgeMap.empty()) return;

    // Chain edges into a polygon starting from any edge
    auto start = edgeMap.begin()->first;
    auto cur = start;
    int maxIter = static_cast<int>(edgeMap.size()) + 1;
    int iter = 0;

    do {
        // Convert corner indices to U/V coordinates
        double u = minU + (cur.first - 0.5) * stepU;
        double v = minV + (cur.second - 0.5) * stepV;
        contourU.push_back(u);
        contourV.push_back(v);

        auto it = edgeMap.find(cur);
        if (it == edgeMap.end()) break; // broken chain
        cur = it->second;
    } while (cur != start && ++iter < maxIter);

    // Simplify: remove collinear points
    if (contourU.size() < 3) return;
    std::vector<double> simpU, simpV;
    size_t n = contourU.size();
    for (size_t i = 0; i < n; ++i) {
        size_t prev = (i + n - 1) % n;
        size_t next = (i + 1) % n;
        double du1 = contourU[i] - contourU[prev];
        double dv1 = contourV[i] - contourV[prev];
        double du2 = contourU[next] - contourU[i];
        double dv2 = contourV[next] - contourV[i];
        if (std::fabs(du1 * dv2 - dv1 * du2) > 1e-10) {
            simpU.push_back(contourU[i]);
            simpV.push_back(contourV[i]);
        }
    }
    contourU = std::move(simpU);
    contourV = std::move(simpV);
}

// ============================================================================
// Main HCMap-based clipping function
// ============================================================================

ClippedPortalResult clipPortalAgainstGeometry(
    const Portal& portal,
    const HCMap* map,
    float rayLength,
    float gridSpacing) {

    ClippedPortalResult result{};
    result.regionA = portal.regionA;
    result.regionB = portal.regionB;
    result.normalX = portal.normalX;
    result.normalY = portal.normalY;
    result.normalZ = portal.normalZ;
    result.totalBoundaryArea = portal.area;
    result.wallCoveredArea = 0;
    result.totalOpeningArea = 0;
    result.blockedCellCount = 0;

    size_t nv = portal.vertexCount();
    if (nv < 3 || !map || !map->IsLoaded()) return result;

    float nx = portal.normalX, ny = portal.normalY, nz = portal.normalZ;

    // Set up 2D projection
    PlaneProjection proj = makePlaneProjection(nx, ny, nz,
        portal.centerX, portal.centerY, portal.centerZ);

    // Project portal vertices to 2D
    std::vector<double> portalU(nv), portalV(nv);
    double minU = 1e30, maxU = -1e30, minV = 1e30, maxV = -1e30;
    for (size_t i = 0; i < nv; ++i) {
        proj.project(portal.vx(i), portal.vy(i), portal.vz(i), portalU[i], portalV[i]);
        minU = std::min(minU, portalU[i]);
        maxU = std::max(maxU, portalU[i]);
        minV = std::min(minV, portalV[i]);
        maxV = std::max(maxV, portalV[i]);
    }

    double spanU = maxU - minU;
    double spanV = maxV - minV;
    if (spanU < gridSpacing || spanV < gridSpacing) {
        // Portal too small for grid — treat as fully open
        result.totalOpeningArea = result.totalBoundaryArea;
        return result;
    }

    int cols = static_cast<int>(std::ceil(spanU / gridSpacing)) + 1;
    int rows = static_cast<int>(std::ceil(spanV / gridSpacing)) + 1;

    const int MAX_GRID = 512;
    if (cols > MAX_GRID) cols = MAX_GRID;
    if (rows > MAX_GRID) rows = MAX_GRID;

    double stepU = spanU / (cols - 1);
    double stepV = spanV / (rows - 1);

    // Cast rays at each grid point using HCMap's CheckLOS
    int totalCells = rows * cols;
    std::vector<bool> blocked(totalCells, false);
    std::vector<bool> insidePortal(totalCells, false);
    int blockedCount = 0;
    int insideCount = 0;

    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < cols; ++c) {
            double u = minU + c * stepU;
            double v = minV + r * stepV;

            if (!pointInPolygon2D(u, v, portalU, portalV)) continue;
            int idx = r * cols + c;
            insidePortal[idx] = true;
            insideCount++;

            // Unproject to 3D (EQ coords: x, y, z-up)
            float px, py, pz;
            proj.unproject(u, v, px, py, pz);

            // Cast ray in +normal direction: from portal point to point+normal*rayLength
            // CheckLOS returns true if clear, false if blocked
            glm::vec3 origin(px, py, pz);
            glm::vec3 endA(px + nx * rayLength, py + ny * rayLength, pz + nz * rayLength);
            glm::vec3 endB(px - nx * rayLength, py - ny * rayLength, pz - nz * rayLength);

            bool hit = !map->CheckLOS(origin, endA) || !map->CheckLOS(origin, endB);

            if (hit) {
                blocked[idx] = true;
                blockedCount++;
            }
        }
    }

    if (insideCount == 0) return result;

    double cellArea = stepU * stepV;
    result.wallCoveredArea = static_cast<float>(blockedCount * cellArea);
    result.totalOpeningArea = static_cast<float>((insideCount - blockedCount) * cellArea);
    result.blockedCellCount = blockedCount;

    // Fully blocked — no openings
    if (blockedCount >= insideCount) {
        result.totalOpeningArea = 0;
        return result;
    }

    // Fully open — no openings list (caller uses isFullyOpen())
    if (blockedCount == 0) {
        return result;
    }

    // Flood-fill connected components of open cells
    std::vector<int> labels(totalCells, -1);
    int numComponents = 0;

    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < cols; ++c) {
            int idx = r * cols + c;
            if (!insidePortal[idx] || blocked[idx] || labels[idx] >= 0) continue;
            floodFill(r, c, rows, cols, blocked, labels, numComponents);
            numComponents++;
        }
    }

    // Build opening polygon for each component via contour tracing
    for (int comp = 0; comp < numComponents; ++comp) {
        // Count cells in this component
        int cellCount = 0;
        for (int i = 0; i < totalCells; ++i) {
            if (labels[i] == comp) cellCount++;
        }
        if (cellCount < 1) continue;

        float compArea = static_cast<float>(cellCount * cellArea);
        if (compArea < 1.0f) continue;

        // Trace the boundary contour
        std::vector<double> contourU, contourV;
        traceContour(comp, rows, cols, blocked, insidePortal, labels,
                     minU, minV, stepU, stepV, contourU, contourV);

        if (contourU.size() < 3) continue;

        // Convert contour to 3D
        PortalOpening opening;
        opening.area = compArea;
        opening.vertices.reserve(contourU.size() * 3);
        float cx = 0, cy = 0, cz = 0;

        for (size_t i = 0; i < contourU.size(); ++i) {
            float x3, y3, z3;
            proj.unproject(contourU[i], contourV[i], x3, y3, z3);
            opening.vertices.push_back(x3);
            opening.vertices.push_back(y3);
            opening.vertices.push_back(z3);
            cx += x3; cy += y3; cz += z3;
        }

        float inv = 1.0f / static_cast<float>(contourU.size());
        opening.centerX = cx * inv;
        opening.centerY = cy * inv;
        opening.centerZ = cz * inv;
        result.openings.push_back(std::move(opening));
    }

    return result;
}

} // namespace Graphics
} // namespace EQT
