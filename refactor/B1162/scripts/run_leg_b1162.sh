#!/usr/bin/env bash
# B1143: one oracle class leg on lane 5 with the branch driver (observer rig).
# Pattern: review/B1134/scripts/run_leg.sh, worktree + evidence paths of this card.
# usage: run_leg.sh <name> <class> [extra oracle_class_leg.sh args]
#        (plan = refactor/B1162/plans/<name>.tsv,
#         out  = ~/kb-evidence/kb-b1162-combat-fx/oracle/<name>)
# e.g. run_leg.sh p3_rig93 6 --pre-sql-file refactor/B1162/plans/p3_guard_off.sql \
#                              --post-sql-file refactor/B1162/plans/p3_guard_on.sql
R=/home/user/kb-evidence/kb-b1162-combat-fx
W=/home/user/src/willeq-wt/kb-b1162-combat-fx
n=$1 c=$2; shift 2
mkdir -p "$R/oracle/$n"
echo "$(date -u +%FT%TZ) leg $n class $c start" >> $R/progress.log
EQEMU_LANE=5 bash "$W/scripts/eqemu/oracle/oracle_class_leg.sh" 5 "$c" "$W/refactor/B1162/plans/$n.tsv" "$R/oracle/$n" "$@" > "$R/oracle/$n.run.out" 2>&1
rc=$?
echo "$(date -u +%FT%TZ) leg $n class $c rc=$rc ($(tail -1 "$R/oracle/$n/driver.log" 2>/dev/null))" >> $R/progress.log
exit $rc
