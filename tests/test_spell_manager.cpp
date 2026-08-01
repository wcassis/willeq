// T24 — SpellManager coverage, part 1 of 2 (spell/combat batch, unit 2 of 3).
// Covers priorities 1-2 of refactor/T24_spell_manager.md: gem state
// machine, cast validation matrix, target adjustment, beginCastFromGem/
// beginCastById state machine. Timing (priority 3), interrupt/server
// packet handlers (priority 4), and callbacks (priority 5) continue in
// test_spell_manager_timing.cpp -- split purely to keep each file under
// the plan's ~800-line guidance; both link against the same live
// SpellManager via the same seam, there is no behavioral split.
//
// Seam: settled as Option A (refactor/T23_spell_combat_seam_scoping.md,
// refactor/T24_spell_manager.md, Will's ruling) -- no interface extraction.
// A bare EverQuest("127.0.0.1", 0, "test", "test", "test", "Testchar")
// constructs a LIVE SpellManager reachable via the existing public
// eq.GetSpellManager(). No network, no graphics, no live server.
//
// This file uses the T17/T23 `#define private public` technique around the
// *first* inclusion of spell_manager.h (spell_manager.h is #pragma once and
// NOT otherwise included by eq.h -- eq.h only forward-declares EQ::SpellManager,
// so there is no transitive-include ordering hazard here, unlike T23's
// combat.h case) to call private validation methods
// (validateCast/checkMana/checkRange/adjustTargetForSpellType/
// isValidCreatureTarget/startGemCooldown) directly with hand-built SpellData
// structs. This sidesteps needing the real SQLite spell DB for most of the
// "cast validation matrix" priority, since those methods take a `const
// SpellData&` by reference rather than a spell id. The dead-delta_time
// timing finding (spell_manager.cpp:57/115/133/117 never reads its
// parameter) is exercised and documented in test_spell_manager_timing.cpp.
//
// A handful of tests DO need the real DB (spell existence gates inside
// beginCastFromGem/beginCastById/memorizeSpell/scribeSpell) and follow
// test_spell_database.cpp's hasDb() + GTEST_SKIP precedent so this binary
// degrades cleanly where data/willeq.db is missing.

#include <gtest/gtest.h>

#define private public
#define protected public
#include "client/spell/spell_manager.h"
#undef private
#undef protected

#include "client/combat.h"
#include "client/willeq_database.h"
#include "zone_stream_test_utils.h"

#include <chrono>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <string>

using namespace zonetest;
using namespace EQ;

#ifndef WILLEQ_DB_PATH
#define WILLEQ_DB_PATH "data/willeq.db"
#endif

namespace {

// ===================================================================
// DB availability (mirrors test_spell_database.cpp's hasDb() precedent)
// ===================================================================

EQT::WilleqDatabase g_willeq_db;
bool g_db_available = false;

bool hasDb() { return g_db_available; }

std::string getWilleqDbPath() {
    if (const char* env_path = std::getenv("WILLEQ_DB_PATH")) {
        return env_path;
    }
    return WILLEQ_DB_PATH;
}

// Scans [lo, hi] for the first spell id matching `pred`, using the
// SpellManager's own live database (getDatabase() is public). Callers must
// re-fetch getSpell(id) when they actually need the pointer -- the LRU
// cache can evict earlier entries during a long scan, so pointers are not
// held across scan iterations.
uint32_t FindSpellId(SpellDatabase& db, uint32_t lo, uint32_t hi,
                      const std::function<bool(const SpellData&)>& pred) {
    for (uint32_t id = lo; id <= hi; ++id) {
        const SpellData* s = db.getSpell(id);
        if (s && pred(*s)) {
            return id;
        }
    }
    return 0;
}

// Extends zonetest::NewSpawnPayloadAt with an explicit bodytype (offset
// base+335, base=2 for a single NewSpawn packet -- same struct WriteSpawnStruct
// writes, see eq_entities.cpp:339 for the read side). Kept local to this file
// rather than promoted into zone_stream_test_utils.h since only the creature-
// type-restriction tests need it.
std::vector<uint8_t> NewSpawnPayloadWithBodyType(uint32_t spawn_id, const std::string& name,
                                                  int32_t server_x, int32_t server_y, int32_t z,
                                                  uint8_t bodytype, uint8_t level = 5,
                                                  uint8_t npc_type = 1, uint8_t char_class = 1) {
    return BuildPayload(387, [&](EQ::Net::DynamicPacket& p) {
        WriteSpawnStruct(p, 2, spawn_id, name, level, 54, char_class, 0, npc_type, 100,
                          EncodePos(server_x, server_y, z, 0));
        p.PutUInt8(2 + 335, bodytype);
    });
}

// A minimal hand-built SpellData for direct private-method tests that don't
// need a real DB row -- validateCast/checkMana/checkRange/
// adjustTargetForSpellType/isValidCreatureTarget all take `const SpellData&`
// by reference, not a spell id, so a local struct is sufficient and avoids
// coupling these tests to specific DB content.
SpellData MakeSpell(SpellTargetType tt, int32_t mana_cost = 0, uint32_t range = 0) {
    SpellData s;
    s.id = 1;
    s.name = "Test Spell";
    s.target_type = tt;
    s.mana_cost = mana_cost;
    s.range = range;
    s.cast_time_ms = 1000;
    s.recast_time_ms = 0;
    return s;
}

}  // namespace

