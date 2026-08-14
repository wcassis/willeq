// T28 — coverage for the four eq_commands.cpp readers repointed from the
// never-populated state-layer CombatState target to the authoritative
// CombatManager (refactor/T28_state_layer_readers.md Part 2;
// refactor/deferred.md "Dead parallel state copies in the state layer, WITH
// LIVE READERS"). Originally covered all four ("/assist", "/shield",
// "/duel", "/bandage").
//
// T102 MIGRATES assist/shield/duel to action::CommandProcessor and deletes
// their eq_commands.cpp registrations -- this file's ProcessChatInput()
// seam can no longer reach them (they are no longer in the game registry
// at all), so their six TEST_F cases are REMOVED, not repointed.
// Equivalent coverage now lives in test_command_processor.cpp's T102
// section (FakeActionHandler-based, matching every other migrated
// family). /bandage (bindwound) is OUT of T102's scope -- it stays live in
// eq_commands.cpp -- so its test remains here, on the original seam. See
// refactor/T102_migrate_combat_social.md.
//
// Seam: same as T23's test_combat_manager.cpp -- a bare
// EverQuest("127.0.0.1", 0, ...) via MakeEq() (zone_stream_test_utils.h)
// gives a live, unconnected EverQuest with a real CommandRegistry
// (constructed unconditionally in the EverQuest ctor) and a real
// CombatManager (eq.GetCombatManager()). ProcessChatInput() is the same
// public entry point production chat input uses; it strips the leading '/'
// and dispatches through m_command_registry, reaching the exact lambda
// handler still in eq_commands.cpp. SendBindWound no-ops safely when
// m_zone_connection is null (verified by reading eq_player_info.cpp), so no
// network is needed -- the AddChatSystemMessage side effect (captured via
// the bridge's SystemMessage event, same idiom as test_zone_chat.cpp's
// DrainChat) and CombatManager state are what's asserted.

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

// /bandage (bindwound) has different fallback semantics: no target ->
// self-target, not an error message. Assert via the absence of any "no
// target" system message either way (there never was one for this command)
// and rely on this being the sole remaining test in the file to cover the
// repointing logic that (still) applies to it; bindwound's own
// SendBindWound() call is unconditional and its target-id argument is not
// independently observable without a live connection, so this only
// confirms the command still executes cleanly with a CombatManager target
// set (no exception, no unexpected message).
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
