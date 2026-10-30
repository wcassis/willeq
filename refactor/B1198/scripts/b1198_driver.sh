#!/usr/bin/env bash
# B1198 (#517) driver: waits for a quiet ThinkPad (load1 < 6, no other
# model_viewer --spell-test running), then runs the 60-frame readback-candidate
# matrix: both rows (tour_93 + b1130_r2, B1193's committed plans) x five
# candidates (driver | glread | pbo | fbo | dirty) at CONC=1, 2.0 s = 60
# frames at 30 fps. Everything lands in $EV (the render host's kb-frames);
# the worker rsyncs the TSVs back.
#
# Every run starts under quiet conditions (load1 < 6 AND no other
# model_viewer --spell-test running) -- a run measured under contention is
# not a number (Will: measured, never argued). The load at every run start
# AND end is recorded (run_readback_ab.sh's summary line: field 8 = end,
# field 9 = start).
#
# Run it on the render host:
#   ssh thinkpad 'bash /mnt/projects/fable/refactor/B1198/scripts/b1198_driver.sh'
set -uo pipefail

WT=/home/user/src/willeq-wt/kb-b1198-readback-cost
BIN=/home/user/willeq-builds/kb-b1198-readback-cost/build/bin/model_viewer
CLIENT=/mnt/projects/EverQuestP1999
PACK=/mnt/projects/EverQuest_ETC1/willeq.eqe1
EV=/home/user/kb-frames/kb-b1198-readback-cost
SUM=$EV/summary.tsv
AB=$WT/refactor/B1198/scripts/run_readback_ab.sh
ROWS_DIR=$WT/refactor/B1193/rows  # the card's rows, B1193's committed plans
mkdir -p "$EV"
printf 'tag\treadback\tconc\tframes\twall_s\trender_ms_median\treadback_ms_median\tload_at_end\tload_at_start\n' > "$SUM"

quiet() {
    local load n
    load=$(cut -d' ' -f1 /proc/loadavg)
    n=$(pgrep -fc "[m]odel_viewer --spell-test" 2>/dev/null || true)
    n=${n:-0}
    python3 -c "import sys; sys.exit(0 if ($load < 6.0 and $n == 0) else 1)"
}

wait_quiet() {
    for i in $(seq 1 240); do  # up to 2 h
        if quiet; then return 0; fi
        sleep 30
    done
    echo "b1198_driver: never got a quiet ThinkPad" >&2
    return 1
}

run() {  # tag plan camera mode
    local tag=$1 plan=$2 camera=$3 mode=$4
    echo "=== $tag readback=$mode (load $(cut -d' ' -f1 /proc/loadavg))" >&2
    BIN=$BIN CLIENT=$CLIENT PACK=$PACK PLAN="$ROWS_DIR/$plan" \
        CAMERA=$camera ROW_SECONDS=2.0 TAG=$tag MODE=$mode CONC=1 \
        OUT="$EV/${tag}_${mode}_c1_s2.0" bash "$AB" >> "$SUM" || {
        echo "b1198_driver: $tag $mode FAILED" >&2; return 1; }
}

status() { echo "stage: $1" > "$EV/driver.status"; }

run_quiet() {
    if ! wait_quiet; then return 1; fi
    run "$@"
}

status "waiting-for-quiet"

# 60-frame (2.0 s at 30 fps), CONC=1, both rows, all five candidates. A
# single failed run is recorded and the driver moves on (a quiet window is
# scarce); the exit code at the end names how many failed.
failed=0
for mode in driver glread pbo fbo dirty; do
    run_quiet tour_93   tour_93.plan   b1118 "$mode" || failed=$((failed + 1))
    run_quiet b1130_r2  b1130_r2.plan  b1130  "$mode" || failed=$((failed + 1))
done

status "done"
echo "b1198_driver: finished with $failed failed run(s); summary at $SUM" >&2
[ "$failed" -eq 0 ]
