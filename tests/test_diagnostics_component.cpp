// L04 (Batch L, refactor/L04_diagnostics_component.md).
//
// The diagnostics family is the batch's BREADTH test of RendererContext: its
// bodies read 30 of the context's 54 members and 39 more IrrlichtRenderer
// members through the owner_ transition handle. What is pinned here is that
// both paths reach the RENDERER'S OWN storage, not a copy:
//
//   1. Diagnostics() reaches one stable DiagnosticsComponent.
//   2. The ctx_ path: a toggle whose body writes only ctx_.<member> is
//      observed through IrrlichtRenderer's own public reader, and a context
//      member the same body READS gates it. Both directions, one round trip.
//   3. The owner_ path: a toggle whose body writes only owner_.<member_> is
//      observed through IrrlichtRenderer's own public reader.
//   4. The three forwarders that stay on IrrlichtRenderer for the bridge and
//      Application (dumpScene, getMemoryReport, runSceneProfile) still reach
//      the component.
//
// Per L01 correction 9 every assertion that can be a MUTATION is one -- value
// equality on a default-constructed object proves nothing.
//
// Genuinely headless, same idiom as test_rdp_component.cpp: a
// default-constructed, never-Init()'d IrrlichtRenderer. Nothing here touches a
// GL context.

#include <gtest/gtest.h>

#include "client/graphics/irrlicht_renderer.h"
#include "client/graphics/diagnostics/diagnostics_component.h"

using namespace EQT::Graphics;

TEST(DiagnosticsComponentTest, DiagnosticsReturnsTheSameComponentEveryTime) {
    IrrlichtRenderer renderer;
    EXPECT_EQ(&renderer.Diagnostics(), &renderer.Diagnostics());
}

// --- the ctx_ path, both directions -----------------------------------------

TEST(DiagnosticsComponentTest, PortalOcclusionToggleReadsAndWritesThroughTheContext) {
    IrrlichtRenderer renderer;
    ASSERT_FALSE(renderer.isPortalOcclusionEnabled());

    // togglePortalOcclusion() first READS ctx_.portalOcclusionEligible and
    // bails when it is false. Nothing has loaded a zone, so it is false and
    // the toggle must be a no-op -- that is the context READ under test.
    renderer.Diagnostics().togglePortalOcclusion();
    EXPECT_FALSE(renderer.isPortalOcclusionEnabled());

    // Make it eligible through the context, and the same call must now flip
    // ctx_.portalOcclusionEnabled -- which IrrlichtRenderer reads back off its
    // own member. Context write -> component read -> component write ->
    // renderer read.
    renderer.Context().portalOcclusionEligible = true;
    renderer.Diagnostics().togglePortalOcclusion();
    EXPECT_TRUE(renderer.isPortalOcclusionEnabled());
    renderer.Diagnostics().togglePortalOcclusion();
    EXPECT_FALSE(renderer.isPortalOcclusionEnabled());
}

TEST(DiagnosticsComponentTest, WireframeToggleWritesTheRenderersOwnMember) {
    IrrlichtRenderer renderer;
    // toggleWireframe()'s only unconditional write is ctx_.wireframeMode; the
    // node loops are all empty on a never-Init()'d renderer.
    renderer.Context().wireframeMode = true;
    renderer.Diagnostics().toggleWireframe();
    EXPECT_FALSE(renderer.Context().wireframeMode);
    renderer.Diagnostics().toggleWireframe();
    EXPECT_TRUE(renderer.Context().wireframeMode);
}

// --- the owner_ path --------------------------------------------------------

TEST(DiagnosticsComponentTest, PortalDebugDrawToggleWritesThroughTheOwnerHandle) {
    IrrlichtRenderer renderer;
    // portalDebugDraw_ was below the context's 5-family core threshold; L13
    // moved it ONTO DiagnosticsComponent under the pairwise rule (diagnostics
    // writes it, frame reads it as ctx_.Diagnostics().portalDebugDraw()).
    // IrrlichtRenderer's own
    // reader must see the flip.
    ASSERT_FALSE(renderer.isPortalDebugDrawEnabled());
    renderer.Diagnostics().togglePortalDebugDraw();
    EXPECT_TRUE(renderer.isPortalDebugDrawEnabled());
    renderer.Diagnostics().togglePortalDebugDraw();
    EXPECT_FALSE(renderer.isPortalDebugDrawEnabled());
}

TEST(DiagnosticsComponentTest, AllUIToggleWritesThroughTheOwnerHandle) {
    IrrlichtRenderer renderer;
    ASSERT_FALSE(renderer.isAllUIHidden());
    renderer.Diagnostics().toggleAllUI();
    EXPECT_TRUE(renderer.isAllUIHidden());
    renderer.Diagnostics().toggleAllUI();
    EXPECT_FALSE(renderer.isAllUIHidden());
}

TEST(DiagnosticsComponentTest, StencilDebugDrawToggleWritesThroughTheOwnerHandle) {
    IrrlichtRenderer renderer;
    ASSERT_FALSE(renderer.isStencilDebugDrawEnabled());
    renderer.Diagnostics().toggleStencilDebugDraw();
    EXPECT_TRUE(renderer.isStencilDebugDrawEnabled());
}

// --- the forwarders that stay -----------------------------------------------

TEST(DiagnosticsComponentTest, MemoryReportForwarderReachesTheComponent) {
    IrrlichtRenderer renderer;
    MemoryReportInput ext{};
    // irrlicht_bridge.cpp calls this one (the /pmem path); it must produce the
    // same lines as the component it now forwards to.
    const auto viaRenderer = renderer.getMemoryReport(ext);
    const auto viaComponent = renderer.Diagnostics().getMemoryReport(ext);
    EXPECT_FALSE(viaRenderer.empty());
    EXPECT_EQ(viaRenderer.size(), viaComponent.size());
}

TEST(DiagnosticsComponentTest, DumpSceneAndSceneProfileForwardersAreLive) {
    IrrlichtRenderer renderer;
    // dumpScene() is what irrlicht_bridge.cpp calls on the
    // DiagnosticsSceneDump event; runSceneProfile() is what application.cpp
    // calls for --scene-profile. Neither may reach a null component.
    renderer.dumpScene();
    renderer.runSceneProfile();
    SUCCEED();
}
