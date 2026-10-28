-- B1174 (#493) server-data change, run by oracle_session.sh with the stack DOWN
-- (only mariadb up) before up.sh, so the zone's spell table (shared memory,
-- built at boot) holds it. The lane's spells_new row 8604 is "Bear Claw"
-- (targettype 5, detrimental, every class 255), while the client's 8604 is
-- Leechcurse Discipline (SHD 60, field 145 = 325). This points row 8604 at the
-- lane's OWN Leechcurse Discipline row 4504 (SHD 60, targettype 6 self, cast 0):
-- every column of 4504 copied onto 8604, id kept. The original row is kept
-- whole in _b1174_spells_new_8604 for the undo (b1174_server_8604.undo.sql).
DROP TABLE IF EXISTS _b1174_spells_new_8604;
CREATE TABLE _b1174_spells_new_8604 AS SELECT * FROM spells_new WHERE id=8604;
CREATE TEMPORARY TABLE _b1174_src AS SELECT * FROM spells_new WHERE id=4504;
UPDATE _b1174_src SET id=8604;
REPLACE INTO spells_new SELECT * FROM _b1174_src;
DROP TEMPORARY TABLE _b1174_src;
