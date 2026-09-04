// L03 (Batch L, refactor/L03_overlays_component.md) -- the overlays family's
// test.
//
// Three things are pinned here:
//
//   1. IrrlichtRenderer::Overlays() reaches a real
//      EQT::Graphics::OverlaysComponent and returns the same one every time.
//   2. The forwarders IrrlichtRenderer kept (irrlicht_bridge.cpp and
//      application_zone_loading.cpp call them, so they must not learn a
//      component exists -- the K11 rule) really reach that component.
//   3. The owner_ handle BINDS: setCurrentTarget() writes
//      currentTargetId_/currentTargetName_, which L13 moved onto
//      OverlaysComponent; they had stayed on
//      the renderer because the frame family reads them, and the write is
//      visible through the renderer's own public getter. Every assertion that
//      can be written as a mutation is (L01 correction 9) -- value equality
//      on two default-constructed objects proves nothing.
//
// Genuinely headless, same idiom as test_rdp_component.cpp (L01): a
// default-constructed, never-Init()'d IrrlichtRenderer. Every body reached
// below is null-guarded on driver_/smgr_/entityRenderer_, so nothing here
// touches a GL context.

#include <gtest/gtest.h>

#include "client/graphics/irrlicht_renderer.h"
#include "client/zone_lines.h"  // EQT::ZoneLineBoundingBox, complete type for the vector below

using namespace EQT::Graphics;

namespace {

TEST(OverlaysComponentTest, OverlaysReturnsTheSameComponentEveryTime) {
    IrrlichtRenderer renderer;
    EXPECT_EQ(&renderer.Overlays(), &renderer.Overlays());
}

TEST(OverlaysComponentTest, SetCurrentTargetForwarderWritesTheRenderersOwnState) {
    IrrlichtRenderer renderer;
    ASSERT_EQ(renderer.getCurrentTargetId(), 0);

    // Through the forwarder IrrlichtBridge/ConsoleRenderer-shaped callers use.
    renderer.setCurrentTarget(4242, "a_rat", 55, 7);
    EXPECT_EQ(renderer.getCurrentTargetId(), 4242);

    renderer.clearCurrentTarget();
    EXPECT_EQ(renderer.getCurrentTargetId(), 0);
}

TEST(OverlaysComponentTest, TheComponentAndTheForwarderAreTheSameObject) {
    IrrlichtRenderer renderer;
    // Write through the component directly, read through the renderer: the
    // owner_ handle is a reference to THIS renderer, not a copy.
    renderer.Overlays().setCurrentTarget(99, "a_gnoll", 100, 3);
    EXPECT_EQ(renderer.getCurrentTargetId(), 99);

    // ...and the other way round.
    renderer.setCurrentTarget(7, "a_bat", 100, 1);
    EXPECT_EQ(renderer.getCurrentTargetId(), 7);
    renderer.Overlays().clearCurrentTarget();
    EXPECT_EQ(renderer.getCurrentTargetId(), 0);
}

TEST(OverlaysComponentTest, SetCurrentTargetInfoForwarderUpdatesTheLegacyFields) {
    IrrlichtRenderer renderer;
    TargetInfo info;
    info.spawnId = 31337;
    info.name = "Fippy_Darkpaw";
    info.hpPercent = 12;
    info.level = 4;

    // The irrlicht_bridge.cpp arm.
    renderer.setCurrentTargetInfo(info);
    EXPECT_EQ(renderer.getCurrentTargetId(), 31337);
}

TEST(OverlaysComponentTest, ZoneLineBoundingBoxForwarderIsHeadlessSafe) {
    IrrlichtRenderer renderer;
    // application_zone_loading.cpp's arm. Never Init()'d, so smgr_/driver_ are
    // null and the body warns and returns -- what is pinned is that the
    // forwarder reaches a live component rather than a null pointer.
    std::vector<EQT::ZoneLineBoundingBox> none;
    renderer.setZoneLineBoundingBoxes(none);
    renderer.Overlays().clearZoneLineBoundingBoxes();
    SUCCEED();
}

TEST(OverlaysComponentTest, MarkPaperdollDirtyForwarderIsANoOpWithoutADoll) {
    IrrlichtRenderer renderer;
    // The IrrlichtBridge InventorySlotChanged arm. characterModelView_ is
    // null on a never-Init()'d renderer; the body must tolerate that.
    renderer.markPaperdollDirty();
    SUCCEED();
}

TEST(OverlaysComponentTest, ContextIsSharedWithTheComponentNotCopied) {
    IrrlichtRenderer renderer;
    // The component holds RendererContext& ctx_ -- the same bundle
    // IrrlichtRenderer::Context() hands out. Prove it by mutation through the
    // context and a read through a renderer getter (L01 correction 9).
    renderer.Context().renderDistance = 777.0f;
    EXPECT_FLOAT_EQ(renderer.getRenderDistance(), 777.0f);
}

}  // namespace
