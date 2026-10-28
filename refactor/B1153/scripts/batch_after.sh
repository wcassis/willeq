#!/usr/bin/env bash
# B1153 (#473) step-3/4 batch: every "after" (post-retirement config, current
# binary) harness run, rendered in parallel on the ThinkPad and measured HERE.
#
# Variants (the config is the shared-worktree config/spell_effects.json):
#   after  -- the committed post-retirement config (the six spellOverrides kept)
#   noov   -- the same config with the spellOverrides block stripped
# Config phases are SERIALIZED on the NFS worktree: all "after" jobs finish
# rendering before the config is stripped for the "noov" jobs.
#
# Jobs (104):
#   after: tour b1118 (7), sweep b1118 (40, run_sweep.sh geometry, --seed 1),
#          sweep b1130 (40, B1155 geometry), b1130 override tour (5),
#          b1130 record 51 (1, spell 4499 -- the B1134 observer leg's record,
#          excluded from the 40-record sweep)
#   noov:  tour b1118 (6), b1130 override tour (5)
#
# usage: batch_after.sh            # everything
#        batch_after.sh after      # only the after variant
#        batch_after.sh noov       # only the noov variant (config must be
#                                  # stripped beforehand; see strip_cfg.sh)
# Progress: $EV/batch_after.log, $EV/batch.exit (0 = every job rc 0)
set -u
EV=/home/user/kb-evidence/kb-b1153-retire
R=/home/user/src/willeq-wt/kb-b1153-retire/refactor/B1153
S=$R/scripts
TABLE=/mnt/projects/fable/review/B1130/record_table.tsv
BIN=/home/user/willeq-builds/kb-b1153-retire/build/bin/model_viewer
CLIENT=/mnt/projects/EverQuestP1999
PACK=/mnt/projects/EverQuest_ETC1/willeq.eqe1
REPO=/home/user/src/willeq-wt/kb-b1153-retire
PY=/home/user/.venvs/b1155/bin/python
H1118=/mnt/projects/fable/review/B1139/scripts/harness_measure.py
H1130=$REPO/refactor/B1155/scripts/harness_measure_b1130.py
TP_FRAMES=/home/user/kb-frames/kb-b1153-retire
LOCAL_FRAMES=$EV/frames
FPS=30
SIM_HZ=60
POOL=${POOL:-4}
ONLY=${1:-}
LOG=$EV/batch_after.log
mkdir -p "$EV/jobs" "$LOCAL_FRAMES"
ssh thinkpad "mkdir -p '$TP_FRAMES/plans'"

# jobs.tsv columns: variant tag cam spell tgt cast_ms level secs seed fixed
#   fixed = - | fixed_npc | fixed_self (b1130 measure kind)
gen_jobs() {
    local out=$1
    : > "$out"
    # tour b1118: B1150's tour7.sh geometry EXACTLY (the tour-7 preset):
    # cast_ms = the record representative's cast_ms (review/B1130/
    # record_table.tsv: 38 -> record 0 rep 93, 54/656 -> record 23 rep 372,
    # 41 -> record 20 rep 341, 458 -> record 33 rep 382, 253 self),
    # target other except 253 self, secs = 1.5 + cast/1000 + 11.5, seed 1.
    local v=$2
    while read spell tgt cast; do
        case "$spell" in \#*) continue ;; esac
        local tag; case "$v" in after) tag=b1153_after_tour_$spell ;; *) tag=b1153_noov_b1118_$spell ;; esac
        local secs; secs=$(python3 -c "print(round(1.5 + $cast/1000.0 + 11.5, 1))")
        printf '%s\t%s\tb1118\t%s\t%s\t%s\t60\t%s\t-\t-\n' "$v" "$tag" "$spell" "$tgt" "$cast" "$secs" >> "$out"
    done <<'EOF'
93 other 1500
38 other 1500
54 other 1500
656 other 1500
41 other 1500
253 self 1750
458 other 3150
EOF
    if [ "$v" = noov ]; then
        # the noov half has no 93 (93 has no override; its row comes from the
        # after sweep, record 0)
        grep -v $'\t93\t' "$out" > "$out.tmp" && mv "$out.tmp" "$out"
    fi
}

