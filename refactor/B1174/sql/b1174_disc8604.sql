-- B1174 (#493) record-325 /disc leg: character pre-SQL, runs in the lock BEFORE
-- login, after the driver's class switch (12 -> 5 Shadow Knight) + SAVE_PRE.
-- Level 60 (the client's 8604 row gives SHD level 60) and 8604 as the ONLY
-- discipline, so it is the first prefix match of "Leechcurse Discipline".
-- Cleanup: the driver's SAVE_POST restores character_disciplines + timers;
-- b1174_disc8604.post.sql restores the level.
DROP TABLE IF EXISTS _b1174_home_lvl;
CREATE TABLE _b1174_home_lvl AS SELECT `level` AS lvl FROM character_data WHERE id=67;
UPDATE character_data SET `level`=60 WHERE id=67;
DELETE FROM character_disciplines WHERE id=67;
INSERT INTO character_disciplines (id, slot_id, disc_id) VALUES (67, 0, 8604);
