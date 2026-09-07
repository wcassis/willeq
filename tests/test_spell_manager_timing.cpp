// T24 — SpellManager coverage, part 2 of 2 (spell/combat batch, unit 2 of 3).
// Covers priorities 3-5 of refactor/T24_spell_manager.md: cooldown/
// memorization timing (including the dead-delta_time finding), interrupt +
// server packet handlers, and callbacks, plus getCastProgress/
// getCastTimeRemaining. See test_spell_manager.cpp (part 1) for the seam
// rationale, the private-access technique, and gem-state-machine/cast-
// validation-matrix coverage — split purely to keep each file under the
// plan's ~800-line guidance; both files exercise the same live SpellManager
// via the same seam, there is no behavioral split.
//
// None of this file needs the real SQLite spell DB: every test here either
// drives pure state-machine logic (interrupt/handlers/callbacks, all
// null-check spell lookups internally) or backdates private timestamp
// members directly (T17/T23 `#define private public` technique — see part
// 1's header for why this is safe with spell_manager.h specifically), so
// main() below is a plain InitGoogleTest()/RUN_ALL_TESTS() with no DB
// bootstrap.

#include <gtest/gtest.h>

#define private public
#define protected public
#include "client/spell/spell_manager.h"
#undef private
#undef protected

#include "client/combat.h"
#include "zone_stream_test_utils.h"

#include <chrono>
#include <fstream>
#include <sstream>
#include <string>

using namespace zonetest;
using namespace EQ;

// ===================================================================
// 3. Cooldowns + memorization timing (backdating -- trap 1: update()
// does not honor delta_time, all gating is std::chrono::steady_clock).
// ===================================================================

TEST(SpellManagerTiming, GemCooldownProgressAndRemainingOnReadyGem) {
    EverQuest eq = MakeEq();
    auto* mgr = eq.GetSpellManager();
    EXPECT_FLOAT_EQ(mgr->getGemCooldownProgress(0), 1.0f);
    EXPECT_EQ(mgr->getGemCooldownRemaining(0), 0u);
}

TEST(SpellManagerTiming, GemCooldownProgressAndRemainingInvalidSlot) {
    EverQuest eq = MakeEq();
    auto* mgr = eq.GetSpellManager();
    EXPECT_FLOAT_EQ(mgr->getGemCooldownProgress(EQ::MAX_SPELL_GEMS), 1.0f);
    EXPECT_EQ(mgr->getGemCooldownRemaining(EQ::MAX_SPELL_GEMS), 0u);
}

TEST(SpellManagerTiming, StartGemCooldownThenBackdatePastDurationTransitionsToReady) {
    EverQuest eq = MakeEq();
    auto* mgr = eq.GetSpellManager();
    zonetest::TestBridge bridge;
    mgr->setBridge(&bridge);

    mgr->startGemCooldown(0, 5000);
    ASSERT_EQ(mgr->getGemState(0), GemState::Refresh);
    bridge.drainEvents();

    // Backdate past the 5000ms window without sleeping.
    mgr->m_gem_cooldown_start[0] = std::chrono::steady_clock::now() - std::chrono::milliseconds(6000);
    mgr->updateGemCooldowns(0.0f);  // delta_time is ignored -- pass 0 deliberately

    EXPECT_EQ(mgr->getGemState(0), GemState::Ready);
    EXPECT_EQ(mgr->getGemCooldownRemaining(0), 0u);

    auto events = bridge.drainEvents();
    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].type, eqt::state::GameEventType::SpellGemChanged);
}

TEST(SpellManagerTiming, GemCooldownProgressIsPartialMidway) {
    EverQuest eq = MakeEq();
    auto* mgr = eq.GetSpellManager();
    mgr->startGemCooldown(0, 10000);
    mgr->m_gem_cooldown_start[0] = std::chrono::steady_clock::now() - std::chrono::milliseconds(5000);

    float progress = mgr->getGemCooldownProgress(0);
    EXPECT_GT(progress, 0.3f);
    EXPECT_LT(progress, 0.7f);
    uint32_t remaining = mgr->getGemCooldownRemaining(0);
    EXPECT_GT(remaining, 0u);
    EXPECT_LT(remaining, 10000u);
}

TEST(SpellManagerTiming, StartGemCooldownEnforcesMinimum1500ms) {
    EverQuest eq = MakeEq();
    auto* mgr = eq.GetSpellManager();
    mgr->startGemCooldown(0, 200);  // below the 1500ms floor
    EXPECT_EQ(mgr->m_gem_cooldown_duration[0], 1500u);
}

