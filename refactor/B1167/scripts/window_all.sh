#!/usr/bin/env bash
# B1167: window_table.py over every matrix run (harness: OP_Action at frame 97.5 = 1.5 s + 1.75 s at
# 30 fps, landing span frames 98..330, tail baseline frames 470..500) -> runs/window_table.tsv
D=$(cd "$(dirname "$0")" && pwd); R=/mnt/projects/fable/review/B1167/worker/runs
out=$R/window_table.tsv; first=1
for d in "$R"/mx_*; do
  [ -s "$d/frames/frame_00500.png" ] || continue
  python3 -P "$D/window_table.py" "$(basename "$d")" "$d/frames/frame_{f:05d}.png" 97.5 30 98 330 470 500 > "$d/window.tsv" || continue
  if [ $first = 1 ]; then cat "$d/window.tsv"; first=0; else tail -1 "$d/window.tsv"; fi
done > "$out"
cat "$out"