// ===================================================================
// 1. Gem state machine
// ===================================================================

// ---- Invalid-slot boundaries (no DB needed -- these return before any
// spell lookup). EQ::MAX_SPELL_GEMS == 8, so slot 8+ is out of range; pinning
// which side of the 0-vs-1-based boundary these use (T16 precedent: 0-based
// throughout, slot 0 is the first gem, slot 8 is already invalid).

TEST(SpellManagerGems, MemorizeSpellInvalidSlotReturnsFalse) {
    EverQuest eq = MakeEq();
    auto* mgr = eq.GetSpellManager();
    EXPECT_FALSE(mgr->memorizeSpell(1, EQ::MAX_SPELL_GEMS));
    EXPECT_FALSE(mgr->memorizeSpell(1, 200));
}

TEST(SpellManagerGems, ForgetSpellInvalidSlotReturnsFalse) {
    EverQuest eq = MakeEq();
    auto* mgr = eq.GetSpellManager();
    EXPECT_FALSE(mgr->forgetSpell(EQ::MAX_SPELL_GEMS));
}

TEST(SpellManagerGems, SwapGemsInvalidSlotReturnsFalse) {
    EverQuest eq = MakeEq();
    auto* mgr = eq.GetSpellManager();
    EXPECT_FALSE(mgr->swapGems(EQ::MAX_SPELL_GEMS, 0));
    EXPECT_FALSE(mgr->swapGems(0, EQ::MAX_SPELL_GEMS));
}

TEST(SpellManagerGems, SwapGemsSameSlotReturnsFalse) {
    EverQuest eq = MakeEq();
    auto* mgr = eq.GetSpellManager();
    EXPECT_FALSE(mgr->swapGems(3, 3));
}

TEST(SpellManagerGems, GetMemorizedSpellInvalidSlotReturnsUnknown) {
    EverQuest eq = MakeEq();
    auto* mgr = eq.GetSpellManager();
    EXPECT_EQ(mgr->getMemorizedSpell(EQ::MAX_SPELL_GEMS), SPELL_UNKNOWN);
}

TEST(SpellManagerGems, GetGemStateInvalidSlotReturnsEmpty) {
    EverQuest eq = MakeEq();
    auto* mgr = eq.GetSpellManager();
    EXPECT_EQ(mgr->getGemState(EQ::MAX_SPELL_GEMS), GemState::Empty);
}

// ---- setSpellGems / setSpellbook (public, direct injection -- no packet
// replay, no DB needed).

TEST(SpellManagerGems, SetSpellGemsPopulatesReadyForNonEmptyEntries) {
    EverQuest eq = MakeEq();
    auto* mgr = eq.GetSpellManager();
    uint32_t gems[EQ::MAX_SPELL_GEMS] = {100, 0, SPELL_UNKNOWN, 200, 0, 0, 0, 0};
    mgr->setSpellGems(gems, EQ::MAX_SPELL_GEMS);

    EXPECT_EQ(mgr->getMemorizedSpell(0), 100u);
    EXPECT_EQ(mgr->getGemState(0), GemState::Ready);
    // 0 and SPELL_UNKNOWN both map to Empty (spell_manager.cpp:883).
    EXPECT_EQ(mgr->getGemState(1), GemState::Empty);
    EXPECT_EQ(mgr->getGemState(2), GemState::Empty);
    EXPECT_EQ(mgr->getMemorizedSpell(3), 200u);
    EXPECT_EQ(mgr->getGemState(3), GemState::Ready);
}

TEST(SpellManagerGems, SetSpellbookAndScribeQueriesRoundTrip) {
    EverQuest eq = MakeEq();
    auto* mgr = eq.GetSpellManager();
    uint32_t book[3] = {10, 20, 30};
    mgr->setSpellbook(book, 3);

    EXPECT_EQ(mgr->getSpellbookSpell(0), 10u);
    EXPECT_EQ(mgr->getSpellbookSpell(1), 20u);
    EXPECT_EQ(mgr->getSpellbookSpell(2), 30u);
    EXPECT_EQ(mgr->getSpellbookSpell(3), SPELL_UNKNOWN);
    EXPECT_EQ(mgr->getSpellbookSpell(MAX_SPELLBOOK_SLOTS), SPELL_UNKNOWN);

    EXPECT_TRUE(mgr->hasSpellScribed(10));
    EXPECT_FALSE(mgr->hasSpellScribed(999));

    auto scribed = mgr->getScribedSpells();
    EXPECT_EQ(scribed.size(), 3u);
}

