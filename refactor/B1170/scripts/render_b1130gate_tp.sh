#!/bin/bash
# B1170 (#489): the b1130 gate spells in the harness, before/after. RUN ON THE
# THINKPAD (ssh 172.16.200.205 bash <this> ...): the worktree is NFS-mounted at
# the same path there, and the frames land on the NFS evidence tree.
# usage: render_b1130gate_tp.sh <model_viewer> <out_dir> [list.tsv]
#   list.tsv rows: spell_id <tab> cast_ms <tab> seconds (default: B1163's list,
#   refactor/B1163/plans/b1130gate/list.tsv; plans are B1163's s<id>.tsv).
# Per spell: <out_dir>/b1130_s<id>/ (30 fps frames) + b1130_s<id>.run.log, and
# one summary line in <out_dir>/b1130gate.log: rc, the createLanding line and
# whether any B1152 missile / B1170 missile-object line was printed.
set -u
BIN=$1; OUT=$2
WT=/mnt/projects/fable/main/wt/kb-b1170-missile-exact
PLANS=$WT/refactor/B1163/plans/b1130gate
LIST=${3:-$PLANS/list.tsv}
PACK=/mnt/projects/EverQuest_ETC1/willeq.eqe1
CLIENT=/mnt/projects/EverQuestP1999
LOG=$OUT/b1130gate.log
mkdir -p "$OUT"
: > "$LOG"
while IFS=$'\t' read -r sid cms secs; do
  [ -z "${sid:-}" ] && continue
  n="b1130_s${sid}"
  p="$PLANS/s${sid}.tsv"
  [ -f "$p" ] || { echo "spell ${sid}: MISSING PLAN $p" >> "$LOG"; continue; }
  rm -rf "$OUT/$n"
  ( cd "$WT" && WILLEQ_DRM_DEVICE=/dev/dri/no-such-card \
    "$BIN" --spell-test --client "$CLIENT" --eqe1 "$PACK" \
      --plan "$p" --frames-dir "$OUT/$n" --fps 30 --seconds "$secs" \
      --headless --camera b1130 --seed 1 > "$OUT/${n}.run.log" 2>&1 )
  rc=$?
  ml=$(grep -c "B1152 missile" "$OUT/${n}.run.log")
  mo=$(grep -c "B1170 missile" "$OUT/${n}.run.log")
  ll=$(grep "createLanding" "$OUT/${n}.run.log" | head -1 | sed 's/^.*createLanding/createLanding/')
  echo "spell ${sid}: rc=$rc b1152_missile_lines=$ml b1170_missile_lines=$mo frames=$(ls "$OUT/$n" | wc -l) | ${ll:-NO_LANDING_LINE}" >> "$LOG"
done < "$LIST"
echo "B1130 GATE DONE" >> "$LOG"
