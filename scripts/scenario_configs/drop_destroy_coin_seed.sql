-- UIGAP-live2 (#151): drop_destroy_coin. 9 units from Sorn Copperhold (-439 319;
-- fable's interaction range is 15). Run with --inventory-chars "<Testchar id>".
-- The bank coin is not in the lane's snapshot; the unit restores the whole DB.
SET @tc = (SELECT id FROM character_data WHERE name = 'Testchar');
DELETE FROM inventory WHERE character_id = @tc;
INSERT INTO inventory (character_id, slot_id, item_id, charges) VALUES
  (@tc, 23, 17005, 0),    -- Backpack
  (@tc, 24, 7007, 1),     -- Rusty Dagger: destroyed
  (@tc, 25, 16598, 1);    -- Bottle: dropped
UPDATE character_currency SET platinum = 10, gold = 0, silver = 0, copper = 0,
  platinum_bank = 3, gold_bank = 0, silver_bank = 0, copper_bank = 0,
  platinum_cursor = 0, gold_cursor = 0, silver_cursor = 0, copper_cursor = 0 WHERE id = @tc;
UPDATE character_data SET zone_id = 1, zone_instance = 0, x = -430, y = 319, z = 3.75, heading = 384
  WHERE id = @tc;
