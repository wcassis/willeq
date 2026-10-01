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

#include "client/graphics/region_world_triangles.h"
#include "client/graphics/constrained_texture_cache.h"  // TextureEvictionListener

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace irr {
namespace video { class IVideoDriver; class ITexture; struct SMaterial; }
namespace scene { class IMesh; class ISceneNode; class IMeshSceneNode; }
}

namespace EQT { namespace Graphics { struct EntityVisual; struct DoorVisual; } }

namespace EQT {
namespace Graphics {

struct ZoneDrawEntry;  // defined in irrlicht_renderer.h

// T504 (load-step audit, zone draw rework): one region buffer's worth of
// geometry inside the pooled merge buffers. In POOLED mode this 16-byte record
// replaces, per region buffer, a scene node + SMesh + SMeshBuffer (312 B) + a
// material copy (176 B) + a VBO/EBO pair -- MEASURED iceclad: 16,049 buffers of
// 10.6 vertices each, zone_mesh 54.9 MiB for 6.6 MiB of geometry.
struct ZoneSlice {
    uint32_t regionIdx;
    uint16_t materialId;   // index into ZoneMergeBuffers' material table
    uint16_t generation;   // which <=65535-vertex buffer of that material
    int32_t indexOffset;   // into that generation's template index array
    int32_t indexCount;
};

// B909: the pools are also the zone's collision triangles in pooled mode
// (RegionTriangleSource, region_world_triangles.h) -- held once, not copied.
class ZoneMergeBuffers : public RegionTriangleSource {
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

    // ---- T504 POOLED mode -------------------------------------------------
    // The region meshes are appended straight into the per-material pools at
    // zone load and then dropped: no per-region scene node, mesh or VBO ever
    // exists. The material table is the state carrier the region buffers used
    // to be (animated textures, shader swaps and the eviction guard act on it).
    void beginPooled();
    // Appends every EVT_STANDARD buffer of `mesh`, baking (px,py,pz) into the
    // vertices. Returns the number of slices added (0: nothing drawable).
    int appendRegion(size_t regionIdx, irr::scene::IMesh* mesh, float px, float py, float pz);
    void finishPooled();
    bool isPooled() const { return pooled_; }
    size_t pooledRegionCount() const { return regionSpans_.size(); }
    // The contiguous slices of one region (nullptr/0 when it has none).
    const ZoneSlice* slicesForRegion(size_t regionIdx, size_t& countOut) const;
    size_t materialCount() const;
    irr::video::SMaterial& material(size_t id);
    // Eviction guard / animated textures: replace a texture in every pool
    // material. Returns the number of materials changed.
    int swapTexture(irr::video::ITexture* from, irr::video::ITexture* to);
    // World-space triangles of one region, for collision. fn(const float* a,
    // const float* b, const float* c), each a pointer to x,y,z.
    void forEachRegionTriangle(size_t regionIdx,
                               const std::function<void(const float*, const float*, const float*)>& fn) const override;
    // B909: pooled regions with at least one whole triangle, ascending.
    std::vector<size_t> regionsWithTriangles() const override;
    // B909: the hot-path form (region_world_triangles.h) -- the region's
    // slices as (pool vertices, template indices, count), no per-triangle call.
    size_t regionRuns(size_t regionIdx, size_t first, TriangleRun* out, size_t maxOut) const override;

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

