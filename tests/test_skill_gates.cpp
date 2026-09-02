// T317 — the two skill gates that were not gates.
//
// Before this unit, in src/client/skill/skill_manager.cpp:
//
//   * `checkCombatRequirement()` returned `GetCombatManager()->HasTarget()`
//     with the comment "For now, assume combat skills can be used if we have
//     a target". Every requires_combat skill also sets requires_target, so
//     checkTargetRequirement() had already asked exactly that -- the combat
//     gate could not refuse anything a target check had not already refused.
//   * `checkPositionRequirement()` returned an unconditional `true`, AND had
//     no caller: canUseSkill() never consulted it. Backstab's
//     behind-the-target rule was doubly unreachable.
//   * The matching branches in getSkillUseError() were empty bodies, so
//     canUseSkill() could refuse while getSkillUseError() reported "".
//
// T325 (2026-09-02) REVERTED the combat gate on Will's ruling -- see section
// B1 for the per-opcode server derivation and for which of T317's assertions
// were pinning real behaviour and which were pinning the over-strict gate.
// The facing gate is untouched: it IS server-derived and stays, paired with
// its message exactly as T159 instructed.
//
// Every FACING test below asserts the NEGATIVE -- the gate refusing when it
// should. A test that only proves the allowed case passes on the old code.
//
// The facing rule is a port of eqemu's Mob::BehindMob / Mob::MobAngle
// (akk-stack/code/zone/mob.h:222, zone/mob.cpp:4576), which is what
// Mob::TryBackstab is itself gated on. See client/skill/behind_target.h for
// the heading convention -- only the TARGET's heading is used, so the
// player/NPC heading asymmetry docs/CLAUDE.md warns about never enters.

#include <gtest/gtest.h>

#include "zone_stream_test_utils.h"

#include "client/skill/behind_target.h"
#include "client/skill/skill_cap_table.h"
#include "client/skill/skill_manager.h"
#include "client/combat.h"

#include <cmath>
#include <string>

using namespace zonetest;

