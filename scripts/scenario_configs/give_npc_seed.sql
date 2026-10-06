-- UIGAP-live2 (#212): give_npc. Captain Tillin (South Qeynos, -512 37 31.75)
-- takes one Gnoll Fang and pays a Moonstone (quests/qeynos/Captain_Tillin.lua).
-- Run with --inventory-chars "<Testchar id>".
SET @tc = (SELECT id FROM character_data WHERE name = 'Testchar');
DELETE FROM inventory WHERE character_id = @tc;
INSERT INTO inventory (character_id, slot_id, item_id, charges) VALUES
  (@tc, 23, 17005, 0),    -- Backpack
  (@tc, 26, 13915, 1);    -- Gnoll Fang
UPDATE character_data SET zone_id = 1, zone_instance = 0, x = -505, y = 37, z = 31.75, heading = 384
  WHERE id = @tc;
