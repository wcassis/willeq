#!/bin/bash
# B1163 (#482): the B1152 missile timing gate re-run (B). RUN ON THE THINKPAD.
# model_viewer --camera b1130 per gate spell; the measurement is the FX-clock
# log line "B1152 missile: ... launch@ reach@ land@ (cast, flight)" (Act-relative),
# compared to missile_oracle.tsv Act-relative values. CWD = the worktree.
set -u
WT=/home/user/src/willeq-wt/kb-b1163-missile-fu
T=/home/user/willeq-builds/kb-b1163-missile-fu/build
F=/home/user/kb-frames/kb-b1163-missile-fu
PLANS=$F/plans/b1130gate
PACK=/mnt/projects/EverQuest_ETC1/willeq.eqe1
CLIENT=/mnt/projects/EverQuestP1999
LOG=$F/b1130gate.log
mkdir -p "$F"
: > "$LOG"
while IFS=$'\t' read -r sid cms secs; do
  n="b1130_s${sid}"
  p="$PLANS/s${sid}.tsv"
  [ -f "$p" ] || { echo "$(date -u +%FT%TZ) $n MISSING PLAN" >> "$LOG"; continue; }
  rm -rf "$F/$n"
  echo "$(date -u +%FT%TZ) render $n start secs=$secs" >> "$LOG"
  ( cd "$WT" && WILLEQ_DRM_DEVICE=/dev/dri/no-such-card \
    "$T/bin/model_viewer" --spell-test --client "$CLIENT" --eqe1 "$PACK" \
      --plan "$p" --frames-dir "$F/$n" --fps 30 --seconds "$secs" \
      --headless --camera b1130 --seed 1 > "$F/${n}.run.log" 2>&1 )
  rc=$?
  ml=$(grep "B1152 missile" "$F/${n}.run.log" 2>/dev/null | head -1)
  echo "spell ${sid}: rc=$rc ${ml:-NO_MISSILE_LINE}" >> "$LOG"
done < "$PLANS/list.tsv"
echo "$(date -u +%FT%TZ) B1130 GATE DONE" >> "$LOG"