TEST(SpellManagerTiming, MemorizeProgressNotMemorizingReturns1) {
    EverQuest eq = MakeEq();
    auto* mgr = eq.GetSpellManager();
    EXPECT_FLOAT_EQ(mgr->getMemorizeProgress(0), 1.0f);
}

TEST(SpellManagerTiming, MemorizeProgressDifferentSlotReturns1) {
    EverQuest eq = MakeEq();
    auto* mgr = eq.GetSpellManager();
    mgr->m_is_memorizing = true;
    mgr->m_memorize_slot = 2;
    mgr->m_memorize_start_time = std::chrono::steady_clock::now();
    mgr->m_memorize_duration_ms = 5000;

    EXPECT_FLOAT_EQ(mgr->getMemorizeProgress(0), 1.0f) << "slot 0 isn't the one being memorized";
    EXPECT_LT(mgr->getMemorizeProgress(2), 1.0f);
}

TEST(SpellManagerTiming, BackdatedMemorizationCompletesOnUpdateRegardlessOfDeltaTime) {
    // Directly stages memorization state via private members rather than
    // the real memorizeSpell() workflow, so this doesn't need the DB --
    // updateMemorization() reads spell data only for a log line (guarded by
    // `spell ?  : "Unknown"`), never for the completion decision itself.
    EverQuest eq = MakeEq();
    auto* mgr = eq.GetSpellManager();
    zonetest::TestBridge bridge;
    mgr->setBridge(&bridge);

    mgr->m_is_memorizing = true;
    mgr->m_memorize_slot = 3;
    mgr->m_memorize_spell_id = 12345;  // need not exist in any DB
    mgr->m_memorize_duration_ms = 4000;
    mgr->m_memorize_start_time = std::chrono::steady_clock::now() - std::chrono::milliseconds(5000);

    mgr->updateMemorization(0.0f);  // delta_time ignored -- pass 0 deliberately

    EXPECT_FALSE(mgr->isMemorizing());
    EXPECT_EQ(mgr->getMemorizedSpell(3), 12345u);
    EXPECT_EQ(mgr->getGemState(3), GemState::Ready);

    auto events = bridge.drainEvents();
    bool sawComplete = false;
    for (auto& e : events) {
        if (e.type == eqt::state::GameEventType::SpellMemorizeVisualComplete) sawComplete = true;
    }
    EXPECT_TRUE(sawComplete);
}

// ===================================================================
// 3b. T45: EverQuest::GameTick() now runs headless once the GAME (not the
// renderer) is ready. Both of GameTick()'s gates (its own
// `#ifdef EQT_HAS_GRAPHICS { if (!m_graphics_initialized) return; }`, and
// Application::updateGameState()'s separate `if (m_graphicsInitialized)`
// call-site gate, mirrored here since Application isn't unit-testable)
// change from "the renderer subsystem exists" -- permanently false in this
// no-graphics test binary, since InitGraphics() is never called here,
// matching a real --no-graphics run -- to also accept
// EverQuest::IsGraphicsReady() (loading phase == COMPLETE), the same
// game-lifecycle "state ready" signal T41 used for the full-state snapshot,
// set from EverQuest::OnGameStateComplete()'s no-graphics branch.
//
// Memorization is used as the "requires update() to have run" proxy, per
// the plan's own instruction and the immediately-preceding test's proof
// that updateMemorization()'s *completion* is a real state-machine
// transition (GemState::Memorize -> Ready), not something T44's
// countdown-on-read model could fake. The only difference from that test:
// driving the resolution through EverQuest::GameTick() (the thing this unit
// actually fixes) instead of calling SpellManager::updateMemorization()
// directly.
// ===================================================================

