-- B1181 leg B pre: the observer's survivability only (the bolt needs no
-- caster gear — #castspell bypasses memorisation). The driver restores
-- cur_hp/mana/endurance/pvp from vitals-before.tsv with the world down.
UPDATE character_data SET cur_hp=999999, mana=999999, endurance=999999 WHERE id=103 AND name='Oraclewatch';
SELECT 'BOLT_PRE_OBS', id, name, cur_hp, mana, endurance FROM character_data WHERE id=103;