TEST(SpellManagerGems, ScribeSpellFailsWithoutRealDbEntry) {
    // scribeSpell() checks m_spell_db.hasSpell(spell_id); without
    // initialize() having loaded the real SQLite DB, hasSpell() is always
    // false, so this is real, DB-independent, no-op behavior worth pinning.
    EverQuest eq = MakeEq();
    auto* mgr = eq.GetSpellManager();
    EXPECT_FALSE(mgr->scribeSpell(1, 0));
}

TEST(SpellManagerGems, ScribeSpellInvalidBookSlotReturnsFalse) {
    EverQuest eq = MakeEq();
    auto* mgr = eq.GetSpellManager();
    EXPECT_FALSE(mgr->scribeSpell(1, MAX_SPELLBOOK_SLOTS));
}

// ---- forgetSpell (state-machine behavior, no DB needed: it only clears
// arrays and attempts a packet send, which is a suppressed no-op).

TEST(SpellManagerGems, ForgetSpellOnEmptySlotReturnsFalse) {
    EverQuest eq = MakeEq();
    auto* mgr = eq.GetSpellManager();
    EXPECT_FALSE(mgr->forgetSpell(0));
}

TEST(SpellManagerGems, ForgetSpellClearsSlotAndPublishesGemChanged) {
    EverQuest eq = MakeEq();
    auto* mgr = eq.GetSpellManager();
    zonetest::TestBridge bridge;
    mgr->setBridge(&bridge);

    uint32_t gems[1] = {42};
    mgr->setSpellGems(gems, 1);
    bridge.drainEvents();  // discard nothing (setSpellGems doesn't publish)

    EXPECT_TRUE(mgr->forgetSpell(0));
    EXPECT_EQ(mgr->getMemorizedSpell(0), SPELL_UNKNOWN);
    EXPECT_EQ(mgr->getGemState(0), GemState::Empty);

    auto events = bridge.drainEvents();
    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].type, eqt::state::GameEventType::SpellGemChanged);
}

TEST(SpellManagerGems, ForgetSpellWhileCastingSameSlotReturnsFalse) {
    EverQuest eq = MakeEq();
    auto* mgr = eq.GetSpellManager();
    uint32_t gems[1] = {42};
    mgr->setSpellGems(gems, 1);
    mgr->m_is_casting = true;
    mgr->m_current_gem_slot = 0;

    EXPECT_FALSE(mgr->forgetSpell(0));
    EXPECT_EQ(mgr->getMemorizedSpell(0), 42u) << "must not be cleared while casting from it";
}

TEST(SpellManagerGems, ForgetSpellWhileCastingDifferentSlotSucceeds) {
    EverQuest eq = MakeEq();
    auto* mgr = eq.GetSpellManager();
    uint32_t gems[2] = {42, 99};
    mgr->setSpellGems(gems, 2);
    mgr->m_is_casting = true;
    mgr->m_current_gem_slot = 1;  // casting from slot 1, not slot 0

    EXPECT_TRUE(mgr->forgetSpell(0));
}

// ---- swapGems

TEST(SpellManagerGems, SwapGemsSwapsSpellsAndStates) {
    EverQuest eq = MakeEq();
    auto* mgr = eq.GetSpellManager();
    uint32_t gems[2] = {11, 22};
    mgr->setSpellGems(gems, 2);

    EXPECT_TRUE(mgr->swapGems(0, 1));
    EXPECT_EQ(mgr->getMemorizedSpell(0), 22u);
    EXPECT_EQ(mgr->getMemorizedSpell(1), 11u);
}

TEST(SpellManagerGems, SwapGemsBlockedWhileCastingFromEitherSlot) {
    EverQuest eq = MakeEq();
    auto* mgr = eq.GetSpellManager();
    uint32_t gems[2] = {11, 22};
    mgr->setSpellGems(gems, 2);
    mgr->m_is_casting = true;
    mgr->m_current_gem_slot = 0;

    EXPECT_FALSE(mgr->swapGems(0, 1));
    EXPECT_EQ(mgr->getMemorizedSpell(0), 11u) << "must not swap while casting from slot 0";
}

TEST(SpellManagerGems, SwapGemsBlockedWhenEitherSlotOnCooldown) {
    EverQuest eq = MakeEq();
    auto* mgr = eq.GetSpellManager();
    uint32_t gems[2] = {11, 22};
    mgr->setSpellGems(gems, 2);
    mgr->m_gem_states[1] = GemState::Refresh;

    EXPECT_FALSE(mgr->swapGems(0, 1));
}

TEST(SpellManagerGems, SwapGemsBlockedWhenEitherSlotMemorizing) {
    EverQuest eq = MakeEq();
    auto* mgr = eq.GetSpellManager();
    uint32_t gems[2] = {11, 22};
    mgr->setSpellGems(gems, 2);
    mgr->m_gem_states[0] = GemState::MemorizeProgress;

    EXPECT_FALSE(mgr->swapGems(0, 1));
}

// ---- memorizeSpell real workflow (DB needed: hasSpellScribed +
// canClassUse both gate on real spell data).

