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
#include <cstdint>
#include <functional>
#include <map>
#include <vector>

namespace EQT {
namespace Graphics {

// One contiguous run of a region's triangles: `indexCount` indices into
// `vertices`, three per triangle (a trailing partial triangle is ignored,
// exactly as forEachRegionTriangle() ignores it).
struct TriangleRun {
    const irr::video::S3DVertex* vertices = nullptr;
    const uint16_t* indices = nullptr;
    size_t indexCount = 0;
};

// Anything that can enumerate one region's world-space triangles, in a fixed
// order (Irrlicht Y-up).
class RegionTriangleSource {
public:
    using TriangleFn = std::function<void(const float*, const float*, const float*)>;
    virtual ~RegionTriangleSource() = default;
    // Convenience form, one std::function call per triangle. NOT for a ray
    // loop: the Pi A/B measured it (refactor/B909/README.md, "Orange Pi").
    virtual void forEachRegionTriangle(size_t regionIdx, const TriangleFn& fn) const = 0;
    // The hot-path form: copies up to `maxOut` of the region's runs, starting
    // at run `first`, into `out`; returns how many it wrote (0 = no more).
    // The triangles of the runs, in order, are exactly forEachRegionTriangle's.
    virtual size_t regionRuns(size_t regionIdx, size_t first, TriangleRun* out, size_t maxOut) const = 0;
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

    // fn(const vector3df& a, const vector3df& b, const vector3df& c) for every
    // triangle, in storage order. Inlined into the caller: for a pooled region
    // the loop reads the pool's vertices through its indices directly (one
    // virtual call per 16 runs), with no per-triangle call or copy.
    template <class Fn>
    void forEachTriangle(Fn&& fn) const {
        if (owned_) {
            for (const auto& t : *owned_) fn(t.pointA, t.pointB, t.pointC);
        } else if (source_) {
            constexpr size_t kBatch = 16;
            TriangleRun runs[kBatch];
            size_t first = 0;
            for (;;) {
                const size_t n = source_->regionRuns(regionIdx_, first, runs, kBatch);
                for (size_t k = 0; k < n; ++k) {
                    const TriangleRun& r = runs[k];
                    for (size_t i = 0; i + 2 < r.indexCount; i += 3) {
                        fn(r.vertices[r.indices[i]].Pos, r.vertices[r.indices[i + 1]].Pos,
                           r.vertices[r.indices[i + 2]].Pos);
                    }
                }
                if (n < kBatch) break;
                first += n;
            }
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
