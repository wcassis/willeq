/*
 * Spell Database Tests
 *
 * Tests for the SQLite-backed SpellDatabase (client/spell/spell_database.h).
 *
 * The database is opened once for the whole binary (see main()) against
 * data/willeq.db and installed as the WilleqDatabase global instance;
 * SpellDatabase::initFromDatabase() reads from that global instance.
 *
 * NOTE: This file was rewritten against the current SQLite-backed API.
 * The previous version tested a legacy in-memory, file-loaded
 * (spells_us.txt) SpellDatabase with bulk-query methods
 * (getAllSpellIds, getSpellsForClass, getSpellsByLevelRange,
 * getBeneficialSpells/getDetrimentalSpells, getSpellsByEffect,
 * filterSpells) and a case-insensitive/partial name search. None of
 * those exist on the current API, which only supports single-spell
 * lookups by id or exact name backed by an LRU cache in front of
 * SQLite queries. See refactor/F00_test_repair.md review section for
 * the full list of removed test cases and why.
 */

#include <gtest/gtest.h>
#include "client/spell/spell_database.h"
#include "client/spell/spell_data.h"
#include "client/spell/spell_constants.h"
#include "client/willeq_database.h"
#include <algorithm>
#include <cctype>
#include <fstream>
#include <cstdlib>
#include <string>
#include <vector>

using namespace EQ;

#ifndef WILLEQ_DB_PATH
#define WILLEQ_DB_PATH "data/willeq.db"
#endif

namespace {

// Opened once for the process in main(); tests treat it as read-only.
EQT::WilleqDatabase g_willeq_db;
bool g_db_available = false;

std::string getWilleqDbPath() {
    const char* env_path = std::getenv("WILLEQ_DB_PATH");
    if (env_path) {
        return std::string(env_path);
    }
    return WILLEQ_DB_PATH;
}

} // namespace

class SpellDatabaseTest : public ::testing::Test {
protected:
    SpellDatabase db;

    void SetUp() override {
        if (g_db_available) {
            ASSERT_TRUE(db.initFromDatabase()) << db.getLoadError();
        }
    }

    bool hasDb() const { return g_db_available; }

    // Replaces the removed SpellDatabase::getAllSpellIds(): scans
    // [lo, hi] and returns up to max_results ids that exist in the DB.
    std::vector<uint32_t> scanSpellIds(uint32_t lo, uint32_t hi, size_t max_results) {
        std::vector<uint32_t> found;
        for (uint32_t id = lo; id <= hi && found.size() < max_results; ++id) {
            if (db.hasSpell(id)) {
                found.push_back(id);
            }
        }
        return found;
    }
};

// ============================================================================
// Basic Loading Tests
// ============================================================================

TEST_F(SpellDatabaseTest, LoadsSuccessfully) {
    if (!hasDb()) {
        GTEST_SKIP() << "willeq.db not found, skipping";
    }

    EXPECT_TRUE(db.isLoaded());
    EXPECT_GT(db.getSpellCount(), 0u);
    EXPECT_TRUE(db.getLoadError().empty());
}

TEST_F(SpellDatabaseTest, LoadsReasonableSpellCount) {
    if (!hasDb()) {
        GTEST_SKIP() << "willeq.db not found, skipping";
    }

    // Titanium client should have thousands of spells
    EXPECT_GT(db.getSpellCount(), 1000u);
    EXPECT_LT(db.getSpellCount(), 50000u);  // Sanity check
}

// Original test loaded SpellDatabase::loadFromFile("/nonexistent/...") and
// checked graceful failure. loadFromFile no longer exists — SpellDatabase
// always reads from the WilleqDatabase global instance. The equivalent
// "missing file" failure now lives on WilleqDatabase::open() itself.
TEST(WilleqDatabaseTest, FailsGracefullyOnMissingFile) {
    EQT::WilleqDatabase bad_db;
    EXPECT_FALSE(bad_db.open("/nonexistent/path/willeq.db"));
    EXPECT_FALSE(bad_db.isOpen());
}

// ============================================================================
// Spell Lookup Tests
// ============================================================================

