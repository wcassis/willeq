// K16 (board B006, refactor/K16_initgraphics_interface.md) --
// EQT::Graphics::IZoneLoadSurface.
//
// EverQuest::InitGraphics used to take an IrrlichtRenderer*. K16 re-measured
// the body of BridgeComponent::InitGraphics and the renderer pointer reaches
// exactly two calls: initLoadingScreen(const RendererConfig&) and
// initDeferredGraphics(). It is not stored, not passed on, not compared. So
// the parameter is now the two-method interface and IrrlichtRenderer
// implements it.
//
// The proof that the narrowing is COMPLETE is that this file compiles and
// passes at all: a stub with only those two methods drives the real
// InitGraphics body to a successful return. No renderer, no window, no GPU.
// Test-only stubs are otherwise no proof of anything -- what makes this one
// load-bearing is that the production body is the code under test.

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "client/graphics/zone_load_surface.h"
#include "client/eq.h"
#include "client/bridge/bridge_component.h"
#include "client/bridge/game_state_bridge.h"
#include "zone_stream_test_utils.h"

using namespace zonetest;

namespace {

// Records the call ORDER, not just the counts -- initDeferredGraphics() runs
// after the loading screen exists, and swapping them would still satisfy a
// pair of booleans.
class StubZoneLoadSurface : public EQT::Graphics::IZoneLoadSurface {
public:
    bool initLoadingScreen(const EQT::Graphics::RendererConfig& config) override {
        calls.push_back("initLoadingScreen");
        lastConfig = config;
        return loadingScreenResult;
    }

    void initDeferredGraphics() override {
        calls.push_back("initDeferredGraphics");
    }

    std::vector<std::string> calls;
    EQT::Graphics::RendererConfig lastConfig;
    bool loadingScreenResult = true;
};

class ZoneLoadSurfaceTest : public ::testing::Test {
protected:
    ZoneLoadSurfaceTest() : eq(MakeEq()) {
        eq.setBridge(&bridge);
        eq.SetEQClientPath("/nonexistent/eq/client/path");
        // InitGraphics calls InitializeAudio() at the end; this test is about
        // the renderer surface, and a real ALSA device is neither present nor
        // relevant here.
        eq.Bridge().SetAudioEnabled(false);
    }
    ~ZoneLoadSurfaceTest() override {
        eq.ShutdownGraphics();
        eq.setBridge(nullptr);
    }

    EverQuest eq;
    TestBridge bridge;
    StubZoneLoadSurface surface;
};

// The whole unit in one assertion: the production InitGraphics body reaches
// both methods, in this order, through the interface alone.
TEST_F(ZoneLoadSurfaceTest, InitGraphicsCallsBothSurfaceMethodsInOrder) {
    ASSERT_TRUE(eq.InitGraphics(800, 600, &surface, &bridge));

    ASSERT_EQ(surface.calls.size(), 2u);
    EXPECT_EQ(surface.calls[0], "initLoadingScreen");
    EXPECT_EQ(surface.calls[1], "initDeferredGraphics");
}

// The config the game side builds is what the surface receives -- the
// dimensions InitGraphics was called with, and the client path it was told.
TEST_F(ZoneLoadSurfaceTest, InitGraphicsPassesTheConfigItBuilt) {
    ASSERT_TRUE(eq.InitGraphics(1024, 768, &surface, &bridge));

    EXPECT_EQ(surface.lastConfig.width, 1024);
    EXPECT_EQ(surface.lastConfig.height, 768);
    EXPECT_EQ(surface.lastConfig.eqClientPath, "/nonexistent/eq/client/path");
}

// A failed loading screen is the surface's answer, and InitGraphics must
// honour it: no deferred init, and the whole call fails.
TEST_F(ZoneLoadSurfaceTest, LoadingScreenFailureStopsBeforeDeferredInit) {
    surface.loadingScreenResult = false;

    EXPECT_FALSE(eq.InitGraphics(800, 600, &surface, &bridge));

    ASSERT_EQ(surface.calls.size(), 1u);
    EXPECT_EQ(surface.calls[0], "initLoadingScreen");
}

}  // namespace
