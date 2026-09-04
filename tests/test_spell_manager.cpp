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
// writes; the read side is EverQuest::ZoneProcessNewSpawn /
// ZoneProcessZoneSpawns in eq_entities.cpp). Kept local to this file
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

// T27 (refactor/T27_spell_class_levels.md): the T24 findings below this
// comment used to pin a LIVE BUG -- SpellDatabase::queryAndCache() never
// assigned SpellData::class_levels, so it stayed all-zero and
// canClassUse() was always true (see git history / testing_progress.md's
// "T24 finding" section for the original writeup). Will's ruling was to
// populate class_levels from spells_us.txt fields 104-119 (via the new
// willeq.db `spells.class_levels` BLOB column) rather than delete or leave
// the dead gate. These tests are re-pointed to pin the now-real pipeline
// against known spell data, so a future regression that breaks the
// builder/runtime mapping (wrong column, off-by-one class index, sentinel
// corruption) fails loudly instead of silently. Spell IDs and their real
// class-level requirements were confirmed directly against
// /mnt/projects/EverQuestP1999/spells_us.txt (fields 104-119, one byte per
// class in PlayerClass enum order Warrior..Berserker) and cross-checked
// against known EQ lore:
//   - id 3   "Summon Corpse" -- Necromancer-only, min level 39; every other
//     class (including Warrior) carries the 255 "cannot use" sentinel.
//   - id 200 "Minor Healing" -- Cleric/Druid/Shaman min level 1 (classic
//     starter heal), Paladin/Ranger min level 9, all others 255.
//   - id 4588 "Infuriate" -- Warrior-only, min level 40 (a real threshold:
//     rejected at 39, accepted at 40).
TEST(SpellManagerGems, ClassLevelsPopulatedFromDbPinsRealSpellData) {
    if (!hasDb()) GTEST_SKIP() << "willeq.db not found, skipping";
    EverQuest eq = MakeEq();
    ASSERT_TRUE(eq.InitializeSpellSystem(""));
    auto* mgr = eq.GetSpellManager();

    const SpellData* summonCorpse = mgr->getDatabase().getSpell(3);
    ASSERT_NE(summonCorpse, nullptr);
    EXPECT_EQ(summonCorpse->name, "Summon Corpse");
    // Warrior (class idx 1 -> class_levels[0]) carries the 255 sentinel --
    // must round-trip exactly, never decay to 0 or -1.
    EXPECT_EQ(summonCorpse->class_levels[0], 255u);
    EXPECT_FALSE(summonCorpse->canClassUse(PlayerClass::Warrior, 1));
    EXPECT_FALSE(summonCorpse->canClassUse(PlayerClass::Warrior, 60));
    // Necromancer (class idx 11 -> class_levels[10]) is 39.
    EXPECT_EQ(summonCorpse->class_levels[10], 39u);
    EXPECT_FALSE(summonCorpse->canClassUse(PlayerClass::Necromancer, 38));
    EXPECT_TRUE(summonCorpse->canClassUse(PlayerClass::Necromancer, 39));

    const SpellData* minorHealing = mgr->getDatabase().getSpell(200);
    ASSERT_NE(minorHealing, nullptr);
    EXPECT_EQ(minorHealing->name, "Minor Healing");
    // Cleric (class idx 2 -> class_levels[1]) usable from level 1.
    EXPECT_TRUE(minorHealing->canClassUse(PlayerClass::Cleric, 1));

    const SpellData* infuriate = mgr->getDatabase().getSpell(4588);
    ASSERT_NE(infuriate, nullptr);
    EXPECT_EQ(infuriate->name, "Infuriate");
    // Warrior threshold: rejected below 40, accepted at/above 40.
    EXPECT_FALSE(infuriate->canClassUse(PlayerClass::Warrior, 39));
    EXPECT_TRUE(infuriate->canClassUse(PlayerClass::Warrior, 40));
}

