-- B900 (#216): ranged_single_shot. The kit the lane's client cannot assemble
-- in-world: the arrow's server slot bitmask is bit 22 (RoF2 slotAmmo) while
-- the client's ammo slot is 21 (Titanium), so canEquipInSlot() rejects it and
-- /equip can never seat it. Seeded after the snapshots (scripts/scenario.sh
-- 3b) so the inventory restore takes it out again.
--
-- Bloodscream (111529, itemtype 5 = ItemTypeBow, attack 37, range 215) in
-- the range slot; Arrow (8005, itemtype 27 = ItemTypeArrow, range 25,
-- stackable, expendablearrow 0) in the ammo slot, 10 arrows. The server's
-- RangedAttack (zone/special_attacks.cpp:860) reads exactly these two slots;
-- the client's /range reads the same range slot from its synced inventory.
-- Bow 215 + arrow 25 = 240 units range: the dummy is 50 units away, inside
-- the 25..240 band (MinRangedAttackDist rule 25, common/ruletypes.h:576).
-- The seed INSERTs the rows directly, so the server's equip checks (item
-- classes bitmask 285) are bypassed -- the login loads the inventory as-is,
-- and RangedAttack itself only checks the range slot for a bow and the ammo
-- slot for arrows (special_attacks.cpp:877-911). The client reads the same
-- synced rows, so /range sees the bow.
--
-- Testchar (level 60) sits at (20, 5); #spawn puts the dummy 50 units north
-- of it -> 50 units.
SET @tc = (SELECT id FROM character_data WHERE name = 'Testchar');
INSERT INTO inventory (character_id, slot_id, item_id, charges, color, augment_one, augment_two, augment_three, augment_four, augment_five, augment_six, instnodrop, ornament_icon, ornament_idfile, ornament_hero_model)
VALUES (@tc, 11, 111529, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
INSERT INTO inventory (character_id, slot_id, item_id, charges, color, augment_one, augment_two, augment_three, augment_four, augment_five, augment_six, instnodrop, ornament_icon, ornament_idfile, ornament_hero_model)
VALUES (@tc, 22, 8005, 10, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
