#!/usr/bin/env bash
# B1195: show the lease state for a lane (usage: lock_show.sh <lane>)
cd /home/user/src/willeq-wt/kb-b1195-oracle-lanes
export EQEMU_LANE=$1
source scripts/eqemu/env.sh
eqemu_lock_show 2>&1 | head -5
