#!/usr/bin/env bash
# B1195: release the lane lease after a forced kill (usage: release_lease.sh <lane>)
cd /home/user/src/willeq-wt/kb-b1195-oracle-lanes
export EQEMU_LANE=$1 ORACLE_OWNER=kb-b1195-oracle-lanes
source scripts/eqemu/env.sh
eqemu_lock_release 2>&1 | head -3
eqemu_lock_show 2>&1 | head -3
