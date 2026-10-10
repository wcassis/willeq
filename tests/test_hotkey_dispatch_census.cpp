/**
 * B905 (GitLab #225, UIGAP-small): every default key binding reaches a
 * dispatch consumer.
 *
 * MR !34 (B562) made config/hotkeys.json and setupDefaults() agree, and in
 * doing so put eight Global bindings in the compiled-in table that nothing
 * consumed: ToggleUILock (Ctrl+L), SaveUILayout (Ctrl+S), ResetUIDefaults
 * (Ctrl+Shift+R), ConfirmDialog (Return, Y), CancelDialog (Escape, N),
 * SubmitInput (Return), CancelInput (Escape) and ChatAutocomplete (Tab). They
 * fell through RendererEventReceiver::OnEvent()'s `default: break;` -- and,
 * worse, because getAction() resolves Global before Player they SHADOWED the
 * real Player bindings on the same keys: Tab never cycled targets. Ctrl+Q
 * (Attack) had the same no-arm shape. B905 deleted the eight (none is on the
 * oracle's measured Options > Keys pages, review/T377/oracle/) and gave Attack
 * its arm.
 *
 * What this pins, for EVERY binding in BOTH tables (the compiled-in defaults
 * and the shipped file, through the exact load sequence the client runs):
 *
 *   1. The binding is not shadowed: getAction() on its own key returns its own
 *      action. (B582: the former exemption -- F11 MusicVolumeUp, the
 *      B561 double binding that test_hotkey_manager.cpp's T464 test carried --
 *      is gone; the official keymap binds no volume key, so the defaults have
 *      none to shadow.)
 *   2. The binding has a consumer, measured behaviourally on a fresh
 *      RendererEventReceiver: pressing the key leaves an OBSERVABLE request on
 *      one of the receiver's outputs (renderer action queue, bridge queue,
 *      quit flag, gem/hotbar request, zoom or volume accumulator). Two
 *      explicit exceptions, each naming its consumer:
 *        - the held-key actions, which the receiver does not decode at all:
 *          PlayerCameraComponent polls them every frame through
 *          isActionHeld() in renderer_player_physics.cpp -- read from that
 *          file, so a held action can only be exempted by being polled;
 *        - three "latch" consumers: OpenChat (the Enter latch),
 *          OpenChatSlash (the slash latch) and ClearTarget on ESCAPE (the
 *          Escape latch -> FrameComponent's ESC window stack, which clears
 *          the target only when no window took the press). For ClearTarget on
 *          Escape the bridge queue must stay EMPTY, which pins that ESC does
 *          not drop the target on a window-closing press.
 *
 * A new binding whose action has no arm fails (2); a new Global binding that
 * steals a Player key fails (1).
 */
#include <gtest/gtest.h>

#include <fstream>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "client/graphics/renderer_event_receiver.h"
#include "client/input/hotkey_manager.h"

#ifndef WILLEQ_REPO_ROOT
#error "WILLEQ_REPO_ROOT must be defined (tests/CMakeLists.txt)"
#endif

using eqt::input::HotkeyAction;
using eqt::input::HotkeyBinding;
using eqt::input::HotkeyManager;
using eqt::input::HotkeyMode;
using eqt::input::ModifierFlags;

