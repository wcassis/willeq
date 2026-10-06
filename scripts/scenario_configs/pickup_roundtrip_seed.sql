-- UIGAP-live (#151): pickup_roundtrip and the click_equip pair.
--
-- Run with --inventory-chars "<Testchar id>,<Testbuddy id>" so the inventory
-- rows this replaces are snapshotted first and put back by the restore.
--
-- Slot numbers are `inventory.slot_id` (RoF2 numbering): general 23..30,
-- cursor 33, general 23's bag contents 4010..4019, bank 2000..
SET @tc = (SELECT id FROM character_data WHERE name = 'Testchar');
SET @tb = (SELECT id FROM character_data WHERE name = 'Testbuddy');

DELETE FROM inventory WHERE character_id IN (@tc, @tb);
INSERT INTO inventory (character_id, slot_id, item_id, charges) VALUES
  (@tc, 23, 17005, 0),    -- Testchar: Backpack (8 slots), empty
  (@tc, 24, 7007, 1),     --           Rusty Dagger: the item that travels
  (@tb, 23, 17005, 0),    -- Testbuddy: Backpack ...
  (@tb, 4010, 7007, 1);   --            ... holding a Rusty Dagger (Magician-usable); primary EMPTY

-- Both beside Sorn Copperhold, the South Qeynos banker (-439, 319): eqemu
-- KICKS a client that moves an item into a bank slot with no banker within
-- 200 units (zone/inventory.cpp SwapItem, "Inventory desync").
UPDATE character_data SET zone_id = 1, zone_instance = 0, x = -420, y = 319, z = 3.75, heading = 0
  WHERE id IN (@tc, @tb);
