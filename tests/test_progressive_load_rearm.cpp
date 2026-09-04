// T111 -- coverage for ZoneStreamComponent::isEntityBlockingProgressiveLoad(),
// the predicate extracted from checkProgressiveLoadingComplete() to fix the
// bug T109 found and shipped inert around: deferred entities outside the
// player's landing BSP region were invisible to the old
// currentPvsRegion_-equality check, so progressive loading declared itself
// complete (and permanently stopped) while 89/178 Befallen entities sat
// forever in Placeholder phase. See
// refactor/T111_progressive_loading_rearm.md's Review for the live
// reproduction/fix.
//
// This is a pure static predicate -- no scene graph, no Irrlicht device, no
// IrrlichtRenderer instance needed. It takes exactly the inputs
// checkProgressiveLoadingComplete() reads per-entity (meshBuilt,
// inSceneGraph, is-this-the-player-entity, whether the player's PVS region
// is known, this entity's region PVS depth, and the configured
// entityPrepMaxPvsDepth cap) and returns whether that entity should still
// count as blocking completion.
//
// The predicate is deliberately symmetric with queueEntityPrepRequests()'s
// own gate (same entityPrepMaxPvsDepth comparison) rather than the old
// currentPvsRegion_ scoping: an entity this predicate says blocks MUST be
// one the prep queue will actually pick up (or completion would wait
// forever -- the plan's explicit "a completion test that never completes
// would be worse" warning), and one it says doesn't block must not be
// silently dropped from the count either (T109's actual bug).

#include <gtest/gtest.h>

#include "client/graphics/irrlicht_renderer.h"

// L08 (Batch L): the predicate moved to ZoneStreamComponent with the rest of
// the zone streaming family. Receiver only -- every assertion below is
// unchanged, and it is still a pure static needing no renderer instance.
using EQT::Graphics::IrrlichtRenderer;
using EQT::Graphics::ZoneStreamComponent;

namespace {

constexpr bool kMeshBuilt = true;
constexpr bool kNotBuilt = false;
constexpr bool kInScene = true;
constexpr bool kNotInScene = false;
constexpr bool kIsPlayer = true;
constexpr bool kNotPlayer = false;
constexpr bool kPvsValid = true;
constexpr bool kPvsUnknown = false;

}  // namespace

// --- Built entities never block, regardless of anything else ---

TEST(ProgressiveLoadRearmTest, BuiltEntityNeverBlocks) {
    EXPECT_FALSE(ZoneStreamComponent::isEntityBlockingProgressiveLoad(
        kMeshBuilt, kInScene, kNotPlayer, kPvsValid, /*depth=*/0, /*maxDepth=*/255));
    // Even "obviously blocking" inputs otherwise (deep in the zone, no PVS,
    // is the player) -- meshBuilt short-circuits everything.
    EXPECT_FALSE(ZoneStreamComponent::isEntityBlockingProgressiveLoad(
        kMeshBuilt, kInScene, kIsPlayer, kPvsUnknown, /*depth=*/255, /*maxDepth=*/0));
}

// --- Entities not in the scene graph never block ---

TEST(ProgressiveLoadRearmTest, NotInSceneGraphNeverBlocks) {
    EXPECT_FALSE(ZoneStreamComponent::isEntityBlockingProgressiveLoad(
        kNotBuilt, kNotInScene, kNotPlayer, kPvsValid, /*depth=*/0, /*maxDepth=*/255));
}

// --- The player entity always blocks while unbuilt and in-scene, regardless
//     of PVS validity or depth (matches processFrameProgressiveLoad's own
//     "always prioritize the player entity" rule) ---

TEST(ProgressiveLoadRearmTest, UnbuiltPlayerEntityAlwaysBlocks) {
    EXPECT_TRUE(ZoneStreamComponent::isEntityBlockingProgressiveLoad(
        kNotBuilt, kInScene, kIsPlayer, kPvsValid, /*depth=*/0, /*maxDepth=*/255));
    EXPECT_TRUE(ZoneStreamComponent::isEntityBlockingProgressiveLoad(
        kNotBuilt, kInScene, kIsPlayer, kPvsUnknown, /*depth=*/255, /*maxDepth=*/0));
}

