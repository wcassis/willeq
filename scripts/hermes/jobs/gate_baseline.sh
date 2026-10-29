#!/usr/bin/env bash
# B1203 (#522): gate_baseline.sh -- the per-fable-tip spell-FX gate BASELINE as a
# no-agent job (render on the ThinkPad + measure on hermes-ext via render_gate.sh,
# then the B1188 judge), parameterised, idempotent, one at a time.
#
#   gate_baseline.sh [<ref>=origin/fable] [--force]
#   gate_baseline.sh --smoke [rows...]   # default rows b1130 r0 r1; no checkout, no build,
#                                        # no lock: renders the gate-baseline tree AS IS into
#                                        # $JOBS_ROOT/smoke/<short>-sl (proves the wrapper)
#
# What it does (the solo prompt rule 7 chain, made a script):
#   1. resolve <ref> to a sha (short = 8 chars); done-file $JOBS_ROOT/gate-baseline/<short>.done
#      exists -> print it, exit 0 (idempotent);
#   2. ONE AT A TIME: a lock dir $JOBS_ROOT/gate-baseline.lock (mkdir is atomic on NFS) plus the
#      legacy check `pgrep -f "[g]ate-baseline.*render_gate"`; while either is held the job waits
#      (it is queued, not refused) -- the next tip's baseline starts after the running one;
#   3. git fetch + checkout --detach <sha> in $WT (/home/user/src/willeq-wt/gate-baseline),
#      rbuild --target model_viewer (ThinkPad);
#   4. render_gate.sh all with CONC (default 3) into /mnt/scratch/gate-baseline/<short>-sl;
#      render_gate.sh itself skips measured rows (re-runs resume) and YIELDS while an eqgame
#      runs on the ThinkPad or load1 > LOADMAX (default 12);
#   5. judge: refactor/B1188/scripts/build_tables_b1188.py over <short>-sl/measure with the
#      checkout as WT -> <short>-sl/tables/ (b1130.tsv b1118.tsv tour.tsv counts.tsv ...);
#   6. the done-file line: JOB gate_baseline <short> status=<ok|incomplete|judge_verify_errors|
#      judge_failed|build_failed|checkout_failed> rows=N/M ...
set -u
here="$(cd "$(dirname "$0")" && pwd)"; . "$here/jobs_lib.sh"
WT=${WT:-/home/user/src/willeq-wt/gate-baseline}
SLUG=gate-baseline
OUT_ROOT=${OUT_ROOT:-/mnt/scratch/gate-baseline}
CONC=${CONC:-3}
SMOKE=0; FORCE=""; REF=origin/fable; ROWS=()
while [ $# -gt 0 ]; do
  case "$1" in
    --smoke) SMOKE=1 ;;
    --force) FORCE=--force ;;
    -h|--help) sed -n 2,26p "$0"; exit 0 ;;
    *) if [ $SMOKE = 1 ]; then ROWS+=("$1"); else REF=$1; fi ;;
  esac; shift
done
JOB_NAME=gate_baseline

if [ $SMOKE = 1 ]; then
  [ ${#ROWS[@]} -gt 0 ] || ROWS=(r0 r1)
  SHA=$(git -C "$WT" rev-parse HEAD); SHORT=${SHA:0:8}
  JOB_SLUG=smoke-$SHORT
  job_begin "smoke" "gate_baseline-$SHORT" --force || exit 0
  EV="$JOBS_ROOT/smoke/$SHORT-sl"; rm -rf "$EV"
  t0=$(date -u +%s)
  CONC=${CONC_SMOKE:-2} SLUG=$SLUG WT=$WT EV=$EV bash "$RUNNER" b1130 "${ROWS[@]}" >> "$JOB_LOG" 2>&1
  n=$(measured_rows "$EV")
  judge "$WT" "$EV/measure" "$EV/tables"; jr=$?
  st=ok; [ "$n" -eq ${#ROWS[@]} ] || st=incomplete
  [ $jr -eq 1 ] && st=judge_verify_errors; [ $jr -gt 1 ] && st=judge_failed
  job_end "$st" "sha=$SHORT rows=$n/${#ROWS[@]} judge_rc=$jr wall_s=$(( $(date -u +%s) - t0 )) ev=$EV tables=$EV/tables"
  exit 0
fi

git -C "$WT" fetch -q origin || { echo "fetch failed in $WT" >&2; exit 3; }
SHA=$(git -C "$WT" rev-parse --verify "$REF^{commit}") || { echo "unknown ref $REF" >&2; exit 3; }
SHORT=${SHA:0:8}; JOB_SLUG=$SHORT
job_begin gate-baseline "$SHORT" $FORCE; rc=$?
[ $rc -eq 10 ] && exit 0; [ $rc -eq 11 ] && exit 0
LOCK="$JOBS_ROOT/gate-baseline.lock"
queued=0
until { ! pgrep -f "[g]ate-baseline.*render_gate" >/dev/null; } && mkdir "$LOCK" 2>/dev/null; do
  if [ -d "$LOCK" ] && ! kill -0 "$(cat "$LOCK/pid" 2>/dev/null || echo 0)" 2>/dev/null; then
    rmdir "$LOCK" 2>/dev/null || rm -rf "$LOCK"; continue      # stale lock (holder gone)
  fi
  [ $queued = 0 ] && echo "queued behind the running baseline $(job_now)" >> "$JOB_LOG"; queued=1
  sleep 60
done
echo $$ > "$LOCK/pid"
trap 'rm -rf "$LOCK"; rm -f "$JOB_RUN"' EXIT
t0=$(date -u +%s)
git -C "$WT" checkout -q --detach "$SHA" || { job_end checkout_failed "sha=$SHORT"; exit 1; }
if [ "${NO_BUILD:-0}" != 1 ]; then
  "$RBUILD" "$WT" --target model_viewer >> "$JOB_LOG" 2>&1 || { job_end build_failed "sha=$SHORT log=$JOB_LOG"; exit 1; }
fi
EV="$OUT_ROOT/$SHORT-sl"
CONC=$CONC SLUG=$SLUG WT=$WT EV=$EV bash "$RUNNER" all >> "$JOB_LOG" 2>&1
n=$(measured_rows "$EV"); m=$(cat "$EV"/rows.all.tsv 2>/dev/null | wc -l)
judge "$WT" "$EV/measure" "$EV/tables"; jr=$?
st=ok; [ "$n" -ge "$m" ] && [ "$m" -gt 0 ] || st=incomplete
[ $jr -eq 1 ] && st=judge_verify_errors; [ $jr -gt 1 ] && st=judge_failed
job_end "$st" "sha=$SHORT rows=$n/$m judge_rc=$jr wall_s=$(( $(date -u +%s) - t0 )) ev=$EV tables=$EV/tables"
