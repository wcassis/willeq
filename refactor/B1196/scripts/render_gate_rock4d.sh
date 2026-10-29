#!/usr/bin/env bash
# B1196 (#515): the 17 gate rows (tour-7 + b1130 r0-r7,r9,r10) rendered on the
# Rock 4D (SBC lane 3, scripts/pi/env.sh) with the ci-arm64 publish for the
# SAME fable tip the ThinkPad surfaceless baseline was rendered on
# (8c2bfe7c2, /mnt/scratch/gate-baseline/8c2bfe7c-sl/), with the SAME plans,
# seed, sim-hz and camera presets as /mnt/projects/fable/review/GATE-BASELINE/
# render_gate.sh, and measured ON hermes-ext with the same scripts (RM /
# ANCHOR / MEASURE_B1130 / LVL all resolved exactly as render_gate.sh does).
#
# Differences from render_gate.sh, all named:
#   TP       = lane 3 (PI_LANE=3 -> scripts/pi/env.sh; the card names the LANE,
#              never an address), not the thinkpad alias.
#   BIN      = /mnt/projects/fable/ci-arm64/<sha>/bin/model_viewer (the
#              ci-arm64/<sha> publish; docs/ci.md "The aarch64 SBC lanes":
#              lanes 2-3 run the scripts/build-arm64-noble.sh binary, lane 3's
#              Armbian resolute runs it, system Mesa).
#   RUN_CWD  = the publish root: its config/ and data/ are byte-identical to
#              the worktree at <sha> (diff -rq, device_info.txt), so the run
#              reads the same config bytes as the baseline run.
#   TPF      = /tmp on the device (5.9 G tmpfs; the lane env.sh I/O rule: all
#              timed-run output goes to tmpfs, never NFS/SD/eMMC).
#   time.txt = per-row T0/T1 (date +%s.%N), loadavg before/after, and bash
#              `time` real/user/sys for the render process (the CPU-per-frame
#              input; render_gate.sh only times the ssh call).
#   CONC     = 3, matching the baseline's row_times.tsv conc column.
#   measure  = $EVHOME/measure (~/kb-evidence/<slug>/, per the card).
# Everything else (plan bytes, --fps 30 --sim-hz 60 --seed 1 --headless,
# camera, the seconds formula, WILLEQ_DRM_DEVICE=/dev/dri/no-such-card
# surfaceless on every row, the level check, the measure invocations) is
# render_gate.sh's.
#
# usage: PI_LANE=3 render_gate_rock4d.sh   (from the worktree)
set -u
export PI_LANE=3
: "${SLUG:=kb-b1196-rock4d-render}"
: "${SHA:=8c2bfe7c2daa45360c57003a9e15971b77507bfc}"
WT=$(cd "$(dirname "$0")/../../.." && pwd)
# shellcheck disable=SC1091
source "$WT/scripts/pi/env.sh"   # PI_HOST / PI_USER / pi_remote
export -f pi_remote
export PI_SSH_OPTS PI_USER PI_HOST
BIN=${BIN:-${PI_LANE_PUBLISH}/${SHA}/bin/model_viewer}
PUB=${BIN%/bin/model_viewer}
EV=${EV:-/mnt/scratch/kb-evidence/$SLUG/gate}
EVHOME=${EVHOME:-$HOME/kb-evidence/$SLUG}
TPF=${TPF:-/tmp/$SLUG-frames}
CONC=${CONC:-3}
CLIENT=/mnt/projects/EverQuestP1999
PACK=/mnt/projects/EverQuest_ETC1/willeq.eqe1
LF=${LF:-/home/user/kb-frames-tmp/$SLUG/rock4d}
RM=${RM:-/mnt/projects/fable/review/B1139/scripts/harness_measure.py}
ANCHOR=${ANCHOR:-/mnt/projects/fable/review/B1130/scripts/anchor.py}
TABLE=${TABLE:-/mnt/projects/fable/review/B1130/record_table.tsv}
MEASURE_B1130=$WT/refactor/B1155/scripts/harness_measure_b1130.py
LVL="python3 -I $WT/scripts/fx/capture_level.py"
ROWS=${ROWS:-$EV/rows.rock4d.tsv}
LOG=${LOG:-$EVHOME/logs/gate.rock4d.log}
mkdir -p "$EV/plans" "$EVHOME/measure" "$EVHOME/logs" "$LF"
echo "render_gate_rock4d tree=$WT bin=$BIN conc=$CONC $(date -u +%FT%T) tip=$(git -C "$WT" rev-parse HEAD)" >> "$LOG"