    // T504 pooled mode
    struct RegionSpan { uint32_t regionIdx; uint32_t first; uint32_t count; };
    struct PoolMaterials;                       // vector<SMaterial>, out of line
    bool pooled_ = false;
    PoolMaterials* poolMaterials_ = nullptr;
    std::vector<std::vector<MergedGeneration*>> poolBuckets_;  // [materialId][generation]
    std::vector<ZoneSlice> slices_;             // grouped by region, regions ascending
    std::vector<RegionSpan> regionSpans_;       // sorted by regionIdx
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
    // see renderer_init.cpp, "Pre-create placeholder texture before
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
    // T504 (zone draw rework): in POOLED mode the zone's materials live in
    // ZoneMergeBuffers' table, not on region mesh buffers. The hook swaps an
    // evicted texture there too; like the rest of this guard it must not call
    // back into the cache.
    using PoolSwapFn = std::function<int(irr::video::ITexture* from, irr::video::ITexture* to)>;
    void setPoolSwap(PoolSwapFn fn) { poolSwap_ = std::move(fn); }

private:
    const std::map<size_t, irr::scene::IMeshSceneNode*>& regionMeshNodes_;
    irr::video::ITexture* placeholder_;
    PoolSwapFn poolSwap_;
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
    // "placeholder cursor" concept).
    // T460 (board B520): what this comment used to call a safe fallback --
    // "not drawing the cursor for the rest of this zone visit" -- was in fact
    // FOREVER: nothing reloaded the cursor, and zone unload's clear() evicted
    // it at the first zone change (Will: no cursor on the Pi). The cursor is
    // now protectTexture()'d by AcquireSoftwareCursorTexture()
    // (software_cursor_texture.h), so this guard should never fire for it;
    // it stays registered as the use-after-free net if that ever regresses.
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

// T123: object-node sibling of RegionMeshTextureGuard/EntityMeshTextureGuard
// above. `IrrlichtRenderer::applySimulationOutput()` (renderer_sim_bridge.cpp)
// calls `objectNodes_[i]->remove()` on any zone-object node that becomes
// invisible (PVS/simulation-driven), and `addChild()` again when it becomes
// visible -- a continuous, live detach/reattach cycle for as long as the zone
// is loaded, NOT a one-time thing settled at zone load or unload.
// `unloadZone()` itself tears every objectNodes_ entry down cleanly
// (`remove()` + `drop()`, confirmed -- see refactor/T123_texture_holder_audit.md),
// so this is not an unload-time gap; it is the WINDOW during normal play
// while an object is hidden and therefore unreachable from
// `smgr_->getRootSceneNode()`. A texture eviction/zone-unload `clear()` that
// runs while an object is in that detached state leaves its material's
// texture reference un-nulled (both its mesh-buffer materials AND its own
// node-level override `Materials` array, per `ISceneNode::getMaterial(i)` --
// T123 confirmed via a live core dump that the actual holder for one
// observed use-after-free write was exactly a node's own override-materials
// array, `irr::core::array<irr::video::SMaterial>`, identified via its
// embedded `irrAllocator<SMaterial>` vtable). When the object becomes
// visible again and is re-attached, `applyMaterialRenderState()` renders it
// with the stale pointer still in place -> `cacheTexParams()`
// use-after-free. This guard closes it exactly like the two guards above:
// scan `objectNodes_` directly (renderer bookkeeping, not scene-graph
// reachability) on every eviction notification, covering mesh-buffer
// materials AND the node's own override materials (mirroring
// ConstrainedTextureCache::clearTextureReferences()'s own two-part scan, but
// without requiring scene-graph reachability).
class ObjectNodeTextureGuard : public TextureEvictionListener {
public:
    // objectNodes: IrrlichtRenderer::objectNodes_ -- renderer-owned, cleared
    // (remove()+drop() each entry) not destroyed/recreated between zone
    // visits (renderer_zone_lifecycle.cpp), safe to hold a reference for the
    // guard's own lifetime (also renderer-owned, constructed once).
    //
    // placeholder: resolved ONCE by the caller before construction, same
    // load-bearing non-reentrancy constraint as RegionMeshTextureGuard's
    // constructor doc above (never re-derive it inside onTextureEvicted()).
    ObjectNodeTextureGuard(const std::vector<irr::scene::IMeshSceneNode*>& objectNodes,
                           irr::video::ITexture* placeholder);

