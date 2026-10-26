#!/usr/bin/env bash
# B1153 (#473): the five tour override-spells NOT in the 40-record b1130 sweep
# (38, 41 other; 54, 458, 656 self) on the b1130 / b1130-self presets, one run
# each, measured with the B1155 instrument (harness_measure_b1130.py) exactly
# as run_sweep_b1130.sh does. Run FROM THIS HOST (the venv python is here).
#
# usage: b1130_tour6.sh <tag_prefix> <spell> <target> <cast_ms> [more triples]
#   e.g. b1130_tour6.sh b1153_noov 38 other 2500 41 other 4000 54 self 2500 ...
# Rendered frames: thinkpad:/home/user/kb-frames/kb-b1153-retire/<tag>_<spell>
# Measured rows:   /home/user/kb-evidence/kb-b1153-retire/b1130_tour6_<tag>/<spell>/
set -u
TAG=$1; shift
EV=/home/user/kb-evidence/kb-b1153-retire/b1130_tour6_$TAG
TP_BASE=/home/user/kb-frames/kb-b1153-retire
BIN=/home/user/willeq-builds/kb-b1153-retire/build/bin/model_viewer
CLIENT=/mnt/projects/EverQuestP1999
PACK=/mnt/projects/EverQuest_ETC1/willeq.eqe1
PY=/home/user/.venvs/b1155/bin/python
REPO=/home/user/src/willeq-wt/kb-b1153-retire
mkdir -p "$EV/plans" "$EV/runs" "$EV/measure"

# each triple: <spell> <target> <cast_ms> (tour7 geometry, caster level 60)
while [ $# -ge 3 ]; do
  spell=$1 tgt=$2 cms=$3; shift 3
  [ "${spell:0:1}" = "#" ] && continue
  kind=fixed_self; cam=b1130-self
  [ "$tgt" = other ] && { kind=fixed_npc; cam=b1130; }
  secs=$(awk -v c="$cms" 'BEGIN{printf "%.1f", 1.5 + c/1000 + 13.5}')
  plan="$EV/plans/$spell.plan"
  printf 't_begin_s\tspell_id\ttarget\tdistance_units\tcast_ms\tcaster_level\tpackets\n1.5\t%s\t%s\t0\t%s\t60\t\n' "$spell" "$tgt" "$cms" > "$plan"
  run="$TP_BASE/${TAG}_$spell"
  mroot="$EV/measure/$spell"
  if [ -s "$mroot/b1130_rows.tsv" ]; then echo "$spell: already measured, skipping"; continue; fi
  echo "$spell ($kind, cast ${cms}ms, $secs s) rendering..."
  if scp -q "$plan" "thinkpad:$plan" \
    && ssh thinkpad "mkdir -p '$run' && cd '$REPO' && '$BIN' --spell-test --client '$CLIENT' --eqe1 '$PACK' \
        --plan '$plan' --frames-dir '$run' --fps 30 --seconds $secs --sim-hz 60 \
        --seed 1 --headless --camera $cam > '$run/run.log' 2>&1" \
    && rsync -a "thinkpad:$run/" "$EV/runs/$spell/"; then
    echo "$spell: measuring..."
    if "$PY" "$REPO/refactor/B1155/scripts/harness_measure_b1130.py" "$EV/runs/$spell" "$mroot" "$kind" > "$EV/measure/$spell.log" 2>&1; then
      echo "$spell: done"
    else
      echo "$spell: MEASURE FAILED (see $EV/measure/$spell.log)"
    fi
  else
    echo "$spell: RENDER FAILED (see $EV/runs/$spell/run.log)"
  fi
done
echo "b1130_tour6 $TAG done"
