-- UIGAP-live2 (#26): bank_change. 9 units from Sorn Copperhold.
SET @tc = (SELECT id FROM character_data WHERE name = 'Testchar');
UPDATE character_currency SET platinum = 0, gold = 25, silver = 37, copper = 123,
  platinum_bank = 3, gold_bank = 14, silver_bank = 9, copper_bank = 50,
  platinum_cursor = 0, gold_cursor = 0, silver_cursor = 0, copper_cursor = 0 WHERE id = @tc;
UPDATE character_data SET zone_id = 1, zone_instance = 0, x = -430, y = 319, z = 3.75, heading = 384
  WHERE id = @tc;
