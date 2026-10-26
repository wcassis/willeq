#!/usr/bin/env bash
# B1153 (#473): measure-only pass over already-rendered local frames
# ($EV/frames/<tag>/). Use when a render batch finished (or a previous pass
# measured with the wrong interpreter). Skips runs whose measurement is
# already in place. Parallel: POOL (default 4).
set -u
EV=/home/user/kb-evidence/kb-b1153-retire
R=/home/user/src/willeq-wt/kb-b1153-retire/refactor/B1153
REPO=/home/user/src/willeq-wt/kb-b1153-retire
PY=/home/user/.venvs/b1155/bin/python
H1118=/mnt/projects/fable/review/B1139/scripts/harness_measure.py
H1130=$REPO/refactor/B1155/scripts/harness_measure_b1130.py
LOCAL_FRAMES=$EV/frames
POOL=${POOL:-4}
LOG=$EV/measure_pass.log

# b1118 tags: b1153_after_tour_* (b1118 camera, the B1139 instrument)
# b1130 tags: after_* (b1130 / b1130-self, the B1155 instrument)
measure_one() {
    local tag=$1
    local lf=$LOCAL_FRAMES/$tag
    [ -d "$lf" ] || return 0
    local mroot fixed
    case "$tag" in
        b1153_after_tour_*|b1153_noov_b1118_*)
            mroot=$R/runs/$tag/measure; fixed=- ;;
        after_*)
            mroot=$EV/b1130_evidence/measure/$tag
            case "$tag" in *fixed_npc*) fixed=fixed_npc ;; *) fixed=fixed_self ;; esac ;;
        after_ovb1130_*|noov_ovb1130_*)
            mroot=$EV/b1130_evidence/measure/$tag
            case "$tag" in *self*) fixed=fixed_self ;; *) fixed=fixed_npc ;; esac
            # ovb1130 tags carry the spell, not the kind: re-derive from the plan
            local tgt; tgt=$(awk -F'\t' 'NR==2{print $3}' "$EV/jobs/$tag.plan" 2>/dev/null)
            [ "$tgt" = self ] && fixed=fixed_self
            ;;
        *) return 0 ;;
    esac
    [ -s "$mroot/summary.tsv" ] || [ -s "$mroot/b1130_rows.tsv" ] && return 0
    local out=$EV/jobs/${tag}.measure2.log
    if [ "$fixed" = "-" ]; then
        rm -rf "$lf.measure"
        ( PATH=/home/user/.venvs/b1155/bin:$PATH FX_KEEP=1 python3 -I "$H1118" "$lf" "$lf.measure" > "$out" 2>&1 ) || { echo "$tag: MEASURE FAILED" >> "$LOG"; return 1; }
        mkdir -p "$mroot"
        cp "$lf.measure/summary.tsv" "$mroot/summary.tsv"
        cp -r "$lf.measure/fable_"* "$mroot/" 2>/dev/null
    else
        mkdir -p "$mroot"
        if ! "$PY" "$H1130" "$lf" "$mroot" "$fixed" >> "$out" 2>&1; then
            echo "$tag: MEASURE FAILED" >> "$LOG"; return 1
        fi
    fi
    echo "$tag: measured" >> "$LOG"
}
export -f measure_one
export EV R REPO PY H1118 H1130 LOCAL_FRAMES LOG

echo "$(date -u +%FT%TZ) measure pass start pool=$POOL" >> "$LOG"
# jobs in flight in a live render batch may still be writing frames: only
# measure runs whose frame dir holds a timing.tsv (the harness writes it last)
ls "$LOCAL_FRAMES" | grep -E '^(b1153_after_tour|b1153_noov_b1118|after_)' \
  | xargs -d '\n' -P "$POOL" -I {} bash -c 'measure_one "$@"' _ {}
echo "$(date -u +%FT%TZ) measure pass end" >> "$LOG"
