#!/usr/bin/env bash
# B1203 (#522): gate_after.sh <worktree-slug> [--force] [rows...] -- a card's AFTER as a
# no-agent job: build model_viewer for the card's worktree (rbuild), render + measure the
# gate with render_gate.sh into the card's evidence dir, judge BOTH the matching surfaceless
# fable-tip baseline and the AFTER with the same judge (the card worktree's
# refactor/B1188/scripts/build_tables_b1188.py), and write the per-row delta, so the next
# leg only READS tables.
#
#   gate_after.sh kb-b1197-rec35-blue                 # all 87 rows (tour-7 + b1118 40 + b1130 40)
#   SET=b1130 gate_after.sh kb-b1197-rec35-blue r0 r35 # a subset (SET = tour7|b1118|b1130|all)
#
# Inputs / env: WT (default /home/user/src/willeq-wt/<slug>), EV (default
# /mnt/scratch/kb-evidence/<slug>/gate), CONC (default 2), BASE_SHA (default = the 8-char
# merge-base of the worktree HEAD and origin/fable), BASE_DIR (default
# /mnt/scratch/gate-baseline/<BASE_SHA>-sl), NO_BUILD=1 (skip rbuild).
# Outputs: $EV/measure/<row>/ (render_gate), $EV/tables/after/ + $EV/tables/base/ (judge),
# $EV/tables/delta.tsv + flips.tsv (gate_delta.py), a copy of tables/ in
# ~/kb-evidence/<slug>/gate_tables/ (small evidence), and the done-file
# $JOBS_ROOT/<slug>/gate_after.done:
#   JOB gate_after <slug> status=<ok|incomplete|no_baseline|build_failed|judge_failed> base=<sha> rows=N/M ...
# Exit 0 whenever a done-file was written (the status says how it went); 0 also when the job
# is already done or already running (idempotent; --force re-runs).
set -u
here="$(cd "$(dirname "$0")" && pwd)"; . "$here/jobs_lib.sh"
SLUG=${1:?usage: gate_after.sh <worktree-slug> [--force] [rows...]}; shift
FORCE=""; ROWS=()
for a in "$@"; do case "$a" in --force) FORCE=--force ;; *) ROWS+=("$a") ;; esac; done
SET=${SET:-all}
WT=${WT:-/home/user/src/willeq-wt/$SLUG}
EV=${EV:-/mnt/scratch/kb-evidence/$SLUG/gate}
CONC=${CONC:-2}
JOB_NAME=gate_after; JOB_SLUG=$SLUG
job_begin "$SLUG" gate_after $FORCE; rc=$?
[ $rc -eq 10 ] && exit 0; [ $rc -eq 11 ] && exit 0
[ -d "$WT" ] || { job_end no_worktree "wt=$WT"; exit 0; }
t0=$(date -u +%s)
HEAD_SHA=$(git -C "$WT" rev-parse HEAD)
git -C "$WT" fetch -q origin 2>>"$JOB_LOG"
BASE_SHA=${BASE_SHA:-$(git -C "$WT" merge-base HEAD origin/fable | cut -c1-8)}
BASE_DIR=${BASE_DIR:-/mnt/scratch/gate-baseline/$BASE_SHA-sl}
if [ ! -d "$BASE_DIR/measure" ]; then
  job_end no_baseline "head=${HEAD_SHA:0:8} base=$BASE_SHA missing=$BASE_DIR/measure (run gate_baseline.sh $BASE_SHA first)"
  exit 0
fi
if [ "${NO_BUILD:-0}" != 1 ]; then
  "$RBUILD" "$WT" --target model_viewer >> "$JOB_LOG" 2>&1 || { job_end build_failed "head=${HEAD_SHA:0:8} log=$JOB_LOG"; exit 0; }
fi
CONC=$CONC SLUG=$SLUG WT=$WT EV=$EV bash "$RUNNER" "$SET" "${ROWS[@]}" >> "$JOB_LOG" 2>&1
n=$(measured_rows "$EV"); m=$(cat "$EV/rows.$SET.tsv" 2>/dev/null | wc -l)
judge "$WT" "$EV/measure" "$EV/tables/after"; ja=$?
judge "$WT" "$BASE_DIR/measure" "$EV/tables/base"; jb=$?
python3 -I "$here/gate_delta.py" "$EV/tables/base" "$EV/tables/after" "$EV/tables" >> "$JOB_LOG" 2>&1; jd=$?
mkdir -p "$HOME/kb-evidence/$SLUG/gate_tables"
rsync -a --exclude judge.py "$EV/tables/" "$HOME/kb-evidence/$SLUG/gate_tables/" 2>>"$JOB_LOG"
st=ok; [ "$n" -ge "$m" ] && [ "$m" -gt 0 ] || st=incomplete
{ [ $ja -gt 1 ] || [ $jb -gt 1 ] || [ $jd -ne 0 ]; } && st=judge_failed
job_end "$st" "head=${HEAD_SHA:0:8} base=$BASE_SHA rows=$n/$m judge_rc=$ja/$jb delta_rc=$jd wall_s=$(( $(date -u +%s) - t0 )) tables=$EV/tables"
