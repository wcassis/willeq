#!/bin/bash
# B1163 (#482) rework: the B1152 missile timing gate on dev-u24 (local, per
# the session ENV). Usage: render_b1130gate_local.sh <model_viewer> <out_dir> [list.tsv]
# Run from the worktree root. Per spell: model_viewer --spell-test --camera
# b1130 --plan refactor/B1163/plans/b1130gate/s<spell>.tsv; the measurement is
# the FX-clock line "B1152 missile: ... launch@ reach@ land@ (cast, flight)"
# (Act-relative) plus, for 859, the "B1163 859 removal" line.
set -u
MV=$1; OUT=$2; LIST=${3:-refactor/B1163/plans/b1130gate/list.tsv}
PLANS=refactor/B1163/plans/b1130gate
PACK=/mnt/projects/EverQuest_ETC1/willeq.eqe1
CLIENT=/mnt/projects/EverQuestP1999
mkdir -p "$OUT"; LOG=$OUT/b1130gate.log; : > "$LOG"
while IFS=$'\t' read -r sid cms secs; do
  n="b1130_s${sid}"
  rm -rf "$OUT/$n"
  WILLEQ_DRM_DEVICE=/dev/dri/no-such-card "$MV" --spell-test --client "$CLIENT" --eqe1 "$PACK" \
      --plan "$PLANS/s${sid}.tsv" --frames-dir "$OUT/$n" --fps 30 --seconds "$secs" \
      --headless --camera b1130 --seed 1 > "$OUT/${n}.run.log" 2>&1
  rc=$?
  ml=$(grep -h "B1152 missile:" "$OUT/${n}.run.log" | head -1 | sed 's/.*B1152 missile:/B1152 missile:/')
  rl=$(grep -h "B1163 859" "$OUT/${n}.run.log" | head -1 | sed 's/.*B1163 859/B1163 859/')
  echo "spell ${sid}: rc=$rc ${ml:-NO_MISSILE_LINE} ${rl}" >> "$LOG"
done < "$LIST"
echo "B1130 GATE DONE" >> "$LOG"
