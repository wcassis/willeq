#ifndef EQT_GRAPHICS_RENDERER_ZONE_MERGE_H
#define EQT_GRAPHICS_RENDERER_ZONE_MERGE_H

// P10: production landing of the P09/P09b/P09c-validated cross-region
// same-material zone draw merge. Mirrors tools/hwcaps/zone_bench_merge.cpp's
// semantics EXACTLY (see refactor/P09_cross_region_merge.md, P09b_merge_fix.md,
// P09c_driver_fix_and_measure.md for the validated design + correctness
// lessons this class must reproduce):
//   - shared per-material world-space vertex storage, baked once at build time
//     (region translation-only world position added to each vertex, so a
//     merged draw uses an identity world transform);
//   - runs keyed by FULL SMaterial::operator== (not just materialKey -- the
//     P09b lesson: materialKey is a coarse (type,texture) grouping that
//     misses TextureLayer wrap/filter/matrix and other per-buffer state);
//   - one client-side-index glDrawElements per run, index order EXACTLY the
//     order the unmerged path would submit (deterministic, z-fight-parity,
//     kills the P09b reordering class by construction);
//   - the scratch SMeshBuffer is never registered with the driver's
//     HWBufferMap, so COpenGLES2Driver::drawMeshBuffer() takes its existing
//     client-side-vertex-array fallback path (same path already used for
//     entities/doors/UI) -- no raw GL calls, reuses production driver state
//     tracking/shader binding unchanged.
//
// Reduced-landing scope (P10 plan A4 -- see refactor/P10_merge_production_landing.md
// Review for the STOP-condition design memo): production's ConstrainedMeshCache
// does per-region LRU eviction with live streaming, which this class does NOT
// attempt to reconcile with (that would require a per-material shared-VBO
// sub-allocator with free-list/compaction PLUS new GPUUploadThread request
// types for safe append/free of live buffers -- a unit-sized project on its
// own). Instead: ZoneMergeBuffers::build() is only ever called by the caller
// (IrrlichtRenderer::drawZoneGeometrySorted()) once a zone's region set is
// FULLY resident with zero mesh-cache evictions so far. If an eviction occurs
// AFTER a successful build, the caller destroys the merge buffers and
// disables merging for the remainder of that zone visit -- the merge buffers
// are a derived, rebuildable snapshot, never a source of truth, so this is
// safe (never draws stale/evicted geometry) at the cost of losing the merge
// benefit entirely for zones whose region set doesn't fit the mesh cache
// budget. Zone objects/placeables are always excluded (out of scope, matches
// the P09 prototype -- build() only reads region mesh nodes).

#include <cstdint>
#include <map>
#include <vector>

namespace irr {
namespace video { class IVideoDriver; }
namespace scene { class IMeshSceneNode; }
}

namespace EQT {
namespace Graphics {

struct ZoneDrawEntry;  // defined in irrlicht_renderer.h

class ZoneMergeBuffers {
public:
    // Declared (not defaulted) here and defined in renderer_zone_merge.cpp --
    // buckets_/ranges_ hold incomplete nested types (MergedGeneration,
    // RegionBufferRange) at this point, so the implicitly-generated special
    // members (which need the complete types to construct/destroy map nodes)
    // must only be instantiated where those types are complete, i.e. in the
    // .cpp, not at every call site that merely constructs/destroys a
    // ZoneMergeBuffers (std::make_unique<ZoneMergeBuffers>() et al).
    ZoneMergeBuffers();
    ~ZoneMergeBuffers();

    ZoneMergeBuffers(const ZoneMergeBuffers&) = delete;
    ZoneMergeBuffers& operator=(const ZoneMergeBuffers&) = delete;

    // Builds the persistent per-material world-space vertex buffers from the
    // given (fully resident, per the caller's contract) region mesh node set.
    // Only EVT_STANDARD (S3DVertex) mesh buffers are merged; any other vertex
    // type is left out of the merge buckets and always falls back to the
    // normal per-entry unmerged draw at render time (defensive; matches
    // buildTexturedMesh/buildColoredMesh's actual output for zone geometry --
    // never observed to trigger, see zone_bench_merge.h precedent).
    void build(const std::map<size_t, irr::scene::IMeshSceneNode*>& regionMeshNodes);

    // Releases all merge buffers. Safe to call when not built.
    void destroy();

    bool isBuilt() const { return built_; }

    // Total bytes resident in the merge buffers (for /pmem-style reporting
    // and the P10 bless-package memory-overhead numbers). Zero if !isBuilt().
    size_t totalBytes() const { return totalBytes_; }

    // Draws an already-collected, already-(materialKey,distanceSq)-sorted
    // ZoneDrawEntry list using the merge-aware path: consecutive runs sharing
    // the same FULL SMaterial (operator==) are coalesced into one
    // glDrawElements each; entries whose buffer wasn't captured at build time
    // (non-EVT_STANDARD, or belongs to a region/object not in the merge set --
    // e.g. zone objects, which build() never sees) draw via the exact
    // unmerged single-buffer path. Returns the number of actual draw calls
    // issued (for the frame-timing draws column / P10 draw-count evidence).
    int renderMerged(irr::video::IVideoDriver* driver,
                      const std::vector<ZoneDrawEntry>& sortedEntries);

private:
    struct MergedGeneration;
    struct RegionBufferRange;

    std::map<uint32_t, std::vector<MergedGeneration*>> buckets_;
    std::map<const void*, RegionBufferRange> ranges_;
    bool built_ = false;
    size_t totalBytes_ = 0;
};

} // namespace Graphics
} // namespace EQT

#endif // EQT_GRAPHICS_RENDERER_ZONE_MERGE_H
