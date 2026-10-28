#!/usr/bin/env bash
# B1180 (#499): the re-gate for the sprite half-width work -- B1157's gate, one
# "after" label, at the merged tip (B1164's attach scale s + B1172's target
# scale + B1180):
#   tour7 = B1171's 7-row tour-7 (b1118 camera, WILLEQ_DRM_DEVICE=/dev/dri/no-such-card
#           surfaceless, B1166's anchor, oracle leg's level per
#           scripts/fx/capture_level.py b1118)
#   b1118 = the 40-record sweep (refactor/B1152/plans/sweep/list.tsv, b1118 camera,
#           surfaceless, level 60 on both sides per B1171 #490)
#   b1130 = the 40-record sweep (refactor/B1155 layout, B1166 anchor, i915 DRM depth 16
#           -- NO WILLEQ_DRM_DEVICE override, exactly B1171's runner -- and the
#           capture's level per scripts/fx/capture_level.py b1130, never the table's
#           class-level column, per B1171 #490)
#
# Frames are rendered ON the ThinkPad into /home/user/kb-frames/kb-b1180-sprite-width/gate/after/
# and stream back HERE per row (this host has no NFS, ~4 GB free, and numpy+cv2;
# the ThinkPad has no numpy/cv2): rsync the row's frames, measure, keep the
# small artifacts (run.log, timing.tsv, plan, the measure outputs) and free the
# frames again. The full frames stay on the ThinkPad at the path above.
# The binary runs in $RUN_CWD (config/ + data/ + B1152 plans rsynced from this
# worktree; the ThinkPad cannot reach GitLab, so no worktree there).
#
# usage: run_gate_b1180.sh <tour7|b1118|b1130|all> [tag|record ...]
set -u
SET=${1:?tour7|b1118|b1130|all}; shift 2>/dev/null || shift $#
TP=${TP:-thinkpad}
SLUG=kb-b1180-sprite-width
BIN=/home/user/willeq-builds/$SLUG/build/bin/model_viewer
RUN_CWD=/home/user/willeq-builds/$SLUG/run2
CLIENT=/mnt/projects/EverQuestP1999
PACK=/mnt/projects/EverQuest_ETC1/willeq.eqe1
GATE=/home/user/kb-frames/$SLUG/gate/after
EV=/home/user/kb-evidence/$SLUG/gate/after
RM=/mnt/projects/fable/review/B1139/scripts/harness_measure.py
ANCHOR=/mnt/projects/fable/review/B1130/scripts/anchor.py
TABLE=/mnt/projects/fable/review/B1130/record_table.tsv
LIST=$PWD/refactor/B1152/plans/sweep/list.tsv
MEASURE_B1130=$PWD/refactor/B1155/scripts/harness_measure_b1130.py
LVL="python3 -I $PWD/scripts/fx/capture_level.py"
mkdir -p "$EV/plans" "$EV/measure"
ssh -n "$TP" "mkdir -p '$GATE/frames' '$GATE/plans'" || exit 3
LOG="$EV/gate.$SET.log"

# stream: rsync the row's frames back, keep the small artifacts, free the frames.
stream() { # fd_remote tag
  local tag=$1 fd="$EV/frames/$1" m="$EV/measure/$1"
  mkdir -p "$fd" "$m"
  rsync -a --delete "$TP:$GATE/frames/$tag/" "$fd/" < /dev/null || { echo "[stream] $tag rsync FAILED" | tee -a "$LOG"; return 1; }
  cp "$fd/run.log" "$fd/timing.tsv" "$m/" 2>/dev/null
  cp "$EV/plans/$tag.plan" "$m/plan.tsv" 2>/dev/null
}

