#!/bin/bash
# B1176 live leg: read-only probe -- account ids, gm flags, oracle client inis.
set -u
cd "$(cd "$(dirname "$0")/../../.." && pwd)"
export EQEMU_LANE=5
source scripts/eqemu/env.sh
mysql_exec "SELECT id,username,admin FROM account WHERE username IN ('willeq_test','willeqwatch')\\G" 2>&1 | head -20
echo ===GM===
mysql_exec "SELECT id,name,account_id,gm,level,cur_hp,mana FROM character_data WHERE id IN (67,74,78,103)\\G" 2>&1 | head -40
echo ===SIZE===
mysql_exec "SHOW COLUMNS FROM character_data LIKE '%size%';" 2>&1 | head
