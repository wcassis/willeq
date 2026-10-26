#!/bin/bash
# B1152 (#471): copy of refactor/B1150/scripts/run_scene.sh, paths only.
# The model_viewer is THIS branch's rbuild tree on the ThinkPad
# (/home/user/willeq-builds/kb-b1152-missile/build, built by rbuild from the
# shared worktree /home/user/src/willeq-wt/kb-b1152-missile); the runs land
# under this branch's refactor/B1152/runs/ (evidence in the worktree, so it
# lands in the MR), the frames in /tmp/b1152-frames. WILLEQ_DRM_DEVICE is
# pointed at a nonexistent card: the b1118 rig is calibrated to the
# SURFACELESS 1280x800 render (B1139), so the ThinkPad's world-accessible
# /dev/dri must not auto-select the 3840x2160 GBM mode (see the B1150 copy).
# usage: run_scene.sh <name> <fps> [model_viewer args...]
# env: TREE (build dir, default the rbuild tree), FRAMES (default
#      /tmp/b1152-frames), PLAN, SECONDS_ (1.5 + cast_ms/1000 + 11.5)
set -u
R=/home/user/src/willeq-wt/kb-b1152-missile/refactor/B1152
R39=/mnt/projects/fable/review/B1139
T=${TREE:-/home/user/willeq-builds/kb-b1152-missile/build}
WT=/home/user/src/willeq-wt/kb-b1152-missile
F=${FRAMES:-/tmp/b1152-frames}
PLAN=${PLAN:-$R39/plans/tour_93.tsv}
PACK=/mnt/projects/EverQuest_ETC1/willeq.eqe1
n=$1 fps=$2; shift 2
O=$R/runs/$n; mkdir -p "$O" "$F"
rm -rf "$F/$n" "$F/${n}_30"
echo "$(date -u +%FT%TZ) render $n fps=$fps $*" >> $R/progress.log
( cd "$WT" && WILLEQ_DRM_DEVICE=/dev/dri/no-such-card \
  "$T/bin/model_viewer" --spell-test --client /mnt/projects/EverQuestP1999 \
    --eqe1 "$PACK" --plan "$PLAN" --frames-dir "$F/$n" --fps "$fps" --seconds ${SECONDS_:-13.2} \
    --headless --camera b1118 "$@" > "$O/run.log" 2>&1 )
rc=$?
echo "$(date -u +%FT%TZ) render $n rc=$rc" >> $R/progress.log
[ -n "${NO_MEASURE:-}" ] && exit $rc
fd="$F/$n"
if [ "$fps" = 60 ]; then python3 -I $R39/scripts/decimate.py "$fd" "$F/${n}_30"; fd="$F/${n}_30"; fi
cp "$fd/timing.tsv" "$fd/scene.tsv" "$O/" 2>/dev/null
rm -rf "$F/$n.measure"
FX_KEEP=1 python3 -I $R39/scripts/harness_measure.py "$fd" "$F/$n.measure" > "$O/measure.log" 2>&1
mrc=$?
rsync -a --exclude "*.png" --exclude video.mkv "$F/$n.measure/" "$O/measure/"
echo "$(date -u +%FT%TZ) measure $n rc=$mrc -> $(grep -v '^spell' $O/measure/summary.tsv | cut -f2,3,5,9,10)" >> $R/progress.log
if [ -z "${NO_X264:-}" ]; then
  rm -rf "$F/$n.x264.measure"
  FX_CODEC=lane FX_KEEP=1 python3 -I $R39/scripts/harness_measure.py "$fd" "$F/$n.x264.measure" > "$O/measure_x264.log" 2>&1
  xrc=$?
  rsync -a --exclude "*.png" --exclude video.mkv "$F/$n.x264.measure/" "$O/measure_x264/"
  echo "$(date -u +%FT%TZ) measure_x264 $n rc=$xrc -> $(grep -v '^spell' $O/measure_x264/summary.tsv | cut -f2,3,5,9,10)" >> $R/progress.log
fi
exit $((rc + mrc))
