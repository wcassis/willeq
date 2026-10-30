#!/usr/bin/env bash
# B1197 A/B renders on the ThinkPad: record 35 (spell 253), the e432 isolation
# (WILLEQ_FX_LANDING_EMITTERS=432, B1188's method) against PATCHED spellsnew.edd.
#
#   ab-L -- record 432 +0x7C (life) 3.5 -> 8.0   (does the lifetime word move
#          the end of the Target light?)
#   ab-T -- record 432 +0x78 (run) -5.0 -> -8.0  (does the run word move it?)
#
# The patched EDDs are produced on hermes-ext by
# refactor/B1197/scripts/b1197_edd_ab.py (committed, verified: the first
# differing file byte is printed and checked there) and copied to
# /home/user/kb-frames/kb-b1197-rec35-blue/edd/ (this host).
#
# Client resolution: a per-variant client dir is a symlink tree over
# /mnt/projects/EverQuestP1999 with only spellsnew.edd replaced, so
# eq253-ab-L/spellsnew.edd -> edd/spellsnew_ab-L.edd (every other file is the
# P1999 original). The plan is B1191's r35.plan; everything else (camera,
# fps, seconds, seed) is render_all.sh's, so the frames align one-for-one
# with the full/e0/e432 rows.
#
# Run:  ssh thinkpad 'bash /home/user/kb-frames/kb-b1197-rec35-blue/ab_render.sh'
#       (copy this script there first), CONC<=2.
set -e
BIN=/home/user/willeq-builds/kb-b1197-rec35-blue/build/bin/model_viewer
PACK=/mnt/projects/EverQuest_ETC1/willeq.eqe1
PLAN=/home/user/kb-frames/kb-b1191-green-channel/plans/r35.plan
BASE=/home/user/kb-frames/kb-b1197-rec35-blue
EDD=$BASE/edd
CLIENT=/mnt/projects/EverQuestP1999
cd /home/user/src/willeq-wt/kb-b1197-rec35-blue   # data/ resolution (render_all.sh)

for v in L T; do
  tag=ab-$v
  dir=$BASE/$tag-client
  rm -rf "$dir"; mkdir -p "$dir"
  ( cd "$CLIENT" && for f in *; do ln -s "$CLIENT/$f" "$dir/$f"; done )
  rm "$dir/spellsnew.edd"
  ln -s "$EDD/spellsnew_ab-$v.edd" "$dir/spellsnew.edd"
  echo "== $tag: spellsnew.edd -> $(readlink "$dir/spellsnew.edd")"
  rm -rf "$BASE/frames/$tag"; mkdir -p "$BASE/frames/$tag"
  WILLEQ_FX_LANDING_EMITTERS=432 "$BIN" --spell-test \
    --client "$dir" --eqe1 "$PACK" --plan "$PLAN" \
    --frames-dir "$BASE/frames/$tag" --fps 30 --seconds 16.8 --sim-hz 60 \
    --seed 1 --headless --camera b1130 > "$BASE/frames/$tag/run.log" 2>&1
  echo "RENDERED $tag ($(ls "$BASE/frames/$tag" | grep -c frame_) frames)"
done
echo AB_RENDERS_DONE
