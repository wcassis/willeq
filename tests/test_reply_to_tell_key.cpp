// T319 -- the R key (HotkeyAction::ReplyToTell) end to end.
//
// The feature was implemented in three disconnected pieces and reached by
// nothing:
//   * the binding existed (hotkey_manager.cpp, KEY_KEY_R -> ReplyToTell),
//   * the consumer existed (InputActionBridge::processDiscreteActions ->
//     ActionDispatcher::openReplyToTell(), made honest by T316), and
//   * the middle was missing -- HotkeyAction::ReplyToTell had no case in
//     RendererEventReceiver::OnEvent's switch, so the keypress decoded to
//     nothing at all.
// And even with that closed, InputActionBridge::setActionCallback() had NO
// production caller, so the "/tell <Name> " prefill the dispatcher produces
// would have reached a debug log and stopped there.
//
// This drives the WHOLE chain with the real production pieces at every
// step and asserts on the value a user would actually see -- the text
// sitting in the chat input:
//
//   RendererEventReceiver::OnEvent(KEY_KEY_R)        [render thread]
//     -> bridgeQueue_ (mutex-guarded, pushBridgeAction only)
//     -> GraphicsInputHandler::updateFromEventReceiver()  [game thread]
//     -> InputActionBridge::update() -> processDiscreteActions()
//     -> ActionDispatcher::openReplyToTell()
//     -> the action callback Application installs
//        -> IrrlichtRenderer::handleActionResult()
//        -> requestChatPrefill()                     [parks under a mutex]
//     -> IrrlichtRenderer::processChatInput()        [render thread]
//     -> chatPanelState_.inputText
//
// Only the ActionHandler is a fake (RecordingActionHandler, which supplies
// the last tell sender the way EverQuest::m_last_tell_sender does live).
// The renderer is a bare, never-Init()'d IrrlichtRenderer with a real
// RendererEventReceiver installed -- the T235/T66 no-device idiom, see
// tests/test_spell_gem_key_race.cpp.

#include <gtest/gtest.h>

#include <memory>
#include <string>

#include "action_test_fakes.h"
#include "client/action/action_dispatcher.h"
#include "client/action/input_action_bridge.h"
#include "client/graphics/irrlicht_renderer.h"
#include "client/input/graphics_input_handler.h"
#include "client/input/hotkey_manager.h"
#include "client/state/game_state.h"

// IrrlichtRenderer's friend declaration names the fixture unqualified, so
// it resolves against EQT::Graphics -- the whole file lives there (the
// T116/T235 idiom). Only SetUp() needs the friendship (it installs
// eventReceiver_ where Init() would); everything the TEST_F bodies touch
// -- processChatInput(), handleActionResult(), chatPanelState_ -- is
// already public, and friendship is not inherited anyway.
namespace EQT {
namespace Graphics {

irr::SEvent KeyDownEvent(irr::EKEY_CODE key, wchar_t ch) {
    irr::SEvent ev{};
    ev.EventType = irr::EET_KEY_INPUT_EVENT;
    ev.KeyInput.Key = key;
    ev.KeyInput.Char = ch;
    ev.KeyInput.PressedDown = true;
    ev.KeyInput.Shift = false;
    ev.KeyInput.Control = false;
    return ev;
}

class ReplyToTellKeyTest : public ::testing::Test {
protected:
    void SetUp() override {
        eqt::input::HotkeyManager::instance().resetToDefaults();

        // Render side: bare renderer + real receiver, as Init() would leave it.
        renderer.eventReceiver_ = std::make_unique<RendererEventReceiver>();
        receiver = renderer.eventReceiver_.get();

        // Game side: the real dispatcher and bridge over a recording handler.
        state.world().setZoneConnected(true);
        dispatcher = std::make_unique<eqt::action::ActionDispatcher>(state);
        dispatcher->setActionHandler(&handler);
        bridge = std::make_unique<eqt::action::InputActionBridge>(state, *dispatcher);

        graphicsInput = std::make_unique<eqt::input::GraphicsInputHandler>(receiver);
        bridge->setInputHandler(graphicsInput.get());

        // EXACTLY the callback Application installs (application.cpp,
        // "T319: setActionCallback() had NO production caller"). The
        // decision lives in handleActionResult(), which is production code.
        auto* r = &renderer;
        bridge->setActionCallback(
            [r](const std::string& name, const eqt::action::ActionResult& result) {
                r->handleActionResult(name, result.success, result.message);
            });
    }

    // One game tick followed by one render-thread frame step.
    void tick() {
        bridge->update(0.016f);
        renderer.Frame().processChatInput();
    }

