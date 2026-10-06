// T50 -- coverage for /bank and /train's guard/validation logic, now living
// in EverQuest::RequestBankToggle()/RequestTraining() (T140 migrated the
// commands themselves to action::CommandProcessor -- see
// refactor/T140_migrate_final_seventeen.md; the guard bodies moved
// verbatim into these two new EverQuest methods, since they read private
// m_combat_manager/m_entities/m_class/m_bridge state). Closes two
// automation-reachability gaps this project has now hit five times
// (refactor/deferred.md "PATTERN"): /bank used to bypass its own
// gesture-validated intent (BankerInteractIntent) via a no-arg
// OpenBankWindow() that falls back to a hardcoded dummy npc id, and /train
// had no distance or class checks of its own -- the server drops both
// failures in total silence (verified against the real eqemu source,
// zone/client_process.cpp's Client::OPGMTraining and
// zone/client_packet.cpp's Handle_OP_BankerChange), so previously the only
// symptom was "nothing happened". See refactor/T50_bank_train_prevalidation.md.
//
// Modelled on test_eq_commands_vendor.cpp's (T48) MakeEq()/TestBridge seam,
// but calls the new EverQuest methods directly (T136/T140's pattern for
// EqActionHandler-fronted wrappers) rather than going through
// ProcessChatInput/the now-deleted game-registry commands.
//
// /bank's assertions stop at "the right intent was pushed with the right
// npc id" -- BankerInteractIntent's own handler (npc_type/distance/
// class==CLASS_BANKER validation, eq_bridge.cpp) is explicitly out of this
// unit's territory (untouched, pre-existing code) and is exercised for
// real in the live eqemu acceptance under benchmarks/audits/T50/.
//
// /train's new distance/class checks are NOT behind an intent -- they were
// added directly to the command handler (the plan's Piece 2), so these
// tests assert the printed message directly, per the plan's explicit bar
// ("assert the message, not just the refusal").

#include <gtest/gtest.h>

#include "client/bridge/game_state_bridge.h"
#include "client/combat.h"
#include "client/events/renderer_intents.h"
#include "client/movement/movement_component.h"
#include "eqstr_test_db.h"
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

// Sets EverQuest::m_class via the real production path (ZoneProcessPlayerProfile,
// HC_OP_PlayerProfile) rather than a test-only backdoor -- same discipline
// T48's vendor-open setup used. PlayerProfile_Struct is a fixed 19588 bytes;
// only the class offset (base+12) is populated, everything else stays zero.
void SetPlayerClass(EverQuest& eq, uint32_t classId) {
    auto payload = BuildPayload(2 + 19588, [&](EQ::Net::DynamicPacket& p) {
        p.PutUInt32(2 + 12, classId);
    });
    ReplayZone(eq, HC_OP_PlayerProfile, payload);
}

class EqCommandsBankTrainTest : public ::testing::Test {
protected:
    EqCommandsBankTrainTest() : eq(MakeEq()) { eq.setBridge(&bridge); }
    ~EqCommandsBankTrainTest() override { eq.setBridge(nullptr); }

    // Spawns an NPC at the player's own position (0,0,0 -- MakeEq()'s
    // default) so distance checks pass by default; tests that need a
    // too-far target override x/y explicitly.
    void SpawnNpcAt(uint32_t spawnId, const std::string& name, uint8_t classId,
                     int32_t x = 0, int32_t y = 0) {
        ReplayZone(eq, HC_OP_NewSpawn, NewSpawnPayloadAt(spawnId, name, x, y, 0,
                                                          /*level=*/5, /*npc_type=*/1, classId));
        bridge.drainEvents();  // discard NewSpawn-related events
    }

    EverQuest eq;
    TestBridge bridge;
};

// ---------------------------------------------------------------------------
// /bank -- T50 Piece 1: now pushes BankerInteractIntent{targetId} for the
// current target instead of bypassing it with a no-arg OpenBankWindow().
// This IS a deliberate behaviour change (the plan requires it be stated
// plainly): /bank now requires a targeted banker to open.
// ---------------------------------------------------------------------------

