// B460 (GitLab #156, UIGAP-small) -- ESC closes the MOST RECENTLY OPENED
// window, one per press.
//
// MEASURED on the official client (review/UIGAP-ORACLE/156/README.md): with
// the Inventory, its Tip window and a bag open (opened in that order), Escape
// #1 closed the tip, #2 the bag, #3 the inventory, #4 and #5 nothing. Before
// this fable walked a FIXED list (options -> friends -> item display ->
// containers -> tracking -> trainer -> bank -> merchant -> centre popups), so
// e.g. Options opened first and a bag second closed Options first.
//
// Two layers:
//   * EscWindowStack, device-free: open order, close, same-frame ties.
//   * FrameComponent::handleEscapeKey() on a device-less IrrlichtRenderer
//     (the test_debug_hotkey_arms idiom), driven through an OPENED-ORDER
//     sequence, checking what each press closed or asked the server to close.
//
// Whether the official client's first press ALSO clears the target while it
// closes a window is not separated by the oracle run; fable keeps a
// window-closing press away from the target, and the last case pins that as
// the assumption it is.

#include <gtest/gtest.h>

#include <variant>
#include <vector>

#include "client/bridge/game_state_bridge.h"
#include "client/events/renderer_intents.h"
#include "client/graphics/irrlicht_renderer.h"
#include "client/graphics/frame/frame_component.h"
#include "client/graphics/overlays/overlays_component.h"
#include "client/graphics/ui/esc_window_stack.h"
#include "client/graphics/ui/static_ui_input.h"

using namespace EQT::Graphics;

namespace {

EscWindowKey K(EscWindow w, int sub = 0) { return EscWindowKey{w, sub}; }

class CapturingBridge : public eqt::bridge::GameStateBridge {
public:
    void applyEvent(const eqt::state::GameEvent&) override {}
};

}  // namespace

// ---------------------------------------------------------------------------
// The stack itself
// ---------------------------------------------------------------------------

TEST(EscWindowStack, NewestIsTheLastOpenedNotTheTopOfTheDraw) {
    EscWindowStack s;
    s.observe({K(EscWindow::Options)});
    s.observe({K(EscWindow::Inventory), K(EscWindow::Options)});
    ASSERT_TRUE(s.newest().has_value());
    EXPECT_EQ(s.newest()->kind, EscWindow::Inventory)
        << "the inventory was opened after Options, so it goes first";
}

TEST(EscWindowStack, AClosedWindowLeavesAndAReopenedOneIsNewAgain) {
    EscWindowStack s;
    s.observe({K(EscWindow::Inventory)});
    s.observe({K(EscWindow::Inventory), K(EscWindow::Bank)});
    s.observe({K(EscWindow::Bank)});  // inventory closed
    EXPECT_EQ(s.newest()->kind, EscWindow::Bank);
    s.observe({K(EscWindow::Inventory), K(EscWindow::Bank)});  // reopened
    EXPECT_EQ(s.newest()->kind, EscWindow::Inventory);
    s.observe({});
    EXPECT_FALSE(s.newest().has_value());
}

TEST(EscWindowStack, WindowsOpenedInOneFrameAreOrderedAsListed) {
    EscWindowStack s;
    s.observe({K(EscWindow::Bank), K(EscWindow::Container, 2), K(EscWindow::Options)});
    EXPECT_EQ(s.newest()->kind, EscWindow::Options);
    s.observe({K(EscWindow::Bank), K(EscWindow::Container, 2)});
    EXPECT_EQ(s.newest()->kind, EscWindow::Container);
    s.observe({K(EscWindow::Bank)});
    EXPECT_EQ(s.newest()->kind, EscWindow::Bank);
}

TEST(EscWindowStack, TwoBagsAreTwoEntries) {
    EscWindowStack s;
    s.observe({K(EscWindow::Container, 1)});
    s.observe({K(EscWindow::Container, 1), K(EscWindow::Container, 3)});
    EXPECT_EQ(s.newest()->sub, 3);
    s.observe({K(EscWindow::Container, 1)});
    EXPECT_EQ(s.newest()->sub, 1);
}

// ---------------------------------------------------------------------------
// FrameComponent::handleEscapeKey(), an opened-order sequence
// ---------------------------------------------------------------------------

class EscPressTest : public ::testing::Test {
protected:
    IrrlichtRenderer renderer;
    CapturingBridge bridge;

    FrameComponent& F() { return renderer.Frame(); }

    void SetUp() override {
        renderer.Context().bridge = &bridge;
        F().staticUIInput_ = std::make_unique<StaticUIInput>();
        F().staticUIInput_->init(&bridge);
    }

    // One frame: the per-frame observation production runs.
    void frame() { F().observeEscWindows(); }

    void openBag(int idx) {
        auto& w = F().containerState_.windows[idx];
        w.isOpen = true;
        w.inventorySlot = static_cast<int16_t>(22 + idx - 1);
        w.openSequence = F().containerState_.nextSequence++;
    }

