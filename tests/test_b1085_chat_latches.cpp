// B1085 (GitLab #405) -- the three chat key latches latch ONLY while the
// chat is unfocused, and every focus transition resets them.
//
// The measured mechanism (issue #405, Will's 2026-10-15 XU4 sessions):
// RendererEventReceiver::OnEvent() used to latch enterKeyPressed_ /
// escapeKeyPressed_ / slashKeyPressed_ on EVERY press, BEFORE the
// chat-focused gate that returns early for a focused chat. The latches are
// read-and-reset only in FrameComponent's UNFOCUSED branch of
// processChatInput() (enter/slash open the chat; escape -> handleEscapeKey(),
// which closes the newest window or clears the target), so every press typed
// INTO the focused box left a stale latch for the first unfocused frame:
//   1. `/who` + Enter: Submit drops focus; the next frame the stale ENTER
//      latch re-opened the chat EMPTY.
//   2. ESC: Cancel drops focus; the next frame the stale SLASH latch (the
//      `/` from step 1) re-opened it with `/` -- "sometimes a forward
//      slash enters the chat text input field".
//   3. A stale ESCAPE ran handleEscapeKey() -- closing a window or
//      CLEARING THE TARGET although the press was meant for the chat box.
//
// The fix: OnEvent() sets the three latches only while chat is unfocused
// (below the focused gate), and setChatInputFocused() resets all three on
// every transition in BOTH directions. The pendingKeyEvents_ queue is
// unchanged: a press while focused is the chat box's, and only the box sees
// the queue. The B998 character-select host's hand-spending of the three
// latches (char_select_host.cpp:144-146) is now the shared reset behind the
// host's own setChatInputFocused(true) -- behaviour kept, pinned by
// tests/test_character_select.cpp.
//
// Device-free: the T235/T66 no-device idiom (a bare, never-Init()'d
// IrrlichtRenderer with a real RendererEventReceiver installed). Part 1
// drives OnEvent directly; parts 2-4 run the production body
// processChatInput() itself, frame by frame.

#include <gtest/gtest.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "client/bridge/game_state_bridge.h"
#include "client/graphics/irrlicht_renderer.h"
#include "client/graphics/renderer_event_receiver.h"
#include "client/input/hotkey_manager.h"

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

// A bridge that records every intent -- the ChatSubmitIntent a Submit
// publishes and the CloseBankIntent a stale ESC would push. pushIntent() is
// a plain base method (NOT virtual), so the recording happens through
// drainIntents(): whatever processChatInput() pushed is pulled out and kept.
class RecordingBridge : public eqt::bridge::GameStateBridge {
public:
    void applyEvent(const eqt::state::GameEvent&) override {}
    std::vector<eqt::events::RendererIntent> intents;
    void collect() { for (auto& i : drainIntents()) intents.push_back(std::move(i)); }
    bool saw(const std::function<bool(const eqt::events::RendererIntent&)>& pred) const {
        for (const auto& i : intents) {
            if (pred(i)) return true;
        }
        return false;
    }
    bool sawChatSubmit() const {
        return saw([](const auto& i) { return std::get_if<eqt::events::ChatSubmitIntent>(&i); });
    }
    bool sawCloseBank() const {
        return saw([](const auto& i) { return std::get_if<eqt::events::CloseBankIntent>(&i); });
    }
};

class B1085ChatLatchesTest : public ::testing::Test {
protected:
    void SetUp() override {
        eqt::input::HotkeyManager::instance().resetToDefaults();

        // The T235 idiom: a real receiver where Init() would, through the
        // public context accessor; processChatInput() reads
        // ctx_.eventReceiver (a reference to the renderer's own member) and
        // publishes on ctx_.bridge.
        renderer.Context().eventReceiver = std::make_unique<RendererEventReceiver>();
        receiver = renderer.Context().eventReceiver.get();
        renderer.setBridge(&bridge);
    }

    // One frame, the production way: processChatInput() itself (the
    // prefill, options-capture and text-entry gates included -- none is
    // armed in these tests), then pull its intents out of the base bridge
    // queue (pushIntent() is not virtual, so recording goes through the
    // drain).
    void Frame() {
        renderer.Frame().processChatInput();
        bridge.collect();
    }

    IrrlichtRenderer renderer;
    RendererEventReceiver* receiver = nullptr;
    RecordingBridge bridge;
};

// ---------------------------------------------------------------------------
// Part 1 -- the receiver alone: what OnEvent latches, per focus state.
// ---------------------------------------------------------------------------