TEST(SpellManagerGems, MemorizeSpellSuccessStartsMemorizationAndPublishesEvent) {
    if (!hasDb()) GTEST_SKIP() << "willeq.db not found, skipping";
    EverQuest eq = MakeEq();
    ASSERT_TRUE(eq.InitializeSpellSystem(""));
    auto* mgr = eq.GetSpellManager();

    // Find any spell usable by Warrior at level 60 (class_levels[0] != 255
    // and <= 60) -- canClassUse's class_idx-1 indexing (spell_data.h:200).
    uint32_t id = FindSpellId(mgr->getDatabase(), 1, 4000, [](const SpellData& s) {
        return s.canClassUse(PlayerClass::Warrior, 60);
    });
    if (id == 0) GTEST_SKIP() << "no Warrior-usable spell found in scanned range";

    zonetest::TestBridge bridge;
    mgr->setBridge(&bridge);
    ASSERT_TRUE(mgr->scribeSpell(id, 0));
    eq.GetGameState().player().setClass(static_cast<uint32_t>(PlayerClass::Warrior));
    eq.GetGameState().player().setLevel(60);

    EXPECT_TRUE(mgr->memorizeSpell(id, 2));
    EXPECT_TRUE(mgr->isMemorizing());
    EXPECT_EQ(mgr->getGemState(2), GemState::MemorizeProgress);

    auto events = bridge.drainEvents();
    bool sawStart = false;
    for (auto& e : events) {
        if (e.type == eqt::state::GameEventType::SpellMemorizeVisualStarted) sawStart = true;
    }
    EXPECT_TRUE(sawStart);
}

TEST(SpellManagerGems, MemorizeSpellNotScribedFails) {
    if (!hasDb()) GTEST_SKIP() << "willeq.db not found, skipping";
    EverQuest eq = MakeEq();
    ASSERT_TRUE(eq.InitializeSpellSystem(""));
    auto* mgr = eq.GetSpellManager();

    uint32_t id = FindSpellId(mgr->getDatabase(), 1, 4000, [](const SpellData&) { return true; });
    if (id == 0) GTEST_SKIP() << "no spell found in scanned range";

    // Deliberately NOT scribed.
    EXPECT_FALSE(mgr->memorizeSpell(id, 0));
    EXPECT_FALSE(mgr->isMemorizing());
}

// Real, previously-undocumented bug found while writing this unit (NOT
// listed in T23_spell_combat_seam_scoping.md or T24_spell_manager.md --
// this is new, verified by reading spell_database.cpp, not guessed).
//
// SpellDatabase::queryAndCache() (spell_database.cpp:54-92) maps every
// EQT::SpellInfo field into SpellData EXCEPT class_levels -- because
// EQT::SpellInfo (willeq_database.h:72-98) has no class-level-requirement
// field at all to map from (same missing-source-data root cause as T22's
// already-documented `effects`-never-populated gap in
// test_spell_database.cpp). SpellData::class_levels therefore keeps its
// default-constructed value for every DB-backed spell: `std::array<uint8_t,
// NUM_CLASSES> class_levels = {};` -- all zeros, NOT the "unusable" sentinel
// 255.
//
// SpellData::canClassUse() (spell_data.h:200-207) reads
// `class_levels[class_idx - 1]` and returns `required != 255 && level >=
// required`. With every entry defaulted to 0, this is `0 != 255 && level >=
// 0`, which is true for every level >= 0 and every valid PlayerClass
// (1..16) -- i.e. canClassUse() ALWAYS returns true for any DB-backed
// spell and any class, regardless of the spell's real level/class
// restriction. That directly breaks the level check inside
// SpellManager::memorizeSpell() (spell_manager.cpp:628-633): a level-1
// Warrior can memorize a spell the live game reserves for level-60
// Wizards, provided only that it's scribed. This is a real, currently-live
// gap in cast validation, not a hypothetical -- pinned below, NOT fixed,
// per this unit's STOP-condition instructions (src/ change required to fix
// it -- either populate class_levels from a real DB column if one exists,
// or treat 0 as "no requirement recorded" differently from an explicit
// level-1 requirement).
TEST(SpellManagerGems, BUG_CanClassUseAlwaysTrueBecauseClassLevelsNeverPopulatedFromDb) {
    if (!hasDb()) GTEST_SKIP() << "willeq.db not found, skipping";
    EverQuest eq = MakeEq();
    ASSERT_TRUE(eq.InitializeSpellSystem(""));
    auto* mgr = eq.GetSpellManager();

    uint32_t id = FindSpellId(mgr->getDatabase(), 1, 4000, [](const SpellData&) { return true; });
    if (id == 0) GTEST_SKIP() << "no spell found in scanned range";

    const SpellData* spell = mgr->getDatabase().getSpell(id);
    ASSERT_NE(spell, nullptr);
    // Every class_levels entry is the default-constructed 0, never the
    // "unusable" sentinel 255 -- confirming the root cause directly.
    for (uint8_t v : spell->class_levels) {
        EXPECT_EQ(v, 0u);
    }
    // Consequence: canClassUse is true for every class at level 1, i.e. no
    // real level gate exists for any DB-backed spell.
    EXPECT_TRUE(spell->canClassUse(PlayerClass::Warrior, 1));
    EXPECT_TRUE(spell->canClassUse(PlayerClass::Wizard, 1));
}

