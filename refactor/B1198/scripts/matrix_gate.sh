#!/usr/bin/env bash
# B1198 (#517) driver, GATE PATH: the surfaceless 1280x800 matrix the issue
# specifies (#517: "Measured at 1280x800, the client's gate resolution (Will
# 2026-10-29: the b1130 rows render at 1280x800 surfaceless; the 3840x2160
# virtual surface is not the gate's path)").
#
# Same 2x5 matrix as b1198_driver.sh (both rows x five candidates, CONC=1,
# 2.0 s = 60 frames at 30 fps) with ONE difference: WILLEQ_DRM_DEVICE is
# pointed at an absent card, so the driver opens no DRM device and renders
# surfaceless at the native requested frame (1280x800) -- no 3840x2160
# virtual surface, no downscale. The per-run exact command line (the
# reviewer's recheck matrix ran this shape; /mnt/projects/fable/review/
# B1198-recheck/matrix.sh):
#
#   WILLEQ_DRM_DEVICE=/dev/dri/no-such-card BIN=$BIN CLIENT=$CLIENT PACK=$PACK \
#       PLAN="$WT/refactor/B1193/rows/$plan" CAMERA=$cam ROW_SECONDS=2.0 TAG=$row \
#       MODE=$mode CONC=1 OUT=$out bash $WT/refactor/B1198/scripts/run_readback_ab.sh
#
# Quiet gate per run: load1 < 6.0 AND no other `model_viewer --spell-test`
# (quiet.py). The load at every run start and end is in the summary line
# (run_readback_ab.sh) and the per-run load in this driver's log.
#
# Run it on the render host:
#   ssh thinkpad 'bash /mnt/projects/fable/refactor/B1198/scripts/matrix_gate.sh'
set -uo pipefail

WT=/home/user/src/willeq-wt/kb-b1198-readback-cost
BIN=/home/user/willeq-builds/kb-b1198-readback-cost/build/bin/model_viewer
CLIENT=/mnt/projects/EverQuestP1999
PACK=/mnt/projects/EverQuest_ETC1/willeq.eqe1
EV=/home/user/kb-frames/kb-b1198-gate
LOG=$EV/matrix.log
AB=$WT/refactor/B1198/scripts/run_readback_ab.sh
QUIET=$WT/refactor/B1198/scripts/quiet.py
ROWS_DIR=$WT/refactor/B1193/rows  # the card's rows, B1193's committed plans
mkdir -p "$EV"
: > "$LOG"

quiet() {
    local load n
    load=$(cut -d' ' -f1 /proc/loadavg)
    n=$(pgrep -fc "[m]odel_viewer --spell-test" 2>/dev/null || true)
    n=${n:-0}
    python3 "$QUIET" "$load" "$n"
}

wait_quiet() {
    for i in $(seq 1 480); do   # up to 8 h (gate renders may be busy)
        if quiet; then return 0; fi
        sleep 10
    done
    echo "matrix_gate: never got a quiet ThinkPad" >> "$LOG"
    return 1
}

run() {  # row camera plan mode
    local row=$1 cam=$2 plan=$3 mode=$4
    local out="$EV/${row}_${mode}_c1_s2.0"
    echo "=== $row $mode (load $(cut -d' ' -f1 /proc/loadavg)) $(date -u +%FT%T)" >> "$LOG"
    WILLEQ_DRM_DEVICE=/dev/dri/no-such-card BIN=$BIN CLIENT=$CLIENT PACK=$PACK \
        PLAN="$ROWS_DIR/$plan" CAMERA=$cam ROW_SECONDS=2.0 TAG=$row \
        MODE=$mode CONC=1 OUT="$out" bash "$AB" >> "$LOG" 2>&1 || {
        echo "FAILED $row $mode (see $out/c1/run.log)" >> "$LOG"; return 1; }
}

status() { echo "stage: $1" > "$EV/driver.status"; }
failed=0
for mode in driver glread pbo fbo dirty; do
    if ! wait_quiet; then echo "SKIP (not quiet) $mode" >> "$LOG"; failed=$((failed + 1)); continue; fi
    run tour_93   b1118 tour_93.plan   $mode || failed=$((failed + 1))
    run b1130_r2  b1130 b1130_r2.plan  $mode || failed=$((failed + 1))
done
status "done"
echo "matrix finished; failed=$failed" >> "$LOG"
[ $failed -eq 0 ]