TEST_F(SpellDatabaseTest, LookupByIdWorks) {
    if (!hasDb()) {
        GTEST_SKIP() << "willeq.db not found, skipping";
    }

    auto ids = scanSpellIds(1, 9000, 1);
    ASSERT_GT(ids.size(), 0u);

    const SpellData* spell = db.getSpell(ids[0]);
    EXPECT_NE(spell, nullptr);
    EXPECT_EQ(spell->id, ids[0]);
}

TEST_F(SpellDatabaseTest, LookupByIdReturnsNullForInvalid) {
    if (!hasDb()) {
        GTEST_SKIP() << "willeq.db not found, skipping";
    }

    EXPECT_EQ(db.getSpell(999999999), nullptr);
    EXPECT_EQ(db.getSpell(SPELL_UNKNOWN), nullptr);
}

TEST_F(SpellDatabaseTest, HasSpellWorks) {
    if (!hasDb()) {
        GTEST_SKIP() << "willeq.db not found, skipping";
    }

    auto ids = scanSpellIds(1, 9000, 1);
    ASSERT_GT(ids.size(), 0u);

    EXPECT_TRUE(db.hasSpell(ids[0]));
    EXPECT_FALSE(db.hasSpell(999999999));
}

// ============================================================================
// Name Lookup Tests
//
// getSpellByName() is the exact-match query (WHERE name=?, no COLLATE
// NOCASE); findSpellsByName() is the partial one (T316: a bound, escaped
// LIKE query, ASCII-case-insensitive, capped at kMaxNameSearchResults).
// Until T316 findSpellsByName() just forwarded to getSpellByName(), so the
// legacy partial-name capability was gone and the tests for it had been
// deleted; the /mem-facing behaviour is pinned in
// tests/test_eq_action_handler_spell_pet.cpp, at the layer a player reaches.
// What is left here is the exact-match round-trip plus the escaping edge
// cases.
// ============================================================================

TEST_F(SpellDatabaseTest, GetSpellByNameWorks) {
    if (!hasDb()) {
        GTEST_SKIP() << "willeq.db not found, skipping";
    }

    auto ids = scanSpellIds(1, 9000, 1);
    ASSERT_GT(ids.size(), 0u);

    const SpellData* spell = db.getSpell(ids[0]);
    ASSERT_NE(spell, nullptr);
    std::string name = spell->name;

    const SpellData* looked_up = db.getSpellByName(name);
    ASSERT_NE(looked_up, nullptr);
    EXPECT_EQ(looked_up->id, spell->id);
}

// ============================================================================
// Spell Data Validation Tests
// ============================================================================

TEST_F(SpellDatabaseTest, SpellsHaveValidData) {
    if (!hasDb()) {
        GTEST_SKIP() << "willeq.db not found, skipping";
    }

    auto ids = scanSpellIds(1, 9000, 500);
    ASSERT_GT(ids.size(), 0u);
    int valid_count = 0;

    for (uint32_t id : ids) {
        const SpellData* spell = db.getSpell(id);
        ASSERT_NE(spell, nullptr);

        // Every spell must have an ID and name
        EXPECT_NE(spell->id, SPELL_UNKNOWN);
        EXPECT_FALSE(spell->name.empty());

        // Verify target type is in valid range
        EXPECT_LE(static_cast<int>(spell->target_type), 100);

        // Verify resist type is in valid range
        EXPECT_LE(static_cast<int>(spell->resist_type), 10);

        // Cast time should be reasonable (0 to 30 seconds)
        EXPECT_LE(spell->cast_time_ms, 30000u);

        valid_count++;
    }

    EXPECT_GT(valid_count, 0);
}

// ============================================================================
// Effect Slot Tests (T68 — restores SpellEffectsAreValid/EffectCountAndHasEffect,
// deleted when SpellData::effects was still always-empty; see the comment
// that used to live here, still readable in git history. effects is now
// populated by queryAndCache() from EQT::SpellInfo's 61 effect-slot columns).
// ============================================================================