TEST_F(EqCommandsBankTrainTest, Bank_NoTargetNotOpen_PrintsNoTarget_PushesNoIntent) {
    eq.Commands().RequestBankToggle();

    auto msgs = DrainSystemMessages(bridge);
    ASSERT_EQ(msgs.size(), 1u);
    EXPECT_NE(msgs[0].find("target"), std::string::npos);
    EXPECT_TRUE(bridge.drainIntents().empty());
}

TEST_F(EqCommandsBankTrainTest, Bank_WithNonBankerTarget_StillPushesIntent_HandlerValidatesClass) {
    // T50's territory is the COMMAND's reachability into the intent system,
    // not re-verifying BankerInteractIntent's own npc_type/class checks
    // (out of scope per the plan's fence -- "Do not change...
    // BankerInteractIntent's handler"). /bank itself does not duplicate
    // those checks: it pushes the intent for whatever is targeted, and the
    // existing (untouched) handler in eq_bridge.cpp is the one that refuses
    // a non-banker. So a non-banker target still produces a pushed intent
    // here -- the live eqemu acceptance (benchmarks/audits/T50/) exercises
    // the handler's own refusal for real.
    SpawnNpcAt(100, "a_random_npc", /*classId=*/1);  // class 1 != CLASS_BANKER(40)
    auto* cm = eq.GetCombatManager();
    ASSERT_TRUE(cm->SetTarget(100));

    eq.Commands().RequestBankToggle();

    EXPECT_TRUE(DrainSystemMessages(bridge).empty());
    auto intents = bridge.drainIntents();
    ASSERT_EQ(intents.size(), 1u);
    ASSERT_TRUE(std::holds_alternative<eqt::events::BankerInteractIntent>(intents[0]));
    EXPECT_EQ(std::get<eqt::events::BankerInteractIntent>(intents[0]).npcId, 100u);
}

TEST_F(EqCommandsBankTrainTest, Bank_WithValidBankerTarget_PushesBankerInteractIntentWithTargetId) {
    constexpr uint8_t CLASS_BANKER = 40;
    SpawnNpcAt(200, "a_banker", CLASS_BANKER);
    auto* cm = eq.GetCombatManager();
    ASSERT_TRUE(cm->SetTarget(200));

    eq.Commands().RequestBankToggle();

    EXPECT_TRUE(DrainSystemMessages(bridge).empty());
    auto intents = bridge.drainIntents();
    ASSERT_EQ(intents.size(), 1u);
    ASSERT_TRUE(std::holds_alternative<eqt::events::BankerInteractIntent>(intents[0]));
    EXPECT_EQ(std::get<eqt::events::BankerInteractIntent>(intents[0]).npcId, 200u);
}

TEST_F(EqCommandsBankTrainTest, Bank_AlreadyOpen_ClosesDirectly_WithoutRequiringTarget) {
    // The close half deliberately does NOT go through the intent (it has no
    // toggle/close branch -- see RegisterSpellCommands()'s "bank" Command's
    // comment) and deliberately does not require a valid current target:
    // you may have changed target since opening, and closing must still
    // work.
    eq.Merchant().OpenBankWindow(999);  // real production entry point, not a backdoor
    ASSERT_TRUE(eq.IsBankWindowOpen());
    bridge.drainEvents();
    bridge.drainIntents();
    ASSERT_FALSE(eq.GetCombatManager()->HasTarget());  // no target set at all

    eq.Commands().RequestBankToggle();

    EXPECT_FALSE(eq.IsBankWindowOpen());
    EXPECT_TRUE(bridge.drainIntents().empty());  // close is a direct call, not an intent
}

// ---------------------------------------------------------------------------
// /train -- T50 Piece 2: pre-validates distance (NPC_INTERACTION_DISTANCE,
// 15 units -- same constant the banker gesture and movement proximity gate
// use) and class match (trainerClass - 19 == playerClass, mirroring the
// server's own Client::OPGMTraining check) instead of sending a request the
// server silently drops.
// ---------------------------------------------------------------------------

TEST_F(EqCommandsBankTrainTest, Train_NoTarget_PrintsNoTrainerTargeted) {
    eq.Commands().RequestTraining();

    auto msgs = DrainSystemMessages(bridge);
    ASSERT_EQ(msgs.size(), 1u);
    EXPECT_EQ(msgs[0], "You must have a trainer targeted.");
}

