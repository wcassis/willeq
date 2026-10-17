// T162 — the commands that emitted a skill opcode WITHOUT passing T159's gate.
//
// T159 put a class-possession gate inside SkillManager::activateSkill(). It
// works: T161 watched a live Rogue be refused /useskill taunt with no packet
// on the wire. Forty-five seconds later the SAME character ran /taunt on the
// SAME target, the client sent OP_Taunt anyway, printed "Taunting target",
// and the arena zone log recorded the packet arriving and being discarded in
// silence by Handle_OP_Taunt's HasSkill() guard.
//
// The gate was never wrong. It simply was not on the path: /taunt went
// EqActionHandler::taunt() -> CombatManager::Taunt(), one function away from
// activateSkill(). /bandage (EverQuest::SendBindWound) and /track
// (EverQuest::SendTrackTarget) had the same shape.
//
// These tests drive EqActionHandler directly -- the layer where the bypass
// lived -- against a real EverQuest, a real CombatManager and a real
// SkillManager loaded from the SHIPPED config/skill_caps.json. No synthetic
// cap table: a broken shipped file must fail these tests, not pass them.
//
// New file rather than growth of tests/test_command_processor.cpp, which is
// already past the 3000-line FAIL ceiling.

#include <gtest/gtest.h>

#include "zone_stream_test_utils.h"

#include "client/eq_action_handler.h"
#include "client/skill/skill_cap_table.h"
#include "client/skill/skill_constants.h"
#include "client/skill/skill_manager.h"
#include "client/combat.h"

#include <string>
#include <vector>

using namespace zonetest;

namespace {

const char* const kShippedCaps = WILLEQ_REPO_PATH("config/skill_caps.json");  // B1090: run-time root

// Class ids as the server (and the PlayerProfile) numbers them.
constexpr uint8_t kWarrior = 1;
constexpr uint8_t kRanger  = 4;
constexpr uint8_t kRogue   = 9;
constexpr uint8_t kHuman   = 1;

constexpr uint8_t kProbeLevel = 10;  // Oraclerogue's level.

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

class SkillGateBypassTest : public ::testing::Test {
protected:
    SkillGateBypassTest() : eq(MakeEq()), handler(eq) { eq.setBridge(&bridge); }
    ~SkillGateBypassTest() override { eq.setBridge(nullptr); }

    void SetUp() override {
        ASSERT_TRUE(EQ::SkillCapTable::instance().load(kShippedCaps))
            << "the shipped config/skill_caps.json must load: " << kShippedCaps;
    }
    void TearDown() override {
        EQ::SkillCapTable::instance().load(kShippedCaps);
    }

    uint16_t TargetAnNpc(uint16_t spawn_id = 500) {
        EXPECT_NO_THROW(ReplayZone(eq, HC_OP_NewSpawn,
            NewSpawnPayloadAt(spawn_id, "a_probe_dummy", 0, 10, 0)));
        auto* cm = eq.GetCombatManager();
        EXPECT_TRUE(cm->SetTarget(spawn_id));
        return spawn_id;
    }

    EQ::SkillManager& Skills(uint8_t player_class, uint8_t level = kProbeLevel) {
        auto* sm = eq.GetSkillManager();
        EXPECT_NE(sm, nullptr);
        sm->initialize(player_class, kHuman, level);
        return *sm;
    }

