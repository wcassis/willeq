-- B1162 continuation leg (Intimidation, class 9 Rogue): pre-SQL, runs in the
-- lock BEFORE login after the driver's class switch + SAVE_PRE. Level 60 and
-- skill 71 (Intimidation) = 5000 before zone-in, so the zone-in profile carries
-- the skill (attempt 2 proved this delivers 71=5000). No spell 113: attempt 2's
-- "known-113 gate" was a misreading (README "Continuation step 1").
-- The client's skill table gives a Rogue Intimidation from level 22
-- (eqgame.exe .data 0x698b30); the server's InstillDoubt has no skill gate and
-- rolls GetSkill(71) + CHA/4 - (target level*4 + target WIS/4).
-- Cleanup: the driver's SAVE_POST restores character_skills; the post-sql-file
-- (b1162_combat13_v2.post.sql) restores the level.
DROP TABLE IF EXISTS _b1162_home_lvl;
CREATE TABLE _b1162_home_lvl AS SELECT `level` AS lvl FROM character_data WHERE id=67;
UPDATE character_data SET `level`=60 WHERE id=67;
REPLACE INTO character_skills (id, skill_id, value) VALUES (67, 71, 5000);
