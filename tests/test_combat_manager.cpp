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
    // (the m_replay_suppressed_sends sites in eq_login_world.cpp /
    // eq_zone_handshake.cpp / eq_player_buffs.cpp), not by the generic public
    // EverQuest::QueuePacket(opcode, packet) overload CombatManager calls
    // (eq.cpp), which silently no-ops when m_zone_connection is null
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

// T230: the SetTargetByName_* tests were deleted with
// CombatManager::SetTarget(const std::string&) -- the second, case-sensitive,
// corpse-unaware name resolver (T173/T97) that no production code reached.
// Name resolution is covered where it lives now: EntityManagerTest.
// FindEntityByName* (test_state_modules.cpp) and CommandProcessorTest.
// Target_* (test_command_processor_combat_guild.cpp).

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

// T104: SetTarget()/ClearTarget() are the sole choke points that publish
// TargetChanged so Application::m_gameState->combat() can be fed without a
// per-frame poll (see combat.cpp's PublishTargetChanged()/
// PublishTargetCleared() and refactor/T104_combat_poll_to_bridge.md). Proven
// via a real bridge attached to EverQuest -- every other test in this file
// runs without one, which exercises the null-bridge no-op guard implicitly
// (none of them crash).
TEST(CombatManagerTargeting, SetTargetPublishesTargetChangedWithEntityData) {
    EverQuest eq = MakeEq();
    zonetest::TestBridge bridge;
    eq.setBridge(&bridge);
    ASSERT_NO_THROW(ReplayZone(eq, HC_OP_NewSpawn,
        NewSpawnPayloadAt(100, "a_test_rat", 0, 10, 0)));
    auto* cm = eq.GetCombatManager();
    bridge.drainEvents();  // discard the spawn publish

    ASSERT_TRUE(cm->SetTarget(100));

    auto events = bridge.drainEvents();
    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].type, eqt::state::GameEventType::TargetChanged);
    const auto& d = std::get<eqt::state::TargetChangedData>(events[0].data);
    EXPECT_EQ(d.spawnId, 100u);
    EXPECT_EQ(d.name, "a_test_rat");
    EXPECT_EQ(d.level, 5);
    EXPECT_EQ(d.hpPercent, 100);
}

TEST(CombatManagerTargeting, ClearTargetPublishesTargetChangedWithZeroSpawnId) {
    EverQuest eq = MakeEq();
    zonetest::TestBridge bridge;
    eq.setBridge(&bridge);
    ASSERT_NO_THROW(ReplayZone(eq, HC_OP_NewSpawn,
        NewSpawnPayloadAt(100, "a_test_rat", 0, 10, 0)));
    auto* cm = eq.GetCombatManager();
    ASSERT_TRUE(cm->SetTarget(100));
    bridge.drainEvents();  // discard the SetTarget publish, isolate ClearTarget's

    cm->ClearTarget();

    auto events = bridge.drainEvents();
    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].type, eqt::state::GameEventType::TargetChanged);
    const auto& d = std::get<eqt::state::TargetChangedData>(events[0].data);
    EXPECT_EQ(d.spawnId, 0u);
}

