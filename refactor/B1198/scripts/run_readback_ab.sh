#!/usr/bin/env bash
# B1198 (#517): the frame-capture READBACK candidates, one row, one candidate,
# one concurrency level, on the render host (the ThinkPad; the renders are
# CPU-bound and it is the saturated host). Same shape as B1193's
# run_fps_ab.sh, but MODE is a readback candidate (tools/model_viewer/
# readback_candidates.h):
#
#   driver  -- the current path (createScreenShot); the baseline
#   glread  -- the harness's own glReadPixels + the driver's post-work
#   pbo     -- glReadPixels into a PBO + map copy out + the post-work
#   fbo     -- GPU blit to the requested frame size + readback of the FBO
#   dirty   -- a small probe gates the full driver readback (clean frames
#              are skipped; captured frames are the driver path's frames)
#
# The frame output is B1193's default (the writer thread) -- the readback
# phase is identical in both B1193 modes, so the writer mode is the one the
# residual lives in. frame_cost.tsv (B1193's per-phase split) carries the
# per-frame readback_ms; readback_cost.tsv (the candidate's sub-phases) is
# written by the candidate itself for every mode but driver.
#
# Run it on the render host:
#   ssh thinkpad 'BIN=/home/user/willeq-builds/kb-b1198-readback-cost/build/bin/model_viewer \
#     CLIENT=/mnt/projects/EverQuestP1999 PACK=/mnt/projects/EverQuest_ETC1/willeq.eqe1 \
#     PLAN=/mnt/projects/fable/refactor/B1193/rows/tour_93.plan \
#     CAMERA=b1118 ROW_SECONDS=2.0 TAG=tour_93 MODE=driver CONC=1 \
#     OUT=/home/user/kb-frames/kb-b1198-readback-cost/tour_93_driver_c1 \
#     bash /mnt/projects/fable/refactor/B1198/scripts/run_readback_ab.sh'
# The summary line (tab-separated) goes to $OUT/c1/summary.tsv (data-only,
# one row per run) AND stdout; the driver appends stdout to its own
# summary.tsv. The TSVs stay in $OUT (rsync them back to the worker, then
# delete the frames).
set -euo pipefail

BIN=${BIN:?}            # the model_viewer binary (the build tree, render host)
CLIENT=${CLIENT:?}      # the EQ client files
PACK=${PACK:?}          # willeq.eqe1
PLAN=${PLAN:?}          # the row's plan.tsv
CAMERA=${CAMERA:?}      # the row's camera preset (b1118, b1130, b1130-self)
ROW_SECONDS=${ROW_SECONDS:?}  # the row's seconds (2.0 = 60 frames at 30 fps)
TAG=${TAG:?}            # the row tag (tour_93, b1130_r2)
MODE=${MODE:?}          # driver | glread | pbo | fbo | dirty (the readback candidate)
CONC=${CONC:-1}         # 1 (2 would confound the readback with CPU contention)
OUT=${OUT:?}            # the frames directory (created here)
WORKDIR=${WORKDIR:-/home/user/src/willeq-wt/kb-b1198-readback-cost}
# The CWD must be the worktree: the harness resolves
# data/eqe1_texture_index.json relative to it (ResolveDataPath), and without
# it the EQE1 pack misses its texture names and the run dies FATAL.
cd "$WORKDIR"

case "$MODE" in
    driver) EXTRA="" ;;   # the default path, named explicitly for the table
    glread|pbo|fbo|dirty) EXTRA="--readback-mode $MODE" ;;
    *) echo "run_readback_ab: MODE is driver|glread|pbo|fbo|dirty" >&2; exit 2 ;;
esac

mkdir -p "$OUT"
load_start=$(cut -d' ' -f1 /proc/loadavg)
echo "run_readback_ab: $TAG readback=$MODE conc=$CONC load_at_start=$load_start" >&2

t0=$(date +%s.%N)
pids=()
for i in $(seq 1 "$CONC"); do
    d="$OUT/c$i"
    mkdir -p "$d"
    timeout -k 20 300 "$BIN" --spell-test --client "$CLIENT" --eqe1 "$PACK" --plan "$PLAN" \
        --frames-dir "$d" --fps 30 --seconds "$ROW_SECONDS" --headless --frames-raw \
        --camera "$CAMERA" --seed 1 $EXTRA > "$d/run.log" 2>&1 &
    pids+=($!)
done
rc=0
for p in "${pids[@]}"; do wait "$p" || rc=1; done
t1=$(date +%s.%N)
load=$(cut -d' ' -f1 /proc/loadavg)   # the load at run END
wall=$(python3 -c "print(f'{$t1 - $t0:.2f}')")

# The summary line: the wall (startup + the per-frame cost x frames) and the
# frame_cost.tsv medians (the per-phase split; readback_ms = the candidate's
# whole phase on the render thread). The load at run END (sampled above) and
# the load at run START (sampled before t0) are BOTH committed here (field 8
# = end, field 9 = start); the start was previously stderr-only.
d="$OUT/c1"
python3 - "$d" "$TAG" "$MODE" "$CONC" "$ROW_SECONDS" "$wall" "$load" "$load_start" <<'PY'
import statistics, sys, os
d, tag, mode, conc, row_s, wall, load, load_start = sys.argv[1:9]
def col(name, idx):
    rows = []
    with open(os.path.join(d, name)) as f:
        f.readline()  # header
        for line in f:
            c = line.rstrip("\n").split("\t")
            if len(c) > idx and c[idx].replace(".", "").replace("-", "").isdigit():
                rows.append(float(c[idx]))
    return rows
# frame_cost.tsv: frame render_ms readback_ms downscale_ms encode_ms write_ms
render = col("frame_cost.tsv", 1)
readback = col("frame_cost.tsv", 2)
rm = statistics.median(render) if render else float("nan")
bm = statistics.median(readback) if readback else float("nan")
line = f"{tag}\t{mode}\t{conc}\t{len(render)}\t{wall}\t{rm:.2f}\t{bm:.2f}\t{load}\t{load_start}\n"
with open(os.path.join(d, "summary.tsv"), "a") as f:
    f.write(line)
print(line, end="")
PY
[ "$rc" -eq 0 ] || { echo "run_readback_ab: $TAG $MODE c$CONC exited $rc (see $OUT/c1/run.log)" >&2; exit 1; }
