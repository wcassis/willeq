// T314 -- a group spell affects the CASTER'S GROUP, not whoever happens to be
// standing nearby.
//
// SpellTypeProcessor::findGroupTargets() used to carry this:
//
//     // TODO: When group system is implemented, get actual group member IDs
//     // For now, find nearby players within range as a placeholder
//     ...
//             // TODO: Check if actually in group
//             // For now, just check range
//
// The premise was stale -- eqt::state::GroupState is a live domain in the
// single GameState (T286/T288) -- and the consequence was user-visible in the
// most ordinary situation the game has: standing in a crowd. A group heal
// landed on strangers next to the caster and MISSED group mates out of range.
//
// The two assertions that matter here are the NEGATIVE ones. A test that only
// proves group members are targeted passes on the broken code too, because the
// broken code targeted everybody. So every case below asserts who is NOT in
// the target set.
//
// Range decision (pinned): membership AND range, not membership INSTEAD OF
// range. eqemu Group::CastGroupSpell() (zone/groups.cpp:813) casts on the
// caster's own slot unconditionally and range-checks every other member
// against Mob::GetAOERange() -- logging "Group spell: [x] is out of range".
// GetAOERange() (zone/spells.cpp:5666) is aoe_range, else range, else 10;
// SpellTypeProcessor::groupSpellRadius() is that function, and the radius
// tests at the bottom pin it.
//
// Seam: a bare EverQuest constructs no SpellTypeProcessor (that happens in
// InitializeSpellSystem, which needs the SQLite spell DB), but the processor
// only reaches the client through its EverQuest* for this path, so these
// tests construct one directly against a live EverQuest and drive group
// membership the way the server does -- by replaying OP_GroupUpdate. No
// network, no graphics, no live server.

#include <gtest/gtest.h>

#include "client/spell/spell_type_processor.h"
#include "zone_stream_test_utils.h"

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

using namespace zonetest;
using namespace EQ;

namespace {

// One player spawn `world_units` away from the origin along the server's y
// axis. npc_type 0 == player, which is what findGroupTargets() filters on
// before it ever looks at the group.
//
// Spawn_Struct positions are eighths of a world unit (eq_entities.cpp divides
// the 19-bit field by 8), so the packet carries 8x the distance we mean --
// a spawn written as "400" would land 50 units away, comfortably inside a
// 100-unit radius, and the out-of-range case would prove nothing.
std::vector<uint8_t> PlayerSpawnAt(uint32_t id, const std::string& name, float world_units) {
    return NewSpawnPayloadAt(id, name, /*server_x=*/0,
                             /*server_y=*/static_cast<int32_t>(world_units * 8.0f), /*z=*/0,
                             /*level=*/10, /*npc_type=*/0, /*char_class=*/1,
                             /*hp_percent=*/100);
}

// A full OP_GroupUpdate (GROUP_ACT_UPDATE) naming up to five members besides
// self. GroupUpdate_Struct: action@0, yourname@4, membername[5][64]@68,
// leadersname@388; 452 bytes, so the packet is 454 with its opcode.
std::vector<uint8_t> GroupUpdatePayload(const std::string& self,
                                        const std::vector<std::string>& members,
                                        const std::string& leader) {
    return BuildPayload(2 + 452, [&](EQ::Net::DynamicPacket& p) {
        p.PutUInt32(2 + 0, EQT::GROUP_ACT_UPDATE);
        p.PutCString(2 + 4, self.c_str());
        for (size_t i = 0; i < members.size() && i < 5; ++i) {
            p.PutCString(2 + 68 + 64 * i, members[i].c_str());
        }
        p.PutCString(2 + 388, leader.c_str());
    });
}

bool Contains(const std::vector<uint16_t>& v, uint16_t id) {
    return std::find(v.begin(), v.end(), id) != v.end();
}

// The scene every membership test runs in:
//   100 "Testchar"  -- the caster (this client's own character), at the origin
//   101 "Nearmate"  -- group member, 20 units away  (IN a 100-unit radius)
//   102 "Farmate"   -- group member, 400 units away (OUT of it)
//   103 "Stranger"  -- NOT in the group, 3 units away
//   104 "Bystander" -- NOT in the group, 4 units away
// The strangers stand CLOSER than the member who qualifies, so proximity and
// membership disagree on every entity in the scene.
constexpr uint16_t kCaster = 100;
constexpr uint16_t kNearMate = 101;
constexpr uint16_t kFarMate = 102;
constexpr uint16_t kStranger = 103;
constexpr uint16_t kBystander = 104;

void SpawnScene(EverQuest& eq) {
    ASSERT_NO_THROW(ReplayZone(eq, HC_OP_NewSpawn, PlayerSpawnAt(kCaster, "Testchar", 0.0f)));
    ASSERT_NO_THROW(ReplayZone(eq, HC_OP_NewSpawn, PlayerSpawnAt(kNearMate, "Nearmate", 20.0f)));
    ASSERT_NO_THROW(ReplayZone(eq, HC_OP_NewSpawn, PlayerSpawnAt(kFarMate, "Farmate", 400.0f)));
    ASSERT_NO_THROW(ReplayZone(eq, HC_OP_NewSpawn, PlayerSpawnAt(kStranger, "Stranger", 3.0f)));
    ASSERT_NO_THROW(ReplayZone(eq, HC_OP_NewSpawn, PlayerSpawnAt(kBystander, "Bystander", 4.0f)));

    // The scene means what it says: check the decoded world positions rather
    // than trusting the packet encoding.
    const auto& ents = eq.GetEntities();
    ASSERT_NEAR(ents.at(kNearMate).x, 20.0f, 0.2f);
    ASSERT_NEAR(ents.at(kFarMate).x, 400.0f, 0.2f);
    ASSERT_NEAR(ents.at(kStranger).x, 3.0f, 0.2f);
}

void FormGroup(EverQuest& eq) {
    ASSERT_NO_THROW(ReplayZone(eq, HC_OP_GroupUpdate,
        GroupUpdatePayload("Testchar", {"Nearmate", "Farmate"}, "Testchar")));
    ASSERT_TRUE(eq.GetGameState().group().inGroup());
    // self + Nearmate + Farmate
    ASSERT_EQ(eq.GetGameState().group().memberCount(), 3);
}

}  // namespace

