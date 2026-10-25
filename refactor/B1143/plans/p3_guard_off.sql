-- B1143 p3_rig93 pre-SQL (runs inside the lock, after the driver's save + class
-- switch; B1134 P2's guard_assist_off.sql verbatim): guard faction assist OFF
-- for the player-target phase, with its own verification SELECT.
UPDATE rule_values SET rule_value='false' WHERE ruleset_id=1 AND rule_name='Character:PVPEnableGuardFactionAssist' AND rule_value='true'; SELECT 'GUARDASSIST',rule_value FROM rule_values WHERE ruleset_id=1 AND rule_name='Character:PVPEnableGuardFactionAssist';
