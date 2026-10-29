-- B1181 leg B server-side (B1174 hook; undo runs on every exit path):
-- (1) Spells:UseLiveSpellProjectileGFX = true for the leg (as found:
-- 'false', pinned by the server dump), so TrySpellProjectile's
-- ProjectileAnimation takes the player_1 branch and names the bolt's model
-- spells_new.player_1 = IT11504 (the issue's "IT11504" — it lives in the
-- live-GFX field, NOT in FRProjectileItem_Titanium, which stays item 1113
-- for the GFX-off path);
-- (2) 'Darkshadow' (104) moved to the NEUTRAL account 77 ('Wizzy') so the
-- caster's account lists Testcaster first and the willeqwatch account lists
-- only Oraclewatch (the caster-account move re-broke the caster: D < T,
-- archery4; see leg A's server SQL for the full history).
UPDATE rule_values SET rule_value='true' WHERE rule_name='Spells:UseLiveSpellProjectileGFX';
UPDATE character_data SET account_id=77 WHERE id=104 AND name='Darkshadow';
