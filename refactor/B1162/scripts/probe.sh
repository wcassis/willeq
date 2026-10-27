#!/usr/bin/env bash
# B1162: lane-5 DB probe. ASSUMES the caller already holds the lane lease
# (kb-b1162-combat-fx) -- no lock_take/release/compose-down here, so it can be
# run for both lane_before and lane_after. Pattern: refactor/B1159/scripts/probe.sh.
# usage: probe.sh   (stdout = the probe)
set -u
cd /home/user/src/willeq-wt/kb-b1162-combat-fx
export EQEMU_LANE=5
. scripts/eqemu/env.sh
sql() { mysql_exec "$1" 2>&1; }
sql "SELECT id,account_id,name,class,level,race,zone_id,x,y,z,heading,cur_hp,mana,endurance,pvp_status,deity,gm FROM character_data WHERE id IN (67,103) ORDER BY id;"
sql "SELECT id,name,status,lsaccount_id FROM account WHERE id IN (SELECT account_id FROM character_data WHERE id IN (67,103));"
sql "SELECT id,slot_id,spell_id FROM character_memmed_spells WHERE id IN (67,103) ORDER BY id,slot_id;"
sql "SELECT character_id,slot_id,spell_id FROM character_buffs WHERE character_id IN (67,103) ORDER BY character_id,slot_id;"
sql "SELECT 'skills',COUNT(*) FROM character_skills WHERE id=67; SELECT 'skills_sum',COALESCE(SUM(value),0) FROM character_skills WHERE id=67; SELECT 'disc',COUNT(*) FROM character_disciplines WHERE id IN (67,103); SELECT 'timers',COUNT(*) FROM timers WHERE char_id IN (67,103); SELECT 'currency67',COUNT(*) FROM character_currency WHERE id=67; SELECT 'currency103',COUNT(*) FROM character_currency WHERE id=103;"
sql "SELECT 'inv67',COUNT(*) FROM inventory WHERE character_id=67; SELECT 'inv103',COUNT(*) FROM inventory WHERE character_id=103;"
sql "SELECT rule_value FROM rule_values WHERE ruleset_id=1 AND rule_name='Character:PVPEnableGuardFactionAssist';"
sql "SELECT table_name FROM information_schema.tables WHERE table_name LIKE 'oracle_save_%';"
exit 0
