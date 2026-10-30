#!/usr/bin/env bash
# B1162 /disc record leg (card t_a74670e3): render the /disc harness rows for
# records 325/326 -- 8604 Leechcurse Discipline (record 325, CastingAnim 44 = t06)
# and 4499 Defensive Discipline (record 326, CastingAnim 27 = s01) -- on the
# ThinkPad with the FX harness (model_viewer --spell-test, action-only /disc
# self-cast, b1130-self camera, the geometry of the previous legs' plans),
# pull the frames back to hermes-ext, and measure HERE with the committed
# B1132-instrument fork refactor/B1162/scripts/harness_measure_b1162_anchor.py
# (fx_measure.py + fx_timing.py + landing_onset_abs.py + anchor.py at the preset
# and the rendered anchors).
#
# usage: disc_harness_rows.sh <worktree> <slug> <out_root> [tags ...]
#   <worktree>  the repo checkout that owns the rendered binary (its tip is
#               stamped into <out_root>/render_env.tsv)
#   <slug>      the worktree slug; the binary is
#               /home/user/willeq-builds/<slug>/build/bin/model_viewer
#   <out_root>  the evidence root (e.g. ~/kb-evidence/kb-b1162-disc-record);
#               frames land in <out_root>/frames/<tag>/ (kept, ~80 MB) and the
#               measure in <out_root>/measure/<tag>/
#   [tags ...]  default: disc325_8604 disc326_4499
#
# The render is the B1162 leg's own recipe (refactor/B1162/scripts/
# run_harness.sh's pattern, B1157 heritage): 30 fps, sim 60 Hz, seed 1,
# 16.0 s (= t_begin 1.5 + cast 0 + tail 14.5, B1157's formula), which carries
# the ledger window BC-1.0..Act+8.0 (270 frames).
set -euo pipefail
WT=${1:?worktree}; SLUG=${2:?slug}; OUT=${3:?out root}; shift 3 || shift $#
TP=${TP:-thinkpad}
BIN="/home/user/willeq-builds/$SLUG/build/bin/model_viewer"
CLIENT=/mnt/projects/EverQuestP1999
PACK=/mnt/projects/EverQuest_ETC1/willeq.eqe1
TPF="/home/user/kb-frames/$SLUG/disc-rows"
FPS=30; SIM_HZ=60; SEED=1; SECS=16.0; CAM=b1130-self
TIP=$(git -C "$WT" rev-parse HEAD)
mkdir -p "$OUT/frames" "$OUT/measure"
printf 'tip\t%s\t%s\tpack\t%s\tbin\t%s\tsim_hz\t%s\tseed\t%s\tfps\t%s\tsecs\t%s\tcamera\t%s\n' \
  "$TIP" "$(date -u +%FT%T)" "$PACK" "$BIN" "$SIM_HZ" "$SEED" "$FPS" "$SECS" "$CAM" > "$OUT/render_env.tsv"
[ $# -gt 0 ] && TAGS=("$@") || TAGS=(disc325_8604 disc326_4499)
for TAG in "${TAGS[@]}"; do
  case "$TAG" in
    disc325_8604) SID=8604;;
    disc326_4499) SID=4499;;
    *) echo "unknown tag $TAG (want disc325_8604 / disc326_4499)" >&2; exit 2;;
  esac
  PLAN="$WT/refactor/B1162/plans/harness_$SID.plan"
  F="$OUT/frames/$TAG"; M="$OUT/measure/$TAG"
  if [ -s "$M/anchor_table.tsv" ]; then echo "[$TAG] already measured"; continue; fi
  rsync -a "$PLAN" "$TP:$TPF/${TAG}.plan" < /dev/null
  # The b1130-camera rows render SURFACELESS on the Quadro at the native
  # 1280x800 (render_gate.sh, Will 2026-10-29): WILLEQ_DRM_DEVICE points at a
  # non-existent card so the DRM path is refused and the surfaceless EGL
  # platform takes over -- the same path the 47-row gate and its baselines use.
  ssh -n "$TP" "mkdir -p '$TPF/$TAG' && cd '$WT' && WILLEQ_DRM_DEVICE=/dev/dri/no-such-card '$BIN' --spell-test --client '$CLIENT' --eqe1 '$PACK' \
      --plan '$TPF/${TAG}.plan' --frames-dir '$TPF/$TAG' --fps $FPS --seconds $SECS --sim-hz $SIM_HZ \
      --seed $SEED --headless --camera $CAM > '$TPF/$TAG/run.log' 2>&1" \
    || { echo "[$TAG] RENDER FAILED (ThinkPad $TPF/$TAG/run.log)" >&2; exit 1; }
  rm -rf "$F"; mkdir -p "$F" "$M"
  rsync -a "$TP:$TPF/$TAG/" "$F/" < /dev/null
  ssh -n "$TP" "rm -rf '$TPF/$TAG'" < /dev/null
  grep -q '\[fx-harness\] wrote .* frames' "$F/run.log" \
    || { echo "[$TAG] no [fx-harness] wrote line in run.log:" >&2; tail -5 "$F/run.log" >&2; exit 1; }
  if ! (cd "$WT" && FX_KEEP=1 python3 -I "$WT/refactor/B1162/scripts/harness_measure_b1162_anchor.py" \
        "$F" "$M" fixed_self > "$M/measure_stdout.txt" 2>&1); then
    echo "[$TAG] MEASURE FAILED (see $M/measure.log)" >&2; exit 1
  fi
  echo "[$TAG] rendered + measured ($F, $M/anchor_table.tsv)"
done
echo "[disc-rows] done: $(ls "$OUT"/measure/*/anchor_table.tsv 2>/dev/null | wc -l) rows measured"
