// T28 — coverage for the four eq_commands.cpp readers repointed from the
// never-populated state-layer CombatState target to the authoritative
// CombatManager (refactor/T28_state_layer_readers.md Part 2;
// refactor/deferred.md "Dead parallel state copies in the state layer, WITH
// LIVE READERS"). Before this unit, all four ("/assist", "/shield",
// "/duel", "/bandage") read m_game_state.combat().targetId(), which nothing
// in production ever sets, so they always behaved as if there were no
// target -- even with a real target selected via CombatManager. This file
// asserts the corrected before/after: no target -> the "no target"
// message/self-target fallback; a real CombatManager target set ->
// commands see it.
//
// Seam: same as T23's test_combat_manager.cpp -- a bare
// EverQuest("127.0.0.1", 0, ...) via MakeEq() (zone_stream_test_utils.h)
// gives a live, unconnected EverQuest with a real CommandRegistry
// (constructed unconditionally in the EverQuest ctor) and a real
// CombatManager (eq.GetCombatManager()). ProcessChatInput() is the same
// public entry point production chat input uses; it strips the leading '/'
// and dispatches through m_command_registry, reaching the exact lambda
// handlers this unit modified in eq_commands.cpp. SendAssist/SendShielding/
// SendDuelRequest/SendBindWound all no-op safely when m_zone_connection is
// null (verified by reading eq_player_info.cpp), so no network is needed --
// the AddChatSystemMessage side effect (captured via the bridge's
// SystemMessage event, same idiom as test_zone_chat.cpp's DrainChat) and
// CombatManager state are what's asserted.

#include <gtest/gtest.h>

#include "client/bridge/game_state_bridge.h"
#include "client/combat.h"
#include "zone_stream_test_utils.h"

using namespace zonetest;

namespace {

// Drains the bridge and returns SystemMessage payloads' text only.
std::vector<std::string> DrainSystemMessages(eqt::bridge::GameStateBridge& bridge) {
    std::vector<std::string> out;
    for (auto& ev : bridge.drainEvents()) {
        if (ev.type == eqt::state::GameEventType::SystemMessage) {
            out.push_back(std::get<eqt::state::ChatMessageData>(ev.data).message);
        }
    }
    return out;
}

class EqCommandsAssistTest : public ::testing::Test {
protected:
    EqCommandsAssistTest() : eq(MakeEq()) { eq.setBridge(&bridge); }
    ~EqCommandsAssistTest() override { eq.setBridge(nullptr); }

    EverQuest eq;
    TestBridge bridge;
};

TEST_F(EqCommandsAssistTest, AssistNoArgs_NoTarget_PrintsNoTargetMessage) {
    // No target ever set on CombatManager -- before T28 this was also true
    // via a different mechanism (CombatState's dead target copy), so this
    // half of the behavior is unchanged.
    eq.ProcessChatInput("/assist");

    auto msgs = DrainSystemMessages(bridge);
    ASSERT_EQ(msgs.size(), 1u);
    EXPECT_EQ(msgs[0], "You don't have a target.");
}

TEST_F(EqCommandsAssistTest, AssistNoArgs_WithCombatManagerTarget_DoesNotPrintNoTargetMessage) {
    // This is the corrected behavior: before T28, /assist read the
    // never-populated CombatState target and ALWAYS printed "You don't have
    // a target." here, even with a real target selected. It no longer does.
    ASSERT_NO_THROW(ReplayZone(eq, HC_OP_NewSpawn,
        NewSpawnPayloadAt(100, "a_test_rat", 0, 10, 0)));
    auto* cm = eq.GetCombatManager();
    ASSERT_TRUE(cm->SetTarget(100));
    ASSERT_TRUE(cm->HasTarget());

    eq.ProcessChatInput("/assist");

    auto msgs = DrainSystemMessages(bridge);
    EXPECT_TRUE(msgs.empty())
        << "T28 regression: /assist should not claim there is no target "
           "when CombatManager has one; got: "
        << (msgs.empty() ? std::string() : msgs[0]);
}

TEST_F(EqCommandsAssistTest, ShieldNoTarget_PrintsShieldSpecificNoTargetMessage) {
    eq.ProcessChatInput("/shield");

    auto msgs = DrainSystemMessages(bridge);
    ASSERT_EQ(msgs.size(), 1u);
    EXPECT_EQ(msgs[0], "You don't have a target to shield.");
}

TEST_F(EqCommandsAssistTest, ShieldWithCombatManagerTarget_DoesNotPrintNoTargetMessage) {
    ASSERT_NO_THROW(ReplayZone(eq, HC_OP_NewSpawn,
        NewSpawnPayloadAt(100, "a_test_rat", 0, 10, 0)));
    auto* cm = eq.GetCombatManager();
    ASSERT_TRUE(cm->SetTarget(100));

    eq.ProcessChatInput("/shield");

    auto msgs = DrainSystemMessages(bridge);
    EXPECT_TRUE(msgs.empty());
}

TEST_F(EqCommandsAssistTest, DuelNoTarget_PrintsNoTargetMessage) {
    eq.ProcessChatInput("/duel");

    auto msgs = DrainSystemMessages(bridge);
    ASSERT_EQ(msgs.size(), 1u);
    EXPECT_EQ(msgs[0], "You don't have a target.");
}

TEST_F(EqCommandsAssistTest, DuelWithCombatManagerTarget_DoesNotPrintNoTargetMessage) {
    ASSERT_NO_THROW(ReplayZone(eq, HC_OP_NewSpawn,
        NewSpawnPayloadAt(100, "a_test_rat", 0, 10, 0)));
    auto* cm = eq.GetCombatManager();
    ASSERT_TRUE(cm->SetTarget(100));

    eq.ProcessChatInput("/duel");

    auto msgs = DrainSystemMessages(bridge);
    EXPECT_TRUE(msgs.empty());
}

// /bandage (bindwound) has different fallback semantics: no target ->
// self-target, not an error message. Assert via the absence of any "no
// target" system message either way (there never was one for this command)
// and rely on the other three tests to cover the shared repointing logic;
// bindwound's own SendBindWound() call is unconditional and its target-id
// argument is not independently observable without a live connection, so
// this only confirms the command still executes cleanly with a
// CombatManager target set (no exception, no unexpected message).
TEST_F(EqCommandsAssistTest, BandageWithCombatManagerTarget_RunsCleanly) {
    ASSERT_NO_THROW(ReplayZone(eq, HC_OP_NewSpawn,
        NewSpawnPayloadAt(100, "a_test_rat", 0, 10, 0)));
    auto* cm = eq.GetCombatManager();
    ASSERT_TRUE(cm->SetTarget(100));

    ASSERT_NO_THROW(eq.ProcessChatInput("/bandage"));

    auto msgs = DrainSystemMessages(bridge);
    EXPECT_TRUE(msgs.empty());
}

}  // namespace