# sweep b1118: run_sweep.sh's geometry (record_table, --seed 1, secs 1.5+cast+11.5)
gen_sweep_b1118() {
    local out=$1
    while IFS=$'\t' read -r rec spell tgt cast; do
        local secs; secs=$(python3 -c "print(round(1.5 + ${cast}/1000.0 + 11.5, 1))")
        printf 'after\tb1153_after_b1118_r%s_%s\tb1118\t%s\t%s\t%s\t60\t%s\t1\t-\n' \
            "$rec" "$spell" "$spell" "$tgt" "$cast" "$secs" >> "$out"
    done < <(python3 - "$TABLE" <<'PY'
import csv, sys
for r in csv.DictReader(open(sys.argv[1]), delimiter='\t'):
    if r['record'] in ('51',): continue
    tt = r['targettype']
    print(r['record'], r['spell_id'], 'self' if tt in ('self','group','group-teleport') else 'other', r['cast_ms'], sep='\t')
PY
)
}

# sweep b1130: run_sweep_b1130.sh's geometry (secs 1.5+cast+13.5, seed 1,
# camera per the record's B1130 capture class)
gen_sweep_b1130() {
    local out=$1
    local rec spell name captures targettype cast_ms caster_level kind cam tag
    while IFS=$'\t' read -r rec spell name captures targettype cast_ms caster_level; do
        kind=fixed_self; cam=b1130-self
        case "$captures" in *fixed_npc*) kind=fixed_npc; cam=b1130 ;; esac
        tgt=other; case "$targettype" in self|group|group-teleport) tgt=self ;; esac
        local secs; secs=$(awk -v c="$cast_ms" 'BEGIN{printf "%.1f", 1.5 + c/1000 + 13.5}')
        tag=after_${kind}_r${rec}
        # B1171 (#490): the capture's executed `#level`, not the table's class level.
        caster_level=$(python3 -I "$(dirname "$0")/../../../scripts/fx/capture_level.py" b1130 "$rec") || return 1
        printf 'after\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t1\t%s\n' \
            "$tag" "$cam" "$spell" "$tgt" "$cast_ms" "$caster_level" "$secs" "$kind" >> "$out"
    done < <(python3 - "$TABLE" <<'PY'
import sys
for line in open(sys.argv[1]):
    c = line.rstrip('\n').split('\t')
    if c[0] == 'record' or c[0] == '51': continue
    print(c[0], c[5], c[6], c[11], c[10], c[9], c[8], sep='\t')
PY
)
    # record 51 (the B1134 observer leg's record; B1130 never captured it):
    # fixed_self, cast 0 ms, level 55, spell 4499
    printf 'after\tafter_fixed_self_r51\tb1130-self\t4499\tself\t0\t55\t15.0\t1\tfixed_self\n' >> "$out"
}

# b1130 override tour: the six override spells NOT in the b1130 sweep
# (38/41 other; 54/253/458/656 self), tour geometry (level 60), B1155 seconds
gen_ovb1130() {
    local out=$1 v=$2 tag
    while read spell tgt cast; do
        case "$spell" in \#*) continue ;; esac
        local kind=fixed_self cam=b1130-self
        [ "$tgt" = other ] && { kind=fixed_npc; cam=b1130; }
        local secs; secs=$(awk -v c="$cast" 'BEGIN{printf "%.1f", 1.5 + c/1000 + 13.5}')
        case "$v" in after) tag=after_ovb1130_$spell ;; *) tag=noov_ovb1130_$spell ;; esac
        printf '%s\t%s\t%s\t%s\t%s\t%s\t60\t%s\t1\t%s\n' \
            "$v" "$tag" "$cam" "$spell" "$tgt" "$cast" "$secs" "$kind" >> "$out"
    done <<'EOF'
38 other 1500
41 other 1500
54 other 1500
253 self 1750
458 other 3150
656 other 1500
EOF
}

JOBS=$EV/jobs/jobs.tsv
if [ -z "$ONLY" ]; then
    gen_jobs "$JOBS" after
    gen_sweep_b1118 "$JOBS"
    gen_sweep_b1130 "$JOBS"
    gen_ovb1130 "$JOBS" after
    gen_jobs "$JOBS" noov
    gen_ovb1130 "$JOBS" noov
else
    case "$ONLY" in
        after) gen_jobs "$JOBS" after; gen_sweep_b1118 "$JOBS"; gen_sweep_b1130 "$JOBS"; gen_ovb1130 "$JOBS" after ;;
        noov)  gen_jobs "$JOBS" noov; gen_ovb1130 "$JOBS" noov ;;
    esac
fi
wc -l < "$JOBS" >> "$LOG"

