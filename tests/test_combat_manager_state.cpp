// T23 / T245 -- CombatManager's self-paced Update() tick, the consider
// round-trip, the combat state machine and the config round-trips.
//
// T446 split test_combat_manager.cpp (1,316 lines) by functional group; the
// seam, the T17 private-access preamble and the unit's scope notes live in
// combat_manager_test_fixture.h. Same target, same suites, same case names.

#include "combat_manager_test_fixture.h"

namespace {

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
// T414 Leg B (board B299) resolved what this block used to describe.
//
// SetAssistRange() and SetCombatRange() had exactly ONE caller each -- the
// EXPECT_NO_THROW smoke test that used to live here -- and their members were
// read by nothing, so the "no public getter to round-trip against" problem was
// not a testability gap: there was no behaviour to observe because the values
// reached no decision.  Both setters and their members are gone, along with
// m_spell_range.  What actually decides each of the three is elsewhere and is
// covered by its own tests: /assist is a server round-trip, melee reach is
// CalculateCombatRange(my_size, target_size, race), and spell range comes from
// the spell row.
//
// SetAggroRadius() survives -- m_aggro_radius IS read (GetNearbyHostiles() and
// the hunt scan) -- so it now gets a real round-trip through observable
// behaviour rather than an EXPECT_NO_THROW: with the radius at 0 no hostile
// can be within it, and the potential-target scan must come back empty.

TEST(CombatManagerConfig, AggroRadiusIsHonouredByTheHostileScan) {
    EverQuest eq = MakeEq();
    auto* cm = eq.GetCombatManager();

    // A generous radius and a zero radius must not agree, and zero must be
    // empty: the radius is a real gate, not a stored number.
    cm->SetAggroRadius(0.0f);
    EXPECT_TRUE(cm->GetNearbyHostiles(0.0f).empty());

    // The setter is total: any value round-trips through the scan without
    // throwing, including the large one the deleted smoke test used.
    EXPECT_NO_THROW(cm->SetAggroRadius(40.0f));
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

}  // namespace