# render_row: tag cam secs plevel drm(0/1) [harness-level-check: 1 = require capture level]
render_row() { # tag cam secs plevel drm checklvl
  local tag=$1 cam=$2 secs=$3 plevel=$4 drm=$5 checklvl=$6 plan="$EV/plans/$tag.plan"
  if [ -s "$EV/measure/$tag/summary.tsv" ] || [ -s "$EV/measure/$tag/b1130_rows.tsv" ]; then
    echo "[$tag] already measured" >> "$LOG"; return 0
  fi
  # the caller wrote the full plan (header + one data row) to $plan
  rsync -a "$plan" "$TP:$GATE/plans/$tag.plan" < /dev/null || return 1
  echo "[$tag] ($cam, $secs s, level $plevel) rendering $(date -u +%T)" >> "$LOG"
  local drmcmd=""
  [ "$drm" = 1 ] && drmcmd="WILLEQ_DRM_DEVICE=/dev/dri/no-such-card"
  ssh -n "$TP" "mkdir -p '$GATE/frames/$tag'" || return 1
  if ! ssh -n "$TP" "cd '$RUN_CWD' && $drmcmd '$BIN' --spell-test --client '$CLIENT' --eqe1 '$PACK' \
      --plan '$GATE/plans/$tag.plan' --frames-dir '$GATE/frames/$tag' --fps 30 --seconds $secs \
      --sim-hz 60 --seed 1 --headless --camera $cam > '$GATE/frames/$tag/run.log' 2>&1"; then
    echo "[$tag] RENDER FAILED (see frames/$tag/run.log on the ThinkPad)" >> "$LOG"; return 1
  fi
  if ! stream "$tag"; then return 1; fi
  if [ "$checklvl" = 1 ]; then
    local hlevel
    hlevel=$($LVL harness "$EV/frames/$tag" --plan "$plan" 2>>"$LOG")
    local olevel
    olevel=$($LVL ${oracleset} "$oracleid" 2>>"$LOG")
    if ! $LVL check "$olevel" "$hlevel" >> "$LOG" 2>&1; then
      echo "[$tag] harness_level $hlevel REFUSED vs oracle_level $olevel -- not measured" >> "$LOG"
      return 1
    fi
  fi
  return 0
}

