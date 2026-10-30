#!/usr/bin/env bash
# B1195: release this card's lane lease with the exact identity incl. the recorded pid.
# usage: release_as.sh <lane> <card> <run> <pid>
cd /home/user/src/willeq-wt/kb-b1195-oracle-lanes
export EQEMU_LANE=$1 ORACLE_OWNER=kb-b1195-oracle-lanes
export LANE_LEASE_CARD=$2 LANE_LEASE_RUN=$3 LANE_LEASE_PID=$4
source scripts/eqemu/env.sh
eqemu_lease_release 2>&1 | head -5
echo "=== after ==="
eqemu_lease_show 2>&1 | head -4