TEST(CombatManagerTargeting, SetTargetMissDoesNotPublish) {
    // Falsification pair for SetTargetPublishesTargetChangedWithEntityData:
    // a failed SetTarget (unknown id) must NOT publish -- proves the publish
    // is gated on success, not called unconditionally.
    EverQuest eq = MakeEq();
    zonetest::TestBridge bridge;
    eq.setBridge(&bridge);
    auto* cm = eq.GetCombatManager();

    EXPECT_FALSE(cm->SetTarget(999));
    EXPECT_TRUE(bridge.drainEvents().empty());
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

    uint64_t before = eq.Net().ReplaySuppressedSendCount();
    cm->EnableAutoAttack();
    EXPECT_FALSE(cm->IsAutoAttackEnabled());
    EXPECT_EQ(eq.Net().ReplaySuppressedSendCount(), before);
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

// T104/T221: EnableAutoAttack()/DisableAutoAttack() are the sole choke
// points that write GameState::combat().setAutoAttacking() (combat.cpp's
// WriteAutoAttackState()). T264 deleted the AutoAttackChanged bridge event
// they used to publish alongside (no consumer on either bridge), so the
// observable is the state write itself, and NO event.
TEST(CombatManagerAutoAttack, EnableAutoAttackWritesGameStateTrueAndPublishesNothing) {
    EverQuest eq = MakeEq();
    zonetest::TestBridge bridge;
    eq.setBridge(&bridge);
    ASSERT_NO_THROW(ReplayZone(eq, HC_OP_NewSpawn,
        NewSpawnPayloadAt(100, "a_test_rat", 0, 10, 0)));
    auto* cm = eq.GetCombatManager();
    ASSERT_TRUE(cm->SetTarget(100));
    bridge.drainEvents();  // discard the SetTarget publish
    ASSERT_FALSE(eq.GetGameState().combat().isAutoAttacking());

    cm->EnableAutoAttack();

    EXPECT_TRUE(eq.GetGameState().combat().isAutoAttacking());
    EXPECT_TRUE(bridge.drainEvents().empty());
}

TEST(CombatManagerAutoAttack, EnableWithoutTargetDoesNotPublish) {
    // Falsification pair: the no-target early return (see
    // EnableWithoutTargetIsNoOp above) must not write the state either --
    // the write sits after the guard, not before it.
    EverQuest eq = MakeEq();
    zonetest::TestBridge bridge;
    eq.setBridge(&bridge);
    auto* cm = eq.GetCombatManager();

    cm->EnableAutoAttack();

    EXPECT_FALSE(eq.GetGameState().combat().isAutoAttacking());
    EXPECT_TRUE(bridge.drainEvents().empty());
}

TEST(CombatManagerAutoAttack, DisableAutoAttackWritesGameStateFalseAndPublishesNothing) {
    EverQuest eq = MakeEq();
    zonetest::TestBridge bridge;
    eq.setBridge(&bridge);
    ASSERT_NO_THROW(ReplayZone(eq, HC_OP_NewSpawn,
        NewSpawnPayloadAt(100, "a_test_rat", 0, 10, 0)));
    auto* cm = eq.GetCombatManager();
    ASSERT_TRUE(cm->SetTarget(100));
    cm->EnableAutoAttack();
    ASSERT_TRUE(eq.GetGameState().combat().isAutoAttacking());
    bridge.drainEvents();

    cm->DisableAutoAttack();

    EXPECT_FALSE(eq.GetGameState().combat().isAutoAttacking());
    EXPECT_TRUE(bridge.drainEvents().empty());
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
    // opcode prefix (EverQuest::ZoneProcessDeath, eq_player_state.cpp).
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
// 3b. T245: Update() is a self-paced game-thread tick
// ===================================================================
//
// The 500 ms m_update_thread is gone; EverQuest::UpdateMovement() now calls
// CombatManager::Update() every game tick (~60 Hz) and Update() itself keeps
// the old 500 ms cadence via m_last_update / kUpdateIntervalMs. Observable
// effect used here: ScanForTargets() populating m_potential_targets (the
// same signal the Consider fixture below relies on). Both clocks are
// backdated with the file's T17 private access -- no sleeps.

TEST(CombatManagerTick, UpdateRunsOnFirstCallThenPacesToInterval) {
    EverQuest eq = MakeEq();
    ASSERT_NO_THROW(ReplayZone(eq, HC_OP_NewSpawn,
        NewSpawnPayloadAt(500, "a_cave_bat", 0, 15, 0, /*level=*/5, /*npc_type=*/1,
                           /*char_class=*/0)));
    auto* cm = eq.GetCombatManager();
    cm->Enable();

    // Fresh manager: m_last_update is the epoch, so the very first tick does
    // work (the old thread's first iteration did too). Open the scan gate.
    ASSERT_EQ(cm->m_last_update, std::chrono::steady_clock::time_point{});
    cm->m_last_target_scan = std::chrono::steady_clock::now() - std::chrono::seconds(2);
    cm->Update();
    ASSERT_NE(cm->GetHighestPriorityTarget(), nullptr)
        << "first Update() must run the scan immediately";
    EXPECT_NE(cm->m_last_update, std::chrono::steady_clock::time_point{})
        << "Update() stamps its pacing clock when it does work";

    // Within kUpdateIntervalMs of that call: a no-op even with the scan gate
    // open again. This is what makes the per-tick call from UpdateMovement()
    // equivalent to the deleted thread's sleep(500).
    cm->m_potential_targets.clear();
    cm->m_last_target_scan = std::chrono::steady_clock::now() - std::chrono::seconds(2);
    cm->Update();
    EXPECT_EQ(cm->GetHighestPriorityTarget(), nullptr)
        << "a second Update() inside the interval must not scan";

    // Past the interval: works again.
    cm->m_last_update = std::chrono::steady_clock::now() -
        std::chrono::milliseconds(CombatManager::kUpdateIntervalMs + 10);
    cm->Update();
    EXPECT_NE(cm->GetHighestPriorityTarget(), nullptr)
        << "Update() past kUpdateIntervalMs must scan again";
}

TEST(CombatManagerTick, UpdateIsANoOpWhenDisabled) {
    // /combat off (the default) -- the tick from UpdateMovement() must cost
    // nothing and touch nothing, whatever the clocks say.
    EverQuest eq = MakeEq();
    ASSERT_NO_THROW(ReplayZone(eq, HC_OP_NewSpawn,
        NewSpawnPayloadAt(500, "a_cave_bat", 0, 15, 0, /*level=*/5, /*npc_type=*/1,
                           /*char_class=*/0)));
    auto* cm = eq.GetCombatManager();
    ASSERT_FALSE(cm->IsEnabled());
    cm->m_last_target_scan = std::chrono::steady_clock::now() - std::chrono::seconds(2);
    cm->Update();
    EXPECT_EQ(cm->GetHighestPriorityTarget(), nullptr);
    EXPECT_EQ(cm->m_last_update, std::chrono::steady_clock::time_point{});
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
// Thread trace (re-read at J21, not guessed). Two paths reach the loot
// machine, and both end on the GAME THREAD:
//
//   * The UI "loot all" click arrives as LootAllIntent and is handled in
//     EverQuest::ProcessBridgeIntents (eq_bridge.cpp), which calls
//     CombatManager::LootAll(). (This is a change from the pre-T268 shape
//     this comment used to describe: there is no EverQuest::LootAllFromCorpse
//     any more.)
//   * CombatManager::ProcessCombatRound() (combat.cpp) calls
//     LootCorpse()/BeginLootRequest() when an auto-hunted target dies with
//     auto-loot enabled.
//
// The server's loot item response (OP_ItemPacket) is dispatched by
// DispatchZonePacket (eq_zone_dispatch.cpp since J02). T268 deleted the
// second, headless-only ZoneProcessLootItem parser that used to sit behind an
// "IsLooting()" branch there; every loot ItemPacket now goes to
// ZoneProcessLootItemToUI (eq_merchant.cpp) -- or, when a slot request is
// pending, ZoneProcessLootedItemToInventory -- and that path also feeds
// CombatManager's auto-loot queue. It runs directly and synchronously, no
// dispatch to another thread. Per CLAUDE.md's threading model, all network
// packet handling ("Network events (TickNetwork)") runs on the GAME THREAD,
// so CheckAutoLoot() (and everything it calls) must never block.
//
// T26 replaced the sleeps with LootSubState + CombatManager::UpdateLootState(),
// ticked every game frame from Application::updateGameState (unconditional,
// alongside UpdateMovement()) -- never from Update() (T245: itself a
// game-thread tick now, the old m_update_thread is gone). These
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
    // never spawns (or never matches the expected id). Drive the machine
    // directly into AwaitingCorpse the way NotifyTargetDied() does.
    cm->m_pending_corpse_id = 1;
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
        NewSpawnPayloadAt(900, "a_test_rat", 0, 10, 0)));

    // Death_Struct: victim_id(4) killer_id(4) ... (offsets include the
    // 2-byte opcode prefix, see CombatManagerCycling.CorpsesAreExcluded).
    // This is what actually flips is_corpse=true on entity 900 -- T33's
    // TickAwaitingCorpse now requires that flag, not just a name match.
    auto deathPayload = BuildPayload(30, [&](EQ::Net::DynamicPacket& p) {
        p.PutUInt32(2, 900);  // victim_id
        p.PutUInt32(6, 0);    // killer_id (0 = the player)
    });
    ASSERT_NO_THROW(ReplayZone(eq, HC_OP_Death, deathPayload));
    ASSERT_TRUE(eq.GetEntities().at(900).is_corpse);

    auto* cm = eq.GetCombatManager();
    // No target was set, so ZoneProcessDeath's own NotifyTargetDied() call
    // above was skipped (victim_id != GetTargetId()) -- drive
    // AwaitingCorpse directly here to isolate TickAwaitingCorpse's id
    // match from the ZoneProcessDeath wiring, which has its own test below.
    cm->m_pending_corpse_id = 900;
    cm->SetCombatState(COMBAT_STATE_LOOTING);
    cm->EnterLootSubState(LootSubState::AwaitingCorpse);

    cm->UpdateLootState();
    EXPECT_EQ(cm->GetLootSubState(), LootSubState::AwaitingItems)
        << "matching corpse entity should trigger BeginLootRequest()";
    EXPECT_EQ(cm->m_current_corpse_id, 900);
}

