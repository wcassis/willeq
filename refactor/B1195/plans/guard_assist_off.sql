UPDATE character_data SET level=60 WHERE id=67; SELECT 'LVLSET',level,class FROM character_data WHERE id=67;
UPDATE rule_values SET rule_value='false' WHERE ruleset_id=1 AND rule_name='Character:PVPEnableGuardFactionAssist' AND rule_value='true'; SELECT 'GUARDASSIST',rule_value FROM rule_values WHERE ruleset_id=1 AND rule_name='Character:PVPEnableGuardFactionAssist';
