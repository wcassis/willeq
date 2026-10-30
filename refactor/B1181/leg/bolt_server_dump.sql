-- B1181 leg B server dump (B1174): as-found record of everything the leg
-- touches.
SELECT 'DUMP-CHAR', id, name, account_id, level, class, cur_hp, mana, endurance, pvp_status FROM character_data WHERE id IN (67, 103, 104);
SELECT 'DUMP-RULE', rule_name, rule_value FROM rule_values WHERE rule_name IN ('Spells:FRProjectileItem_Titanium', 'Spells:UseLiveSpellProjectileGFX');
SELECT 'DUMP-SP68', id, name, targettype, resisttype, player_1 FROM spells_new WHERE id IN (68, 322);
SELECT 'DUMP-ITEM', id, name, IDFile FROM items WHERE id IN (8005, 1113);
