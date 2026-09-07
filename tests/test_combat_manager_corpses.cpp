// T26 / T153 / T160 -- everything downstream of something dying: the
// event-driven loot state machine, the /consider opcode choice for a corpse
// target, and corpse-name recognition.
//
// T446 split test_combat_manager.cpp (1,316 lines) by functional group; the
// seam, the T17 private-access preamble and the unit's scope notes live in
// combat_manager_test_fixture.h. Same target, same suites, same case names.

#include "combat_manager_test_fixture.h"

namespace {

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
