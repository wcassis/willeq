#!/usr/bin/env bash
# B1186 (#505): render + measure the landing_cov gate rows on THIS tree.
#
# The B1184 runner (review/B1184/worker/refactor-B1184, /mnt/scratch/kb-
# evidence/kb-b1184-gate-tip/scripts/run_gate_b1184.sh) is the template; the
# differences:
#   - the binary comes from THIS worktree's rbuild tree (B1087: the worktree
#     is NFS-mounted on the ThinkPad at the same path, the build tree is
#     /home/user/willeq-builds/<slug>/build)
#   - the sets are tour7 + b1130 (the B1186 card's before/after scope; the
#     b1118 40-record sweep is not in scope)
#   - the frames STREAM from the ThinkPad to /mnt/scratch (persistent, the
#     card-mandated frames location -- never ~) before the ThinkPad copy is
#     freed; the measure then runs HERE off /mnt/scratch (numpy/cv2)
#
# The judging step is separate: refactor/B1186/scripts/unify_window.py over
# these measure outputs (the window unification + before/after table).
#
# usage: run_b1186.sh <tour7|b1130|all> [spell|record ...]
#        CONC=<n>  (default 3 parallel rows)
set -u
SET=${1:?tour7|b1130|all}; shift 2>/dev/null || shift $#
TP=${TP:-thinkpad}
CONC=${CONC:-3}
WT=/home/user/src/willeq-wt/kb-b1186-landing-window        # NFS-mounted on the ThinkPad at the same path (B1087)
BIN=/home/user/willeq-builds/kb-b1186-landing-window/build/bin/model_viewer
RUN_CWD=$WT
CLIENT=/mnt/projects/EverQuestP1999
PACK=/mnt/projects/EverQuest_ETC1/willeq.eqe1
TPF=/home/user/kb-frames/kb-b1186-landing-window          # TRANSIENT on the ThinkPad
FR=/mnt/scratch/kb-b1186-landing-window/frames            # PERSISTENT frames (the card: /mnt/scratch, never ~)
EV=/mnt/scratch/kb-evidence/kb-b1186-landing-window       # plans, measure, tables, logs
RM=/mnt/projects/fable/review/B1139/scripts/harness_measure.py
ANCHOR=/mnt/projects/fable/review/B1130/scripts/anchor.py
TABLE=/mnt/projects/fable/review/B1130/record_table.tsv
MEASURE_B1130=$WT/refactor/B1155/scripts/harness_measure_b1130.py
LVL="python3 -I $WT/scripts/fx/capture_level.py"
mkdir -p "$EV/plans" "$EV/measure" "$FR"
ssh -n "$TP" "mkdir -p '$TPF/frames' '$TPF/plans'" || exit 3
LOG="$EV/gate.$SET.log"

# row_job: tag cam secs drm mk summary|b1130 kind lvlcheck olevel sid
row_job() {
  local tag=$1 cam=$2 secs=$3 drm=$4 mk=$5 kind=$6 lvlcheck=$7 olevel=$8 sid=$9
  local plan="$EV/plans/$tag.plan" m="$EV/measure/$tag" fd="$FR/$tag"
  local fkey="$m/summary.tsv" mkey="$m/b1130_rows.tsv"
  [ "$mk" = summary ] && mkey="$fkey"
  if [ -s "$fkey" ] || [ -s "$mkey" ]; then
    echo "[$tag] already measured" >> "$LOG"; return 0
  fi
  local drmcmd=""
  [ "$drm" = 1 ] && drmcmd="WILLEQ_DRM_DEVICE=/dev/dri/no-such-card"
  rsync -a "$plan" "$TP:$TPF/plans/$tag.plan" < /dev/null || { echo "[$tag] PLAN SYNC FAILED" >> "$LOG"; return 1; }
  echo "[$tag] ($cam, $secs s, level $olevel) rendering $(date -u +%T)" >> "$LOG"
  if ! ssh -n "$TP" "mkdir -p '$TPF/frames/$tag' && cd '$RUN_CWD' && $drmcmd '$BIN' --spell-test --client '$CLIENT' --eqe1 '$PACK' \
      --plan '$TPF/plans/$tag.plan' --frames-dir '$TPF/frames/$tag' --fps 30 --seconds $secs \
      --sim-hz 60 --seed 1 --headless --camera $cam > '$TPF/frames/$tag/run.log' 2>&1"; then
    echo "[$tag] RENDER FAILED (see ThinkPad $TPF/frames/$tag/run.log)" >> "$LOG"; return 1
  fi
  # stream the frames to /mnt/scratch (persistent) and free the ThinkPad copy
  mkdir -p "$fd"
  if ! rsync -a --delete "$TP:$TPF/frames/$tag/" "$fd/" < /dev/null; then
    echo "[$tag] STREAM FAILED" >> "$LOG"; return 1
  fi
  ssh -n "$TP" "rm -rf '$TPF/frames/$tag'" < /dev/null
  cp "$fd/timing.tsv" "$m/" 2>/dev/null
  if [ "$lvlcheck" = 1 ]; then
    local hlevel
    hlevel=$($LVL harness "$fd" --plan "$plan" 2>>"$LOG")
    if ! $LVL check "$olevel" "$hlevel" >> "$LOG" 2>&1; then
      echo "[$tag] harness_level $hlevel REFUSED vs oracle_level $olevel -- not measured" >> "$LOG"; return 1
    fi
  fi
  mkdir -p "$m"
  if [ "$mk" = summary ]; then
    if FX_KEEP=1 FX_SPELLS=auto python3 -I "$RM" "$fd" "$m" < /dev/null > "$m/measure.log" 2>&1; then
      for st in SOURCE LANDING; do
        python3 "$ANCHOR" "$m/fable_$sid" 640,605 235 "$st" < /dev/null > "$m/anchor_${st}.tsv" 2>&1 || echo "[$tag] anchor $st FAILED" >> "$LOG"
      done
      echo "[$tag] done $(date -u +%T) -> $(awk -F'\t' -v s="$sid" '$1 == s' "$m/summary.tsv" | head -1)" >> "$LOG"
    else echo "[$tag] MEASURE FAILED (see $m/measure.log)" >> "$LOG"; return 1; fi
  else
    if FX_KEEP=1 python3 -I "$MEASURE_B1130" "$fd" "$m" "$kind" < /dev/null > "$m/measure.log" 2>&1; then
      echo "[$tag] (kind $kind) done $(date -u +%T)" >> "$LOG"
    else echo "[$tag] MEASURE FAILED (see $m/measure.log)" >> "$LOG"; return 1; fi
  fi
  return 0
}
export -f row_job
export WT BIN RUN_CWD CLIENT PACK TPF FR EV RM ANCHOR MEASURE_B1130 LOG LVL TP

