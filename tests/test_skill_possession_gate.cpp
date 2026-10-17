// T159 — the skill possession gate, and the real cap table under it.
//
// T158 watched all three of these on a live server:
//
//   * `Oraclerogue`, a Rogue with Taunt at 0, ran /useskill taunt. The client
//     sent OP_Taunt, printed "You use Taunt!", and the server discarded the
//     packet in silence at Handle_OP_Taunt's HasSkill() guard. `skill_caps`
//     has NO row for (class 9, skill 73) at any level -- a Rogue can never
//     taunt. That is the case the gate must refuse.
//   * The same Rogue, at level 10 with Apply Poison at 0, applied poison
//     SUCCESSFULLY -- the rogue cap for skill 6 only starts at level 18, but
//     Handle_OP_ApplyPoison never consults HasSkill; it checks the item and
//     GetClass() == Rogue. That is the case the gate must keep working, and it
//     is why the gate is keyed on "can my class EVER have this skill" rather
//     than on "cap at my current level > 0". A level-gated check would have
//     refused a send that demonstrably works.
//   * `Testmelee`, a Warrior, taunted and got a server string back in 52 ms.
//
// The gate's data is the SHIPPED config/skill_caps.json, loaded here by path
// from WILLEQ_REPO_ROOT -- never a fixture invented in this file. A synthetic
// cap table would pass against a client that ships a broken one.
//
// New file rather than growth of tests/test_command_processor.cpp, which is
// already past the 3000-line FAIL ceiling.

#include <gtest/gtest.h>

#include "zone_stream_test_utils.h"

#include "client/skill/skill_cap_table.h"
#include "client/skill/skill_manager.h"
#include "client/combat.h"

#include <string>
#include <vector>

using namespace zonetest;

namespace {

const char* const kShippedCaps = WILLEQ_REPO_PATH("config/skill_caps.json");  // B1090: run-time root

// Class ids as the server (and the PlayerProfile) numbers them.
constexpr uint8_t kWarrior = 1;
constexpr uint8_t kRogue = 9;
constexpr uint8_t kHuman = 1;

// Skill ids.
constexpr uint8_t kApplyPoison = 6;
constexpr uint8_t kTaunt = 73;

// T158's exact facts, restated as constants so a data-file change that moves
// them fails a test instead of silently changing what these tests mean.
constexpr uint8_t kRogueApplyPoisonFirstLevel = 18;
constexpr uint8_t kProbeLevel = 10;  // Oraclerogue's level: BELOW 18, on purpose.

// Every SystemMessage the bridge has queued, as plain strings.
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

// ===================================================================
// Fixture
// ===================================================================

// The cap table is a process-wide singleton, so every fixture reloads the
// shipped file in SetUp and again in TearDown -- the fallback test below
// deliberately breaks it, and nothing may inherit that.
class SkillPossessionGateTest : public ::testing::Test {
protected:
    SkillPossessionGateTest() : eq(MakeEq()) { eq.setBridge(&bridge); }
    ~SkillPossessionGateTest() override { eq.setBridge(nullptr); }

    void SetUp() override {
        ASSERT_TRUE(EQ::SkillCapTable::instance().load(kShippedCaps))
            << "the shipped config/skill_caps.json must load: " << kShippedCaps;
    }
    void TearDown() override {
        EQ::SkillCapTable::instance().load(kShippedCaps);
    }

    // Spawns an NPC and makes it the current target, the way canUseSkill()
    // reads it (CombatManager::GetTargetId()).
    uint16_t TargetAnNpc(uint16_t spawn_id = 500) {
        EXPECT_NO_THROW(ReplayZone(eq, HC_OP_NewSpawn,
            NewSpawnPayloadAt(spawn_id, "a_probe_dummy", 0, 10, 0)));
        auto* cm = eq.GetCombatManager();
        EXPECT_TRUE(cm->SetTarget(spawn_id));
        return spawn_id;
    }

    EQ::SkillManager& Skills(uint8_t player_class, uint8_t level) {
        auto* sm = eq.GetSkillManager();
        EXPECT_NE(sm, nullptr);
        sm->initialize(player_class, kHuman, level);
        return *sm;
    }

