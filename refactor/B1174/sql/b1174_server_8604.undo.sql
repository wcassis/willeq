-- B1174 (#493) server-data undo, run by oracle_session.sh's cleanup on every
-- exit path once the change was armed, with the world down. Puts the saved
-- original row 8604 back whole; the helper table is dropped only if that
-- succeeded (mysql stops at the first error). The driver then requires
-- b1174_server_8604.dump.sql's output to equal the one taken before the change.
REPLACE INTO spells_new SELECT * FROM _b1174_spells_new_8604;
DROP TABLE _b1174_spells_new_8604;