// Before: GameTick() must still be a no-op before the game reaches
// IsGraphicsReady() -- a fresh EverQuest (no zone-in at all) is
// simultaneously "not ready" and "not graphics" here, matching the pre-fix
// production state for the entire startup/connecting/authenticating window
// in both graphics and headless runs. Pins that GameTick() didn't just start
// running unconditionally.
//
// EQT_HAS_GRAPHICS only: this exercises EverQuest::GameTick()'s OWN
// `#ifdef EQT_HAS_GRAPHICS { if (!m_graphics_initialized && !IsGraphicsReady())
// return; }` gate directly. In an EQT_GRAPHICS=OFF configure that whole gate
// is compiled out -- it always has been, T45 didn't add or remove the
// `#ifdef` wrapper, only changed the condition inside it -- so GameTick()
// called directly (bypassing Application) has never respected readiness in
// that configure; the sole gate for EQT_GRAPHICS=OFF lives at
// Application::updateGameState()'s call site, which this unit test (by
// design, per the plan's territory) does not exercise. Confirmed by running
// this exact test in the build-nogfx tree: it fails, precisely because
// GameTick() ran unconditionally there -- see the T45 Review for why that's
// pre-existing structure, not a gap this fix introduced.
#ifdef EQT_HAS_GRAPHICS
TEST(GameTickHeadless, BeforeGameStateReady_GameTickDoesNotResolveMemorization) {
    EverQuest eq = MakeEq();
    ASSERT_FALSE(eq.IsGraphicsReady());
    auto* mgr = eq.GetSpellManager();

    mgr->m_is_memorizing = true;
    mgr->m_memorize_slot = 3;
    mgr->m_memorize_spell_id = 22222;  // need not exist in any DB
    mgr->m_memorize_duration_ms = 4000;
    mgr->m_memorize_start_time = std::chrono::steady_clock::now() - std::chrono::milliseconds(5000);
    mgr->m_gem_states[3] = GemState::MemorizeProgress;

    eq.GameTick(0.0f);  // delta_time is irrelevant either way -- see trap 1 below

    EXPECT_TRUE(mgr->isMemorizing())
        << "GameTick() must not run before the game is ready -- true both "
           "before and after T45's fix, only the gate's condition changed";
    EXPECT_EQ(mgr->getGemState(3), GemState::MemorizeProgress)
        << "gem state must not have advanced -- SpellManager::update() never ran";
    EXPECT_EQ(mgr->getMemorizedSpell(3), SPELL_UNKNOWN);
}
#endif  // EQT_HAS_GRAPHICS

// After: once EverQuest::OnGameStateComplete()'s no-graphics branch has run
// (simulated directly via the same SetLoadingPhase(COMPLETE) call it makes --
// test_zone_connection_flow_snapshot.cpp's FullStateSnapshot tests drive this via a
// full synthetic zone-in and independently confirm it lands on
// LoadingPhase::COMPLETE / IsGraphicsReady()==true), GameTick() must now
// actually run in this no-graphics test binary. Before T45 it never did, in
// either build tree -- m_graphics_initialized (EverQuest's own gate) is
// never set true here at all (InitGraphics() is never called), so it blocked
// unconditionally regardless of game-state readiness.
TEST(GameTickHeadless, AfterGameStateReady_GameTickResolvesMemorizationViaSpellManagerUpdate) {
    EverQuest eq = MakeEq();
    eq.SetLoadingPhase(LoadingPhase::COMPLETE);
    ASSERT_TRUE(eq.IsGraphicsReady());
    auto* mgr = eq.GetSpellManager();

    mgr->m_is_memorizing = true;
    mgr->m_memorize_slot = 3;
    mgr->m_memorize_spell_id = 22222;
    mgr->m_memorize_duration_ms = 4000;
    mgr->m_memorize_start_time = std::chrono::steady_clock::now() - std::chrono::milliseconds(5000);
    mgr->m_gem_states[3] = GemState::MemorizeProgress;

    // Same call as the "Before" test above -- only IsGraphicsReady() differs.
    eq.GameTick(0.0f);

    EXPECT_FALSE(mgr->isMemorizing())
        << "GameTick() should have run SpellManager::update() -> "
           "updateMemorization(), completing a backdated memorization -- if "
           "still memorizing, GameTick()'s gate is still blocking headless "
           "post-fix";
    EXPECT_EQ(mgr->getGemState(3), GemState::Ready);
    EXPECT_EQ(mgr->getMemorizedSpell(3), 22222u);
}

// ---- The dead-delta_time finding, pinned explicitly (plan section "Two
// traps I verified while scoping", trap 1). update(float delta_time) never
// reads delta_time; all three sub-updates gate on steady_clock::now(). A
// caller that passes a scaled/paused/huge delta (as a headless or
// fast-forward test harness would) gets silently ignored -- only real
// wall-clock time advances any of this. Two independent channels pinned
// below (gem cooldown, cast timeout) to show this isn't a one-off.

