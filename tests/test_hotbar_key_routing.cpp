/**
 * B1025-H4 (#345) -- the hotbar window's page and toggle keys.
 *
 * H-O measured the official defaults on the stock client (kb-h0-hotbar-
 * oracle pages.tsv:2, still 27_keys_hb1.png): Hotbar 1's buttons are 1-0,
 * its ten pages are Shift+1 through Shift+0, and "Toggle Hotbar 1 window"
 * (eqstr 6587) is Alt+H. Hotbars 2-4 are unbound (pages.tsv:3-6) and the
 * stock client cannot show them (EQUI_HotButtonWnd.xml gives HotButtonWnd2-
 * 4 zero size), so the model is one window x 10 pages x 10 buttons and the
 * key set is one toggle only.
 *
 * What this pins (the census, test_hotkey_dispatch_census.cpp, only proves a
 * press leaves SOME observable output; these pin WHICH one, end to end):
 *   - On a real RendererEventReceiver with the SHIPPED config/hotkeys.json
 *     (the exact load sequence the client runs): Shift+<digit> ->
 *     RendererAction::HotbarPageChange with the right 0-9 page in intData
 *     (Shift+3 -> page 2, Shift+0 -> page 9); Alt+H ->
 *     RendererAction::ToggleHotbar1Window on the RENDERER action queue; and
 *     the plain 1-0 keys still produce the slot request, unchanged (H-4's
 *     activation keeps firing slot N of the current page).
 *   - Through the production consumer, FrameComponent::processPlayerInput
 *     on a bare renderer (the T343 idiom): the page action pushes the SAME
 *     HotbarPageChangedIntent the page buttons push (the model owns the
 *     page; its republish is what the panel redraws), out of range and
 *     negative pages are clamped away, and the toggle action flips
 *     hotbarState_.isOpen on and back off.
 *   - The Options Keys page lists all eleven new rows under "Hot Buttons"
 *     with their shipped bindings, so the user can see and rebind them.
 */
#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <variant>
#include <vector>

#include "client/bridge/game_state_bridge.h"
#include "client/graphics/frame/frame_component.h"
#include "client/graphics/irrlicht_renderer.h"
#include "client/events/renderer_intents.h"
#include "client/graphics/renderer_event_receiver.h"
#include "client/graphics/ui/options_keys.h"
#include "client/input/hotkey_manager.h"

#ifndef WILLEQ_REPO_ROOT
#error "WILLEQ_REPO_ROOT must be defined (tests/CMakeLists.txt)"
#endif

using namespace EQT::Graphics;
using namespace eqt::input;

