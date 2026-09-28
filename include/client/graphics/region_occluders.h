#ifndef EQT_GRAPHICS_REGION_OCCLUDERS_H
#define EQT_GRAPHICS_REGION_OCCLUDERS_H

// The ONE selection of a BSP region's software-occlusion occluders, shared by
// the client's zone load (ZoneLoadComponent::loadZoneStep4_Regions) and the
// CI gate's benchmark loader (tools/hwcaps/zone_bench_loader.cpp).
//
// Why it exists (spatial budget overrun, B813 follow-up): the two loaders
// carried their own copies of this loop. T504 right-sized the client's copy
// (push_back growth + resize(maxPerRegion) kept each region's vector at its
// PRE-truncation capacity; dreadlands 30,381 kept triangles held 7.34 MiB for
// 1.16 MiB of data) but not the benchmark's, so the gate -- which measures the
// benchmark -- kept charging the dead capacity to `spatial`. One body means
// the fix cannot drift again; tests/test_region_occluders.cpp pins
// capacity == size <= maxPerRegion.
//
// A template over the geometry type so this header needs neither the WLD
// loader nor Irrlicht: Geom needs `vertices` (x,y,z), `triangles`
// (v1,v2,v3) and centerX/centerY/centerZ, which ZoneGeometry has.

#include "client/graphics/software_occlusion_culler.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace EQT {
namespace Graphics {

// Every well-formed triangle whose world-space area (EQ coords, region centre
// applied) is >= minArea; when more than maxPerRegion qualify, the
// maxPerRegion largest are kept. The result's capacity equals its size.
// Selection and order are exactly those of the two loops it replaced
// (same push order, same std::sort, same resize).
template <typename Geom>
std::vector<OccluderTriangle> SelectRegionOccluders(const Geom& geom, float minArea, int maxPerRegion) {
    std::vector<OccluderTriangle> occluders;
    const size_t nv = geom.vertices.size();
    for (const auto& tri : geom.triangles) {
        if (tri.v1 >= nv || tri.v2 >= nv || tri.v3 >= nv) continue;

        const auto& va = geom.vertices[tri.v1];
        const auto& vb = geom.vertices[tri.v2];
        const auto& vc = geom.vertices[tri.v3];

        // World-space positions (add region center offset)
        float ax = geom.centerX + va.x, ay = geom.centerY + va.y, az = geom.centerZ + va.z;
        float bx = geom.centerX + vb.x, by = geom.centerY + vb.y, bz = geom.centerZ + vb.z;
        float cx = geom.centerX + vc.x, cy = geom.centerY + vc.y, cz = geom.centerZ + vc.z;

        // Cross product for area
        float ex = bx - ax, ey = by - ay, ez = bz - az;
        float fx = cx - ax, fy = cy - ay, fz = cz - az;
        float nx = ey * fz - ez * fy;
        float ny = ez * fx - ex * fz;
        float nz = ex * fy - ey * fx;
        float area = 0.5f * std::sqrt(nx * nx + ny * ny + nz * nz);

        if (area < minArea) continue;

        OccluderTriangle ot;
        ot.v0[0] = ax; ot.v0[1] = ay; ot.v0[2] = az;
        ot.v1[0] = bx; ot.v1[1] = by; ot.v1[2] = bz;
        ot.v2[0] = cx; ot.v2[1] = cy; ot.v2[2] = cz;
        ot.area = area;
        occluders.push_back(ot);
    }

    // Keep only the largest triangles per region
    if (maxPerRegion >= 0 && static_cast<int>(occluders.size()) > maxPerRegion) {
        std::sort(occluders.begin(), occluders.end(),
                  [](const OccluderTriangle& a, const OccluderTriangle& b) {
                      return a.area > b.area;
                  });
        occluders.resize(static_cast<size_t>(maxPerRegion));
    }

    // Right-size: without this every region keeps its pre-truncation capacity
    // for the whole zone visit (the defect this header exists for).
    occluders.shrink_to_fit();
    return occluders;
}

} // namespace Graphics
} // namespace EQT

#endif // EQT_GRAPHICS_REGION_OCCLUDERS_H