TEST(SpellManagerTiming, BUG_UpdateIgnoresDeltaTimeHugeDeltaDoesNotFastForwardGemCooldown) {
    EverQuest eq = MakeEq();
    auto* mgr = eq.GetSpellManager();

    mgr->startGemCooldown(0, 5000);
    ASSERT_EQ(mgr->getGemState(0), GemState::Refresh);

    // A delta_time of 999999.0f (~11.5 days) passed to update() must NOT
    // fast-forward a cooldown that has barely had any real wall-clock time
    // elapse -- if delta_time were honored, this would immediately flip to
    // Ready. It stays Refresh because update()/updateGemCooldowns() never
    // reads delta_time (spell_manager.cpp:57-60/115-131).
    mgr->update(999999.0f);
    EXPECT_EQ(mgr->getGemState(0), GemState::Refresh)
        << "update()'s delta_time parameter must be dead for this to hold; "
           "if this starts failing, delta_time started being honored and "
           "the T17-style backdating tests above should be revisited too.";

    // Backdating real wall-clock time (not delta_time) DOES complete it,
    // confirming the gate is steady_clock-based, not a no-op.
    mgr->m_gem_cooldown_start[0] = std::chrono::steady_clock::now() - std::chrono::milliseconds(6000);
    mgr->update(0.0f);  // delta_time 0 this time -- still transitions
    EXPECT_EQ(mgr->getGemState(0), GemState::Ready);
}

TEST(SpellManagerTiming, BUG_UpdateIgnoresDeltaTimeHugeDeltaDoesNotFastForwardCastTimeout) {
    EverQuest eq = MakeEq();
    auto* mgr = eq.GetSpellManager();
    zonetest::TestBridge bridge;
    mgr->setBridge(&bridge);

    // Stage an in-progress cast waiting on the "no Action packet, timed out"
    // branch (update()'s second cast-timeout check, spell_manager.cpp:85-112).
    mgr->m_is_casting = true;
    mgr->m_waiting_for_server_confirm = false;
    mgr->m_current_spell_id = 12345;  // need not exist in any DB
    mgr->m_current_gem_slot = 0xFF;   // item cast -- no gem cooldown side effect to worry about
    mgr->m_cast_duration_ms = 1000;
    mgr->m_cast_start_time = std::chrono::steady_clock::now();

    // Huge delta_time immediately after starting: must NOT complete the
    // cast if honored naively (duration 1000 + 5000ms buffer = 6000ms
    // real-time timeout, essentially 0ms has actually elapsed).
    mgr->update(999999.0f);
    EXPECT_TRUE(mgr->isCasting())
        << "update()'s delta_time parameter must be dead for this to hold";

    // Backdating real wall-clock time past duration+5000ms buffer DOES
    // complete it (assume-success path), confirming the gate is
    // steady_clock-based.
    mgr->m_cast_start_time = std::chrono::steady_clock::now() - std::chrono::milliseconds(7000);
    mgr->update(0.0f);
    EXPECT_FALSE(mgr->isCasting());

    auto events = bridge.drainEvents();
    bool sawComplete = false;
    for (auto& e : events) {
        if (e.type == eqt::state::GameEventType::SpellCastVisualComplete) sawComplete = true;
    }
    EXPECT_TRUE(sawComplete);
}

// ===================================================================
// 4. Interrupt + server packet handlers -- pure state updates, no DB
// needed (spell lookups are null-checked throughout: `spell ? spell->name
// : "Unknown"`, `if (spell) { ... }`).
// ===================================================================

TEST(SpellManagerInterrupt, InterruptCastWhileCastingResetsStateAndFiresCallback) {
    EverQuest eq = MakeEq();
    auto* mgr = eq.GetSpellManager();
    uint32_t gems[1] = {42};
    mgr->setSpellGems(gems, 1);
    mgr->m_is_casting = true;
    mgr->m_current_spell_id = 42;
    mgr->m_current_gem_slot = 0;
    mgr->m_gem_states[0] = GemState::Casting;

    CastResult seenResult = CastResult::Success;
    uint32_t seenSpellId = 0;
    mgr->setOnCastComplete([&](CastResult r, uint32_t id) {
        seenResult = r;
        seenSpellId = id;
    });

    mgr->interruptCast();

    EXPECT_FALSE(mgr->isCasting());
    EXPECT_EQ(mgr->getGemState(0), GemState::Ready);
    EXPECT_EQ(seenResult, CastResult::Interrupted);
    EXPECT_EQ(seenSpellId, 42u);
    EXPECT_EQ(mgr->getCurrentSpellId(), SPELL_UNKNOWN);
}

TEST(SpellManagerInterrupt, InterruptCastWhileNotCastingIsNoOp) {
    EverQuest eq = MakeEq();
    auto* mgr = eq.GetSpellManager();
    bool called = false;
    mgr->setOnCastComplete([&](CastResult, uint32_t) { called = true; });

    EXPECT_NO_THROW(mgr->interruptCast());
    EXPECT_FALSE(called);
}