// Spell id 3 ("Summon Corpse") is the plan's confirmed reference spell:
// slot 1 = SPA 91 (SummonCorpse), base_value 70, formula 100 (unused-marker,
// which is also SpellEffectSlot's own "no scaling" default), slots 2-12
// carry the raw 254 "unused" marker and must NOT be interpreted as SPA 254.
TEST_F(SpellDatabaseTest, EffectSlotsMatchConfirmedReferenceSpell) {
    if (!hasDb()) {
        GTEST_SKIP() << "willeq.db not found, skipping";
    }

    const SpellData* spell = db.getSpell(3);
    ASSERT_NE(spell, nullptr);
    EXPECT_EQ(spell->name, "Summon Corpse");

    ASSERT_EQ(spell->effects[0].effect_id, SpellEffect::SummonCorpse);
    EXPECT_EQ(spell->effects[0].base_value, 70);
    EXPECT_TRUE(spell->effects[0].isValid());
    EXPECT_EQ(spell->getEffectCount(), 1);

    for (size_t i = 1; i < spell->effects.size(); ++i) {
        EXPECT_EQ(spell->effects[i].effect_id, SpellEffect::UnusedEffect)
            << "slot " << i << " should be the unused marker, not SPA 254";
        EXPECT_FALSE(spell->effects[i].isValid()) << "slot " << i;
    }
}

// Marker interpretation must happen at the consumer boundary (queryAndCache),
// not survive as the raw 254/100 file values — that would silently corrupt
// every predicate built on effects (hasEffect, isDamageSpell, ...). Scans a
// broad id range and asserts the invariant holds for every returned spell:
// an "unused" slot's effect_id must be exactly UnusedEffect, never the raw
// 254 misread as an enumerator that happens to also be 254 in spells_us.txt
// terms (i.e. it must be recognized as unused by isValid()).
TEST_F(SpellDatabaseTest, UnusedSlotMarkerNeverMisreadAsRealEffect) {
    if (!hasDb()) {
        GTEST_SKIP() << "willeq.db not found, skipping";
    }

    auto ids = scanSpellIds(1, 9000, 500);
    ASSERT_GT(ids.size(), 0u);

    int slotFillCounts[12] = {0};
    for (uint32_t id : ids) {
        const SpellData* spell = db.getSpell(id);
        ASSERT_NE(spell, nullptr);
        for (size_t i = 0; i < spell->effects.size(); ++i) {
            const auto& e = spell->effects[i];
            if (e.effect_id == SpellEffect::UnusedEffect) {
                EXPECT_FALSE(e.isValid()) << "id=" << id << " slot=" << i;
            } else {
                slotFillCounts[i]++;
            }
        }
    }

    // Slot fill must decay monotonically-ish across the 12 slots (slot 1
    // used far more often than slot 12) — the shape the plan predicted for
    // a correctly-interpreted marker. A marker bug (e.g. treating 254 as a
    // real SPA) would instead show every slot uniformly "full".
    EXPECT_GT(slotFillCounts[0], slotFillCounts[11])
        << "slot 1 fill=" << slotFillCounts[0] << " slot 12 fill=" << slotFillCounts[11];
}

TEST_F(SpellDatabaseTest, EffectCountAndHasEffect) {
    if (!hasDb()) {
        GTEST_SKIP() << "willeq.db not found, skipping";
    }

    // Spell 3 has exactly one populated slot (SummonCorpse).
    const SpellData* single = db.getSpell(3);
    ASSERT_NE(single, nullptr);
    EXPECT_EQ(single->getEffectCount(), 1);
    EXPECT_TRUE(single->hasEffect(SpellEffect::SummonCorpse));
    EXPECT_FALSE(single->hasEffect(SpellEffect::ArmorClass));
    EXPECT_NE(single->getEffect(SpellEffect::SummonCorpse), nullptr);
    EXPECT_EQ(single->getEffect(SpellEffect::ArmorClass), nullptr);

    // Spell 891 is a confirmed full-12-slot spell (spot-checked against
    // spells_us.txt in the T68 Review): every slot populated, none unused.
    const SpellData* full = db.getSpell(891);
    ASSERT_NE(full, nullptr);
    EXPECT_EQ(full->getEffectCount(), 12);
    for (const auto& e : full->effects) {
        EXPECT_TRUE(e.isValid());
    }
}

