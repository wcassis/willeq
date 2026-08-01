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
    EQT::Action_Struct action{};
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
    bool landed = false;
    mgr->setOnSpellLand([&](uint32_t, uint16_t, uint16_t) { landed = true; });

    EQT::Action_Struct action{};
    action.type = 231;
    action.effect_flag = 4;  // success packet
    action.source = eq.GetEntityID();
    action.target = eq.GetEntityID();
    action.spell = 99;

    mgr->handleAction(action);

    EXPECT_FALSE(mgr->isCasting());
    EXPECT_EQ(seenResult, CastResult::Success);
    EXPECT_TRUE(landed);
}

TEST(SpellManagerHandlers, HandleActionClearsTargetCastTracking) {
    EverQuest eq = MakeEq();
    auto* mgr = eq.GetSpellManager();
    mgr->m_target_caster_id = 200;
    mgr->m_target_spell_id = 88;

    EQT::Action_Struct action{};
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
// 5. Callbacks -- setOnCastComplete/setOnSpellLand/setOnSpellFade fire
// with the right arguments. (setOnSpellFade has no production caller in
// spell_manager.cpp itself -- see Review; round-tripped via direct
// std::function assignment/invocation instead.)
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

TEST(SpellManagerCallbacks, OnSpellLandReceivesSpellCasterTarget) {
    EverQuest eq = MakeEq();
    auto* mgr = eq.GetSpellManager();

    bool called = false;
    mgr->setOnSpellLand([&](uint32_t spell_id, uint16_t caster_id, uint16_t target_id) {
        called = true;
        EXPECT_EQ(spell_id, 500u);
        EXPECT_EQ(caster_id, 201u);
        EXPECT_EQ(target_id, 202u);
    });

    EQT::Action_Struct action{};
    action.type = 231;
    action.effect_flag = 4;
    action.source = 201;
    action.target = 202;
    action.spell = 500;
    mgr->handleAction(action);

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

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
