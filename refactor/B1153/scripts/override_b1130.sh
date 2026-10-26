#!/usr/bin/env bash
# B1153 (#473): the five tour spells that are NOT B1130-survey rows --
# 38/41/54/458/656 -- on the b1130 preset, for the spellOverrides retirement
# decision (step 1 of the card: "drop it and re-run the harness for that
# spell (tour-7 preset b1118 + the b1130 preset)"). Same plans as tour7.sh
# (target/cast_ms per spell, level 60, distance 0), same B1155 render ->
# rsync -> measure pattern (venv python, harness_measure_b1130.py, kind
# fixed_npc). 93 and 253 are sweep rows 0 and 35 (run_sweep_b1130.sh).
#
# usage: override_b1130.sh   (runs all five; re-runs are skipped)
set -u
EV=/home/user/kb-evidence/kb-b1153-retire/override_b1130${SFX:-}
TP_BASE=/home/user/kb-frames/kb-b1153-retire
BIN=/home/user/willeq-builds/kb-b1153-retire/build/bin/model_viewer
CLIENT=/mnt/projects/EverQuestP1999
PACK=/mnt/projects/EverQuest_ETC1/willeq.eqe1
PY=/home/user/.venvs/b1155/bin/python
REPO=/home/user/src/willeq-wt/kb-b1153-retire
FPS=30
SIM_HZ=60
SEED=1

mkdir -p "$EV/plans" "$EV/runs" "$EV/measure"
ssh thinkpad "mkdir -p '$TP_BASE/plans'"

# spell target cast_ms  (tour7.sh's plans)
while read spell tgt cms; do
    tag="spell$spell"
    mroot="$EV/measure/$tag"
    secs=$(awk -v c="$cms" 'BEGIN{printf "%.1f", 1.5 + c/1000 + 13.5}')
    if [ -s "$mroot/b1130_rows.tsv" ]; then
        echo "$tag: already measured, skipping" >> "$EV/sweep.log"
        continue
    fi
    plan="$EV/plans/s$spell.plan"
    printf 't_begin_s\tspell_id\ttarget\tdistance_units\tcast_ms\tcaster_level\tpackets\n1.5\t%s\t%s\t0\t%s\t60\n' \
        "$spell" "$tgt" "$cms" > "$plan"
    run="$TP_BASE/$tag"
    echo "$tag (spell $spell, $tgt, cast ${cms}ms, $secs s) rendering..." >> "$EV/sweep.log"
    if scp -q "$plan" "thinkpad:$TP_BASE/plans/s$spell.plan" \
        && ssh thinkpad "mkdir -p '$run' && cd '$REPO' && '$BIN' --spell-test --client '$CLIENT' --eqe1 '$PACK' \
        --plan '$TP_BASE/plans/s$spell.plan' --frames-dir '$run' --fps $FPS --seconds $secs --sim-hz $SIM_HZ \
        --seed $SEED --headless --camera b1130 > '$run/run.log' 2>&1" \
        && rsync -a "thinkpad:$run/" "$EV/runs/$tag/"; then
        :
    else
        echo "$tag: RENDER FAILED (see $EV/runs/$tag/run.log)" >> "$EV/sweep.log"
        rsync -a "thinkpad:$run/run.log" "$EV/runs/$tag/" 2>/dev/null || true
        continue
    fi
    echo "$tag: measuring..." >> "$EV/sweep.log"
    kind=fixed_npc
    [ "$tgt" = self ] && kind=fixed_self
    if "$PY" "$REPO/refactor/B1155/scripts/harness_measure_b1130.py" "$EV/runs/$tag" "$mroot" "$kind" \
        >> "$EV/measure/$tag.log" 2>&1; then
        echo "$tag: done" >> "$EV/sweep.log"
    else
        echo "$tag: MEASURE FAILED (see $EV/measure/$tag.log)" >> "$EV/sweep.log"
    fi
done <<'EOF2'
38 other 2500
41 other 4000
54 self 2500
458 self 3000
656 self 3000
EOF2
echo "done" >> "$EV/sweep.log"