namespace {

std::string ReadRepoFile(const std::string& rel) {
    std::ifstream in(std::string(WILLEQ_REPO_ROOT) + "/" + rel);
    EXPECT_TRUE(in.good()) << "cannot open " << rel;
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

// The held-key consumers, read from the one place that polls them.
std::set<std::string> PolledHeldActions() {
    const std::string src =
        ReadRepoFile("src/client/graphics/player/renderer_player_physics.cpp");
    std::set<std::string> out;
    const std::regex re(R"(isActionHeld\(eqt::input::HotkeyAction::(\w+)\))");
    for (auto it = std::sregex_iterator(src.begin(), src.end(), re);
         it != std::sregex_iterator(); ++it) {
        out.insert((*it)[1].str());
    }
    return out;
}

std::vector<HotkeyBinding> AllBindings(const HotkeyManager& mgr) {
    std::vector<HotkeyBinding> out;
    for (uint32_t i = 0; i < static_cast<uint32_t>(HotkeyAction::Count); ++i) {
        for (const auto& b : mgr.getBindingsForAction(static_cast<HotkeyAction>(i))) {
            out.push_back(b);
        }
    }
    return out;
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

struct PressResult {
    bool rendererAction = false;
    bool bridgeAction = false;
    bool other = false;  // quit, gem, hotbar, zoom, volume
    bool enterLatch = false;
    bool slashLatch = false;
    bool escapeLatch = false;
};

PressResult Press(const HotkeyBinding& b) {
    EQT::Graphics::RendererEventReceiver r;
    const bool ctrl = eqt::input::hasModifier(b.modifiers, ModifierFlags::Ctrl);
    const bool shift = eqt::input::hasModifier(b.modifiers, ModifierFlags::Shift);
    const bool alt = eqt::input::hasModifier(b.modifiers, ModifierFlags::Alt);
    if (alt) {
        // OnEvent reads Alt from the held LMENU (Irrlicht's KeyInput has no
        // Alt field); LMENU itself is unbound.
        r.OnEvent(KeyEvent(irr::KEY_LMENU, true, false, false));
        r.drainActions();
        r.enterKeyPressed();
        r.slashKeyPressed();
        r.escapeKeyPressed();
    }
    r.OnEvent(KeyEvent(b.keyCode, true, ctrl, shift));
    PressResult p;
    p.rendererAction = !r.drainActions().empty();
    p.bridgeAction = r.getBridgeQueueSize() > 0;
    p.other = r.quitRequested() || r.getSpellGemCastRequest() != -1 ||
              r.getHotbarActivationRequest() != -1 || r.getCameraZoomDelta() != 0.0f ||
              r.getMusicVolumeStepDelta() != 0 || r.getEffectsVolumeStepDelta() != 0;
    p.enterLatch = r.enterKeyPressed();
    p.slashLatch = r.slashKeyPressed();
    p.escapeLatch = r.escapeKeyPressed();
    return p;
}

void CheckEveryBindingIsConsumed(const char* table) {
    auto& mgr = HotkeyManager::instance();
    const std::set<std::string> held = PolledHeldActions();
    ASSERT_GE(held.size(), 9u) << "the held-key poll moved out of renderer_player_physics.cpp";

    const std::vector<HotkeyBinding> bindings = AllBindings(mgr);
    ASSERT_GT(bindings.size(), 50u);
    for (const auto& b : bindings) {
        const std::string name = HotkeyManager::actionEnumToName(b.action);
        const std::string where = std::string(table) + ": " + name + " = " +
                                  HotkeyManager::bindingToString(b);

        // (1) not shadowed.
        const auto resolved = mgr.getAction(
            b.keyCode, eqt::input::hasModifier(b.modifiers, ModifierFlags::Ctrl),
            eqt::input::hasModifier(b.modifiers, ModifierFlags::Shift),
            eqt::input::hasModifier(b.modifiers, ModifierFlags::Alt), HotkeyMode::Player);
        ASSERT_TRUE(resolved.has_value()) << where;
        EXPECT_EQ(*resolved, b.action)
            << where << " is SHADOWED by "
            << HotkeyManager::actionEnumToName(*resolved) << " -- the key never reaches it";

        // (2) a consumer.
        if (held.count(name)) continue;  // polled every frame
        const PressResult p = Press(b);
        if (b.action == HotkeyAction::OpenChat) {
            EXPECT_TRUE(p.enterLatch) << where;
            continue;
        }
        if (b.action == HotkeyAction::OpenChatSlash) {
            EXPECT_TRUE(p.slashLatch) << where;
            continue;
        }
        if (b.action == HotkeyAction::ClearTarget && b.keyCode == irr::KEY_ESCAPE) {
            EXPECT_TRUE(p.escapeLatch) << where;
            EXPECT_FALSE(p.bridgeAction || p.rendererAction)
                << where << ": ESC must reach the window stack, not clear the target outright";
            continue;
        }
        EXPECT_TRUE(p.rendererAction || p.bridgeAction || p.other)
            << where << " has NO dispatch consumer: the key falls through "
                        "RendererEventReceiver::OnEvent()'s default";
    }
}

}  // namespace

TEST(HotkeyDispatchCensus, EveryDefaultBindingHasADispatchConsumer) {
    HotkeyManager::instance().resetToDefaults();
    CheckEveryBindingIsConsumed("defaults");
}

TEST(HotkeyDispatchCensus, EveryShippedBindingHasADispatchConsumer) {
    auto& mgr = HotkeyManager::instance();
    mgr.resetToDefaults();
    ASSERT_TRUE(mgr.loadFromFile(std::string(WILLEQ_REPO_ROOT) + "/config/hotkeys.json"));
    // B582: the shipped file carries F10's ToggleAllUI meaning; the pre-B582
    // F11 double binding (B561) and the volume rows are gone from both tables.
    CheckEveryBindingIsConsumed("config/hotkeys.json");
    mgr.resetToDefaults();
}

// The defect that made this a live bug: Tab, Escape and Return resolved to a
// Global action nobody consumed.
TEST(HotkeyDispatchCensus, TabCyclesTargetsAndReturnOpensChat) {
    auto& mgr = HotkeyManager::instance();
    mgr.resetToDefaults();
    ASSERT_TRUE(mgr.loadFromFile(std::string(WILLEQ_REPO_ROOT) + "/config/hotkeys.json"));
    EXPECT_EQ(mgr.getAction(irr::KEY_TAB, false, false, false, HotkeyMode::Player),
              std::optional<HotkeyAction>(HotkeyAction::CycleTargets));
    EXPECT_EQ(mgr.getAction(irr::KEY_RETURN, false, false, false, HotkeyMode::Player),
              std::optional<HotkeyAction>(HotkeyAction::OpenChat));
    EXPECT_EQ(mgr.getAction(irr::KEY_ESCAPE, false, false, false, HotkeyMode::Player),
              std::optional<HotkeyAction>(HotkeyAction::ClearTarget));
    // Ctrl+S is the oracle's Sit/stand (review/T377/oracle/
    // T377_oracle_options_keys_Commands_page1.png); fable binds nothing there now.
    EXPECT_FALSE(mgr.getAction(irr::KEY_KEY_S, true, false, false, HotkeyMode::Player).has_value());
    mgr.resetToDefaults();
}

// A user copy saved before B905 still names the deleted actions: they load
// as nothing, and every other binding in the file still loads.
TEST(HotkeyDispatchCensus, ARetiredActionNameInASavedFileIsIgnored) {
    const std::string path = ::testing::TempDir() + "b905_retired_hotkeys.json";
    {
        std::ofstream out(path);
        out << R"({"version":1,"bindings":{"global":{"ChatAutocomplete":"Tab",)"
               R"("CancelDialog":["Escape","N"]},"player":{"CycleTargets":"Tab"}}})";
    }
    auto& mgr = HotkeyManager::instance();
    ASSERT_TRUE(mgr.loadFromFile(path));
    EXPECT_EQ(mgr.getAction(irr::KEY_TAB, false, false, false, HotkeyMode::Player),
              std::optional<HotkeyAction>(HotkeyAction::CycleTargets));
    EXPECT_FALSE(mgr.getAction(irr::KEY_KEY_N, false, false, false, HotkeyMode::Player).has_value());
    EXPECT_EQ(HotkeyManager::actionNameToEnum("ChatAutocomplete"), HotkeyAction::Count);
    mgr.resetToDefaults();
}
