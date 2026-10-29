#!/usr/bin/env bash
# B1195: one oracle leg on a named lane with this worktree's driver (the B1134 observer rig:
# caster on the session Xvfb, observer on :99 = the x11grab display). The lane lease is taken
# by oracle_session.sh itself; ORACLE_OWNER is the card slug so the lease log names the card.
# usage: run_leg.sh <lane 1..5> <out_dir>
R=/home/user/src/willeq-wt/kb-b1195-oracle-lanes
LANE=$1 OUT=$2
[[ $LANE =~ ^[1-5]$ && -n $OUT ]] || { echo "usage: run_leg.sh <lane> <out_dir>" >&2; exit 64; }
if [[ $LANE == 5 ]]; then PLAN=$R/refactor/B1195/plans/b1195_lane5_rig93_253.tsv; else PLAN=$R/refactor/B1195/plans/b1195_lane2_rig93_253.tsv; fi
export ORACLE_OWNER=kb-b1195-oracle-lanes EQEMU_LANE=$LANE
mkdir -p "$OUT"
echo "$(date -u +%FT%TZ) leg b1195_rig93_253 class 6 lane $LANE start (owner $ORACLE_OWNER, plan $(basename $PLAN))" >> "$R/refactor/B1195/progress.log"
bash "$R/scripts/eqemu/oracle/oracle_class_leg.sh" "$LANE" 6 "$PLAN" "$OUT" \
    --pre-sql-file "$R/refactor/B1195/plans/guard_assist_off.sql" \
    --post-sql-file "$R/refactor/B1195/plans/guard_assist_on.sql" \
    > "$OUT/run.out" 2>&1
rc=$?
echo "$(date -u +%FT%TZ) leg b1195_rig93_253 class 6 lane $LANE rc=$rc ($(tail -1 "$OUT/driver.log" 2>/dev/null))" >> "$R/refactor/B1195/progress.log"
LEASE=$(cd "$R" && EQEMU_LANE=$LANE bash -c 'source scripts/eqemu/env.sh; eqemu_lock_show 2>/dev/null || echo free')
echo "$(date -u +%FT%TZ) lease after: $LEASE" >> "$R/refactor/B1195/progress.log"
exit $rc
