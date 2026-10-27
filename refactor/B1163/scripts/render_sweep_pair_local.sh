#!/bin/bash
# B1163 (#482) rework: render sweep records with one model_viewer on dev-u24.
# Usage: render_sweep_pair_local.sh <model_viewer> <out_dir> <rec_spell>...   (e.g. r1_200)
# Same flags as render_sweep_tp.sh (--camera b1118, seed 1, 30 fps); run from the worktree.
set -u
MV=$1; OUT=$2; shift 2
PLANS=refactor/B1163/plans/sweep
mkdir -p "$OUT"
for n in "$@"; do
  cms=$(awk -F'\t' -v r="${n%%_*}" 'NR>1 && $1 ~ /^[0-9.]+$/ {print $5}' "$PLANS/$n.tsv" | head -1)
  secs=$(python3 -c "print(round(1.5 + $cms/1000.0 + 11.5, 1))")
  rm -rf "$OUT/$n"
  WILLEQ_DRM_DEVICE=/dev/dri/no-such-card "$MV" --spell-test --client /mnt/projects/EverQuestP1999 \
      --eqe1 /mnt/projects/EverQuest_ETC1/willeq.eqe1 --plan "$PLANS/$n.tsv" --frames-dir "$OUT/$n" \
      --fps 30 --seconds "$secs" --headless --camera b1118 --seed 1 > "$OUT/$n.run.log" 2>&1
  echo "$n rc=$? secs=$secs frames=$(ls "$OUT/$n"/*.png 2>/dev/null | wc -l)" >> "$OUT/render.log"
done
