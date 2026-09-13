// L06 (Batch L, refactor/L06_slash_commands_component.md).
//
// The slash family is ONE 593-line body, processSlashCommand(), and it owns no
// state: every member it touches belongs to somebody else. So what is pinned
// here is that the ONE surviving entry point -- the IrrlichtRenderer forwarder
// the bridge calls -- reaches a constructed SlashCommandsComponent, and that
// each of the four routes the moved body now uses lands on the RENDERER'S OWN
// storage rather than a copy:
//
//   1. owner_.member_  -- the frame family's UI-panel state (/newui, /inventory).
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
#include "client/graphics/frame/frame_component.h"  // T363: skillsPopupState_, processPlayerInput

#include <vector>

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
    renderer.processSlashCommand("/inventory");
    EXPECT_TRUE(renderer.isInventoryOpen());
    renderer.processSlashCommand("/inventory");
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
    EXPECT_EQ(renderer.getCameraModeString(), "First Person Camera");

    // cycleCameraMode() has no IrrlichtRenderer forwarder (L05); /camera cycle
    // reaches it as ctx_.PlayerCamera().cycleCameraMode() (and since T461 so
    // does F9's arm in FrameComponent).
    renderer.processSlashCommand("/camera cycle");
    EXPECT_EQ(renderer.getCameraModeString(), "Overhead Camera");
    renderer.processSlashCommand("/camera cycle");
    EXPECT_EQ(renderer.getCameraModeString(), "Chase Camera");

    // A write through the component's sibling is seen by the next dispatch.
    renderer.PlayerCamera().setCameraMode(CameraMode::FirstPerson);
    renderer.processSlashCommand("/camera cycle");
    EXPECT_EQ(renderer.getCameraModeString(), "Overhead Camera");

    // An unparseable mode name must leave the mode alone.
    renderer.processSlashCommand("/camera sidescroll");
    EXPECT_EQ(renderer.getCameraModeString(), "Overhead Camera");
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

// ---------------------------------------------------------------------------
// T363 (B058): `/newskills` renamed to `/skills`, no alias (Will's ruling
// 2026-09-05). The surviving body is the fuller one -- it toggles the skills
// popup AND closes the spellbook and the inventory popup, the same
// one-center-popup-at-a-time rule /newbook and the loot bridge arm apply.
// ---------------------------------------------------------------------------

TEST(SlashCommandsComponentTest, SkillsCommandTogglesThePopupAndClosesTheOtherTwo) {
    IrrlichtRenderer renderer;

    // Put both of the other centre popups up first, through their own
    // commands -- a default-constructed state would prove nothing about the
    // closing (L01 correction 6: assert a MUTATION).
    renderer.processSlashCommand("/newbook");
    renderer.processSlashCommand("/inventory");
    ASSERT_TRUE(renderer.Frame().spellbookState_.isOpen);
    ASSERT_TRUE(renderer.isInventoryOpen());
    ASSERT_FALSE(renderer.Frame().skillsPopupState_.isOpen);

    renderer.processSlashCommand("/skills");
    EXPECT_TRUE(renderer.Frame().skillsPopupState_.isOpen);
    EXPECT_FALSE(renderer.Frame().spellbookState_.isOpen)
        << "/skills opened the skills popup without closing the spellbook";
    EXPECT_FALSE(renderer.isInventoryOpen())
        << "/skills opened the skills popup without closing the inventory";

    // Closing again must NOT re-close anything -- the mutual exclusion is
    // guarded by the open branch only.
    renderer.processSlashCommand("/skills");
    EXPECT_FALSE(renderer.Frame().skillsPopupState_.isOpen);
}

TEST(SlashCommandsComponentTest, NewSkillsIsNoLongerARendererCommand) {
    IrrlichtRenderer renderer;
    ASSERT_FALSE(renderer.Frame().skillsPopupState_.isOpen);
    // No alias: the old name falls to the chain's `else` (one LOG_WARN) and
    // must leave every panel exactly as it was.
    renderer.processSlashCommand("/newskills");
    EXPECT_FALSE(renderer.Frame().skillsPopupState_.isOpen)
        << "/newskills still toggles the skills popup -- the alias was not removed";
    EXPECT_FALSE(renderer.Frame().spellbookState_.isOpen);
    EXPECT_FALSE(renderer.isInventoryOpen());
}

// The K keybind (hotkeys.json "ToggleSkills") reaches RendererAction::
// ToggleSkills, which before T363 had its OWN copy of the toggle that closed
// nothing -- the drift B058 recorded. Both paths are now one body.
TEST(SlashCommandsComponentTest, ToggleSkillsActionClosesTheOtherTwoPopupsToo) {
    IrrlichtRenderer renderer;
    renderer.processSlashCommand("/newbook");
    renderer.processSlashCommand("/inventory");
    ASSERT_TRUE(renderer.Frame().spellbookState_.isOpen);
    ASSERT_TRUE(renderer.isInventoryOpen());

    renderer.Frame().processPlayerInput(
        std::vector<RendererEvent>{RendererEvent{RendererAction::ToggleSkills}});
    EXPECT_TRUE(renderer.Frame().skillsPopupState_.isOpen);
    EXPECT_FALSE(renderer.Frame().spellbookState_.isOpen)
        << "the K keybind's arm still has its own toggle -- it did not close the spellbook";
    EXPECT_FALSE(renderer.isInventoryOpen())
        << "the K keybind's arm still has its own toggle -- it did not close the inventory";
}
