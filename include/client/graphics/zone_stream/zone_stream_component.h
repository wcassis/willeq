#ifndef EQT_GRAPHICS_ZONE_STREAM_ZONE_STREAM_COMPONENT_H
#define EQT_GRAPHICS_ZONE_STREAM_ZONE_STREAM_COMPONENT_H

// Batch L (refactor/L_renderer_component_extraction.md), unit L08 -- the zone
// streaming family, lifted out of class IrrlichtRenderer: the deferred-object
// index and per-object build, the per-frame progressive-load state machine,
// the PVS-depth/neighbourhood bookkeeping, the entity-prep request queue, the
// load-progress/timing reporting, and the T124 texture-guard registration
// helpers.
//
// Every body in src/client/graphics/zone_stream/renderer_zone_stream.cpp and
// the three IrrlichtRenderer bodies at the end of
// src/client/graphics/zone_stream/renderer_zone_merge.cpp are the bodies that
// used to live in src/client/graphics/renderer_zone_stream.cpp and
// src/client/graphics/renderer_zone_merge.cpp, moved verbatim. Only the
// directory moved; both basenames are unchanged.
//
// The only edits inside a body are the mechanism's substitutions: a core
// member becomes ctx_.member, a non-core IrrlichtRenderer member becomes
// owner_.member_, and a method that stayed behind becomes owner_.Method()
// (there are exactly two: destroyZonePlaceholder() and isRegionPvsVisible(),
// both zone_lifecycle/zone_draw territory).
//
// ⚠ renderer_zone_merge.h DID NOT MOVE. It stays at
// include/client/graphics/renderer_zone_merge.h and declares NOTHING that
// belongs to this component: ZoneMergeBuffers, the five TextureEvictionListener
// guards, the four T124 swapTextureIn*() free functions and the unregisterGuard()
// template are all namespace-scope EQT::Graphics names with no IrrlichtRenderer
// membership at all. Twelve TUs and the three T124/T126 guard tests
// (test_object_node_texture_guard.cpp, test_t126_equip_texture_guard.cpp,
// test_t124_texture_holder_guards.cpp) include it by that path and twenty-odd
// comments cite it by name; moving it would have been fifteen include edits and
// twenty stale citations bought for nothing. The TU that defines those classes
// moved with the family; its header did not.
//
// ⚠ THREADING. No thread ownership changed. The two worker handles this family
// drives -- entityPrepWorker_ (EntityPrepWorker's own thread) and
// gpuUploadThread_ (GPUUploadThread's) -- are RendererContext core members and
// stay exactly where they were; queueEntityPrepRequests(),
// onPvsRegionChanged() and processFrameProgressiveLoad() call them from the
// render thread precisely as they did before L08, and the two lambdas they
// hand across (updateDepths()/reprioritize() priority callbacks) are unchanged
// captures. The plan's hard STOP was "if the WORKER thread calls a method this
// unit moves, report the shape": it does not. The only mentions of
// queueEntityPrepRequests()/processFrameProgressiveLoad() outside the renderer
// are prose -- simulation_worker.cpp:509 and constrained_renderer_config.h:193,
// 194, 209 are comments describing the streaming path, not calls (K04
// correction (g) again).
//
// isEntityBlockingProgressiveLoad() is a pure static predicate, kept public and
// static here for exactly the reason T111 extracted it: it is unit-testable
// without a device or a scene graph. tests/test_progressive_load_rearm.cpp
// calls it directly; after L08 it does so as
// ZoneStreamComponent::isEntityBlockingProgressiveLoad(...), a receiver change
// only -- every assertion is untouched.
//
// The component needs friendship on axis 1 of K13 correction (ii): it reaches
// 23 private IrrlichtRenderer members through owner_ and two private
// IrrlichtRenderer methods. It names no IrrlichtRenderer static and no
// IrrlichtRenderer nested type, so axis 2 is not live.

#include "client/graphics/renderer_context.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

namespace EQT {
namespace Graphics {

class IrrlichtRenderer;
// Full definitions in include/client/graphics/renderer_zone_merge.h, which
// this header deliberately does NOT include: irrlicht_renderer.h includes this
// component header, and pulling the merge header in there would widen every
// renderer consumer's transitive include set for two unique_ptr members. The
// destructor is therefore declared here and defined in
// src/client/graphics/zone_stream/renderer_zone_merge.cpp, where both guard
// types are complete -- the same pattern, for the same reason,
// ZoneMergeBuffers itself documents for buckets_/ranges_.
class DoorTextureGuard;
class ForwardingTextureGuard;

class ZoneStreamComponent {
public:
	// Both declared, not defined, here: the two unique_ptr guard members hold
	// incomplete types at this point, and the constructor's own unwind path
	// needs them complete just as the destructor does.
	ZoneStreamComponent(RendererContext& ctx, IrrlichtRenderer& owner);
	~ZoneStreamComponent();

	ZoneStreamComponent(const ZoneStreamComponent&) = delete;
	ZoneStreamComponent& operator=(const ZoneStreamComponent&) = delete;

	// --- Deferred/progressive asset loading ---
	// Index objects without building meshes (deferred mode)
	void indexObjectMeshes();
	// Build a single deferred object mesh by index
	void buildDeferredObject(size_t idx);
	// Progressive per-frame asset building (priority-ordered, GREEN-only single-step)
	void processFrameProgressiveLoad();
	// Check if all deferred assets are built
	void checkProgressiveLoadingComplete();

