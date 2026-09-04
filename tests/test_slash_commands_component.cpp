// L06 (Batch L, refactor/L06_slash_commands_component.md).
//
// The slash family is ONE 593-line body, processSlashCommand(), and it owns no
// state: every member it touches belongs to somebody else. So what is pinned
// here is that the ONE surviving entry point -- the IrrlichtRenderer forwarder
// the bridge calls -- reaches a constructed SlashCommandsComponent, and that
// each of the four routes the moved body now uses lands on the RENDERER'S OWN
// storage rather than a copy:
//
//   1. owner_.member_  -- the frame family's UI-panel state (/newui, /newinv).
//   2. owner_.Method() -- a method that stayed on IrrlichtRenderer
//      (/renderdist).
//   3. ctx_.<member>   -- observed through IrrlichtRenderer::Context().
//   4. owner_.<Family>().X() -- the two sibling components the dispatcher
//      reaches, DiagnosticsComponent (/hideui) and PlayerCameraComponent
//      (/camera).
//
// Per L01 correction 6 / L01 correction 9 every assertion that can be a
// MUTATION is one -- value equality on a default-constructed object proves
// nothing.
//
// Genuinely headless, the same idiom as test_diagnostics_component.cpp and
// test_player_camera_component.cpp: a default-constructed, never-Init()'d
// IrrlichtRenderer. Nothing here touches a GL context. Each of these commands
// is already driven this way by an existing test (test_ui_hide_command,
// test_inventory_toggle, test_irrlicht_camera_command), so the reachable
// paths are known-safe on a bare object.

#include <gtest/gtest.h>

#include "client/graphics/irrlicht_renderer.h"
#include "client/graphics/slash/slash_commands_component.h"

using namespace EQT::Graphics;

// --- route 1: owner_.member_, the frame family's UI-panel state -------------

TEST(SlashCommandsComponentTest, NewUiCommandWritesTheRenderersOwnMemberThroughOwner) {
    IrrlichtRenderer renderer;
    const bool before = renderer.isNewUIEnabled();

    // /newui's whole body is `owner_.newUIEnabled_ = !owner_.newUIEnabled_;`.
    // Reading it back off IrrlichtRenderer's own accessor is the proof the
    // component wrote the renderer's member and not a copy of it.
    renderer.processSlashCommand("/newui");
    EXPECT_EQ(renderer.isNewUIEnabled(), !before);
    renderer.processSlashCommand("/newui");
    EXPECT_EQ(renderer.isNewUIEnabled(), before);
}

TEST(SlashCommandsComponentTest, NewInvCommandReachesTheInventoryPopupState) {
    IrrlichtRenderer renderer;
    ASSERT_FALSE(renderer.isInventoryOpen());
    renderer.processSlashCommand("/newinv");
    EXPECT_TRUE(renderer.isInventoryOpen());
    renderer.processSlashCommand("/newinv");
    EXPECT_FALSE(renderer.isInventoryOpen());
}

// --- route 2 + 3: owner_.Method(), observed through the context -------------

TEST(SlashCommandsComponentTest, RenderDistCommandWritesTheContextsMember) {
    IrrlichtRenderer renderer;
    // 300 is deliberately under the default constrained preset's 500-unit
    // clipDistance: setRenderDistance() caps the EFFECTIVE distance by it, so
    // asking for 777 here would store 500 and prove nothing about the route.
    renderer.processSlashCommand("/renderdist 300");
    EXPECT_FLOAT_EQ(renderer.getRenderDistance(), 300.0f);
    // renderDistance_ is a RendererContext core member: the same write must be
    // visible through Context(), which is how every other component sees it.
    EXPECT_FLOAT_EQ(renderer.Context().renderDistance, 300.0f);

    // The other direction: a context write is seen by the renderer's reader,
    // so the two are one storage location and not two.
    renderer.Context().renderDistance = 321.0f;
    EXPECT_FLOAT_EQ(renderer.getRenderDistance(), 321.0f);

    // Out-of-range arguments are rejected by the body, not clamped.
    renderer.processSlashCommand("/renderdist 5");
    EXPECT_FLOAT_EQ(renderer.getRenderDistance(), 321.0f);
}

// --- route 4: the two sibling components ------------------------------------

TEST(SlashCommandsComponentTest, HideUiCommandReachesTheSameDiagnosticsComponent) {
    IrrlichtRenderer renderer;
    ASSERT_FALSE(renderer.isAllUIHidden());

    // The body calls owner_.Diagnostics().toggleAllUI(). Interleaving the two
    // routes proves they are the same component instance and the same bool.
    renderer.processSlashCommand("/hideui");
    EXPECT_TRUE(renderer.isAllUIHidden());
    renderer.Diagnostics().toggleAllUI();
    EXPECT_FALSE(renderer.isAllUIHidden());
    renderer.processSlashCommand("/toggleui");
    EXPECT_TRUE(renderer.isAllUIHidden());
    renderer.processSlashCommand("/ui");
    EXPECT_FALSE(renderer.isAllUIHidden());
}

TEST(SlashCommandsComponentTest, CameraCommandReachesTheSamePlayerCameraComponent) {
    IrrlichtRenderer renderer;
    renderer.processSlashCommand("/camera first person");
    EXPECT_EQ(renderer.getCameraModeString(), "First Person");

    // /camera cycle is the ONLY route to cycleCameraMode() -- it has no
    // IrrlichtRenderer forwarder (L05) -- so this is also that method's live
    // path, reached as owner_.PlayerCamera().cycleCameraMode().
    renderer.processSlashCommand("/camera cycle");
    EXPECT_EQ(renderer.getCameraModeString(), "Free");
    renderer.processSlashCommand("/camera cycle");
    EXPECT_EQ(renderer.getCameraModeString(), "Follow");

    // A write through the component's sibling is seen by the next dispatch.
    renderer.PlayerCamera().setCameraMode(CameraMode::FirstPerson);
    renderer.processSlashCommand("/camera cycle");
    EXPECT_EQ(renderer.getCameraModeString(), "Free");

    // An unparseable mode name must leave the mode alone.
    renderer.processSlashCommand("/camera sidescroll");
    EXPECT_EQ(renderer.getCameraModeString(), "Free");
}

// --- the dispatcher's fall-through -------------------------------------------

TEST(SlashCommandsComponentTest, UnknownCommandIsInertAndDisturbsNothing) {
    IrrlichtRenderer renderer;
    const bool ui = renderer.isNewUIEnabled();
    renderer.processSlashCommand("/l06-no-such-command with args");
    renderer.processSlashCommand("");
    EXPECT_EQ(renderer.isNewUIEnabled(), ui);
    EXPECT_FALSE(renderer.isAllUIHidden());
}
