#!/usr/bin/env bash
# B1193 (#512): the before/after frames/s of the harness frame output, on the
# render host (the ThinkPad; the renders are CPU-bound and it is the
# saturated host). One row, one output mode (sync = the pre-B1193
# render-thread frame output, the "before"; writer = the B1193 writer thread,
# the "after"), one concurrency level.
#
# The wall time is what the gate pays: startup (client files, the eqe1 pack,
# the camera fit) + the per-frame cost x frames. The two modes share the
# startup, so the difference is the frame output.
#
# Run it on the render host:
#   ssh thinkpad 'BIN=/home/user/willeq-builds/kb-b1193-harness-frame-cost/build/bin/model_viewer \
#     CLIENT=/mnt/projects/EverQuestP1999 PACK=/mnt/projects/EverQuest_ETC1/willeq.eqe1 \
#     PLAN=/mnt/projects/fable/review/B1193/rows/tour_93.plan \
#     CAMERA=b1118 ROW_SECONDS=14.5 TAG=tour_93 MODE=sync CONC=1 \
#     OUT=/home/user/kb-frames/kb-b1193-harness-frame-cost/tour_93_sync_c1 \
#     bash /mnt/projects/fable/review/B1193/scripts/run_fps_ab.sh'
# The summary line (tab-separated) goes to $OUT/summary.tsv AND stdout; the
# frames stay in $OUT (rsync the summary back to the worker, then delete).
set -euo pipefail

BIN=${BIN:?}            # the model_viewer binary (the build tree, render host)
CLIENT=${CLIENT:?}      # the EQ client files
PACK=${PACK:?}          # willeq.eqe1
PLAN=${PLAN:?}          # the row's plan.tsv
CAMERA=${CAMERA:?}      # the row's camera preset (b1118, b1130, b1130-self)
ROW_SECONDS=${ROW_SECONDS:?}  # the row's seconds
TAG=${TAG:?}            # the row tag (tour_93, b1130_r2, ...)
MODE=${MODE:-writer}    # sync | writer
CONC=${CONC:-1}         # 1 | 2
OUT=${OUT:?}            # the frames directory (created here)

case "$MODE" in
    sync) EXTRA="--frames-sync" ;;
    writer) EXTRA="" ;;
    *) echo "run_fps_ab: MODE is sync|writer" >&2; exit 2 ;;
esac

mkdir -p "$OUT"
load=$(cut -d' ' -f1 /proc/loadavg)
echo "run_fps_ab: $TAG mode=$MODE conc=$CONC load_at_start=$load" >&2

t0=$(date +%s.%N)
pids=()
for i in $(seq 1 "$CONC"); do
    d="$OUT/c$i"
    mkdir -p "$d"
    "$BIN" --spell-test --client "$CLIENT" --eqe1 "$PACK" --plan "$PLAN" \
        --frames-dir "$d" --fps 30 --seconds "$ROW_SECONDS" --headless \
        --camera "$CAMERA" --seed 1 $EXTRA > "$d/run.log" 2>&1 &
    pids+=($!)
done
rc=0
for p in "${pids[@]}"; do wait "$p" || rc=1; done
t1=$(date +%s.%N)
load_end=$(cut -d' ' -f1 /proc/loadavg)
wall=$(python3 -c "print(f'{$t1 - $t0:.2f}')")

# The frames written (at CONC=2 the two copies of the row are separate, so
# the row's own count is one subdir's).
frames=0
for i in $(seq 1 "$CONC"); do
    n=$(ls "$OUT/c$i" 2>/dev/null | grep -c '^frame_[0-9]*\.png' || true)
    frames=$((frames > n ? frames : n))
done
fps=$(python3 -c "print(f'{$frames / $wall:.2f}')")
printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n' "$TAG" "$MODE" "$CONC" "$frames" "$wall" "$fps" "$load" "$load_end" | tee "$OUT/summary.tsv"

if [ "$rc" -ne 0 ]; then
    echo "run_fps_ab: $TAG mode=$MODE conc=$CONC FAILED (see $OUT/c*/run.log)" >&2
    exit "$rc"
fi
if [ "$frames" -eq 0 ]; then
    echo "run_fps_ab: $TAG mode=$MODE conc=$CONC wrote no frames" >&2
    exit 4
fi
