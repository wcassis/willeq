#!/bin/bash
# B1176 live leg: lane 5 state probe (run from the worktree root).
set -u
cd "$(cd "$(dirname "$0")/../../.." && pwd)"
export EQEMU_LANE=5
source scripts/eqemu/env.sh
echo "lock:"
eqemu_lock_show || echo "(free)"
echo "---"
if eqemu_stack_ready; then echo STACK_READY; else echo STACK_NOT_READY; fi
echo "---"
mysql_exec "SELECT id,name,race,class,level,zone_id,x,y,z,heading FROM character_data WHERE id IN (67,74,77,78,80,81,103) OR race=14 ORDER BY id;" 2>&1 | head -20
echo "---accounts---"
mysql_exec "SELECT la.id,la.name,cd.id AS char_id,cd.name AS char_name FROM login_account la LEFT JOIN character_data cd ON cd.account_id=la.id WHERE cd.id IN (67,74,77,78,80,81,103) OR cd.race=14 OR la.name IN ('willeq_test','willeqwatch') ORDER BY cd.id;" 2>&1 | head -25
