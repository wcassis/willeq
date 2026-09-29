// region_world_triangles.h -- the zone's collision triangles per BSP region,
// held ONCE.
//
// B909 (GitLab #229). Collision, the detail system's ground query and every
// BSP-filtered ray read "the world-space triangles of region R". Before B909
// they read std::map<size_t, std::vector<irr::core::triangle3df>>, built at
// zone load by copying every region's triangles out of the zone draw pools:
// 36 B per triangle plus a map node and a vector per region, a second copy of
// vertices the pools already hold. The gate's benchmark never builds it, so
// only the CLIENT's own ledger shows it (refactor/B909/README.md has the
// measured bytes per zone).
//
// Now a region's triangles come from one of two places:
//   * POOLED zones (the orangepi preset's mergeZoneDraws): a view onto the
//     pools themselves (RegionTriangleSource, implemented by ZoneMergeBuffers).
//     No per-triangle storage; the triangles are read from the pool's vertices
//     through its index list, in the same order the copy was made.
//   * The legacy per-region-node path (WILLEQ_ZONE_POOLS=0, non-pooled
//     presets): an owned vector per region, exactly as before.
// RayIntersectTriangles (door_collision.h) runs the same Moller-Trumbore
// arithmetic over either, on the same floats, so every hit point, distance and
// hit triangle is bit-identical to the copy's (test_region_world_triangles).
//
// Lifetime: the pools live until ZoneLifecycleComponent::unloadZone(), which
// also clears this set; in pooled mode nothing destroys them mid-visit
// (/mergedraws refuses, a texture eviction swaps a texture and keeps the pool).
#ifndef EQT_GRAPHICS_REGION_WORLD_TRIANGLES_H
#define EQT_GRAPHICS_REGION_WORLD_TRIANGLES_H

#include <irrlicht.h>
#include <cstddef>
#include <functional>
#include <map>
#include <vector>

namespace EQT {
namespace Graphics {

// Anything that can enumerate one region's world-space triangles, in a fixed
// order, as three pointers to x,y,z (Irrlicht Y-up).
class RegionTriangleSource {
public:
    using TriangleFn = std::function<void(const float*, const float*, const float*)>;
    virtual ~RegionTriangleSource() = default;
    virtual void forEachRegionTriangle(size_t regionIdx, const TriangleFn& fn) const = 0;
    // Regions this source has at least one triangle for, ascending.
    virtual std::vector<size_t> regionsWithTriangles() const = 0;
};

// One region's triangles, by value and cheap to copy: either a pointer to an
// owned vector or (source, region). Tests false when the region has none.
class RegionTrianglesView {
public:
    RegionTrianglesView() = default;
    explicit RegionTrianglesView(const std::vector<irr::core::triangle3df>* owned) : owned_(owned) {}
    RegionTrianglesView(const RegionTriangleSource* source, size_t regionIdx)
        : source_(source), regionIdx_(regionIdx) {}
    explicit operator bool() const { return owned_ || source_; }

    // fn(const irr::core::triangle3df&) for every triangle, in storage order.
    template <class Fn>
    void forEach(Fn&& fn) const {
        if (owned_) {
            for (const auto& t : *owned_) fn(t);
        } else if (source_) {
            source_->forEachRegionTriangle(regionIdx_, [&](const float* a, const float* b, const float* c) {
                const irr::core::triangle3df t(irr::core::vector3df(a[0], a[1], a[2]),
                                               irr::core::vector3df(b[0], b[1], b[2]),
                                               irr::core::vector3df(c[0], c[1], c[2]));
                fn(t);
            });
        }
    }

private:
    const std::vector<irr::core::triangle3df>* owned_ = nullptr;
    const RegionTriangleSource* source_ = nullptr;
    size_t regionIdx_ = 0;
};

// The zone's set: a borrowed source (pooled zone) plus any owned regions.
class RegionWorldTriangles {
public:
    // Pooled zone: lookups for the source's regions read the source; no copy.
    // Drops any owned region the source also covers (before B909 the pooled
    // copy overwrote such a region, so the pools' triangles are the answer).
    void setSource(const RegionTriangleSource* source);
    // Legacy per-node path: one owned region (replaces any previous value).
    void setOwned(size_t regionIdx, std::vector<irr::core::triangle3df> triangles);
    // The region's triangles; tests false if it has none.
    RegionTrianglesView find(size_t regionIdx) const;
    bool empty() const { return owned_.empty() && sourceRegions_.empty(); }
    size_t size() const;   // regions with triangles
    void clear();
    // Heap bytes held for triangle storage (0 for a pooled zone).
    size_t ownedTriangleBytes() const;

private:
    std::map<size_t, std::vector<irr::core::triangle3df>> owned_;
    const RegionTriangleSource* source_ = nullptr;
    std::vector<size_t> sourceRegions_;   // ascending; 8 B per region
};

}  // namespace Graphics
}  // namespace EQT

#endif
