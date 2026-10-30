#!/bin/bash
# B1195: read-only lane-N DB probe inside the lane lease (mariadb only; compose down after).
# usage: db_probe.sh <lane> "<sql>" ...
set -u
R=/home/user/src/willeq-wt/kb-b1195-oracle-lanes
LANE=$1; shift
cd "$R"
case $LANE in
  5) export EQEMU_LANE=5; export AKK_DIR=/opt/akk-stack; HOST=172.16.200.205 ;;
  *) export EQEMU_LANE=$LANE; export AKK_DIR=/mnt/projects/akk-stack ;;
esac
source scripts/eqemu/env.sh
source scripts/eqemu/restore_lib.sh
HOST="${HOST:-$EQEMU_HOST}"
OWNER="kb-b1195-oracle-lanes_probe_$(date -u +%Y%m%dT%H%M%S)_$$"
eqemu_lock_take "$OWNER" || { echo "lane $LANE held: $(eqemu_lock_show)"; exit 64; }
ssh -n user@$HOST "cd $AKK_DIR && docker compose up -d mariadb" >/dev/null 2>&1
for i in $(seq 1 30); do eqemu_sql "SELECT 1;" >/dev/null 2>&1 && break; sleep 2; done
for q in "$@"; do echo "## $q"; eqemu_sql "$q"; done
ssh -n user@$HOST "cd $AKK_DIR && docker compose down" >/dev/null 2>&1
eqemu_lock_release
echo "lock=$(eqemu_lock_show 2>/dev/null || echo free)"