// ===================================================================
// Membership: the defect
// ===================================================================

TEST(SpellGroupTargeting, AdjacentNonMembersAreNotTargeted) {
    EverQuest eq = MakeEq();
    SpawnScene(eq);
    FormGroup(eq);
    SpellTypeProcessor proc(&eq, nullptr, nullptr);

    // 100-unit radius: reaches the two strangers (3 and 4 units) and Nearmate
    // (20), not Farmate (400).
    std::vector<uint16_t> targets = proc.findGroupTargets(kCaster, 100.0f);

    // THE defect: on the pre-T314 code both of these were in the set.
    EXPECT_FALSE(Contains(targets, kStranger))
        << "a non-member standing 3 units away was targeted by a group spell";
    EXPECT_FALSE(Contains(targets, kBystander))
        << "a non-member standing 4 units away was targeted by a group spell";

    EXPECT_TRUE(Contains(targets, kCaster));
    EXPECT_TRUE(Contains(targets, kNearMate));
    EXPECT_EQ(targets.size(), 2u);
}

TEST(SpellGroupTargeting, MemberOutOfRadiusIsSkippedButMemberInRadiusIsNot) {
    EverQuest eq = MakeEq();
    SpawnScene(eq);
    FormGroup(eq);
    SpellTypeProcessor proc(&eq, nullptr, nullptr);

    std::vector<uint16_t> targets = proc.findGroupTargets(kCaster, 100.0f);

    EXPECT_TRUE(Contains(targets, kNearMate)) << "member at 20 units missed";
    EXPECT_FALSE(Contains(targets, kFarMate))
        << "member at 400 units was inside a 100-unit group radius";
}

// Membership and range are an AND: widening the radius must bring in the
// distant MEMBER and still leave the adjacent strangers out. This is the test
// that separates "fixed the membership check" from "dropped the range check".
TEST(SpellGroupTargeting, WideRadiusReachesEveryMemberAndStillNoStrangers) {
    EverQuest eq = MakeEq();
    SpawnScene(eq);
    FormGroup(eq);
    SpellTypeProcessor proc(&eq, nullptr, nullptr);

    std::vector<uint16_t> targets = proc.findGroupTargets(kCaster, 1000.0f);

    EXPECT_TRUE(Contains(targets, kCaster));
    EXPECT_TRUE(Contains(targets, kNearMate));
    EXPECT_TRUE(Contains(targets, kFarMate));
    EXPECT_FALSE(Contains(targets, kStranger));
    EXPECT_FALSE(Contains(targets, kBystander));
    EXPECT_EQ(targets.size(), 3u);
}

TEST(SpellGroupTargeting, UngroupedCasterAffectsOnlySelfEvenInACrowd) {
    EverQuest eq = MakeEq();
    SpawnScene(eq);
    ASSERT_FALSE(eq.GetGameState().group().inGroup());
    SpellTypeProcessor proc(&eq, nullptr, nullptr);

    std::vector<uint16_t> targets = proc.findGroupTargets(kCaster, 1000.0f);

    ASSERT_EQ(targets.size(), 1u);
    EXPECT_EQ(targets[0], kCaster);
}

