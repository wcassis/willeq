-- UIGAP-live (#214): train_skill. Testchar (Warrior 20) beside Brin
-- Stolunger, the Warrior guildmaster in South Qeynos (-587, 81, 31.75), with
-- 1H Blunt (skill 0) at 1 -- free to train (cost 0 below 11) -- and training
-- points to spend. The skill row and the points are NOT in eqemu_snapshot's
-- columns: the UIGAP-live unit restores its whole database from a dump taken
-- before it started; train_skill.post.sh asserts the trained row.
SET @tc = (SELECT id FROM character_data WHERE name = 'Testchar');

UPDATE character_data SET zone_id = 1, zone_instance = 0, x = -580, y = 81, z = 31.75, heading = 0,
  class = 1, points = 160 WHERE id = @tc;
REPLACE INTO character_skills (id, skill_id, value) VALUES (@tc, 0, 1);