// Will's exact presses, typed INTO a focused chat: none of the three latches
// may set, and the presses must still reach the box (only the queue).
// RED at the parent commit: the latches were set above the focused gate.
TEST_F(B1085ChatLatchesTest, PressesWhileChatFocusedDoNotLatchAnything) {
    receiver->setChatInputFocused(true);

    receiver->OnEvent(KeyDownEvent(irr::KEY_RETURN, 0));
    EXPECT_FALSE(receiver->enterKeyPressed())
        << "a focused-chat Enter must not leave a latch for the first unfocused frame";
    receiver->OnEvent(KeyDownEvent(irr::KEY_ESCAPE, 0));
    EXPECT_FALSE(receiver->escapeKeyPressed())
        << "a focused-chat Escape must not arm FrameComponent's ESC handler";
    receiver->OnEvent(KeyDownEvent(irr::KEY_OEM_2, L'/'));
    EXPECT_FALSE(receiver->slashKeyPressed())
        << "the `/` Will saw inserted after an ESC is this latch replaying";

    // The presses are still the chat box's: only the queue is filled.
    ASSERT_TRUE(receiver->hasPendingKeyEvents());
    const auto e1 = receiver->popKeyEvent();
    ASSERT_EQ(e1.key, irr::KEY_RETURN);
    const auto e2 = receiver->popKeyEvent();
    ASSERT_EQ(e2.key, irr::KEY_ESCAPE);
    const auto e3 = receiver->popKeyEvent();
    ASSERT_EQ(e3.key, irr::KEY_OEM_2);
    EXPECT_FALSE(receiver->hasPendingKeyEvents());
}

// Unfocused: the latches are the whole point of the three keys -- they latch.
TEST_F(B1085ChatLatchesTest, PressesWhileChatUnfocusedLatch) {
    receiver->setChatInputFocused(false);

    receiver->OnEvent(KeyDownEvent(irr::KEY_RETURN, 0));
    EXPECT_TRUE(receiver->enterKeyPressed());
    receiver->OnEvent(KeyDownEvent(irr::KEY_OEM_2, L'/'));
    EXPECT_TRUE(receiver->slashKeyPressed());
    receiver->OnEvent(KeyDownEvent(irr::KEY_ESCAPE, 0));
    EXPECT_TRUE(receiver->escapeKeyPressed());
}

// The shared reset, BOTH directions: a press that changed the focus is
// consumed by the transition and can never replay in the new state.
TEST_F(B1085ChatLatchesTest, AFocusTransitionResetsEveryLatch) {
    receiver->setChatInputFocused(false);
    receiver->OnEvent(KeyDownEvent(irr::KEY_RETURN, 0));
    receiver->OnEvent(KeyDownEvent(irr::KEY_OEM_2, L'/'));
    receiver->OnEvent(KeyDownEvent(irr::KEY_ESCAPE, 0));
    ASSERT_TRUE(receiver->enterKeyPressed() && receiver->slashKeyPressed() &&
                receiver->escapeKeyPressed());

    receiver->setChatInputFocused(true);
    EXPECT_FALSE(receiver->enterKeyPressed());
    EXPECT_FALSE(receiver->slashKeyPressed());
    EXPECT_FALSE(receiver->escapeKeyPressed());

    // ...and back: the same rule for the off->on transition.
    receiver->setChatInputFocused(false);
    receiver->OnEvent(KeyDownEvent(irr::KEY_ESCAPE, 0));
    ASSERT_TRUE(receiver->escapeKeyPressed());
    receiver->setChatInputFocused(true);
    EXPECT_FALSE(receiver->escapeKeyPressed());
}

// ---------------------------------------------------------------------------
// Parts 2-4 -- Will's exact session, through the production body, frame by
// frame.
// ---------------------------------------------------------------------------

// `/` -> type `who` -> Enter -> ESC: the chat must be CLOSED after ONE
// Escape, the input empty, and handleEscapeKey() never run. RED at the
// parent commit: the stale ENTER latch re-opened the chat empty on the first
// unfocused frame (the "focus stayed" state), the stale SLASH latch
// re-opened it with `/` (the slash symptom), and a stale ESCAPE reached the
// window stack.
TEST_F(B1085ChatLatchesTest, WhoThenEnterThenEscapeLeavesTheChatClosedAndEmpty) {
    FrameComponent& f = renderer.Frame();

    // `/` unfocused: the frame's slash check opens the chat (and clears the
    // queue, so the `/` is not typed in twice).
    receiver->OnEvent(KeyDownEvent(irr::KEY_OEM_2, L'/'));
    Frame();
    ASSERT_TRUE(receiver->isChatInputFocused());
    ASSERT_EQ(f.chatPanelState_.inputText, "/");

    for (char c : std::string("who")) {
        receiver->OnEvent(KeyDownEvent(irr::KEY_KEY_CODES_COUNT, static_cast<wchar_t>(c)));
    }
    Frame();
    EXPECT_EQ(f.chatPanelState_.inputText, "/who");

    // Enter: Submit -- publishes, drops focus, clears the text.
    receiver->OnEvent(KeyDownEvent(irr::KEY_RETURN, 0));
    Frame();
    EXPECT_FALSE(receiver->isChatInputFocused());
    EXPECT_TRUE(bridge.sawChatSubmit()) << "the sent line must publish";
    EXPECT_FALSE(bridge.sawCloseBank());

    // The FIRST UNFOCUSED FRAME -- where the stale ENTER/SLASH latches would
    // replay and re-open the chat (Will's "focus stayed").
    Frame();
    EXPECT_FALSE(receiver->isChatInputFocused())
        << "a stale latch re-opened the chat after the sent line";
    EXPECT_TRUE(f.chatPanelState_.inputText.empty())
        << "the re-opened chat carried / -- the slash symptom";

    // ESC, now genuinely unfocused (Will's session: a second, deliberate
    // Escape with the chat already gone): it is the world's, so it latches
    // and the frame's ESC arm consumes it -- but the arm must find NO
    // window to close (nothing is open here) and must not clear anything.
    receiver->OnEvent(KeyDownEvent(irr::KEY_ESCAPE, 0));
    ASSERT_TRUE(receiver->escapeKeyPressed())
        << "an UNFOCUSED Escape latches -- that is its real job";
    Frame();

    EXPECT_FALSE(receiver->isChatInputFocused());
    EXPECT_FALSE(f.chatPanelState_.inputActive);
    EXPECT_TRUE(f.chatPanelState_.inputText.empty());
    EXPECT_FALSE(bridge.sawCloseBank())
        << "no window is open, so the ESC must close nothing";
}