// A -1 SpellAffectIndex (799 spells in spells_us.txt, one shy of that in
// this Titanium data file at 798) must round-trip as -1, not get clamped
// or misread as an unsigned "no data" sentinel.
TEST_F(SpellDatabaseTest, NegativeSpellAffectIndexRoundTrips) {
    if (!hasDb()) {
        GTEST_SKIP() << "willeq.db not found, skipping";
    }

    const SpellData* spell = db.getSpell(523);
    ASSERT_NE(spell, nullptr);
    EXPECT_EQ(spell->spell_affect_index, -1);
}

// B1127 (#447): spells_us.txt field 145 (eqemu spellanim) -- the spellsnew.eff
// record the official client draws -- reaches SpellData verbatim, including
// for a spell with no spells.eff record (1899 Imbue Fire Opal).
TEST_F(SpellDatabaseTest, SpellAnimFieldRoundTrips) {
    if (!hasDb()) {
        GTEST_SKIP() << "willeq.db not found, skipping";
    }
    struct Row { uint32_t id; int32_t affect; int32_t anim; };
    for (const Row& r : {Row{54, 23, 82}, Row{656, 23, 82}, Row{458, 33, 121},
                         Row{93, 0, 80}, Row{1656, 25, 85}, Row{14, 25, 115},
                         Row{1899, -1, 83}}) {
        const SpellData* spell = db.getSpell(r.id);
        ASSERT_NE(spell, nullptr) << r.id;
        EXPECT_EQ(spell->spell_affect_index, r.affect) << r.id;
        EXPECT_EQ(spell->spell_anim, r.anim) << r.id;
        EXPECT_EQ(spell->visualVocabulary().spellAnim, r.anim) << r.id;
    }
}

// ============================================================================
// Filtering Tests — all deleted.
//
// FilterByClass, FilterByLevelRange, FilterBeneficialDetrimental,
// FilterByEffect, CustomFilter all called bulk-query methods that no
// longer exist on SpellDatabase: getSpellsForClass, getSpellsByLevelRange,
// getBeneficialSpells, getDetrimentalSpells, getSpellsByEffect,
// filterSpells. The current API is single-spell lookup only (by id or
// exact name); there is no equivalent bulk/filter capability to test.
// ============================================================================

// ============================================================================
// SpellData Helper Method Tests
// ============================================================================

// SpellDataHelperMethods (T68 restore): isDamageSpell()/isHealSpell()
// (spell_data.h) walk SpellData::effects — dead code until this unit
// populated it. "Strike" (14) and "Smite" (16) are confirmed-in-DB CurrentHP
// spells with a negative base_value (damage); "Healing" (12) is a confirmed
// CurrentHP spell with a positive base_value (heal). Exercising the real,
// now-live predicate rather than asserting on a hand-built SpellData.
TEST_F(SpellDatabaseTest, IsDamageSpellDiscriminatesOnRealData) {
    if (!hasDb()) {
        GTEST_SKIP() << "willeq.db not found, skipping";
    }

    const SpellData* strike = db.getSpell(14);
    ASSERT_NE(strike, nullptr);
    EXPECT_TRUE(strike->isDamageSpell()) << "id=14 (\"Strike\") has CurrentHP base_value < 0";
    EXPECT_FALSE(strike->isHealSpell());
}

TEST_F(SpellDatabaseTest, IsHealSpellDiscriminatesOnRealData) {
    if (!hasDb()) {
        GTEST_SKIP() << "willeq.db not found, skipping";
    }

    const SpellData* healing = db.getSpell(12);
    ASSERT_NE(healing, nullptr);
    EXPECT_TRUE(healing->isHealSpell()) << "id=12 (\"Healing\") has CurrentHP base_value > 0";
    EXPECT_FALSE(healing->isDamageSpell());
}

TEST_F(SpellDatabaseTest, DurationCalculation) {
    if (!hasDb()) {
        GTEST_SKIP() << "willeq.db not found, skipping";
    }

    // Find a spell with duration formula (duration_formula/duration_cap
    // ARE populated by queryAndCache, unlike effects/class_levels).
    auto ids = scanSpellIds(1, 9000, 500);
    const SpellData* found = nullptr;
    for (uint32_t id : ids) {
        const SpellData* spell = db.getSpell(id);
        if (spell && spell->hasDuration() && spell->duration_formula > 0) {
            found = spell;
            break;
        }
    }

    if (!found) {
        GTEST_SKIP() << "No spell with a duration formula found in scanned range";
    }

    // Duration at level 50 should be positive
    uint32_t duration = found->calculateDuration(50);
    EXPECT_GT(duration, 0u);

    // Duration at higher level should be >= lower level (for most formulas)
    uint32_t duration_low = found->calculateDuration(10);
    uint32_t duration_high = found->calculateDuration(50);
    EXPECT_GE(duration_high, duration_low);
}