TEST(SpellManagerHandlers, HandleBeginCastOwnCastConfirmsAndUsesServerCastTime) {
    EverQuest eq = MakeEq();
    auto* mgr = eq.GetSpellManager();
    zonetest::TestBridge bridge;
    mgr->setBridge(&bridge);
    mgr->m_is_casting = true;
    mgr->m_current_spell_id = 55;
    mgr->m_waiting_for_server_confirm = true;

    mgr->handleBeginCast(eq.GetEntityID(), 55, 2500);

    EXPECT_FALSE(mgr->m_waiting_for_server_confirm);
    EXPECT_EQ(mgr->m_cast_duration_ms, 2500u);

    auto events = bridge.drainEvents();
    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].type, eqt::state::GameEventType::SpellCastVisualStarted);
}

TEST(SpellManagerHandlers, HandleBeginCastItemCastStartsTrackingWhenNotAlreadyCasting) {
    EverQuest eq = MakeEq();
    auto* mgr = eq.GetSpellManager();
    ASSERT_FALSE(mgr->isCasting());

    mgr->handleBeginCast(eq.GetEntityID(), 77, 1200);

    EXPECT_TRUE(mgr->isCasting());
    EXPECT_EQ(mgr->getCurrentSpellId(), 77u);
    EXPECT_EQ(mgr->m_current_gem_slot, 255);
}

TEST(SpellManagerHandlers, HandleBeginCastOtherCasterTracksAsTargetCastWhenTargeted) {
    EverQuest eq = MakeEq();
    ASSERT_NO_THROW(ReplayZone(eq, HC_OP_NewSpawn,
        NewSpawnPayloadAt(200, "an_enemy_caster", 0, 10, 0)));
    auto* mgr = eq.GetSpellManager();
    auto* combat = eq.GetCombatManager();
    ASSERT_TRUE(combat->SetTarget(200));

    mgr->handleBeginCast(/*caster_id=*/200, /*spell_id=*/88, /*cast_time_ms=*/1500);

    EXPECT_EQ(mgr->m_target_caster_id, 200u);
    EXPECT_EQ(mgr->m_target_spell_id, 88u);
}

TEST(SpellManagerHandlers, HandleActionNonSpellTypeIsNoOp) {
    EverQuest eq = MakeEq();
    auto* mgr = eq.GetSpellManager();
    EQT::SpellActionInfo action{};
    action.type = 1;  // not 231
    EXPECT_NO_THROW(mgr->handleAction(action));
}

TEST(SpellManagerHandlers, HandleActionSuccessPacketCompletesOwnCastAndFiresCallback) {
    EverQuest eq = MakeEq();
    auto* mgr = eq.GetSpellManager();
    mgr->m_is_casting = true;
    mgr->m_current_spell_id = 99;
    mgr->m_current_gem_slot = 0xFF;  // no gem cooldown side effect (no DB spell object)

    CastResult seenResult = CastResult::Interrupted;
    mgr->setOnCastComplete([&](CastResult r, uint32_t) { seenResult = r; });

    EQT::SpellActionInfo action{};
    action.type = 231;
    action.effect_flag = 4;  // success packet
    action.source = eq.GetEntityID();
    action.target = eq.GetEntityID();
    action.spell = 99;

    mgr->handleAction(action);

    EXPECT_FALSE(mgr->isCasting());
    EXPECT_EQ(seenResult, CastResult::Success);
}

TEST(SpellManagerHandlers, HandleActionClearsTargetCastTracking) {
    EverQuest eq = MakeEq();
    auto* mgr = eq.GetSpellManager();
    mgr->m_target_caster_id = 200;
    mgr->m_target_spell_id = 88;

    EQT::SpellActionInfo action{};
    action.type = 231;
    action.effect_flag = 4;
    action.source = 200;
    action.spell = 88;

    mgr->handleAction(action);

    EXPECT_EQ(mgr->m_target_caster_id, 0u);
    EXPECT_EQ(mgr->m_target_spell_id, SPELL_UNKNOWN);
}

TEST(SpellManagerHandlers, HandleInterruptOwnCastMessageType2IsFizzle) {
    EverQuest eq = MakeEq();
    auto* mgr = eq.GetSpellManager();
    mgr->m_is_casting = true;
    mgr->m_current_spell_id = 33;
    mgr->m_current_gem_slot = 0xFF;

    CastResult seen = CastResult::Success;
    mgr->setOnCastComplete([&](CastResult r, uint32_t) { seen = r; });

    mgr->handleInterrupt(eq.GetEntityID(), 33, /*message_type=*/2);

    EXPECT_FALSE(mgr->isCasting());
    EXPECT_EQ(seen, CastResult::Fizzle);
}

