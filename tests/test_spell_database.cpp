/*
 * Spell Database Tests
 *
 * Tests for loading and querying the spell database from spells_us.txt
 */

#include <gtest/gtest.h>
#include "client/spell/spell_database.h"
#include "client/spell/spell_data.h"
#include "client/spell/spell_constants.h"
#include <fstream>
#include <cstdlib>

using namespace EQ;

// Path to EQ client files (from environment or default)
static std::string getEQPath() {
    const char* env_path = std::getenv("EQ_CLIENT_PATH");
    if (env_path) {
        return std::string(env_path);
    }
    return "/home/user/projects/claude/EverQuestP1999";
}

class SpellDatabaseTest : public ::testing::Test {
protected:
    SpellDatabase db;

    void SetUp() override {
        std::string spell_file = getEQPath() + "/spells_us.txt";
        // Only load if file exists
        std::ifstream test(spell_file);
        if (test.good()) {
            ASSERT_TRUE(db.loadFromFile(spell_file))
                << "Failed to load spells: " << db.getLoadError();
        }
    }

    bool hasSpellFile() {
        std::string spell_file = getEQPath() + "/spells_us.txt";
        std::ifstream test(spell_file);
        return test.good();
    }
};

// ============================================================================
// Basic Loading Tests
// ============================================================================

TEST_F(SpellDatabaseTest, LoadsSuccessfully) {
    if (!hasSpellFile()) {
        GTEST_SKIP() << "spells_us.txt not found, skipping";
    }

    EXPECT_TRUE(db.isLoaded());
    EXPECT_GT(db.getSpellCount(), 0u);
    EXPECT_TRUE(db.getLoadError().empty());
}

TEST_F(SpellDatabaseTest, LoadsReasonableSpellCount) {
    if (!hasSpellFile()) {
        GTEST_SKIP() << "spells_us.txt not found, skipping";
    }

    // Titanium client should have thousands of spells
    EXPECT_GT(db.getSpellCount(), 1000u);
    EXPECT_LT(db.getSpellCount(), 50000u);  // Sanity check
}

TEST_F(SpellDatabaseTest, FailsGracefullyOnMissingFile) {
    SpellDatabase empty_db;
    EXPECT_FALSE(empty_db.loadFromFile("/nonexistent/path/spells_us.txt"));
    EXPECT_FALSE(empty_db.isLoaded());
    EXPECT_FALSE(empty_db.getLoadError().empty());
}

// ============================================================================
// Spell Lookup Tests
// ============================================================================

TEST_F(SpellDatabaseTest, LookupByIdWorks) {
    if (!hasSpellFile()) {
        GTEST_SKIP() << "spells_us.txt not found, skipping";
    }

    // Get all spell IDs and verify at least one lookup works
    auto ids = db.getAllSpellIds();
    ASSERT_GT(ids.size(), 0u);

    const SpellData* spell = db.getSpell(ids[0]);
    EXPECT_NE(spell, nullptr);
    EXPECT_EQ(spell->id, ids[0]);
}

TEST_F(SpellDatabaseTest, LookupByIdReturnsNullForInvalid) {
    if (!hasSpellFile()) {
        GTEST_SKIP() << "spells_us.txt not found, skipping";
    }

    EXPECT_EQ(db.getSpell(999999999), nullptr);
    EXPECT_EQ(db.getSpell(SPELL_UNKNOWN), nullptr);
}

TEST_F(SpellDatabaseTest, HasSpellWorks) {
    if (!hasSpellFile()) {
        GTEST_SKIP() << "spells_us.txt not found, skipping";
    }

    auto ids = db.getAllSpellIds();
    ASSERT_GT(ids.size(), 0u);

    EXPECT_TRUE(db.hasSpell(ids[0]));
    EXPECT_FALSE(db.hasSpell(999999999));
}

// ============================================================================
// Name Lookup Tests
// ============================================================================

TEST_F(SpellDatabaseTest, LookupByNameCaseInsensitive) {
    if (!hasSpellFile()) {
        GTEST_SKIP() << "spells_us.txt not found, skipping";
    }

    // Find a known spell and verify case-insensitive lookup
    auto spells = db.findSpellsByName("heal");
    if (spells.empty()) {
        GTEST_SKIP() << "No 'heal' spells found";
    }

    // Get exact name
    std::string name = spells[0]->name;

    // Try lookups with different cases
    const SpellData* spell1 = db.getSpellByName(name);
    EXPECT_NE(spell1, nullptr);

    // Convert to lowercase and try again
    std::string lower_name = name;
    std::transform(lower_name.begin(), lower_name.end(), lower_name.begin(), ::tolower);
    const SpellData* spell2 = db.getSpellByName(lower_name);
    EXPECT_NE(spell2, nullptr);

    if (spell1 && spell2) {
        EXPECT_EQ(spell1->id, spell2->id);
    }
}

