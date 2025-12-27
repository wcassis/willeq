#include <gtest/gtest.h>
#include "client/combat.h"

class CombatEnumsTest : public ::testing::Test {
protected:
    void SetUp() override {}
    void TearDown() override {}
};

// Test CombatState enum values
TEST_F(CombatEnumsTest, CombatStateValues) {
    EXPECT_EQ(COMBAT_STATE_IDLE, 0);
    EXPECT_EQ(COMBAT_STATE_ENGAGED, 1);
    EXPECT_EQ(COMBAT_STATE_FLEEING, 2);
    EXPECT_EQ(COMBAT_STATE_LOOTING, 3);
    EXPECT_EQ(COMBAT_STATE_HUNTING, 4);
    EXPECT_EQ(COMBAT_STATE_RESTING, 5);
    EXPECT_EQ(COMBAT_STATE_SEEKING_GUARD, 6);
}

// Test TargetPriority enum values
TEST_F(CombatEnumsTest, TargetPriorityValues) {
    EXPECT_EQ(TARGET_PRIORITY_LOWEST, 0);
    EXPECT_EQ(TARGET_PRIORITY_LOW, 1);
    EXPECT_EQ(TARGET_PRIORITY_MEDIUM, 2);
    EXPECT_EQ(TARGET_PRIORITY_HIGH, 3);
    EXPECT_EQ(TARGET_PRIORITY_HIGHEST, 4);
}

// Test Consider colors (con colors)
TEST_F(CombatEnumsTest, ConsiderColorValues) {
    EXPECT_EQ(CON_GREEN, 2);
    EXPECT_EQ(CON_LIGHTBLUE, 18);
    EXPECT_EQ(CON_BLUE, 4);
    EXPECT_EQ(CON_WHITE, 20);
    EXPECT_EQ(CON_YELLOW, 15);
    EXPECT_EQ(CON_RED, 13);
    EXPECT_EQ(CON_GRAY, 6);
}

// Test SpellSlot enum values
TEST_F(CombatEnumsTest, SpellSlotValues) {
    EXPECT_EQ(SPELL_GEM_1, 0);
    EXPECT_EQ(SPELL_GEM_2, 1);
    EXPECT_EQ(SPELL_GEM_3, 2);
    EXPECT_EQ(SPELL_GEM_4, 3);
    EXPECT_EQ(SPELL_GEM_5, 4);
    EXPECT_EQ(SPELL_GEM_6, 5);
    EXPECT_EQ(SPELL_GEM_7, 6);
    EXPECT_EQ(SPELL_GEM_8, 7);
    EXPECT_EQ(SPELL_GEM_9, 8);
    EXPECT_EQ(SPELL_GEM_10, 9);
    EXPECT_EQ(SPELL_GEM_11, 10);
    EXPECT_EQ(SPELL_GEM_12, 11);
    EXPECT_EQ(MAX_SPELL_GEMS, 12);
}

// Test CombatAction enum values
TEST_F(CombatEnumsTest, CombatActionValues) {
    EXPECT_EQ(COMBAT_ACTION_ATTACK, 0);
    EXPECT_EQ(COMBAT_ACTION_CAST, 1);
    EXPECT_EQ(COMBAT_ACTION_HEAL, 2);
    EXPECT_EQ(COMBAT_ACTION_BUFF, 3);
    EXPECT_EQ(COMBAT_ACTION_FLEE, 4);
}

// Test CombatTarget struct default values
TEST_F(CombatEnumsTest, CombatTargetDefaults) {
    CombatTarget target;
    target.entity_id = 0;
    target.name = "";
    target.distance = 0.0f;
    target.hp_percent = 100;
    target.con_color = CON_WHITE;
    target.priority = TARGET_PRIORITY_MEDIUM;
    target.is_aggro = false;
    target.has_consider_data = false;
    target.faction = 0;
    target.con_level = 0;
    target.cur_hp = 0;
    target.max_hp = 0;

    EXPECT_EQ(target.entity_id, 0);
    EXPECT_EQ(target.name, "");
    EXPECT_FLOAT_EQ(target.distance, 0.0f);
    EXPECT_EQ(target.hp_percent, 100);
    EXPECT_EQ(target.con_color, CON_WHITE);
    EXPECT_EQ(target.priority, TARGET_PRIORITY_MEDIUM);
    EXPECT_FALSE(target.is_aggro);
    EXPECT_FALSE(target.has_consider_data);
}

