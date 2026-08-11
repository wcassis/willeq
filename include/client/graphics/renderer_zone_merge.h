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
// Review for the STOP-condition design memo): this design memo originally
// wrote against production's ConstrainedMeshCache, which was meant to do
// per-region LRU eviction with live streaming that this class does NOT
// attempt to reconcile with (that would require a per-material shared-VBO
// sub-allocator with free-list/compaction PLUS new GPUUploadThread request
// types for safe append/free of live buffers -- a unit-sized project on its
// own). T67 (2026-08-11) removed ConstrainedMeshCache entirely: it was never
// constructed in any shipped build, so this reconciliation problem never
// actually existed in production -- see src/client/graphics/CLAUDE.md for
// the reference pointer. The design/gating logic below is unchanged and
// still applies verbatim to the one live eviction source
// (ConstrainedTextureCache): ZoneMergeBuffers::build() is only ever called
// by the caller (IrrlichtRenderer::drawZoneGeometrySorted()) once a zone's
// region set is FULLY resident with zero evictions so far. If an eviction
// occurs AFTER a successful build, the caller destroys the merge buffers and
// disables merging for the remainder of that zone visit -- the merge buffers
// are a derived, rebuildable snapshot, never a source of truth, so this is
// safe (never draws stale/evicted geometry) at the cost of losing the merge
// benefit entirely for zones whose region set doesn't fit budget. Zone
// objects/placeables are always excluded (out of scope, matches the P09
// prototype -- build() only reads region mesh nodes).

#include "client/graphics/constrained_texture_cache.h"  // TextureEvictionListener

#include <cstdint>
#include <functional>
#include <map>
#include <vector>

namespace irr {
namespace video { class IVideoDriver; class ITexture; }
namespace scene { class IMeshSceneNode; }
}

namespace EQT { namespace Graphics { struct EntityVisual; } }

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
    //
    // P10b: isTextureLive, if provided, is called with each buffer's
    // material's unit-0 texture BEFORE it is ever dereferenced (before
    // textureGroupId() touches it). A buffer whose texture fails this check
    // is skipped (counted, WARNed once in aggregate) rather than merged --
    // defense in depth against a dangling ITexture* reaching this point at
    // all (root cause: P10b_merge_crash_fix.md -- ConstrainedTextureCache
    // eviction couldn't null out materials on region mesh nodes detached
    // from the scene graph for front-to-back manual draw; fixed at the
    // source by RegionMeshTextureGuard below, but this call is intentionally
    // defensive regardless, per the plan's explicit requirement, in case any
    // other path ever leaves a stale texture pointer in a region buffer's
    // material). Omitting the callback (default) preserves prior behavior
    // (every non-null texture is trusted), for callers outside production
    // (e.g. tools/tests) that have no cache to validate against.
    void build(const std::map<size_t, irr::scene::IMeshSceneNode*>& regionMeshNodes,
               const std::function<bool(irr::video::ITexture*)>& isTextureLive = {});

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