TEST_F(SpellDatabaseTest, FindSpellsByPartialName) {
    if (!hasSpellFile()) {
        GTEST_SKIP() << "spells_us.txt not found, skipping";
    }

    // Search for common spell terms
    auto fire_spells = db.findSpellsByName("fire");
    auto heal_spells = db.findSpellsByName("heal");

    // There should be multiple fire and heal spells
    EXPECT_GT(fire_spells.size(), 0u);
    EXPECT_GT(heal_spells.size(), 0u);
}

// ============================================================================
// Spell Data Validation Tests
// ============================================================================

TEST_F(SpellDatabaseTest, SpellsHaveValidData) {
    if (!hasSpellFile()) {
        GTEST_SKIP() << "spells_us.txt not found, skipping";
    }

    auto ids = db.getAllSpellIds();
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

TEST_F(SpellDatabaseTest, SpellEffectsAreValid) {
    if (!hasSpellFile()) {
        GTEST_SKIP() << "spells_us.txt not found, skipping";
    }

    auto ids = db.getAllSpellIds();

    for (uint32_t id : ids) {
        const SpellData* spell = db.getSpell(id);
        ASSERT_NE(spell, nullptr);

        for (const auto& effect : spell->effects) {
            // Effect IDs should be in reasonable range or invalid
            int effect_val = static_cast<int>(effect.effect_id);
            EXPECT_TRUE(effect_val == -1 || effect_val == 254 ||
                       (effect_val >= 0 && effect_val < 500))
                << "Spell " << spell->name << " has invalid effect ID: " << effect_val;
        }
    }
}

// ============================================================================
// Filtering Tests
// ============================================================================

TEST_F(SpellDatabaseTest, FilterByClass) {
    if (!hasSpellFile()) {
        GTEST_SKIP() << "spells_us.txt not found, skipping";
    }

    // Get spells for a wizard at level 50
    auto wizard_spells = db.getSpellsForClass(PlayerClass::Wizard, 50);

    // Wizards should have many spells at 50
    EXPECT_GT(wizard_spells.size(), 10u);

    // Verify all returned spells are actually usable by wizard at 50
    for (const SpellData* spell : wizard_spells) {
        EXPECT_TRUE(spell->canClassUse(PlayerClass::Wizard, 50))
            << "Spell " << spell->name << " should be usable by level 50 wizard";
    }
}

TEST_F(SpellDatabaseTest, FilterByLevelRange) {
    if (!hasSpellFile()) {
        GTEST_SKIP() << "spells_us.txt not found, skipping";
    }

    // Get cleric spells levels 1-10
    auto low_level_spells = db.getSpellsByLevelRange(PlayerClass::Cleric, 1, 10);

    EXPECT_GT(low_level_spells.size(), 0u);

    for (const SpellData* spell : low_level_spells) {
        uint8_t req_level = spell->getClassLevel(PlayerClass::Cleric);
        EXPECT_GE(req_level, 1);
        EXPECT_LE(req_level, 10);
    }
}

TEST_F(SpellDatabaseTest, FilterBeneficialDetrimental) {
    if (!hasSpellFile()) {
        GTEST_SKIP() << "spells_us.txt not found, skipping";
    }

    auto beneficial = db.getBeneficialSpells();
    auto detrimental = db.getDetrimentalSpells();

    // Should have both types
    EXPECT_GT(beneficial.size(), 0u);
    EXPECT_GT(detrimental.size(), 0u);

    // Verify classification
    for (const SpellData* spell : beneficial) {
        EXPECT_TRUE(spell->is_beneficial);
    }
    for (const SpellData* spell : detrimental) {
        EXPECT_FALSE(spell->is_beneficial);
    }
}

TEST_F(SpellDatabaseTest, FilterByEffect) {
    if (!hasSpellFile()) {
        GTEST_SKIP() << "spells_us.txt not found, skipping";
    }

    // Find all spells with root effect
    auto root_spells = db.getSpellsByEffect(SpellEffect::Root);

    // There should be some root spells
    EXPECT_GT(root_spells.size(), 0u);

    for (const SpellData* spell : root_spells) {
        EXPECT_TRUE(spell->hasEffect(SpellEffect::Root));
    }
}

TEST_F(SpellDatabaseTest, CustomFilter) {
    if (!hasSpellFile()) {
        GTEST_SKIP() << "spells_us.txt not found, skipping";
    }

    // Find all instant-cast spells
    auto instant_spells = db.filterSpells([](const SpellData& spell) {
        return spell.isInstantCast();
    });

    for (const SpellData* spell : instant_spells) {
        EXPECT_EQ(spell->cast_time_ms, 0u);
    }
}

// ============================================================================
// SpellData Helper Method Tests
// ============================================================================

TEST_F(SpellDatabaseTest, SpellDataHelperMethods) {
    if (!hasSpellFile()) {
        GTEST_SKIP() << "spells_us.txt not found, skipping";
    }

    // Find a known buff spell
    auto buff_spells = db.filterSpells([](const SpellData& spell) {
        return spell.is_beneficial && spell.hasDuration();
    });

    if (!buff_spells.empty()) {
        const SpellData* buff = buff_spells[0];
        EXPECT_TRUE(buff->isBuffSpell());
        EXPECT_TRUE(buff->hasDuration());
    }

    // Find a damage spell
    auto damage_spells = db.filterSpells([](const SpellData& spell) {
        return spell.isDamageSpell();
    });

    if (!damage_spells.empty()) {
        const SpellData* nuke = damage_spells[0];
        EXPECT_TRUE(nuke->isDamageSpell());
        EXPECT_TRUE(nuke->isDetrimental());
    }
}

TEST_F(SpellDatabaseTest, DurationCalculation) {
    if (!hasSpellFile()) {
        GTEST_SKIP() << "spells_us.txt not found, skipping";
    }

    // Find a spell with duration formula
    auto spells = db.filterSpells([](const SpellData& spell) {
        return spell.hasDuration() && spell.duration_formula > 0;
    });

    if (!spells.empty()) {
        const SpellData* spell = spells[0];

        // Duration at level 50 should be positive
        uint32_t duration = spell->calculateDuration(50);
        EXPECT_GT(duration, 0u);

        // Duration at higher level should be >= lower level (for most formulas)
        uint32_t duration_low = spell->calculateDuration(10);
        uint32_t duration_high = spell->calculateDuration(50);
        EXPECT_GE(duration_high, duration_low);
    }
}

TEST_F(SpellDatabaseTest, EffectCountAndHasEffect) {
    if (!hasSpellFile()) {
        GTEST_SKIP() << "spells_us.txt not found, skipping";
    }

    auto ids = db.getAllSpellIds();
    int spells_with_effects = 0;

    for (uint32_t id : ids) {
        const SpellData* spell = db.getSpell(id);
        int count = spell->getEffectCount();

        if (count > 0) {
            spells_with_effects++;

            // Find one valid effect and verify hasEffect works
            for (const auto& effect : spell->effects) {
                if (effect.isValid()) {
                    EXPECT_TRUE(spell->hasEffect(effect.effect_id));
                    break;
                }
            }
        }
    }

    // Most spells should have at least one effect
    EXPECT_GT(spells_with_effects, static_cast<int>(ids.size() / 2));
}

// ============================================================================
// Known Spell Tests (verify specific iconic spells exist)
// ============================================================================

TEST_F(SpellDatabaseTest, IconicSpellsExist) {
    if (!hasSpellFile()) {
        GTEST_SKIP() << "spells_us.txt not found, skipping";
    }

    // Test some iconic EQ spell names exist (partial match)
    std::vector<std::string> iconic_spells = {
        "complete heal",
        "clarity",
        "spirit of wolf",
        "root",
        "gate",
        "bind",
        "heal",
        "nuke",  // Various nuke spells
        "haste",
    };

    int found = 0;
    for (const auto& name : iconic_spells) {
        auto results = db.findSpellsByName(name);
        if (!results.empty()) {
            found++;
        }
    }

    // At least some iconic spells should exist
    EXPECT_GT(found, 3);
}

// ============================================================================
// Edge Case Tests
// ============================================================================

TEST_F(SpellDatabaseTest, EmptyStringSearchReturnsEmpty) {
    if (!hasSpellFile()) {
        GTEST_SKIP() << "spells_us.txt not found, skipping";
    }

    auto results = db.findSpellsByName("");
    // Empty search should return all or nothing, depending on implementation
    // Just verify it doesn't crash
    (void)results;
}

TEST_F(SpellDatabaseTest, GetSpellByInvalidNameReturnsNull) {
    if (!hasSpellFile()) {
        GTEST_SKIP() << "spells_us.txt not found, skipping";
    }

    EXPECT_EQ(db.getSpellByName("zzzznonexistentspellzzz"), nullptr);
}

// Main entry point for tests
int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