TEST(CombatManagerLoot, AwaitingCorpseIgnoresOtherCorpseAndMatchesRightId) {
    // T33's headline hazard: TickAwaitingCorpse used to accept the FIRST
    // is_corpse entity in the zone. Stage TWO corpses and confirm it picks
    // the one m_pending_corpse_id actually names, not whichever the map
    // iterates to first.
    EverQuest eq = MakeEq();
    ASSERT_NO_THROW(ReplayZone(eq, HC_OP_NewSpawn,
        NewSpawnPayloadAt(500, "a_stale_rat", 0, 10, 0)));
    ASSERT_NO_THROW(ReplayZone(eq, HC_OP_NewSpawn,
        NewSpawnPayloadAt(900, "a_test_rat", 0, 20, 0)));

    for (uint32_t id : {500u, 900u}) {
        auto deathPayload = BuildPayload(30, [&](EQ::Net::DynamicPacket& p) {
            p.PutUInt32(2, id);  // victim_id
            p.PutUInt32(6, 0);   // killer_id (0 = the player)
        });
        ASSERT_NO_THROW(ReplayZone(eq, HC_OP_Death, deathPayload));
    }
    ASSERT_TRUE(eq.GetEntities().at(500).is_corpse);
    ASSERT_TRUE(eq.GetEntities().at(900).is_corpse);

    auto* cm = eq.GetCombatManager();
    // We're awaiting the SECOND kill's corpse (900), not the pre-existing
    // one (500) that just happens to also satisfy is_corpse.
    cm->m_pending_corpse_id = 900;
    cm->SetCombatState(COMBAT_STATE_LOOTING);
    cm->EnterLootSubState(LootSubState::AwaitingCorpse);

    cm->UpdateLootState();
    EXPECT_EQ(cm->GetLootSubState(), LootSubState::AwaitingItems);
    EXPECT_EQ(cm->m_current_corpse_id, 900)
        << "must loot the specific victim's corpse, not the first is_corpse "
           "entity the map happens to iterate to (the pre-T33 bug)";
}

