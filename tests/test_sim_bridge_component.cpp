// L07 (Batch L, refactor/L07_sim_bridge_component.md): SimBridgeComponent,
// extracted from IrrlichtRenderer. Same bare, never-Init()'d IrrlichtRenderer
// idiom as test_player_camera_component / test_diagnostics_component: the
// constructor is empty apart from the component wiring, and shutdown() (called
// from the destructor) null-guards every member it touches, so no Irrlicht
// device and no GL context are needed.
//
// L01 correction 6: both binding paths are pinned by MUTATION through public
// accessors, never by address equality on private members. Every assertion is
// a round trip -- write on one side of a binding, read it back on the other.
//
// The T116 lifetime split itself is pinned by tests/test_simulation_output_
// lifetime.cpp, which after L07 reaches the same two production bodies through
// renderer.SimBridge(). This file covers the wiring that test now depends on.

#include <gtest/gtest.h>

#include "client/graphics/irrlicht_renderer.h"
#include "client/graphics/sim/sim_bridge_component.h"
#include "client/graphics/simulation_worker.h"

using namespace EQT::Graphics;

namespace {

class SimBridgeComponentTest : public ::testing::Test {
protected:
    IrrlichtRenderer renderer;
};

// --- The component exists and is reachable -------------------------------

TEST_F(SimBridgeComponentTest, AccessorReachesAConstructedComponent) {
    // If the ctor body in renderer_init.cpp ever stops constructing it, every
    // call site retargeted by L07 is a null dereference in production, not a
    // test failure.
    SimBridgeComponent& sb = renderer.SimBridge();
    EXPECT_EQ(&sb, &renderer.SimBridge());
}

// --- The ctx_ path, renderer -> component --------------------------------
//
// buildSimulationZoneData() is the snapshot the worker culls and lights
// against; every field asserted below is copied straight off RendererContext,
// so writing through Context() and reading the built snapshot back is one
// round trip across the binding. On a bare renderer every container the
// function walks (objectNodes, objectLights, regionMeshNodes, the zone) is
// empty or null and each is guarded, so the whole body runs.

TEST_F(SimBridgeComponentTest, BuildZoneDataReadsTheContextTheRendererWroteThrough) {
    renderer.Context().usePvsCulling = true;
    renderer.Context().indoorRegions = {3, 7};
    renderer.Context().config.constrainedConfig.portalOcclusion = true;
    renderer.Context().config.constrainedConfig.enableStencilBuffer = true;

    SimulationZoneData zoneData = renderer.SimBridge().buildSimulationZoneData();

    EXPECT_TRUE(zoneData.usePvsCulling);
    EXPECT_TRUE(zoneData.enablePortalVis);
    EXPECT_TRUE(zoneData.enableStencilBuffer);
    EXPECT_EQ(zoneData.indoorRegions, (std::unordered_set<size_t>{3, 7}));
    EXPECT_TRUE(zoneData.regionBounds.empty());  // no zone loaded
}

TEST_F(SimBridgeComponentTest, BuildZoneDataSeesContextChangesOnEveryCall) {
    renderer.Context().usePvsCulling = false;
    EXPECT_FALSE(renderer.SimBridge().buildSimulationZoneData().usePvsCulling);

    renderer.Context().usePvsCulling = true;
    EXPECT_TRUE(renderer.SimBridge().buildSimulationZoneData().usePvsCulling);
}

// --- The ctx_ path, component -> renderer --------------------------------
//
// applySimulationOutput()'s per-region portal-occlusion toggle writes
// ctx_.portalOcclusionEnabled from the worker's cameraInIndoorRegion, behind a
// four-condition gate. isPortalOcclusionEnabled() reads the renderer's own
// member back. The gate is exercised in both directions, so a component that
// silently stopped writing through ctx_ would fail here.

TEST_F(SimBridgeComponentTest, PortalOcclusionToggleWritesTheRenderersMemberThroughTheContext) {
    renderer.Context().usePvsCulling = true;
    renderer.Context().portalOcclusionEligible = true;
    renderer.Context().indoorRegions = {0};
    renderer.Context().config.constrainedConfig.portalOcclusion = true;
    renderer.Context().config.constrainedConfig.enableStencilBuffer = true;
    ASSERT_FALSE(renderer.isPortalOcclusionEnabled());

    SimulationOutput output;
    output.regionVisible = {1};
    output.currentPvsRegion = 0;
    output.cameraInIndoorRegion = true;
    renderer.SimBridge().applySimulationOutput(output);
    EXPECT_TRUE(renderer.isPortalOcclusionEnabled());

    output.cameraInIndoorRegion = false;
    renderer.SimBridge().applySimulationOutput(output);
    EXPECT_FALSE(renderer.isPortalOcclusionEnabled());
}

TEST_F(SimBridgeComponentTest, PortalOcclusionToggleStaysPutWhenTheGateIsClosed) {
    // portalOcclusionEligible off -- the same output must change nothing.
    renderer.Context().usePvsCulling = true;
    renderer.Context().portalOcclusionEligible = false;
    renderer.Context().indoorRegions = {0};
    renderer.Context().config.constrainedConfig.portalOcclusion = true;
    renderer.Context().config.constrainedConfig.enableStencilBuffer = true;

    SimulationOutput output;
    output.regionVisible = {1};
    output.cameraInIndoorRegion = true;
    renderer.SimBridge().applySimulationOutput(output);
    EXPECT_FALSE(renderer.isPortalOcclusionEnabled());
}

// --- The owner_ transition-handle path ------------------------------------
//
// simulationWorker_ stayed on IrrlichtRenderer -- L07 changed NO thread
// ownership -- so every worker touch in this component goes through owner_.
// On a bare renderer the worker is null, which is exactly the branch that
// proves the handle is read: the component reports "not created" and both
// lifetime calls are safe no-ops.

TEST_F(SimBridgeComponentTest, DebugInfoReadsTheOwnersWorkerHandle) {
    std::vector<std::string> lines = renderer.SimBridge().getSimWorkerDebugInfo();
    ASSERT_EQ(lines.size(), 1u);
    EXPECT_EQ(lines[0], "SimulationWorker: not created");
}

TEST_F(SimBridgeComponentTest, WorkerLifetimeCallsAreNoOpsWithNoWorkerOnTheOwner) {
    renderer.SimBridge().startSimulationWorkerEarly();
    renderer.SimBridge().registerSimulationWorkerVertexAnimData();
    renderer.SimBridge().stopSimulationWorker();
    // Still nothing created: none of the three may construct a worker, and the
    // renderer is still the only owner of one if it existed.
    EXPECT_EQ(renderer.SimBridge().getSimWorkerDebugInfo()[0],
              "SimulationWorker: not created");
}

TEST_F(SimBridgeComponentTest, PostSimulationInputIsANoOpWithNoWorkerOnTheOwner) {
    // The 340-line input marshaller early-outs on owner_.simulationWorker_
    // before touching anything else -- the first line of the body.
    renderer.SimBridge().postSimulationInput(0.016f);
    SUCCEED();
}

// --- applySimulationResults(): the runtime path's own early-out -----------

TEST_F(SimBridgeComponentTest, ApplyResultsIsANoOpWithNoWorkerOnTheOwner) {
    renderer.SimBridge().applySimulationResults();
    SUCCEED();
}

}  // namespace
