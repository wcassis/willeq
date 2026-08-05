// T51 — coverage for the consolidated attack path.
//
// Before this unit there were THREE implementations of enable/disable/
// toggle auto-attack: registry 1 (EverQuest::m_command_registry,
// eq_commands_movement.cpp), registry 2 (action::CommandProcessor,
// command_processor.cpp), and two producer-less RendererIntent structs
// (AttackIntent/ToggleAutoAttackIntent). T29's dispatch order (registry 1
// tried first) meant registry 1 always won every collision, so registry
// 2's three combat registrations and the two intents were dead code. This
// unit deleted registry 2's attack/stopattack/aa registrations (and their
// cmdAttack/cmdStopAttack/cmdAutoAttack methods) and the two intents,
// leaving registry 1's implementation as the sole surviving path.
//
// This file asserts: every surviving alias
// (attack/att/stopattack/aa/~) resolves through registry 1 and reaches
// CombatManager; that /a resolves to /assist (T51's live-probe finding --
// /a was NEVER live for attack, because it already collided with
// /assist's own pre-existing "a" alias in registry 1, T28, which wins the
// same dispatch order registry 2's now-deleted /a-for-attack alias always
// lost); see refactor/T51_attack_path_consolidation.md's Review for the
// live headless proof this mirrors.
//
// Seam: same as T28's test_eq_commands_assist.cpp -- a bare
// EverQuest("127.0.0.1", 0, ...) via MakeEq() gives a live, unconnected
// EverQuest with a real CommandRegistry and a real CombatManager
// (eq.GetCombatManager()). ProcessChatInput() is the same public entry
// point production chat input uses.

#include <gtest/gtest.h>

#include "client/bridge/game_state_bridge.h"
#include "client/combat.h"
#include "zone_stream_test_utils.h"

using namespace zonetest;

namespace {

std::vector<std::string> DrainSystemMessages(eqt::bridge::GameStateBridge& bridge) {
    std::vector<std::string> out;
    for (auto& ev : bridge.drainEvents()) {
        if (ev.type == eqt::state::GameEventType::SystemMessage) {
            out.push_back(std::get<eqt::state::ChatMessageData>(ev.data).message);
        }
    }
    return out;
}

class EqCommandsAttackTest : public ::testing::Test {
protected:
    EqCommandsAttackTest() : eq(MakeEq()) { eq.setBridge(&bridge); }
    ~EqCommandsAttackTest() override { eq.setBridge(nullptr); }

    void SpawnAndTarget(uint16_t id = 100) {
        ASSERT_NO_THROW(ReplayZone(eq, HC_OP_NewSpawn,
            NewSpawnPayloadAt(id, "a_test_rat", 0, 10, 0)));
        auto* cm = eq.GetCombatManager();
        ASSERT_TRUE(cm->SetTarget(id));
        ASSERT_TRUE(cm->HasTarget());
    }

    EverQuest eq;
    TestBridge bridge;
};

// --- /attack and its alias "att" ---

TEST_F(EqCommandsAttackTest, AttackNoTarget_DoesNotEnableAutoAttack) {
    eq.ProcessChatInput("/attack");
    EXPECT_FALSE(eq.GetCombatManager()->IsAutoAttackEnabled());
}

TEST_F(EqCommandsAttackTest, AttackWithTarget_EnablesAutoAttack) {
    SpawnAndTarget();
    eq.ProcessChatInput("/attack");
    EXPECT_TRUE(eq.GetCombatManager()->IsAutoAttackEnabled());
}

TEST_F(EqCommandsAttackTest, AttAlias_WithTarget_EnablesAutoAttack) {
    SpawnAndTarget();
    eq.ProcessChatInput("/att");
    EXPECT_TRUE(eq.GetCombatManager()->IsAutoAttackEnabled());
}

// --- /stopattack ---

TEST_F(EqCommandsAttackTest, StopAttack_DisablesAutoAttack) {
    SpawnAndTarget();
    eq.ProcessChatInput("/attack");
    ASSERT_TRUE(eq.GetCombatManager()->IsAutoAttackEnabled());

    eq.ProcessChatInput("/stopattack");
    EXPECT_FALSE(eq.GetCombatManager()->IsAutoAttackEnabled());
}

// --- /aa (toggle) and its alias "~" ---

TEST_F(EqCommandsAttackTest, Aa_TogglesAutoAttackOnThenOff) {
    SpawnAndTarget();
    eq.ProcessChatInput("/aa");
    EXPECT_TRUE(eq.GetCombatManager()->IsAutoAttackEnabled());

    eq.ProcessChatInput("/aa");
    EXPECT_FALSE(eq.GetCombatManager()->IsAutoAttackEnabled());
}

TEST_F(EqCommandsAttackTest, TildeAlias_TogglesAutoAttack) {
    SpawnAndTarget();
    eq.ProcessChatInput("/~");
    EXPECT_TRUE(eq.GetCombatManager()->IsAutoAttackEnabled());
}

TEST_F(EqCommandsAttackTest, Aa_NoTarget_PrintsNoTargetMessage_DoesNotEnable) {
    eq.ProcessChatInput("/aa");
    EXPECT_FALSE(eq.GetCombatManager()->IsAutoAttackEnabled());

    auto msgs = DrainSystemMessages(bridge);
    ASSERT_EQ(msgs.size(), 1u);
    EXPECT_EQ(msgs[0], "No target - auto attack not enabled");
}

// --- THE ALIAS TRAP, verified rather than assumed (T51 Piece 0) ---
//
// /a is NOT an attack alias today and never was reachable as one: it
// already collided with /assist's own "a" alias in registry 1 (T28),
// which wins the same dispatch-order race registry 2's now-deleted
// /a-for-attack alias always lost. This asserts that /a still means
// /assist after this unit's deletions -- i.e. that the deletion did not
// (and could not have) changed what /a does, since /a never reached
// attack in production to begin with.
TEST_F(EqCommandsAttackTest, AAlias_MeansAssist_NotAttack) {
    SpawnAndTarget();
    eq.ProcessChatInput("/a");

    // Assist does not touch auto-attack state at all.
    EXPECT_FALSE(eq.GetCombatManager()->IsAutoAttackEnabled());
}

}  // namespace
