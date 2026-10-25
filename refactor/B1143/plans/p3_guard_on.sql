-- B1143 p3_rig93 post-SQL (runs with the world DOWN after the snapshot restore;
-- B1134 P2's guard_assist_on.sql verbatim): guard faction assist back ON, with
-- its own verification SELECT.
UPDATE rule_values SET rule_value='true' WHERE ruleset_id=1 AND rule_name='Character:PVPEnableGuardFactionAssist'; SELECT 'GUARDASSIST',rule_value FROM rule_values WHERE ruleset_id=1 AND rule_name='Character:PVPEnableGuardFactionAssist';
