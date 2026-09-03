// L01 (Batch L, refactor/L01_rdp_component.md) -- the pilot's test.
//
// Two things are pinned here:
//
//   1. IrrlichtRenderer::Rdp() reaches a real EQT::Graphics::RdpComponent,
//      and the five forwarders Application still calls agree with it.
//   2. RendererContext really BINDS: each member is a reference to the
//      IrrlichtRenderer member of the same name, so a write on one side is
//      visible on the other. Value equality alone would pass on two
//      independent default-constructed objects; every assertion below that
//      can be written as a mutation is.
//
// Genuinely headless, same idiom as test_irrlicht_camera_command.cpp (T66):
// a default-constructed, never-Init()'d IrrlichtRenderer. The constructor now
// carries an initialiser list (it binds ctx_) and, under WITH_RDP, builds the
// component in its body; nothing here touches a GL context.

#include <gtest/gtest.h>

#include "client/graphics/irrlicht_renderer.h"
#include "client/bridge/game_state_bridge.h"
#include "client/graphics/constrained_texture_cache.h"

using namespace EQT::Graphics;

namespace {

class TestBridge : public eqt::bridge::GameStateBridge {
public:
    void applyEvent(const eqt::state::GameEvent&) override {}
};

// ---------------------------------------------------------------------------
// RendererContext binding
// ---------------------------------------------------------------------------

TEST(RendererContextTest, ContextReturnsTheSameBundleEveryTime) {
    IrrlichtRenderer renderer;
    EXPECT_EQ(&renderer.Context(), &renderer.Context());
}

TEST(RendererContextTest, DriverAndDeviceMatchTheRenderersOwnAccessors) {
    IrrlichtRenderer renderer;
    // Never Init()'d, so both are null -- but they are read through the
    // context reference, which is what is under test.
    EXPECT_EQ(renderer.Context().driver, renderer.getDriver());
    EXPECT_EQ(renderer.Context().device, renderer.getDevice());
}

TEST(RendererContextTest, CurrentZoneNameIsTheSameOBJECTNotACopy) {
    IrrlichtRenderer renderer;
    // getCurrentZoneName() returns a const reference to currentZoneName_, so
    // this is address equality on the bound member itself.
    EXPECT_EQ(&renderer.Context().currentZoneName, &renderer.getCurrentZoneName());
}

TEST(RendererContextTest, WritingThroughTheContextIsVisibleOnTheRenderer) {
    IrrlichtRenderer renderer;
    ASSERT_FALSE(renderer.isDeferredAssetLoading());
    // config_ is private and has no public reference accessor; this writes it
    // through the context and reads it back through the renderer's own getter.
    renderer.Context().config.constrainedConfig.deferredAssetLoading = true;
    EXPECT_TRUE(renderer.isDeferredAssetLoading());

    renderer.Context().renderDistance = 1234.0f;
    EXPECT_FLOAT_EQ(renderer.getRenderDistance(), 1234.0f);

    renderer.Context().portalOcclusionEnabled = true;
    EXPECT_TRUE(renderer.isPortalOcclusionEnabled());
}

TEST(RendererContextTest, WritingThroughTheRendererIsVisibleOnTheContext) {
    IrrlichtRenderer renderer;
    TestBridge bridge;
    renderer.setBridge(&bridge);
    EXPECT_EQ(renderer.Context().bridge, &bridge);

    ConstrainedTextureCache* sentinel = reinterpret_cast<ConstrainedTextureCache*>(0x1);
    renderer.setConstrainedTextureCache(sentinel);
    EXPECT_EQ(renderer.Context().constrainedTextureCache, sentinel);
    renderer.setConstrainedTextureCache(nullptr);
    EXPECT_EQ(renderer.Context().constrainedTextureCache, nullptr);

    renderer.setRenderDistance(500.0f);
    EXPECT_FLOAT_EQ(renderer.Context().renderDistance, 500.0f);
}

// ---------------------------------------------------------------------------
// RdpComponent
// ---------------------------------------------------------------------------

#ifdef WITH_RDP

TEST(RdpComponentTest, RdpReturnsTheSameComponentEveryTime) {
    IrrlichtRenderer renderer;
    EXPECT_EQ(&renderer.Rdp(), &renderer.Rdp());
}

TEST(RdpComponentTest, NoServerBeforeInitRDP) {
    IrrlichtRenderer renderer;
    EXPECT_FALSE(renderer.Rdp().isRDPRunning());
    EXPECT_EQ(renderer.Rdp().getRDPServer(), nullptr);
}

TEST(RdpComponentTest, ForwardersAgreeWithTheComponent) {
    IrrlichtRenderer renderer;
    // The five Application-facing forwarders (the K11 rule): they must be
    // pass-throughs, not a second implementation.
    EXPECT_EQ(renderer.isRDPRunning(), renderer.Rdp().isRDPRunning());
    EXPECT_EQ(renderer.getRDPServer(), renderer.Rdp().getRDPServer());
    // startRDPServer() with no server initialised fails on both sides.
    EXPECT_FALSE(renderer.startRDPServer());
    EXPECT_FALSE(renderer.Rdp().startRDPServer());
    // stopRDPServer() on a never-initialised component is a no-op, not a crash.
    renderer.stopRDPServer();
    EXPECT_FALSE(renderer.isRDPRunning());
}

#else

TEST(RdpComponentTest, SkippedBecauseWithRdpIsOff) {
    // WITH_RDP is OFF in this configure, so RdpComponent is not compiled and
    // IrrlichtRenderer declares none of the five forwarders. Recorded, not
    // silently absent (refactor/L01_rdp_component.md, "Tests").
    SUCCEED();
}

#endif // WITH_RDP

}  // namespace