// The caster is not range checked -- eqemu casts on the caster's own group
// slot before it measures anything. A zero radius still buffs the caster.
TEST(SpellGroupTargeting, CasterIsAlwaysAffectedRegardlessOfRadius) {
    EverQuest eq = MakeEq();
    SpawnScene(eq);
    FormGroup(eq);
    SpellTypeProcessor proc(&eq, nullptr, nullptr);

    std::vector<uint16_t> targets = proc.findGroupTargets(kCaster, 0.0f);

    ASSERT_EQ(targets.size(), 1u);
    EXPECT_EQ(targets[0], kCaster);
}

// A member who zones in AFTER the group update has spawnId 0 in GroupState
// (eq_group.cpp only resolves ids it can see at update time), so spawn-id
// matching alone would silently drop them -- exactly the "misses group mates"
// half of the defect, in a different disguise. Name matching covers it.
TEST(SpellGroupTargeting, MemberWhoZonedInAfterTheGroupUpdateIsStillTargeted) {
    EverQuest eq = MakeEq();
    ASSERT_NO_THROW(ReplayZone(eq, HC_OP_NewSpawn, PlayerSpawnAt(kCaster, "Testchar", 0.0f)));
    ASSERT_NO_THROW(ReplayZone(eq, HC_OP_NewSpawn, PlayerSpawnAt(kStranger, "Stranger", 3.0f)));

    // Group forms while Latemate is not in the zone.
    ASSERT_NO_THROW(ReplayZone(eq, HC_OP_GroupUpdate,
        GroupUpdatePayload("Testchar", {"Latemate"}, "Testchar")));
    const eqt::state::GroupMember* late =
        eq.GetGameState().group().getMember(eq.GetGameState().group().findMemberByName("Latemate"));
    ASSERT_NE(late, nullptr);
    ASSERT_EQ(late->spawnId, 0) << "precondition: the member has no spawn id yet";

    // ...then they zone in.
    ASSERT_NO_THROW(ReplayZone(eq, HC_OP_NewSpawn, PlayerSpawnAt(105, "Latemate", 15.0f)));

    SpellTypeProcessor proc(&eq, nullptr, nullptr);
    std::vector<uint16_t> targets = proc.findGroupTargets(kCaster, 100.0f);

    EXPECT_TRUE(Contains(targets, 105)) << "group mate with an unresolved spawn id was missed";
    EXPECT_FALSE(Contains(targets, kStranger));
}

// NPCs and corpses are not group spell targets whatever else is true.
TEST(SpellGroupTargeting, NpcsAndNonPlayersAreNeverGroupTargets) {
    EverQuest eq = MakeEq();
    SpawnScene(eq);
    FormGroup(eq);
    // An NPC that shares a group member's NAME -- the name fallback must not
    // promote it, because it is filtered out as a non-player first.
    ASSERT_NO_THROW(ReplayZone(eq, HC_OP_NewSpawn,
        NewSpawnPayloadAt(106, "Nearmate", 0, 80, 0, /*level=*/10, /*npc_type=*/1)));

    SpellTypeProcessor proc(&eq, nullptr, nullptr);
    std::vector<uint16_t> targets = proc.findGroupTargets(kCaster, 1000.0f);

    EXPECT_FALSE(Contains(targets, 106)) << "an NPC was targeted by a group spell";
}

// ===================================================================
// Radius resolution -- eqemu Mob::GetAOERange()
// ===================================================================

TEST(SpellGroupRadius, PrefersAoeRangeOverCastRange) {
    SpellData s;
    s.aoe_range = 100.0f;
    s.range = 200.0f;
    EXPECT_FLOAT_EQ(SpellTypeProcessor::groupSpellRadius(s), 100.0f);
}

TEST(SpellGroupRadius, FallsBackToCastRangeWhenNoAoeRange) {
    SpellData s;
    s.aoe_range = 0.0f;
    s.range = 200.0f;
    EXPECT_FLOAT_EQ(SpellTypeProcessor::groupSpellRadius(s), 200.0f);
}

TEST(SpellGroupRadius, FallsBackToTenWhenNeitherIsSet) {
    SpellData s;
    s.aoe_range = 0.0f;
    s.range = 0.0f;
    EXPECT_FLOAT_EQ(SpellTypeProcessor::groupSpellRadius(s),
                    SpellTypeProcessor::DEFAULT_GROUP_RADIUS);
    EXPECT_FLOAT_EQ(SpellTypeProcessor::DEFAULT_GROUP_RADIUS, 10.0f);
}
