// T329 (refactor/T329_navmesh_overlay_wire.md, B041): the NavmeshChanged
// bridge arm. Before this unit its whole body was a LOG_TRACE -- the only
// writer of OverlaysComponent::navmesh_ (EQT::Graphics::OverlaysComponent::
// setNavmesh) had zero callers, so the Ctrl+N overlay could never draw
// anything. This pins that IrrlichtBridge::applyEvent(NavmeshChanged) now
// forwards the event's PathfinderNavmesh* through to the renderer's
// overlays component, both non-null (zone load) and null (zone leave).
//
// Same bare, never-Init()'d IrrlichtRenderer idiom as
// test_overlays_component.cpp -- OverlaysComponent::setNavmesh() is
// null-guarded on nothing but IrrlichtRenderer::isLoading() (default
// false), so this is genuinely headless. The navmesh pointer is never
// dereferenced along this path (bridge -> IrrlichtRenderer::setNavmesh ->
// OverlaysComponent::setNavmesh, a plain pointer store), so a sentinel
// value stands in for a real PathfinderNavmesh* -- same idiom as
// test_rdp_component.cpp's ConstrainedTextureCache* sentinel -- rather than
// constructing a real Detour-backed navmesh from a file.

#include <gtest/gtest.h>

#include "client/bridge/irrlicht_bridge.h"
#include "client/graphics/irrlicht_renderer.h"
#include "client/state/event_bus.h"

using namespace EQT::Graphics;

namespace {

TEST(NavmeshOverlayWireTest, NavmeshChangedForwardsThePointerToTheOverlaysComponent) {
    IrrlichtRenderer renderer;
    eqt::bridge::IrrlichtBridge bridge;
    bridge.setRenderer(&renderer);

    ASSERT_EQ(renderer.Overlays().navmesh(), nullptr);

    // Never dereferenced along this path -- see file header.
    PathfinderNavmesh* sentinel = reinterpret_cast<PathfinderNavmesh*>(0x1);

    eqt::state::NavmeshChangedData loaded;
    loaded.navmesh = sentinel;
    bridge.applyEvent(eqt::state::GameEvent(eqt::state::GameEventType::NavmeshChanged, loaded));
    EXPECT_EQ(renderer.Overlays().navmesh(), sentinel);

    // Zone leave: the publishers push {nullptr}, which must pass through
    // and clear the pointer, not be treated as "no change".
    eqt::state::NavmeshChangedData cleared;
    cleared.navmesh = nullptr;
    bridge.applyEvent(eqt::state::GameEvent(eqt::state::GameEventType::NavmeshChanged, cleared));
    EXPECT_EQ(renderer.Overlays().navmesh(), nullptr);
}

}  // namespace