# rows: tag cam secs drm mk kind lvlcheck olevel sid
ROWS="$EV/rows.$SET.tsv"
: > "$ROWS"
if [ "$SET" = tour7 ] || [ "$SET" = all ]; then
  for line in "93 other 1500" "38 other 1500" "54 other 1500" "656 other 1500" "41 other 1500" "253 self 1750" "458 other 3150"; do
    IFS=' ' read -r sid tgt cms <<< "$line"
    case " ${*:- } " in *" tour_$sid "*) : ;; *"$sid"*) : ;; *) [ $# -gt 0 ] && continue ;; esac
    tag=tour_$sid
    olevel=$($LVL b1118 "$sid" </dev/null 2>>"$LOG") || { echo "[$tag] NO ORACLE LEVEL" >> "$LOG"; continue; }
    secs=$(awk -v c="$cms" 'BEGIN{printf "%.1f", 1.5 + c/1000 + 11.5}' < /dev/null)
    { printf 't_begin_s\tspell_id\ttarget\tdistance_units\tcast_ms\tcaster_level\n'; printf '1.5\t%s\t%s\t0\t%s\t%s\n' "$sid" "$tgt" "$cms" "$olevel"; } > "$EV/plans/$tag.plan"
    printf '%s\tb1118\t%s\t1\tsummary\t%s\t1\t%s\t%s\n' "$tag" "$secs" "$sid" "$olevel" "$sid" >> "$ROWS"
  done
fi
if [ "$SET" = b1130 ] || [ "$SET" = all ]; then
  for rec in $(awk -F'\t' 'NR > 1 && $1 != 51 {print $1}' "$TABLE"); do
    case " ${*:- } " in *" r$rec "*) : ;; *"$rec"*) : ;; *) [ $# -gt 0 ] && continue ;; esac
    line=$(awk -F'\t' -v r="$rec" '$1 == r {print $1"\t"$6"\t"$7"\t"$12"\t"$11"\t"$10}' "$TABLE")
    IFS=$'\t' read -r rec sid name captures ttype cms <<< "$line"
    kind=fixed_self; case "$captures" in *fixed_npc*) kind=fixed_npc ;; esac
    cam=b1130-self; [ "$kind" = fixed_npc ] && cam=b1130
    tgt=other; case "$ttype" in self|group|group-teleport) tgt=self ;; esac
    tag="b1130_r${rec}"
    olevel=$($LVL b1130 "$rec" </dev/null 2>>"$LOG") || { echo "[$tag] NO ORACLE LEVEL" >> "$LOG"; continue; }
    secs=$(awk -v c="$cms" 'BEGIN{printf "%.1f", 1.5 + c/1000 + 13.5}' < /dev/null)
    { printf 't_begin_s\tspell_id\ttarget\tdistance_units\tcast_ms\tcaster_level\n'; printf '1.5\t%s\t%s\t0\t%s\t%s\n' "$sid" "$tgt" "$cms" "$olevel"; } > "$EV/plans/$tag.plan"
    printf '%s\t%s\t%s\t0\tb1130\t%s\t1\t%s\t%s\n' "$tag" "$cam" "$secs" "$kind" "$olevel" "$sid" >> "$ROWS"
  done
fi
nrows=$(wc -l < "$ROWS")
echo "gate.$SET: $nrows rows, concurrency $CONC, $(date -u +%FT%T)" >> "$LOG"
xargs -P "$CONC" -n 9 -a "$ROWS" bash -c 'row_job "$@"' _ >> "$LOG" 2>&1
ok=$(find "$EV/measure" -maxdepth 2 \( -name summary.tsv -o -name b1130_rows.tsv \) | wc -l)
echo "gate.$SET finished $(date -u +%FT%T): rows=$nrows measured_total=$ok" >> "$LOG"
tail -2 "$LOG"