TEST(SpellManagerHandlers, HandleInterruptOwnCastOtherMessageTypeIsInterrupted) {
    EverQuest eq = MakeEq();
    auto* mgr = eq.GetSpellManager();
    mgr->m_is_casting = true;
    mgr->m_current_spell_id = 33;
    mgr->m_current_gem_slot = 0xFF;

    CastResult seen = CastResult::Success;
    mgr->setOnCastComplete([&](CastResult r, uint32_t) { seen = r; });

    mgr->handleInterrupt(eq.GetEntityID(), 33, /*message_type=*/1);

    EXPECT_EQ(seen, CastResult::Interrupted);
}

TEST(SpellManagerHandlers, HandleInterruptOtherCasterOnlyClearsTargetTracking) {
    EverQuest eq = MakeEq();
    auto* mgr = eq.GetSpellManager();
    mgr->m_target_caster_id = 200;
    mgr->m_target_spell_id = 88;
    // Not our own cast -- must not touch m_is_casting/callback.
    bool called = false;
    mgr->setOnCastComplete([&](CastResult, uint32_t) { called = true; });

    mgr->handleInterrupt(/*caster_id=*/200, /*spell_id=*/88, /*message_type=*/1);

    EXPECT_EQ(mgr->m_target_caster_id, 0u);
    EXPECT_FALSE(called);
}

TEST(SpellManagerHandlers, HandleManaChangeDoesNotCrash) {
    // Log-only per the implementation comment (spell_manager.cpp:1108-1114)
    // -- mana itself is updated elsewhere in EverQuest. Smoke test only.
    EverQuest eq = MakeEq();
    auto* mgr = eq.GetSpellManager();
    EXPECT_NO_THROW(mgr->handleManaChange(100, 50, 42));
}

// ===================================================================
// 5. Callbacks -- setOnCastComplete/setOnSpellFade fire with the right
// arguments. (setOnSpellFade has no production caller in spell_manager.cpp
// itself -- see Review; round-tripped via direct std::function
// assignment/invocation instead.)
//
// T357 (B061): setOnSpellLand is GONE, and so is the test that only
// exercised it. It had zero setters in production, and its invocation in
// handleAction() sat OUTSIDE the `spell && is_success_packet` gate, so it
// fired on the animation packet too -- it was never a landing hook. The real
// landing producer is OP_Action effect_flag 4 (T348). The absence is pinned
// below by SpellLandHookIsGone.
// ===================================================================

TEST(SpellManagerCallbacks, OnCastCompleteReceivesResultAndSpellId) {
    EverQuest eq = MakeEq();
    auto* mgr = eq.GetSpellManager();
    mgr->m_is_casting = true;
    mgr->m_current_spell_id = 7;
    mgr->m_current_gem_slot = 0xFF;

    bool called = false;
    mgr->setOnCastComplete([&](CastResult r, uint32_t id) {
        called = true;
        EXPECT_EQ(r, CastResult::Success);
        EXPECT_EQ(id, 7u);
    });
    mgr->completeCast(true);
    EXPECT_TRUE(called);
}

TEST(SpellManagerCallbacks, OnSpellFadeInvokesWithGivenArguments) {
    // No production call site invokes m_on_spell_fade in spell_manager.cpp
    // (grep-verified: only assigned via setOnSpellFade, never called --
    // likely reserved for a future buff-fade wiring elsewhere). Pinning
    // that the callback slot itself works mechanically.
    EverQuest eq = MakeEq();
    auto* mgr = eq.GetSpellManager();
    bool called = false;
    mgr->setOnSpellFade([&](uint32_t spell_id, uint16_t entity_id) {
        called = true;
        EXPECT_EQ(spell_id, 9u);
        EXPECT_EQ(entity_id, 10u);
    });
    mgr->m_on_spell_fade(9, 10);
    EXPECT_TRUE(called);
}

// ===================================================================
// getCastProgress / getCastTimeRemaining
// ===================================================================

TEST(SpellManagerCastProgress, NotCastingReturnsZero) {
    EverQuest eq = MakeEq();
    auto* mgr = eq.GetSpellManager();
    EXPECT_FLOAT_EQ(mgr->getCastProgress(), 0.0f);
    EXPECT_EQ(mgr->getCastTimeRemaining(), 0u);
}

TEST(SpellManagerCastProgress, ZeroDurationReturnsZeroProgress) {
    EverQuest eq = MakeEq();
    auto* mgr = eq.GetSpellManager();
    mgr->m_is_casting = true;
    mgr->m_cast_duration_ms = 0;
    EXPECT_FLOAT_EQ(mgr->getCastProgress(), 0.0f);
}