TEST(SpellManagerGems, BUG_MemorizeSpellSucceedsAtLevel1RegardlessOfSpellsRealClassRestriction) {
    if (!hasDb()) GTEST_SKIP() << "willeq.db not found, skipping";
    EverQuest eq = MakeEq();
    ASSERT_TRUE(eq.InitializeSpellSystem(""));
    auto* mgr = eq.GetSpellManager();

    uint32_t id = FindSpellId(mgr->getDatabase(), 1, 4000, [](const SpellData&) { return true; });
    if (id == 0) GTEST_SKIP() << "no spell found in scanned range";

    ASSERT_TRUE(mgr->scribeSpell(id, 0));
    // A level-1 Warrior -- the most restrictive plausible caster -- must be
    // BLOCKED by a real level/class gate for an arbitrary spell. It isn't.
    eq.GetGameState().player().setClass(static_cast<uint32_t>(PlayerClass::Warrior));
    eq.GetGameState().player().setLevel(1);

    EXPECT_TRUE(mgr->memorizeSpell(id, 0))
        << "documents the live gap: memorizeSpell's level/class gate "
           "(spell_manager.cpp:630) never actually rejects anything against "
           "the DB-backed spell database -- see the BUG_ test above for the "
           "root cause (class_levels is never populated from SQLite).";
}

TEST(SpellManagerGems, MemorizeSpellUnknownSpellFails) {
    // No DB needed -- getSpell() returns nullptr for any id when the
    // manager was never initialize()'d, and also for a genuinely unknown id
    // when it was.
    EverQuest eq = MakeEq();
    auto* mgr = eq.GetSpellManager();
    EXPECT_FALSE(mgr->memorizeSpell(999999999, 0));
}

TEST(SpellManagerGems, MemorizeSpellBlockedWhileAlreadyCastingOrMemorizing) {
    if (!hasDb()) GTEST_SKIP() << "willeq.db not found, skipping";
    EverQuest eq = MakeEq();
    ASSERT_TRUE(eq.InitializeSpellSystem(""));
    auto* mgr = eq.GetSpellManager();

    uint32_t id = FindSpellId(mgr->getDatabase(), 1, 4000, [](const SpellData& s) {
        return s.canClassUse(PlayerClass::Warrior, 60);
    });
    if (id == 0) GTEST_SKIP() << "no Warrior-usable spell found in scanned range";
    ASSERT_TRUE(mgr->scribeSpell(id, 0));
    eq.GetGameState().player().setClass(static_cast<uint32_t>(PlayerClass::Warrior));
    eq.GetGameState().player().setLevel(60);

    mgr->m_is_casting = true;
    EXPECT_FALSE(mgr->memorizeSpell(id, 0));
    mgr->m_is_casting = false;

    mgr->m_is_memorizing = true;
    EXPECT_FALSE(mgr->memorizeSpell(id, 0));
}

// ===================================================================
// 2. Cast validation matrix -- private validateCast()/checkMana()/
// checkRange()/adjustTargetForSpellType()/isValidCreatureTarget() called
// directly with hand-built SpellData. This is deliberately NOT routed
// through the DB for most branches: these methods take `const SpellData&`
// by reference, so a local struct exercises the exact same decision logic
// without needing a matching SQLite row. Only the public beginCastFromGem/
// beginCastById wiring itself (spell lookup by id via the manager's
// internal m_spell_db) needs the real DB -- covered separately below.
// ===================================================================

TEST(SpellManagerValidate, AlreadyCastingBlocksAnyCast) {
    EverQuest eq = MakeEq();
    auto* mgr = eq.GetSpellManager();
    mgr->m_is_casting = true;

    SpellData spell = MakeSpell(SpellTargetType::Self);
    EXPECT_EQ(mgr->validateCast(spell, 0), CastResult::AlreadyCasting);
}

TEST(SpellManagerValidate, NotEnoughManaBlocksCast) {
    EverQuest eq = MakeEq();
    auto* mgr = eq.GetSpellManager();
    eq.GetGameState().player().setMana(10, 100);

    SpellData spell = MakeSpell(SpellTargetType::Self, /*mana_cost=*/50);
    EXPECT_EQ(mgr->validateCast(spell, 0), CastResult::NotEnoughMana);
}

TEST(SpellManagerValidate, ZeroOrNegativeManaCostAlwaysPassesManaCheck) {
    EverQuest eq = MakeEq();
    auto* mgr = eq.GetSpellManager();
    eq.GetGameState().player().setMana(0, 0);

    SpellData spell = MakeSpell(SpellTargetType::Self, /*mana_cost=*/0);
    EXPECT_TRUE(mgr->checkMana(spell));
    SpellData freeSpell = MakeSpell(SpellTargetType::Self, /*mana_cost=*/-10);
    EXPECT_TRUE(mgr->checkMana(freeSpell));
}

