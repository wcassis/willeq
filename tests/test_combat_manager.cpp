// T23 — CombatManager coverage (spell/combat batch, unit 1 of 3).
//
// Seam: settled as Option A (refactor/T23_spell_combat_seam_scoping.md,
// Will's ruling) -- no interface extraction. A bare
// EverQuest("127.0.0.1", 0, "test", "test", "test", "Testchar") constructs a
// LIVE CombatManager reachable via the existing public eq.GetCombatManager().
// Fixtures are staged by replaying spawn packets (zone_stream_test_utils.h,
// T18-promoted helpers) -- no network, no graphics, no live server.
//
// Timing: CombatManager reads steady_clock::now() in ~12 places with no
// injectable clock, and Update() is wall-clock gated (target scans only
// every 1000ms). Per plan, no sleep-based tests. Every test here drives a
// deterministic public entry point directly. The lone exception is the
// ProcessConsiderResponse test, which needs CombatManager::Update() to have
// actually run ScanForTargets() once to populate m_potential_targets; that
// private timing gate is backdated via the T17 `#define private public`
// technique (see CombatManagerConsiderFixture below) rather than sleeping
// past it.
//
// Note: tests/test_combat.cpp already covers combat *enums and structs*
// only -- left untouched, not disturbed by this file.

#include <gtest/gtest.h>

// T17-style private access, needed only for the ProcessConsiderResponse
// fixture (backdating m_last_target_scan so Update() actually scans instead
// of sleeping past the 1000ms gate). combat.h is #pragma once, so this
// first inclusion determines the class body's access specifiers for the
// whole translation unit -- eq.h's later (transitive) include of combat.h
// is a no-op.
#define private public
#define protected public
#include "client/combat.h"
#undef private
#undef protected

#include "zone_stream_test_utils.h"

#include <chrono>

using namespace zonetest;

