#!/bin/bash
set -u
cd "$(cd "$(dirname "$0")/../../.." && pwd)"
export EQEMU_LANE=5
source scripts/eqemu/env.sh
mysql_exec "SHOW COLUMNS FROM account;" 2>&1 | head -25
echo ===LOGIN_ACCT===
mysql_exec "SHOW COLUMNS FROM login_accounts;" 2>&1 | head -25
echo ===ROWS===
mysql_exec "SELECT * FROM login_accounts\\G" 2>&1 | head -60
