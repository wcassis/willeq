/**
 * T301 Leg D -- hiding the UI, from a path a user (or a capture script) can
 * actually reach.
 *
 * T300 Leg C recorded 285 first-person combat frames with the chat window
 * (bottom-left, ~310x140) and the hotbar (a bottom-centre strip) opaque over
 * every one of them, and had to state as an honest limit that a weapon sliver
 * appearing ONLY under those panels could not be ruled out. Every visual gate
 * since T63 has fought the same overlay.
 *
 * fable was NOT missing the feature, which is the interesting part:
 * IrrlichtRenderer::toggleAllUI() has existed for a long time, bound to F11
 * (HotkeyAction::ToggleAllUI, hotkey_manager.cpp's global defaults). It was
 * broken and unreachable in two separate ways, both closed by this unit:
 *
 *   1. `allUIHidden_` gated only what existed when it was written -- the
 *      entity casting bars, the old `guienv_->drawAll()` and the FPS counter
 *      (renderer_frame.cpp). The STATIC UI block right below them -- chat,
 *      hotbar, gems, buffs, popups, XP bar, i.e. the entire UI the client
 *      actually ships -- was not gated at all, so F11 hid nothing a capture
 *      cared about. (The reachability pattern's cousin: a feature with a
 *      caller, whose effect no longer reaches the thing it names.)
 *   2. A raw key event is not reachable from a `--script` run or from any
 *      harness that does not own an X server. `/hideui` (aliases `/toggleui`,
 *      `/ui`) is the scriptable half, exactly as `/camera` was for camera
 *      modes in T66.
 *
 * WHAT IS PINNED HERE, and by what: the two ends are exercised as production
 * functions on a real IrrlichtRenderer (no device needed -- toggleAllUI()
 * touches one bool, and processSlashCommand()/processCommonInput() are the
 * same functions the bridge queue and the input path call; the idiom is
 * test_irrlicht_camera_command.cpp's). The two steps that live inside
 * device-bound code -- the key-event switch arm and the render-time gate --
 * are pinned by parsing their source, which is what this suite already does
 * for a handful of other single-site invariants; it is stated plainly rather
 * than dressed up as behavioural coverage.
 */
#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "client/graphics/irrlicht_renderer.h"
#include "client/bridge/game_state_bridge.h"
#include "client/state/event_bus.h"
#include "client/input/hotkey_manager.h"

#ifndef WILLEQ_REPO_ROOT
#error "WILLEQ_REPO_ROOT must be defined (tests/CMakeLists.txt)"
#endif

namespace fs = std::filesystem;
using namespace EQT::Graphics;

namespace {

const fs::path kRoot = WILLEQ_REPO_ROOT;

std::string readFile(const fs::path& p) {
    std::ifstream in(p);
    EXPECT_TRUE(in.good()) << "cannot open " << p;
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

class TestBridge : public eqt::bridge::GameStateBridge {
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

class UIHideCommandTest : public ::testing::Test {
protected:
    void SetUp() override { renderer.setBridge(&bridge); }
    IrrlichtRenderer renderer;
    TestBridge bridge;
};

// --- The state it toggles ---

TEST_F(UIHideCommandTest, UIStartsVisible) {
    EXPECT_FALSE(renderer.isAllUIHidden());
}

// --- User path 1: the slash command (a script, a chat box, /help's registry 3) ---

TEST_F(UIHideCommandTest, SlashHideUiTogglesAndReportsBothWays) {
    renderer.processSlashCommand("/hideui");
    EXPECT_TRUE(renderer.isAllUIHidden());
    auto msgs = DrainSystemMessages(bridge);
    ASSERT_EQ(msgs.size(), 1u);
    EXPECT_EQ(msgs[0], "UI: HIDDEN");

    renderer.processSlashCommand("/hideui");
    EXPECT_FALSE(renderer.isAllUIHidden());
    msgs = DrainSystemMessages(bridge);
    ASSERT_EQ(msgs.size(), 1u);
    EXPECT_EQ(msgs[0], "UI: VISIBLE");
}

TEST_F(UIHideCommandTest, AliasesReachTheSameToggle) {
    renderer.processSlashCommand("/toggleui");
    EXPECT_TRUE(renderer.isAllUIHidden());
    renderer.processSlashCommand("/ui");
    EXPECT_FALSE(renderer.isAllUIHidden());
}

// --- User path 2: the F11 key ---

TEST_F(UIHideCommandTest, F11IsBoundToToggleAllUIInEveryMode) {
    auto& hotkeys = eqt::input::HotkeyManager::instance();
    hotkeys.resetToDefaults();
    for (auto mode : {eqt::input::HotkeyMode::Global, eqt::input::HotkeyMode::Player}) {
        auto action = hotkeys.getAction(irr::KEY_F11, false, false, false, mode);
        ASSERT_TRUE(action.has_value()) << "F11 is not bound in this mode";
        EXPECT_EQ(*action, eqt::input::HotkeyAction::ToggleAllUI);
    }
}

TEST_F(UIHideCommandTest, RendererActionFromThatKeyTogglesTheUI) {
    // The queue entry the key-event switch pushes, drained by the exact
    // production consumer (processCommonInput()).
    const std::vector<RendererEvent> actions{RendererEvent(RendererAction::ToggleAllUI)};
    renderer.processCommonInput(actions);
    EXPECT_TRUE(renderer.isAllUIHidden());
    renderer.processCommonInput(actions);
    EXPECT_FALSE(renderer.isAllUIHidden());
}

// --- The two single-site steps that need a GL device to execute ---

TEST(UIHideSourceInvariants, KeyEventSwitchQueuesTheRendererAction) {
    const std::string src = readFile(kRoot / "src/client/graphics/irrlicht_renderer.cpp");
    EXPECT_NE(src.find("case HA::ToggleAllUI: actionQueue_.push_back({RA::ToggleAllUI});"),
              std::string::npos)
        << "the F11 hotkey no longer reaches RendererAction::ToggleAllUI -- the key is dead";
}

TEST(UIHideSourceInvariants, StaticUiBlockIsGatedOnAllUIHidden) {
    // THE DEFECT THIS LEG FIXED. Before T301 this read
    // `if (newUIEnabled_ && uiRenderer_)`, so hiding the UI left the chat
    // panel and the hotbar painted over every captured frame.
    const std::string src = readFile(kRoot / "src/client/graphics/renderer_frame.cpp");
    EXPECT_NE(src.find("if (newUIEnabled_ && uiRenderer_ && !allUIHidden_)"), std::string::npos)
        << "the static UI panels are drawn without consulting allUIHidden_ -- /hideui and F11 "
           "will leave the chat box and hotbar over every frame of a visual gate again";
}

}  // namespace