// The second half of the symptom: an Enter AFTER a sent line must not
// re-open the chat. RED at the parent commit -- the Enter's own latch
// survived the Submit's focus drop and the next frame's enter check
// re-opened the chat empty, "the chat window in focus with no text in the
// input field".
TEST_F(B1085ChatLatchesTest, AnEnterAfterASentLineDoesNotReopenTheChat) {
    FrameComponent& f = renderer.Frame();
    // `/` opens the chat the production way, then the rest of the line.
    receiver->OnEvent(KeyDownEvent(irr::KEY_OEM_2, L'/'));
    Frame();
    for (char c : std::string("who")) {
        receiver->OnEvent(KeyDownEvent(irr::KEY_KEY_CODES_COUNT, static_cast<wchar_t>(c)));
    }
    Frame();
    ASSERT_EQ(f.chatPanelState_.inputText, "/who");

    receiver->OnEvent(KeyDownEvent(irr::KEY_RETURN, 0));
    Frame();
    ASSERT_FALSE(receiver->isChatInputFocused());
    ASSERT_TRUE(bridge.sawChatSubmit());

    // A frame later -- the stale-latch replay window -- still closed,
    // still empty.
    Frame();
    EXPECT_FALSE(receiver->isChatInputFocused())
        << "the submit's Enter must die with the focus transition";
    EXPECT_FALSE(f.chatPanelState_.inputActive);
    EXPECT_TRUE(f.chatPanelState_.inputText.empty());
}

// Mechanism 3 (issue #405): an ESC typed into the focused box must not arm
// handleEscapeKey() for the first unfocused frame -- here with the bank open
// and a target set, the two arms it can reach. At the parent commit the
// box's ESC latched, and the first unfocused frame after the box was closed
// (its ESC, or a re-open) closed the bank and cleared the target although
// the press was the box's.
TEST_F(B1085ChatLatchesTest, AStaleEscapeFromTheBoxDoesNotCloseAWindowOrClearTheTarget) {
    FrameComponent& f = renderer.Frame();
    f.bankState_.isOpen = true;
    renderer.Overlays().setCurrentTarget(42, "Stalaggit");
    ASSERT_EQ(renderer.getCurrentTargetId(), 42u);

    // The box's own ESC, with the bank open behind it.
    receiver->OnEvent(KeyDownEvent(irr::KEY_OEM_2, L'/'));
    Frame();
    receiver->OnEvent(KeyDownEvent(irr::KEY_KEY_CODES_COUNT, L'q'));
    Frame();
    receiver->OnEvent(KeyDownEvent(irr::KEY_ESCAPE, 0));  // the box's ESC
    ASSERT_FALSE(receiver->escapeKeyPressed())
        << "the box's ESC latched -- the first unfocused frame would run "
           "handleEscapeKey() on a press that was the box's";
    Frame();  // Cancel: drops focus, clears the text

    EXPECT_FALSE(receiver->isChatInputFocused());
    EXPECT_TRUE(f.bankState_.isOpen)
        << "a stale ESC from the chat box closed a window (B460's press)";
    EXPECT_EQ(renderer.getCurrentTargetId(), 42u)
        << "a stale ESC from the chat box cleared the target (B905's press)";
    EXPECT_FALSE(bridge.sawCloseBank());

    // A genuinely UNFOCUSED Escape still does its real job: the window
    // stack closes the newest window the way the window itself closes (the
    // bank is server-owned, so it pushes CloseBankIntent and stays up until
    // the server's Closed event comes back, T290's rule) -- and ONLY the
    // window: the target stays (B460).
    receiver->OnEvent(KeyDownEvent(irr::KEY_ESCAPE, 0));
    Frame();
    EXPECT_TRUE(bridge.sawCloseBank()) << "a real ESC still closes the bank";
    EXPECT_EQ(renderer.getCurrentTargetId(), 42u)
        << "a window-closing press does not clear the target (B460)";
}

}  // namespace Graphics
}  // namespace EQT
