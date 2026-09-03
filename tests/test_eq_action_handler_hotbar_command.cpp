// T239 -- real-EverQuest/EqActionHandler coverage for the keyboard hotbar
// Emote / Macro / Combat route below the action layer:
//
//   EqActionHandler::activateHotbarCommandSlot(type, text)
//       -> EverQuest::ActivateHotbarCommandSlot(type, text) (eq_bridge.cpp;
//          the ONE command-slot body, shared with the mouse
//          HotbarActivateIntent arm)
//            Emote/Macro -> ProcessChatInput(text)   (the chat window's entry)
//            Combat      -> CombatManager::ToggleAutoAttack()
//
// Before T239 the keyboard arm was a logged skip for all three types, and on
// the mouse path Macro/Combat were no-ops. Observability: the replay harness
// has no zone connection and no Application, so neither the OP_Emote /
// OP_ChannelMessage bytes nor Application's fallback registries (where /wave
// and /say actually live, action::CommandProcessor) exist here. What IS
// observable, and what the tests pin:
//   - the command text reaches ProcessChatInput's unified dispatch: registry
//     1 misses, so the text (minus its '/') arrives at the fallback hook
//     EverQuest::SetUnrecognizedCommandFallback -- the exact hook Application
//     wires to CommandProcessor -- and, with no fallback installed, the
//     "Unknown command: /wave" system message proves the same reach;
//   - Combat: CombatManager's auto-attack flag and the GameState it writes at
//     the same choke point (T264: the AutoAttackChanged bridge event that
//     used to accompany it was deleted -- nothing consumed it), on then off,
//     and nothing without a target.
// The live leg (refactor/T239_hotbar_emote_macro.md) covers the wire.

#include <gtest/gtest.h>

#include "client/bridge/game_state_bridge.h"
#include "client/combat.h"
#include "client/eq.h"
#include "client/eq_action_handler.h"
#include "zone_stream_test_utils.h"

using namespace zonetest;
using eqt::EqActionHandler;

namespace {

std::string lastSystemMessage(const std::vector<eqt::state::GameEvent>& events) {
    std::string last;
    for (auto& e : events) {
        if (e.type == eqt::state::GameEventType::SystemMessage) {
            last = std::get<eqt::state::ChatMessageData>(e.data).message;
        }
    }
    return last;
}

struct Harness {
    EverQuest eq = MakeEq();
    TestBridge bridge;
    EqActionHandler handler{eq};
    std::vector<std::string> fallbackSeen;  // cmd_line without its leading '/'
    Harness() { eq.setBridge(&bridge); }
    void installFallback() {
        eq.Chat().SetUnrecognizedCommandFallback([this](const std::string& cmdLine) {
            fallbackSeen.push_back(cmdLine);
            return true;
        });
    }
    void spawnTarget(uint16_t id) {
        ReplayZone(eq, HC_OP_NewSpawn, NewSpawnPayloadAt(id, "a_test_rat", 0, 10, 0));
        ASSERT_TRUE(eq.GetCombatManager()->SetTarget(id));
    }
};

} // namespace

TEST(EqActionHandlerHotbarCommand, Emote_TextReachesUnifiedCommandDispatch) {
    Harness h;
    h.installFallback();
    h.bridge.drainEvents();

    h.handler.activateHotbarCommandSlot(eqt::HotbarSlotType::Emote, "/wave");

    EXPECT_EQ(h.fallbackSeen, (std::vector<std::string>{"wave"}));
    EXPECT_EQ(lastSystemMessage(h.bridge.drainEvents()), "");  // handled, no "Unknown command"
}

TEST(EqActionHandlerHotbarCommand, Emote_WithoutFallback_ReachesProcessChatInput) {
    Harness h;
    h.bridge.drainEvents();

    h.handler.activateHotbarCommandSlot(eqt::HotbarSlotType::Emote, "/wave");

    // Registry 1 has no /wave and nothing else is wired: ProcessChatInput's own
    // refusal is the proof the text got there (the pre-T239 keyboard arm
    // produced no event at all).
    EXPECT_EQ(lastSystemMessage(h.bridge.drainEvents()),
              "Unknown command: /wave. Type /help for a list of commands.");
}

TEST(EqActionHandlerHotbarCommand, Macro_SayLineReachesUnifiedCommandDispatch) {
    Harness h;
    h.installFallback();

    h.handler.activateHotbarCommandSlot(eqt::HotbarSlotType::Macro, "/say hi");

    EXPECT_EQ(h.fallbackSeen, (std::vector<std::string>{"say hi"}));
}

TEST(EqActionHandlerHotbarCommand, Macro_EmptyText_RefusesWithMessage) {
    Harness h;
    h.installFallback();
    h.bridge.drainEvents();

    h.handler.activateHotbarCommandSlot(eqt::HotbarSlotType::Macro, "");

    EXPECT_TRUE(h.fallbackSeen.empty());
    EXPECT_EQ(lastSystemMessage(h.bridge.drainEvents()), "Hotbar slot has no command");
}

TEST(EqActionHandlerHotbarCommand, Combat_TogglesAutoAttackOnThenOff) {
    Harness h;
    h.spawnTarget(100);
    h.bridge.drainEvents();

    h.handler.activateHotbarCommandSlot(eqt::HotbarSlotType::Combat, "");
    EXPECT_TRUE(h.eq.GetCombatManager()->IsAutoAttackEnabled());
    EXPECT_TRUE(h.eq.GetGameState().combat().isAutoAttacking());
    h.handler.activateHotbarCommandSlot(eqt::HotbarSlotType::Combat, "");
    EXPECT_FALSE(h.eq.GetCombatManager()->IsAutoAttackEnabled());
    EXPECT_FALSE(h.eq.GetGameState().combat().isAutoAttacking());
}

TEST(EqActionHandlerHotbarCommand, Combat_NoTarget_RefusesWithMessageAndNoToggle) {
    Harness h;
    h.bridge.drainEvents();

    h.handler.activateHotbarCommandSlot(eqt::HotbarSlotType::Combat, "");

    auto events = h.bridge.drainEvents();
    EXPECT_FALSE(h.eq.GetCombatManager()->IsAutoAttackEnabled());
    EXPECT_FALSE(h.eq.GetGameState().combat().isAutoAttacking());
    EXPECT_EQ(lastSystemMessage(events), "You must have a target to attack");
}

TEST(EqActionHandlerHotbarCommand, SharedBody_RefusesNonCommandTypes) {
    Harness h;
    h.installFallback();
    h.bridge.drainEvents();

    EXPECT_FALSE(h.eq.ActivateHotbarCommandSlot(eqt::HotbarSlotType::Spell, "/wave"));
    EXPECT_FALSE(h.eq.ActivateHotbarCommandSlot(eqt::HotbarSlotType::Item, "/wave"));
    EXPECT_FALSE(h.eq.ActivateHotbarCommandSlot(eqt::HotbarSlotType::Empty, "/wave"));

    EXPECT_TRUE(h.fallbackSeen.empty());
    EXPECT_TRUE(h.bridge.drainEvents().empty());
}
