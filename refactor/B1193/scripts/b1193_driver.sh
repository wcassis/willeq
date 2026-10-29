#!/usr/bin/env bash
# B1193 (#512) driver: waits for a quiet ThinkPad (load1 < 6, no other
# model_viewer --spell-test running), then runs the 60-frame split runs and
# the full-row before/after A/B matrix. Everything lands in $EV (the
# render host's kb-frames); the worker rsyncs the summaries + TSVs back.
set -uo pipefail

WT=/home/user/src/willeq-wt/kb-b1193-harness-frame-cost
BIN=/home/user/willeq-builds/kb-b1193-harness-frame-cost/build/bin/model_viewer
CLIENT=/mnt/projects/EverQuestP1999
PACK=/mnt/projects/EverQuest_ETC1/willeq.eqe1
EV=/home/user/kb-frames/kb-b1193-harness-frame-cost/b2
SUM=$EV/fps_ab.tsv
AB=$WT/refactor/B1193/scripts/run_fps_ab.sh
mkdir -p "$EV"
printf 'tag\tmode\tconc\tframes\twall_s\tfps\tload_at_start\tload_at_end\n' > "$SUM"

quiet() {
    local load n
    load=$(cut -d' ' -f1 /proc/loadavg)
    n=$(pgrep -fc "model_viewer --spell-test" 2>/dev/null || true)
    n=${n:-0}
    python3 -c "import sys; sys.exit(0 if ($load < 6.0 and $n == 0) else 1)"
}

wait_quiet() {
    for i in $(seq 1 240); do  # up to 2 h
            if quiet; then return 0; fi
            sleep 30
    done
    echo "driver: never got a quiet ThinkPad" >&2
    return 1
}

run() {  # tag plan camera row_seconds mode seconds conc
    local tag=$1 plan=$2 camera=$3 row_s=$4 mode=$5 secs=$6 conc=$7
    echo "=== $tag mode=$mode conc=$conc seconds=$secs (load $(cut -d' ' -f1 /proc/loadavg))" >&2
    BIN=$BIN CLIENT=$CLIENT PACK=$PACK PLAN="$WT/refactor/B1193/rows/$plan" \
        CAMERA=$camera ROW_SECONDS=$row_s TAG=$tag MODE=$mode CONC=$conc \
        OUT="$EV/${tag}_${mode}_c${conc}_s${secs}" bash "$AB" >> "$SUM" || {
            echo "driver: $tag $mode c$conc FAILED" >&2; return 1; }
}

status() { echo "stage: $1" > "$EV/driver.status"; }

status "waiting-for-quiet"

# Every run starts under quiet conditions (load1 < 6 AND no other
# model_viewer --spell-test running) -- the baseline gate renders share this
# box, and a run measured under contention is not a number (Will: measured,
# never argued).
run_quiet() {  # same args as run
    if ! wait_quiet; then return 1; fi
    run "$@"
}

# The 60-frame split runs (2.0 s at 30 fps), both rows, both modes.
run_quiet tour_93   tour_93.plan   b1118 2.0  sync   2.0 1 || exit 1
run_quiet tour_93   tour_93.plan   b1118 2.0  writer 2.0 1 || exit 1
run_quiet b1130_r2  b1130_r2.plan  b1130 2.0  sync   2.0 1 || exit 1
run_quiet b1130_r2  b1130_r2.plan  b1130 2.0  writer 2.0 1 || exit 1

# The full rows: before/after at CONC=1 and CONC=2.
run_quiet tour_93   tour_93.plan   b1118 14.5 sync   14.5 1 || exit 1
run_quiet tour_93   tour_93.plan   b1118 14.5 writer 14.5 1 || exit 1
run_quiet tour_93   tour_93.plan   b1118 14.5 sync   14.5 2 || exit 1
run_quiet tour_93   tour_93.plan   b1118 14.5 writer 14.5 2 || exit 1
run_quiet b1130_r2  b1130_r2.plan  b1130 17.0 sync   17.0 1 || exit 1
run_quiet b1130_r2  b1130_r2.plan  b1130 17.0 writer 17.0 1 || exit 1
run_quiet b1130_r2  b1130_r2.plan  b1130 17.0 sync   17.0 2 || exit 1
run_quiet b1130_r2  b1130_r2.plan  b1130 17.0 writer 17.0 2 || exit 1

status "done"
echo "driver: all 12 runs finished; summary at $SUM" >&2
