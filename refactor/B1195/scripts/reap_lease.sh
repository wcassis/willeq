#!/usr/bin/env bash
# B1195: reap a dead lease run (usage: reap_lease.sh <lane> <run-id>)
cd /home/user/src/willeq-wt/kb-b1195-oracle-lanes
export EQEMU_LANE=$1 ORACLE_OWNER=kb-b1195-oracle-lanes
source scripts/eqemu/env.sh
F=$(mktemp)
echo "$2" > "$F"
eqemu_lease_reap "$F" 2>&1 | head -8
rm -f "$F"
echo "=== after ==="
eqemu_lease_show 2>&1 | head -4