TEST(SpellManagerValidate, EnoughManaPasses) {
    EverQuest eq = MakeEq();
    auto* mgr = eq.GetSpellManager();
    eq.GetGameState().player().setMana(50, 100);

    SpellData spell = MakeSpell(SpellTargetType::Self, /*mana_cost=*/50);
    EXPECT_TRUE(mgr->checkMana(spell));
}

TEST(SpellManagerValidate, OutOfRangeBlocksCast) {
    EverQuest eq = MakeEq();
    ASSERT_NO_THROW(ReplayZone(eq, HC_OP_NewSpawn,
        NewSpawnPayloadAt(100, "a_test_rat", 0, 500, 0)));
    auto* mgr = eq.GetSpellManager();

    SpellData spell = MakeSpell(SpellTargetType::Single, /*mana_cost=*/0, /*range=*/50);
    EXPECT_EQ(mgr->validateCast(spell, 100), CastResult::OutOfRange);
}

TEST(SpellManagerValidate, InRangePasses) {
    EverQuest eq = MakeEq();
    ASSERT_NO_THROW(ReplayZone(eq, HC_OP_NewSpawn,
        NewSpawnPayloadAt(100, "a_test_rat", 0, 10, 0)));
    auto* mgr = eq.GetSpellManager();

    SpellData spell = MakeSpell(SpellTargetType::Single, /*mana_cost=*/0, /*range=*/50);
    EXPECT_TRUE(mgr->checkRange(spell, 100));
}

TEST(SpellManagerValidate, ZeroRangeMeansNoLimit) {
    EverQuest eq = MakeEq();
    ASSERT_NO_THROW(ReplayZone(eq, HC_OP_NewSpawn,
        NewSpawnPayloadAt(100, "a_test_rat", 0, 99999, 0)));
    auto* mgr = eq.GetSpellManager();

    SpellData spell = MakeSpell(SpellTargetType::Single, /*mana_cost=*/0, /*range=*/0);
    EXPECT_TRUE(mgr->checkRange(spell, 100));
}

TEST(SpellManagerValidate, RangeCheckMissingTargetFails) {
    EverQuest eq = MakeEq();
    auto* mgr = eq.GetSpellManager();
    SpellData spell = MakeSpell(SpellTargetType::Single, /*mana_cost=*/0, /*range=*/50);
    EXPECT_FALSE(mgr->checkRange(spell, 999));
}

TEST(SpellManagerValidate, SelfSpellSkipsRangeCheckEvenFarAway) {
    // validateCast only range-checks when target_type != Self AND
    // target_id != 0 (spell_manager.cpp:503). A Self spell must pass
    // regardless of any "target" distance.
    EverQuest eq = MakeEq();
    ASSERT_NO_THROW(ReplayZone(eq, HC_OP_NewSpawn,
        NewSpawnPayloadAt(100, "a_test_rat", 0, 99999, 0)));
    auto* mgr = eq.GetSpellManager();

    SpellData spell = MakeSpell(SpellTargetType::Self, /*mana_cost=*/0, /*range=*/10);
    EXPECT_EQ(mgr->validateCast(spell, 100), CastResult::Success);
}

TEST(SpellManagerValidate, AnimalRestrictionRejectsNonAnimalTarget) {
    EverQuest eq = MakeEq();
    // bodytype 0 = Humanoid.
    ASSERT_NO_THROW(ReplayZone(eq, HC_OP_NewSpawn,
        NewSpawnPayloadWithBodyType(100, "a_guard", 0, 10, 0, /*bodytype=*/0)));
    auto* mgr = eq.GetSpellManager();

    SpellData spell = MakeSpell(SpellTargetType::Animal, /*mana_cost=*/0, /*range=*/0);
    EXPECT_EQ(mgr->validateCast(spell, 100), CastResult::InvalidTarget);
}

TEST(SpellManagerValidate, AnimalRestrictionAcceptsAnimalTarget) {
    EverQuest eq = MakeEq();
    // bodytype 20 = Animal (isValidCreatureTarget's BT_ANIMAL).
    ASSERT_NO_THROW(ReplayZone(eq, HC_OP_NewSpawn,
        NewSpawnPayloadWithBodyType(100, "a_wolf", 0, 10, 0, /*bodytype=*/20)));
    auto* mgr = eq.GetSpellManager();

    SpellData spell = MakeSpell(SpellTargetType::Animal, /*mana_cost=*/0, /*range=*/0);
    EXPECT_EQ(mgr->validateCast(spell, 100), CastResult::Success);
}

