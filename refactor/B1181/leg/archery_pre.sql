-- B1181 leg A pre: the caster's archery gear (T494 server slot numbering:
-- 11 = main hand, 22 = ammo) and the observer's survivability. The driver
-- restores cur_hp/mana/endurance/pvp from vitals-before.tsv with the world
-- down (oracle_session.sh), so no undo of the observer bump is needed here.
DELETE FROM inventory WHERE character_id=67 AND slot_id=11;
INSERT INTO inventory (character_id, slot_id, item_id, charges) VALUES (67, 11, 8003, 0);
DELETE FROM inventory WHERE character_id=67 AND slot_id=22;
INSERT INTO inventory (character_id, slot_id, item_id, charges) VALUES (67, 22, 8005, 0);
UPDATE character_data SET cur_hp=999999, mana=999999, endurance=999999 WHERE id=103 AND name='Oraclewatch';
SELECT 'ARCHERY_PRE', slot_id, item_id FROM inventory WHERE character_id=67 AND slot_id IN (11, 22);
SELECT 'ARCHERY_PRE_OBS', id, name, cur_hp, mana, endurance FROM character_data WHERE id=103;
