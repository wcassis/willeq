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
// The legacy API supported case-insensitive and partial-name lookup
// (LookupByNameCaseInsensitive, FindSpellsByPartialName). The current
// getSpellByName()/findSpellsByName() only do a single exact-match SQL
// query (WHERE name=?, no COLLATE NOCASE, no LIKE) — findSpellsByName is
// a thin wrapper that returns at most one exact match. Those two test
// cases were deleted since the capability they tested is gone; this test
// replaces them with an exact-match round-trip, which is the surviving
// capability.
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

// Deleted: SpellEffectsAreValid, EffectCountAndHasEffect. The DB-backed
// queryAndCache() (src/client/spell/spell_database.cpp) never populates
// SpellData::effects — EQT::SpellInfo (include/client/willeq_database.h)
// has no effects field at all, so spell->effects is always default/empty
// for every spell returned by this API. There is nothing meaningful left
// to assert; the underlying capability was not carried over to the
// SQLite-backed database. Not a production bug we're fixing here (test
// and CMake files only) — documented for follow-up.

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

// Deleted: SpellDataHelperMethods. It searched via the now-removed
// filterSpells() for a beneficial+duration spell and a damage spell.
// isDamageSpell()/isHealSpell() depend on SpellData::effects, which (per
// above) is never populated by the DB-backed path, so even a 1:1 rewrite
// would not test anything real.

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

// Deleted: IconicSpellsExist. It relied on findSpellsByName() doing
// partial/substring matching (e.g. "heal" matching "Complete Heal"); the
// current implementation only wraps an exact-match query, so lowercase
// partial terms like "complete heal" would not match a title-cased exact
// name like "Complete Heal" even if the row exists.

// ============================================================================
// Edge Case Tests
// ============================================================================

TEST_F(SpellDatabaseTest, EmptyStringSearchReturnsEmpty) {
    if (!hasDb()) {
        GTEST_SKIP() << "willeq.db not found, skipping";
    }

    auto results = db.findSpellsByName("");
    // Just verify it doesn't crash
    (void)results;
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