TEST(SpellManagerValidate, UndeadRestrictionAcceptsVampireBodytype) {
    EverQuest eq = MakeEq();
    // bodytype 11 = Vampire, folded into Undead per isValidCreatureTarget.
    ASSERT_NO_THROW(ReplayZone(eq, HC_OP_NewSpawn,
        NewSpawnPayloadWithBodyType(100, "a_vampire", 0, 10, 0, /*bodytype=*/11)));
    auto* mgr = eq.GetSpellManager();

    SpellData spell = MakeSpell(SpellTargetType::Undead, /*mana_cost=*/0, /*range=*/0);
    EXPECT_EQ(mgr->validateCast(spell, 100), CastResult::Success);
}

TEST(SpellManagerValidate, UnrestrictedTargetTypeIgnoresBodytype) {
    EverQuest eq = MakeEq();
    ASSERT_NO_THROW(ReplayZone(eq, HC_OP_NewSpawn,
        NewSpawnPayloadWithBodyType(100, "a_guard", 0, 10, 0, /*bodytype=*/0)));
    auto* mgr = eq.GetSpellManager();

    SpellData spell = MakeSpell(SpellTargetType::Single, /*mana_cost=*/0, /*range=*/0);
    EXPECT_EQ(mgr->validateCast(spell, 100), CastResult::Success);
}

// ---- adjustTargetForSpellType

TEST(SpellManagerAdjustTarget, SelfSpellAlwaysReturnsOwnId) {
    EverQuest eq = MakeEq();
    auto* mgr = eq.GetSpellManager();
    // Bare EverQuest defaults GetEntityID() to 0 (never received a
    // PlayerProfile) -- see T23's CombatManagerCycling.SelfIsExcluded.
    SpellData spell = MakeSpell(SpellTargetType::Self);
    EXPECT_EQ(mgr->adjustTargetForSpellType(spell, 12345), eq.GetEntityID());
}

TEST(SpellManagerAdjustTarget, PetSpellWithNoPetReturnsZero) {
    // Pinning the documented "no pet" no-op (spell_manager.cpp:387) --
    // standing up a real owned-pet fixture would need a PlayerProfile-driven
    // spawn_id plus a pet-owner spawn packet, out of proportion for this
    // single branch; the "has a pet" branch is skipped (see Review).
    EverQuest eq = MakeEq();
    auto* mgr = eq.GetSpellManager();
    ASSERT_EQ(eq.GetPetSpawnId(), 0u);

    SpellData spell = MakeSpell(SpellTargetType::Pet);
    EXPECT_EQ(mgr->adjustTargetForSpellType(spell, 5), 0u);
}

TEST(SpellManagerAdjustTarget, GroupSpellWithNoTargetDefaultsToSelf) {
    EverQuest eq = MakeEq();
    auto* mgr = eq.GetSpellManager();
    SpellData spell = MakeSpell(SpellTargetType::GroupV2);
    EXPECT_EQ(mgr->adjustTargetForSpellType(spell, 0), eq.GetEntityID());
}

TEST(SpellManagerAdjustTarget, GroupSpellWithTargetKeepsIt) {
    EverQuest eq = MakeEq();
    auto* mgr = eq.GetSpellManager();
    SpellData spell = MakeSpell(SpellTargetType::GroupV2);
    EXPECT_EQ(mgr->adjustTargetForSpellType(spell, 77), 77u);
}

TEST(SpellManagerAdjustTarget, AECasterAlwaysTargetsSelf) {
    EverQuest eq = MakeEq();
    auto* mgr = eq.GetSpellManager();
    SpellData spell = MakeSpell(SpellTargetType::AECaster);
    EXPECT_EQ(mgr->adjustTargetForSpellType(spell, 999), eq.GetEntityID());
}

TEST(SpellManagerAdjustTarget, DefaultTargetTypePassesThroughUnchanged) {
    EverQuest eq = MakeEq();
    auto* mgr = eq.GetSpellManager();
    SpellData spell = MakeSpell(SpellTargetType::Single);
    EXPECT_EQ(mgr->adjustTargetForSpellType(spell, 42), 42u);
}

// ---- beginCastFromGem / beginCastById state machine (no DB needed --
// these branches return before the internal m_spell_db.getSpell() call).

TEST(SpellManagerBeginCast, InvalidSlotReturnsInvalidSpell) {
    EverQuest eq = MakeEq();
    auto* mgr = eq.GetSpellManager();
    EXPECT_EQ(mgr->beginCastFromGem(EQ::MAX_SPELL_GEMS), CastResult::InvalidSpell);
}

TEST(SpellManagerBeginCast, EmptyGemReturnsNotMemorized) {
    EverQuest eq = MakeEq();
    auto* mgr = eq.GetSpellManager();
    EXPECT_EQ(mgr->beginCastFromGem(0), CastResult::NotMemorized);
}

TEST(SpellManagerBeginCast, CastingGemStateReturnsAlreadyCasting) {
    EverQuest eq = MakeEq();
    auto* mgr = eq.GetSpellManager();
    uint32_t gems[1] = {1};
    mgr->setSpellGems(gems, 1);
    mgr->m_gem_states[0] = GemState::Casting;

    EXPECT_EQ(mgr->beginCastFromGem(0), CastResult::AlreadyCasting);
}

