#!/usr/bin/env bash
# B1172 (#491): every job of <jobs.tsv> for one variant, P at a time.
# usage: run_all.sh <before|after> <jobs.tsv> [P=3]
set -u
VAR=$1 JOBS=$2 P=${3:-3}
D=$(cd "$(dirname "$0")" && pwd)
EV=/mnt/projects/fable/review/B1172/worker/runs_$VAR
mkdir -p "$EV"
awk -F'\t' -v v="$VAR" '{printf "%s\0%s\0%s\0%s\0%s\0%s\0%s\0", v, $1, $2, $3, $4, $5, $6}' "$JOBS" \
  | xargs -0 -n 7 -P "$P" bash "$D/run_one.sh"
fails=$(grep -c 'FAILED' "$EV/sweep.log" 2>/dev/null)
echo "${fails:-0}" > "$EV/sweep.exit"
echo "done $(date -u +%T): $(wc -l < "$JOBS") jobs, ${fails:-0} failed" >> "$EV/sweep.log"
