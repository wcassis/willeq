#!/usr/bin/env bash
# B1157 (#476): tour-7 + the 40 B1130 records on --camera b1118, the ANIMATED
# caster (the spell's CastingAnim track through the client's own code,
# spell_cast_animation.h -- started at BeginCast, held for the channel, ended
# at OP_Action) -- re-gated against the B1152 runs (the pre-B1157 IDLE caster:
# the o01 loop), committed in refactor/B1152/runs/. Render on the ThinkPad
# (the rbuild tree and the client files live there; the 4 GiB scope here
# cannot hold the frames), ALL frames + video.mkv kept in $EV (B1158's visual
# ledger consumes them), measured HERE with B1139's harness_measure.py
# (video.mkv in the measure root) and B1130's anchor.py motion signatures
# (rig b1118: feet 640,605, model 235).
#
# The plans copy B1152's own (refactor/B1152/plans/sweep/, the record's
# representative cast_ms from review/B1130/record_table.tsv, t_begin 1.5 s,
# target distance 0, caster level 60) so the only variable between the two
# gates is the caster's animation.
#
# usage: run_sweep_b1118.sh [tour | sweep | all]   (default: all)
#   tour  = the seven tour spells (93 38 54 656 41 253 458), tag b1157_tour_<sid>
#   sweep = the 40 records, tag b1157_r<rec>_<sid> (B1152's tag minus the prefix)
# Progress: $EV/sweep.log (one line per run), exit status in $EV/sweep.exit
# (0 = every run rendered + measured). Re-running an existing tag is skipped
# (its measure/summary.tsv must be present).
set -u
EV=/home/user/kb-evidence/kb-b1157-cast-anim/b1118
TP_BASE=/home/user/kb-frames/kb-b1157-cast-anim
BIN=/home/user/willeq-builds/kb-b1157-cast-anim/build/bin/model_viewer
CLIENT=/mnt/projects/EverQuestP1999
PACK=/mnt/projects/EverQuest_ETC1/willeq.eqe1
REPO=/home/user/src/willeq-wt/kb-b1157-cast-anim
RM=/mnt/projects/fable/review/B1139/scripts/harness_measure.py
ANCHOR=/mnt/projects/fable/review/B1130/scripts/anchor.py
PY3=/home/user/.venvs/b1155/bin/python3  # harness_measure.py + fx_measure.py (cv2)
PYV=/home/user/.venvs/b1155/bin/python  # anchor.py's numpy/cv2
FPS=30
SEED=1

mkdir -p "$EV/plans" "$EV/frames" "$EV/measure"
ssh thinkpad "mkdir -p '$TP_BASE/plans'"

