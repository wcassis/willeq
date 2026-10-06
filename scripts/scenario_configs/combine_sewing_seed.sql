-- UIGAP-live (#215): combine_sewing. Run with --inventory-chars "<Testchar id>".
--
-- Recipes (tradeskill_recipe on this server):
--   3103 Ruined Wolf Pelt (Tailoring): Low Quality Wolf Skin 13753 x1 +
--        Skinning Knife 21565 x1 (returned) -> 13782, in a sewing kit.
--        The oracle's own successful combine (215 combine.tsv row 4).
--   3532 A Ruby (Blacksmithing, nofail): 6027 x1 + 12900 x1 -> 12896, in a
--        forge (container type 17).
-- Testchar is GM (character_data.gm = 1), which this server treats as a sure
-- success (the oracle's Testchar too).
SET @tc = (SELECT id FROM character_data WHERE name = 'Testchar');

DELETE FROM inventory WHERE character_id = @tc;
INSERT INTO inventory (character_id, slot_id, item_id, charges) VALUES
  (@tc, 23, 17165, 0),    -- Collapsible Sewing Kit (8 slots: 4010..4017)
  (@tc, 24, 13753, 1),    -- Low Quality Wolf Skin, ONE (a stack is refused, row 7)
  (@tc, 25, 21565, 1),    -- Skinning Knife
  (@tc, 26, 6027, 1),     -- A Ruby, part 1
  (@tc, 27, 12900, 1);    -- A Ruby, part 2

-- Beside the South Qeynos forge (object 201 at -296, 179).
UPDATE character_data SET zone_id = 1, zone_instance = 0, x = -300, y = 185, z = 4, heading = 0,
  gm = 1 WHERE id = @tc;