// ============================================================================
// Field-mapping and cache-bookkeeping tests (T22)
//
// The above tests exercise getSpell()/getSpellByName() but mostly assert
// loose range bounds (e.g. resist_type <= 10), which do not distinguish a
// correctly-mapped field from a mutant that reads the wrong source field
// (a same-range swap survives a bounds check). These compare against the
// raw EQT::SpellInfo returned directly by WilleqDatabase for the same id,
// which pins queryAndCache()'s field-by-field mapping exactly.
// ============================================================================

TEST_F(SpellDatabaseTest, FieldsMatchRawDatabaseQuery) {
    if (!hasDb()) {
        GTEST_SKIP() << "willeq.db not found, skipping";
    }

    auto ids = scanSpellIds(1, 9000, 25);
    ASSERT_GT(ids.size(), 0u);

    for (uint32_t id : ids) {
        const SpellData* spell = db.getSpell(id);
        ASSERT_NE(spell, nullptr);

        EQT::SpellInfo raw = g_willeq_db.getSpell(id);
        ASSERT_TRUE(raw.found);

        EXPECT_EQ(spell->cast_time_ms, raw.castTimeMs) << "id=" << id;
        EXPECT_EQ(spell->recovery_time_ms, raw.recoveryTimeMs) << "id=" << id;
        EXPECT_EQ(spell->recast_time_ms, raw.recastTimeMs) << "id=" << id;
        EXPECT_EQ(spell->duration_formula, raw.durationFormula) << "id=" << id;
        EXPECT_EQ(spell->duration_cap, raw.durationCap) << "id=" << id;
        EXPECT_EQ(static_cast<int32_t>(spell->target_type), raw.targetType) << "id=" << id;
        EXPECT_EQ(static_cast<int32_t>(spell->resist_type), raw.resistType) << "id=" << id;
    }
}

TEST_F(SpellDatabaseTest, CacheHitCounterIncrementsOnRepeatLookup) {
    if (!hasDb()) {
        GTEST_SKIP() << "willeq.db not found, skipping";
    }

    // NOTE: scanSpellIds() itself calls db.hasSpell() -> db.getSpell(),
    // which populates the cache and counters as a side effect. So by the
    // time ids[0] is known, it is already cached. Capture counters AFTER
    // scanning (so the "first" explicit call below is knowingly a hit),
    // and assert only the delta the repeat lookup produces.
    auto ids = scanSpellIds(1, 9000, 1);
    ASSERT_GT(ids.size(), 0u);

    ASSERT_NE(db.getSpell(ids[0]), nullptr);  // ensure cached (may be hit or miss)
    uint64_t hits_before = db.getCacheHits();
    uint64_t misses_before = db.getCacheMisses();

    // Repeat lookup of the same (now-cached) id: must hit, not miss.
    ASSERT_NE(db.getSpell(ids[0]), nullptr);
    EXPECT_EQ(db.getCacheHits(), hits_before + 1);
    EXPECT_EQ(db.getCacheMisses(), misses_before);
}

TEST_F(SpellDatabaseTest, CacheNeverExceedsCapacity) {
    if (!hasDb()) {
        GTEST_SKIP() << "willeq.db not found, skipping";
    }

    size_t capacity = db.getCacheCapacity();
    // Look up strictly more distinct ids than the cache capacity so the
    // LRU evictIfNeeded() boundary check is actually exercised.
    auto ids = scanSpellIds(1, 9000, capacity + 20);
    ASSERT_GT(ids.size(), capacity)
        << "not enough distinct spells in scanned range to exceed cache capacity";

    for (uint32_t id : ids) {
        ASSERT_NE(db.getSpell(id), nullptr);
        ASSERT_LE(db.getCacheSize(), capacity)
            << "cache grew past capacity after looking up id=" << id;
    }
}