# one job: render on the ThinkPad, rsync frames back, measure HERE
one_job() {
    local line=$(cat) variant tag cam spell tgt cast level secs seed fixed
    IFS=$'\t' read -r variant tag cam spell tgt cast level secs seed fixed <<< "$line"
    local out=$EV/jobs/$tag.log
    local plan=$EV/jobs/${tag}.plan
    local tpplan=$TP_FRAMES/plans/${tag}.plan
    local run=$TP_FRAMES/$tag
    local lf=$LOCAL_FRAMES/$tag
    local mroot
    if [ "$fixed" = "-" ]; then mroot=$R/runs/$tag/measure; else mroot=$EV/b1130_evidence/measure/$tag; fi
    mkdir -p "$mroot"
    if [ -s "$mroot/summary.tsv" ] || [ -s "$mroot/b1130_rows.tsv" ]; then
        echo "$tag: already measured, skipping" >> "$out"; echo 0 > "$EV/jobs/$tag.rc"; return
    fi
    printf 't_begin_s\tspell_id\ttarget\tdistance_units\tcast_ms\tcaster_level\n1.5\t%s\t%s\t0\t%s\t%s\n' \
        "$spell" "$tgt" "$cast" "$level" > "$plan"
    local drmopt=""
    [ "$cam" = b1118 ] && drmopt="WILLEQ_DRM_DEVICE=/dev/dri/no-such-card"
    local seedopt=""
    [ "$seed" != "-" ] && seedopt="--seed $seed"
    echo "$tag: render ($cam, $secs s)" >> "$out"
    if scp -q "$plan" "thinkpad:$tpplan" \
        && ssh thinkpad "mkdir -p '$run' && cd '$REPO' && $drmopt '$BIN' --spell-test --client '$CLIENT' --eqe1 '$PACK' \
            --plan '$tpplan' --frames-dir '$run' --fps $FPS --seconds $secs --sim-hz $SIM_HZ \
            $seedopt --headless --camera $cam > '$run/run.log' 2>&1" \
        && rsync -a "thinkpad:$run/" "$lf/"; then
        :
    else
        echo "$tag: RENDER FAILED" >> "$out"; echo 1 > "$EV/jobs/$tag.rc"; return
    fi
    echo "$tag: measure" >> "$out"
    local mrc=0
    if [ "$fixed" = "-" ]; then
        # b1118: the B1139 instrument (run_scene.sh's form), evidence in the
        # worktree's runs/<tag>/measure (lands in the MR). harness_measure.py
        # spawns bare `python3` for its sub-instruments, so the venv (numpy +
        # cv2; the system python3 has neither) must front the PATH. The outer
        # interpreter is called by ABSOLUTE path: a `PATH=... python3` prefix
        # still resolves python3 through bash's hash table, and a hashed
        # Hermes python3 runs the harness with every sub-instrument dying
        # "No module named cv2" (the 42 failed after-measures of 14:26).
        rm -rf "$lf.measure"
        ( PATH=/home/user/.venvs/b1155/bin:$PATH FX_KEEP=1 FX_SPELLS=auto /home/user/.venvs/b1155/bin/python -I "$H1118" "$lf" "$lf.measure" > "$out.measure" 2>&1 ) || mrc=1
        if [ "$mrc" = 0 ]; then
            cp "$lf.measure/summary.tsv" "$mroot/summary.tsv"
            cp -r "$lf.measure/fable_"* "$mroot/" 2>/dev/null
            cp "$lf.measure/roi.txt" "$mroot/roi.txt" 2>/dev/null
            cp "$lf/timing.tsv" "$lf/scene.tsv" "$R/runs/$tag/" 2>/dev/null
            cp "$lf/run.log" "$R/runs/$tag/run.log" 2>/dev/null
        fi
    else
        # b1130: the B1155 instrument (B1130's own, unchanged)
        if ! "$PY" "$H1130" "$lf" "$mroot" "$fixed" >> "$out.measure" 2>&1; then mrc=1; fi
    fi
    echo "$mrc" > "$EV/jobs/$tag.rc"
    echo "$tag: rc=$mrc" >> "$out"
}
export -f one_job
export EV R S TABLE BIN CLIENT PACK REPO PY H1118 H1130 TP_FRAMES LOCAL_FRAMES FPS SIM_HZ

echo "$(date -u +%FT%TZ) batch start variant=${ONLY:-all} pool=$POOL" >> "$LOG"
cat "$JOBS" | xargs -d '\n' -P "$POOL" -I {} bash -c 'one_job <<< "$@"' _ {} >> "$LOG" 2>&1
echo "$(date -u +%FT%TZ) batch end" >> "$LOG"
fails=0
for f in "$EV"/jobs/*.rc; do [ "$(cat "$f")" = 0 ] || fails=$((fails + 1)); done
echo "$fails" > "$EV/batch.exit"
echo "batch exit: $fails failed" >> "$LOG"