namespace {

std::string ReadRepoFile(const std::string& rel) {
    std::ifstream in(std::string(WILLEQ_REPO_ROOT) + "/" + rel);
    EXPECT_TRUE(in.good()) << "cannot open " << rel;
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

irr::SEvent KeyEvent(irr::EKEY_CODE key, bool down, bool ctrl, bool shift) {
    irr::SEvent ev{};
    ev.EventType = irr::EET_KEY_INPUT_EVENT;
    ev.KeyInput.Key = key;
    ev.KeyInput.PressedDown = down;
    ev.KeyInput.Control = ctrl;
    ev.KeyInput.Shift = shift;
    ev.KeyInput.Char = 0;
    return ev;
}

// The receiver's OnEvent resolves the action through HotkeyManager's
// singleton, so load the SHIPPED file the way Application::loadConfigFile()
// does (loadFromFile REPLACES the defaults -- config/hotkeys.json IS the
// production table; T464 pins it equal to setupDefaults()).
void LoadShippedBindings() {
    auto& mgr = HotkeyManager::instance();
    mgr.resetToDefaults();
    ASSERT_TRUE(mgr.loadFromFile(std::string(WILLEQ_REPO_ROOT) + "/config/hotkeys.json"));
}

void PressWithAlt(RendererEventReceiver& r, irr::EKEY_CODE key) {
    // OnEvent reads Alt from the held LMENU (Irrlicht's KeyInput has no
    // Alt field).
    r.OnEvent(KeyEvent(irr::KEY_LMENU, true, false, false));
    r.drainActions();
    r.enterKeyPressed();
    r.slashKeyPressed();
    r.escapeKeyPressed();
    r.OnEvent(KeyEvent(key, true, false, false));
}

class HotbarKeyRoutingTest : public ::testing::Test {
protected:
    void SetUp() override {
        LoadShippedBindings();
        receiver = std::make_unique<RendererEventReceiver>();
    }

    std::unique_ptr<RendererEventReceiver> receiver;
};

// ---------------------------------------------------------------------
// The receiver side: the SHIPPED table resolves to the right outputs.
// ---------------------------------------------------------------------

TEST_F(HotbarKeyRoutingTest, PageKeysCarryTheirPageOnTheRendererActionQueue) {
    const std::pair<irr::EKEY_CODE, int> expected[] = {
        {irr::KEY_KEY_1, 0}, {irr::KEY_KEY_2, 1}, {irr::KEY_KEY_3, 2},
        {irr::KEY_KEY_4, 3}, {irr::KEY_KEY_5, 4}, {irr::KEY_KEY_6, 5},
        {irr::KEY_KEY_7, 6}, {irr::KEY_KEY_8, 7}, {irr::KEY_KEY_9, 8},
        {irr::KEY_KEY_0, 9},
    };
    for (const auto& [key, page] : expected) {
        receiver->OnEvent(KeyEvent(key, true, false, true));  // Shift+<digit>
        const auto actions = receiver->drainActions();
        ASSERT_EQ(actions.size(), 1u) << "Shift+" << key;
        EXPECT_EQ(actions[0].action, RendererAction::HotbarPageChange);
        EXPECT_EQ(actions[0].intData, page) << "Shift+<digit> -> wrong page";
        // The page key must not fire a slot of its own.
        EXPECT_EQ(receiver->getHotbarActivationRequest(), -1);
    }
}

TEST_F(HotbarKeyRoutingTest, AltHTogglesTheHotbarWindowOnTheRendererActionQueue) {
    PressWithAlt(*receiver, irr::KEY_KEY_H);
    const auto actions = receiver->drainActions();
    ASSERT_EQ(actions.size(), 1u);
    EXPECT_EQ(actions[0].action, RendererAction::ToggleHotbar1Window)
        << "Alt+H must reach the window toggle (H-O: pages.tsv:2, 27_keys_hb1.png)";
    // The toggle is a renderer action, not a game-thread bridge action.
    EXPECT_EQ(receiver->getBridgeQueueSize(), 0u);
    EXPECT_EQ(receiver->getHotbarActivationRequest(), -1);
}

TEST_F(HotbarKeyRoutingTest, PlainDigitKeysStillActivateSlotsOfTheCurrentPage) {
    receiver->OnEvent(KeyEvent(irr::KEY_KEY_3, true, false, false));
    const auto actions = receiver->drainActions();
    EXPECT_EQ(actions.size(), 0u) << "a bare digit must not queue a renderer action";
    EXPECT_EQ(receiver->getHotbarActivationRequest(), 2)
        << "H-4 kept slot keys as the slot request (0-based); the page is the "
           "model's, read by the game thread's one HotbarModel";
}

// ---------------------------------------------------------------------
// The consumer side: processPlayerInput on a bare renderer.
// ---------------------------------------------------------------------

// The one intent carrier the consumer writes to: a concrete GameStateBridge
// with an empty game-thread side; the renderer side (drainIntents) is the
// exact shape a pump reads.
class IntentSink : public eqt::bridge::GameStateBridge {
public:
    void applyEvent(const eqt::state::GameEvent&) override {}
};

std::vector<RendererEvent> One(RendererAction a, int8_t d = -1) {
    return std::vector<RendererEvent>{RendererEvent(a, d)};
}

TEST_F(HotbarKeyRoutingTest, ThePageActionPushesTheSameIntentThePageButtonsPush) {
    IrrlichtRenderer renderer;
    IntentSink sink;
    renderer.Context().bridge = &sink;

    renderer.Frame().processPlayerInput(One(RendererAction::HotbarPageChange, 2));
    {
        auto intents = sink.drainIntents();
        ASSERT_EQ(intents.size(), 1u);
        EXPECT_TRUE(std::holds_alternative<eqt::events::HotbarPageChangedIntent>(intents[0]));
        EXPECT_EQ(std::get<eqt::events::HotbarPageChangedIntent>(intents[0]).page, 2);
    }

    // The consumer clamps: an out-of-range intData pushes nothing.
    renderer.Frame().processPlayerInput(One(RendererAction::HotbarPageChange, 9));
    {
        auto intents = sink.drainIntents();
        ASSERT_EQ(intents.size(), 1u);
        EXPECT_EQ(std::get<eqt::events::HotbarPageChangedIntent>(intents[0]).page, 9);
    }
    renderer.Frame().processPlayerInput(One(RendererAction::HotbarPageChange, 10));
    renderer.Frame().processPlayerInput(One(RendererAction::HotbarPageChange, -1));
    EXPECT_TRUE(sink.drainIntents().empty()) << "out-of-range pages must be clamped away";
}

TEST_F(HotbarKeyRoutingTest, TheToggleActionFlipsTheHotbarWindowOnAndOff) {
    IrrlichtRenderer renderer;
    renderer.Context().bridge = nullptr;
    auto& hb = renderer.Frame().hotbarState_;
    ASSERT_TRUE(hb.isOpen);  // the oracle's window is shown by default

    renderer.Frame().processPlayerInput(One(RendererAction::ToggleHotbar1Window));
    EXPECT_FALSE(hb.isOpen)
        << "Alt+H (H-O's measured default) must hide the window -- the arm is missing";
    renderer.Frame().processPlayerInput(One(RendererAction::ToggleHotbar1Window));
    EXPECT_TRUE(hb.isOpen) << "the toggle is a flip: the second press reopens it";
    // The toggle disturbs nothing beside the window's flag.
    EXPECT_EQ(hb.currentPage, 0);
    EXPECT_EQ(hb.hoveredSlot, -1);
}

// ---------------------------------------------------------------------
// The Options Keys page: the new rows exist with their shipped bindings.
// ---------------------------------------------------------------------

TEST_F(HotbarKeyRoutingTest, TheOptionsKeysPageListsThePageAndToggleRows) {
    LoadShippedBindings();
    using HA = HotkeyAction;
    const std::pair<HA, const char*> rows[] = {
        {HA::HotbarPage1, "Shift+1"},   {HA::HotbarPage2, "Shift+2"},
        {HA::HotbarPage3, "Shift+3"},   {HA::HotbarPage4, "Shift+4"},
        {HA::HotbarPage5, "Shift+5"},   {HA::HotbarPage6, "Shift+6"},
        {HA::HotbarPage7, "Shift+7"},   {HA::HotbarPage8, "Shift+8"},
        {HA::HotbarPage9, "Shift+9"},   {HA::HotbarPage10, "Shift+0"},
        {HA::ToggleHotbar1Window, "Alt+H"},
    };
    // All eleven live in the one "Hot Buttons" category (the oracle's
    // "Hotbar 1" page: buttons, pages and the toggle on one page).
    int category = -1;
    for (int c = 0; c < OptionsKeys::kCategoryCount; ++c) {
        if (std::strcmp(OptionsKeys::categoryName(c), "Hot Buttons") == 0) category = c;
    }
    ASSERT_GE(category, 0);
    const auto& actions = OptionsKeys::actionsIn(category);
    for (const auto& [action, binding] : rows) {
        const bool listed = std::find(actions.begin(), actions.end(), action) != actions.end();
        EXPECT_TRUE(listed) << "Options Keys is missing the row for "
                            << HotkeyManager::actionEnumToName(action);
        EXPECT_EQ(OptionsKeys::bindingText(action, 0), binding)
            << "the shipped binding must show on the page for "
            << HotkeyManager::actionEnumToName(action);
    }
    // The row text is derived from the enum name the same way every other
    // row's is (camelCase split -- see OptionsKeys::commandText).
    EXPECT_EQ(OptionsKeys::commandText(HA::HotbarPage1), "Hotbar page 1");
    EXPECT_EQ(OptionsKeys::commandText(HA::ToggleHotbar1Window),
              "Toggle hotbar 1 window");
}

}  // namespace
