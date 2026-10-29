-- B1181 leg B server undo (B1174): both changes back to as found.
UPDATE rule_values SET rule_value='false' WHERE rule_name='Spells:UseLiveSpellProjectileGFX';
UPDATE character_data SET account_id=(SELECT account_id FROM character_data WHERE id=103) WHERE id=104 AND name='Darkshadow';