    eqt::state::GameState state;
    eqt::action::test::RecordingActionHandler handler;
    std::unique_ptr<eqt::action::ActionDispatcher> dispatcher;
    std::unique_ptr<eqt::action::InputActionBridge> bridge;
    std::unique_ptr<eqt::input::GraphicsInputHandler> graphicsInput;

    IrrlichtRenderer renderer;
    RendererEventReceiver* receiver = nullptr;
};

// The headline: press R, and the chat input opens holding "/tell <Name> ".
// Red before this unit at the very first step -- OnEvent decoded R to
// nothing, so the bridge queue stayed empty.
TEST_F(ReplyToTellKeyTest, RKeyOpensChatPrefilledWithTheLastTellSender) {
    handler.lastTellSender = "Grimjaw";

    receiver->OnEvent(KeyDownEvent(irr::KEY_KEY_R, L'r'));
    ASSERT_EQ(receiver->getBridgeQueueSize(), 1u)
        << "OnEvent did not decode KEY_KEY_R into a bridge action";

    tick();

    EXPECT_TRUE(renderer.Frame().chatPanelState_.inputActive);
    EXPECT_EQ(renderer.Frame().chatPanelState_.inputText, "/tell Grimjaw ");
    EXPECT_EQ(renderer.Frame().chatPanelState_.cursorPos,
              static_cast<int>(std::string("/tell Grimjaw ").size()))
        << "cursor must sit after the trailing space, ready for the message";
    EXPECT_TRUE(receiver->isChatInputFocused())
        << "typed characters must go to the chat input, not back to the hotkeys";
}

// The refusal half (T316): with no tell received, openReplyToTell() fails
// and nothing is prefilled -- no empty chat box, no "/tell  ".
TEST_F(ReplyToTellKeyTest, NoTellReceived_OpensNothing) {
    handler.lastTellSender = "";

    receiver->OnEvent(KeyDownEvent(irr::KEY_KEY_R, L'r'));
    tick();

    EXPECT_FALSE(renderer.Frame().chatPanelState_.inputActive);
    EXPECT_TRUE(renderer.Frame().chatPanelState_.inputText.empty());
    EXPECT_FALSE(receiver->isChatInputFocused());
}

// The prefill is a one-shot: the render thread takes it, and later frames
// must not re-open the box over whatever the user has since typed.
TEST_F(ReplyToTellKeyTest, PrefillIsTakenExactlyOnce) {
    handler.lastTellSender = "Grimjaw";

    receiver->OnEvent(KeyDownEvent(irr::KEY_KEY_R, L'r'));
    tick();
    ASSERT_EQ(renderer.Frame().chatPanelState_.inputText, "/tell Grimjaw ");

    // User types; further frames must leave the text alone.
    renderer.Frame().chatPanelState_.inputText = "/tell Grimjaw hello";
    for (int i = 0; i < 5; ++i) renderer.Frame().processChatInput();
    EXPECT_EQ(renderer.Frame().chatPanelState_.inputText, "/tell Grimjaw hello");
}

// Once chat is focused, R is a typed character -- OnEvent must not decode
// it as a hotkey, or every "r" in a sentence would re-prefill the box.
TEST_F(ReplyToTellKeyTest, RIsNotAHotkeyWhileChatIsFocused) {
    handler.lastTellSender = "Grimjaw";
    receiver->setChatInputFocused(true);

    receiver->OnEvent(KeyDownEvent(irr::KEY_KEY_R, L'r'));
    EXPECT_EQ(receiver->getBridgeQueueSize(), 0u);
}

// A second tell from someone else re-aims the reply. Pins that the prefill
// is read at keypress time, not cached from the first press.
TEST_F(ReplyToTellKeyTest, PrefillFollowsTheMostRecentSender) {
    handler.lastTellSender = "Grimjaw";
    receiver->OnEvent(KeyDownEvent(irr::KEY_KEY_R, L'r'));
    tick();
    ASSERT_EQ(renderer.Frame().chatPanelState_.inputText, "/tell Grimjaw ");

    // Close the box the way ESC/submit does, then a new tell arrives.
    renderer.Frame().chatPanelState_.inputText.clear();
    renderer.Frame().chatPanelState_.inputActive = false;
    receiver->setChatInputFocused(false);
    handler.lastTellSender = "Slyvia";

    receiver->OnEvent(KeyDownEvent(irr::KEY_KEY_R, L'r'));
    tick();
    EXPECT_EQ(renderer.Frame().chatPanelState_.inputText, "/tell Slyvia ");
}

}  // namespace Graphics
}  // namespace EQT