# $1 tag  $2 spell_id  $3 target  $4 cast_ms
render_and_measure() {
  local tag=$1 sid=$2 tgt=$3 cms=$4
  local mroot="$EV/measure/$tag"
  if [ -s "$mroot/summary.tsv" ]; then
    echo "[$tag] already measured, skipping" >> "$EV/sweep.log"
    return 0
  fi
  local plan="$EV/plans/$tag.plan"
  # B1171 (#490): the level the oracle capture behind the row executed, not a
  # literal: a tour spell's B1118 leg (spell_fx_table.tsv), a record's B1130 capture.
  local CL="$(dirname "$0")/../../../scripts/fx/capture_level.py" lvl rec
  case "$tag" in
    b1157_tour_*) lvl=$(python3 -I "$CL" b1118 "$sid") || return 1 ;;
    *) rec=${tag#b1157_r}; rec=${rec%%_*}; lvl=$(python3 -I "$CL" b1130 "$rec") || return 1 ;;
  esac
  printf 't_begin_s\tspell_id\ttarget\tdistance_units\tcast_ms\tcaster_level\n1.5\t%s\t%s\t0\t%s\t%s\n' \
      "$sid" "$tgt" "$cms" "$lvl" > "$plan"
  local secs run
  secs=$(python3 -c "print(round(1.5 + $cms/1000.0 + 11.5, 1))" </dev/null)
  run="$TP_BASE/$tag"
  echo "[$tag] (spell $sid, $tgt, cast ${cms}ms, ${secs}s) rendering..." >> "$EV/sweep.log"
  if { scp -q "$plan" "thinkpad:$TP_BASE/plans/$tag.plan" </dev/null \
      && ssh thinkpad "mkdir -p '$run' && cd '$REPO' && \
          WILLEQ_DRM_DEVICE=/dev/dri/no-such-card '$BIN' --spell-test --client '$CLIENT' --eqe1 '$PACK' \
          --plan '$TP_BASE/plans/$tag.plan' --frames-dir '$run' --fps $FPS --seconds $secs \
          --seed $SEED --headless --camera b1118 > '$run/run.log' 2>&1" </dev/null \
      && rsync -a "thinkpad:$run/" "$EV/frames/$tag/" </dev/null; }; then
    :
  else
    echo "[$tag] RENDER FAILED (see $EV/frames/$tag/run.log)" >> "$EV/sweep.log"
    rsync -a "thinkpad:$run/run.log" "$EV/frames/$tag/" </dev/null 2>/dev/null || true
    return 1
  fi
  echo "[$tag] measuring..." >> "$EV/sweep.log"
  mkdir -p "$mroot"
  # fx_measure.py is launched as plain `python3` inside harness_measure.py,
  # so the venv (numpy/cv2) goes first on PATH. FX_SPELLS=auto: b1123_summary
  # otherwise lists its B1118 tour set (93 38 54 656 41 253 458) and a
  # record run's summary.tsv carries no row for its own spell. The stdin
  # redirect is REQUIRED: the measure's child processes (fx_*.py) would
  # otherwise read the loop's stdin (the plan list) and eat its rows.
  if ! FX_KEEP=1 FX_SPELLS=auto PATH=/home/user/.venvs/b1155/bin:$PATH "$PY3" "$RM" "$EV/frames/$tag" "$mroot" < /dev/null > "$mroot/measure.log" 2>&1; then
    echo "[$tag] MEASURE FAILED (see $mroot/measure.log)" >> "$EV/sweep.log"
    return 1
  fi
  # B1151's motion signatures (the gate's source/landing anatomy), rig b1118.
  for st in SOURCE LANDING; do
    "$PYV" "$ANCHOR" "$mroot/fable_${sid}" 640,605 235 $st < /dev/null > "$mroot/anchor_${st}.tsv" 2>&1 \
        || echo "[$tag] anchor_$st FAILED" >> "$EV/sweep.log"
  done
  echo "[$tag] done -> $(awk -F'\t' -v s="$sid" '$1 == s {print $2, $3, $5, $9, $10, $11}' "$mroot/summary.tsv")" >> "$EV/sweep.log"
}

mode=${1:-all}
n=0; fail=0

run_one() { local tag=$1 sid=$2 tgt=$3 cms=$4; n=$((n + 1)); render_and_measure "$tag" "$sid" "$tgt" "$cms" || fail=$((fail + 1)); }

if [ "$mode" = tour ] || [ "$mode" = all ]; then
  # B1151's tour rows (the record representatives' cast_ms).
  while read -r sid tgt cms; do
    run_one "b1157_tour_${sid}" "$sid" "$tgt" "$cms"
  done <<'ROWS'
93 other 1500
38 other 1500
54 other 1500
656 other 1500
41 other 1500
253 self 1750
458 other 3150
ROWS
fi

if [ "$mode" = sweep ] || [ "$mode" = all ]; then
  # The 40 records from B1152's own plan list (refactor/B1152/plans/sweep/
  # list.tsv: record, spell_id, target, cast_ms -- record 51 absent, B1130 did
  # not capture it), so the only variable vs the baseline is the caster's
  # animation.
  while IFS=$'\t' read -r rec sid tgt cms; do
    [ -z "${rec:-}" ] && continue
    run_one "b1157_r${rec}_${sid}" "$sid" "$tgt" "$cms"
  done < "$REPO/refactor/B1152/plans/sweep/list.tsv"
fi

echo "done ($mode): $n runs, $fail failed" >> "$EV/sweep.log"
echo "$fail" > "$EV/sweep.exit"
exit "$fail"
