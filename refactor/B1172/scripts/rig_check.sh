#!/usr/bin/env bash
# B1172 fix leg (review note 7955 finding 4): render ONE b1130 record with a
# given model_viewer and source tree in a given GL context on the ThinkPad,
# then measure it HERE exactly as run_one.sh does (B1155's
# harness_measure_b1130.py from that tree). The context is the variable:
#   nvidia = WILLEQ_DRM_DEVICE=/dev/dri/no-such-card -> surfaceless EGL on the
#            Quadro, depth 24 (B1150's run_scene.sh; B1172's before/after runs)
#   i915   = no override -> DRM/GBM on the UHD 630, depth 16 (what B1157's
#            run_sweep_b1130.sh ran: its run logs print "driver=i915")
# usage: rig_check.sh <out_tag> <nvidia|i915> <binary> <tree> <camera> <kind> <seconds> <sim_hz> <plan_text>
set -u
TAG=$1 CTX=$2 BIN=$3 WT=$4 CAM=$5 KIND=$6 SECS=$7 HZ=$8 PLAN=$9
EV=/mnt/projects/fable/review/B1172/worker/fix/rig
TP=${TP:-172.16.200.205}
mkdir -p "$EV/plans" "$EV/frames" "$EV/measure"
printf '%b\n' "$PLAN" > "$EV/plans/$TAG.plan"
fd="$EV/frames/$TAG"; rm -rf "$fd"; mkdir -p "$fd"
env=""; [ "$CTX" = nvidia ] && env="WILLEQ_DRM_DEVICE=/dev/dri/no-such-card"
ssh "$TP" "cd '$WT' && $env '$BIN' --spell-test \
    --client /mnt/projects/EverQuestP1999 --eqe1 /mnt/projects/EverQuest_ETC1/willeq.eqe1 \
    --plan '$EV/plans/$TAG.plan' --frames-dir '$fd' --fps 30 --seconds $SECS --sim-hz $HZ --seed 1 --headless \
    --camera $CAM > '$fd/run.log' 2>&1" < /dev/null || { echo "[$TAG] RENDER FAILED"; exit 1; }
m="$EV/measure/$TAG"; rm -rf "$m"; mkdir -p "$m"
FX_KEEP=1 python3 -P "$WT/refactor/B1155/scripts/harness_measure_b1130.py" "$fd" "$m" "$KIND" < /dev/null > "$m/measure.log" 2>&1
echo "[$TAG] rc=$? $(grep -m1 -o 'driver=[a-z0-9]*\|vendor=[A-Za-z]*' "$fd/run.log" | head -1)"