// P10b root-cause fix: ConstrainedTextureCache::clearTextureReferences()
// (called on every eviction, BEFORE driver_->removeTexture()) only reaches
// materials on scene nodes still attached to the scene graph -- it scans
// from smgr_->getRootSceneNode() down. Production's front-to-back manual
// zone draw (loadZoneStep4_Regions(), manualZoneDrawEnabled_) explicitly
// DETACHES every region mesh node from the graph right after building it
// (node->remove()) so it can be drawn manually instead of via
// CSceneManager::drawAll(). In any zone whose full texture set (zone +
// entity/equipment textures loaded afterward) exceeds the constrained
// texture budget, ConstrainedTextureCache evicts LRU zone-region textures
// to make room -- but since their owning nodes are unreachable from the
// scene graph root, clearTextureReferences() never finds and nulls those
// materials' texture pointers. The region buffer's SMaterial is left
// holding a non-null but freed (driver_->removeTexture()'d) ITexture*,
// which is what ZoneMergeBuffers::build()/textureGroupId() dereferences on
// the first frame -- the qeynos2 SIGSEGV (P10b_merge_crash_fix.md). This
// is a general production bug (the unmerged draw path is equally exposed,
// it just usually escapes because it only touches the CURRENTLY VISIBLE
// subset of regions per frame, whereas build() unconditionally scans EVERY
// region every time it runs) -- fixing it here benefits both paths, not
// just the merge.
//
// RegionMeshTextureGuard closes the gap: it registers as a
// TextureEvictionListener and, on eviction, scans the renderer's
// regionMeshNodes_ map directly (which includes BOTH in-graph and
// detached/out-of-graph nodes -- it is the renderer's own bookkeeping of
// "every region mesh node that exists this zone visit", unlike the scene
// graph) and swaps any material texture slot matching the evicted pointer
// to the cache's placeholder texture, mirroring exactly what
// clearTextureReferences() already does for in-graph nodes.
class RegionMeshTextureGuard : public TextureEvictionListener {
public:
    // regionMeshNodes: renderer-owned, persists for the renderer's lifetime
    // (cleared, never destroyed/recreated, between zone visits -- see
    // renderer_zone_lifecycle.cpp) -- safe to hold a reference for the
    // guard's own lifetime (also renderer-owned, constructed once in
    // initLoadingScreen()).
    //
    // placeholder: the cache's placeholder ITexture*, resolved ONCE by the
    // caller BEFORE constructing this guard (via a plain, non-reentrant
    // ConstrainedTextureCache::getPlaceholderTexture() call at init time --
    // see irrlicht_renderer.cpp, "Pre-create placeholder texture before
    // render loop starts", which already runs before any eviction can
    // occur) and cached here as a plain pointer. THIS IS LOAD-BEARING, not
    // a style choice: onTextureEvicted() runs on the SAME thread and INSIDE
    // the SAME call stack as ConstrainedTextureCache::evictTexture(), which
    // is only ever reached via evictUntilAvailable() from uploadTexture()/
    // uploadAllPending()/uploadCompressedETC1() etc. -- all of which hold
    // ConstrainedTextureCache::mutex_ (a plain, non-recursive std::mutex)
    // for their entire body. An earlier version of this class took a
    // std::function<ITexture*()> callback that called BACK into
    // getPlaceholderTexture() (which itself locks mutex_) from inside
    // onTextureEvicted() -- a same-thread re-lock of a non-recursive mutex,
    // i.e. a guaranteed self-deadlock. Caught via gdb on a hung
    // loading-thread backtrace during this unit's own qeynos2 verification
    // (Thread stuck in getPlaceholderTexture()'s lock_guard, called from
    // RegionMeshTextureGuard::onTextureEvicted(), called from
    // evictTexture(), called from evictUntilAvailable(), called from
    // uploadTexture() -- which still held mutex_ the whole way down). The
    // placeholder texture itself is never entered into cache_ (created via
    // driver_->addTexture() directly into placeholderTexture_, never
    // eligible for its own eviction), so caching the pointer once is safe
    // for the guard's/renderer's entire lifetime.
    RegionMeshTextureGuard(const std::map<size_t, irr::scene::IMeshSceneNode*>& regionMeshNodes,
                            irr::video::ITexture* placeholder);

    void onTextureEvicted(const std::string& name, irr::video::ITexture* texture) override;

private:
    const std::map<size_t, irr::scene::IMeshSceneNode*>& regionMeshNodes_;
    irr::video::ITexture* placeholder_;
};

// P10b: found while verifying the qeynos2 automatic-deferred-loading combo
// (post RegionMeshTextureGuard fix) -- a SEPARATE, pre-existing, unrelated
// dangling-texture-pointer bug in the same eviction-safety class as the
// merge crash, just in a different subsystem: IrrlichtRenderer's
// `softwareCursorTexture_` (irrlicht_renderer.h) is a raw ITexture* set
// once from ConstrainedTextureCache and read directly by
// renderer_frame.cpp's draw2DImage() cursor-draw call -- it was NEVER
// registered as a TextureEvictionListener (unlike every other
// texture-caching subsystem in this codebase: AnimatedTextureManager,
// SkyRenderer, DetailManager, StormCloudLayer, AnimatedTreeManager all
// null their own cached pointers on eviction; this one didn't). Since
// "softwareCursor" is created once at startup and never touched again, it
// is always the LRU-oldest entry -- the very FIRST texture evicted under
// any budget pressure (confirmed: first eviction in every qeynos2 run's
// log). Once evicted, draw2DImage(softwareCursorTexture_, ...) dereferences
// a freed ITexture -- SIGSEGV in COpenGLES2Driver::draw2DImage(), on the
// first frame, independent of mergeZoneDraws entirely (reproduced with
// mergeZoneDraws's own crash already fixed). This is not a mergeZoneDraws
// bug and was not introduced by this unit's changes -- it is a pre-existing
// gap in the same "texture cache eviction leaves dangling raw pointers
// outside its own bookkeeping" class this unit's investigation already
// established as qeynos2's structural hazard (P10b_merge_crash_fix.md).
// Fixed with the exact same precedented pattern the other 5 subsystems
// already use (see TextureEvictionListener's own doc comment) because it
// blocked this unit's own step-4 verification matrix (qeynos2 automatic
// mode could not otherwise render a single crash-free frame) -- not a
// mergeZoneDraws-specific fix, but necessary, minimal, and isolated,
// exactly the P10/anim-video-capture precedent for pre-existing blockers
// found while building required verification evidence.
class SingleTexturePointerGuard : public TextureEvictionListener {
public:
    // target: reference to the caller-owned raw pointer to null out on
    // eviction (e.g. IrrlichtRenderer::softwareCursorTexture_). The caller
    // must already null-check this pointer before every use (confirmed for
    // softwareCursorTexture_ in renderer_frame.cpp) -- this guard only
    // prevents the pointer from going dangling, it does not draw a
    // placeholder in its place (unlike RegionMeshTextureGuard, there is no
    // "placeholder cursor" concept; simply not drawing the cursor for the
    // rest of this zone visit is the correct, safe fallback).
    explicit SingleTexturePointerGuard(irr::video::ITexture*& target) : target_(target) {}

