#!/usr/bin/env bash
# B1191 renders on the ThinkPad: MY tree's ETC1 pack (pin-verified) + RGBA8888 A/B for r0.
set -x
WT=/home/user/src/willeq-wt/kb-b1191-green-channel
BIN=/home/user/willeq-builds/kb-b1191-green-channel/build/bin/model_viewer
CLIENT=/mnt/projects/EverQuestP1999
ETC1=/home/user/kb-packs/kb-b1191-green-channel/etcl/willeq.eqe1
RGBA=/home/user/kb-packs/kb-b1191-green-channel/rgba/willeq.eqe1
F=/home/user/kb-frames/kb-b1191-green-channel
cd $WT
for r in "r0:b1130:16.5" "r35:b1130:16.8" "r43:b1130-self:18.0" "tour_93:b1118:14.5"; do
  tag=${r%%:*}; rest=${r#*:}; cam=${rest%%:*}; secs=${rest#*:}
  rm -rf $F/$tag; mkdir -p $F/$tag
  $BIN --spell-test --client $CLIENT --eqe1 $ETC1 --plan $F/plans/$tag.plan --frames-dir $F/$tag --fps 30 --seconds $secs --sim-hz 60 --seed 1 --headless --camera $cam > $F/$tag/run.log 2>&1
  echo "RENDERED $tag"
done
# A/B: same plan/seed/camera, RGBA8888 pack only
rm -rf $F/r0_rgba; mkdir -p $F/r0_rgba
$BIN --spell-test --client $CLIENT --eqe1 $RGBA --plan $F/plans/r0.plan --frames-dir $F/r0_rgba --fps 30 --seconds 16.5 --sim-hz 60 --seed 1 --headless --camera b1130 > $F/r0_rgba/run.log 2>&1
echo "RENDERED r0_rgba"
echo ALL_RENDERS_DONE
