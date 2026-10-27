#!/usr/bin/env bash
# B1166 (#485) ask 4: re-render the three named harness rows, before (fable
# 4895adc2) and after (this branch), with their cards' own plans and lengths:
#   fear229_b1130      review/B1162/worker/harness/fear229_b1130/plan.tsv, 10.0 s (300 frames), --camera b1130
#   disc4499_b1130self review/B1162/worker/harness/disc4499_b1130self/plan.tsv, 16.0 s (480), --camera b1130-self
#     (both: fps 30, sim 60 Hz, seed 1 -- review/B1162/worker/harness/run_harness.sh)
#   t93_b1130          review/B1164/worker/dev2/tour_93.tsv, 13.2 s (396), --camera b1130, fps 30, seed 1
#     (refactor/B1164/scripts/run93.sh on task/kb-b1164-pixel-area)
# Frames go straight to the NFS evidence dir; measurement is done afterwards
# (README sec. 5 names each command).
# usage: run_named_rows.sh <before|after>
set -u
VAR=${1:?before|after}
case "$VAR" in before) SLUG=kb-b1166-before ;; after) SLUG=kb-b1166-target-fit ;; *) exit 2 ;; esac
WT=/mnt/projects/fable/main/wt/$SLUG
BIN=/home/user/willeq-builds/$SLUG/build/bin/model_viewer
EV=/mnt/projects/fable/review/B1166/worker/named_$VAR
H=/mnt/projects/fable/review/B1162/worker/harness
TP=${TP:-172.16.200.205}
mkdir -p "$EV"
fail=0
while read -r tag plan secs cam extra; do
  fd="$EV/$tag"; rm -rf "$fd"; mkdir -p "$fd"; cp "$plan" "$fd/plan.tsv"
  extra=${extra//,/ }
  if ssh "$TP" "cd '$WT' && '$BIN' --spell-test --client /mnt/projects/EverQuestP1999 \
      --eqe1 /mnt/projects/EverQuest_ETC1/willeq.eqe1 --plan '$fd/plan.tsv' --frames-dir '$fd/frames' \
      --fps 30 --seconds $secs --seed 1 --headless --camera $cam $extra > '$fd/run.log' 2>&1" < /dev/null; then
    echo "[$tag] rendered $(date -u +%T)" >> "$EV/run.log"
  else
    echo "[$tag] RENDER FAILED" >> "$EV/run.log"; fail=$((fail + 1))
  fi
done <<ROWS
fear229_b1130 $H/fear229_b1130/plan.tsv 10.0 b1130 --sim-hz,60
disc4499_b1130self $H/disc4499_b1130self/plan.tsv 16.0 b1130-self --sim-hz,60
t93_b1130 /mnt/projects/fable/review/B1164/worker/dev2/tour_93.tsv 13.2 b1130
ROWS
exit "$fail"
