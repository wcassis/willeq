-- B1181 leg A post: the caster's leg gear is removed (the character is
-- "as found" minus the leg inventory; the driver handles buffs/vitals).
DELETE FROM inventory WHERE character_id=67 AND item_id IN (8003, 8005);
SELECT 'ARCHERY_POST', COUNT(*) FROM inventory WHERE character_id=67 AND item_id IN (8003, 8005);
