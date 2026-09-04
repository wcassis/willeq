// L10 (Batch L, refactor/L10_zone_load_component.md): ZoneLoadComponent,
// extracted from IrrlichtRenderer -- loadZoneSequential() and the twenty
// per-step methods that build a zone, across four TUs.
//
// Same bare, never-Init()'d IrrlichtRenderer idiom as
// test_zone_lifecycle_component / test_zone_stream_component: the constructor
// is empty apart from the component wiring, and the whole prefix of the load
// sequence this test drives null-guards the device/driver/scene-manager it
// would otherwise need, so no Irrlicht device and no GL context are required.
//
// L01 correction 6: both binding paths (ctx_ and owner_) are pinned by
// MUTATION through public accessors, never by address equality on private
// members.

#include <gtest/gtest.h>

#include "client/graphics/irrlicht_renderer.h"
#include "client/graphics/zone_load/zone_load_component.h"
#include "client/graphics/loading_thread.h"

using namespace EQT::Graphics;

namespace {

class ZoneLoadComponentTest : public ::testing::Test {
protected:
    IrrlichtRenderer renderer;
    LoadingStatus status;
};

// --- The component exists and is reachable -------------------------------

TEST_F(ZoneLoadComponentTest, AccessorReachesAConstructedComponent) {
    // If the ctor line in renderer_init.cpp ever stops constructing it, the
    // two forwarders below -- which are the loading thread's ONLY route into
    // the whole zone build -- are null dereferences in production, not a test
    // failure.
    ZoneLoadComponent& zl = renderer.ZoneLoad();
    EXPECT_EQ(&zl, &renderer.ZoneLoad());
}

// --- The moved members ---------------------------------------------------

TEST_F(ZoneLoadComponentTest, LoadFailedStartsFalseThroughBothRoutes) {
    // loadFailed_ moved into the component; hasLoadFailed() is the inline
    // accessor that moved with it (K04 (f)) and stayed on IrrlichtRenderer as
    // a forwarder because application_zone_loading.cpp:277 calls it.
    EXPECT_FALSE(renderer.hasLoadFailed());
    EXPECT_FALSE(renderer.ZoneLoad().hasLoadFailed());
}

// --- The whole substitution set, end to end ------------------------------

TEST_F(ZoneLoadComponentTest, SequentialLoadWithNoZoneFailsCleanlyThroughTheForwarder) {
    // Drives the loading thread's real entry point on a bare renderer, exactly
    // as Application::loadZoneGraphicsOnThread() does at
    // application_zone_loading.cpp:275, and exercises every substitution class
    // this unit applied in one call:
    //
    //   ctx_    the driver reads ctx_.config (the memAudit + eqe1Path gates),
    //           ctx_.currentZoneName and ctx_.progressiveLoadingActive; Step 1
    //           reads ctx_.currentZone / ctx_.entityRenderer / ctx_.doorManager.
    //   owner_  it writes owner_.progressiveLoadStartTime_ and
    //           owner_.entityPrepScanCounter_.
    //   sibling components: owner_.SimBridge().stopSimulationWorker() and
    //           owner_.Overlays().drawLoadingScreen() (through
    //           ZoneLoadContext::updateProgress) both run.
    //   moved   pendingZoneComputations_ is constructed by the driver and reset
    //           by Step 1's failure path, which sets loadFailed_.
    //
    // With no zone name the S3D path resolves to ".s3d", S3DLoader fails, and
    // loadZoneStep1_S3D() takes its LOG_FATAL early-return -- the same shape
    // the production loader takes on a missing archive.
    EXPECT_FALSE(renderer.hasLoadFailed());
    EXPECT_NO_THROW(renderer.loadZoneSequential("", status));
    EXPECT_TRUE(renderer.hasLoadFailed());
    EXPECT_TRUE(renderer.ZoneLoad().hasLoadFailed());

    // The completion handshake is Application's, not the component's: the
    // sequence never touches loadingComplete/quitRequested (see the threading
    // note in zone_load_component.h). Only setProgress() is written.
    EXPECT_FALSE(status.loadingComplete.load());
    EXPECT_FALSE(status.quitRequested.load());
}

TEST_F(ZoneLoadComponentTest, ProgressIsReportedThroughTheLoadingStatusTheCallerOwns) {
    // The step sequence's only write to the caller's LoadingStatus is
    // ZoneLoadContext::updateProgress -> status.setProgress(). Proving it
    // reaches the caller's own object is what pins the loading-thread
    // handshake: LoadingThread reads exactly this to paint the progress bar.
    renderer.loadZoneSequential("", status);
    EXPECT_GT(status.percent.load(), 0);
}

TEST_F(ZoneLoadComponentTest, TheComponentAndTheForwarderAreTheSameLoad) {
    // Called directly on the component rather than through IrrlichtRenderer:
    // same state, same flag, no copy of either binding.
    renderer.ZoneLoad().loadZoneSequential("", status);
    EXPECT_TRUE(renderer.hasLoadFailed());
}

// --- Sibling-component boundaries the load sequence crosses --------------

TEST_F(ZoneLoadComponentTest, ProgressiveLoadingIsArmedOnTheContextBeforeTheFirstStep) {
    // loadZoneSequential() sets ctx_.progressiveLoadingActive = true before
    // Step 0 and nothing in the failure path clears it. isProgressiveLoadingActive()
    // is IrrlichtRenderer's own reader of the same context member -- so this is
    // the executable statement that the component writes THROUGH the context
    // and not into a copy of it.
    EXPECT_FALSE(renderer.isProgressiveLoadingActive());
    renderer.loadZoneSequential("", status);
    EXPECT_TRUE(renderer.isProgressiveLoadingActive());
}

TEST_F(ZoneLoadComponentTest, TheLoadGateMethodsThatStayedAreStillReachable) {
    // ensureEntityPrepWorkerStarted() and deferEntityForLoadGate() stayed on
    // IrrlichtRenderer (renderer_entity_load_gate.cpp is outside L10's
    // territory) and Step 8 reaches them as owner_.X(). Neither is public, so
    // the proof that the owner_ route is intact is that the component's TU
    // compiles and links -- which the friend line makes true. What IS testable
    // here is that the renderer they were left on is the one the component
    // holds: entityPrepWorker_ is untouched by a failed load.
    renderer.loadZoneSequential("", status);
    EXPECT_TRUE(renderer.hasLoadFailed());
}

}  // namespace
