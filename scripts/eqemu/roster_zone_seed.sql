-- Lane seed: keep the test roster OUT of timorous (B1047 #367, B1025-H5 #345).
--
-- The lanes were cloned from one image whose character_data stores Testcaster
-- in zone 96 = timorous. timorous never finishes loading on the lanes: world
-- boot-loops a static timorous every ~65 s and an instance with a client in it
-- goes silent after zone-in (B1047). A willeq client stalls at
-- [LOADING] Phase 9/15 and scenario.sh reports could-not-start (exit 3); the
-- B1025-H5 hotbar_lifecycle run of 2026-10-16 08:47Z hit exactly that while
-- its peer Testbuddy, stored in befallen, zoned in within 3 s in the SAME run.
--
-- This moves any roster character still STORED in timorous to befallen's own
-- safe point (the zone table's safe_x/safe_y/safe_z, the same spawn
-- scripts/eqemu/zone_place_lib.sh seed_char uses for the headless sweeps).
-- zone_id is the zone's zoneidnumber, never the zone table's PK (the column
-- trap documented in zone_place_lib.sh). It is idempotent and touches no other
-- row: a character stored anywhere else is left alone, and a plan or seed that
-- WANTS timorous still sets it per run, after the snapshot.
--
-- Applied BEFORE the snapshot by scripts/scenario.sh and
-- scripts/eqemu/oracle/oracle_session.sh, and by scripts/eqemu/seed.sh, so it
-- is a PERSISTENT lane fix: the per-run restore puts back the post-seed row.
UPDATE character_data SET
    zone_id = (SELECT zoneidnumber FROM zone WHERE short_name = 'befallen' AND version = 0 LIMIT 1),
    x       = (SELECT safe_x       FROM zone WHERE short_name = 'befallen' AND version = 0 LIMIT 1),
    y       = (SELECT safe_y       FROM zone WHERE short_name = 'befallen' AND version = 0 LIMIT 1),
    z       = (SELECT safe_z       FROM zone WHERE short_name = 'befallen' AND version = 0 LIMIT 1),
    heading = 0
WHERE name IN ('Testchar', 'Testmelee', 'Testcaster', 'Testbuddy')
  AND zone_id = (SELECT zoneidnumber FROM zone WHERE short_name = 'timorous' AND version = 0 LIMIT 1);
