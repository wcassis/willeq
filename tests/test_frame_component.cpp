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

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

#include "client/graphics/irrlicht_renderer.h"
#include "client/graphics/frame/frame_component.h"
#include "client/bridge/game_state_bridge.h"
#include "client/state/event_bus.h"

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
    // L13 moved chatInputFocused_ ONTO FrameComponent (it is the only writer;
    // renderer_player_physics reads it as ctx_.Frame().chatInputFocused()).
    // RendererEventReceiver's same-named std::atomic<bool> is a DIFFERENT
    // member and is untouched. The early-continue at the top of
    // processPlayerInput() now reads this component's own member. Driving it through
    // processChatInput() is the production way to set it -- no device needed.
    renderer.requestChatPrefill("/tell Grimjaw ");
    renderer.Frame().processChatInput();
    EXPECT_TRUE(renderer.Frame().chatPanelState_.inputActive)
        << "the T319 prefill did not reach the moved chat panel state";
    EXPECT_EQ(renderer.Frame().chatPanelState_.inputText, "/tell Grimjaw ");

    // With chat focused, a renderer-local action must do nothing at all.
    renderer.Frame().processPlayerInput(One(RendererAction::ToggleSkills));
    EXPECT_FALSE(renderer.Frame().skillsPopupState_.isOpen)
        << "processPlayerInput ran its switch while chat was focused -- the "
           "read of chatInputFocused_ is not seeing the component's member";
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

// =========================================================================
// T357 (B057) -- Tab completion, through the key drain a player reaches.
//
// Red before the fix: processChatInput()'s while-loop had no KEY_TAB arm, so
// Tab fell through to the printable-character test (char 9 < 32) and did
// nothing at all; CommandProcessor::getCompletions() and
// CommandRegistry::getCompletions() were both implemented, tested and called
// by NOTHING in production while both CLAUDE.md files advertised the feature.
//
// The vocabulary here is a stand-in for the closure Application installs in
// initRendererAndGraphics() (that statement is itself pinned by
// tests/goldens/application_init_order_g04.txt entry 256, which is the
// reachability net -- this file pins the behaviour).
// =========================================================================

irr::SEvent TabKeyDown() {
    irr::SEvent ev{};
    ev.EventType = irr::EET_KEY_INPUT_EVENT;
    ev.KeyInput.Key = irr::KEY_TAB;
    ev.KeyInput.Char = L'\t';
    ev.KeyInput.PressedDown = true;
    ev.KeyInput.Shift = false;
    ev.KeyInput.Control = false;
    return ev;
}

class CompletionCaptureBridge : public eqt::bridge::GameStateBridge {
public:
    void applyEvent(const eqt::state::GameEvent&) override {}
};

std::vector<std::string> DrainSystemMessages(eqt::bridge::GameStateBridge& bridge) {
    std::vector<std::string> out;
    for (auto& ev : bridge.drainEvents()) {
        if (ev.type == eqt::state::GameEventType::SystemMessage) {
            out.push_back(std::get<eqt::state::ChatMessageData>(ev.data).message);
        }
    }
    return out;
}

class ChatTabCompletionTest : public ::testing::Test {
protected:
    void SetUp() override {
        renderer.Context().eventReceiver = std::make_unique<RendererEventReceiver>();
        renderer.setBridge(&bridge);
        // A tiny fixed vocabulary standing in for the merged registry-1 +
        // registry-2 set Application supplies. Prefix-matched and sorted
        // exactly as both production getCompletions() do.
        renderer.setCommandCompletionProvider([](const std::string& partial) {
            static const std::vector<std::string> kVocab = {
                "guild", "guildinvite", "gu", "say", "target"};
            std::vector<std::string> hits;
            for (const std::string& name : kVocab) {
                if (name.rfind(partial, 0) == 0) hits.push_back(name);
            }
            std::sort(hits.begin(), hits.end());
            return hits;
        });
    }
    // Open the chat box on the given text, the way the T319 prefill path does,
    // then press Tab and let the drain run.
    void TypeThenTab(const std::string& text) {
        renderer.requestChatPrefill(text);
        renderer.Frame().processChatInput();
        renderer.Context().eventReceiver->OnEvent(TabKeyDown());
        renderer.Frame().processChatInput();
    }
    IrrlichtRenderer renderer;
    CompletionCaptureBridge bridge;
};

