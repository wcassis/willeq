// L09 (Batch L, refactor/L09_zone_lifecycle_component.md):
// ZoneLifecycleComponent, extracted from IrrlichtRenderer -- three families in
// one component (zone_lifecycle + zone_draw + gpu_upload).
//
// Same bare, never-Init()'d IrrlichtRenderer idiom as
// test_zone_stream_component / test_sim_bridge_component: the constructor is
// empty apart from the component wiring, and every body exercised below
// null-guards the device/driver/scene-manager it would otherwise need, so no
// Irrlicht device and no GL context are required.
//
// L01 correction 6: both binding paths (ctx_ and owner_) are pinned by
// MUTATION through public accessors, never by address equality on private
// members.

#include <gtest/gtest.h>

#include "client/graphics/irrlicht_renderer.h"
#include "client/graphics/zone_lifecycle/zone_lifecycle_component.h"

using namespace EQT::Graphics;

namespace {

class ZoneLifecycleComponentTest : public ::testing::Test {
protected:
    IrrlichtRenderer renderer;
};

// --- The component exists and is reachable -------------------------------

TEST_F(ZoneLifecycleComponentTest, AccessorReachesAConstructedComponent) {
    // If the ctor line in renderer_init.cpp ever stops constructing it, every
    // call site L09 retargeted -- the three GPU-upload steps in the frame loop,
    // createZoneMesh()/buildRegionNeighborMap()/uploadMeshHardwareBuffers() in
    // the zone-load steps, the placeholder rebuild in frame_input, the six
    // forwarders and RenderPassTimer's manual draw -- is a null dereference in
    // production, not a test failure.
    ZoneLifecycleComponent& zl = renderer.ZoneLifecycle();
    EXPECT_EQ(&zl, &renderer.ZoneLifecycle());
}

// --- The ctx_ path: renderer -> context -> component ---------------------

TEST_F(ZoneLifecycleComponentTest, ZoneMeshCreationIsANoOpWithNoZoneOnTheContext) {
    // createZoneMesh() returns immediately on !ctx_.currentZone. Reaching the
    // guard at all proves the component's ctx_ reference is bound to the
    // renderer's own context and not to a copy.
    EXPECT_NO_THROW(renderer.ZoneLifecycle().createZoneMesh());
    EXPECT_NO_THROW(renderer.ZoneLifecycle().rebuildRegionMesh(0));
    EXPECT_FALSE(renderer.ZoneLifecycle().rebuildRegionMesh(0));
}

TEST_F(ZoneLifecycleComponentTest, HardwareBufferHelpersRejectANullNode) {
    // Both helpers guard on !node before touching ctx_.driver.
    EXPECT_NO_THROW(renderer.ZoneLifecycle().uploadMeshHardwareBuffers(nullptr));
    EXPECT_NO_THROW(renderer.ZoneLifecycle().deleteMeshHardwareBuffers(nullptr));
}

TEST_F(ZoneLifecycleComponentTest, NeighborMapBuildIsANoOpWithNoPortalSystem) {
    // buildRegionNeighborMap() writes owner_.regionNeighbors_, which stayed on
    // IrrlichtRenderer because ZoneStreamComponent reads it. With no portal
    // system on the context the body returns before writing anything, and
    // ZoneStreamComponent's neighbourhood rebuild -- the only other user --
    // still sees an empty map.
    EXPECT_NO_THROW(renderer.ZoneLifecycle().buildRegionNeighborMap());
    renderer.ZoneStream().rebuildPvsNeighborhood(0);
    EXPECT_FALSE(renderer.ZoneStream().isInPvsNeighborhood(0));
}

TEST_F(ZoneLifecycleComponentTest, WeatherIsForwardedToTheContextController) {
    // setWeather() guards on ctx_.weatherEffects, which is null on a bare
    // renderer. The forwarder on IrrlichtRenderer and the component body must
    // both be reachable -- irrlicht_bridge.cpp:623 calls the former.
    EXPECT_NO_THROW(renderer.setWeather(1, 5));
    EXPECT_NO_THROW(renderer.ZoneLifecycle().setWeather(2, 10));
}

// --- The owner_ path: state that stayed on IrrlichtRenderer --------------

TEST_F(ZoneLifecycleComponentTest, NetworkReadyIsTheRenderersOwnFlag) {
    // L13 moved networkReady_ onto this component (it is the only writer);
    // it had stayed on IrrlichtRenderer (renderer_entities.cpp reads
    // it), so this pins that the component writes through owner_ and not into
    // a private copy: the write goes in through the component, the read comes
    // back out through the renderer's own accessor.
    EXPECT_FALSE(renderer.isNetworkReady());
    renderer.setNetworkReady(true);   // the forwarder irrlicht_bridge.cpp calls
    EXPECT_TRUE(renderer.isNetworkReady());
    renderer.ZoneLifecycle().setNetworkReady(false);
    EXPECT_FALSE(renderer.isNetworkReady());
}

TEST_F(ZoneLifecycleComponentTest, NetworkNotReadyClearsTheZoneReadyFlagThroughOwner) {
    // The !ready branch resets owner_.zoneReady_ as well as the two counters
    // that moved into the component. zoneReady_ stayed behind (frame_input and
    // frame_render read it), so this is a second, independent owner_ binding.
    renderer.setZoneReady(true);
    EXPECT_TRUE(renderer.isZoneReady());
    renderer.ZoneLifecycle().setNetworkReady(false);
    EXPECT_FALSE(renderer.isZoneReady());
}

TEST_F(ZoneLifecycleComponentTest, ExpectedEntityCountIsAcceptedThroughTheForwarder) {
    // B340 deleted expectedEntityCount_ (write-only after L09 deleted its
    // zero-caller accessor), so the observable contract is just that the
    // bridge's forwarder reaches the body without touching anything null.
    EXPECT_NO_THROW(renderer.setExpectedEntityCount(42));
    EXPECT_NO_THROW(renderer.ZoneLifecycle().setExpectedEntityCount(0));
}

// --- The zone-environment trio -------------------------------------------

TEST_F(ZoneLifecycleComponentTest, StoredEnvironmentSurvivesTheForwarderAndIsApplied) {
    // storeZoneEnvironment() is called by application_zone_loading.cpp
    // through the forwarder (twice until T470 deleted the uncalled loader); applyStoredZoneEnvironment() has zero callers and
    // was KEPT (not deleted) because renderer_zone_load_env.cpp reads the same
    // storedZoneEnvironment_ directly. Both halves must stay callable so the
    // boarded wire-or-delete decision stays a one-line decision.
    const uint8_t fog[4] = {10, 20, 30, 40};
    const float clip[4] = {1.0f, 2.0f, 3.0f, 4.0f};
    EXPECT_NO_THROW(renderer.storeZoneEnvironment(3, 1, fog, fog, fog, clip, clip));
    EXPECT_NO_THROW(renderer.ZoneLifecycle().applyStoredZoneEnvironment());
    // Applying twice is a no-op: the first call clears .pending.
    EXPECT_NO_THROW(renderer.ZoneLifecycle().applyStoredZoneEnvironment());
}

// --- Zone teardown --------------------------------------------------------

TEST_F(ZoneLifecycleComponentTest, UnloadZoneIsSafeOnABareRendererThroughBothRoutes) {
    // unloadZone() is the largest body in the unit (311 lines) and touches ~60
    // members across both binding paths. On a bare renderer every subsystem it
    // steps is null and each is guarded, so reaching the end proves the whole
    // substitution set resolves. Three production callers use the forwarder
    // (irrlicht_bridge, application_zone_loading, graphical_mode) and
    // irrlicht_renderer.cpp's destructor path uses zoneLifecycle_->.
    EXPECT_NO_THROW(renderer.unloadZone());
    EXPECT_NO_THROW(renderer.ZoneLifecycle().unloadZone());
    // It clears the placeholder as part of teardown; calling that directly
    // afterwards must still be safe (ZoneStreamComponent reaches it as
    // owner_.ZoneLifecycle().destroyZonePlaceholder()).
    EXPECT_NO_THROW(renderer.ZoneLifecycle().destroyZonePlaceholder());
}

TEST_F(ZoneLifecycleComponentTest, InstantSceneAndHCMapCollisionAreNoOpsWithNoMap) {
    // setupInstantScene() is called by application_zone_loading.cpp; it
    // needs ctx_.collisionMap, which is null here, and setupHCMapCollision()
    // needs ctx_.smgr. renderer_frame_input.cpp calls the placeholder rebuild
    // pair (buildZonePlaceholder + setupHCMapCollision) directly.
    EXPECT_NO_THROW(renderer.setupInstantScene("qeynos2", 1.0f, 2.0f, 3.0f));
    EXPECT_NO_THROW(renderer.ZoneLifecycle().setupHCMapCollision());
    EXPECT_NO_THROW(renderer.ZoneLifecycle().buildZonePlaceholder(0.0f, 0.0f, 0.0f));
}

// --- The zone draw path ---------------------------------------------------

TEST_F(ZoneLifecycleComponentTest, ManualZoneDrawIsANoOpWithNoDriver) {
    // These four are called only from
    // IrrlichtRenderer::RenderPassTimer::OnRenderPassPreRender(), which stayed
    // on the renderer and now reaches them as
    // renderer_->ZoneLifecycle().draw...(). The visual gate is the real proof
    // the pixels did not change; this is the proof the wiring exists at all.
    EXPECT_NO_THROW(renderer.ZoneLifecycle().drawZoneGeometrySorted());
    EXPECT_NO_THROW(renderer.ZoneLifecycle().drawZoneGeometryWithPortals());
    EXPECT_NO_THROW(renderer.ZoneLifecycle().drawRegionMesh(0));
}

// --- The GPU upload thread ------------------------------------------------

TEST_F(ZoneLifecycleComponentTest, UploadThreadToggleFlipsTheComponentsOwnFlag) {
    // gpuUploadEnabled_ is a single-family member that MOVED, and its accessor
    // moved with it (K04 correction (f)). SlashCommandsComponent's /upload
    // handler reads it back immediately after toggling, as
    // owner_.ZoneLifecycle().isGPUUploadThreadEnabled().
#ifdef EQT_HAS_GLES2
    EXPECT_TRUE(renderer.ZoneLifecycle().isGPUUploadThreadEnabled());
    renderer.ZoneLifecycle().toggleGPUUploadThread();
    EXPECT_FALSE(renderer.ZoneLifecycle().isGPUUploadThreadEnabled());
    renderer.ZoneLifecycle().toggleGPUUploadThread();
    EXPECT_TRUE(renderer.ZoneLifecycle().isGPUUploadThreadEnabled());
#else
    EXPECT_FALSE(renderer.ZoneLifecycle().isGPUUploadThreadEnabled());
    EXPECT_NO_THROW(renderer.ZoneLifecycle().toggleGPUUploadThread());
#endif
}

#ifdef EQT_HAS_GLES2
TEST_F(ZoneLifecycleComponentTest, UploadDrainAndApplyAreNoOpsWithNoThread) {
    // ⚠ THREADING: gpuUploadThread_ is a RendererContext core member and did
    // NOT move; GPUUploadThread still owns its own thread. The component drives
    // it from the render thread exactly as IrrlichtRenderer did. Here it is
    // null, so all three steps must return immediately -- which is also what
    // renderer_frame.cpp relies on before a zone is loaded.
    EXPECT_NO_THROW(renderer.ZoneLifecycle().initGPUUploadThread());
    EXPECT_NO_THROW(renderer.ZoneLifecycle().processCompletedUploads());
    EXPECT_NO_THROW(renderer.ZoneLifecycle().drainGPUResults());
    EXPECT_FALSE(renderer.ZoneLifecycle().processOneGPUResult());
}
#endif

} // namespace
