-- UIGAP-live2 (#212): trade_roundtrip + trade_peer.
-- Run with --inventory-chars "<Testchar id>,<Testbuddy id>" so the rows this
-- replaces are snapshotted and put back. inventory.slot_id numbering.
SET @tc = (SELECT id FROM character_data WHERE name = 'Testchar');
SET @tb = (SELECT id FROM character_data WHERE name = 'Testbuddy');
DELETE FROM inventory WHERE character_id IN (@tc, @tb);
INSERT INTO inventory (character_id, slot_id, item_id, charges) VALUES
  (@tc, 23, 17005, 0),    -- Testchar: Backpack
  (@tc, 24, 7007, 1),     --           Rusty Dagger: traded to the peer
  (@tc, 25, 16598, 1),    --           Bottle: offered, then the trade is cancelled
  (@tb, 23, 17005, 0);    -- Testbuddy: Backpack, empty
UPDATE character_currency SET platinum = 10, gold = 0, silver = 0, copper = 0,
  platinum_cursor = 0, gold_cursor = 0, silver_cursor = 0, copper_cursor = 0 WHERE id = @tc;
UPDATE character_currency SET platinum = 0, gold = 0, silver = 0, copper = 0,
  platinum_cursor = 0, gold_cursor = 0, silver_cursor = 0, copper_cursor = 0 WHERE id = @tb;
-- Side by side in South Qeynos, outside the bank.
UPDATE character_data SET zone_id = 1, zone_instance = 0, x = -420, y = 319, z = 3.75, heading = 0
  WHERE id = @tc;
UPDATE character_data SET zone_id = 1, zone_instance = 0, x = -420, y = 325, z = 3.75, heading = 0
  WHERE id = @tb;