TEST_F(ChatTabCompletionTest, TabCompletesAUniquePartialAndLeavesRoomForArguments) {
    TypeThenTab("/target");
    EXPECT_EQ(renderer.Frame().chatPanelState_.inputText, "/target ")
        << "Tab did not complete the only matching command";
    EXPECT_EQ(renderer.Frame().chatPanelState_.cursorPos,
              static_cast<int>(std::string("/target ").size()));
    EXPECT_TRUE(DrainSystemMessages(bridge).empty())
        << "a unique completion must not also print a candidate list";
}

TEST_F(ChatTabCompletionTest, TabOnSeveralMatchesListsThemOnOneChatLineAndKeepsTheText) {
    TypeThenTab("/gu");
    EXPECT_EQ(renderer.Frame().chatPanelState_.inputText, "/gu")
        << "an ambiguous Tab must not rewrite what the player typed";
    auto msgs = DrainSystemMessages(bridge);
    ASSERT_EQ(msgs.size(), 1u) << "the candidates must arrive as ONE chat line";
    EXPECT_NE(msgs[0].find("/gu"), std::string::npos) << "got: " << msgs[0];
    EXPECT_NE(msgs[0].find("/guild"), std::string::npos) << "got: " << msgs[0];
    EXPECT_NE(msgs[0].find("/guildinvite"), std::string::npos) << "got: " << msgs[0];
}

TEST_F(ChatTabCompletionTest, TabWithNoMatchDoesNothingAtAll) {
    TypeThenTab("/zzz");
    EXPECT_EQ(renderer.Frame().chatPanelState_.inputText, "/zzz");
    EXPECT_TRUE(DrainSystemMessages(bridge).empty());
}

TEST_F(ChatTabCompletionTest, TabOnPlainChatTextIsANoOp) {
    // Not a slash command: there is no vocabulary to complete against, and
    // eating the player's sentence would be the worst possible outcome.
    TypeThenTab("guild meeting at 8");
    EXPECT_EQ(renderer.Frame().chatPanelState_.inputText, "guild meeting at 8");
    EXPECT_TRUE(DrainSystemMessages(bridge).empty());
}

TEST_F(ChatTabCompletionTest, TabAfterTheCommandWordDoesNotTouchTheArguments) {
    // "/say gu" -- 'gu' is an argument, not a command; completing it would
    // rewrite the message being typed.
    TypeThenTab("/say gu");
    EXPECT_EQ(renderer.Frame().chatPanelState_.inputText, "/say gu");
    EXPECT_TRUE(DrainSystemMessages(bridge).empty());
}

TEST_F(ChatTabCompletionTest, TabStaysInsideTheChatBox) {
    // Consuming the event is what keeps Tab from reaching
    // HotkeyAction::CycleTargets (and, in an Irrlicht build with widgets,
    // from moving focus). The receiver's chat-focus gate is the mechanism;
    // pin that chat focus is still held after the completion.
    TypeThenTab("/target");
    EXPECT_TRUE(renderer.Frame().chatPanelState_.inputActive);
    EXPECT_TRUE(renderer.Frame().chatInputFocused())
        << "Tab dropped chat focus -- the next keystroke would go to the hotkey table";
}

TEST_F(ChatTabCompletionTest, ABareRendererWithNoProviderTreatsTabAsANoOp) {
    IrrlichtRenderer bare;
    bare.Context().eventReceiver = std::make_unique<RendererEventReceiver>();
    EXPECT_FALSE(bare.Frame().hasCommandCompletionProvider());
    bare.requestChatPrefill("/gu");
    bare.Frame().processChatInput();
    bare.Context().eventReceiver->OnEvent(TabKeyDown());
    bare.Frame().processChatInput();
    EXPECT_EQ(bare.Frame().chatPanelState_.inputText, "/gu");
}