TEST(SpellManagerCastProgress, BackdatedStartGivesPartialProgressAndRemaining) {
    EverQuest eq = MakeEq();
    auto* mgr = eq.GetSpellManager();
    mgr->m_is_casting = true;
    mgr->m_cast_duration_ms = 4000;
    mgr->m_cast_start_time = std::chrono::steady_clock::now() - std::chrono::milliseconds(2000);

    float progress = mgr->getCastProgress();
    EXPECT_GT(progress, 0.3f);
    EXPECT_LT(progress, 0.7f);
    uint32_t remaining = mgr->getCastTimeRemaining();
    EXPECT_GT(remaining, 0u);
    EXPECT_LT(remaining, 4000u);
}

TEST(SpellManagerCastProgress, ElapsedPastDurationClampsProgressTo1AndRemainingTo0) {
    EverQuest eq = MakeEq();
    auto* mgr = eq.GetSpellManager();
    mgr->m_is_casting = true;
    mgr->m_cast_duration_ms = 1000;
    mgr->m_cast_start_time = std::chrono::steady_clock::now() - std::chrono::milliseconds(5000);

    EXPECT_FLOAT_EQ(mgr->getCastProgress(), 1.0f);
    EXPECT_EQ(mgr->getCastTimeRemaining(), 0u);
}

// ===================================================================
// 6. T44 -- SpellCastComplete publishes. SpellCastComplete was defined but
// never published before this unit; completeCast()/interruptCast()/
// handleInterrupt() are the three places m_is_casting flips back to false
// for our own cast, and each now pushes it. (SpellCastStarted's publish
// sites -- beginCastFromGem()/beginCastById() -- need a real DB-backed
// spell to reach CastResult::Success, so they're covered in
// test_spell_manager.cpp's beginCast end-to-end tests instead.)
// ===================================================================

TEST(SpellManagerCastComplete, CompleteCastSuccessPublishesSpellCastComplete) {
    EverQuest eq = MakeEq();
    auto* mgr = eq.GetSpellManager();
    zonetest::TestBridge bridge;
    mgr->setBridge(&bridge);
    mgr->m_is_casting = true;
    mgr->m_current_spell_id = 42;
    mgr->m_current_gem_slot = 0xFF;  // no DB spell object, skip gem cooldown

    mgr->completeCast(true);

    auto events = bridge.drainEvents();
    bool saw = false;
    for (auto& e : events) {
        if (e.type == eqt::state::GameEventType::SpellCastComplete) {
            const auto& d = std::get<eqt::state::SpellCastCompleteData>(e.data);
            EXPECT_EQ(d.spellId, 42u);
            EXPECT_EQ(d.result, static_cast<uint8_t>(CastResult::Success));
            saw = true;
        }
    }
    EXPECT_TRUE(saw);
}

TEST(SpellManagerCastComplete, InterruptCastPublishesSpellCastCompleteInterrupted) {
    EverQuest eq = MakeEq();
    auto* mgr = eq.GetSpellManager();
    zonetest::TestBridge bridge;
    mgr->setBridge(&bridge);
    mgr->m_is_casting = true;
    mgr->m_current_spell_id = 7;
    mgr->m_current_gem_slot = 0xFF;

    mgr->interruptCast();

    auto events = bridge.drainEvents();
    bool saw = false;
    for (auto& e : events) {
        if (e.type == eqt::state::GameEventType::SpellCastComplete) {
            const auto& d = std::get<eqt::state::SpellCastCompleteData>(e.data);
            EXPECT_EQ(d.spellId, 7u);
            EXPECT_EQ(d.result, static_cast<uint8_t>(CastResult::Interrupted));
            saw = true;
        }
    }
    EXPECT_TRUE(saw);
}

TEST(SpellManagerCastComplete, HandleInterruptFizzlePublishesSpellCastCompleteWithFizzleResult) {
    EverQuest eq = MakeEq();
    auto* mgr = eq.GetSpellManager();
    zonetest::TestBridge bridge;
    mgr->setBridge(&bridge);
    mgr->m_is_casting = true;
    mgr->m_current_spell_id = 33;
    mgr->m_current_gem_slot = 0xFF;

    mgr->handleInterrupt(eq.GetEntityID(), 33, /*message_type=*/2);

    auto events = bridge.drainEvents();
    bool saw = false;
    for (auto& e : events) {
        if (e.type == eqt::state::GameEventType::SpellCastComplete) {
            const auto& d = std::get<eqt::state::SpellCastCompleteData>(e.data);
            EXPECT_EQ(d.spellId, 33u);
            EXPECT_EQ(d.result, static_cast<uint8_t>(CastResult::Fizzle));
            saw = true;
        }
    }
    EXPECT_TRUE(saw);
}

