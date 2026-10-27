-- B1162 attempt-2 Leg A (disc, class 5 SHD): pre-SQL, runs in the lock BEFORE
-- login (after the driver's class switch + SAVE_PRE). Fixes the character state
-- BEFORE the client zones in (ruling 2026-10-26 23:05Z step 2): level 60 + the
-- Leechcurse Discipline rows in character_disciplines.
--
-- 8604 = the SHD-lvl-60 "Leechcurse Discipline" (ruling's /disc-triggerable pick,
--   spells_us.txt class-cap column 60); 4504 = the generic "Leechcurse Discipline"
--   that #traindisc 60 delivered in attempt 1 (hedge on the client's name-resolution
--   target). Both named "Leechcurse Discipline"; /disc Leechcurse Discipline must
--   resolve to one of them.
--
-- Cleanup: the driver's SAVE_POST does DELETE WHERE id=67 + INSERT <saved
-- originals (0 rows) for character_disciplines, so these rows are removed. The
-- level is NOT restored by the driver (it only restores class/gems/buffs), so the
-- post-sql-file restores it from the captured home value.
DROP TABLE IF EXISTS _b1162_home_lvl;
CREATE TABLE _b1162_home_lvl AS SELECT `level` AS lvl FROM character_data WHERE id=67;
UPDATE character_data SET `level`=60 WHERE id=67;
DELETE FROM character_disciplines WHERE id=67;
INSERT INTO character_disciplines (id, slot_id, disc_id) VALUES (67, 0, 8604);
INSERT INTO character_disciplines (id, slot_id, disc_id) VALUES (67, 1, 4504);
