#!/bin/bash
# B1176 live leg: read-only schema probe for creating a dark-elf character.
set -u
cd "$(cd "$(dirname "$0")/../../.." && pwd)"
export EQEMU_LANE=5
source scripts/eqemu/env.sh
mysql_exec "SHOW TABLES LIKE '%account%';" 2>&1 | head
echo ===CHAR_COLS===
mysql_exec "SHOW COLUMNS FROM character_data;" 2>&1 | head -60
echo ===ACCT===
mysql_exec "SELECT * FROM accounts WHERE username IN ('willeq_test','willeqwatch')\\G" 2>&1 | head -30
echo ===ORPHANS===
mysql_exec "SELECT id,name FROM character_data WHERE name LIKE 'Oracle%';" 2>&1 | head
