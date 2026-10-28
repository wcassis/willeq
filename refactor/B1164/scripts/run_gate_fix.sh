#!/usr/bin/env bash
# B1164 (#483) fix leg (review note 7941 finding 3; copy of run_gate_dev5.sh with the evidence root
# and the renderer choice as parameters): the B1157 gate (tour-7 + 40 records on --camera b1118, the 40
# records on b1130 / b1130-self) re-rendered for ONE build, from dev-u24: render on the
# ThinkPad (172.16.200.205; the frames go straight to the NFS evidence tree), measure HERE
# with the B1157 instruments unchanged (refactor/B1157/scripts/run_sweep_b1118.sh,
# run_sweep_b1130.sh: same plans, fps 30, seed 1, sim 60 Hz on b1130).
# usage: run_gate_dev5.sh <label before|after> <model_viewer path on the ThinkPad> [b1118|b1130|all] [tag ...]
set -u
LABEL=$1 BIN=$2 WHICH=${3:-all}; shift 3 2>/dev/null || shift $#
ONLY=("$@")
TP=172.16.200.205
GATE_ROOT=${GATE_ROOT:-/mnt/projects/fable/review/B1164/worker/dev5/fix/gate}
EV=$GATE_ROOT/$LABEL
# DRM=nvidia (default): WILLEQ_DRM_DEVICE=/dev/dri/no-such-card -> surfaceless EGL on the
# NVIDIA driver, depth 24 (dev5's gate). DRM=i915: no override -> the DRM/GBM path opens
# /dev/dri/card1 (i915), depth 16 -- what B1157's sweep ran on (its run.log lines 37-46).
DRM=${DRM:-nvidia}
DRMENV="WILLEQ_DRM_DEVICE=/dev/dri/no-such-card"; [ "$DRM" = i915 ] && DRMENV=""
REPO=/mnt/projects/fable/main/wt/kb-b1164-pin-s
# RUN_CWD: the tree the binary runs in (its config/ and data/ are read from the cwd);
# MEASURE_B1130: the b1130 measure script (default: this worktree's).
RUN_CWD=${RUN_CWD:-$REPO}
MEASURE_B1130=${MEASURE_B1130:-$REPO/refactor/B1155/scripts/harness_measure_b1130.py}
CLIENT=/mnt/projects/EverQuestP1999
PACK=/mnt/projects/EverQuest_ETC1/willeq.eqe1
RM=/mnt/projects/fable/review/B1139/scripts/harness_measure.py
ANCHOR=/mnt/projects/fable/review/B1130/scripts/anchor.py
TABLE=/mnt/projects/fable/review/B1130/record_table.tsv
mkdir -p "$EV/plans" "$EV/frames" "$EV/measure"
want() { [ ${#ONLY[@]} -eq 0 ] && return 0; local t; for t in "${ONLY[@]}"; do [ "$t" = "$1" ] && return 0; done; return 1; }
render() {  # tag plan secs camera [extra]
    local tag=$1 plan=$2 secs=$3 cam=$4 extra=${5:-} run="$EV/frames/$1"
    mkdir -p "$run"
    ssh -n "$TP" "cd '$RUN_CWD' && $DRMENV '$BIN' --spell-test --client '$CLIENT' \
        --eqe1 '$PACK' --plan '$plan' --frames-dir '$run' --fps 30 --seconds $secs $extra --seed 1 --headless \
        --camera $cam > '$run/run.log' 2>&1"
}
n=0 fail=0
if [ "$WHICH" = b1118 ] || [ "$WHICH" = all ]; then
    { printf '93 other 1500\n38 other 1500\n54 other 1500\n656 other 1500\n41 other 1500\n253 self 1750\n458 other 3150\n' |
        awk '{print "tour_"$1"\t"$1"\t"$2"\t"$3}'
      awk -F'\t' 'NF>=4 {print "r"$1"_"$2"\t"$2"\t"$3"\t"$4}' "$REPO/refactor/B1152/plans/sweep/list.tsv"; } |
    while IFS=$'\t' read -r tag sid tgt cms; do
        tag="b1118_$tag"; want "$tag" || continue
        m="$EV/measure/$tag"; [ -s "$m/summary.tsv" ] && continue
        plan="$EV/plans/$tag.plan"
        printf 't_begin_s\tspell_id\ttarget\tdistance_units\tcast_ms\tcaster_level\n1.5\t%s\t%s\t0\t%s\t60\n' "$sid" "$tgt" "$cms" >"$plan"
        secs=$(python3 -c "print(round(1.5 + $cms/1000.0 + 11.5, 1))")
        echo "[$tag] render" >>"$EV/gate.log"
        render "$tag" "$plan" "$secs" b1118 || { echo "[$tag] RENDER FAILED" >>"$EV/gate.log"; continue; }
        mkdir -p "$m"
        FX_KEEP=1 FX_SPELLS=auto python3 "$RM" "$EV/frames/$tag" "$m" </dev/null >"$m/measure.log" 2>&1 ||
            { echo "[$tag] MEASURE FAILED" >>"$EV/gate.log"; continue; }
        for st in SOURCE LANDING; do
            python3 "$ANCHOR" "$m/fable_${sid}" 640,605 235 $st </dev/null >"$m/anchor_${st}.tsv" 2>&1
        done
        echo "[$tag] done $(awk -F'\t' -v s="$sid" '$1 == s {print $2, $3, $5}' "$m/summary.tsv")" >>"$EV/gate.log"
    done
fi
if [ "$WHICH" = b1130 ] || [ "$WHICH" = all ]; then
    awk -F'\t' 'NR>1 && $1!=51 {print $1"\t"$6"\t"$7"\t"$12"\t"$11"\t"$10"\t"$9}' "$TABLE" |
    while IFS=$'\t' read -r rec sid name captures ttype cms lvl; do
        kind=fixed_self; case "$captures" in *fixed_npc*) kind=fixed_npc ;; esac
        cam=b1130-self; [ $kind = fixed_npc ] && cam=b1130
        tgt=other; case "$ttype" in self|group|group-teleport) tgt=self ;; esac
        tag="b1130_${kind}_r$rec"; want "$tag" || continue
        m="$EV/measure/$tag"; [ -s "$m/b1130_rows.tsv" ] && continue
        plan="$EV/plans/$tag.plan"
        printf 't_begin_s\tspell_id\ttarget\tdistance_units\tcast_ms\tcaster_level\tpackets\n1.5\t%s\t%s\t0\t%s\t%s\n' "$sid" "$tgt" "$cms" "$lvl" >"$plan"
        secs=$(awk -v c="$cms" 'BEGIN{printf "%.1f", 1.5 + c/1000 + 13.5}')
        echo "[$tag] render" >>"$EV/gate.log"
        render "$tag" "$plan" "$secs" "$cam" "--sim-hz 60" || { echo "[$tag] RENDER FAILED" >>"$EV/gate.log"; continue; }
        mkdir -p "$m"
        python3 "$MEASURE_B1130" "$EV/frames/$tag" "$m" "$kind" </dev/null >>"$m/measure.log" 2>&1 &&
            echo "[$tag] done" >>"$EV/gate.log" || echo "[$tag] MEASURE FAILED" >>"$EV/gate.log"
    done
fi
echo "finished $WHICH $(date -u +%FT%TZ)" >>"$EV/gate.log"
