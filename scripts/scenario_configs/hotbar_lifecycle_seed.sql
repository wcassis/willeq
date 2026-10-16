-- B1025-H5 (#345): hotbar_lifecycle. The kit the lane's client cannot seat
-- in-world: Testcaster (wizard, lane 1) is already fully equipped --
-- primary 13 holds 14337, and the live inventory dump (2026-10-16, this card)
-- shows general 23-30 all filled (9990, 9991, 15288, 15372, 18755, 21779,
-- 32601, +8007 in ammo 22), so #summonitem + /putdown would swap the OLD
-- primary onto the cursor instead of seating the pole. The rows go in by
-- direct INSERT, the way ranged_single_shot_seed.sql does: the server loads
-- the inventory as-is at login, and the client reads the same synced rows.
-- The seed ALSO places the character in freporte (the zone UPDATE below):
-- the stored zone is timorous (96), which never finishes loading headless.
-- freporte is where the FISH leg works (see the watermap note under the
-- UPDATE) and loads headless on this client (freporte.s3d is in the EQ
-- client path, and B894's forage_lands zoned a character in there on a lane).
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
-- (gm_commands/fish.cpp:22), the guarantee half of forage.cpp:302.
--
-- The FISH leg's water requirement. Lane 1's ruleset runs the water probe
-- (Watermap:CheckForWaterWhenFishing is ON -- run-3's zone log emitted
-- FISHING_LAND from the probe branch, forage.cpp:246, whatever the rule's
-- source default, ruletypes.h:418). The probe (forage.cpp:213-266) needs:
--   1. zone->watermap != nullptr (every locally-loadable zone has one --
--      befallen.wtr, freporte.wtr... the 16 zones WITHOUT a watermap file
--      are all shard/dungeon variants with no local .s3d, so the client
--      FATALs on load; verified against the client path on 2026-10-16),
--   2. a rod tip 30u ahead along the heading at the player's z (FishingRod-
--      Length 30, ruletypes.h:419) whose ground (FindBestZ) is within
--      FishingLineLength 100 of the player's z (ruletypes.h:420),
--   3. a 1u-step scan from the rod tip down 100u that hits InWater/InVWater
--      (forage.cpp:240).
-- befallen's watermap has NO water region (befallen.wtr, 9,882-node ZBSP:
-- its 4,046 leaves carry special=0 (normal) or 3 (zone line) only, never
-- 1/7), so there the probe can never return true -- run-3 proved it.
-- FREPORTE's does: the lane host's freporte.wtr has a deep water body
-- (surface z=-70, floor z=-112, spanning roughly x -1450..-950,
-- y -950..-400; probes in /home/user/kb-evidence/kb-h5-hotbar-scenario/
-- water/). So this seed places the character in freporte, and the scenario's
-- #goto prep (scenario_hotbar_lifecycle.cpp MakePrep, kShoreGotoAt) stands it
-- at a computed water's edge: land at (-1325,-850,-70) facing heading 320
-- (225deg), rod tip (-1346,-871,-70) in the water body, seabed 42u below --
-- every probe gate clears. That is B894's forage_lands pattern (a #goto to a
-- shore stand parsed from the lane's own watermap, then #fish), proven on a
-- lane. The seed itself only sets freporte's SAFE POINT (on land, near the
-- dock); the #goto does the positioning, in-zone (MovePC -> ZonePC, a same-
-- zone teleport that sends the character's packet with the goto's coords +
-- heading).
--
-- Slots 13, 20 and 30 are ALREADY OCCUPIED on Testcaster (live dump 2026-10-
-- 16: primary 13 holds 14337, waist 20 holds 11666, general 30 holds 32601),
-- so a bare INSERT is a duplicate-key. The restore is a wholesale
-- DELETE+INSERT from the pre-seed snapshot (restore_lib.sh:74), which puts
-- the three originals back, so we DELETE the three target slots first and
-- re-seat them here.
SET @tc = (SELECT id FROM character_data WHERE name = 'Testcaster');
-- B1025-H5 (2026-10-16, lane-1 runs, acceptance A): Testcaster's STORED zone
-- is 96 = timorous (the client stalls at [LOADING] Phase 9/15 and the
-- scenario reports could-not-start, exit 3; timorous is the lane trap, B1047
-- #367). The first fix placed the character in befallen (loads headless --
-- the peer, Testbuddy, zoned in there in 2.95 s) -- run-3 proved the LOAD
-- works, but the fish leg refused with FISHING_LAND: befallen's watermap has
-- no water region. This seed therefore places the character in FREPORTE
-- instead -- the one zone that both loads headless on this client AND has a
-- real water body the fish probe can find (see the water-requirement note
-- above). The server zones at the stored x/y/z, so the coordinates MUST be
-- set too (the same trap b630_seed.sql:15-19 documents); freporte's own safe
-- point from the zone table is the spawn.
--
-- LEVEL 50 + GM, the b630 caster shape (b630_seed.sql:27-40). A level-1
-- caster in befallen (level-16 mobs) got aggroed -- run-1's log shows the
-- necro hitting Testcaster at t+12 ("You try to hit Testcaster but miss!") --
-- and incoming damage interrupts the item leg's 2.5s spell-86 click-cast
-- before the server completes it, so NO charge is consumed (the post-check
-- then reads 10). At level 50 nothing in the zone aggroes (no damage, no
-- interruption), and every leg spell clears its class/level gate (Minor
-- Shielding 288 is cast via #memspell, Enduring Breath 86 via the item).
-- Testcaster is already GM (the run's "Your GM flag prevents you from
-- fizzling" line); gm=1 just pins it. mana/endurance fill so no leg is
-- starved. The snapshot (character_data restore) puts the original level/gm/
-- zone/pose/resources all back, so the lane is left as found.
UPDATE character_data SET
    gm      = 1,
    level   = 50,
    mana    = 9999,
    endurance = 100,
    zone_id = (SELECT zoneidnumber FROM zone WHERE short_name = 'freporte'),
    x       = (SELECT safe_x       FROM zone WHERE short_name = 'freporte'),
    y       = (SELECT safe_y       FROM zone WHERE short_name = 'freporte'),
    z       = (SELECT safe_z       FROM zone WHERE short_name = 'freporte'),
    heading = 0
WHERE id = @tc;
DELETE FROM inventory WHERE character_id = @tc AND slot_id IN (13, 20, 30);
INSERT INTO inventory (character_id, slot_id, item_id, charges, color, augment_one, augment_two, augment_three, augment_four, augment_five, augment_six, instnodrop, ornament_icon, ornament_idfile, ornament_hero_model)
VALUES (@tc, 13, 13100, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
INSERT INTO inventory (character_id, slot_id, item_id, charges, color, augment_one, augment_two, augment_three, augment_four, augment_five, augment_six, instnodrop, ornament_icon, ornament_idfile, ornament_hero_model)
VALUES (@tc, 20, 13101, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
INSERT INTO inventory (character_id, slot_id, item_id, charges, color, augment_one, augment_two, augment_three, augment_four, augment_five, augment_six, instnodrop, ornament_icon, ornament_idfile, ornament_hero_model)
VALUES (@tc, 30, 8443, 10, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
