-- B1162 attempt-2 Leg B (Intimidation): post-SQL, runs in cleanup WITH THE
-- WORLD DOWN, after the driver's own post-SQL (class restore + SAVE_POST skill
-- restore + spellbook cleanup). Restores the character level the pre-SQL
-- changed (the driver restores class/gems/buffs/skills/spellbook but NOT
-- level), so lane before == after.
UPDATE character_data SET `level`=(SELECT lvl FROM _b1162_home_lvl) WHERE id=67;
DROP TABLE IF EXISTS _b1162_home_lvl;