TEST_F(SpellDatabaseTest, MaxSpellIdTracksHighestLookedUpId) {
    if (!hasDb()) {
        GTEST_SKIP() << "willeq.db not found, skipping";
    }

    // NOTE: db is a fresh instance per TEST_F (SetUp() only calls
    // initFromDatabase(), no lookups), so getMaxSpellId() starts at 0 here
    // — but scanSpellIds() itself performs lookups via db.hasSpell(), so
    // it must not be used to pick the ids under test here (it would
    // pollute the baseline before we can observe it). Find candidate ids
    // via the raw WilleqDatabase handle instead, which bypasses `db`
    // entirely.
    ASSERT_EQ(db.getMaxSpellId(), 0u);

    std::vector<uint32_t> ids;
    for (uint32_t id = 1; id <= 9000 && ids.size() < 5; ++id) {
        if (g_willeq_db.getSpell(id).found) ids.push_back(id);
    }
    ASSERT_GT(ids.size(), 0u);

    // Look up in a deliberately non-monotonic (descending) order via `db`;
    // getMaxSpellId() must still reflect the highest id ever looked up,
    // not the most recently looked-up one.
    uint32_t highest = 0;
    for (uint32_t id : ids) highest = std::max(highest, id);
    std::vector<uint32_t> descending(ids.rbegin(), ids.rend());
    for (uint32_t id : descending) {
        ASSERT_NE(db.getSpell(id), nullptr);
    }

    EXPECT_EQ(db.getMaxSpellId(), highest);

    // Looking up a smaller id afterward must not lower it.
    ASSERT_NE(db.getSpell(descending.back()), nullptr);
    EXPECT_EQ(db.getMaxSpellId(), highest);
}

// T316 restored partial/substring matching to findSpellsByName(), so a
// lowercase term like "heal" matches "Complete Heal" again. The
// substring/case/escaping cases live with the /mem coverage in
// test_eq_action_handler_spell_pet.cpp; the two below are the local edges.

// ============================================================================
// Edge Case Tests
// ============================================================================

TEST_F(SpellDatabaseTest, EmptyStringSearchReturnsEmpty) {
    if (!hasDb()) {
        GTEST_SKIP() << "willeq.db not found, skipping";
    }

    // T316: an empty search term would build the pattern "%%" -- every spell
    // in the game. It must return nothing instead.
    EXPECT_TRUE(db.findSpellsByName("").empty());
}

TEST_F(SpellDatabaseTest, PartialNameSearchIsCaseInsensitiveAndSubstring) {
    if (!hasDb()) {
        GTEST_SKIP() << "willeq.db not found, skipping";
    }

    auto ids = scanSpellIds(1, 9000, 1);
    ASSERT_GT(ids.size(), 0u);
    const SpellData* spell = db.getSpell(ids[0]);
    ASSERT_NE(spell, nullptr);
    ASSERT_GE(spell->name.size(), 4u);

    // A lowercased interior slice of the real name: not an exact match, and
    // not even the same case as the row.
    std::string fragment = spell->name.substr(1, spell->name.size() - 2);
    for (char& c : fragment) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }

    EXPECT_EQ(db.getSpellByName(fragment), nullptr) << "fragment: " << fragment;

    auto results = db.findSpellsByName(fragment);
    bool found = false;
    for (const auto* s : results) {
        if (s->id == spell->id) found = true;
    }
    EXPECT_TRUE(found) << "'" << fragment << "' must match '" << spell->name
                       << "' -- before T316 findSpellsByName() was exact-match only";
}

TEST_F(SpellDatabaseTest, GetSpellByInvalidNameReturnsNull) {
    if (!hasDb()) {
        GTEST_SKIP() << "willeq.db not found, skipping";
    }

    EXPECT_EQ(db.getSpellByName("zzzznonexistentspellzzz"), nullptr);
}

// Main entry point for tests
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

// =====================================================================
// T246: reagents (spells_us.txt fields 58-69) reach SpellData. Before T246
// willeq.db had no reagent columns, so reagent_id/reagent_count were
// always 0 and requiresReagents() was false for every spell (RED:
// review/T246/red_spell_database.txt). Reference rows verified directly
// against /mnt/projects/EverQuestP1999/spells_us.txt (see
// review/T246/db_regeneration_proof.txt).
// =====================================================================