// --- T368 (B101): the three keys that used to reach empty arms ------------
//
// RA::ToggleGroup (Alt+P), RA::TogglePet (Alt+F1, T382/B170) and
// RA::ToggleBuffWindow (Alt+B) each hit a bare `break;`. Pressing them did
// NOTHING -- not "nothing visible", nothing at all. Each now flips the
// isOpen its panel's visibility predicate reads.

TEST_F(FrameComponentTest, T368_GroupKeyFlipsTheGroupWindowOpenState) {
    EXPECT_TRUE(renderer.Frame().groupPanelState_.isOpen);
    renderer.Frame().processPlayerInput(One(RendererAction::ToggleGroup));
    EXPECT_FALSE(renderer.Frame().groupPanelState_.isOpen)
        << "RA::ToggleGroup is still an empty arm";
    renderer.Frame().processPlayerInput(One(RendererAction::ToggleGroup));
    EXPECT_TRUE(renderer.Frame().groupPanelState_.isOpen);
}

TEST_F(FrameComponentTest, T368_PetKeyFlipsThePetWindowOpenState) {
    EXPECT_TRUE(renderer.Frame().petPanelState_.isOpen);
    renderer.Frame().processPlayerInput(One(RendererAction::TogglePet));
    EXPECT_FALSE(renderer.Frame().petPanelState_.isOpen)
        << "RA::TogglePet is still an empty arm";
    renderer.Frame().processPlayerInput(One(RendererAction::TogglePet));
    EXPECT_TRUE(renderer.Frame().petPanelState_.isOpen);
}

TEST_F(FrameComponentTest, T368_BuffKeyFlipsTheBuffBarOpenState) {
    EXPECT_TRUE(renderer.Frame().buffBarState_.isOpen);
    renderer.Frame().processPlayerInput(One(RendererAction::ToggleBuffWindow));
    EXPECT_FALSE(renderer.Frame().buffBarState_.isOpen)
        << "RA::ToggleBuffWindow is still an empty arm";
    renderer.Frame().processPlayerInput(One(RendererAction::ToggleBuffWindow));
    EXPECT_TRUE(renderer.Frame().buffBarState_.isOpen);
}

TEST_F(FrameComponentTest, T368_ClosingAPanelHonoursTheDrawGateItsRendererReads) {
    // The toggle and the draw must be the same fact. Drive the key, then ask
    // the predicate the render function's early return calls.
    renderer.Frame().groupPanelState_.inGroup = true;
    EXPECT_TRUE(groupPanelIsVisible(renderer.Frame().groupPanelState_));
    renderer.Frame().processPlayerInput(One(RendererAction::ToggleGroup));
    EXPECT_FALSE(groupPanelIsVisible(renderer.Frame().groupPanelState_));

    renderer.Frame().petPanelState_.hasPet = true;
    EXPECT_TRUE(petPanelIsVisible(renderer.Frame().petPanelState_));
    renderer.Frame().processPlayerInput(One(RendererAction::TogglePet));
    EXPECT_FALSE(petPanelIsVisible(renderer.Frame().petPanelState_));

    renderer.Frame().buffBarState_.buffs[2].spellId = 42;
    EXPECT_TRUE(buffBarIsVisible(renderer.Frame().buffBarState_));
    renderer.Frame().processPlayerInput(One(RendererAction::ToggleBuffWindow));
    EXPECT_FALSE(buffBarIsVisible(renderer.Frame().buffBarState_));
}

TEST_F(FrameComponentTest, T368_TheThreeTogglesDoNotCloseTheCentrePopups) {
    // toggleSkillsWindow() clears the other centre popups on its OPEN branch;
    // these are HUD panels and must not touch them either way.
    renderer.Frame().processPlayerInput(One(RendererAction::ToggleSkills));
    ASSERT_TRUE(renderer.Frame().skillsPopupState_.isOpen);
    renderer.Frame().processPlayerInput(One(RendererAction::ToggleGroup));
    renderer.Frame().processPlayerInput(One(RendererAction::TogglePet));
    renderer.Frame().processPlayerInput(One(RendererAction::ToggleBuffWindow));
    EXPECT_TRUE(renderer.Frame().skillsPopupState_.isOpen);
}

}  // namespace