namespace {

// ===================================================================
// 1. Targeting
// ===================================================================

TEST(CombatManagerTargeting, SetTargetById_Hit) {
    EverQuest eq = MakeEq();
    ASSERT_NO_THROW(ReplayZone(eq, HC_OP_NewSpawn,
        NewSpawnPayloadAt(100, "a_test_rat", 0, 10, 0)));
    auto* cm = eq.GetCombatManager();

    // NOTE (deviation from plan, verified not guessed): the plan expected
    // GetReplaySuppressedSendCount() to make "this action attempted to
    // send a packet" assertable for CombatManager's sends too. Empirically
    // it does not -- that counter is only incremented by the internal
    // reply-helper methods used inside ZoneProcess* S->C handlers
    // (eq.cpp:1630 etc.), not by the generic public
    // EverQuest::QueuePacket(opcode, packet) overload CombatManager calls
    // (eq.cpp:3672), which silently no-ops when m_zone_connection is null
    // with no side effect at all. There is no accessor to assert a
    // CombatManager-initiated send was attempted; only the resulting state
    // change is assertable. See Review for the full note.
    EXPECT_TRUE(cm->SetTarget(100));
    EXPECT_TRUE(cm->HasTarget());
    EXPECT_EQ(cm->GetTargetId(), 100);
}

TEST(CombatManagerTargeting, SetTargetById_MissOnEmptyTableIsRealNoOp) {
    // Verified fact from the scoping doc's probe: SetTarget on an empty
    // entity table is a no-op that returns false -- not a bug, the fixture
    // requirement. Pinning it here as real product behavior.
    EverQuest eq = MakeEq();
    auto* cm = eq.GetCombatManager();

    EXPECT_FALSE(cm->SetTarget(1234));
    EXPECT_FALSE(cm->HasTarget());
    EXPECT_EQ(cm->GetTargetId(), 0);
}

TEST(CombatManagerTargeting, SetTargetById_MissOnUnknownIdWithNonEmptyTable) {
    EverQuest eq = MakeEq();
    ASSERT_NO_THROW(ReplayZone(eq, HC_OP_NewSpawn,
        NewSpawnPayloadAt(100, "a_test_rat", 0, 10, 0)));
    auto* cm = eq.GetCombatManager();

    EXPECT_FALSE(cm->SetTarget(999));
    EXPECT_FALSE(cm->HasTarget());
}

TEST(CombatManagerTargeting, SetTargetByName_ExactMatch) {
    EverQuest eq = MakeEq();
    ASSERT_NO_THROW(ReplayZone(eq, HC_OP_NewSpawn,
        NewSpawnPayloadAt(200, "Guard_Test", 0, 10, 0)));
    auto* cm = eq.GetCombatManager();

    EXPECT_TRUE(cm->SetTarget(std::string("Guard_Test")));
    EXPECT_EQ(cm->GetTargetId(), 200);
}

TEST(CombatManagerTargeting, SetTargetByName_PartialMatchFallback) {
    EverQuest eq = MakeEq();
    ASSERT_NO_THROW(ReplayZone(eq, HC_OP_NewSpawn,
        NewSpawnPayloadAt(300, "a_swamp_snake", 0, 10, 0)));
    auto* cm = eq.GetCombatManager();

    // "snake" is not an exact name but is found as a substring.
    EXPECT_TRUE(cm->SetTarget(std::string("snake")));
    EXPECT_EQ(cm->GetTargetId(), 300);
}

TEST(CombatManagerTargeting, SetTargetByName_Miss) {
    EverQuest eq = MakeEq();
    ASSERT_NO_THROW(ReplayZone(eq, HC_OP_NewSpawn,
        NewSpawnPayloadAt(300, "a_swamp_snake", 0, 10, 0)));
    auto* cm = eq.GetCombatManager();

    EXPECT_FALSE(cm->SetTarget(std::string("nonexistent")));
    EXPECT_FALSE(cm->HasTarget());
}

TEST(CombatManagerTargeting, ClearTarget) {
    EverQuest eq = MakeEq();
    ASSERT_NO_THROW(ReplayZone(eq, HC_OP_NewSpawn,
        NewSpawnPayloadAt(100, "a_test_rat", 0, 10, 0)));
    auto* cm = eq.GetCombatManager();
    ASSERT_TRUE(cm->SetTarget(100));

    cm->ClearTarget();
    EXPECT_FALSE(cm->HasTarget());
    EXPECT_EQ(cm->GetTargetId(), 0);
}

TEST(CombatManagerTargeting, ConsiderTarget_NoTargetIsNoOp) {
    EverQuest eq = MakeEq();
    auto* cm = eq.GetCombatManager();

    EXPECT_NO_THROW(cm->ConsiderTarget());
}

TEST(CombatManagerTargeting, ConsiderTarget_WithTargetDoesNotCrash) {
    EverQuest eq = MakeEq();
    ASSERT_NO_THROW(ReplayZone(eq, HC_OP_NewSpawn,
        NewSpawnPayloadAt(100, "a_test_rat", 0, 10, 0)));
    auto* cm = eq.GetCombatManager();
    ASSERT_TRUE(cm->SetTarget(100));

    // ConsiderTarget's only observable effect (besides the unassertable
    // packet send, see the note in SetTargetById_Hit above) is the packet
    // build itself -- covered indirectly by not crashing/throwing on the
    // real target id.
    EXPECT_NO_THROW(cm->ConsiderTarget());
}

// ===================================================================
// 2. Auto-attack (and auto-fire, its non-aliased sibling)
// ===================================================================

TEST(CombatManagerAutoAttack, EnableWithoutTargetIsNoOp) {
    EverQuest eq = MakeEq();
    auto* cm = eq.GetCombatManager();

    uint64_t before = eq.GetReplaySuppressedSendCount();
    cm->EnableAutoAttack();
    EXPECT_FALSE(cm->IsAutoAttackEnabled());
    EXPECT_EQ(eq.GetReplaySuppressedSendCount(), before);
}

TEST(CombatManagerAutoAttack, EnableWithTarget) {
    EverQuest eq = MakeEq();
    ASSERT_NO_THROW(ReplayZone(eq, HC_OP_NewSpawn,
        NewSpawnPayloadAt(100, "a_test_rat", 0, 10, 0)));
    auto* cm = eq.GetCombatManager();
    ASSERT_TRUE(cm->SetTarget(100));

    cm->EnableAutoAttack();
    EXPECT_TRUE(cm->IsAutoAttackEnabled());
    EXPECT_EQ(cm->GetCombatState(), COMBAT_STATE_ENGAGED);
}

TEST(CombatManagerAutoAttack, DisableIsHarmlessRegardlessOfPriorState) {
    // Real behavior: unlike Enable (guarded on HasTarget()), Disable has no
    // "already disabled" guard -- it unconditionally builds both off-
    // packets even if auto-attack was never enabled. Not independently
    // assertable via the send counter (see the note in
    // CombatManagerTargeting.SetTargetById_Hit); pinning what IS
    // observable: it doesn't crash and it clears the flag.
    EverQuest eq = MakeEq();
    auto* cm = eq.GetCombatManager();

    EXPECT_NO_THROW(cm->DisableAutoAttack());
    EXPECT_FALSE(cm->IsAutoAttackEnabled());
}

TEST(CombatManagerAutoAttack, DisableFromEngagedReturnsToIdle) {
    EverQuest eq = MakeEq();
    ASSERT_NO_THROW(ReplayZone(eq, HC_OP_NewSpawn,
        NewSpawnPayloadAt(100, "a_test_rat", 0, 10, 0)));
    auto* cm = eq.GetCombatManager();
    ASSERT_TRUE(cm->SetTarget(100));
    cm->EnableAutoAttack();
    ASSERT_EQ(cm->GetCombatState(), COMBAT_STATE_ENGAGED);

    cm->DisableAutoAttack();
    EXPECT_EQ(cm->GetCombatState(), COMBAT_STATE_IDLE);
}

TEST(CombatManagerAutoAttack, DisableWhileNotEngagedLeavesStateAlone) {
    EverQuest eq = MakeEq();
    auto* cm = eq.GetCombatManager();
    cm->SetCombatState(COMBAT_STATE_HUNTING);

    cm->DisableAutoAttack();
    EXPECT_EQ(cm->GetCombatState(), COMBAT_STATE_HUNTING);
}

TEST(CombatManagerAutoFire, EnableWithoutTargetIsNoOp) {
    EverQuest eq = MakeEq();
    auto* cm = eq.GetCombatManager();

    cm->EnableAutoFire();
    EXPECT_FALSE(cm->IsAutoFireEnabled());
}

TEST(CombatManagerAutoFire, EnableDisablesMeleeAutoAttack_FlagsDoNotAlias) {
    // Both flags are independently readable; enabling ranged auto-fire
    // forces melee auto-attack off (real coupling), but the flags
    // themselves are separate storage -- not aliased to the same bit.
    EverQuest eq = MakeEq();
    ASSERT_NO_THROW(ReplayZone(eq, HC_OP_NewSpawn,
        NewSpawnPayloadAt(100, "a_test_rat", 0, 10, 0)));
    auto* cm = eq.GetCombatManager();
    ASSERT_TRUE(cm->SetTarget(100));
    cm->EnableAutoAttack();
    ASSERT_TRUE(cm->IsAutoAttackEnabled());

    cm->EnableAutoFire();
    EXPECT_TRUE(cm->IsAutoFireEnabled());
    EXPECT_FALSE(cm->IsAutoAttackEnabled())
        << "EnableAutoFire must turn off melee auto-attack";
}

TEST(CombatManagerAutoFire, ToggleFlipsState) {
    EverQuest eq = MakeEq();
    ASSERT_NO_THROW(ReplayZone(eq, HC_OP_NewSpawn,
        NewSpawnPayloadAt(100, "a_test_rat", 0, 10, 0)));
    auto* cm = eq.GetCombatManager();
    ASSERT_TRUE(cm->SetTarget(100));

    EXPECT_FALSE(cm->IsAutoFireEnabled());
    cm->ToggleAutoFire();
    EXPECT_TRUE(cm->IsAutoFireEnabled());
    cm->ToggleAutoFire();
    EXPECT_FALSE(cm->IsAutoFireEnabled());
}

// ===================================================================
// 3. Target cycling
// ===================================================================

TEST(CombatManagerCycling, ZeroCandidatesIsNoOp) {
    EverQuest eq = MakeEq();
    auto* cm = eq.GetCombatManager();

    EXPECT_NO_THROW(cm->CycleTargets(true));
    EXPECT_FALSE(cm->HasTarget());
}

TEST(CombatManagerCycling, SingleCandidateStaysOnItWhenCyclingForward) {
    EverQuest eq = MakeEq();
    ASSERT_NO_THROW(ReplayZone(eq, HC_OP_NewSpawn,
        NewSpawnPayloadAt(100, "a_test_rat", 0, 10, 0)));
    auto* cm = eq.GetCombatManager();

    cm->CycleTargets(true);
    EXPECT_EQ(cm->GetTargetId(), 100);
    cm->CycleTargets(true);
    EXPECT_EQ(cm->GetTargetId(), 100);
}

TEST(CombatManagerCycling, ForwardOrderIsNearestFirstThenWrapsAround) {
    // Entities placed at distances 10, 50, 100 from the origin (default
    // player position for a bare EverQuest). entity.x <- server_y
    // (INVERSEXY), so server_y directly sets the distance along X.
    EverQuest eq = MakeEq();
    ASSERT_NO_THROW(ReplayZone(eq, HC_OP_NewSpawn, NewSpawnPayloadAt(10, "near", 0, 10, 0)));
    ASSERT_NO_THROW(ReplayZone(eq, HC_OP_NewSpawn, NewSpawnPayloadAt(50, "mid", 0, 50, 0)));
    ASSERT_NO_THROW(ReplayZone(eq, HC_OP_NewSpawn, NewSpawnPayloadAt(100, "far", 0, 100, 0)));
    auto* cm = eq.GetCombatManager();

    // m_cycle_index starts at -1 ("no active cycle position"); forward
    // advances it to 0 first, i.e. the nearest candidate.
    cm->CycleTargets(true);
    EXPECT_EQ(cm->GetTargetId(), 10);
    cm->CycleTargets(true);
    EXPECT_EQ(cm->GetTargetId(), 50);
    cm->CycleTargets(true);
    EXPECT_EQ(cm->GetTargetId(), 100);
    // Wraps back to nearest.
    cm->CycleTargets(true);
    EXPECT_EQ(cm->GetTargetId(), 10);
}

TEST(CombatManagerCycling, BackwardFromFreshIndexWrapsToFarthest) {
    // Pins what m_cycle_index == -1 means for the backward direction:
    // with no active target, index-- takes -1 -> -2, which then wraps to
    // size-1 (farthest), not size-2 or an out-of-bounds access.
    EverQuest eq = MakeEq();
    ASSERT_NO_THROW(ReplayZone(eq, HC_OP_NewSpawn, NewSpawnPayloadAt(10, "near", 0, 10, 0)));
    ASSERT_NO_THROW(ReplayZone(eq, HC_OP_NewSpawn, NewSpawnPayloadAt(50, "mid", 0, 50, 0)));
    ASSERT_NO_THROW(ReplayZone(eq, HC_OP_NewSpawn, NewSpawnPayloadAt(100, "far", 0, 100, 0)));
    auto* cm = eq.GetCombatManager();

    cm->CycleTargets(false);
    EXPECT_EQ(cm->GetTargetId(), 100);
    cm->CycleTargets(false);
    EXPECT_EQ(cm->GetTargetId(), 50);
    cm->CycleTargets(false);
    EXPECT_EQ(cm->GetTargetId(), 10);
    // Wraps back to farthest.
    cm->CycleTargets(false);
    EXPECT_EQ(cm->GetTargetId(), 100);
}

TEST(CombatManagerCycling, SelfIsExcluded) {
    EverQuest eq = MakeEq();
    // Self: same spawn id EverQuest reports via GetEntityID() (0 for a bare
    // replay-seam EverQuest that never received a PlayerProfile).
    ASSERT_EQ(eq.GetEntityID(), 0);
    ASSERT_NO_THROW(ReplayZone(eq, HC_OP_NewSpawn, NewSpawnPayloadAt(0, "self", 0, 5, 0)));
    ASSERT_NO_THROW(ReplayZone(eq, HC_OP_NewSpawn, NewSpawnPayloadAt(20, "a_live_rat", 0, 20, 0)));
    auto* cm = eq.GetCombatManager();

    cm->CycleTargets(true);
    EXPECT_EQ(cm->GetTargetId(), 20) << "self (id 0) must be excluded from the cycle list";
}

TEST(CombatManagerCycling, CorpsesAreExcluded) {
    // is_corpse is never set true by spawn parsing itself (only by the
    // HC_OP_Death and HC_OP_MobRename handlers post-spawn) -- stage a real
    // corpse via a Death packet rather than inventing a field.
    EverQuest eq = MakeEq();
    ASSERT_NO_THROW(ReplayZone(eq, HC_OP_NewSpawn, NewSpawnPayloadAt(30, "a_dead_rat", 0, 10, 0)));
    ASSERT_NO_THROW(ReplayZone(eq, HC_OP_NewSpawn, NewSpawnPayloadAt(40, "a_live_rat", 0, 40, 0)));

    // Death_Struct: victim_id(4) killer_id(4) corpse_id(4) bind_zone(4)
    // spell_id(4) attack_skill(4) damage(4) -- offsets include the 2-byte
    // opcode prefix (eq.cpp:3136-3142).
    auto deathPayload = BuildPayload(30, [&](EQ::Net::DynamicPacket& p) {
        p.PutUInt32(2, 30);   // victim_id
        p.PutUInt32(6, 0);    // killer_id (0 = the player)
    });
    ASSERT_NO_THROW(ReplayZone(eq, HC_OP_Death, deathPayload));
    ASSERT_TRUE(eq.GetEntities().at(30).is_corpse);

    auto* cm = eq.GetCombatManager();
    cm->CycleTargets(true);
    EXPECT_EQ(cm->GetTargetId(), 40) << "corpse (id 30) must be excluded from the cycle list";
}

// ===================================================================
// 4. Consider
// ===================================================================

// Backdates the private m_last_target_scan gate so a single Update() call
// actually runs ScanForTargets() -- the only way to populate
// m_potential_targets, which GetHighestPriorityTarget() (public) exposes.
// This is the one behavior in this unit that is otherwise unreachable
// without either a sleep (forbidden) or this T17-style backdate.
TEST(CombatManagerConsider, ProcessConsiderResponseUpdatesPotentialTarget) {
    EverQuest eq = MakeEq();
    // class_id=0 (NPC), within default aggro radius (30.0f), distance 15.
    ASSERT_NO_THROW(ReplayZone(eq, HC_OP_NewSpawn,
        NewSpawnPayloadAt(500, "a_cave_bat", 0, 15, 0, /*level=*/5, /*npc_type=*/1,
                           /*char_class=*/0)));
    auto* cm = eq.GetCombatManager();
    cm->Enable();
    // Force the 1000ms scan gate open without sleeping.
    cm->m_last_target_scan = std::chrono::steady_clock::now() - std::chrono::seconds(2);
    cm->Update();

    CombatTarget* before = cm->GetHighestPriorityTarget();
    ASSERT_NE(before, nullptr);
    ASSERT_EQ(before->entity_id, 500);
    EXPECT_FALSE(before->has_consider_data);

    // level=13 maps to CON_RED per the documented switch in
    // ProcessConsiderResponse.
    cm->ProcessConsiderResponse(/*target_id=*/500, /*faction=*/8, /*level=*/13,
                                 /*cur_hp=*/40, /*max_hp=*/80);

    CombatTarget* after = cm->GetHighestPriorityTarget();
    ASSERT_NE(after, nullptr);
    ASSERT_EQ(after->entity_id, 500);
    EXPECT_TRUE(after->has_consider_data);
    EXPECT_EQ(after->faction, 8u);
    EXPECT_EQ(after->con_level, 13u);
    EXPECT_EQ(after->con_color, CON_RED);
    EXPECT_EQ(after->cur_hp, 40);
    EXPECT_EQ(after->max_hp, 80);
}

TEST(CombatManagerConsider, ProcessConsiderResponseUnknownIdIsNoOp) {
    EverQuest eq = MakeEq();
    auto* cm = eq.GetCombatManager();

    // No potential targets, no current target -- must not crash or
    // fabricate state.
    EXPECT_NO_THROW(cm->ProcessConsiderResponse(999, 5, 10, 50, 100));
    EXPECT_EQ(cm->GetHighestPriorityTarget(), nullptr);
}

// ===================================================================
// 5. Combat state machine
// ===================================================================

TEST(CombatManagerState, SetGetRoundTripsAllSevenValues) {
    EverQuest eq = MakeEq();
    auto* cm = eq.GetCombatManager();

    const CombatState states[] = {
        COMBAT_STATE_IDLE, COMBAT_STATE_ENGAGED, COMBAT_STATE_FLEEING,
        COMBAT_STATE_LOOTING, COMBAT_STATE_HUNTING, COMBAT_STATE_RESTING,
        COMBAT_STATE_SEEKING_GUARD,
    };
    for (CombatState s : states) {
        cm->SetCombatState(s);
        EXPECT_EQ(cm->GetCombatState(), s);
    }
}

TEST(CombatManagerState, SettingSameStateTwiceIsHarmless) {
    EverQuest eq = MakeEq();
    auto* cm = eq.GetCombatManager();
    cm->SetCombatState(COMBAT_STATE_HUNTING);
    EXPECT_NO_THROW(cm->SetCombatState(COMBAT_STATE_HUNTING));
    EXPECT_EQ(cm->GetCombatState(), COMBAT_STATE_HUNTING);
}

TEST(CombatManagerState, IsFleeingTracksStateExactly) {
    EverQuest eq = MakeEq();
    auto* cm = eq.GetCombatManager();
    EXPECT_FALSE(cm->IsFleeing());
    cm->SetCombatState(COMBAT_STATE_FLEEING);
    EXPECT_TRUE(cm->IsFleeing());
    cm->SetCombatState(COMBAT_STATE_IDLE);
    EXPECT_FALSE(cm->IsFleeing());
}

TEST(CombatManagerState, IsLootingTracksStateExactly) {
    EverQuest eq = MakeEq();
    auto* cm = eq.GetCombatManager();
    EXPECT_FALSE(cm->IsLooting());
    cm->SetCombatState(COMBAT_STATE_LOOTING);
    EXPECT_TRUE(cm->IsLooting());
    cm->SetCombatState(COMBAT_STATE_IDLE);
    EXPECT_FALSE(cm->IsLooting());
}

TEST(CombatManagerState, ShouldFleeDefaultIsFalseAtFullHealth) {
    EverQuest eq = MakeEq();
    auto* cm = eq.GetCombatManager();
    // Constructor initializes m_stats.hp_percent to 100.0f and the default
    // flee threshold to 20.0f.
    EXPECT_FALSE(cm->ShouldFlee());
}

TEST(CombatManagerState, SetFleeThresholdAboveCurrentHpTriggersShouldFlee) {
    EverQuest eq = MakeEq();
    auto* cm = eq.GetCombatManager();
    cm->SetFleeThreshold(150.0f);
    EXPECT_TRUE(cm->ShouldFlee());
}

TEST(CombatManagerState, UpdateCombatStatsFeedsShouldFlee) {
    EverQuest eq = MakeEq();
    auto* cm = eq.GetCombatManager();
    CombatStats stats{};
    stats.hp_percent = 10.0f;
    stats.mana_percent = 100.0f;
    stats.endurance_percent = 100.0f;
    cm->UpdateCombatStats(stats);
    // Default flee threshold is 20.0f.
    EXPECT_TRUE(cm->ShouldFlee());
}

// ===================================================================
// 6. Config round-trips
// ===================================================================
//
// Assist range, aggro radius, and combat range have setters
// (SetAssistRange/SetAggroRadius/SetCombatRange) but NO public getters --
// there is no accessor to round-trip against, and their only observable
// effect is inside the wall-clock-gated Update()/ScanForTargets path,
// which is out of proportion to "cheap" for a plain setter smoke test.
// Per the plan's STOP conditions ("cannot be reached without ... a new
// accessor -> skip it, name it in the Review"), these three are skipped
// as true round-trips; only a call-does-not-crash smoke test is included.
// Auto-loot and auto-movement DO have matching getters and are fully
// round-tripped below.

TEST(CombatManagerConfig, SettersWithoutGettersDoNotCrash) {
    EverQuest eq = MakeEq();
    auto* cm = eq.GetCombatManager();
    EXPECT_NO_THROW(cm->SetAssistRange(75.0f));
    EXPECT_NO_THROW(cm->SetAggroRadius(40.0f));
    EXPECT_NO_THROW(cm->SetCombatRange(8.0f));
}

TEST(CombatManagerConfig, AutoLootRoundTripsViaEnableAutoLoot) {
    EverQuest eq = MakeEq();
    auto* cm = eq.GetCombatManager();
    // Constructor default is true.
    EXPECT_TRUE(cm->IsAutoLootEnabled());
    cm->EnableAutoLoot(false);
    EXPECT_FALSE(cm->IsAutoLootEnabled());
    cm->EnableAutoLoot(true);
    EXPECT_TRUE(cm->IsAutoLootEnabled());
}

TEST(CombatManagerConfig, AutoLootRoundTripsViaSetAutoLootEnabled) {
    // SetAutoLootEnabled and EnableAutoLoot are two setters over the same
    // underlying flag -- confirm they don't alias to different storage.
    EverQuest eq = MakeEq();
    auto* cm = eq.GetCombatManager();
    cm->SetAutoLootEnabled(false);
    EXPECT_FALSE(cm->IsAutoLootEnabled());
    cm->EnableAutoLoot(true);
    EXPECT_TRUE(cm->IsAutoLootEnabled());
}

TEST(CombatManagerConfig, AutoMovementRoundTrips) {
    EverQuest eq = MakeEq();
    auto* cm = eq.GetCombatManager();
    // Constructor default is false.
    EXPECT_FALSE(cm->IsAutoMovementEnabled());
    cm->EnableAutoMovement();
    EXPECT_TRUE(cm->IsAutoMovementEnabled());
    cm->DisableAutoMovement();
    EXPECT_FALSE(cm->IsAutoMovementEnabled());
}

// ===================================================================
// T26: event-driven loot state machine (was: blocking sleeps in the loot
// path, combat.cpp:818/825/1168/1843/1861). See
// refactor/T26_loot_state_machine.md.
// ===================================================================
//
// Thread trace (read, not guessed): the modern UI-driven "loot all" click
// goes through EverQuest::LootAllFromCorpse (eq_bridge.cpp:1573, the D18a
// game-side loot tracking path) which is NON-blocking -- it queues one slot
// at a time and re-requests per server round trip. It does NOT call
// CombatManager::LootAll() at all.
//
// CombatManager::LootAll()/ProcessLootWindow()/CheckAutoLoot() used to
// block via sleep_for; they are reached through a different, still-live
// path: ProcessCombatRound() (combat.cpp:1192) calls
// CombatManager::LootCorpse()/BeginLootRequest() when an auto-hunted target
// dies with auto-loot enabled. That path does NOT set
// EverQuest::m_player_looting_corpse_id (that variable belongs to the
// separate UI loot-window flow), so when the server's loot item response
// (OP_ItemPacket) arrives, EverQuest::eq.cpp's dispatch switch
// (~eq.cpp:1020-1023) takes the "Headless mode looting" branch:
//     } else if (m_combat_manager && m_combat_manager->IsLooting()) {
//         ZoneProcessLootItem(p);
//     }
// ZoneProcessLootItem (eq_combat_actions.cpp:527) calls
// m_combat_manager->CheckAutoLoot() directly and synchronously -- no
// dispatch to another thread. Per CLAUDE.md's threading model, all network
// packet handling ("Network events (TickNetwork)") runs on the GAME
// THREAD, so CheckAutoLoot() (and everything it calls) must never block.
//
// T26 replaced the sleeps with LootSubState + CombatManager::UpdateLootState(),
// ticked every game frame from Application::updateGameState (unconditional,
// alongside UpdateMovement()) -- never from Update()/m_update_thread. These
// tests use the file's established T17 #define-private-public access to
// drive and observe the sub-state machine deterministically (backdating
// timers, matching the ProcessConsiderResponse fixture's technique) instead
// of sleeping past real deadlines.

TEST(CombatManagerLoot, CheckAutoLootReturnsPromptlyAndAdvancesAcrossTicks) {
    EverQuest eq = MakeEq();
    // TickAwaitingItems/TickLootingItems both check CorpseStillPresent() (the
    // T26 "corpse disappeared" exit) every tick, so the corpse id must exist
    // as a staged entity or the machine correctly aborts to Idle instead of
    // advancing -- stage it first.
    ASSERT_NO_THROW(ReplayZone(eq, HC_OP_NewSpawn,
        NewSpawnPayloadAt(777, "a_test_rat's_corpse1", 0, 10, 0)));
    auto* cm = eq.GetCombatManager();

    // Minimal 1-item fixture. LootCorpse() (-> BeginLootRequest()) sets the
    // corpse id, sends the request, and enters AwaitingItems; AddLootItem()
    // stages the single item and refreshes the quiescence clock. Auto-loot
    // is enabled by default.
    ASSERT_TRUE(cm->IsAutoLootEnabled());
    cm->LootCorpse(777);
    cm->AddLootItem(50);
    ASSERT_EQ(cm->GetCombatState(), COMBAT_STATE_LOOTING);
    ASSERT_EQ(cm->GetLootSubState(), LootSubState::AwaitingItems);

    auto start = std::chrono::steady_clock::now();
    cm->CheckAutoLoot();
    auto elapsed = std::chrono::steady_clock::now() - start;

    // The whole point of T26: this must return essentially instantly. Old
    // BUG_ test asserted >=1000ms here; assert the opposite bound now.
    EXPECT_LT(elapsed, std::chrono::milliseconds(50))
        << "CheckAutoLoot() must return promptly -- it runs on the game "
           "thread once per item packet and must never block network reads, "
           "the game tick, or renderer intents (see T26 plan).";
    // Still in AwaitingItems: quiescence window hasn't elapsed yet.
    EXPECT_EQ(cm->GetLootSubState(), LootSubState::AwaitingItems);

    // Backdate the quiescence clock past kItemQuiescenceMs (matches the
    // ProcessConsiderResponse fixture's backdating technique) instead of
    // sleeping past the real deadline.
    cm->m_last_loot_item_time = std::chrono::steady_clock::now() -
        std::chrono::milliseconds(CombatManager::kItemQuiescenceMs + 10);
    cm->UpdateLootState();
    EXPECT_EQ(cm->GetLootSubState(), LootSubState::LootingItems)
        << "quiescence should advance AwaitingItems -> LootingItems";
    ASSERT_EQ(cm->m_loot_items.size(), 1u) << "item not yet sent this tick";

    // Pacing clock was primed so the first item pops on the very next tick
    // (EnterLootSubState sets m_last_item_send_time in the past).
    cm->UpdateLootState();
    EXPECT_TRUE(cm->m_loot_items.empty()) << "the single item should have been sent";
    EXPECT_EQ(cm->GetLootSubState(), LootSubState::LootingItems)
        << "drains to empty, then the NEXT tick notices empty and closes";

    cm->UpdateLootState();
    EXPECT_EQ(cm->GetLootSubState(), LootSubState::Closing);

    // Backdate past kClosingDelayMs to close without a real wait.
    cm->m_loot_state_entered = std::chrono::steady_clock::now() -
        std::chrono::milliseconds(CombatManager::kClosingDelayMs + 10);
    cm->UpdateLootState();
    EXPECT_EQ(cm->GetLootSubState(), LootSubState::Idle);
    EXPECT_NE(cm->GetCombatState(), COMBAT_STATE_LOOTING)
        << "CloseLootWindow() should have returned us to IDLE/HUNTING";
}

TEST(CombatManagerLoot, AwaitingCorpseDeadlineExpiresWithoutHanging) {
    EverQuest eq = MakeEq();
    auto* cm = eq.GetCombatManager();

    // No corpse entity is ever staged -- simulates a kill whose corpse
    // never spawns (or never matches the expected name). Drive the machine
    // directly into AwaitingCorpse the way ProcessCombatRound() does.
    cm->m_pending_corpse_name = "a_test_rat's_corpse1";
    cm->SetCombatState(COMBAT_STATE_LOOTING);
    cm->EnterLootSubState(LootSubState::AwaitingCorpse);

    // Before the deadline: still waiting, no hang, no state change.
    cm->UpdateLootState();
    EXPECT_EQ(cm->GetLootSubState(), LootSubState::AwaitingCorpse);
    EXPECT_EQ(cm->GetCombatState(), COMBAT_STATE_LOOTING);

    // Backdate past kCorpseSearchDeadlineMs.
    cm->m_loot_state_entered = std::chrono::steady_clock::now() -
        std::chrono::milliseconds(CombatManager::kCorpseSearchDeadlineMs + 10);
    cm->UpdateLootState();
    EXPECT_EQ(cm->GetLootSubState(), LootSubState::Idle)
        << "a corpse that never appears must not hang the machine";
    EXPECT_NE(cm->GetCombatState(), COMBAT_STATE_LOOTING);
}

TEST(CombatManagerLoot, AwaitingCorpseFindsCorpseAndBeginsLootRequest) {
    EverQuest eq = MakeEq();
    ASSERT_NO_THROW(ReplayZone(eq, HC_OP_NewSpawn,
        NewSpawnPayloadAt(900, "a_test_rat's_corpse1", 0, 10, 0)));
    auto* cm = eq.GetCombatManager();

    cm->m_pending_corpse_name = "a_test_rat's_corpse1";
    cm->SetCombatState(COMBAT_STATE_LOOTING);
    cm->EnterLootSubState(LootSubState::AwaitingCorpse);

    cm->UpdateLootState();
    EXPECT_EQ(cm->GetLootSubState(), LootSubState::AwaitingItems)
        << "matching corpse entity should trigger BeginLootRequest()";
    EXPECT_EQ(cm->m_current_corpse_id, 900);
}

TEST(CombatManagerLoot, CorpseDisappearingMidLootAbortsWithoutHanging) {
    EverQuest eq = MakeEq();
    auto* cm = eq.GetCombatManager();

    // Corpse id 777 is never staged as an entity -- stands in for "the
    // corpse despawned" (CorpseStillPresent() only checks GetEntities()
    // membership, so a never-added id and a since-removed id are
    // indistinguishable from the machine's point of view).
    cm->LootCorpse(777);
    cm->AddLootItem(50);
    ASSERT_EQ(cm->GetLootSubState(), LootSubState::AwaitingItems);

    cm->UpdateLootState();
    EXPECT_EQ(cm->GetLootSubState(), LootSubState::Idle)
        << "corpse-gone must abort the loot instead of hanging in AwaitingItems";
    EXPECT_NE(cm->GetCombatState(), COMBAT_STATE_LOOTING);
    EXPECT_EQ(cm->m_current_corpse_id, 0);
}

TEST(CombatManagerLoot, AwaitingItemsHardDeadlineClosesWhenAutoLootDisabled) {
    EverQuest eq = MakeEq();
    ASSERT_NO_THROW(ReplayZone(eq, HC_OP_NewSpawn,
        NewSpawnPayloadAt(901, "a_test_rat's_corpse1", 0, 10, 0)));
    auto* cm = eq.GetCombatManager();

    cm->SetAutoLootEnabled(false);
    cm->LootCorpse(901);
    cm->AddLootItem(50);
    ASSERT_EQ(cm->GetLootSubState(), LootSubState::AwaitingItems);

    // Corpse present, auto-loot disabled: quiescence never fires (gated on
    // m_auto_loot_enabled), so this would hang forever under the pre-T26
    // design. The hard deadline is the new safety net for exactly this.
    cm->UpdateLootState();
    EXPECT_EQ(cm->GetLootSubState(), LootSubState::AwaitingItems)
        << "not yet at the hard deadline";

    cm->m_loot_state_entered = std::chrono::steady_clock::now() -
        std::chrono::milliseconds(CombatManager::kAwaitingItemsHardDeadlineMs + 10);
    cm->UpdateLootState();
    EXPECT_EQ(cm->GetLootSubState(), LootSubState::Idle)
        << "hard deadline must close the session even with auto-loot off";
}

}  // namespace
