-- B1162 attempt-2 Leg B (Intimidation, class 1 Warrior): pre-SQL, runs in the
-- lock BEFORE login (after the driver's class switch + SAVE_PRE). Fixes the
-- character state BEFORE the client zones in (ruling 2026-10-26 23:05Z step 2):
-- level 60 + skill 71 (Intimidation) = 5000 + spell 113 (Shock of Spikes).
--
-- Why pre-SQL for the skill: attempt 1's in-world `#set skill 71 5000` was a
-- NO-OP on the wire (the server answered with OP_SkillUpdate skill=71 value=0),
-- so the client never had Intimidation. The skill goes into character_skills
-- before login so the zone-in profile carries skill 71 = 5000.
--
-- spell 113 (Shock of Spikes) goes into character_spells: the decompile's G-key
-- gate-4 is (in-combat) OR (the character's known-spell list contains 113); the
-- zone-in spellbook is built from character_spells, so this satisfies the
-- known-113 half of the gate.
--
-- Cleanup: the driver's SAVE_POST restores character_skills (DELETE WHERE id=67
-- + INSERT <saved originals (0 rows)>), and its spellbook cleanup does
-- DELETE FROM character_spells WHERE id=67 AND spell_id NOT IN (<home 93-book>)
-- so the added 113 is removed. The level is restored by the post-sql-file.
DROP TABLE IF EXISTS _b1162_home_lvl;
CREATE TABLE _b1162_home_lvl AS SELECT `level` AS lvl FROM character_data WHERE id=67;
UPDATE character_data SET `level`=60 WHERE id=67;
REPLACE INTO character_skills (id, skill_id, value) VALUES (67, 71, 5000);
REPLACE INTO character_spells (id, slot_id, spell_id) VALUES (67, 93, 113);