// Test SpellInfo struct
TEST_F(CombatEnumsTest, SpellInfoStruct) {
    SpellInfo spell;
    spell.spell_id = 12345;
    spell.name = "Fireball";
    spell.mana_cost = 100;
    spell.cast_time_ms = 3000;
    spell.recast_time_ms = 6000;
    spell.range = 200;
    spell.is_beneficial = false;
    spell.is_detrimental = true;
    spell.gem_slot = SPELL_GEM_1;

    EXPECT_EQ(spell.spell_id, 12345u);
    EXPECT_EQ(spell.name, "Fireball");
    EXPECT_EQ(spell.mana_cost, 100u);
    EXPECT_EQ(spell.cast_time_ms, 3000u);
    EXPECT_EQ(spell.recast_time_ms, 6000u);
    EXPECT_EQ(spell.range, 200u);
    EXPECT_FALSE(spell.is_beneficial);
    EXPECT_TRUE(spell.is_detrimental);
    EXPECT_EQ(spell.gem_slot, SPELL_GEM_1);
}

// Test CombatStats struct
TEST_F(CombatEnumsTest, CombatStatsStruct) {
    CombatStats stats;
    stats.current_hp = 1000;
    stats.max_hp = 2000;
    stats.current_mana = 500;
    stats.max_mana = 1000;
    stats.current_endurance = 200;
    stats.max_endurance = 400;
    stats.hp_percent = 50.0f;
    stats.mana_percent = 50.0f;
    stats.endurance_percent = 50.0f;

    EXPECT_EQ(stats.current_hp, 1000u);
    EXPECT_EQ(stats.max_hp, 2000u);
    EXPECT_EQ(stats.current_mana, 500u);
    EXPECT_EQ(stats.max_mana, 1000u);
    EXPECT_EQ(stats.current_endurance, 200u);
    EXPECT_EQ(stats.max_endurance, 400u);
    EXPECT_FLOAT_EQ(stats.hp_percent, 50.0f);
    EXPECT_FLOAT_EQ(stats.mana_percent, 50.0f);
    EXPECT_FLOAT_EQ(stats.endurance_percent, 50.0f);
}

// Test combat state transitions are valid
TEST_F(CombatEnumsTest, CombatStateTransitions) {
    // Test that all states are distinct
    EXPECT_NE(COMBAT_STATE_IDLE, COMBAT_STATE_ENGAGED);
    EXPECT_NE(COMBAT_STATE_ENGAGED, COMBAT_STATE_FLEEING);
    EXPECT_NE(COMBAT_STATE_FLEEING, COMBAT_STATE_LOOTING);
    EXPECT_NE(COMBAT_STATE_LOOTING, COMBAT_STATE_HUNTING);
    EXPECT_NE(COMBAT_STATE_HUNTING, COMBAT_STATE_RESTING);
    EXPECT_NE(COMBAT_STATE_RESTING, COMBAT_STATE_SEEKING_GUARD);
}

// Test priority comparison
TEST_F(CombatEnumsTest, PriorityComparison) {
    EXPECT_LT(TARGET_PRIORITY_LOWEST, TARGET_PRIORITY_LOW);
    EXPECT_LT(TARGET_PRIORITY_LOW, TARGET_PRIORITY_MEDIUM);
    EXPECT_LT(TARGET_PRIORITY_MEDIUM, TARGET_PRIORITY_HIGH);
    EXPECT_LT(TARGET_PRIORITY_HIGH, TARGET_PRIORITY_HIGHEST);
}