    EverQuest eq;
    TestBridge bridge;
};

// ===================================================================
// 1. The data is the shipped file, and it says what T158 observed
// ===================================================================

TEST_F(SkillPossessionGateTest, ShippedCapTableCarriesTheThreeT158Facts) {
    auto& table = EQ::SkillCapTable::instance();

    ASSERT_TRUE(table.isLoaded());
    // 603 (class, skill) rows, collapsed from 58,359 raw peq.skill_caps rows.
    EXPECT_EQ(table.entryCount(), 603u);

    // A Rogue can never taunt -- no row at ANY level. This absence is the
    // whole gate.
    EXPECT_EQ(table.getEntry(kRogue, kTaunt), nullptr);
    EXPECT_FALSE(table.classCanEverHaveSkill(kRogue, kTaunt));

    // A Rogue CAN have Apply Poison -- from level 18, with a lifetime cap of
    // 200. The level is loaded but is deliberately not a gate.
    const auto* poison = table.getEntry(kRogue, kApplyPoison);
    ASSERT_NE(poison, nullptr);
    EXPECT_EQ(poison->first_level, kRogueApplyPoisonFirstLevel);
    EXPECT_EQ(poison->max_cap, 200u);
    EXPECT_TRUE(table.classCanEverHaveSkill(kRogue, kApplyPoison));

    // A Warrior can taunt from level 1.
    const auto* taunt = table.getEntry(kWarrior, kTaunt);
    ASSERT_NE(taunt, nullptr);
    EXPECT_EQ(taunt->first_level, 1u);
    EXPECT_TRUE(table.classCanEverHaveSkill(kWarrior, kTaunt));
}

// ===================================================================
// 2. ACCEPTANCE — the live T158 case: a Rogue cannot activate Taunt
// ===================================================================

TEST_F(SkillPossessionGateTest, RogueCannotActivateTaunt_AndNothingIsSent) {
    auto& skills = Skills(kRogue, kProbeLevel);
    const uint16_t target = TargetAnNpc();
    skills.updateSkill(kTaunt, 0);  // Oraclerogue's actual value.
    DrainSystemMessages(bridge);    // clear spawn/target chatter

    // The gate itself.
    EXPECT_FALSE(skills.hasSkill(kTaunt));
    EXPECT_FALSE(skills.canUseSkill(kTaunt));
    EXPECT_EQ(skills.getSkillUseError(kTaunt), "Your class cannot use Taunt");

    // The activation, with a live target so nothing else can be the reason.
    EXPECT_FALSE(skills.activateSkill(kTaunt, target));

    // Nothing was sent: activateSkill() marks the skill used (starting its
    // 5s Taunt recast) ONLY on the branch that reaches sendSkillUsePacket().
    // A cold cooldown here is the witness that the send branch never ran.
    EXPECT_FALSE(skills.isSkillOnCooldown(kTaunt));
    EXPECT_EQ(skills.getSkillCooldownRemaining(kTaunt), 0u);

    // And the player is told something TRUE. Before T159 this path printed
    // "You use Taunt!" for a packet that went nowhere.
    auto msgs = DrainSystemMessages(bridge);
    EXPECT_TRUE(AnyContains(msgs, "Cannot use Taunt: Your class cannot use Taunt"))
        << "expected a truthful refusal, got " << msgs.size() << " message(s)";
    EXPECT_FALSE(AnyContains(msgs, "You use Taunt!"));
}

// ===================================================================
// 3. ACCEPTANCE — the counter-example: Apply Poison still works
// ===================================================================

TEST_F(SkillPossessionGateTest, RogueCanActivateApplyPoisonAtLevel10WithSkillZero) {
    auto& skills = Skills(kRogue, kProbeLevel);
    skills.updateSkill(kApplyPoison, 0);  // Oraclerogue's actual value.
    DrainSystemMessages(bridge);

    // The exact configuration a level gate would have refused: the class's
    // first_level for this skill is 18 and the player is 10, with the skill
    // at 0. T158 proved the server does not care.
    const auto* data = skills.getSkill(kApplyPoison);
    ASSERT_NE(data, nullptr);
    EXPECT_EQ(data->class_first_level, kRogueApplyPoisonFirstLevel);
    EXPECT_LT(kProbeLevel, kRogueApplyPoisonFirstLevel);
    EXPECT_EQ(data->current_value, 0u);

    EXPECT_TRUE(skills.hasSkill(kApplyPoison));
    EXPECT_TRUE(skills.canUseSkill(kApplyPoison));
    EXPECT_EQ(skills.getSkillUseError(kApplyPoison), "");
    EXPECT_TRUE(skills.activateSkill(kApplyPoison, 0));

    EXPECT_FALSE(AnyContains(DrainSystemMessages(bridge), "Your class cannot use"));
}

// ===================================================================
// 4. ACCEPTANCE — a Warrior CAN taunt
// ===================================================================

TEST_F(SkillPossessionGateTest, WarriorCanActivateTaunt) {
    auto& skills = Skills(kWarrior, kProbeLevel);
    const uint16_t target = TargetAnNpc();
    skills.updateSkill(kTaunt, 0);  // value irrelevant: the gate is on class
    DrainSystemMessages(bridge);

    EXPECT_TRUE(skills.hasSkill(kTaunt));
    EXPECT_TRUE(skills.canUseSkill(kTaunt));
    EXPECT_EQ(skills.getSkillUseError(kTaunt), "");
    EXPECT_TRUE(skills.activateSkill(kTaunt, target));

    // The send branch ran: it is the only thing that starts the recast.
    EXPECT_TRUE(skills.isSkillOnCooldown(kTaunt));
    EXPECT_TRUE(AnyContains(DrainSystemMessages(bridge), "You use Taunt!"));
}

// ===================================================================
// 5. The display predicate did NOT move
// ===================================================================

TEST_F(SkillPossessionGateTest, DisplayListingStillShowsSkillsTheClassCannotHave) {
    auto& skills = Skills(kRogue, kProbeLevel);

    // getAllSkills()/getSkillCount() answer "what should the skills window
    // list", which is every valid skill id -- unchanged by T159. Only the
    // possession query narrowed.
    EXPECT_EQ(skills.getSkillCount(), 75u);

    bool taunt_listed = false;
    for (const auto* s : skills.getAllSkills()) {
        if (s && s->skill_id == kTaunt) {
            taunt_listed = true;
            EXPECT_TRUE(s->isDisplayableSkill());
            EXPECT_FALSE(s->canClassHaveSkill());  // ...but not possessed
            EXPECT_EQ(s->max_value, 0u);           // no class cap
        }
    }
    EXPECT_TRUE(taunt_listed) << "the display test must not be narrowed by the gate";
}

// ===================================================================
// 6. Real per-class caps replaced the stub's eight one-size-fits-all numbers
// ===================================================================

TEST_F(SkillPossessionGateTest, CapsAreRealPerClassValuesNotTheStubsInventions) {
    auto& warrior = Skills(kWarrior, kProbeLevel);
    const auto& table = EQ::SkillCapTable::instance();

    // The pre-T159 stub handed EVERY class the same eight numbers: level*5 for
    // Defense/Dodge/Offense/BindWound and a flat 200 for Swimming/SenseHeading/
    // AlcoholTolerance/Begging. Every one of those is now a per-class value out
    // of the file, and four of them are nowhere near what the stub said.
    struct Case { uint8_t skill; uint32_t stub_said; };
    const Case cases[] = {
        {15, kProbeLevel * 5u},  // Defense          stub 50,  file 390
        {19, kProbeLevel * 5u},  // Dodge            stub 50,  file 480
        {9,  kProbeLevel * 5u},  // Bind Wound       stub 50,  file 525
        {66, 200u},              // Alcohol Tolerance stub 200, file 525
    };
    for (const auto& c : cases) {
        const auto* entry = table.getEntry(kWarrior, c.skill);
        ASSERT_NE(entry, nullptr) << "skill " << int(c.skill);
        EXPECT_EQ(warrior.getSkillMax(c.skill), entry->max_cap) << "skill " << int(c.skill);
        EXPECT_NE(warrior.getSkillMax(c.skill), c.stub_said) << "skill " << int(c.skill);
    }

    // And updateSkill() no longer patches max_value up to whatever the server
    // last sent -- that echo is fallback-only now. A Rogue has no Taunt row, so
    // a stray Taunt value must not manufacture a cap for one.
    auto& rogue = Skills(kRogue, kProbeLevel);
    ASSERT_EQ(rogue.getSkillMax(kTaunt), 0u);
    rogue.updateSkill(kTaunt, 42);
    EXPECT_EQ(rogue.getSkillMax(kTaunt), 0u);
    EXPECT_EQ(rogue.getSkillValue(kTaunt), 42u);
}

// ===================================================================
// 7. A data regression must not brick the skill system
// ===================================================================

TEST_F(SkillPossessionGateTest, MissingCapFileFallsBackToPermissiveBehaviour) {
    auto& table = EQ::SkillCapTable::instance();
    ASSERT_FALSE(table.load(WILLEQ_REPO_PATH("config/no_such_skill_caps.json")));
    ASSERT_FALSE(table.isLoaded());

    // Permissive: pre-T159 behaviour, i.e. no possession gate at all. Wrong,
    // but wrong in the direction that leaves the client playable.
    EXPECT_TRUE(table.classCanEverHaveSkill(kRogue, kTaunt));

    auto& skills = Skills(kRogue, kProbeLevel);
    const uint16_t target = TargetAnNpc();
    EXPECT_TRUE(skills.hasSkill(kTaunt));
    EXPECT_TRUE(skills.canUseSkill(kTaunt));
    EXPECT_TRUE(skills.activateSkill(kTaunt, target));

    // The echo patch-up comes back with it, since it is the only cap source
    // left when the file is gone.
    skills.updateSkill(kTaunt, 77);
    EXPECT_EQ(skills.getSkillMax(kTaunt), 77u);
}

}  // namespace
