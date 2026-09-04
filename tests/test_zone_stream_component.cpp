// L08 (Batch L, refactor/L08_zone_stream_component.md): ZoneStreamComponent,
// extracted from IrrlichtRenderer. Same bare, never-Init()'d IrrlichtRenderer
// idiom as test_sim_bridge_component / test_player_camera_component: the
// constructor is empty apart from the component wiring, and shutdown() (called
// from the destructor) null-guards every member it touches, so no Irrlicht
// device and no GL context are needed.
//
// L01 correction 6: both binding paths are pinned by MUTATION through public
// accessors, never by address equality on private members.
//
// The T111 blocking predicate itself is pinned by
// tests/test_progressive_load_rearm.cpp, which after L08 calls it as
// ZoneStreamComponent::isEntityBlockingProgressiveLoad(...). This file covers
// the wiring that test and the twenty retargeted production call sites depend
// on.

#include <gtest/gtest.h>

#include "client/graphics/irrlicht_renderer.h"
#include "client/graphics/simulation_worker.h"
#include "client/graphics/zone_stream/zone_stream_component.h"

using namespace EQT::Graphics;

namespace {

class ZoneStreamComponentTest : public ::testing::Test {
protected:
    // Declared BEFORE the renderer so it is destroyed AFTER it: the tests below
    // bind a pointer to this buffer onto an IrrlichtRenderer member, and the
    // T116 contract is that the pointee outlives every frame the member is read
    // on. Reverse-declaration-order destruction is how a fixture honours that.
    SimulationOutput persistent;
    IrrlichtRenderer renderer;
};

// --- The component exists and is reachable -------------------------------

TEST_F(ZoneStreamComponentTest, AccessorReachesAConstructedComponent) {
    // If the ctor body in renderer_init.cpp ever stops constructing it, every
    // call site retargeted by L08 -- processFrameProgressiveLoad() in the frame
    // loop, the eleven logAssetBuildTime() sites in the load steps, the two
    // guard-registration sites in init and the shutdown unregister -- is a null
    // dereference in production, not a test failure.
    ZoneStreamComponent& zs = renderer.ZoneStream();
    EXPECT_EQ(&zs, &renderer.ZoneStream());
}

// --- The owner_ path, and the L07/L08 component boundary ------------------
//
// regionPvsDepthMap_ is a T116 front-buffer pointer member that stayed on
// IrrlichtRenderer: SimBridgeComponent::bindFrameSimulationBuffers() is the
// only function permitted to write it, and ZoneStreamComponent::
// getRegionPvsDepth() is its main reader. That makes these three tests the
// executable statement that L07's and L08's components still share exactly one
// member, through owner_, with no copy in between.

TEST_F(ZoneStreamComponentTest, RegionPvsDepthIsUnknownWithNoBoundMap) {
    EXPECT_EQ(renderer.ZoneStream().getRegionPvsDepth(0), 255);
    EXPECT_EQ(renderer.ZoneStream().getRegionPvsDepth(12345), 255);
}

TEST_F(ZoneStreamComponentTest, RegionPvsDepthReadsTheMapSimBridgeBoundOnTheOwner) {
    // bindFrameSimulationBuffers() only binds the two PVS pointers when PVS
    // culling is on and the output actually carries region visibility -- the
    // same gate applySimulationOutput() applies to the values. Satisfy it
    // through the context, exactly as a real frame does.
    renderer.Context().usePvsCulling = true;
    persistent.regionVisible.assign(8, 1);
    persistent.regionPvsDepth[3] = 0;
    persistent.regionPvsDepth[7] = 2;
    renderer.SimBridge().bindFrameSimulationBuffers(&persistent);

    EXPECT_EQ(renderer.ZoneStream().getRegionPvsDepth(3), 0);
    EXPECT_EQ(renderer.ZoneStream().getRegionPvsDepth(7), 2);
    // A region the bound map does not mention is unknown, not depth 0 -- the
    // distinction queueEntityPrepRequests() and the T111 predicate both gate on.
    EXPECT_EQ(renderer.ZoneStream().getRegionPvsDepth(4), 255);
}

TEST_F(ZoneStreamComponentTest, RegionPvsDepthSeesLaterWritesThroughTheSamePointer) {
    // The member is a pointer into the worker's persistent output, not a copy.
    // If a future edit ever turned bindFrameSimulationBuffers() into a copy,
    // this is the test that notices.
    renderer.Context().usePvsCulling = true;
    persistent.regionVisible.assign(8, 1);
    persistent.regionPvsDepth[9] = 1;
    renderer.SimBridge().bindFrameSimulationBuffers(&persistent);
    EXPECT_EQ(renderer.ZoneStream().getRegionPvsDepth(9), 1);

    persistent.regionPvsDepth[9] = 4;
    EXPECT_EQ(renderer.ZoneStream().getRegionPvsDepth(9), 4);
}

TEST_F(ZoneStreamComponentTest, RegionPvsDepthStaysUnknownWhenTheBindGateIsClosed) {
    // usePvsCulling off: the bind runs but leaves the two PVS pointers alone,
    // so every region is still unknown. Proves the gate above, not the write,
    // is what the two tests before it measured.
    renderer.Context().usePvsCulling = false;
    persistent.regionVisible.assign(8, 1);
    persistent.regionPvsDepth[3] = 0;
    renderer.SimBridge().bindFrameSimulationBuffers(&persistent);

    EXPECT_EQ(renderer.ZoneStream().getRegionPvsDepth(3), 255);
}

// --- The PVS neighbourhood cache (owner_ members, component logic) --------

TEST_F(ZoneStreamComponentTest, PvsNeighborhoodIsEmptyUntilRebuilt) {
    EXPECT_FALSE(renderer.ZoneStream().isInPvsNeighborhood(0));
    EXPECT_FALSE(renderer.ZoneStream().isInPvsNeighborhood(SIZE_MAX));
}

TEST_F(ZoneStreamComponentTest, RebuildWithNoNeighbourMapLeavesTheNeighborhoodEmpty) {
    // regionNeighbors_ is built from the portal system at zone load; with no
    // zone there is nothing to expand, so the cache stays empty and
    // isInPvsNeighborhood() answers false for every region. That is the
    // documented "disabled" state in which onPvsRegionChanged() fires on every
    // region crossing rather than only on leaving a cluster -- the behaviour
    // the suppression cache exists to replace, and the one a bare renderer has.
    renderer.ZoneStream().rebuildPvsNeighborhood(4);
    EXPECT_FALSE(renderer.ZoneStream().isInPvsNeighborhood(4));
    EXPECT_FALSE(renderer.ZoneStream().isInPvsNeighborhood(5));

    renderer.ZoneStream().rebuildPvsNeighborhood(SIZE_MAX);
    EXPECT_FALSE(renderer.ZoneStream().isInPvsNeighborhood(SIZE_MAX));
}

TEST_F(ZoneStreamComponentTest, PvsRegionChangedIsANoOpWithNoWorkersOnTheContext) {
    // Both queues it reprioritises (entityPrepWorker, gpuUploadThread) are
    // RendererContext core members and are null on a bare renderer; each touch
    // is guarded. This is the call SimBridgeComponent makes as
    // owner_.ZoneStream().onPvsRegionChanged() on every PVS region crossing.
    renderer.ZoneStream().onPvsRegionChanged();
    SUCCEED();
}

// --- The ctx_ path, renderer -> component --------------------------------

TEST_F(ZoneStreamComponentTest, ProgressiveLoadGateIsTheContextFlag) {
    // processFrameProgressiveLoad()'s first line is the context flag, and it is
    // the same bool the renderer's own isProgressiveLoadingActive() reports --
    // one member, reached two ways. Writing it through Context() and running
    // the whole body is the round trip.
    ASSERT_FALSE(renderer.isProgressiveLoadingActive());
    renderer.ZoneStream().processFrameProgressiveLoad();  // gated off, returns at once
    EXPECT_FALSE(renderer.isProgressiveLoadingActive());

    renderer.Context().progressiveLoadingActive = true;
    EXPECT_TRUE(renderer.isProgressiveLoadingActive());

    // Gated on, the whole body runs: every subsystem it steps (entity renderer,
    // prep worker, door manager, texture cache, governor) is null on a bare
    // renderer and each is guarded. It ends in checkProgressiveLoadingComplete(),
    // which cannot complete because sequentialLoadComplete_ is false, so the
    // flag survives -- proving the body ran to the end without clearing it.
    renderer.ZoneStream().processFrameProgressiveLoad();
    EXPECT_TRUE(renderer.isProgressiveLoadingActive());

    renderer.Context().progressiveLoadingActive = false;
    renderer.ZoneStream().processFrameProgressiveLoad();
    EXPECT_FALSE(renderer.isProgressiveLoadingActive());
}

TEST_F(ZoneStreamComponentTest, CompletionCheckDoesNotCompleteBeforeTheSequentialLoadDoes) {
    renderer.Context().progressiveLoadingActive = true;
    renderer.ZoneStream().checkProgressiveLoadingComplete();
    // sequentialLoadComplete_ is false on a bare renderer, so the check must
    // bail before the "loading is done" branch that clears the flag. A
    // completion test that completed here would be the T109 shape again.
    EXPECT_TRUE(renderer.isProgressiveLoadingActive());
}

TEST_F(ZoneStreamComponentTest, IndexObjectMeshesIsANoOpWithNoZoneOnTheContext) {
    // ctx_.currentZone is null on a bare renderer; the body's first line
    // guards it. buildDeferredObject() then has nothing to build.
    renderer.ZoneStream().indexObjectMeshes();
    renderer.ZoneStream().buildDeferredObject(0);
    SUCCEED();
}

TEST_F(ZoneStreamComponentTest, EntityPrepQueueIsANoOpWithNoWorkerOnTheContext) {
    renderer.ZoneStream().queueEntityPrepRequests();
    renderer.Context().entityPrepReady = true;
    renderer.ZoneStream().queueEntityPrepRequests();  // still no worker, still guarded
    SUCCEED();
}

// --- The two single-family members ---------------------------------------

TEST_F(ZoneStreamComponentTest, GuardRegistrationIsIdempotentAndNeedsTheTextureCache) {
    // doorTextureGuard_ and boidsTextureGuard_ are the family's two moved
    // members. Each ensure*() helper needs BOTH the constrained texture cache
    // and its own subsystem before it constructs anything; a bare renderer has
    // neither, so both stay null and the unregister sweep has nothing to do.
    // Calling each twice is the idempotence the two lazy-construction sites in
    // renderer_init.cpp / overlays_hud_env.cpp rely on.
    renderer.ZoneStream().ensureDoorTextureGuard();
    renderer.ZoneStream().ensureDoorTextureGuard();
    renderer.ZoneStream().ensureBoidsTextureGuard();
    renderer.ZoneStream().ensureBoidsTextureGuard();
    renderer.ZoneStream().unregisterRemainingT124Guards();
    renderer.ZoneStream().unregisterRemainingT124Guards();
    SUCCEED();
}

// --- The T111 predicate is still a pure static ---------------------------

TEST_F(ZoneStreamComponentTest, BlockingPredicateNeedsNoComponentInstance) {
    // The predicate moved with the family but stayed static: it is callable on
    // the class, not the object, which is what keeps
    // tests/test_progressive_load_rearm.cpp a pure unit test.
    EXPECT_FALSE(ZoneStreamComponent::isEntityBlockingProgressiveLoad(
        /*meshBuilt=*/true, /*inSceneGraph=*/true, /*isPlayerEntity=*/false,
        /*pvsValid=*/true, /*regionPvsDepth=*/0, /*entityPrepMaxPvsDepth=*/255));
    EXPECT_TRUE(ZoneStreamComponent::isEntityBlockingProgressiveLoad(
        /*meshBuilt=*/false, /*inSceneGraph=*/true, /*isPlayerEntity=*/true,
        /*pvsValid=*/false, /*regionPvsDepth=*/255, /*entityPrepMaxPvsDepth=*/255));
}

}  // namespace