TEST_F(EqCommandsBankTrainTest, Train_NotAnNpc_PrintsNotAnNpc) {
    // npc_type=0 means player/PC, not NPC.
    ReplayZone(eq, HC_OP_NewSpawn,
        NewSpawnPayloadAt(300, "SomePlayer", 0, 0, 0, /*level=*/5, /*npc_type=*/0, /*class=*/1));
    bridge.drainEvents();
    ASSERT_TRUE(eq.GetCombatManager()->SetTarget(300));

    eq.Commands().RequestTraining();

    auto msgs = DrainSystemMessages(bridge);
    ASSERT_EQ(msgs.size(), 1u);
    EXPECT_EQ(msgs[0], "That is not an NPC.");
}

TEST_F(EqCommandsBankTrainTest, Train_TargetClassOutsideGmRange_PrintsNotATrainer) {
    SpawnNpcAt(301, "a_random_npc", /*classId=*/1);  // not in [20,35]
    ASSERT_TRUE(eq.GetCombatManager()->SetTarget(301));

    eq.Commands().RequestTraining();

    auto msgs = DrainSystemMessages(bridge);
    ASSERT_EQ(msgs.size(), 1u);
    EXPECT_EQ(msgs[0], "That is not a trainer.");
}

TEST_F(EqCommandsBankTrainTest, Train_TooFar_PrintsActionableDistanceMessage) {
    // NPC_INTERACTION_DISTANCE is 15; place the trainer 1000 units away.
    SpawnNpcAt(302, "Distant_WarriorGM", /*classId=*/20, /*x=*/1000, /*y=*/1000);
    ASSERT_TRUE(eq.GetCombatManager()->SetTarget(302));

    eq.Commands().RequestTraining();

    auto msgs = DrainSystemMessages(bridge);
    ASSERT_EQ(msgs.size(), 1u);
    EXPECT_NE(msgs[0].find("too far"), std::string::npos);
    EXPECT_NE(msgs[0].find("Distant_WarriorGM"), std::string::npos);
}

TEST_F(EqCommandsBankTrainTest, Train_WrongClassTrainer_PrintsActionableMessageNamingBothClasses) {
    // Trainer is class 22 (PaladinGM, trains Paladin=3), player is left at
    // the MakeEq() default m_class=0 -- guaranteed to mismatch (valid
    // classes are 1-16). This is T49's exact reproduction case
    // (Miltiades_Tal`Azroth, class 22) -- previously silent, now actionable.
    SpawnNpcAt(303, "Miltiades_Tal_Azroth", /*classId=*/22);
    ASSERT_TRUE(eq.GetCombatManager()->SetTarget(303));

    eq.Commands().RequestTraining();

    auto msgs = DrainSystemMessages(bridge);
    ASSERT_EQ(msgs.size(), 1u);
    EXPECT_NE(msgs[0].find("Miltiades_Tal_Azroth"), std::string::npos);
    EXPECT_NE(msgs[0].find("Paladin"), std::string::npos)
        << "message should name the class the trainer teaches: " << msgs[0];
}

TEST_F(EqCommandsBankTrainTest, Train_MatchingClassInRange_SendsTrainerRequest_NoMessage) {
    constexpr uint8_t CLASS_WARRIOR_GM = 20;
    SetPlayerClass(eq, /*classId=*/1);  // Warrior
    SpawnNpcAt(304, "a_warrior_gm", CLASS_WARRIOR_GM);  // trains class 20-19=1=Warrior
    ASSERT_TRUE(eq.GetCombatManager()->SetTarget(304));

    eq.Commands().RequestTraining();

    // No refusal message -- the pre-validation passed and RequestTrainerWindow()
    // (untouched, pre-existing code, out of this unit's territory) was called.
    // We don't re-assert the outgoing HC_OP_GMTraining packet here (that's
    // RequestTrainerWindow's own behavior, not this unit's territory); the
    // observable this unit owns is "did pre-validation refuse or not".
    EXPECT_TRUE(DrainSystemMessages(bridge).empty());
}