	// T111: pure predicate for whether one entity blocks progressive-load
	// completion. Extracted out of checkProgressiveLoadingComplete() so it's
	// unit-testable without a scene graph/device, and so it can be kept
	// deliberately symmetric with queueEntityPrepRequests()'s own gate
	// (entityPrepMaxPvsDepth) rather than the currentPvsRegion_-equality
	// check T109 shipped, which is why deferred entities outside the
	// player's landing region were never picked up (see
	// refactor/T111_progressive_loading_rearm.md's Review). An entity this
	// predicate says blocks MUST be one queueEntityPrepRequests() will
	// actually queue, or completion would wait forever; one it excludes
	// must not silently vanish from the count either, or the T109 bug
	// recurs.
	static bool isEntityBlockingProgressiveLoad(bool meshBuilt, bool inSceneGraph,
	                                            bool isPlayerEntity, bool pvsValid,
	                                            uint8_t regionPvsDepth,
	                                            uint8_t entityPrepMaxPvsDepth);
	// Queue background prep requests for unbuilt entities
	void queueEntityPrepRequests();
	// Log build time and warn if over budget
	void logAssetBuildTime(const char* type, size_t id, std::chrono::steady_clock::time_point start);
	// Send loading progress message to chat window
	void sendLoadProgress(const std::string& msg);

	// --- PVS depth and neighbourhood bookkeeping ---
	// Depth of a region in the front-buffer PVS map; 255 when unknown.
	uint8_t getRegionPvsDepth(size_t regionIdx) const;
	// Re-derive the K-hop neighbourhood around anchorRegion. Reads
	// owner_.regionNeighbors_ and writes owner_.pvsNeighborhood_/Anchor_,
	// both of which stay on IrrlichtRenderer (renderer_zone_lifecycle.cpp
	// clears them on unload, so §Mechanism 3 keeps them there).
	void rebuildPvsNeighborhood(size_t anchorRegion);
	bool isInPvsNeighborhood(size_t regionIdx) const;
	// Re-prioritise the entity-prep and GPU-upload queues after the player
	// leaves the cached neighbourhood. Called from SimBridgeComponent.
	void onPvsRegionChanged();

	// --- T124 texture-guard registration ---
	// Idempotent guard-registration helpers, defined in
	// zone_stream/renderer_zone_merge.cpp (not irrlicht_renderer.cpp, which
	// was at the check_file_sizes.sh budget when they were written).
	// ensureBoidsTextureGuard() is safe to call from either of boidsManager_'s
	// two lazy-construction sites. See DoorTextureGuard's and
	// BoidsManager::swapEvictedTexture()'s doc comments for the gaps these
	// close.
	void ensureDoorTextureGuard();
	void ensureBoidsTextureGuard();
	// shutdown()-time unregister for objectNodeTextureGuard_ (T123) and the
	// four T124 guards. Two of those four live here; the other two stay on
	// IrrlichtRenderer and are reached through owner_.
	void unregisterRemainingT124Guards();


	// ------------------------------------------------------------------
	// L13 (refactor/L13_pairwise_ownership.md): members that moved here from
	// class IrrlichtRenderer because THIS component is their owner under the
	// pairwise rule (Batch K Mechanism 3 applied to a two-component member: it
	// moves to the component that WRITES it, or on a tie to the one with more
	// uses). Every other reader reaches them through ctx_.ZoneStream().<accessor>();
	// the renderer's own init/teardown reaches them through ZoneStream().
	// Each accessor below is the SMALLEST the other side needs: a const getter
	// where nothing outside this component writes, a mutable reference where
	// something does.
	// ------------------------------------------------------------------
	std::unique_ptr<ObjectNodeTextureGuard>& objectNodeTextureGuard() { return objectNodeTextureGuard_; }
	std::unique_ptr<ObjectNodeTextureGuard> const& objectNodeTextureGuard() const { return objectNodeTextureGuard_; }
	size_t& pvsNeighborhoodAnchor() { return pvsNeighborhoodAnchor_; }
	size_t const& pvsNeighborhoodAnchor() const { return pvsNeighborhoodAnchor_; }
	std::unordered_set<size_t>& pvsNeighborhood() { return pvsNeighborhood_; }
	std::unordered_set<size_t> const& pvsNeighborhood() const { return pvsNeighborhood_; }
	std::unique_ptr<ForwardingTextureGuard>& spellVisualFXTextureGuard() { return spellVisualFXTextureGuard_; }
	std::unique_ptr<ForwardingTextureGuard> const& spellVisualFXTextureGuard() const { return spellVisualFXTextureGuard_; }

private:
	RendererContext& ctx_;
	IrrlichtRenderer& owner_;

	// The family's two single-family members. Both are T124 eviction
	// listeners constructed lazily by the ensure*() helpers above and torn
	// down by unregisterRemainingT124Guards(); nothing outside this family
	// ever named either one. The other three guards
	// (regionMeshTextureGuard_, entityMeshTextureGuard_,
	// objectNodeTextureGuard_, spellVisualFXTextureGuard_) are constructed in
	// renderer_init.cpp / renderer_entities.cpp and stay on IrrlichtRenderer.
	std::unique_ptr<DoorTextureGuard> doorTextureGuard_;
	std::unique_ptr<ForwardingTextureGuard> boidsTextureGuard_;

	// L13: owned state, see the accessors above.
	std::unique_ptr<ObjectNodeTextureGuard> objectNodeTextureGuard_;
	size_t pvsNeighborhoodAnchor_ = SIZE_MAX;
	std::unordered_set<size_t> pvsNeighborhood_;  // regions within K portal hops of anchor
	std::unique_ptr<ForwardingTextureGuard> spellVisualFXTextureGuard_;
};

} // namespace Graphics
} // namespace EQT

#endif // EQT_GRAPHICS_ZONE_STREAM_ZONE_STREAM_COMPONENT_H