row_job() {
  local tag=$1 cam=$2 secs=$3 mk=$4 kind=$5 lvlcheck=$6 olevel=$7 sid=$8
  local plan="$EV/plans/$tag.plan" m="$EVHOME/measure/$tag" tfd="$TPF/frames/$tag"
  local fkey="$m/summary.tsv" mkey="$m/b1130_rows.tsv"
  [ "$mk" = summary ] && mkey="$fkey"
  if [ -s "$fkey" ] || [ -s "$mkey" ]; then echo "[$tag] already measured" >> "$LOG"; return 0; fi
  # the device is single-tenant under the lane lock; just never run against a
  # loaded box (a 12-minute row at load 16 is not like-with-like).
  local waited=0 ld
  while :; do
    ld=$(pi_remote "cut -d. -f1 /proc/loadavg" 2>/dev/null || echo 0)
    [ "${ld%%.*}" -gt 16 ] 2>/dev/null || break
    [ $waited -eq 0 ] && echo "[$tag] waiting: load=$ld $(date -u +%T)" >> "$LOG"
    waited=$((waited+60)); sleep 60
  done
  pi_remote "mkdir -p '$TPF/plans'" < /dev/null
  rsync -a "$plan" "${PI_USER}@${PI_HOST}:$TPF/plans/$tag.plan" < /dev/null \
    || { echo "[$tag] PLAN SYNC FAILED" >> "$LOG"; return 1; }
  echo "[$tag] ($cam, $secs s, level $olevel, $mk/$kind) rendering $(date -u +%T)" >> "$LOG"
  # surfaceless on every row: render_gate.sh's drm=1 for the tour and b1130
  # rows (WILLEQ_DRM_DEVICE=/dev/dri/no-such-card -> surfaceless EGL).
  local rc
  rc=$(pi_remote "mkdir -p '$tfd' && cd '$PUB' && ( { echo T0=\$(date +%s.%N); \
    echo LOAD=\$(cat /proc/loadavg); \
    WILLEQ_DRM_DEVICE=/dev/dri/no-such-card time '$BIN' --spell-test --client '$CLIENT' --eqe1 '$PACK' \
      --plan '$TPF/plans/$tag.plan' --frames-dir '$tfd' --fps 30 --seconds $secs \
      --sim-hz 60 --seed 1 --headless --camera $cam > '$tfd/run.log' 2>&1; \
    echo EXIT=\$?; echo T1=\$(date +%s.%N); echo LOAD=\$(cat /proc/loadavg); \
    } > '$tfd/time.txt' 2>&1 ); grep EXIT= '$tfd/time.txt'" < /dev/null)
  case "$rc" in
    EXIT=0) : ;;
    *) echo "[$tag] RENDER FAILED rc=$rc (run.log on the device: $tfd/run.log)" >> "$LOG"; return 1 ;;
  esac
  local fd="$LF/$tag"; mkdir -p "$fd"
  rsync -a --delete "${PI_USER}@${PI_HOST}:$tfd/" "$fd/" < /dev/null \
    || { echo "[$tag] FRAME SYNC FAILED" >> "$LOG"; return 1; }
  pi_remote "rm -rf '$tfd'" < /dev/null
  mkdir -p "$m"; cp "$fd/time.txt" "$m/" 2>/dev/null; cp "$fd/run.log" "$m/run.log" 2>/dev/null
  if [ "$lvlcheck" = 1 ]; then
    local hlevel
    hlevel=$(cd "$WT" && $LVL harness "$fd" --plan "$plan" 2>>"$LOG")
    if ! (cd "$WT" && $LVL check "$olevel" "$hlevel") >> "$LOG" 2>&1; then
      echo "[$tag] harness_level $hlevel REFUSED vs oracle_level $olevel -- not measured" >> "$LOG"
      rm -rf "$fd"; return 1; fi
  fi
  if [ "$mk" = summary ]; then
    if ! (cd "$WT" && FX_KEEP=1 FX_SPELLS=auto python3 -I "$RM" "$fd" "$m" > "$m/measure.log" 2>&1); then
      echo "[$tag] MEASURE FAILED (see $m/measure.log)" >> "$LOG"; rm -rf "$fd"; return 1; fi
    for st in SOURCE LANDING; do
      (cd "$WT" && python3 "$ANCHOR" "$m/fable_$sid" 640,605 235 "$st") > "$m/anchor_${st}.tsv" 2>&1 \
        || echo "[$tag] anchor $st FAILED" >> "$LOG"
    done
  else
    if ! (cd "$WT" && FX_KEEP=1 python3 -I "$MEASURE_B1130" "$fd" "$m" "$kind" > "$m/measure.log" 2>&1); then
      echo "[$tag] MEASURE FAILED (see $m/measure.log)" >> "$LOG"; rm -rf "$fd"; return 1; fi
  fi
  local nfr
  nfr=$(ls "$fd"/*.png 2>/dev/null | wc -l)
  rm -rf "$fd"
  cp "$m/time.txt" "$EVHOME/logs/$tag.time.txt" 2>/dev/null
  local t0 t1 wall
  t0=$(awk -F= '/^T0=/{print $2}' "$m/time.txt")
  t1=$(awk -F= '/^T1=/{print $2}' "$m/time.txt")
  wall=$(awk -v a="$t0" -v b="$t1" 'BEGIN{printf "%.1f", b-a}')
  printf '%s\t%s\t%s\t%s\t%s\n' "$tag" "$nfr" "$wall" "$CONC" "$(date -u +%FT%T)" >> "$EVHOME/logs/row_times.tsv"
  echo "[$tag] done $(date -u +%T) render ${wall} s" >> "$LOG"
  return 0
}
export -f row_job
export WT EV EVHOME LF RM ANCHOR MEASURE_B1130 LVL LOG TPF CONC CLIENT PACK PI_USER PI_HOST PUB BIN

: > "$ROWS"
[ -s "$EVHOME/logs/row_times.tsv" ] || printf 'tag\tframes\trender_s\tconc\tfinished\n' > "$EVHOME/logs/row_times.tsv"

# ---- the 17 rows, render_gate.sh's row definitions verbatim ----
for line in "93 other 1500" "38 other 1500" "54 other 1500" "656 other 1500" "41 other 1500" "253 self 1750" "458 other 3150"; do
  IFS=' ' read -r sid tgt cms <<< "$line"
  tag=tour_$sid
  olevel=$($LVL b1118 "$sid" </dev/null 2>>"$LOG") || { echo "[$tag] NO ORACLE_LEVEL" >> "$LOG"; continue; }
  secs=$(awk -v c="$cms" 'BEGIN{printf "%.1f", 1.5 + c/1000 + 11.5}' < /dev/null)
  { printf 't_begin_s\tspell_id\ttarget\tdistance_units\tcast_ms\tcaster_level\n'; printf '1.5\t%s\t%s\t0\t%s\t%s\n' "$sid" "$tgt" "$cms" "$olevel"; } > "$EV/plans/$tag.plan"
  printf '%s\t%s\t%s\t%s\t%s\t1\t%s\t%s\n' "$tag" "b1118" "$secs" "summary" "$sid" "$olevel" "$sid" >> "$ROWS"
done
for rec in 0 1 2 3 4 5 6 7 9 10; do
  line=$(awk -F'\t' -v r="$rec" '$1 == r {print $1"\t"$6"\t"$7"\t"$12"\t"$11"\t"$10}' "$TABLE")
  IFS=$'\t' read -r rec sid name captures ttype cms <<< "$line"
  kind=fixed_self; case "$captures" in *fixed_npc*) kind=fixed_npc ;; esac
  cam=b1130-self; [ "$kind" = fixed_npc ] && cam=b1130
  tgt=other; case "$ttype" in self|group|group-teleport) tgt=self ;; esac
  tag="b1130_r${rec}"
  olevel=$($LVL b1130 "$rec" </dev/null 2>>"$LOG") || { echo "[$tag] NO ORACLE_LEVEL" >> "$LOG"; continue; }
  secs=$(awk -v c="$cms" 'BEGIN{printf "%.1f", 1.5 + c/1000 + 13.5}' < /dev/null)
  { printf 't_begin_s\tspell_id\ttarget\tdistance_units\tcast_ms\tcaster_level\n'; printf '1.5\t%s\t%s\t0\t%s\t%s\n' "$sid" "$tgt" "$cms" "$olevel"; } > "$EV/plans/$tag.plan"
  printf '%s\t%s\t%s\t%s\t%s\t1\t%s\t%s\n' "$tag" "$cam" "$secs" "b1130" "$kind" "$olevel" "$sid" >> "$ROWS"
done
nrows=$(wc -l < "$ROWS")
echo "gate.rock4d: $nrows rows, concurrency $CONC, $(date -u +%FT%T)" >> "$LOG"
T0=$(date +%s)
xargs -P "$CONC" -n 8 -a "$ROWS" bash -c 'row_job "$@"' _ >> "$LOG" 2>&1
ok=$(find "$EVHOME/measure" -maxdepth 2 \( -name summary.tsv -o -name b1130_rows.tsv \) | wc -l)
pi_remote "rm -rf '$TPF'" < /dev/null   # leave the device as found
echo "gate.rock4d finished $(date -u +%FT%T): rows=$nrows measured_total=$ok wall=$(( $(date +%s) - T0 )) s" >> "$LOG"
tail -1 "$LOG"
