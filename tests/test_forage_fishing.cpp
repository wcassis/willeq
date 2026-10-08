// B894 (GitLab #210) -- Forage (skill 27) and Fishing (skill 55) are sent but
// the client-side preconditions are missing: eqstr 148/149 (forage while
// attacking / not standing), 156/159/160/161/162 (already fishing, fishing
// while attacking, no pole, pole not in primary, no bait) and 165 (moving
// stops fishing). This file is the offline leg: every refusal case must
// refuse with the SHIPPED eqstr line (fetched through the one eqstr
// formatter, eqt::EqStrLine, so a transcription typo is invisible) and send
// NOTHING, and a satisfied precondition must send exactly one empty
// OP_Forage / OP_Fishing -- the wire form T152 audited for these two
// opcodes.
//
// Same headless EverQuest + TestBridge seam as test_skill_gate_bypasses.cpp;
// no network, no graphics, no eqemu. The refusal reason is observed through
// the D20b2 activation callback wiring in everquest.cpp ("Cannot use {}:" +
// the SkillManager's getSkillUseError text), the exact channel the T162
// tests read. "Sends nothing" is witnessed two ways: the skill's cooldown
// stays cold (activateSkill only starts the recast after
// sendSkillUsePacket) and the queued-packet capture (PacketSender's
// Test_ seams) stays empty of HC_OP_Forage / HC_OP_Fishing.
//
// RED-FIRST STATE (2026-10-08): the not-trained test below is the only test
// in the file for the first commit; it exercises the T159 possession gate
// that already exists, so it is the GREEN baseline. The forage/fishing
// precondition tests are added one at a time, each RED before its fix.

#include <gtest/gtest.h>

#include "zone_stream_test_utils.h"

#include "client/display_eqstr.h"
#include "client/skill/skill_constants.h"
#include "client/skill/skill_cap_table.h"
#include "client/skill/skill_manager.h"
#include "client/combat.h"
#include "client/string_database.h"
#include "client/willeq_database.h"

#include <string>
#include <vector>

using namespace zonetest;

namespace {

constexpr const char* kShippedCaps = WILLEQ_REPO_ROOT "/config/skill_caps.json";

// Class ids as the server (and the PlayerProfile) numbers them.
constexpr uint8_t kWarrior = 1;
constexpr uint8_t kRanger  = 4;
constexpr uint8_t kDruid   = 6;
constexpr uint8_t kHuman   = 1;
constexpr uint8_t kLevel   = 30;

// The two skills B894 names, and their dedicated opcodes (getSkillOpcode,
// skill_manager.cpp: Forage 27 -> HC_OP_Forage, Fishing 55 -> HC_OP_Fishing).
constexpr uint8_t kForage   = 27;
constexpr uint8_t kFishing  = 55;
constexpr uint16_t kOpForage   = HC_OP_Forage;
constexpr uint16_t kOpFishing  = HC_OP_Fishing;

std::vector<std::string> DrainSystemMessages(eqt::bridge::GameStateBridge& bridge) {
    std::vector<std::string> out;
    for (auto& ev : bridge.drainEvents()) {
        if (ev.type == eqt::state::GameEventType::SystemMessage) {
            out.push_back(std::get<eqt::state::ChatMessageData>(ev.data).message);
        }
    }
    return out;
}

bool AnyContains(const std::vector<std::string>& msgs, const std::string& needle) {
    for (const auto& m : msgs) {
        if (m.find(needle) != std::string::npos) {
            return true;
        }
    }
    return false;
}

// True if `eq` queued a packet for `opcode` since the last ClearQueuedPackets.
bool Sent(const EverQuest& eq, uint16_t opcode) {
    for (const auto& p : eq.Test_GetQueuedPackets()) {
        if (p.opcode == opcode) {
            return true;
        }
    }
    return false;
}

class ForageFishingTest : public ::testing::Test {
protected:
    ForageFishingTest() : eq(MakeEq()) { eq.setBridge(&bridge); }
    ~ForageFishingTest() override { eq.setBridge(nullptr); }

