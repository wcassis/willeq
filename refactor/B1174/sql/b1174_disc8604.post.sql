-- B1174 (#493): post-SQL, runs in cleanup WITH THE WORLD DOWN after the
-- driver's own post-SQL (class, gems, buffs, SAVE_POST). Restores the level.
UPDATE character_data SET `level`=(SELECT lvl FROM _b1174_home_lvl) WHERE id=67;
DROP TABLE IF EXISTS _b1174_home_lvl;
