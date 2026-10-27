#!/bin/bash
# B1164 dev2: render spell 93 in the local model_viewer harness and measure it with the gate's
# harness_measure.py, exactly as review/B1149/scripts/run_scene.sh does (30 fps, seed 1, 13.2 s).
# usage: run93.sh <name> <plan.tsv> [extra model_viewer args...]
set -u
WT=/mnt/projects/fable/main/wt/kb-b1164-pixel-area
EV=/mnt/projects/fable/review/B1164/worker/dev2
n=$1 plan=$2; shift 2
mkdir -p "$EV/frames" "$EV/measure" "$EV/runs"
rm -rf "$EV/frames/$n" "$EV/measure/$n"
( cd "$WT" && build/bin/model_viewer --spell-test --client /mnt/projects/EverQuestP1999 \
    --eqe1 /mnt/projects/EverQuest_ETC1/willeq.eqe1 --plan "$plan" --frames-dir "$EV/frames/$n" \
    --fps 30 --seconds 13.2 --headless "$@" > "$EV/runs/$n.run.log" 2>&1 )
echo "render rc=$?" > "$EV/runs/$n.rc"
