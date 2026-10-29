#!/usr/bin/env bash
# B1197 renders on the ThinkPad: record 35 (spell 253) at the current tip.
# Same plan/seed/camera/seconds as B1191's committed render_all.sh
# (branch task/kb-b1191-green-channel, its r35 row: b1130 camera, 16.8 s).
# Pack: the shared NFS pack /mnt/projects/EverQuest_ETC1/willeq.eqe1, whose
# sha256 785690e6... equals this tree's pin (data/willeq.eqe1.sha256) -- the
# B1188 1:1-atlas pack.
#
# Runs (CONC=2, per the render rule):
#   full   -- no WILLEQ_FX_LANDING_EMITTERS: every drawn slot (Ask 1)
#   e0     -- landing filter set to a non-existent emitter: source+impact only
#   e432/e433/e434 -- B1188's per-emitter isolation (Ask 2), the Target stage
#            restricted to one emitter (spell_visual_fx.cpp LandingEmitterFilter)
#
# Frames land in /home/user/kb-frames/kb-b1197-rec35-blue/<run>/ (ThinkPad);
# the measurement leg rsyncs them to hermes-ext and deletes them here.
set -x
cd /home/user/src/willeq-wt/kb-b1197-rec35-blue   # data/ resolution (B1191's cd $WT)
BIN=/home/user/willeq-builds/kb-b1197-rec35-blue/build/bin/model_viewer
CLIENT=/mnt/projects/EverQuestP1999
PACK=/mnt/projects/EverQuest_ETC1/willeq.eqe1
PLAN=/home/user/kb-frames/kb-b1191-green-channel/plans/r35.plan
F=/home/user/kb-frames/kb-b1197-rec35-blue
render() {  # $1 run tag, $2 WILLEQ_FX_LANDING_EMITTERS value ('' = unset)
  tag=$1; val=$2
  rm -rf "$F/$tag"; mkdir -p "$F/$tag"
  if [ -n "$val" ]; then
    WILLEQ_FX_LANDING_EMITTERS=$val "$BIN" --spell-test --client "$CLIENT" --eqe1 "$PACK" \
      --plan "$PLAN" --frames-dir "$F/$tag" --fps 30 --seconds 16.8 --sim-hz 60 \
      --seed 1 --headless --camera b1130 > "$F/$tag/run.log" 2>&1
  else
    "$BIN" --spell-test --client "$CLIENT" --eqe1 "$PACK" \
      --plan "$PLAN" --frames-dir "$F/$tag" --fps 30 --seconds 16.8 --sim-hz 60 \
      --seed 1 --headless --camera b1130 > "$F/$tag/run.log" 2>&1
  fi
  echo "RENDERED $tag ($(ls "$F/$tag" | grep -c frame_) frames)"
}
render full ""
render e0 999999
render e432 432
render e433 433
render e434 434
echo ALL_RENDERS_DONE
