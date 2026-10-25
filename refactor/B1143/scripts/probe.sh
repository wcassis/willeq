#!/usr/bin/env bash
# B1143: lane-5 DB probe inside the lock (mariadb only; compose down after).
# Pattern: review/B1134/scripts/db_probe.sh, driven through env.sh with EQEMU_LANE=5.
# usage: probe.sh <owner>   (stdout = the probe, for lane_before.txt / lane_after.txt)
set -u
cd /home/user/src/willeq-wt/kb-b1143-rig-residue
export EQEMU_LANE=5
. scripts/eqemu/env.sh
. scripts/eqemu/restore_lib.sh
OWNER=${1:-kb-b1143-rig-residue}
eqemu_lock_take "$OWNER" || { echo "lane held: $(eqemu_lock_show)" >&2; exit 64; }
trap 'eq_docker compose down >/dev/null 2>&1; eqemu_lock_release' EXIT
eq_docker compose up -d mariadb >/dev/null 2>&1
ok=
for i in $(seq 1 30); do if eqemu_sql "SELECT 1;" >/dev/null 2>&1; then ok=1; break; fi; sleep 2; done
[ -n "$ok" ] || { echo "mariadb not ready" >&2; exit 65; }
eqemu_sql "SELECT id,account_id,name,class,level,race,zone_id,x,y,z,heading,cur_hp,pvp_status,gm FROM character_data WHERE id IN (67,103);"
eqemu_sql "SELECT id,name,status,lsaccount_id FROM account WHERE id IN (SELECT account_id FROM character_data WHERE id IN (67,103));"
eqemu_sql "SELECT slot_id,spell_id FROM character_memmed_spells WHERE id IN (67,103) ORDER BY id,slot_id;"
eqemu_sql "SELECT id,slot_id,spell_id FROM character_buffs WHERE id IN (67,103) ORDER BY id,slot_id;"
eqemu_sql "SELECT 'skills',COUNT(*) FROM character_skills WHERE id=67; SELECT 'skills_sum',COALESCE(SUM(current_value),0) FROM character_skills WHERE id=67; SELECT 'disc',COUNT(*) FROM character_disciplines WHERE id IN (67,103); SELECT 'timers',COUNT(*) FROM timers WHERE char_id IN (67,103); SELECT 'currency67',COUNT(*) FROM character_currency WHERE id=67; SELECT 'currency103',COUNT(*) FROM character_currency WHERE id=103;"
eqemu_sql "SELECT table_name FROM information_schema.tables WHERE table_name LIKE 'oracle_save_%';"
echo
echo "-- B1143 item 3/4: record-325/1 spell owning classes + Intimidation/Crippling Strike skill ids"
eqemu_sql "SELECT id,name,class FROM skills WHERE name LIKE '%Crippling%' OR name LIKE '%Thunder%' OR name LIKE '%Intimidat%';"
eqemu_sql "SELECT id,name,class FROM spells WHERE id IN (93,229,4504,6236,6268,6299,6311,4593,4629);"
exit 0
