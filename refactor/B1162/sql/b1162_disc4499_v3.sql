-- B1162 /disc landing leg (orchestrator ruling 2026-10-27: observe the oracle's
-- /disc landing once). Pre-SQL, runs in the lock BEFORE login after the
-- driver's class switch (12 -> 1 Warrior) + SAVE_PRE.
--
-- Why 4499 Defensive Discipline (record 326) and not Leechcurse (record 325):
-- the lane-5 server's spells_new rows (review/B1162/worker/
-- lane5_spells_new_discs_20261027.txt) give SHD a level ONLY on 4504, while the
-- client sends a /disc id only when ITS OWN spells_us.txt row gives the class
-- a level (eqgame.exe.c FUN_004bb2dd, spell+0x150+class == 0xff -> refused),
-- and the client's 4504 has no class level; the client's SHD Leechcurse ids
-- 8604/8617/8642 are Bear Claw / Grappling Roots / Tangleweb on the server
-- (classes all 255 -> "Your class cannot learn from this tome."). No record-325
-- id passes both gates on this lane. 4499 is WAR 55 on BOTH sides and is the
-- spell of B1130 row 75 (record 326, B1134's /disc + GM measurement).
--
-- Cleanup: the driver's SAVE_POST restores character_disciplines + timers (the
-- /disc starts the 4499 reuse timer); b1162_disc325_v2.post.sql restores level.
DROP TABLE IF EXISTS _b1162_home_lvl;
CREATE TABLE _b1162_home_lvl AS SELECT `level` AS lvl FROM character_data WHERE id=67;
UPDATE character_data SET `level`=60 WHERE id=67;
DELETE FROM character_disciplines WHERE id=67;
INSERT INTO character_disciplines (id, slot_id, disc_id) VALUES (67, 0, 4499);