TEST(CombatManagerLoot, ZoneProcessDeathEntersAwaitingCorpseWhenAutoHunting) {
    // T33's actual defect: the real server death path (ZoneProcessDeath)
    // used to only call DisableAutoAttack(), which flips m_combat_state
    // out of ENGAGED and permanently forecloses ProcessCombatRound()'s
    // hp_percent<=0 poll -- the only door into AwaitingCorpse before this
    // unit. This test drives death the way a real server does (an
    // HC_OP_Death packet, not a synthetic hp_percent write) and would FAIL
    // against the pre-T33 code: GetLootSubState() would stay Idle and
    // GetCombatState() would land on IDLE, never LOOTING.
    EverQuest eq = MakeEq();
    ASSERT_NO_THROW(ReplayZone(eq, HC_OP_NewSpawn,
        NewSpawnPayloadAt(200, "a_test_rat", 0, 10, 0)));
    auto* cm = eq.GetCombatManager();

    ASSERT_TRUE(cm->SetTarget(200u));
    cm->EnableAutoAttack();
    ASSERT_EQ(cm->GetCombatState(), COMBAT_STATE_ENGAGED);
    // Bypass SetAutoHunting()'s pathfinding-enabled precondition (T32
    // finding, combat.cpp) -- this test is about the death-event wiring,
    // not the auto-hunt toggle's own gating.
    cm->m_auto_hunting_enabled = true;
    ASSERT_TRUE(cm->IsAutoLootEnabled()) << "auto_loot defaults true";

    auto deathPayload = BuildPayload(30, [&](EQ::Net::DynamicPacket& p) {
        p.PutUInt32(2, 200);  // victim_id
        p.PutUInt32(6, 0);    // killer_id (0 = the player)
    });
    ASSERT_NO_THROW(ReplayZone(eq, HC_OP_Death, deathPayload));

    EXPECT_FALSE(cm->IsAutoAttackEnabled());
    EXPECT_EQ(cm->GetCombatState(), COMBAT_STATE_LOOTING)
        << "the real death event must enter the loot machine, not leave it "
           "stuck at IDLE";
    EXPECT_EQ(cm->GetLootSubState(), LootSubState::AwaitingCorpse);
    EXPECT_EQ(cm->m_pending_corpse_id, 200);

    // And it actually advances: the corpse (same entity, same id, T32's
    // "Entity N became corpse" evidence) is already is_corpse=true from
    // ZoneProcessDeath itself, so the very next tick should find it.
    cm->UpdateLootState();
    EXPECT_EQ(cm->GetLootSubState(), LootSubState::AwaitingItems);
    EXPECT_EQ(cm->m_current_corpse_id, 200);
}

