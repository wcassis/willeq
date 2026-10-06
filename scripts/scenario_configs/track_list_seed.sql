-- UIGAP-live (#211): track_list. Testchar becomes a Ranger (class 4) with
-- Tracking (skill 53) at 100 -- the oracle used #permaclass 4 + #setskill 53
-- (clamped to 105). Testbuddy is the target, 20 units away.
--
-- class and character_skills are NOT in eqemu_snapshot's columns: the
-- UIGAP-live unit restores its whole database from a dump taken before it
-- started. Run this leg LAST in a session that does not.
SET @tc = (SELECT id FROM character_data WHERE name = 'Testchar');
SET @tb = (SELECT id FROM character_data WHERE name = 'Testbuddy');
UPDATE character_data SET class = 4 WHERE id = @tc;
REPLACE INTO character_skills (id, skill_id, value) VALUES (@tc, 53, 100);
UPDATE character_data SET zone_id = 1, zone_instance = 0, x = -420, y = 319, z = 3.75, heading = 0
  WHERE id = @tc;
UPDATE character_data SET zone_id = 1, zone_instance = 0, x = -400, y = 319, z = 3.75, heading = 0
  WHERE id = @tb;
