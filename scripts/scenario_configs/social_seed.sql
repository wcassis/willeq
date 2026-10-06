-- UIGAP-live (#213): friends_who / ignore_tell. Testchar and Testbuddy in the
-- same zone (South Qeynos, by the bank) so the main can wait for its peer.
SET @tc = (SELECT id FROM character_data WHERE name = 'Testchar');
SET @tb = (SELECT id FROM character_data WHERE name = 'Testbuddy');
UPDATE character_data SET zone_id = 1, zone_instance = 0, x = -420, y = 319, z = 3.75, heading = 0
  WHERE id IN (@tc, @tb);
