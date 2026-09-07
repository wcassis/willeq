// T308 Leg 1 -- hunt-target filtering classifies creatures by the body type
// the SERVER sent, not by a hand-written race-id table.
//
// CombatManager::IsHumanoid() carried the FIFTH live copy of the pre-JSON race
// numbering (T267, T270, T271, T304 killed the other four). Checked against the
// audited race_models rows it was wrong for everything except 1-12, 85 and 128:
// 44 "Dark Elf (NPC)" is really FPM Freeport Guard, 77 "Skeleton" is NGM Neriak
// Citizen, 117 "Iksar Citizen" is GDM Ghost Dwarf, 140 "Velious Dragon" is FGI
// Forest Giant, 145 "Zombie" is GOO Goo. It is reached by
// IsTargetSuitableForHunt() (combat.cpp), so a player running /hunt had targets
// accepted or refused on the strength of the wrong creature type.
//
// These tests drive the path the USER drives -- /pathfinding on, /combat on,
// /hunt on, then the game tick -- and assert on the target the client actually
// engages (EverQuest::GetGameState().combat(), the public projection SetTarget()
// publishes). Nothing here calls IsHumanoid(); the private predicate is only
// observable through the engagement decision.
//
// Clocks: CombatManager has no injectable clock and its Update()/hunt/scan
// gates are wall-clock. Backdating the private timestamps is the technique
// test_combat_manager_*.cpp established (T17/T23) precisely so no test sleeps.

#include <gtest/gtest.h>

#define private public
#define protected public
#include "client/combat.h"
#undef private
#undef protected

#include "client/eq_body_types.h"
#include "zone_stream_test_utils.h"

#include <chrono>
#include <string>

using namespace zonetest;