    void SetUp() override {
        ASSERT_TRUE(EQ::SkillCapTable::instance().load(kShippedCaps))
            << "the shipped config/skill_caps.json must load: " << kShippedCaps;
        // The eqstr ids this unit's refusal lines come from, fetched from the
        // SHIPPED string table, not transcribed here.
        ASSERT_TRUE(db_.open(WILLEQ_DB_REAL_PATH)) << WILLEQ_DB_REAL_PATH;
        EQT::WilleqDatabase::setGlobalInstance(&db_);
        ASSERT_TRUE(strings_.initFromDatabase());
        // The SAME rows, loaded into the EverQuest-under-test's own string db
        // so the SkillManager's refusal fetches the SHIPPED line and the
        // literal fallback (a never-loaded table) cannot be what the test
        // sees.
        ASSERT_TRUE(eq.Strings().LoadStringFiles(WILLEQ_REPO_ROOT));
    }
    void TearDown() override {
        EQ::SkillCapTable::instance().load(kShippedCaps);
        EQT::WilleqDatabase::setGlobalInstance(nullptr);
    }

    EQ::SkillManager& Skills(uint8_t player_class, uint8_t level = kLevel) {
        // EXPECT, not ASSERT: this returns a reference, and an ASSERT_* would
        // `return;` its way out of the wrong type (compile error).
        auto* sm = eq.GetSkillManager();
        EXPECT_NE(sm, nullptr);
        sm->initialize(player_class, kHuman, level);
        return *sm;
    }

    // The shipped line for an eqstr id, via the one formatter.
    std::string Line(uint32_t id) const { return eqt::EqStrLine(strings_, id); }

    // The eqstr ids B894's table names (eqstr_us.txt, verified against
    // data/willeq.db's eqstrings below).
    static constexpr uint32_t kEqstrForageAttacking = 148;  // You can't try to forage while attacking.
    static constexpr uint32_t kEqstrForageStanding  = 149;  // You must be standing to forage.
    static constexpr uint32_t kEqstrAlreadyFishing  = 156;  // You are already fishing!
    static constexpr uint32_t kEqstrFishingAttacking = 159; // You can't start fishing while attacking!
    static constexpr uint32_t kEqstrNoPole          = 160;  // ...without a fishing pole...
    static constexpr uint32_t kEqstrPolePrimary     = 161;  // ...pole in your primary hand.
    static constexpr uint32_t kEqstrNoBait          = 162;  // ...without fishing bait...
    static constexpr uint32_t kEqstrStopFishing     = 165;  // You stop fishing and go on your way.