namespace {

constexpr const char* kShippedCaps = WILLEQ_REPO_ROOT "/config/skill_caps.json";

constexpr uint8_t kRogue = 9;
constexpr uint8_t kWarrior = 1;
constexpr uint8_t kHuman = 1;
constexpr uint8_t kLevel = 30;

constexpr uint8_t kBackstab = 8;   // requires_target + requires_behind (T325: no combat)
constexpr uint8_t kKick = 30;      // requires_target (T325: no combat)
constexpr uint8_t kApplyPoison = 6;  // no target -- the control

// A NewSpawn at a chosen server position AND heading. zone_stream_test_utils'
// NewSpawnPayloadAt() hardcodes heading 0, and heading is the whole point here.
std::vector<uint8_t> SpawnWithHeading(uint32_t spawn_id, const std::string& name,
                                       int32_t server_x, int32_t server_y,
                                       uint32_t raw_heading) {
    return BuildPayload(387, [&](EQ::Net::DynamicPacket& p) {
        WriteSpawnStruct(p, 2, spawn_id, name, /*level=*/30, /*race=*/54,
                          /*char_class=*/1, /*gender=*/0, /*npc_type=*/1,
                          /*hp_percent=*/100,
                          EncodePos(server_x, server_y, 0, raw_heading));
    });
}

// Spawn_Struct heading is 11 bits and the client reads it as
// (raw / 4) * 360 / 512 degrees, i.e. raw == degrees * 2048 / 360.
uint32_t RawHeadingForDegrees(float degrees) {
    return static_cast<uint32_t>(std::lround(degrees * 2048.0 / 360.0)) & 0x7FFu;
}

// ===================================================================
// A. The ported predicate itself, in server coordinates
// ===================================================================

// eqemu rotates the target's heading by +90 before building its facing
// vector, and works in a mirrored-x space. Rather than re-derive that here,
// these cases fix the target at the origin facing server-heading 0 and check
// the four cardinal directions plus the 90-degree boundary.
class BehindTargetMathTest : public ::testing::Test {
protected:
    // Target at server (0,0) with the given heading in degrees; attacker at
    // the given server offset.
    static bool Behind(float heading_deg, float ax, float ay) {
        return EQ::skill::IsBehindTarget(0.0f, 0.0f, heading_deg, ax, ay);
    }
    static float Angle(float heading_deg, float ax, float ay) {
        return EQ::skill::MobAngleDegrees(0.0f, 0.0f, heading_deg, ax, ay);
    }
};

TEST_F(BehindTargetMathTest, DeadAheadIsNotBehindAndDeadAsternIs) {
    // Whatever axis "ahead" is for heading 0, the point 10 units that way must
    // be angle 0 and the opposite point angle 180. Find "ahead" by scanning
    // the four cardinals for the one with the smallest angle -- that is the
    // facing direction by construction, and it is the RELATION, not the axis
    // label, that this ports.
    struct P { float x, y; };
    const P dirs[4] = {{10, 0}, {-10, 0}, {0, 10}, {0, -10}};
    int ahead = 0;
    for (int i = 1; i < 4; ++i) {
        if (Angle(0.0f, dirs[i].x, dirs[i].y) < Angle(0.0f, dirs[ahead].x, dirs[ahead].y)) {
            ahead = i;
        }
    }
    const P front = dirs[ahead];
    const P back{-front.x, -front.y};

    EXPECT_NEAR(Angle(0.0f, front.x, front.y), 0.0f, 0.5f);
    EXPECT_NEAR(Angle(0.0f, back.x, back.y), 180.0f, 0.5f);

    EXPECT_FALSE(Behind(0.0f, front.x, front.y));
    EXPECT_TRUE(Behind(0.0f, back.x, back.y));

    // The two flanks are the 90-degree razor edge. Measured here, they come
    // out at 89.9999924 and just over 90 -- they STRADDLE the threshold by
    // about 1e-5 of a degree, because eqemu builds the facing vector from
    // `heading * 3.1415f / 180.0f` (3.1415, not pi), so the rotation is a
    // hair off square and the dot product never lands on exactly zero.
    //
    // Which side a dead flank falls on is therefore float noise -- IN THE
    // SERVER TOO. That is precisely why this port keeps eqemu's 3.1415 and
    // its operation order instead of "fixing" them to M_PI: running the
    // identical arithmetic is what makes the client land on the same side of
    // the threshold as the server. Do not tidy the constant. All this test
    // can honestly assert is that we are within noise of 90, and that
    // IsBehindTarget is exactly `MobAngle > 90` with no separate epsilon of
    // its own.
    for (int i = 0; i < 4; ++i) {
        if (i == ahead) continue;
        if (dirs[i].x == back.x && dirs[i].y == back.y) continue;
        const float a90 = Angle(0.0f, dirs[i].x, dirs[i].y);
        EXPECT_NEAR(a90, 90.0f, 0.01f);
        EXPECT_EQ(Behind(0.0f, dirs[i].x, dirs[i].y), a90 > 90.0f)
            << "IsBehindTarget must be exactly `MobAngle > 90`, boundary included";
    }
}

TEST_F(BehindTargetMathTest, TurningTheTargetAroundFlipsTheAnswer) {
    // Same attacker position, target rotated 180 degrees: behind becomes in
    // front. This is the property that makes the check worth having -- it
    // depends on the TARGET's heading, not on where either party stands.
    const float ax = 10.0f, ay = 0.0f;
    const bool at_zero = Behind(0.0f, ax, ay);
    const bool at_180 = Behind(180.0f, ax, ay);
    EXPECT_NE(at_zero, at_180);
}

TEST_F(BehindTargetMathTest, HeadingWrapAtThe270Boundary) {
    // eqemu's rotation branches at 270 (`if (heading < 270) +90 else -270`).
    // 269.9 and 270.1 must not disagree about anything but rounding.
    for (float ax : {10.0f, -10.0f, 0.0f}) {
        for (float ay : {10.0f, -10.0f, 0.0f}) {
            if (ax == 0.0f && ay == 0.0f) continue;
            EXPECT_NEAR(Angle(269.9f, ax, ay), Angle(270.1f, ax, ay), 1.0f)
                << "ax=" << ax << " ay=" << ay;
        }
    }
}

TEST_F(BehindTargetMathTest, SamePointIsNotBehind_WhereEqemuWouldDivideByZero) {
    // The one deliberate departure from the port.
    EXPECT_FLOAT_EQ(Angle(0.0f, 0.0f, 0.0f), 0.0f);
    EXPECT_FALSE(Behind(0.0f, 0.0f, 0.0f));
    EXPECT_FALSE(std::isnan(Angle(123.0f, 0.0f, 0.0f)));
}

// ===================================================================
// B. The gates, through the real SkillManager
// ===================================================================

class SkillGateTest : public ::testing::Test {
protected:
    SkillGateTest() : eq(MakeEq()) { eq.setBridge(&bridge); }
    ~SkillGateTest() override { eq.setBridge(nullptr); }