// --- THE SECOND T111 REGRESSION CASE (found live, mid-unit, via the actual
//     pickup proof session -- see the plan's Review): a non-player unbuilt
//     entity while the player's PVS region is still unknown MUST still
//     block, even though queueEntityPrepRequests() itself declines to queue
//     it yet (identical early-continue). If this returned false, completion
//     could be declared in the one-frame window between
//     sequentialLoadComplete_ and the SimulationWorker's first real PVS
//     swap -- confirmed live to happen literally one frame after Step 8,
//     permanently losing every deferred entity zone-wide before a single
//     one was ever queued. An earlier version of this predicate got this
//     backwards; this test pins the corrected direction. ---

TEST(ProgressiveLoadRearmTest, NonPlayerEntityStillBlocksWhenPvsUnknown) {
    EXPECT_TRUE(ZoneStreamComponent::isEntityBlockingProgressiveLoad(
        kNotBuilt, kInScene, kNotPlayer, kPvsUnknown, /*depth=*/0, /*maxDepth=*/255));
    // Depth is irrelevant while PVS is unknown -- must still block even at
    // depth 255 vs maxDepth 0 (the "obviously excluded once PVS resolves"
    // shape), because we can't yet tell.
    EXPECT_TRUE(ZoneStreamComponent::isEntityBlockingProgressiveLoad(
        kNotBuilt, kInScene, kNotPlayer, kPvsUnknown, /*depth=*/255, /*maxDepth=*/0));
}

// --- THE T109/T111 REGRESSION CASE: an unbuilt, in-scene, non-player entity
//     outside the player's exact current region (which the old
//     currentPvsRegion_-equality check would have excluded) still blocks, as
//     long as it's within the configured entityPrepMaxPvsDepth. This is the
//     case that was silently dropped before this unit -- proving it now
//     returns true is the direct fix for 82/178 Befallen entities getting
//     stuck forever. ---

TEST(ProgressiveLoadRearmTest, OutOfRegionEntityWithinPrepDepthStillBlocks) {
    // depth=3 (several BSP regions away from the player's landing region)
    // but within the shipped-default unbounded entityPrepMaxPvsDepth=255 --
    // must still block. This is exactly what the old code got wrong: it
    // keyed off region EQUALITY, not depth, so this entity was invisible to
    // the completion count no matter what entityPrepMaxPvsDepth said.
    EXPECT_TRUE(ZoneStreamComponent::isEntityBlockingProgressiveLoad(
        kNotBuilt, kInScene, kNotPlayer, kPvsValid, /*depth=*/3, /*maxDepth=*/255));
}

// --- An entity beyond the configured entityPrepMaxPvsDepth correctly does
//     NOT block -- queueEntityPrepRequests() will never queue it either, so
//     if this returned true, completion would wait forever (the plan's
//     explicit "worse than the bug" failure mode). Proves the fix doesn't
//     overcorrect into that trap. ---

TEST(ProgressiveLoadRearmTest, EntityBeyondPrepMaxDepthDoesNotBlock) {
    EXPECT_FALSE(ZoneStreamComponent::isEntityBlockingProgressiveLoad(
        kNotBuilt, kInScene, kNotPlayer, kPvsValid, /*depth=*/5, /*maxDepth=*/2));
}

// --- Depth exactly at the cap still blocks (queueEntityPrepRequests() uses
//     `depth > entityPrepMaxPvsDepth` to exclude, i.e. `<=` to include --
//     mirrored here as `<=`) ---

TEST(ProgressiveLoadRearmTest, EntityExactlyAtPrepMaxDepthBlocks) {
    EXPECT_TRUE(ZoneStreamComponent::isEntityBlockingProgressiveLoad(
        kNotBuilt, kInScene, kNotPlayer, kPvsValid, /*depth=*/2, /*maxDepth=*/2));
}

// --- One past the cap does not ---

TEST(ProgressiveLoadRearmTest, EntityOnePastPrepMaxDepthDoesNotBlock) {
    EXPECT_FALSE(ZoneStreamComponent::isEntityBlockingProgressiveLoad(
        kNotBuilt, kInScene, kNotPlayer, kPvsValid, /*depth=*/3, /*maxDepth=*/2));
}