    void onTextureEvicted(const std::string& /*name*/, irr::video::ITexture* texture) override {
        if (texture && target_ == texture) target_ = nullptr;
    }

private:
    irr::video::ITexture*& target_;
};

// P17: entity-mesh sibling of RegionMeshTextureGuard above. P10b protected
// region mesh nodes only ("region meshes already have the equivalent
// protection... entity meshes have no such guard" -- P15's finding,
// refactor/deferred.md); this closes that gap. EQAnimatedMeshSceneNode
// (animated_mesh_scene_node.h) holds TWO independent places a dangling
// evicted ITexture* can hide, neither reachable from the scene graph scan
// ConstrainedTextureCache::clearTextureReferences() does for ordinary
// nodes (this class exists for the SAME reason RegionMeshTextureGuard
// does -- direct member access beats scene-graph reachability -- not
// because entity nodes are detached like region nodes are):
//   1. materials_ (the node's own per-submesh SMaterial vector, exposed via
//      the IAnimatedMeshSceneNode::getMaterial(i)/getMaterialCount()
//      interface) -- these are the materials actually bound at render time
//      (render() draws instanceMesh_'s buffers using THESE materials, not
//      the buffers' own, per EQAnimatedMeshSceneNode::render()'s existing
//      pattern of copying eqMesh_'s base materials in once and letting
//      callers mutate them for texture-variant swaps).
//   2. instanceMesh_ (the node's own per-instance SMesh copy, returned by
//      getInstanceMesh()) -- each buffer's own SMaterial can also carry a
//      texture reference (e.g. equipment pieces attached as separate
//      buffers/materials), independent of the materials_ vector above.
// Confirmed hazard (refactor/deferred.md, "found by P15, NOT chased"):
// RaceModelLoader::createAnimatedNodeWithEquipment reads
// currentTex->getName() off a cached ITexture* without checking eviction --
// an intermittent SIGSEGV/SIGABRT reproduced even under a 256MB
// (no-eviction) budget, i.e. a general dangling-reference bug. This guard
// makes the eviction side of that hazard impossible (evicted textures are
// swapped to the placeholder the instant they're freed, mirroring exactly
// what RegionMeshTextureGuard/clearTextureReferences() already do for
// region/ordinary nodes) -- it does not by itself audit every entity-mesh
// build-time read site for missing null/liveness checks (out of this
// unit's scope; the eviction-time dangling-pointer window is what's fixed
// here).
class EntityMeshTextureGuard : public TextureEvictionListener {
public:
    // entities: EntityRenderer::entities_ (via getEntities()) -- renderer-
    // owned, persists for the renderer's lifetime (cleared via
    // clearEntities(), never destroyed/recreated, on every zone unload --
    // see renderer_zone_lifecycle.cpp), safe to hold a reference for the
    // guard's own lifetime (also renderer-owned, constructed once
    // alongside EntityRenderer itself).
    //
    // placeholder: see RegionMeshTextureGuard's constructor doc -- same
    // load-bearing constraint (resolved ONCE, safely, before any eviction
    // can occur; NEVER re-derive it from inside onTextureEvicted(), which
    // would self-deadlock on ConstrainedTextureCache::mutex_ -- see
    // TextureEvictionListener's own doc comment in
    // constrained_texture_cache.h).
    EntityMeshTextureGuard(const std::map<uint16_t, EntityVisual>& entities,
                            irr::video::ITexture* placeholder);

    void onTextureEvicted(const std::string& name, irr::video::ITexture* texture) override;

private:
    const std::map<uint16_t, EntityVisual>& entities_;
    irr::video::ITexture* placeholder_;
};

} // namespace Graphics
} // namespace EQT

#endif // EQT_GRAPHICS_RENDERER_ZONE_MERGE_H