    void SetUp() override {
        ASSERT_TRUE(EQ::SkillCapTable::instance().load(kShippedCaps));
    }

    EQ::SkillManager& Skills(uint8_t player_class) {
        auto* sm = eq.GetSkillManager();
        EXPECT_NE(sm, nullptr);
        sm->initialize(player_class, kHuman, kLevel);
        return *sm;
    }

    // Spawns and targets an NPC, and returns its id. Heading in DEGREES on
    // the server's circle; position in server x/y.
    uint16_t TargetNpc(int32_t server_x, int32_t server_y, float heading_deg,
                        uint16_t spawn_id = 500) {
        ReplayZone(eq, HC_OP_NewSpawn,
                   SpawnWithHeading(spawn_id, "a_probe_dummy", server_x, server_y,
                                     RawHeadingForDegrees(heading_deg)));
        EXPECT_TRUE(eq.GetCombatManager()->SetTarget(spawn_id));
        bridge.drainEvents();
        return spawn_id;
    }

    void EngageCombat() { eq.GetCombatManager()->EnableAutoAttack(); }

    EverQuest eq;
    TestBridge bridge;
};

// Finds a server-space attacker offset that IS behind a target facing
// `heading_deg`, and one that is not, using the ported predicate itself as
// the oracle -- so these tests pin SkillManager's WIRING (swap, heading
// source, call site), not the arithmetic, which section A pins directly.
struct Offsets { int32_t behind_x, behind_y, front_x, front_y; };
Offsets FindOffsets(float heading_deg) {
    Offsets o{0, 0, 0, 0};
    const int32_t candidates[4][2] = {{80, 0}, {-80, 0}, {0, 80}, {0, -80}};
    for (auto& c : candidates) {
        const bool behind = EQ::skill::IsBehindTarget(
            0.0f, 0.0f, heading_deg, static_cast<float>(c[0]), static_cast<float>(c[1]));
        if (behind) {
            o.behind_x = c[0];
            o.behind_y = c[1];
        } else if (EQ::skill::MobAngleDegrees(0.0f, 0.0f, heading_deg,
                                               static_cast<float>(c[0]),
                                               static_cast<float>(c[1])) < 45.0f) {
            o.front_x = c[0];
            o.front_y = c[1];
        }
    }
    return o;
}

// ---- B1. The combat gate ------------------------------------------------
//
// T325 (Will's ruling, 2026-09-02): the combat gate is REVERTED, by clearing
// requires_combat in skill_data.h rather than by re-hollowing
// checkCombatRequirement(). Server evidence, re-derived per opcode:
//   * 8/10/21/23/26/30/38/52 -> OP_CombatAbility -> Client::OPCombatAbility
//     (zone/special_attacks.cpp:325)
//   * 16 -> OP_Disarm -> Handle_OP_Disarm (client_packet.cpp:5749)
//   * 71 -> OP_InstillDoubt -> Handle_OP_InstillDoubt (:9095) -> InstillDoubt
// NONE of the three carries an in-combat requirement, so all eleven flags were
// cleared and none was kept.
//
// The tests below are in two groups, and the distinction matters:
//   T325_*            -- the new contract. These FAIL on T317's code.
//   MechanismIsIntact -- the predicate and its refusal message, exercised by
//                        setting the flag by hand. Nothing shipped sets it, so
//                        without these the mechanism T317 built (and the
//                        "You must be in combat" string) would be untested and
//                        free to rot into the no-op it used to be.

TEST_F(SkillGateTest, T325_AnOpeningKickIsPermittedWithATargetAndNoCombat) {
    // THE acceptance case, and the exact state T317's gate refused. eqemu's
    // OPCombatAbility would have accepted this send.
    auto& skills = Skills(kWarrior);
    TargetNpc(0, 0, 0.0f);

    ASSERT_TRUE(eq.GetCombatManager()->HasTarget());
    ASSERT_FALSE(eq.GetCombatManager()->IsAutoAttackEnabled());
    ASSERT_FALSE(eq.GetCombatManager()->IsAutoFireEnabled());
    ASSERT_EQ(eq.GetCombatManager()->GetCombatState(), COMBAT_STATE_IDLE);

    EXPECT_TRUE(skills.canUseSkill(kKick));
    EXPECT_EQ(skills.getSkillUseError(kKick), "");
    EXPECT_TRUE(skills.activateSkill(kKick, 0));
    EXPECT_TRUE(skills.isSkillOnCooldown(kKick))
        << "an allowed activation must have reached sendSkillUsePacket()";
}

TEST_F(SkillGateTest, T325_AnOpeningBackstabFromStealthIsPermitted) {
    // The rogue's standard opener: behind the target, auto-attack off, nothing
    // has aggro on us yet. T317's gate refused it with "You must be in combat";
    // the server has no such rule (Mob::TryBackstab checks the weapon and
    // BehindMob, nothing else).
    const Offsets off = FindOffsets(0.0f);
    ASSERT_NE(off.behind_x + off.behind_y, 0) << "test setup: no behind offset found";

    auto& skills = Skills(kRogue);
    TargetNpc(-off.behind_x, -off.behind_y, /*heading_deg=*/0.0f);

    ASSERT_EQ(eq.GetCombatManager()->GetCombatState(), COMBAT_STATE_IDLE);
    ASSERT_FALSE(eq.GetCombatManager()->IsRecentAttacker(500));

    EXPECT_TRUE(skills.canUseSkill(kBackstab));
    EXPECT_EQ(skills.getSkillUseError(kBackstab), "");
    EXPECT_TRUE(skills.activateSkill(kBackstab, 0));
    EXPECT_TRUE(skills.isSkillOnCooldown(kBackstab));
}

TEST_F(SkillGateTest, T325_NoShippedSkillCarriesTheCombatFlag) {
    // Pins the ruling itself across the whole table, so re-adding the flag to
    // any skill is a deliberate act with a test to answer to -- not a quiet
    // edit in a switch statement.
    auto& skills = Skills(kRogue);
    for (uint8_t id = 0; id < EQT::MAX_PP_SKILL; ++id) {
        const auto* data = skills.getSkill(id);
        if (!data) {
            continue;
        }
        EXPECT_FALSE(data->requires_combat)
            << "skill " << static_cast<int>(id) << " (" << data->name
            << ") sets requires_combat; T325 cleared all eleven. If a server "
               "handler really does require combat, say which and why.";
    }
}

TEST_F(SkillGateTest, T325_TheElevenT317FlaggedStillRequireATarget) {
    // The revert clears requires_combat ONLY. Every one of those eqemu handlers
    // genuinely needs a target, so requires_target must survive it.
    auto& skills = Skills(kRogue);
    for (uint8_t id : {8, 10, 16, 21, 23, 26, 30, 38, 52, 71}) {
        const auto* data = skills.getSkill(id);
        ASSERT_NE(data, nullptr) << "skill " << static_cast<int>(id);
        EXPECT_TRUE(data->requires_target) << "skill " << static_cast<int>(id);
    }
}

TEST_F(SkillGateTest, ATargetIsStillRequired) {
    // The gate that DID have a server counterpart is untouched: no target, no
    // Kick. (Real behaviour, unchanged by T325.)
    auto& skills = Skills(kWarrior);
    ASSERT_EQ(eq.GetCombatManager()->GetTargetId(), 0);
    EXPECT_FALSE(skills.canUseSkill(kKick));
    EXPECT_EQ(skills.getSkillUseError(kKick), "You must have a target");
}

TEST_F(SkillGateTest, EngagingAutoAttackDoesNotChangeTheAnswerEither) {
    // Real behaviour before and after: in combat, Kick is allowed. It is now
    // allowed both ways round, which is the point.
    auto& skills = Skills(kWarrior);
    TargetNpc(0, 0, 0.0f);
    EXPECT_TRUE(skills.canUseSkill(kKick));

    EngageCombat();
    ASSERT_EQ(eq.GetCombatManager()->GetCombatState(), COMBAT_STATE_ENGAGED);
    EXPECT_TRUE(skills.canUseSkill(kKick));

    eq.GetCombatManager()->DisableAutoAttack();
    EXPECT_TRUE(skills.canUseSkill(kKick))
        << "disengaging must no longer take the skill away";
    EXPECT_EQ(skills.getSkillUseError(kKick), "");
}

TEST_F(SkillGateTest, SkillsWithoutTheCombatFlagAreUntouched) {
    auto& skills = Skills(kRogue);
    ASSERT_EQ(eq.GetCombatManager()->GetCombatState(), COMBAT_STATE_IDLE);
    EXPECT_TRUE(skills.canUseSkill(kApplyPoison));
    EXPECT_EQ(skills.getSkillUseError(kApplyPoison), "");
}

// ---- B1b. The mechanism, still whole ------------------------------------

TEST_F(SkillGateTest, MechanismIsIntact_TheFlagStillRefusesAndStillSaysWhy) {
    // checkCombatRequirement() must NOT be re-hollowed to `return true` -- that
    // is the no-op gate T317 was dispatched to fix. Set the flag by hand (the
    // same public mutable accessor board item 35's endurance case uses) and the
    // predicate must still refuse, with the paired message rather than "".
    auto& skills = Skills(kWarrior);
    TargetNpc(0, 0, 0.0f);
    auto* data = skills.getSkill(kKick);
    ASSERT_NE(data, nullptr);
    data->requires_combat = true;

    EXPECT_FALSE(skills.canUseSkill(kKick));
    EXPECT_EQ(skills.getSkillUseError(kKick), "You must be in combat");
    EXPECT_FALSE(skills.activateSkill(kKick, 0));
    EXPECT_FALSE(skills.isSkillOnCooldown(kKick));

    EngageCombat();
    EXPECT_TRUE(skills.canUseSkill(kKick));
    EXPECT_EQ(skills.getSkillUseError(kKick), "");
}

TEST_F(SkillGateTest, MechanismIsIntact_BeingAttackedByTheTargetSatisfiesIt) {
    // T247's aggro memory branch, still live.
    auto& skills = Skills(kWarrior);
    const uint16_t npc = TargetNpc(0, 0, 0.0f);
    auto* data = skills.getSkill(kKick);
    ASSERT_NE(data, nullptr);
    data->requires_combat = true;
    ASSERT_FALSE(skills.canUseSkill(kKick));

    eq.GetCombatManager()->NoteAttackedBy(npc);
    EXPECT_TRUE(eq.GetCombatManager()->IsRecentAttacker(npc));
    EXPECT_TRUE(skills.canUseSkill(kKick));
}

TEST_F(SkillGateTest, MechanismIsIntact_AggroFromSomeoneElseDoesNotSatisfyIt) {
    // The aggro branch is scoped to the CURRENT TARGET on purpose.
    auto& skills = Skills(kWarrior);
    TargetNpc(0, 0, 0.0f, /*spawn_id=*/500);
    auto* data = skills.getSkill(kKick);
    ASSERT_NE(data, nullptr);
    data->requires_combat = true;

    eq.GetCombatManager()->NoteAttackedBy(/*some other spawn=*/999);
    EXPECT_FALSE(skills.canUseSkill(kKick));
    EXPECT_EQ(skills.getSkillUseError(kKick), "You must be in combat");
}

// ---- B2. The facing gate ------------------------------------------------

TEST_F(SkillGateTest, NEGATIVE_BackstabIsRefusedFromTheFrontOfTheTarget) {
    // The whole point of item 7: before this, checkPositionRequirement()
    // returned true unconditionally AND had no caller, so the client sent
    // Backstab attempts the server refuses.
    const float heading = 0.0f;
    const Offsets off = FindOffsets(heading);
    ASSERT_NE(off.front_x + off.front_y, 0) << "test setup: no front offset found";

    auto& skills = Skills(kRogue);
    // Player sits at the origin (MakeEq default); put the TARGET where the
    // player is in front of it. Client entity.x = server_y, so the spawn's
    // server position is the negation of the offset from the target.
    TargetNpc(/*server_x=*/-off.front_x, /*server_y=*/-off.front_y, heading);
    EngageCombat();

    EXPECT_FALSE(skills.canUseSkill(kBackstab));
    EXPECT_EQ(skills.getSkillUseError(kBackstab), "You must be behind your target");
    EXPECT_FALSE(skills.activateSkill(kBackstab, 0));
    EXPECT_FALSE(skills.isSkillOnCooldown(kBackstab));
}

TEST_F(SkillGateTest, BackstabIsAllowedFromBehindTheTarget) {
    const float heading = 0.0f;
    const Offsets off = FindOffsets(heading);
    ASSERT_NE(off.behind_x + off.behind_y, 0) << "test setup: no behind offset found";

    auto& skills = Skills(kRogue);
    TargetNpc(/*server_x=*/-off.behind_x, /*server_y=*/-off.behind_y, heading);
    EngageCombat();

    EXPECT_TRUE(skills.canUseSkill(kBackstab));
    EXPECT_EQ(skills.getSkillUseError(kBackstab), "");
}

TEST_F(SkillGateTest, TheGateFollowsTheTargetsHeading_NotThePlayers) {
    // Same positions, target spun 180 degrees: the answer must flip. This is
    // the assertion that would catch someone "harmonising" the heading
    // conventions -- see docs/CLAUDE.md's Heading/Rotation Conventions.
    const Offsets off = FindOffsets(0.0f);
    ASSERT_NE(off.behind_x + off.behind_y, 0);

    auto& skills = Skills(kRogue);
    TargetNpc(-off.behind_x, -off.behind_y, /*heading_deg=*/0.0f, /*spawn_id=*/500);
    EngageCombat();
    const bool behind_at_zero = skills.canUseSkill(kBackstab);

    TargetNpc(-off.behind_x, -off.behind_y, /*heading_deg=*/180.0f, /*spawn_id=*/501);
    const bool behind_at_180 = skills.canUseSkill(kBackstab);

    EXPECT_TRUE(behind_at_zero);
    EXPECT_NE(behind_at_zero, behind_at_180);
}

TEST_F(SkillGateTest, SkillsWithoutTheBehindFlagAreUntouchedByFacing) {
    auto& skills = Skills(kWarrior);
    const Offsets off = FindOffsets(0.0f);
    TargetNpc(-off.front_x, -off.front_y, 0.0f);
    EngageCombat();
    EXPECT_TRUE(skills.canUseSkill(kKick));
    EXPECT_EQ(skills.getSkillUseError(kKick), "");
}

TEST_F(SkillGateTest, AnUnknownTargetDoesNotProduceAFacingComplaint) {
    // Target id set, but no such entity in the map (out of range / not
    // spawned). Refusing here would report facing for a missing spawn.
    auto& skills = Skills(kRogue);
    TargetNpc(0, 0, 0.0f, /*spawn_id=*/500);
    EngageCombat();
    ReplayZone(eq, HC_OP_DeleteSpawn, BuildPayload(6, [](EQ::Net::DynamicPacket& p) {
        p.PutUInt32(2, 500);
    }));
    if (eq.GetCombatManager()->HasTarget()) {
        EXPECT_NE(skills.getSkillUseError(kBackstab), "You must be behind your target");
    }
}

// ---- B3. The T159 case: endurance must not be blamed on facing ----------

TEST_F(SkillGateTest, T159_ABackstabShortOfEnduranceBlamesEnduranceNotFacing) {
    // T159 deleted a message that said "You must be behind your target" for a
    // failure that was actually a resource failure, and left the instruction
    // that the check and the message go back in TOGETHER. They now have --
    // and this is the case that proves the message did not come back as the
    // same lie. The player is FACING THE TARGET (so facing really is wrong)
    // and also out of endurance; endurance wins.
    const Offsets off = FindOffsets(0.0f);
    ASSERT_NE(off.front_x + off.front_y, 0);

    auto& skills = Skills(kRogue);
    TargetNpc(-off.front_x, -off.front_y, 0.0f);
    EngageCombat();

    // endurance_cost is 0 for every skill in initializeSkillData() today, so
    // the resource gate can never fire from shipped data -- itself worth
    // knowing. Set it through the public mutable accessor to reach the branch.
    auto* data = skills.getSkill(kBackstab);
    ASSERT_NE(data, nullptr);
    data->endurance_cost = 1000000;
    ASSERT_LT(eq.GetCurrentEndurance(), 1000000u);

    EXPECT_EQ(skills.getSkillUseError(kBackstab), "Not enough endurance")
        << "T159's exact defect: an endurance failure reported as a facing failure";

    // And with endurance restored, the facing complaint is the true one.
    data->endurance_cost = 0;
    EXPECT_EQ(skills.getSkillUseError(kBackstab), "You must be behind your target");
}

}  // namespace
