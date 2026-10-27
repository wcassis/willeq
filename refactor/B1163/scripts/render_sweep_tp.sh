#!/bin/bash
# B1163 (#482): the 40-record b1118 rendered sweep (D). RUN ON THE THINKPAD.
# CWD must be the WORKTREE (where data/eqe1_texture_index.json lives), not the
# build dir, or the EQE1 texture index is unresolvable (FATAL on dafft0001.bmp).
set -u
WT=/home/user/src/willeq-wt/kb-b1163-missile-fu
T=/home/user/willeq-builds/kb-b1163-missile-fu/build
F=/home/user/kb-frames/kb-b1163-missile-fu
PLANS=$F/plans/sweep
PACK=/mnt/projects/EverQuest_ETC1/willeq.eqe1
CLIENT=/mnt/projects/EverQuestP1999
LOG=$F/render.log
mkdir -p "$F"
: > "$LOG"
while IFS=$'\t' read -r rec sid tgt cms; do
  n="r${rec}_${sid}"
  p="$PLANS/${n}.tsv"
  [ -f "$p" ] || { echo "$(date -u +%FT%TZ) $n MISSING PLAN" >> "$LOG"; continue; }
  secs=$(python3 -c "print(round(1.5 + $cms/1000.0 + 11.5, 1))")
  rm -rf "$F/$n"
  echo "$(date -u +%FT%TZ) render $n start secs=$secs" >> "$LOG"
  ( cd "$WT" && WILLEQ_DRM_DEVICE=/dev/dri/no-such-card \
    "$T/bin/model_viewer" --spell-test --client "$CLIENT" --eqe1 "$PACK" \
      --plan "$p" --frames-dir "$F/$n" --fps 30 --seconds "$secs" \
      --headless --camera b1118 --seed 1 > "$F/${n}.run.log" 2>&1 )
  rc=$?
  echo "$(date -u +%FT%TZ) render $n rc=$rc frames=$(ls "$F/$n"/*.png 2>/dev/null | wc -l)" >> "$LOG"
done < "$PLANS/list.tsv"
echo "$(date -u +%FT%TZ) SWEEP DONE" >> "$LOG"
