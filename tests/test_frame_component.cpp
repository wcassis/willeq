// L11 (Batch L, refactor/L11_frame_component.md): FrameComponent, extracted
// from IrrlichtRenderer -- the per-frame loop, the input drain and the static
// UI panel state, across three TUs.
//
// Same bare, never-Init()'d IrrlichtRenderer idiom as
// test_zone_load_component / test_spell_gem_key_race: the constructor is empty
// apart from the component wiring, and the input phases this test drives
// null-guard every device/driver/scene-manager they would otherwise need, so
// no Irrlicht device and no GL context are required.
//
// L01 correction 6: both binding paths (ctx_ and owner_) are pinned by
// MUTATION through public accessors, never by address equality on private
// members.

#include <gtest/gtest.h>

#include <memory>
#include <vector>

#include "client/graphics/irrlicht_renderer.h"
#include "client/graphics/frame/frame_component.h"

using namespace EQT::Graphics;

namespace {

class FrameComponentTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Install a real receiver where Init() would (the T235 idiom), through
        // the PUBLIC context accessor rather than a friend declaration -- the
        // input phases drain it every call and it is null on a bare renderer.
        // This is also the ctx_ binding: the component reads
        // ctx_.eventReceiver, which is a reference to the renderer's own
        // member, and the test writes that member through Context().
        renderer.Context().eventReceiver = std::make_unique<RendererEventReceiver>();
    }
    std::vector<RendererEvent> One(RendererAction a) {
        return std::vector<RendererEvent>{RendererEvent(a)};
    }
    IrrlichtRenderer renderer;
};

// --- The component exists and is reachable -------------------------------

TEST_F(FrameComponentTest, AccessorReachesAConstructedComponent) {
    // If the ctor line in renderer_init.cpp ever stops constructing it, the
    // processFrame() forwarder -- the render thread's only entry point -- and
    // all four UI accessors below are null dereferences in production, not a
    // test failure.
    FrameComponent& f = renderer.Frame();
    EXPECT_EQ(&f, &renderer.Frame());
}

// --- The moved members, through both routes ------------------------------

TEST_F(FrameComponentTest, AllUIHiddenIsOneBoolSeenThroughBothRoutes) {
    // allUIHidden_ moved into the component; isAllUIHidden() is the inline
    // accessor that moved with it (K04 (f)) and stayed on IrrlichtRenderer as
    // a forwarder because slash/ and two tests call it.
    EXPECT_FALSE(renderer.isAllUIHidden());
    EXPECT_FALSE(renderer.Frame().allUIHidden_);
    renderer.Frame().allUIHidden_ = true;
    EXPECT_TRUE(renderer.isAllUIHidden()) << "the forwarder reads a copy, not the moved member";
    renderer.Frame().allUIHidden_ = false;
}

TEST_F(FrameComponentTest, NewUIEnabledIsOneBoolSeenThroughBothRoutes) {
    EXPECT_TRUE(renderer.isNewUIEnabled());
    renderer.setNewUIEnabled(false);
    EXPECT_FALSE(renderer.Frame().newUIEnabled_);
    renderer.Frame().newUIEnabled_ = true;
    EXPECT_TRUE(renderer.isNewUIEnabled());
}

TEST_F(FrameComponentTest, UIRendererForwarderReadsTheMovedMember) {
    // Null on a bare renderer (renderer_init.cpp constructs it), which is
    // exactly what the forwarder must report rather than crashing.
    EXPECT_EQ(renderer.getUIRenderer(), nullptr);
    EXPECT_EQ(renderer.getUIRenderer(), renderer.Frame().uiRenderer_.get());
}

// --- owner_: a moved body writing a member that STAYED on the renderer ----

TEST_F(FrameComponentTest, CommonInputToggleReachesTheRenderersOwnStateThroughOwner) {
    // processCommonInput() moved; the toggle it dispatches to lives on
    // DiagnosticsComponent, reached as owner_.Diagnostics(), and the bool it
    // flips (allUIHidden_) is now on THIS component. The whole round trip --
    // component body -> owner_ -> sibling component -> back to the component's
    // own member -- is one call.
    renderer.Frame().processCommonInput(One(RendererAction::ToggleAllUI));
    EXPECT_TRUE(renderer.isAllUIHidden());
    renderer.Frame().processCommonInput(One(RendererAction::ToggleAllUI));
    EXPECT_FALSE(renderer.isAllUIHidden());
}

TEST_F(FrameComponentTest, PlayerInputTogglesAMovedPanelMember) {
    // processPlayerInput()'s RA::ToggleSkills arm writes skillsPopupState_,
    // one of the sixteen panel members that moved with it.
    EXPECT_FALSE(renderer.Frame().skillsPopupState_.isOpen);
    renderer.Frame().processPlayerInput(One(RendererAction::ToggleSkills));
    EXPECT_TRUE(renderer.Frame().skillsPopupState_.isOpen);
    renderer.Frame().processPlayerInput(One(RendererAction::ToggleSpellbook));
    EXPECT_TRUE(renderer.Frame().spellbookState_.isOpen);
}

TEST_F(FrameComponentTest, PlayerInputIsIgnoredWhileChatIsFocusedThroughOwner) {
    // chatInputFocused_ STAYED on IrrlichtRenderer (renderer_event_receiver
    // and renderer_player_physics read it), so the early-continue at the top
    // of processPlayerInput() is an owner_ read. Driving it through
    // processChatInput() is the production way to set it -- no device needed.
    renderer.requestChatPrefill("/tell Grimjaw ");
    renderer.Frame().processChatInput();
    EXPECT_TRUE(renderer.Frame().chatPanelState_.inputActive)
        << "the T319 prefill did not reach the moved chat panel state";
    EXPECT_EQ(renderer.Frame().chatPanelState_.inputText, "/tell Grimjaw ");

    // With chat focused, a renderer-local action must do nothing at all.
    renderer.Frame().processPlayerInput(One(RendererAction::ToggleSkills));
    EXPECT_FALSE(renderer.Frame().skillsPopupState_.isOpen)
        << "processPlayerInput ran its switch while chat was focused -- the owner_ "
           "read of chatInputFocused_ is not seeing the renderer's member";
}

// --- ctx_: the component reads the context the renderer wrote through -----

TEST_F(FrameComponentTest, InventoryToggleCrossesTheRendererAndTheComponent) {
    // toggleInventory() stayed on IrrlichtRenderer (the bridge calls it) and
    // writes frame_->inventoryState_; processPlayerInput() reaches it as
    // owner_.toggleInventory(). Same object either way.
    EXPECT_FALSE(renderer.isInventoryOpen());
    renderer.Frame().processPlayerInput(One(RendererAction::ToggleInventory));
    EXPECT_TRUE(renderer.isInventoryOpen());
    EXPECT_EQ(renderer.Frame().inventoryState_.activePopup, PopupType::Inventory);
    renderer.Frame().processPlayerInput(One(RendererAction::ToggleInventory));
    EXPECT_FALSE(renderer.isInventoryOpen());
}

// --- The forwarder, and the thread statement in executable form ----------

TEST_F(FrameComponentTest, ProcessFrameOnANotRunningRendererStopsThroughTheForwarder) {
    // The exact call application_frame.cpp:181 makes. isRunning() stayed on
    // IrrlichtRenderer and is false on a bare renderer, so processFrame()
    // takes its first early-return -- which is the whole of the forwarder ->
    // component -> owner_ path, with no device touched.
    EXPECT_FALSE(renderer.processFrame(0.016f));
    EXPECT_FALSE(renderer.Frame().processFrame(0.016f));
}

}  // namespace
