#!/usr/bin/env bash
# B1159: one lane-5 oracle leg (the B1134 observer rig) with the branch driver, driven
# from this host. Pattern: refactor/B1143/scripts/run_leg.sh (review/B1134/scripts/run_leg.sh).
# usage: run_leg.sh <name> <class> [extra oracle_class_leg.sh args]
#        (plan = refactor/B1159/plans/<name>.tsv, out = ~/kb-evidence/kb-b1159-missile-recapture/oracle/<name>)
R=/home/user/kb-evidence/kb-b1159-missile-recapture
W=/home/user/src/willeq-wt/kb-b1159-missile-recapture
n=$1 c=$2; shift 2
mkdir -p "$R/oracle/$n"
echo "$(date -u +%FT%TZ) leg $n class $c start" >> "$R/progress.log"
EQEMU_LANE=5 bash "$W/scripts/eqemu/oracle/oracle_class_leg.sh" 5 "$c" "$W/refactor/B1159/plans/$n.tsv" "$R/oracle/$n" "$@" > "$R/oracle/$n.run.out" 2>&1
rc=$?
echo "$(date -u +%FT%TZ) leg $n class $c rc=$rc ($(tail -1 "$R/oracle/$n/driver.log" 2>/dev/null))" >> "$R/progress.log"
exit $rc
