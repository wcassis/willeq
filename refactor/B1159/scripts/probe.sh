#!/usr/bin/env bash
# B1159: lane-5 DB probe inside the lock (mariadb only; compose down after).
# Pattern: refactor/B1143/scripts/probe.sh (review/B1134/scripts/db_probe.sh), driven
# through env.sh with EQEMU_LANE=5.
# usage: probe.sh [owner]   (stdout = the probe, for lane_before.txt / lane_after.txt)
set -u
cd /home/user/src/willeq-wt/kb-b1159-missile-recapture
export EQEMU_LANE=5
. scripts/eqemu/env.sh
. scripts/eqemu/restore_lib.sh
OWNER=${1:-kb-b1159-missile-recapture}
eqemu_lock_take "$OWNER" || { echo "lane held: $(eqemu_lock_show)" >&2; exit 64; }
trap 'compose down >/dev/null 2>&1; eqemu_lock_release' EXIT
# the `compose` wrapper (env.sh) carries -f $COMPOSE_FILE --project-directory
# $AKK_DIR; bare `eq_docker compose` has no configuration and fails.
compose up -d mariadb || { echo "compose up mariadb failed" >&2; exit 65; }
ok=
for i in $(seq 1 60); do if eqemu_sql "SELECT 1;" >/dev/null 2>&1; then ok=1; break; fi; sleep 10; done
[ -n "$ok" ] || { echo "mariadb not ready" >&2; exit 65; }
eqemu_sql "SELECT id,account_id,name,class,level,race,zone_id,x,y,z,heading,cur_hp,mana,endurance,pvp_status,gm FROM character_data WHERE id IN (67,103) ORDER BY id;"
eqemu_sql "SELECT id,name,status,lsaccount_id FROM account WHERE id IN (SELECT account_id FROM character_data WHERE id IN (67,103));"
eqemu_sql "SELECT id,slot_id,spell_id FROM character_memmed_spells WHERE id IN (67,103) ORDER BY id,slot_id;"
eqemu_sql "SELECT character_id,slot_id,spell_id FROM character_buffs WHERE character_id IN (67,103) ORDER BY character_id,slot_id;"
eqemu_sql "SELECT 'skills',COUNT(*) FROM character_skills WHERE id=67; SELECT 'skills_sum',COALESCE(SUM(value),0) FROM character_skills WHERE id=67; SELECT 'disc',COUNT(*) FROM character_disciplines WHERE id IN (67,103); SELECT 'timers',COUNT(*) FROM timers WHERE char_id IN (67,103); SELECT 'currency67',COUNT(*) FROM character_currency WHERE id=67; SELECT 'currency103',COUNT(*) FROM character_currency WHERE id=103;"
eqemu_sql "SELECT 'inv67',COUNT(*) FROM inventory WHERE character_id=67; SELECT 'inv103',COUNT(*) FROM inventory WHERE character_id=103;"
eqemu_sql "SELECT rule_value FROM rule_values WHERE ruleset_id=1 AND rule_name='Character:PVPEnableGuardFactionAssist';"
eqemu_sql "SELECT table_name FROM information_schema.tables WHERE table_name LIKE 'oracle_save_%';"
exit 0
