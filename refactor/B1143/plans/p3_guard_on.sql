-- B1143 p3_rig93 post-SQL (runs with the world DOWN after the snapshot restore;
-- B1134 P2's verbatim line): guard faction assist back ON.
UPDATE rule_values SET rule_value='true' WHERE ruleset_id=1 AND rule_name='Character:PVPEnableGuardFactionAssist';
