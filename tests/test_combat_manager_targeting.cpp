// T23 -- CombatManager targeting, auto-attack/auto-fire and target cycling.
//
// T446 split test_combat_manager.cpp (1,316 lines) by functional group; the
// seam, the T17 private-access preamble and the unit's scope notes live in
// combat_manager_test_fixture.h. Same target, same suites, same case names.

#include "combat_manager_test_fixture.h"

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

}  // namespace
