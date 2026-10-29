UPDATE character_data SET level=1 WHERE id=67; SELECT 'LVLBACK',level,class FROM character_data WHERE id=67;
UPDATE rule_values SET rule_value='true' WHERE ruleset_id=1 AND rule_name='Character:PVPEnableGuardFactionAssist'; SELECT 'GUARDASSIST',rule_value FROM rule_values WHERE ruleset_id=1 AND rule_name='Character:PVPEnableGuardFactionAssist';