// Found live (T44 Review): the server can silently drop a cast request
// (e.g. an invalid target) rather than ever echoing BeginCast back, leaving
// m_waiting_for_server_confirm permanently true. update()'s own 5-second
// "waiting for server confirm" timeout is the only thing that ever clears
// m_is_casting in that case -- and it originally didn't publish
// SpellCastComplete, which would have left GameStateSyncBridge (and
// SpellState) showing "casting" forever with no way to learn otherwise.
TEST(SpellManagerCastComplete, ServerConfirmTimeoutPublishesSpellCastComplete) {
    EverQuest eq = MakeEq();
    auto* mgr = eq.GetSpellManager();
    zonetest::TestBridge bridge;
    mgr->setBridge(&bridge);
    mgr->m_is_casting = true;
    mgr->m_waiting_for_server_confirm = true;
    mgr->m_current_spell_id = 21;
    mgr->m_current_gem_slot = 0xFF;
    mgr->m_cast_start_time = std::chrono::steady_clock::now() - std::chrono::milliseconds(6000);

    mgr->update(0.0f);

    EXPECT_FALSE(mgr->isCasting());
    auto events = bridge.drainEvents();
    bool saw = false;
    for (auto& e : events) {
        if (e.type == eqt::state::GameEventType::SpellCastComplete) {
            const auto& d = std::get<eqt::state::SpellCastCompleteData>(e.data);
            EXPECT_EQ(d.spellId, 21u);
            EXPECT_EQ(d.result, static_cast<uint8_t>(CastResult::Interrupted));
            saw = true;
        }
    }
    EXPECT_TRUE(saw);
}

TEST(SpellManagerCastComplete, HandleInterruptOtherCasterDoesNotPublish) {
    EverQuest eq = MakeEq();
    auto* mgr = eq.GetSpellManager();
    zonetest::TestBridge bridge;
    mgr->setBridge(&bridge);
    mgr->m_target_caster_id = 200;
    mgr->m_target_spell_id = 88;
    // Not our own cast -- publishCastComplete() must not fire.

    mgr->handleInterrupt(/*caster_id=*/200, /*spell_id=*/88, /*message_type=*/1);

    auto events = bridge.drainEvents();
    for (auto& e : events) {
        EXPECT_NE(e.type, eqt::state::GameEventType::SpellCastComplete);
    }
}

// T357 (B061) -- the hook is gone and must stay gone.
//
// Red before the fix: every one of these strings was present in the header
// and the TU. This is a source-reader pin because a deleted member has no
// runtime observable: the whole point is that nothing fires. The invocation
// it replaces sat at function scope in handleAction(), below and OUTSIDE the
// `spell && is_success_packet` gate, which is what made it fire on the
// animation packet.
TEST(SpellLandHookIsGone, NoTypedefSetterMemberOrInvocationRemains) {
    auto slurp = [](const std::string& path) {
        std::ifstream in(path);
        EXPECT_TRUE(in.is_open()) << "cannot open: " << path;
        std::stringstream ss;
        ss << in.rdbuf();
        return ss.str();
    };
    const std::string root = WILLEQ_REPO_ROOT;
    const std::string header = slurp(root + "/include/client/spell/spell_manager.h");
    // T407 split spell_manager.cpp five ways and handleAction() -- the body the
    // hook sat in -- moved to spell_packet_handlers.cpp. Reading only the old
    // file would have left this clause green while pinning nothing, so the pin
    // follows the body: every SpellManager TU is concatenated and searched.
    std::string source;
    for (const char* tu : {"spell_manager.cpp", "spell_packet_handlers.cpp",
                           "spell_gems.cpp", "spell_cast_validation.cpp",
                           "spell_publish.cpp", "spell_cast_checks.cpp"}) {
        source += slurp(root + "/src/client/spell/" + tu);
    }

    EXPECT_EQ(header.find("SpellLandCallback"), std::string::npos)
        << "the typedef came back";
    EXPECT_EQ(header.find("setOnSpellLand"), std::string::npos)
        << "the setter came back -- it had zero production callers";
    EXPECT_EQ(header.find("m_on_spell_land"), std::string::npos)
        << "the member came back";
    EXPECT_EQ(source.find("m_on_spell_land("), std::string::npos)
        << "the invocation came back -- and it fires outside the is_success_packet "
           "gate, i.e. on the animation packet, which is not a landing";

    // The two neighbours it was deleted from between are untouched.
    EXPECT_NE(header.find("SpellCastCallback"), std::string::npos);
    EXPECT_NE(header.find("SpellFadeCallback"), std::string::npos);
    EXPECT_NE(header.find("setOnCastComplete"), std::string::npos);
    EXPECT_NE(header.find("setOnSpellFade"), std::string::npos);
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
