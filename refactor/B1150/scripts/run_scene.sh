#!/bin/bash
# B1150 (#470): copy of review/B1149/scripts/run_scene.sh, paths only.
# Differences from the B1149 copy:
#   - the model_viewer is THIS branch's rbuild tree on the ThinkPad
#     (/home/user/willeq-builds/kb-b1150-shapes, built by rbuild from the
#     shared worktree /home/user/src/willeq-wt/kb-b1150-shapes);
#   - the runs land under refactor/B1150/ (this card's evidence dir);
#   - the frames live in /tmp/b1150-frames on the ThinkPad.
# usage: run_scene.sh <name> <fps> [model_viewer args...]
# env: TREE (build dir, default the rbuild tree), FRAMES (default
#      /tmp/b1150-frames), PLAN, SECONDS_ (1.5 + cast_ms/1000 + 11.5)
set -u
R=/mnt/projects/fable/refactor/B1150; R39=/mnt/projects/fable/review/B1139
T=${TREE:-/home/user/willeq-builds/kb-b1150-shapes/build}
WT=/home/user/src/willeq-wt/kb-b1150-shapes
F=${FRAMES:-/tmp/b1150-frames}
PLAN=${PLAN:-$R39/plans/tour_93.tsv}
PACK=/mnt/projects/EverQuest_ETC1/willeq.eqe1
n=$1 fps=$2; shift 2
O=$R/runs/$n; mkdir -p "$O" "$F"
rm -rf "$F/$n" "$F/${n}_30"
echo "$(date -u +%FT%TZ) render $n fps=$fps $*" >> $R/progress.log
( cd "$WT" && "$T/bin/model_viewer" --spell-test --client /mnt/projects/EverQuestP1999 \
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
# B1139: the same frames measured through the lane's encoder (FX_CODEC=lane)
# as <name>_x264. Skipped by the sweep (NO_X264=1): the B1149 sweep did the
# same, the codec does not change the lit-area numbers.
if [ -z "${NO_X264:-}" ]; then
  rm -rf "$F/$n.x264.measure"
  FX_CODEC=lane FX_KEEP=1 python3 -I $R39/scripts/harness_measure.py "$fd" "$F/$n.x264.measure" > "$O/measure_x264.log" 2>&1
  xrc=$?
  rsync -a --exclude "*.png" --exclude video.mkv "$F/$n.x264.measure/" "$O/measure_x264/"
  echo "$(date -u +%FT%TZ) measure_x264 $n rc=$xrc -> $(grep -v '^spell' $O/measure_x264/summary.tsv | cut -f2,3,5,9,10)" >> $R/progress.log
fi
exit $((rc + mrc))
