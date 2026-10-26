#!/bin/bash
# B1153 (#473): copy of refactor/B1151/scripts/run_scene.sh, paths retargeted.
# The model_viewer is THIS branch's rbuild tree on the ThinkPad
# (/home/user/willeq-builds/kb-b1153-retire/build, built by rbuild from the
# shared worktree /home/user/src/willeq-wt/kb-b1153-retire); the runs land
# under this branch's refactor/B1153/runs/ (evidence in the worktree, so it
# lands in the MR), the frames in /home/user/kb-frames/kb-b1153-retire.
# WILLEQ_DRM_DEVICE is pointed at a nonexistent card: the b1118 rig is
# calibrated to the SURFACELESS 1280x800 render (B1139), so the ThinkPad's
# world-accessible /dev/dri must not auto-select the 3840x2160 GBM mode
# (see the B1150 copy).
# usage: run_scene.sh <name> <fps> [model_viewer args...]
# env: TREE (build dir, default the rbuild tree), FRAMES (default
#      /home/user/kb-frames/kb-b1153-retire), PLAN, SECONDS_
set -u
R=/home/user/src/willeq-wt/kb-b1153-retire/refactor/B1153
R39=/mnt/projects/fable/review/B1139
T=${TREE:-/home/user/willeq-builds/kb-b1153-retire/build}
WT=/home/user/src/willeq-wt/kb-b1153-retire
F=${FRAMES:-/home/user/kb-frames/kb-b1153-retire}
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
rm -rf "$F/$n.measure"   # the frames stay (re-measure / sprite ROI)
echo "$(date -u +%FT%TZ) measure $n rc=$mrc -> $(grep -v '^spell' $O/measure/summary.tsv | cut -f2,3,5,9,10)" >> $R/progress.log
exit $((rc + mrc))