fail=0
if [ "$SET" = tour7 ] || [ "$SET" = all ]; then
  n=0
  for line in "93 other 1500" "38 other 1500" "54 other 1500" "656 other 1500" "41 other 1500" "253 self 1750" "458 other 3150"; do
    n=$((n + 1)); IFS=' ' read -r sid tgt cms <<< "$line"
    case " ${*:- } " in *" tour_$sid "*) : ;; *) [ $# -gt 0 ] && continue ;; esac
    tag=tour_$sid; oracleset=b1118; oracleid=$sid
    { [ -s "$EV/measure/$tag/summary.tsv" ] || [ -s "$EV/measure/$tag/b1130_rows.tsv" ]; } && continue
    olevel=$($LVL b1118 "$sid" </dev/null 2>>"$LOG") || { echo "[$tag] NO ORACLE LEVEL" >> "$LOG"; fail=$((fail+1)); continue; }
    secs=$(awk -v c="$cms" 'BEGIN{printf "%.1f", 1.5 + c/1000 + 11.5}')
    { printf 't_begin_s\tspell_id\ttarget\tdistance_units\tcast_ms\tcaster_level\n'; printf '1.5\t%s\t%s\t0\t%s\t%s\n' "$sid" "$tgt" "$cms" "$olevel"; } > "$EV/plans/$tag.plan"
    if render_row "$tag" b1118 "$secs" "$olevel" 1 1; then
      m="$EV/measure/$tag"
      if FX_KEEP=1 FX_SPELLS=auto python3 -P "$RM" "$EV/frames/$tag" "$m" < /dev/null > "$m/measure.log" 2>&1; then
        echo "[$tag] done $(date -u +%T) -> $(awk -F'\t' -v s="$sid" '$1 == s' "$m/summary.tsv")" >> "$LOG"
      else echo "[$tag] MEASURE FAILED (see $m/measure.log)" >> "$LOG"; fail=$((fail+1)); fi
      rm -rf "$EV/frames/$tag"
    else fail=$((fail+1)); fi
  done
fi
if [ "$SET" = b1118 ] || [ "$SET" = all ]; then
  n=0
  { awk -F'\t' 'NF>=4 {print}' "$LIST"; } |
  while IFS=$'\t' read -r kind sid tgt cms; do
    [ "$kind" = spell ] && continue
    case " ${*:- } " in *" $sid "*) : ;; *) [ $# -gt 0 ] && continue ;; esac
    n=$((n + 1)); tag="b1118_r${sid}"; oracleset=b1118; oracleid=$sid
    { [ -s "$EV/measure/$tag/summary.tsv" ] || [ -s "$EV/measure/$tag/b1130_rows.tsv" ]; } && continue
    secs=$(awk -v c="$cms" 'BEGIN{printf "%.1f", 1.5 + c/1000 + 11.5}' < /dev/null)
    { printf 't_begin_s\tspell_id\ttarget\tdistance_units\tcast_ms\tcaster_level\n'; printf '1.5\t%s\t%s\t0\t%s\t60\n' "$sid" "$tgt" "$cms"; } > "$EV/plans/$tag.plan"
    if render_row "$tag" b1118 "$secs" 60 1 0; then
      m="$EV/measure/$tag"
      if FX_KEEP=1 FX_SPELLS=auto python3 -P "$RM" "$EV/frames/$tag" "$m" < /dev/null > "$m/measure.log" 2>&1; then
        for st in SOURCE LANDING; do
          python3 "$ANCHOR" "$m/fable_${sid}" 640,605 235 "$st" < /dev/null > "$m/anchor_${st}.tsv" 2>&1 || echo "[$tag] anchor $st FAILED" >> "$LOG"
        done
        echo "[$tag] done $(date -u +%T)" >> "$LOG"
      else echo "[$tag] MEASURE FAILED (see $m/measure.log)" >> "$LOG"; fail=$((fail+1)); fi
      rm -rf "$EV/frames/$tag"
    else fail=$((fail+1)); fi
  done
fi
if [ "$SET" = b1130 ] || [ "$SET" = all ]; then
  n=0
  for rec in $(awk -F'\t' 'NR > 1 && $1 != 51 {print $1}' "$TABLE"); do
    case " ${*:- } " in *" r$rec "*) : ;; *" $rec "*) : ;; *) [ $# -gt 0 ] && continue ;; esac
    n=$((n + 1))
    line=$(awk -F'\t' -v r="$rec" '$1 == r {print $1"\t"$6"\t"$7"\t"$12"\t"$11"\t"$10}' "$TABLE")
    IFS=$'\t' read -r rec sid name captures ttype cms <<< "$line"
    kind=fixed_self; case "$captures" in *fixed_npc*) kind=fixed_npc ;; esac
    cam=b1130-self; [ "$kind" = fixed_npc ] && cam=b1130
    tgt=other; case "$ttype" in self|group|group-teleport) tgt=self ;; esac
    tag="b1130_r${rec}"; oracleset=b1130; oracleid=$rec
    { [ -s "$EV/measure/$tag/summary.tsv" ] || [ -s "$EV/measure/$tag/b1130_rows.tsv" ]; } && continue
    olevel=$($LVL b1130 "$rec" </dev/null 2>>"$LOG") || { echo "[$tag] NO ORACLE LEVEL" >> "$LOG"; fail=$((fail+1)); continue; }
    secs=$(awk -v c="$cms" 'BEGIN{printf "%.1f", 1.5 + c/1000 + 13.5}')
    { printf 't_begin_s\tspell_id\ttarget\tdistance_units\tcast_ms\tcaster_level\n'; printf '1.5\t%s\t%s\t0\t%s\t%s\n' "$sid" "$tgt" "$cms" "$olevel"; } > "$EV/plans/$tag.plan"
    if render_row "$tag" "$cam" "$secs" "$olevel" 0 1; then
      m="$EV/measure/$tag"
      if FX_KEEP=1 python3 -P "$MEASURE_B1130" "$EV/frames/$tag" "$m" "$kind" < /dev/null > "$m/measure.log" 2>&1; then
        echo "[$tag] (kind $kind, level $olevel) done $(date -u +%T)" >> "$LOG"
      else echo "[$tag] MEASURE FAILED (see $m/measure.log)" >> "$LOG"; fail=$((fail+1)); fi
      rm -rf "$EV/frames/$tag"
    else fail=$((fail+1)); fi
  done
fi
echo "gate.$SET finished $(date -u +%FT%T): fails=$fail" >> "$LOG"
tail -1 "$LOG"
