#!/usr/bin/env bash
# B1153 (#473): the 40-record sweep on the b1130 / b1130-self presets, BANDED
# against review/B1130/record_table.tsv (the B1155 bands, gate (c) of the card).
# Copy of refactor/B1155/scripts/run_sweep_b1130.sh, paths retargeted to this
# worktree's rbuild tree and evidence dir. The B1155 measurement: render on
# the ThinkPad, rsync the frames back, measure HERE with the venv python
# (numpy/cv2/scipy) running the B1132/B1130 instruments unchanged.
#
# usage: run_sweep_b1130.sh [record ...]   (default: all 40, i.e. record_table
# minus record 51, which B1130 did not capture)
#
# Progress: $EV/sweep.log (one line per record), exit status in $EV/sweep.exit
# (0 = every record rendered + measured). Re-running an existing record is
# skipped (its b1130_rows.tsv must be present).
set -u
EV=/home/user/kb-evidence/kb-b1153-retire/b1130_sweep${SFX:-}
TP_BASE=/home/user/kb-frames/kb-b1153-retire
BIN=/home/user/willeq-builds/kb-b1153-retire/build/bin/model_viewer
CLIENT=/mnt/projects/EverQuestP1999
PACK=/mnt/projects/EverQuest_ETC1/willeq.eqe1
PY=/home/user/.venvs/b1155/bin/python
REPO=/home/user/src/willeq-wt/kb-b1153-retire
TABLE=/mnt/projects/fable/review/B1130/record_table.tsv
FPS=30
SIM_HZ=60
SEED=1

mkdir -p "$EV/plans" "$EV/runs" "$EV/measure"
ssh thinkpad "mkdir -p '$TP_BASE/plans'"

if [ $# -gt 0 ]; then recs=("$@"); else
    recs=()
    while IFS= read -r rec; do
        [ "$rec" = "record" ] && continue
        [ "$rec" = "51" ] && continue
        recs+=("$rec")
    done < <(awk -F'\t' '{print $1}' "$TABLE")
fi

n=0; fail=0
for rec in "${recs[@]}"; do
    n=$((n + 1))
    # record_table.tsv columns (0-indexed): 0=record 5=spell_id 6=name 11=captures
    # 10=targettype 9=cast_ms 8=caster_level. Extract those (the plan casts the
    # ACTUAL spell_id -- the record number is B1130's index, not the EQ spell id).
    line=$(awk -F'\t' -v r="$rec" '$1 == r {print $1"\t"$6"\t"$7"\t"$12"\t"$11"\t"$10"\t"$9}' "$TABLE")
    [ -n "$line" ] || { echo "[$n/${#recs[@]}] record $rec not in the table" >> "$EV/sweep.log"; fail=$((fail + 1)); continue; }
    IFS=$'\t' read -r rec spell_id name captures targettype cast_ms caster_level <<< "$line"
    # B1171 (#490): column 9 is the CLASS level the spell is learned at (1 for r0), not
    # the caster's; the oracle capture ran `#level 60`. The plan casts at the level the
    # record's capture executed (its plan.tsv + steps.tsv). The committed tables of this
    # card were rendered at column 9 (refactor/B1171/README.md).
    caster_level=$(python3 -I "$(dirname "$0")/../../../scripts/fx/capture_level.py" b1130 "$rec") \
        || { echo "[$n] record $rec: no oracle #level" >> "$EV/sweep.log"; fail=$((fail + 1)); continue; }
    kind=fixed_self
    case "$captures" in *fixed_npc*) kind=fixed_npc ;; esac
    cam=b1130-self
    [ "$kind" = fixed_npc ] && cam=b1130
    tgt=other
    case "$targettype" in self|group|group-teleport) tgt=self ;; esac
    secs=$(awk -v c="$cast_ms" 'BEGIN{printf "%.1f", 1.5 + c/1000 + 13.5}')
    tag="${kind}_r${rec}"
    mroot="$EV/measure/$tag"
    if [ -s "$mroot/b1130_rows.tsv" ]; then
        echo "[$n/${#recs[@]}] $tag: already measured, skipping" >> "$EV/sweep.log"
        continue
    fi
    plan="$EV/plans/r${rec}.plan"
    printf 't_begin_s\tspell_id\ttarget\tdistance_units\tcast_ms\tcaster_level\tpackets\n1.5\t%s\t%s\t0\t%s\t%s\n' \
        "$spell_id" "$tgt" "$cast_ms" "$caster_level" > "$plan"
    run="$TP_BASE/$tag"
    echo "[$n/${#recs[@]}] $tag (spell $spell_id $name, $tgt, cast ${cast_ms}ms, $secs s) rendering..." >> "$EV/sweep.log"
    if scp -q "$plan" "thinkpad:$TP_BASE/plans/r${rec}.plan" \
        && ssh thinkpad "mkdir -p '$run' && cd '$REPO' && '$BIN' --spell-test --client '$CLIENT' --eqe1 '$PACK' \
        --plan '$TP_BASE/plans/r${rec}.plan' --frames-dir '$run' --fps $FPS --seconds $secs --sim-hz $SIM_HZ \
        --seed $SEED --headless --camera $cam > '$run/run.log' 2>&1" \
        && rsync -a "thinkpad:$run/" "$EV/runs/$tag/"; then
        :
    else
        echo "[$n/${#recs[@]}] $tag: RENDER FAILED (see $EV/runs/$tag/run.log)" >> "$EV/sweep.log"
        rsync -a "thinkpad:$run/run.log" "$EV/runs/$tag/" 2>/dev/null || true
        fail=$((fail + 1)); continue
    fi
    echo "[$n/${#recs[@]}] $tag: measuring..." >> "$EV/sweep.log"
    if "$PY" "$REPO/refactor/B1155/scripts/harness_measure_b1130.py" "$EV/runs/$tag" "$mroot" "$kind" \
        >> "$EV/measure/$tag.log" 2>&1; then
        echo "[$n/${#recs[@]}] $tag: done" >> "$EV/sweep.log"
    else
        echo "[$n/${#recs[@]}] $tag: MEASURE FAILED (see $EV/measure/$tag.log)" >> "$EV/sweep.log"
        fail=$((fail + 1))
    fi
done
echo "done: ${#recs[@]} records, $fail failed" >> "$EV/sweep.log"
echo "$fail" > "$EV/sweep.exit"
exit "$fail"
