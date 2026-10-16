-- B1025-H5 (#345): hotbar_lifecycle. The kit the lane's client cannot seat
-- in-world: Testcaster (level-1 wizard, lane 1) is already fully equipped --
-- primary 13 holds 14337, and the live inventory dump (2026-10-16, this card)
-- shows general 23-30 all filled (9990, 9991, 15288, 15372, 18755, 21779,
-- 32601, +8007 in ammo 22), so #summonitem + /putdown would swap the OLD
-- primary onto the cursor instead of seating the pole. The rows go in by
-- direct INSERT, the way ranged_single_shot_seed.sql does: the server loads
-- the inventory as-is at login, and the client reads the same synced rows.
-- The seed ALSO places the character in befallen (the zone UPDATE below): the
-- stored zone is timorous (96), which never finishes loading headless.
--
-- Slot numbers are the DB's (RoF2): inventory.slot_id IS the user-facing
-- numbering (T494), and the server's invslot (rof2_limits.h, read by
-- forage.cpp) differs from the client's Titanium numbering only after waist:
-- power source 21 (NOT sent to a Titanium client at all), ammo 22, general
-- 1-10 = 23-32, cursor 33 (include/client/inventory/inventory_constants.h:120).
--
--   13  pole 13100 (Fishing Pole, itemtype 36 = ItemTypeFishingPole) in
--       PRIMARY (13 in BOTH numberings, so the server's m_inv[slotPrimary]
--       read, forage.cpp:182, and the client's SLOT_PRIMARY gate,
--       skill_manager.cpp:161, see the same item). Overwrites the live 14337;
--       the restore (scenario.sh's INVENTORY_CHARS wholesale DELETE+INSERT
--       from the pre-seed snapshot) puts it back.
--   20  bait 13101 (Fishing Bait, itemtype 37 = ItemTypeFishingBait) in
--       WAIST -- a WORN slot, so the server's HasItemByUse(Worn|Personal)
--       bait scan (forage.cpp:183) sees it. Overwrites the live 11666;
--       restored likewise.
--   30  8443 Bag of Bone Dust, 10 charges -- the consumable the ITEM slot
--       assigns and activates twice. itemtype 21 (potion), reclevel 0,
--       Click.Effect = spell 86 (Enduring Breath, self-buff, always lands),
--       Click.Level2 = 0 (no level gate). Seeded in GENERAL (DB 30 =
--       Titanium 29, include/client/inventory/inventory_constants.h:120) so
--       the client SEEES it: the server does not send a power-source row
--       (DB 21) to a Titanium client, so a 8443 parked in slot 21 would be
--       invisible to findItemSlotByItemId and the item leg could never
--       activate. The client's UseInventoryItem refuses only a type-5 click
--       effect in a general slot; 8443's is type 1 = Click, so it sends. The
--       server consumes one charge per landed cast (spells.cpp:1737/1742,
--       GetItemSlotToConsumeCharge -> DeleteItemInInventory); the post-check
--       asserts the 10-charge stack DROPPED from 10 (it does not pin 8-vs-9:
--       the key route re-casts the still-active buff and whether that
--       completed cast consumes a charge is not pinned here).
--
-- The client's own forageOrFishingRefusal reads the pole's PARSED skillType
-- (wire field 56, inventory_item_parser.cpp:314), which a direct seed does
-- not guarantee the client parses as 36 -- so the client-route skill press is
-- best-effort, exactly as forage_lands drives it (scenario_forage_lands.cpp:
-- 38-43): the deterministic server proof is #fish -> GoFish(true)
-- (gm_commands/fish.cpp:22), the guarantee half of forage.cpp:302. #fish works
-- in ANY zone: the Watermap.CheckForWaterWhenFishing rule defaults to false
-- (ruletypes.h:418), so CanFish() (forage.cpp:206) skips the water probe
-- entirely and only needs the pole (seeded, primary) + bait (seeded, worn);
-- the catch then lands from the common_fish_ids fallback (forage.cpp:269-281)
-- because guarantee=true forces the success roll (forage.cpp:302).
--
-- Slots 13, 20 and 30 are ALREADY OCCUPIED on Testcaster (live dump 2026-10-
-- 16: primary 13 holds 14337, waist 20 holds 11666, general 30 holds 32601),
-- so a bare INSERT is a duplicate-key. The restore is a wholesale
-- DELETE+INSERT from the pre-seed snapshot (restore_lib.sh:74), which puts
-- the three originals back, so we DELETE the three target slots first and
-- re-seat them here.
SET @tc = (SELECT id FROM character_data WHERE name = 'Testcaster');
-- B1025-H5 (2026-10-16, lane-1 run2, acceptance A): Testcaster's STORED zone
-- is 96 = timorous (the header's earlier "dreadspire" note was wrong; the
-- client stalls at [LOADING] Phase 9/15 and the scenario reports
-- could-not-start, exit 3). timorous is the known lane trap (B1047 #367: keep
-- test characters out of it). Place the character in befallen instead -- a
-- standard zone that loads headless (the peer, Testbuddy, zoned in there in
-- 2.95 s in the same run2). The server zones at the stored x/y/z, so the
-- coordinates MUST be set too (the same trap b630_seed.sql:15-19 documents):
-- befallen's own safe point from the zone table, the canonical on-land spawn
-- zone_place_lib.seed_char places headless sweeps at. The snapshot
-- (character_data restore) puts the original zone/pose back, so the lane is
-- left as found.
UPDATE character_data SET
    zone_id = (SELECT zoneidnumber FROM zone WHERE short_name = 'befallen'),
    x       = (SELECT safe_x       FROM zone WHERE short_name = 'befallen'),
    y       = (SELECT safe_y       FROM zone WHERE short_name = 'befallen'),
    z       = (SELECT safe_z       FROM zone WHERE short_name = 'befallen'),
    heading = 0
WHERE id = @tc;
DELETE FROM inventory WHERE character_id = @tc AND slot_id IN (13, 20, 30);
INSERT INTO inventory (character_id, slot_id, item_id, charges, color, augment_one, augment_two, augment_three, augment_four, augment_five, augment_six, instnodrop, ornament_icon, ornament_idfile, ornament_hero_model)
VALUES (@tc, 13, 13100, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
INSERT INTO inventory (character_id, slot_id, item_id, charges, color, augment_one, augment_two, augment_three, augment_four, augment_five, augment_six, instnodrop, ornament_icon, ornament_idfile, ornament_hero_model)
VALUES (@tc, 20, 13101, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
INSERT INTO inventory (character_id, slot_id, item_id, charges, color, augment_one, augment_two, augment_three, augment_four, augment_five, augment_six, instnodrop, ornament_icon, ornament_idfile, ornament_hero_model)
VALUES (@tc, 30, 8443, 10, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
