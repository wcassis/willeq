#!/bin/bash
set -u
cd "$(cd "$(dirname "$0")/../../.." && pwd)"
export EQEMU_LANE=5
source scripts/eqemu/env.sh
mysql_exec "SELECT id,name,charname,status FROM account WHERE id IN (70,72,75,80)\\G" 2>&1 | head -30
mysql_exec "SELECT id,name,account_id FROM character_data WHERE name IN ('Testcaster','Oraclealpha','Oracledelta','Oraclewatch') ORDER BY id;" 2>&1 | head