TEST(CombatManagerLoot, ZoneProcessDeathDoesNotLootWithoutAutoHunting) {
    // Landmine 1: auto_loot defaults true but auto_hunting defaults false,
    // so a plain kill (auto-attack, no /hunt on) must NOT enter the loot
    // machine. This is what keeps T33 from turning into "every player
    // auto-loots every corpse".
    EverQuest eq = MakeEq();
    ASSERT_NO_THROW(ReplayZone(eq, HC_OP_NewSpawn,
        NewSpawnPayloadAt(201, "a_test_rat", 0, 10, 0)));
    auto* cm = eq.GetCombatManager();

    ASSERT_TRUE(cm->SetTarget(201u));
    cm->EnableAutoAttack();
    ASSERT_FALSE(cm->IsAutoHunting()) << "auto_hunting defaults false";
    ASSERT_TRUE(cm->IsAutoLootEnabled()) << "auto_loot defaults true";

    auto deathPayload = BuildPayload(30, [&](EQ::Net::DynamicPacket& p) {
        p.PutUInt32(2, 201);
        p.PutUInt32(6, 0);
    });
    ASSERT_NO_THROW(ReplayZone(eq, HC_OP_Death, deathPayload));

    EXPECT_FALSE(cm->IsAutoAttackEnabled());
    EXPECT_NE(cm->GetCombatState(), COMBAT_STATE_LOOTING)
        << "auto_hunting=false must gate off auto-loot-on-kill";
    EXPECT_EQ(cm->GetLootSubState(), LootSubState::Idle);
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

// T268: with auto-loot OFF and items on the corpse, the window is the
// player's -- the machine parks in ManualOpen (no deadline; exits on
// LootAll(), a close, or corpse gone) instead of closing on the hard
// deadline as T26 had it. Closing on the deadline is what made "take ONE
// item" impossible: the window was gone 5 s after it opened.
TEST(CombatManagerLoot, AwaitingItemsWithAutoLootDisabledHoldsTheWindowOpen) {
    EverQuest eq = MakeEq();
    ASSERT_NO_THROW(ReplayZone(eq, HC_OP_NewSpawn,
        NewSpawnPayloadAt(901, "a_test_rat's_corpse1", 0, 10, 0)));
    auto* cm = eq.GetCombatManager();

    cm->SetAutoLootEnabled(false);
    cm->LootCorpse(901);
    cm->AddLootItem(50);
    ASSERT_EQ(cm->GetLootSubState(), LootSubState::AwaitingItems);

    cm->UpdateLootState();
    EXPECT_EQ(cm->GetLootSubState(), LootSubState::AwaitingItems)
        << "not yet quiescent";

    cm->m_loot_state_entered = std::chrono::steady_clock::now() -
        std::chrono::milliseconds(CombatManager::kAwaitingItemsHardDeadlineMs + 10);
    cm->UpdateLootState();
    EXPECT_EQ(cm->GetLootSubState(), LootSubState::ManualOpen)
        << "auto-loot off + items: hold the window for the player";
    EXPECT_EQ(cm->GetCombatState(), COMBAT_STATE_LOOTING);
    EXPECT_TRUE(eq.GetGameState().player().isLooting());

    // ManualOpen has no deadline: many ticks later it is still open.
    for (int i = 0; i < 5; ++i) cm->UpdateLootState();
    EXPECT_EQ(cm->GetLootSubState(), LootSubState::ManualOpen);

    // LootAll() from ManualOpen drives the same drain as auto-loot.
    ASSERT_TRUE(cm->LootAll());
    EXPECT_EQ(cm->GetLootSubState(), LootSubState::LootingItems);
}

TEST(CombatManagerLoot, AwaitingItemsHardDeadlineClosesWhenNothingArrived) {
    EverQuest eq = MakeEq();
    ASSERT_NO_THROW(ReplayZone(eq, HC_OP_NewSpawn,
        NewSpawnPayloadAt(901, "a_test_rat's_corpse1", 0, 10, 0)));
    auto* cm = eq.GetCombatManager();

    cm->SetAutoLootEnabled(false);
    cm->LootCorpse(901);
    ASSERT_EQ(cm->GetLootSubState(), LootSubState::AwaitingItems);

    // No items ever arrived (and no LootRequest response either): the hard
    // deadline is still the safety net that closes the session.
    cm->m_loot_state_entered = std::chrono::steady_clock::now() -
        std::chrono::milliseconds(CombatManager::kAwaitingItemsHardDeadlineMs + 10);
    cm->UpdateLootState();
    EXPECT_EQ(cm->GetLootSubState(), LootSubState::Idle)
        << "hard deadline must close an empty session";
    EXPECT_FALSE(eq.GetGameState().player().isLooting());
}

TEST(CombatManagerLoot, ManualOpenCorpseGoneAbortsToIdle) {
    EverQuest eq = MakeEq();
    ASSERT_NO_THROW(ReplayZone(eq, HC_OP_NewSpawn,
        NewSpawnPayloadAt(902, "a_test_rat's_corpse2", 0, 10, 0)));
    auto* cm = eq.GetCombatManager();
    cm->SetAutoLootEnabled(false);
    cm->LootCorpse(902);
    cm->AddLootItem(50);
    cm->m_last_loot_item_time = std::chrono::steady_clock::now() -
        std::chrono::milliseconds(CombatManager::kItemQuiescenceMs + 10);
    cm->UpdateLootState();
    ASSERT_EQ(cm->GetLootSubState(), LootSubState::ManualOpen);

    // The corpse decays / is deleted server-side.
    ASSERT_NO_THROW(ReplayZone(eq, HC_OP_DeleteSpawn,
        BuildPayload(6, [&](EQ::Net::DynamicPacket& p) { p.PutUInt32(2, 902); })));
    cm->UpdateLootState();
    EXPECT_EQ(cm->GetLootSubState(), LootSubState::Idle);
    EXPECT_NE(cm->GetCombatState(), COMBAT_STATE_LOOTING);
    EXPECT_FALSE(eq.GetGameState().player().isLooting());
}

// ===================================================================
// Consider opcode selection (T153)
// ===================================================================
//
// EverQuest::SendConsiderCorpse() had zero production callers before T153 and
// sent a 4-byte body where eqemu's Handle_OP_ConsiderCorpse
// (zone/client_packet.cpp:5245) gates on sizeof(Consider_Struct) == 28 -- so
// it was silently dropped, and nothing had ever noticed. T153 fixed the body
// and folded the corpse case into the existing /consider rather than adding a
// second command.
//
// Per the T23 note at the top of this file, a CombatManager-initiated send is
// not observable from a test (the generic EverQuest::QueuePacket overload
// no-ops with no side effect when there is no zone connection). So the assert
// is on the routing decision itself, which ConsiderTarget() consults.

TEST(CombatManagerConsiderOpcode, NoTarget_SelectsNothing) {
    EverQuest eq = MakeEq();
    auto* cm = eq.GetCombatManager();
    EXPECT_FALSE(cm->HasTarget());
    EXPECT_EQ(cm->GetConsiderOpcodeForTarget(), 0);
}

TEST(CombatManagerConsiderOpcode, LivingTarget_SelectsPlainConsider) {
    EverQuest eq = MakeEq();
    ASSERT_NO_THROW(ReplayZone(eq, HC_OP_NewSpawn,
        NewSpawnPayloadAt(100, "a_test_rat", 0, 10, 0)));
    auto* cm = eq.GetCombatManager();

    ASSERT_TRUE(cm->SetTarget(100));
    EXPECT_EQ(cm->GetConsiderOpcodeForTarget(), HC_OP_Consider);
    // The pre-T153 behaviour must be untouched for the living case.
    ASSERT_NO_THROW(cm->ConsiderTarget());
}

TEST(CombatManagerConsiderOpcode, CorpseTarget_SelectsConsiderCorpse) {
    EverQuest eq = MakeEq();
    ASSERT_NO_THROW(ReplayZone(eq, HC_OP_NewSpawn,
        NewSpawnPayloadAt(200, "a_test_rat's_corpse0", 0, 10, 0)));
    auto* cm = eq.GetCombatManager();

    ASSERT_TRUE(cm->SetTarget(200));
    // Routes through the same CombatManager::IsCorpse() the loot path uses --
    // there is deliberately no second corpse test.
    ASSERT_TRUE(cm->IsCorpse(eq.GetEntities().at(200)));
    EXPECT_EQ(cm->GetConsiderOpcodeForTarget(), HC_OP_ConsiderCorpse);
    ASSERT_NO_THROW(cm->ConsiderTarget());
}

TEST(CombatManagerConsiderOpcode, TargetNotInEntityList_FallsBackToPlainConsider) {
    // SetTarget() only accepts known entities, so this is reached by a corpse
    // despawning between targeting and /consider. Plain OP_Consider is the
    // safe fallback: the server just fails to resolve the id and says nothing.
    EverQuest eq = MakeEq();
    ASSERT_NO_THROW(ReplayZone(eq, HC_OP_NewSpawn,
        NewSpawnPayloadAt(300, "a_test_rat", 0, 10, 0)));
    auto* cm = eq.GetCombatManager();
    ASSERT_TRUE(cm->SetTarget(300));

    cm->m_current_target_id = 999;  // not in m_entities
    EXPECT_EQ(cm->GetConsiderOpcodeForTarget(), HC_OP_Consider);
}


// ===================================================================
// Corpse recognition (T160)
// ===================================================================
//
// T155's live probe 2 FAILED: /consider on a player corpse cons'd it as a
// living mob. eqemu names corpses in two shapes -- Corpse::CalcCorpseName
// (zone/corpse.cpp:612) writes "'s corpse<id>" (apostrophe + SPACE) for a
// player corpse and "`s_corpse<id>" (BACKTICK) for an NPC corpse, and
// NPC::Death (zone/attack.cpp:2853) renames the latter to "'s_corpse<id>"
// over OP_MobRename. The client only ever matched the third form, and the
// is_corpse flag is only set by handlers that require having WITNESSED the
// death. So a player corpse, or any corpse older than our zone-in, was live.

TEST(CorpseNaming, PlayerCorpseSpaceFormIsParsed) {
    std::string base;
    uint32_t id = 0;
    EXPECT_TRUE(ParseCorpseName("Testmelee's corpse456", &base, &id));
    EXPECT_EQ(base, "Testmelee");
    EXPECT_EQ(id, 456u);
}

TEST(CorpseNaming, NpcCorpseBothFormsAreParsed) {
    std::string base;
    uint32_t id = 0;
    // Post-rename form -- the only one recognised before T160.
    EXPECT_TRUE(ParseCorpseName("Probemob's_corpse123", &base, &id));
    EXPECT_EQ(base, "Probemob");
    EXPECT_EQ(id, 123u);
    // Server-side backtick form: what a corpse that predates our zone-in
    // arrives with, because the rename is only broadcast at death.
    EXPECT_TRUE(ParseCorpseName("Probemob`s_corpse123", &base, &id));
    EXPECT_EQ(base, "Probemob");
    EXPECT_EQ(id, 123u);
}

// T185 -- MakeCorpseName, the inverse of ParseCorpseName.
//
// eqemu does not broadcast a rename for player corpses, so the client has to
// derive the name; Corpse::CalcCorpseName's own comment says "the client does
// this too, so it's unchangable". These pin the derivation against
// CalcCorpseName + EntityList::RemoveNumbers rather than against our own idea
// of what the name should look like.

TEST(CorpseNaming, MakeCorpseNameMatchesTheServerFormsExactly) {
    // Player: apostrophe + SPACE + "corpse". NPC: BACKTICK + "s_corpse".
    // The two are not interchangeable -- ParseCorpseName treats them as
    // distinct forms, and a mismatch is a corpse nobody can name.
    EXPECT_EQ(MakeCorpseName("Testmelee", 275, true), "Testmelee's corpse275");
    EXPECT_EQ(MakeCorpseName("a_cave_rat", 9, false), "a_cave_rat`s_corpse9");
}

TEST(CorpseNaming, MakeCorpseNameStripsEveryDigitLikeRemoveNumbers) {
    // EntityList::RemoveNumbers (zone/entity.cpp:3259) drops EVERY digit, not
    // just a trailing spawn suffix -- it walks the whole string. Spawn names
    // routinely carry one ("Guard_Rahtiz000").
    EXPECT_EQ(MakeCorpseName("Guard_Rahtiz000", 172, false),
              "Guard_Rahtiz`s_corpse172");
    EXPECT_EQ(MakeCorpseName("a_cave_rat2", 26, false), "a_cave_rat`s_corpse26");
    // Interior digits go too. This is faithful, not tidy.
    EXPECT_EQ(MakeCorpseName("Guard2Bob", 5, false), "GuardBob`s_corpse5");
}

// The two functions have to agree, or a corpse we name is a corpse we cannot
// find again.
TEST(CorpseNaming, MakeCorpseNameRoundTripsThroughParseCorpseName) {
    for (bool player : {true, false}) {
        const std::string made = MakeCorpseName("Testmelee", 4242, player);
        std::string base;
        uint32_t id = 0;
        ASSERT_TRUE(ParseCorpseName(made, &base, &id)) << made;
        EXPECT_EQ(base, "Testmelee") << made;
        EXPECT_EQ(id, 4242u) << made;
    }
}

TEST(CorpseNaming, LivingNamesWithCorpseInThemAreNotCorpses) {
    // The false-positive guard. None of these has a separator followed by a
    // run of digits.
    EXPECT_FALSE(ParseCorpseName("a_corpse_beetle"));
    EXPECT_FALSE(ParseCorpseName("a_corpse_beetle001"));
    EXPECT_FALSE(ParseCorpseName("Corpsemaker"));
    EXPECT_FALSE(ParseCorpseName("corpse"));
    EXPECT_FALSE(ParseCorpseName(""));
    // Separator present but no id: not a name eqemu can produce.
    EXPECT_FALSE(ParseCorpseName("Bob's corpse"));
    EXPECT_FALSE(ParseCorpseName("Bob's_corpse"));
    EXPECT_FALSE(ParseCorpseName("Bob's corpse12x"));
    // Absurdly long digit run must be rejected, not thrown out of stoul.
    EXPECT_FALSE(ParseCorpseName("Bob's corpse123456789012345678901234567890"));
}

TEST(CombatManagerCorpseRecognition, PlayerCorpseSpawnedAtZoneInIsACorpse) {
    // The zone-in path, not the death path: a bulk HC_OP_ZoneSpawns batch,
    // no HC_OP_Death and no HC_OP_MobRename anywhere. This is the case
    // T155 probed and the one that FAILED.
    EverQuest eq = MakeEq();
    auto payload = BuildPayload(2 + 2 * 385, [&](EQ::Net::DynamicPacket& p) {
        // npc_type 2 is what eqemu's Corpse::FillSpawnStruct sends for every
        // corpse (zone/corpse.cpp:1792), player or NPC.
        WriteSpawnStruct(p, 2, 30, "Testmelee's corpse30", 20, 54, 1, 0, 2, 100,
                         EncodePos(10, 10, 0, 0));
    });
    ASSERT_NO_THROW(ReplayZone(eq, HC_OP_ZoneSpawns, payload));

    const auto& entities = eq.GetEntities();
    ASSERT_EQ(entities.count(30), 1u);
    EXPECT_FALSE(entities.at(30).is_corpse)
        << "spawn parsing still does not write the flag -- recognition is the "
           "predicate's job, so no consumer of the raw field moved";

    auto* cm = eq.GetCombatManager();
    EXPECT_TRUE(cm->IsCorpse(entities.at(30)));
    ASSERT_TRUE(cm->SetTarget(30));
    EXPECT_EQ(cm->GetConsiderOpcodeForTarget(), HC_OP_ConsiderCorpse)
        << "T153's corpse-consider route must now fire for a player corpse";
}

TEST(CombatManagerCorpseRecognition, NpcCorpseNameAloneIsACorpse) {
    // Regression: the pre-T160 form, and with npc_type=1 so the NAME is the
    // only thing carrying the verdict.
    EverQuest eq = MakeEq();
    ASSERT_NO_THROW(ReplayZone(eq, HC_OP_NewSpawn,
        NewSpawnPayloadAt(31, "Probemob's_corpse31", 0, 10, 0, 5, /*npc_type=*/1)));
    ASSERT_NO_THROW(ReplayZone(eq, HC_OP_NewSpawn,
        NewSpawnPayloadAt(32, "Probemob`s_corpse32", 0, 12, 0, 5, /*npc_type=*/1)));
    auto* cm = eq.GetCombatManager();

    EXPECT_TRUE(cm->IsCorpse(eq.GetEntities().at(31)));
    EXPECT_TRUE(cm->IsCorpse(eq.GetEntities().at(32)));
    ASSERT_TRUE(cm->SetTarget(31));
    EXPECT_EQ(cm->GetConsiderOpcodeForTarget(), HC_OP_ConsiderCorpse);
}

TEST(CombatManagerCorpseRecognition, LivingCorpseBeetleIsNotACorpse) {
    EverQuest eq = MakeEq();
    ASSERT_NO_THROW(ReplayZone(eq, HC_OP_NewSpawn,
        NewSpawnPayloadAt(33, "a_corpse_beetle001", 0, 10, 0, 5, /*npc_type=*/1)));
    auto* cm = eq.GetCombatManager();

    EXPECT_FALSE(cm->IsCorpse(eq.GetEntities().at(33)));
    ASSERT_TRUE(cm->SetTarget(33));
    EXPECT_EQ(cm->GetConsiderOpcodeForTarget(), HC_OP_Consider)
        << "a living mob with 'corpse' in its name must still con normally";
}

TEST(CombatManagerCorpseRecognition, PlayerCorpseIsExcludedFromTargetCycling) {
    // The one deliberate targeting change: CycleTargets' "skip corpses" test
    // moved from the raw is_corpse flag to IsCorpse(), so a corpse we never
    // watched die is no longer tab-targetable.
    EverQuest eq = MakeEq();
    ASSERT_NO_THROW(ReplayZone(eq, HC_OP_NewSpawn,
        NewSpawnPayloadAt(34, "Testmelee's corpse34", 0, 10, 0, 5, /*npc_type=*/2)));
    ASSERT_NO_THROW(ReplayZone(eq, HC_OP_NewSpawn,
        NewSpawnPayloadAt(35, "a_live_rat", 0, 40, 0, 5, /*npc_type=*/1)));
    auto* cm = eq.GetCombatManager();

    cm->CycleTargets(true);
    EXPECT_EQ(cm->GetTargetId(), 35)
        << "the player corpse (id 34) must be skipped even though nothing "
           "ever set its is_corpse flag";
}

TEST(CombatManagerCorpseRecognition, MobRenameToPlayerCorpseFormSetsTheFlag) {
    // The HC_OP_MobRename handler carried the second copy of the literal.
    // MobRename_Struct: old_name[64], old_name_again[64], new_name[64] --
    // the handler reads the FIRST 64-byte field (the HC_OP_MobRename case in
    // DispatchZonePacket, eq_zone_dispatch.cpp).
    EverQuest eq = MakeEq();
    ASSERT_NO_THROW(ReplayZone(eq, HC_OP_NewSpawn,
        NewSpawnPayloadAt(36, "Testmelee", 0, 10, 0, 5, /*npc_type=*/0)));

    auto renamePayload = BuildPayload(2 + 64 * 3 + 8, [&](EQ::Net::DynamicPacket& p) {
        p.PutCString(2, "Testmelee's corpse36");
    });
    ASSERT_NO_THROW(ReplayZone(eq, HC_OP_MobRename, renamePayload));

    const auto& e = eq.GetEntities().at(36);
    EXPECT_TRUE(e.is_corpse) << "the space form must be recognised by the "
                                "rename handler too, not just the underscore one";
    EXPECT_EQ(e.name, "Testmelee's corpse36");
}

}  // namespace