    EverQuest eq;
    TestBridge bridge;
    eqt::EqActionHandler handler;
};

// ===================================================================
// 1. The cap data behind every command this unit gates
// ===================================================================
//
// Gating a command whose skill has no cap row for a class that legitimately
// uses it would BREAK working behaviour -- the false negative T159 was built
// to avoid. Pin the three rows so a data change fails here loudly.

TEST_F(SkillGateBypassTest, CapDataSaysWhoMayTauntBandageAndTrack) {
    const auto& table = EQ::SkillCapTable::instance();
    ASSERT_TRUE(table.isLoaded());

    // Bind Wound (/bandage): ALL SIXTEEN classes. Gating /bandage can refuse
    // nobody -- which is why T158's and T161's live /bandage cannot regress.
    for (uint8_t cls = 1; cls <= 16; ++cls) {
        EXPECT_TRUE(table.classCanEverHaveSkill(cls, EQ::kSkillBindWound))
            << "class " << int(cls) << " must be able to bind wounds";
    }

    // Taunt (/taunt): Warrior, Paladin, Ranger, Shadowknight -- and not Rogue.
    for (uint8_t cls = 1; cls <= 16; ++cls) {
        const bool expected = (cls == 1 || cls == 3 || cls == 4 || cls == 5);
        EXPECT_EQ(table.classCanEverHaveSkill(cls, EQ::kSkillTaunt), expected)
            << "class " << int(cls) << " taunt";
    }

    // Tracking (/track): Ranger, Druid, Bard -- exactly eqemu's
    // Client::CanThisClassTrack(), which Handle_OP_TrackTarget consults
    // before it does anything at all.
    for (uint8_t cls = 1; cls <= 16; ++cls) {
        const bool expected = (cls == 4 || cls == 6 || cls == 8);
        EXPECT_EQ(table.classCanEverHaveSkill(cls, EQ::kSkillTracking), expected)
            << "class " << int(cls) << " tracking";
    }
}

// ===================================================================
// 2. HEADLINE — T161's exact live case: a Rogue's /taunt sends nothing
// ===================================================================
//
// This is the test that must fail against pre-T162 code: before the fix,
// EqActionHandler::taunt() called CombatManager::Taunt() unconditionally and
// printed "Taunting target".

TEST_F(SkillGateBypassTest, RogueTauntCommandIsRefused_AndNothingIsSent) {
    auto& skills = Skills(kRogue);
    TargetAnNpc();                       // so "no target" cannot be the reason
    skills.updateSkill(EQ::kSkillTaunt, 0);
    DrainSystemMessages(bridge);         // clear spawn/target chatter

    handler.taunt();                     // == /taunt

    // Nothing was sent. activateSkill() starts Taunt's 5s recast ONLY on the
    // branch that has already reached sendSkillUsePacket() -- and that is the
    // only path to CombatManager::Taunt() left in the client, since T152 made
    // skill 73 route through it and added no second sender. A cold cooldown
    // is therefore the witness that no OP_Taunt was built.
    EXPECT_FALSE(skills.isSkillOnCooldown(EQ::kSkillTaunt));
    EXPECT_EQ(skills.getSkillCooldownRemaining(EQ::kSkillTaunt), 0u);

    auto msgs = DrainSystemMessages(bridge);
    EXPECT_TRUE(AnyContains(msgs, "Your class cannot use Taunt"))
        << "the player must be told the real reason; got " << msgs.size()
        << " message(s)";
    // The two lies T161 recorded, both gone.
    EXPECT_FALSE(AnyContains(msgs, "Taunting target"));
    EXPECT_FALSE(AnyContains(msgs, "You use Taunt!"));
}

// ===================================================================
// 3. A Warrior's /taunt still sends
// ===================================================================

TEST_F(SkillGateBypassTest, WarriorTauntCommandStillSends) {
    auto& skills = Skills(kWarrior);
    TargetAnNpc();
    skills.updateSkill(EQ::kSkillTaunt, 0);  // value irrelevant: gate is on class
    DrainSystemMessages(bridge);

    handler.taunt();

    // The send branch ran -- it is the only thing that starts the recast.
    EXPECT_TRUE(skills.isSkillOnCooldown(EQ::kSkillTaunt));

    auto msgs = DrainSystemMessages(bridge);
    EXPECT_TRUE(AnyContains(msgs, "You use Taunt!"));
    EXPECT_FALSE(AnyContains(msgs, "Your class cannot use"));
}

// ===================================================================
// 4. /taunt with no target is unchanged
// ===================================================================

TEST_F(SkillGateBypassTest, TauntWithNoTargetStillSaysNoTargetToTaunt) {
    Skills(kWarrior);
    DrainSystemMessages(bridge);

    handler.taunt();

    EXPECT_TRUE(AnyContains(DrainSystemMessages(bridge), "No target to taunt"));
}

// ===================================================================
// 5. /bandage must NOT regress — T158 and T161 both verified it live
// ===================================================================

TEST_F(SkillGateBypassTest, RogueBandageStillWorks) {
    auto& skills = Skills(kRogue);
    TargetAnNpc();
    DrainSystemMessages(bridge);

    handler.bandage(0);                  // == /bandage

    // Bind Wound's 1s recast started, so sendSkillUsePacket() ran and
    // EverQuest::SendBindWound() was reached -- the same sender the bypass
    // used to call directly.
    EXPECT_TRUE(skills.isSkillOnCooldown(EQ::kSkillBindWound));
    EXPECT_FALSE(AnyContains(DrainSystemMessages(bridge), "Your class cannot use"));
}

TEST_F(SkillGateBypassTest, BandageWithNoTargetStillWorks_SelfBandage) {
    // The old EqActionHandler::bandage() fell back to SELF when there was no
    // target, rather than refusing like /assist does. That rule now lives
    // once, in sendBodiedSkillPacket()'s case 9 -- and it must still hold.
    auto& skills = Skills(kWarrior);
    ASSERT_FALSE(eq.GetCombatManager()->HasTarget());
    DrainSystemMessages(bridge);

    handler.bandage(0);

    EXPECT_TRUE(skills.isSkillOnCooldown(EQ::kSkillBindWound))
        << "a targetless /bandage must still bandage yourself";
    EXPECT_FALSE(AnyContains(DrainSystemMessages(bridge), "You must have a target"));
}

// ===================================================================
// 6. /track — the bypass that cannot be routed through activateSkill()
// ===================================================================
//
// HC_OP_TrackTarget is not skill 53's opcode (that is HC_OP_Track), so this
// one is gated by CALLING SkillManager::hasSkill(), the gate's own
// predicate, rather than by routing. The wording is T159's, unchanged.

TEST_F(SkillGateBypassTest, RogueTrackCommandIsRefusedWithTheSameWording) {
    Skills(kRogue);
    TargetAnNpc();
    DrainSystemMessages(bridge);

    handler.trackTarget(0);              // == /track

    auto msgs = DrainSystemMessages(bridge);
    EXPECT_TRUE(AnyContains(msgs, "Cannot use Tracking: Your class cannot use Tracking"))
        << "got " << msgs.size() << " message(s)";
}

TEST_F(SkillGateBypassTest, RangerTrackCommandStillSends) {
    auto& skills = Skills(kRanger);
    const uint16_t target = TargetAnNpc();
    DrainSystemMessages(bridge);

    ASSERT_TRUE(skills.hasSkill(EQ::kSkillTracking));
    handler.trackTarget(target);

    auto msgs = DrainSystemMessages(bridge);
    EXPECT_FALSE(AnyContains(msgs, "Cannot use Tracking"));
    EXPECT_FALSE(AnyContains(msgs, "You don't have a target."));
}

TEST_F(SkillGateBypassTest, TrackGateRunsBeforeTargetResolution) {
    // A Rogue with no target gets the CLASS reason, not the target reason --
    // the same order the server uses (CanThisClassTrack() is checked before
    // the packet is even sized).
    Skills(kRogue);
    ASSERT_FALSE(eq.GetCombatManager()->HasTarget());
    DrainSystemMessages(bridge);

    handler.trackTarget(0);

    auto msgs = DrainSystemMessages(bridge);
    EXPECT_TRUE(AnyContains(msgs, "Your class cannot use Tracking"));
    EXPECT_FALSE(AnyContains(msgs, "You don't have a target."));
}

// ===================================================================
// 7. The gate fails OPEN, exactly as T159's does
// ===================================================================

TEST_F(SkillGateBypassTest, MissingCapFileLeavesEveryCommandPermitted) {
    auto& table = EQ::SkillCapTable::instance();
    ASSERT_FALSE(table.load(WILLEQ_REPO_PATH("config/no_such_skill_caps.json")));
    ASSERT_FALSE(table.isLoaded());

    auto& skills = Skills(kRogue);
    TargetAnNpc();
    DrainSystemMessages(bridge);

    handler.taunt();
    EXPECT_TRUE(skills.isSkillOnCooldown(EQ::kSkillTaunt))
        << "with no cap data the client must behave as it did before the gate";

    handler.trackTarget(0);
    auto msgs = DrainSystemMessages(bridge);
    EXPECT_FALSE(AnyContains(msgs, "Cannot use Tracking"));
}

}  // namespace