    void onTextureEvicted(const std::string& name, irr::video::ITexture* texture) override;

private:
    const std::vector<irr::scene::IMeshSceneNode*>& objectNodes_;
    irr::video::ITexture* placeholder_;
};

// ---------------------------------------------------------------------------
// T124: shared eviction-swap primitives.
//
// Four guards above (RegionMeshTextureGuard, EntityMeshTextureGuard,
// ObjectNodeTextureGuard, and the trivial SingleTexturePointerGuard) each
// re-implemented some form of "walk a material's texture units, swap the
// evicted pointer for a replacement." T124 found six more holders needing
// the identical operation. Per the T124 plan's explicit instruction, the
// loop is extracted ONCE here rather than copied six more times -- every
// holder below (existing and new) is a thin wrapper composing these.
//
// All four free functions are pure bookkeeping: no allocation, no locking,
// no calls back into ConstrainedTextureCache -- safe to call from inside
// onTextureEvicted() under the cache's mutex_, same as the hand-written
// loops they replace.

// The one true loop: swaps every texture-unit slot in `mat` currently
// holding `from` to `to`. Returns the number of slots swapped (0 or 1 in
// practice -- a texture normally occupies at most one unit per material,
// but MATERIAL_MAX_TEXTURES units are all checked defensively).
int swapTextureInMaterial(irr::video::SMaterial& mat, irr::video::ITexture* from, irr::video::ITexture* to);

// Walks every buffer of `mesh`, swapping each buffer's own material via
// swapTextureInMaterial(). Mirrors RegionMeshTextureGuard's existing scan
// and the mesh-buffer half of ObjectNodeTextureGuard's. No-op if mesh is
// null.
int swapTextureInMesh(irr::scene::IMesh* mesh, irr::video::ITexture* from, irr::video::ITexture* to);

// Walks `node`'s own override-materials array (getMaterial(i)/
// getMaterialCount(), NOT the mesh buffers) via swapTextureInMaterial().
// This is what render() actually binds for ReadOnlyMaterials=false nodes
// (Irrlicht's default) -- mirrors the node-materials half of
// ObjectNodeTextureGuard's scan and EntityMeshTextureGuard's materials_
// scan. No-op if node is null.
int swapTextureInNodeMaterials(irr::scene::ISceneNode* node, irr::video::ITexture* from, irr::video::ITexture* to);

// Convenience composing the two functions above for an ordinary
// IMeshSceneNode: mesh-buffer materials AND the node's own override
// materials. Exactly ObjectNodeTextureGuard's two-part scan and doors'
// required coverage (see DoorTextureGuard below) -- both hold their actual
// mesh on an IMeshSceneNode child, not the pivot.
int swapTextureInMeshSceneNode(irr::scene::IMeshSceneNode* node, irr::video::ITexture* from, irr::video::ITexture* to);

// T124: doors_ sibling of ObjectNodeTextureGuard. DoorManager's
// per-door pivotNode is continuously detached/reattached from the scene
// graph, PVS-driven, the identical shape T123 found for objectNodes_ (see
// the door_manager.* family: setDoorState() in door_manager.cpp, update()
// in door_manager_animation.cpp and rebuildSingleDoor() in
// door_manager_rebuild.cpp -- all of
// which node->remove()/addChild() the pivotNode on visibility changes --
// NOT a one-time thing settled at zone load, and NOT an unload-time gap:
// clearDoors() unconditionally re-attaches-then-removes-then-drops every
// entry regardless of state before doors_.clear()). The door's actual mesh
// lives on DoorVisual::sceneNode (an IMeshSceneNode child of pivotNode) --
// when pivotNode is detached, sceneNode goes with it and is equally
// unreachable from smgr_->getRootSceneNode(). Scans DoorManager's own
// doors_ bookkeeping directly, exactly like ObjectNodeTextureGuard scans
// objectNodes_, bypassing scene-graph reachability entirely.
class DoorTextureGuard : public TextureEvictionListener {
public:
    // doors: DoorManager::doors_ -- renderer-owned (via doorManager_),
    // persists for the renderer's lifetime (clearDoors() empties the map,
    // never destroys/recreates DoorManager itself -- doorManager_ is
    // constructed exactly once, by ZoneLifecycleComponent::ensureDoorManager()
    // (T468), which then registers this guard), safe to hold a reference for
    // the guard's own lifetime (also renderer-owned, constructed alongside
    // doorManager_).
    //
    // placeholder: resolved ONCE by the caller before construction, same
    // load-bearing non-reentrancy constraint as the other guards above
    // (never re-derive it inside onTextureEvicted()).
    DoorTextureGuard(const std::map<uint8_t, DoorVisual>& doors,
                      irr::video::ITexture* placeholder);

    void onTextureEvicted(const std::string& name, irr::video::ITexture* texture) override;

private:
    const std::map<uint8_t, DoorVisual>& doors_;
    irr::video::ITexture* placeholder_;
};

// T124: forwarding guard for holders that are singleton renderer
// subsystems (SpellVisualFX, RainOverlay, SnowOverlay, BoidsManager) rather
// than node/map collections. Each of those classes owns its cached
// ITexture* members privately and exposes a single swapEvictedTexture()-
// shaped method that internally uses the same swapTextureInMaterial/
// swapTexturePointer primitives -- this guard is pure forwarding, it
// contains no swap logic of its own (avoiding a fifth near-identical
// TextureEvictionListener subclass for what is, underneath, the exact same
// "call one function with (from, to)" shape four times over).
class ForwardingTextureGuard : public TextureEvictionListener {
public:
    using SwapFn = std::function<void(irr::video::ITexture* from, irr::video::ITexture* to)>;

    // fn: bound to the owning subsystem's swapEvictedTexture()-shaped
    // method (e.g. std::bind or a lambda capturing the subsystem pointer).
    // placeholder: resolved ONCE by the caller before construction, same
    // load-bearing constraint as every guard above.
    ForwardingTextureGuard(SwapFn fn, irr::video::ITexture* placeholder)
        : fn_(std::move(fn)), placeholder_(placeholder) {}

    void onTextureEvicted(const std::string& /*name*/, irr::video::ITexture* texture) override {
        if (texture && fn_) fn_(texture, placeholder_);
    }

private:
    SwapFn fn_;
    irr::video::ITexture* placeholder_;
};

// T124: shutdown()-time unregister-before-destroy helper, shared to avoid
// the 3-line `if (cache && guard) cache->removeEvictionListener(guard.get());
// guard.reset();` pattern being copy-pasted once per guard (irrlicht_renderer.cpp
// was at the check_file_sizes.sh budget -- this also directly served that).
template <typename GuardPtr>
void unregisterGuard(ConstrainedTextureCache* cache, GuardPtr& guard) {
    if (cache && guard) cache->removeEvictionListener(guard.get());
    guard.reset();
}

} // namespace Graphics
} // namespace EQT

#endif // EQT_GRAPHICS_RENDERER_ZONE_MERGE_H