TEST(SpellManagerGems, MemorizeSpellRejectsClassThatCannotUseSpellAtAll) {
    if (!hasDb()) GTEST_SKIP() << "willeq.db not found, skipping";
    EverQuest eq = MakeEq();
    ASSERT_TRUE(eq.InitializeSpellSystem(""));
    auto* mgr = eq.GetSpellManager();

    // "Summon Corpse" (id 3) is Necromancer-only; a Warrior must never be
    // able to memorize it, at any level, once scribed.
    ASSERT_TRUE(mgr->scribeSpell(3, 0));
    eq.GetGameState().player().setClass(static_cast<uint32_t>(PlayerClass::Warrior));
    eq.GetGameState().player().setLevel(60);

    EXPECT_FALSE(mgr->memorizeSpell(3, 0))
        << "the real class/level gate (spell_manager.cpp:630) must reject a "
           "class that carries the 255 'cannot use' sentinel for this spell.";
    EXPECT_FALSE(mgr->isMemorizing());
}

TEST(SpellManagerGems, MemorizeSpellEnforcesRealLevelThreshold) {
    if (!hasDb()) GTEST_SKIP() << "willeq.db not found, skipping";
    EverQuest eq = MakeEq();
    ASSERT_TRUE(eq.InitializeSpellSystem(""));
    auto* mgr = eq.GetSpellManager();

    // "Infuriate" (id 4588) is Warrior-only, min level 40.
    ASSERT_TRUE(mgr->scribeSpell(4588, 0));
    eq.GetGameState().player().setClass(static_cast<uint32_t>(PlayerClass::Warrior));

    eq.GetGameState().player().setLevel(39);
    EXPECT_FALSE(mgr->memorizeSpell(4588, 0))
        << "level 39 Warrior must be rejected -- Infuriate requires level 40.";
    EXPECT_FALSE(mgr->isMemorizing());

    eq.GetGameState().player().setLevel(40);
    EXPECT_TRUE(mgr->memorizeSpell(4588, 0))
        << "level 40 Warrior must be accepted -- meets Infuriate's threshold.";
    EXPECT_TRUE(mgr->isMemorizing());
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

// ---- T355: endurance, the mana check's sibling. SpellData::endurance_cost
// was loaded from nothing before T355 (spell_database.cpp assigned mana_cost
// and skipped it), so every discipline read as free here and the client sent
// a cast the server then refused one round trip later
// (eqemu zone/effects.cpp:900).

TEST(SpellManagerValidate, NotEnoughEnduranceBlocksCast) {
    EverQuest eq = MakeEq();
    auto* mgr = eq.GetSpellManager();
    eq.GetGameState().player().setMana(1000, 1000);
    eq.GetGameState().player().setEndurance(50, 1000);

    SpellData spell = MakeSpell(SpellTargetType::Self);
    spell.endurance_cost = 900;  // Unholy Aura Discipline (4520)
    EXPECT_EQ(mgr->validateCast(spell, 0), CastResult::NotEnoughEndurance);
}

TEST(SpellManagerValidate, ZeroOrNegativeEnduranceCostAlwaysPassesEnduranceCheck) {
    EverQuest eq = MakeEq();
    auto* mgr = eq.GetSpellManager();
    eq.GetGameState().player().setEndurance(0, 0);

    SpellData spell = MakeSpell(SpellTargetType::Self);
    spell.endurance_cost = 0;
    EXPECT_TRUE(mgr->checkEndurance(spell));
    SpellData refunding = MakeSpell(SpellTargetType::Self);
    refunding.endurance_cost = -10;
    EXPECT_TRUE(mgr->checkEndurance(refunding));
}

TEST(SpellManagerValidate, EnoughEndurancePasses) {
    EverQuest eq = MakeEq();
    auto* mgr = eq.GetSpellManager();
    eq.GetGameState().player().setEndurance(900, 1000);

    SpellData spell = MakeSpell(SpellTargetType::Self);
    spell.endurance_cost = 900;
    EXPECT_TRUE(mgr->checkEndurance(spell));
    EXPECT_EQ(mgr->validateCast(spell, 0), CastResult::Success);
}

// Mana is checked first, so a spell short on both names mana -- pinning the
// order rather than leaving it to whichever branch happens to run first.
TEST(SpellManagerValidate, ManaIsCheckedBeforeEndurance) {
    EverQuest eq = MakeEq();
    auto* mgr = eq.GetSpellManager();
    eq.GetGameState().player().setMana(0, 100);
    eq.GetGameState().player().setEndurance(0, 1000);

    SpellData spell = MakeSpell(SpellTargetType::Self, /*mana_cost=*/50);
    spell.endurance_cost = 900;
    EXPECT_EQ(mgr->validateCast(spell, 0), CastResult::NotEnoughMana);
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
    // T314: bodytype 1 = Humanoid (eqemu common/bodytypes.h via
    // client/eq_body_types.h). This test used to say 0, which is the old
    // off-by-one BT_HUMANOID; 0 is "no spawn data recorded".
    ASSERT_NO_THROW(ReplayZone(eq, HC_OP_NewSpawn,
        NewSpawnPayloadWithBodyType(100, "a_guard", 0, 10, 0, /*bodytype=*/1)));
    auto* mgr = eq.GetSpellManager();

    SpellData spell = MakeSpell(SpellTargetType::Animal, /*mana_cost=*/0, /*range=*/0);
    EXPECT_EQ(mgr->validateCast(spell, 100), CastResult::InvalidTarget);
}

TEST(SpellManagerValidate, AnimalRestrictionAcceptsAnimalTarget) {
    EverQuest eq = MakeEq();
    // T314: bodytype 21 = Animal. This test used to say 20 (Luggald), the
    // old off-by-one BT_ANIMAL. Full accept/reject table:
    // tests/test_spell_body_type_targeting.cpp.
    ASSERT_NO_THROW(ReplayZone(eq, HC_OP_NewSpawn,
        NewSpawnPayloadWithBodyType(100, "a_wolf", 0, 10, 0, /*bodytype=*/21)));
    auto* mgr = eq.GetSpellManager();

    SpellData spell = MakeSpell(SpellTargetType::Animal, /*mana_cost=*/0, /*range=*/0);
    EXPECT_EQ(mgr->validateCast(spell, 100), CastResult::Success);
}

TEST(SpellManagerValidate, UndeadRestrictionAcceptsVampireBodytype) {
    EverQuest eq = MakeEq();
    // T314: bodytype 12 = Vampire, an accepted undead target (eqemu
    // ST_Undead). This test used to say 11 (NoTarget), the old BT_VAMPIRE.
    ASSERT_NO_THROW(ReplayZone(eq, HC_OP_NewSpawn,
        NewSpawnPayloadWithBodyType(100, "a_vampire", 0, 10, 0, /*bodytype=*/12)));
    auto* mgr = eq.GetSpellManager();

    SpellData spell = MakeSpell(SpellTargetType::Undead, /*mana_cost=*/0, /*range=*/0);
    EXPECT_EQ(mgr->validateCast(spell, 100), CastResult::Success);
}

TEST(SpellManagerValidate, UnrestrictedTargetTypeIgnoresBodytype) {
    EverQuest eq = MakeEq();
    ASSERT_NO_THROW(ReplayZone(eq, HC_OP_NewSpawn,
        NewSpawnPayloadWithBodyType(100, "a_guard", 0, 10, 0, /*bodytype=*/1)));
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
    zonetest::TestBridge bridge;
    mgr->setBridge(&bridge);

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

    // T44: SpellCastStarted is published synchronously here, client-side,
    // rather than waiting for the server to echo BeginCast back -- see
    // beginCastFromGem()'s comment for why (matches the deleted poll's
    // timing, which saw isCasting flip true on the very next tick).
    auto events = bridge.drainEvents();
    bool sawCastStarted = false;
    for (auto& e : events) {
        if (e.type == eqt::state::GameEventType::SpellCastStarted) {
            const auto& d = std::get<eqt::state::SpellCastStartedData>(e.data);
            EXPECT_EQ(d.casterId, eq.GetEntityID());
            EXPECT_EQ(d.spellId, id);
            sawCastStarted = true;
        }
    }
    EXPECT_TRUE(sawCastStarted);
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