// ---------------------------------------------------------------------------
// UIGAP-residue (B996, GitLab #316): the banker's range, MEASURED on the
// official client by UIGAP-live2 -- review/UIGAP-ORACLE/316/range.tsv. A
// right-click opens the bank at 10 / 15 / 19.8 / 20 units (rows 1, 3, 6, 10)
// and is a /consider at 21.2 / 22.6 / 28.3 (rows 7-9); "Welcome to my bank!"
// (eqstr 1465) is the client's own line; an open bank closes itself past 20
// with "Come back soon!" (eqstr 12143) and stays open at 20 (rows 2, 4, 5).
// The banker is at the origin and the player is moved (a distance from the
// origin is the same in either axis order).
// ---------------------------------------------------------------------------

class BankRangeTest : public EqCommandsBankTrainTest {
protected:
    static constexpr uint8_t kClassBanker = 40;
    void SetUp() override {
        ASSERT_TRUE(eqt_test::ShippedStrings().isLoaded()) << "data/willeq.db";
        ASSERT_TRUE(eq.Strings().LoadStringFiles(""));
        SpawnNpcAt(500, "Kendall_Copperhold000", kClassBanker);
    }
    void openAt(float distance) {
        eq.SetPosition(distance, 0.0f, 0.0f);
        bridge.drainEvents();
        bridge.pushIntent(eqt::events::BankerInteractIntent{500});
        eq.Bridge().ProcessBridgeIntents();
    }
};

TEST_F(BankRangeTest, OpensAtTheMeasuredDistancesWithTheWelcomeLine) {
    for (float d : {10.0f, 15.0f, 19.8f, 20.0f}) {
        openAt(d);
        EXPECT_TRUE(eq.IsBankWindowOpen()) << "range.tsv: opens at " << d;
        const auto msgs = DrainSystemMessages(bridge);
        ASSERT_EQ(msgs.size(), 1u) << d;
        EXPECT_EQ(msgs[0], "Kendall Copperhold tells you, 'Welcome to my bank!'") << "eqstr 1465";
        eq.Merchant().CloseBankWindow();
    }
}

TEST_F(BankRangeTest, BeyondTwentyIsAConsiderNotATooFarLine) {
    for (float d : {21.2f, 22.6f, 28.3f}) {
        openAt(d);
        EXPECT_FALSE(eq.IsBankWindowOpen()) << "range.tsv rows 7-9: no bank at " << d;
        EXPECT_TRUE(DrainSystemMessages(bridge).empty()) << "a consider, no refusal line";
        EXPECT_EQ(eq.GetCombatManager()->GetTargetId(), 500) << "the banker is targeted and considered";
    }
}

TEST_F(BankRangeTest, AnOpenBankStaysAtTwentyAndClosesPastIt) {
    openAt(10.0f);
    ASSERT_TRUE(eq.IsBankWindowOpen());
    bridge.drainEvents();
    eq.SetPosition(20.0f, 0.0f, 0.0f);
    eq.Movement().UpdateMovement();
    EXPECT_TRUE(eq.IsBankWindowOpen()) << "row 4: stays open at 20.0";
    EXPECT_TRUE(DrainSystemMessages(bridge).empty());

    eq.SetPosition(21.2f, 0.0f, 0.0f);
    eq.Movement().UpdateMovement();
    EXPECT_FALSE(eq.IsBankWindowOpen()) << "rows 2, 5: closes past 20";
    const auto msgs = DrainSystemMessages(bridge);
    ASSERT_EQ(msgs.size(), 1u);
    EXPECT_EQ(msgs[0], "Kendall Copperhold tells you, 'Come back soon!'") << "eqstr 12143";
}

// The trainer half of the same gesture keeps its own (unmeasured) 15: the
// banker's 20 does not loosen it.
TEST_F(EqCommandsBankTrainTest, TheBankersRangeDoesNotLoosenTheTrainers) {
    SpawnNpcAt(305, "a_warrior_gm", /*classId=*/20);
    eq.SetPosition(18.0f, 0.0f, 0.0f);
    bridge.drainEvents();
    bridge.pushIntent(eqt::events::BankerInteractIntent{305});
    eq.Bridge().ProcessBridgeIntents();
    const auto msgs = DrainSystemMessages(bridge);
    ASSERT_EQ(msgs.size(), 1u);
    EXPECT_EQ(msgs[0], "You are too far away to interact with this NPC.");
}

}  // namespace