    EverQuest eq;
    TestBridge bridge;
    EQT::WilleqDatabase db_;
    EQT::StringDatabase strings_;
};

// ===================================================================
// 0. The data the table gates on
// ===================================================================
//
// config/skill_caps.json (peq.skill_caps export) says: Forage is Ranger(4),
// Druid(6), Bard(8) and Monk(12)-adjacent rows only; Fishing is EVERY class
// (skill 55 appears in all sixteen rows). Warrior has neither skill 27 nor a
// way past the possession gate for it. Pin those two rows so a caps-file
// change fails here instead of silently re-opening a refusal the live lane
// already proved.

TEST_F(ForageFishingTest, CapTableSaysWhoMayForageAndEveryoneMayFish) {
    const auto& table = EQ::SkillCapTable::instance();
    ASSERT_TRUE(table.isLoaded());
    EXPECT_FALSE(table.classCanEverHaveSkill(kWarrior, kForage))
        << "Warrior must not own Forage in the shipped caps";
    EXPECT_TRUE(table.classCanEverHaveSkill(kRanger, kForage));
    EXPECT_TRUE(table.classCanEverHaveSkill(kDruid, kForage));
    for (uint8_t cls = 1; cls <= 16; ++cls) {
        EXPECT_TRUE(table.classCanEverHaveSkill(cls, kFishing))
            << "class " << int(cls) << " must be able to fish";
    }
    // The shipped eqstr rows this unit's refusals draw -- they exist.
    EXPECT_EQ(Line(kEqstrForageAttacking), "You can't try to forage while attacking.");
    EXPECT_EQ(Line(kEqstrForageStanding), "You must be standing to forage.");
    EXPECT_EQ(Line(kEqstrAlreadyFishing), "You are already fishing!");
    EXPECT_EQ(Line(kEqstrFishingAttacking), "You can't start fishing while attacking!");
    EXPECT_EQ(Line(kEqstrNoPole), "You can't fish without a fishing pole, go buy one.");
    EXPECT_EQ(Line(kEqstrPolePrimary), "You need to put your fishing pole in your primary hand.");
    EXPECT_EQ(Line(kEqstrNoBait), "You can't fish without fishing bait, go buy some.");
    EXPECT_EQ(Line(kEqstrStopFishing), "You stop fishing and go on your way.");
}

// ===================================================================
// 1. Not trained: the possession gate refuses with the right line, sends
//    nothing. (GREEN baseline -- this is T159's gate, exercised for the two
//    B894 skills for the first time.)
// ===================================================================

TEST_F(ForageFishingTest, ForageRefusedWhenTheClassCannotHaveIt) {
    auto& skills = Skills(kWarrior);
    eq.Net().Test_SetBypassSendGuardForSeam(true);
    eq.Test_SetCaptureQueuedPacketsForSeam(true);
    eq.Test_ClearQueuedPackets();
    DrainSystemMessages(bridge);

    ASSERT_FALSE(skills.canUseSkill(kForage));
    const std::string error = skills.getSkillUseError(kForage);
    ASSERT_EQ(error, "Your class cannot use Forage");

    EXPECT_FALSE(skills.activateSkill(kForage));
    EXPECT_FALSE(skills.isSkillOnCooldown(kForage))
        << "a refusal must not have reached sendSkillUsePacket()";
    EXPECT_FALSE(Sent(eq, kOpForage))
        << "no OP_Forage may go out for a skill the class cannot have";

    auto msgs = DrainSystemMessages(bridge);
    EXPECT_TRUE(AnyContains(msgs, "Cannot use Forage: Your class cannot use Forage"))
        << "the player must be told the real reason; got " << msgs.size() << " message(s)";
}

// ===================================================================
// 2. Forage while attacking -> eqstr 148, nothing sent.
//    RED: no such gate exists yet; a Ranger in an active attack (auto-attack
//    on, COMBAT_STATE_ENGAGED -- the client's in-combat signal, same witness
//    test_skill_gates uses) sends OP_Forage.
// ===================================================================

TEST_F(ForageFishingTest, ForageRefusedWhileAttackingWithEqstr148) {
    auto* sm = eq.GetSkillManager();
    ASSERT_NE(sm, nullptr);
    sm->initialize(kRanger, kHuman, kLevel);
    auto& skills = *sm;
    skills.updateSkill(kForage, 100);
    eq.Net().Test_SetBypassSendGuardForSeam(true);
    eq.Test_SetCaptureQueuedPacketsForSeam(true);
    eq.Test_ClearQueuedPackets();
    DrainSystemMessages(bridge);

    auto* combat = eq.GetCombatManager();
    // Auto-attack engages a target; a target is required to engage.
    ReplayZone(eq, HC_OP_NewSpawn,
               NewSpawnPayloadAt(500, "a_probe_dummy", 0, 10, 0));
    ASSERT_TRUE(combat->SetTarget(500));
    combat->EnableAutoAttack();
    ASSERT_TRUE(combat->IsAutoAttackEnabled());
    ASSERT_EQ(combat->GetCombatState(), COMBAT_STATE_ENGAGED);

    EXPECT_FALSE(skills.canUseSkill(kForage))
        << "forage while attacking must refuse (eqstr 148)";
    EXPECT_EQ(skills.getSkillUseError(kForage), Line(kEqstrForageAttacking))
        << "the refusal must be the SHIPPED eqstr 148 line";

    EXPECT_FALSE(skills.activateSkill(kForage));
    EXPECT_FALSE(skills.isSkillOnCooldown(kForage))
        << "a refusal must not have reached sendSkillUsePacket()";
    EXPECT_FALSE(Sent(eq, kOpForage)) << "no OP_Forage may go out while attacking";

    auto msgs = DrainSystemMessages(bridge);
    EXPECT_TRUE(AnyContains(msgs, Line(kEqstrForageAttacking)))
        << "the player must be told " << Line(kEqstrForageAttacking) << "; got "
        << msgs.size() << " message(s)";
}

}  // namespace
