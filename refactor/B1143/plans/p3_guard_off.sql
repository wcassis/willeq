-- B1143 p3_rig93 pre-SQL (runs inside the lock, after the driver's save + class
-- switch; B1134 P2's verbatim line): guard faction assist OFF for the player-target
-- phase. Restored by p3_guard_on.sql (post-SQL, world down).
UPDATE rule_values SET rule_value='false' WHERE ruleset_id=1 AND rule_name='Character:PVPEnableGuardFactionAssist' AND rule_value='true';