// Bone Walk (351): one consumed component, item 13073 x2, no no-expend.
TEST_F(SpellDatabaseTest, ReagentsRoundTripSingleComponentWithCount) {
    if (!hasDb()) GTEST_SKIP() << "willeq.db not found, skipping";
    const SpellData* spell = db.getSpell(351);
    ASSERT_NE(spell, nullptr);
    EXPECT_EQ(spell->name, "Bone Walk");
    EXPECT_EQ(spell->reagent_id[0], 13073u);
    EXPECT_EQ(spell->reagent_count[0], 2u);
    for (size_t i = 1; i < spell->reagent_id.size(); ++i) {
        EXPECT_EQ(spell->reagent_id[i], 0u) << "slot " << i;
        EXPECT_EQ(spell->reagent_count[i], 0u) << "slot " << i << " (file says 1 for an empty slot; must not survive)";
    }
    for (size_t i = 0; i < spell->noexpend_reagent_id.size(); ++i) {
        EXPECT_EQ(spell->noexpend_reagent_id[i], 0u) << "noexpend slot " << i;
    }
    EXPECT_TRUE(spell->requiresReagents());
}

// Thicken Mana (390): two components (10024, 16965), one each.
TEST_F(SpellDatabaseTest, ReagentsRoundTripTwoComponents) {
    if (!hasDb()) GTEST_SKIP() << "willeq.db not found, skipping";
    const SpellData* spell = db.getSpell(390);
    ASSERT_NE(spell, nullptr);
    EXPECT_EQ(spell->name, "Thicken Mana");
    EXPECT_EQ(spell->reagent_id[0], 10024u);
    EXPECT_EQ(spell->reagent_count[0], 1u);
    EXPECT_EQ(spell->reagent_id[1], 16965u);
    EXPECT_EQ(spell->reagent_count[1], 1u);
    EXPECT_EQ(spell->reagent_id[2], 0u);
    EXPECT_EQ(spell->reagent_id[3], 0u);
    EXPECT_TRUE(spell->requiresReagents());
}

// Cloak (5): no reagents at all -- the file's -1 markers must land as 0
// and the "1" counts beside them must not make requiresReagents() true.
TEST_F(SpellDatabaseTest, NoReagentMarkerLandsAsZero) {
    if (!hasDb()) GTEST_SKIP() << "willeq.db not found, skipping";
    const SpellData* spell = db.getSpell(5);
    ASSERT_NE(spell, nullptr);
    EXPECT_EQ(spell->name, "Cloak");
    for (size_t i = 0; i < spell->reagent_id.size(); ++i) {
        EXPECT_EQ(spell->reagent_id[i], 0u) << "slot " << i;
        EXPECT_EQ(spell->reagent_count[i], 0u) << "slot " << i;
    }
    EXPECT_FALSE(spell->requiresReagents());
}

// Immolate (78): component 10307 listed with count 0 AND as no-expend
// 10307. Count 0 is carried as 0 (not consumed, so requiresReagents() is
// false and checkReagents() will not refuse on it); the no-expend id is
// carried as plumbing.
TEST_F(SpellDatabaseTest, ZeroCountComponentAndNoexpendCarriedVerbatim) {
    if (!hasDb()) GTEST_SKIP() << "willeq.db not found, skipping";
    const SpellData* spell = db.getSpell(78);
    ASSERT_NE(spell, nullptr);
    EXPECT_EQ(spell->name, "Immolate");
    EXPECT_EQ(spell->reagent_id[0], 10307u);
    EXPECT_EQ(spell->reagent_count[0], 0u);
    EXPECT_EQ(spell->noexpend_reagent_id[0], 10307u);
    EXPECT_FALSE(spell->requiresReagents());
}