    std::vector<eqt::events::RendererIntent> drain() { return bridge.drainIntents(); }
};

// The oracle's own sequence, minus the tip window (#202 adds it): inventory,
// then a bag. Press 1 asks the server to close the bag (it is server-owned
// and stays up until TradeskillContainerClosed), the server closes it, press 2
// closes the inventory, press 3 does nothing.
TEST_F(EscPressTest, TheMeasuredSequenceInventoryThenBag) {
    F().inventoryState_.activePopup = PopupType::Inventory;
    frame();
    openBag(1);
    frame();

    EXPECT_TRUE(F().handleEscapeKey());
    auto intents = drain();
    ASSERT_EQ(intents.size(), 1u);
    const auto* close = std::get_if<eqt::events::TradeskillCloseIntent>(&intents[0]);
    ASSERT_NE(close, nullptr) << "press 1 must address the bag, the newest window";
    EXPECT_EQ(F().inventoryState_.activePopup, PopupType::Inventory);

    F().containerState_.windows[1].isOpen = false;  // the server's Closed event
    frame();

    EXPECT_TRUE(F().handleEscapeKey());
    EXPECT_EQ(F().inventoryState_.activePopup, PopupType::None);
    EXPECT_TRUE(drain().empty());

    EXPECT_FALSE(F().handleEscapeKey()) << "press 3: nothing left";
}

// The fixed list closed Options before anything; the stack closes whatever
// came last.
TEST_F(EscPressTest, OptionsOpenedFirstClosesLast) {
    F().optionsState_.isOpen = true;
    frame();
    F().skillsPopupState_.isOpen = true;
    frame();
    F().trackingState_.isOpen = true;
    frame();

    EXPECT_TRUE(F().handleEscapeKey());
    EXPECT_FALSE(F().trackingState_.isOpen);
    EXPECT_TRUE(F().skillsPopupState_.isOpen);
    EXPECT_TRUE(F().optionsState_.isOpen);

    EXPECT_TRUE(F().handleEscapeKey());
    EXPECT_FALSE(F().skillsPopupState_.isOpen);
    EXPECT_TRUE(F().optionsState_.isOpen);

    EXPECT_TRUE(F().handleEscapeKey());
    EXPECT_FALSE(F().optionsState_.isOpen);
}

// Two pure client views closed in one frame: the second press must not be
// swallowed by the window the first one already closed.
TEST_F(EscPressTest, TwoPressesInOneFrameCloseTwoWindows) {
    F().itemDisplayState_.isOpen = true;
    frame();
    F().friendsState_.isOpen = true;
    frame();
    EXPECT_TRUE(F().handleEscapeKey());
    EXPECT_TRUE(F().handleEscapeKey());
    EXPECT_FALSE(F().friendsState_.isOpen);
    EXPECT_FALSE(F().itemDisplayState_.isOpen);
}

// The server-owned windows keep T290's rule: the intent, never a local hide.
TEST_F(EscPressTest, BankOpenedAfterTheMerchantGetsTheCloseIntentFirst) {
    F().merchantState_.isOpen = true;
    frame();
    F().bankState_.isOpen = true;
    frame();
    EXPECT_TRUE(F().handleEscapeKey());
    auto intents = drain();
    ASSERT_EQ(intents.size(), 1u);
    EXPECT_NE(std::get_if<eqt::events::CloseBankIntent>(&intents[0]), nullptr);
    EXPECT_TRUE(F().bankState_.isOpen);

    F().bankState_.isOpen = false;  // BankWindowClosed
    frame();
    EXPECT_TRUE(F().handleEscapeKey());
    intents = drain();
    ASSERT_EQ(intents.size(), 1u);
    EXPECT_NE(std::get_if<eqt::events::VendorToggleIntent>(&intents[0]), nullptr);
}

// A window opened in the very frame ESC arrives is still seen (the handler
// observes before it decides).
TEST_F(EscPressTest, AWindowOpenedThisFrameIsSeen) {
    F().spellbookState_.isOpen = true;
    frame();
    F().optionsState_.isOpen = true;  // no frame() in between
    EXPECT_TRUE(F().handleEscapeKey());
    EXPECT_FALSE(F().optionsState_.isOpen);
    EXPECT_TRUE(F().spellbookState_.isOpen);
}

// ASSUMPTION AWAITING ORACLE: a window-closing press leaves the target; with
// nothing open, the press clears it.
TEST_F(EscPressTest, AWindowClosingPressLeavesTheTargetAndTheNextClearsIt) {
    renderer.Overlays().setCurrentTarget(321, "a_bat");
    F().inventoryState_.activePopup = PopupType::Inventory;
    frame();
    EXPECT_TRUE(F().handleEscapeKey());
    EXPECT_EQ(renderer.Overlays().currentTargetId(), 321);
    EXPECT_FALSE(F().handleEscapeKey());
    EXPECT_EQ(renderer.Overlays().currentTargetId(), 0);
}