namespace {

// Backdates every gate Update() consults so one call actually scans and runs
// one auto-hunting step.
void OpenAllGates(CombatManager* cm) {
    auto past = std::chrono::steady_clock::now() - std::chrono::seconds(5);
    cm->m_last_update = past;
    cm->m_last_target_scan = past;
    cm->m_last_hunt_update = past;
}

// Spawns one NPC (class_id 0 -- CombatManager::IsHostile()'s definition of an
// NPC) at `dist` units away with an explicit race id and body type.
void SpawnNpc(EverQuest& eq, uint32_t id, const std::string& name,
              int32_t dist, uint32_t race, uint8_t bodytype) {
    ASSERT_NO_THROW(ReplayZone(eq, HC_OP_NewSpawn,
        NewSpawnPayloadAt(id, name, 0, dist, 0, /*level=*/5, /*npc_type=*/1,
                          /*char_class=*/0, /*hp_percent=*/100, race, bodytype)));
}

// Drives the whole user-visible hunt cycle: turn hunting on, tick once so the
// client scans and sends its considers, answer every consider as an
// indifferent (faction 5) dark-blue (con level 4) con -- the case that reaches
// the humanoid filter at all -- then tick again so a target is engaged.
// Returns the name of the engaged target, or "" if the client engaged nothing.
std::string RunHuntCycle(EverQuest& eq, const std::vector<uint32_t>& spawnIds) {
    eq.Movement().SetPathfinding(true);           // /pathfinding on -- SetAutoHunting requires it
    auto* cm = eq.GetCombatManager();
    cm->Enable();                      // /combat on
    cm->SetAutoHunting(true);          // /hunt on

    OpenAllGates(cm);
    cm->Update();                      // scan + FindNextHuntTarget -> considers sent

    for (uint32_t id : spawnIds) {
        cm->ProcessConsiderResponse(id, /*faction=*/5, /*level=*/4,
                                    /*cur_hp=*/100, /*max_hp=*/100);
    }

    OpenAllGates(cm);
    cm->Update();                      // considers complete -> engage the best target

    return std::string(eq.GetGameState().combat().targetName());
}

// ---------------------------------------------------------------------------
// The behaviour change, on the exact races the dead table named.
// ---------------------------------------------------------------------------

TEST(HuntTargetBodyType, GooIsHuntedNotRefusedAsAZombie) {
    // Race 145. The table called it "Zombie" and returned true (humanoid), so
    // an indifferent con of this mob was REFUSED. Row 145 is GOO Goo, and the
    // server sends body type Monster.
    EverQuest eq = MakeEq();
    SpawnNpc(eq, 501, "a_goo", 15, /*race=*/145, EQT::BodyType::Monster);
    EXPECT_EQ(RunHuntCycle(eq, {501}), "a_goo");
}

TEST(HuntTargetBodyType, GhostDwarfIsHuntedNotRefusedAsAnIksarCitizen) {
    // Race 117. The table called it "Iksar Citizen" -> humanoid -> refused.
    // Row 117 is GDM Ghost Dwarf; the server sends Undead.
    EverQuest eq = MakeEq();
    SpawnNpc(eq, 502, "a_ghost_dwarf", 15, /*race=*/117, EQT::BodyType::Undead);
    EXPECT_EQ(RunHuntCycle(eq, {502}), "a_ghost_dwarf");
}

TEST(HuntTargetBodyType, ForestGiantIsHuntedNotRefusedAsAVeliousDragon) {
    // Race 140. The table called it "Velious Dragon" -> humanoid -> refused.
    // Row 140 is FGI Forest Giant; the server sends Giant.
    EverQuest eq = MakeEq();
    SpawnNpc(eq, 503, "a_forest_giant", 15, /*race=*/140, EQT::BodyType::Giant);
    EXPECT_EQ(RunHuntCycle(eq, {503}), "a_forest_giant");
}

TEST(HuntTargetBodyType, FreeportGuardIsStillRefused) {
    // Race 44. The table called it "Dark Elf (NPC)"; row 44 is FPM Freeport
    // Guard. It got the right ANSWER for the wrong reason, and it must keep
    // getting the right answer: the server sends Humanoid, so an indifferent
    // con of a guard is still not something /hunt picks a fight with.
    EverQuest eq = MakeEq();
    SpawnNpc(eq, 504, "a_freeport_guard", 15, /*race=*/44, EQT::BodyType::Humanoid);
    EXPECT_EQ(RunHuntCycle(eq, {504}), "");
}

TEST(HuntTargetBodyType, NeriakCitizenIsStillRefused) {
    // Race 77. The table called it "Skeleton"; row 77 is NGM Neriak Citizen.
    // Same shape as the guard -- right answer, wrong reason, still right.
    EverQuest eq = MakeEq();
    SpawnNpc(eq, 505, "a_neriak_citizen", 15, /*race=*/77, EQT::BodyType::Humanoid);
    EXPECT_EQ(RunHuntCycle(eq, {505}), "");
}

TEST(HuntTargetBodyType, TheAnimalIsPickedOverTheCitizenStandingCloser) {
    // The decision the defect actually corrupted: two indifferent cons in
    // range, the humanoid nearer. Before this unit BOTH were "humanoid" (44
    // by luck, 145 by the stale label) and /hunt engaged NOTHING. Now the
    // citizen is filtered out on its body type and the goo is engaged even
    // though it is further away.
    EverQuest eq = MakeEq();
    SpawnNpc(eq, 506, "a_freeport_guard", 10, /*race=*/44, EQT::BodyType::Humanoid);
    SpawnNpc(eq, 507, "a_goo", 20, /*race=*/145, EQT::BodyType::Monster);
    EXPECT_EQ(RunHuntCycle(eq, {506, 507}), "a_goo");
}

TEST(HuntTargetBodyType, RaceIdAloneDecidesNothing) {
    // The point of the fix: the SAME race id classifies differently when the
    // server says a different body type, because no race table is consulted
    // any more.
    EverQuest humanoidRun = MakeEq();
    SpawnNpc(humanoidRun, 508, "a_shady_sort", 15, /*race=*/1, EQT::BodyType::Humanoid);
    EXPECT_EQ(RunHuntCycle(humanoidRun, {508}), "");

    EverQuest undeadRun = MakeEq();
    SpawnNpc(undeadRun, 509, "a_shady_sort", 15, /*race=*/1, EQT::BodyType::Undead);
    EXPECT_EQ(RunHuntCycle(undeadRun, {509}), "a_shady_sort");
}

// ---------------------------------------------------------------------------
// The numbering itself. eqemu's common/bodytypes.h is the authority; the
// client's own copy was off by one for every value before this unit.
// ---------------------------------------------------------------------------

TEST(BodyTypeNames, MatchEqemuNumbering) {
    EXPECT_EQ(EQT::BodyType::Humanoid, 1);
    EXPECT_EQ(EQT::BodyType::Name(EQT::BodyType::Humanoid), "Humanoid");
    EXPECT_EQ(EQT::BodyType::Name(EQT::BodyType::Undead), "Undead");
    EXPECT_EQ(EQT::BodyType::Name(EQT::BodyType::Animal), "Animal");
    EXPECT_EQ(EQT::BodyType::Name(EQT::BodyType::Plant), "Plant");
    EXPECT_EQ(EQT::BodyType::Name(EQT::BodyType::Dragon), "Dragon");
    // eqt::strings::GetBodyTypeName() is a one-line delegate to Name() (it is
    // private on EverQuest, so the shared implementation is what is asserted).
    // 0 is not a valid eqemu body type; the deleted switch answered
    // "Humanoid" for it and so mis-named every real value after it.
    EXPECT_EQ(EQT::BodyType::Name(0), "BodyType0");
}

}  // namespace