TEST(SpellManagerBeginCast, RefreshGemStateReturnsGemCooldown) {
    EverQuest eq = MakeEq();
    auto* mgr = eq.GetSpellManager();
    uint32_t gems[1] = {1};
    mgr->setSpellGems(gems, 1);
    mgr->m_gem_states[0] = GemState::Refresh;

    EXPECT_EQ(mgr->beginCastFromGem(0), CastResult::GemCooldown);
}

TEST(SpellManagerBeginCast, MemorizeProgressGemStateReturnsSpellNotReady) {
    EverQuest eq = MakeEq();
    auto* mgr = eq.GetSpellManager();
    uint32_t gems[1] = {1};
    mgr->setSpellGems(gems, 1);
    mgr->m_gem_states[0] = GemState::MemorizeProgress;

    EXPECT_EQ(mgr->beginCastFromGem(0), CastResult::SpellNotReady);
}

TEST(SpellManagerBeginCast, ByIdUnknownSpellReturnsInvalidSpell) {
    // Not memorized in any gem (findGemSlotForSpell misses), and no DB
    // loaded so getSpell() is always nullptr -- InvalidSpell either way.
    EverQuest eq = MakeEq();
    auto* mgr = eq.GetSpellManager();
    EXPECT_EQ(mgr->beginCastById(999999999), CastResult::InvalidSpell);
}

// ---- beginCastFromGem / beginCastById end-to-end wiring (DB needed: the
// manager's internal m_spell_db.getSpell(spell_id) lookup must succeed).

TEST(SpellManagerBeginCast, SuccessSetsStateAndGemCasting) {
    if (!hasDb()) GTEST_SKIP() << "willeq.db not found, skipping";
    EverQuest eq = MakeEq();
    ASSERT_TRUE(eq.InitializeSpellSystem(""));
    auto* mgr = eq.GetSpellManager();

    uint32_t id = FindSpellId(mgr->getDatabase(), 1, 4000, [](const SpellData& s) {
        return s.target_type == SpellTargetType::Self && s.mana_cost <= 0;
    });
    if (id == 0) GTEST_SKIP() << "no free Self-target spell found in scanned range";

    uint32_t gems[1] = {id};
    mgr->setSpellGems(gems, 1);

    EXPECT_EQ(mgr->beginCastFromGem(0), CastResult::Success);
    EXPECT_TRUE(mgr->isCasting());
    EXPECT_EQ(mgr->getCurrentSpellId(), id);
    EXPECT_EQ(mgr->getGemState(0), GemState::Casting);
}

TEST(SpellManagerBeginCast, ByIdFindsGemSlotAndDelegates) {
    if (!hasDb()) GTEST_SKIP() << "willeq.db not found, skipping";
    EverQuest eq = MakeEq();
    ASSERT_TRUE(eq.InitializeSpellSystem(""));
    auto* mgr = eq.GetSpellManager();

    uint32_t id = FindSpellId(mgr->getDatabase(), 1, 4000, [](const SpellData& s) {
        return s.target_type == SpellTargetType::Self && s.mana_cost <= 0;
    });
    if (id == 0) GTEST_SKIP() << "no free Self-target spell found in scanned range";

    uint32_t gems[3] = {0, 0, id};
    mgr->setSpellGems(gems, 3);

    EXPECT_EQ(mgr->beginCastById(id), CastResult::Success);
    EXPECT_EQ(mgr->getGemState(2), GemState::Casting) << "must delegate to the slot holding it";
}

TEST(SpellManagerBeginCast, InvalidSpellIdViaGemReturnsInvalidSpell) {
    // Gem holds a spell id that isn't in the DB at all (real DB loaded, but
    // this specific id doesn't exist) -- must fail after the state checks.
    if (!hasDb()) GTEST_SKIP() << "willeq.db not found, skipping";
    EverQuest eq = MakeEq();
    ASSERT_TRUE(eq.InitializeSpellSystem(""));
    auto* mgr = eq.GetSpellManager();

    uint32_t gems[1] = {999999999};
    mgr->setSpellGems(gems, 1);
    EXPECT_EQ(mgr->beginCastFromGem(0), CastResult::InvalidSpell);
}


// Main entry point: opens the real SQLite spell DB once for the whole
// binary (test_spell_database.cpp precedent) so DB-gated tests can run;
// DB-independent tests run regardless. Timing/handlers/callbacks coverage
// continues in test_spell_manager_timing.cpp (split per the plan's
// <=800-line-per-file guidance -- none of that half needs the DB at all,
// see that file's header).
int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);

    std::string db_path = getWilleqDbPath();
    std::ifstream probe(db_path);
    if (probe.good()) {
        probe.close();
        if (g_willeq_db.open(db_path)) {
            EQT::WilleqDatabase::setGlobalInstance(&g_willeq_db);
            g_db_available = true;
        }
    }

    return RUN_ALL_TESTS();
}