// T355: endurance cost (spells_us.txt field 166) reaches SpellData. Before
// T355 willeq.db had no endurance_cost column at all, so
// SpellData::endurance_cost was left at its 0 default for every spell and
// the field had zero assigning sites (RED: all three EXPECT_EQ below read 0).
//
// The column index is not a guess. eqemu imports spells_us.txt into
// `spells_new` POSITIONALLY -- /mnt/projects/akk-stack/code/client_files/
// import/main.cpp ImportSpells() splits the caret line and writes split[i]
// into column i -- and `EndurCost` is column 166 of that table
// (/mnt/projects/akk-stack/code/common/repositories/base/
// base_spells_new_repository.h, which reads it back as row[166]). These three
// ids were SELECTed out of a live peq `spells_new` (EndurCost > 0) and the
// same numbers were then read out of field 166 of the text file.
TEST_F(SpellDatabaseTest, EnduranceCostRoundTripsForMeasuredDisciplines) {
    if (!hasDb()) GTEST_SKIP() << "willeq.db not found, skipping";

    struct Row { uint32_t id; const char* name; int32_t endurance; };
    const Row kMeasured[] = {
        {4520, "Unholy Aura Discipline", 900},
        {4608, "Provoke",                 85},
        {4614, "Phantom Zephyr",         125},
    };

    for (const Row& r : kMeasured) {
        const SpellData* spell = db.getSpell(r.id);
        ASSERT_NE(spell, nullptr) << "spell " << r.id;
        EXPECT_EQ(spell->name, r.name) << "spell " << r.id;
        EXPECT_EQ(spell->endurance_cost, r.endurance) << "spell " << r.id;
        // All three are disciplines: no mana, endurance only.
        EXPECT_EQ(spell->mana_cost, 0) << "spell " << r.id;
    }
}

// T384 (board B163): target_anim + travel_type reach SpellData.
//
// spells_us.txt fields 121 and 122, the same ordinals eqemu's positional
// import puts at spells_new.TargetAnim / .TravelType
// (Columns()[121]/[122] in
// /mnt/projects/akk-stack/code/common/repositories/base/base_spells_new_repository.h).
// Values re-measured from the shipped spells_us.txt with
// scripts/refactor/t380_spell_visual_census.py; the three spells carry three
// DISTINCT target_anim values so a stuck-at-one-value read fails.
TEST_F(SpellDatabaseTest, SpellVisualColumnsRoundTripForMeasuredSpells) {
    if (!hasDb()) GTEST_SKIP() << "willeq.db not found, skipping";

    struct Row { uint32_t id; const char* name; uint16_t targetAnim; uint8_t travelType; };
    const Row kMeasured[] = {
        // The projectile: travel_type 3 is the 85-spell population, and Frost
        // Bolt is the one T381 measured through the visual vocabulary.
        {  54, "Frost Bolt",  13, 3},
        { 202, "Courage",     27, 0},
        {  50, "Summon Food", 43, 0},
    };

    for (const Row& r : kMeasured) {
        const SpellData* spell = db.getSpell(r.id);
        ASSERT_NE(spell, nullptr) << "spell " << r.id;
        EXPECT_EQ(spell->name, r.name) << "spell " << r.id;
        EXPECT_EQ(spell->target_anim, r.targetAnim) << "spell " << r.id;
        EXPECT_EQ(spell->travel_type, r.travelType) << "spell " << r.id;
        // cast_anim was already bound before T384 and must not have shifted.
        EXPECT_NE(spell->cast_anim, 0) << "spell " << r.id;
    }
}

// The control: 4,458 of 8,066 spells have target_anim 0 and 7,981 have
// travel_type 0, so the new columns must not read non-zero for everything.
TEST_F(SpellDatabaseTest, SpellVisualColumnsAreZeroForANonProjectileSpell) {
    if (!hasDb()) GTEST_SKIP() << "willeq.db not found, skipping";
    const SpellData* spell = db.getSpell(15);
    ASSERT_NE(spell, nullptr);
    EXPECT_EQ(spell->name, "Greater Healing");
    EXPECT_EQ(spell->target_anim, 0);
    EXPECT_EQ(spell->travel_type, 0);
}

// The control: a Classic spell costs no endurance, so the new column must
// not turn every spell into a discipline.
TEST_F(SpellDatabaseTest, EnduranceCostIsZeroForAClassicSpell) {
    if (!hasDb()) GTEST_SKIP() << "willeq.db not found, skipping";
    const SpellData* spell = db.getSpell(5);
    ASSERT_NE(spell, nullptr);
    EXPECT_EQ(spell->name, "Cloak");
    EXPECT_EQ(spell->endurance_cost, 0);
}
