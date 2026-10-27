#!/usr/bin/env bash
# B1170 (#489): re-render named re-gate runs with one binary into another side
# (the harness run-to-run check). RUN ON THE THINKPAD.
# usage: rerender_tp.sh <model_viewer> <from_side> <to_side> <tag>...
# Re-uses <from_side>/plans/<tag>.plan and the camera / seconds / sim-hz the
# original run.log's command implied (read back from regate_tp.sh's rules).
set -u
BIN=$1; FROM=$2; TO=$3; shift 3
WT=/mnt/projects/fable/main/wt/kb-b1170-missile-exact
B=/home/user/kb-frames/kb-b1170-missile-exact
mkdir -p "$B/$TO"
for tag in "$@"; do
  plan=$B/$FROM/plans/$tag.plan
  cms=$(awk -F'\t' 'NR==2{print $5}' "$plan")
  case "$tag" in
    b1118_*) cam=b1118; secs=$(awk -v c="$cms" 'BEGIN{printf "%.1f", 1.5 + c/1000 + 11.5}'); sim="" ;;
    b1130_fixed_npc_*) cam=b1130; secs=$(awk -v c="$cms" 'BEGIN{printf "%.1f", 1.5 + c/1000 + 13.5}'); sim=60 ;;
    b1130_fixed_self_*) cam=b1130-self; secs=$(awk -v c="$cms" 'BEGIN{printf "%.1f", 1.5 + c/1000 + 13.5}'); sim=60 ;;
  esac
  run=$B/$TO/$tag; rm -rf "$run"; mkdir -p "$run"
  ( cd "$WT" && WILLEQ_DRM_DEVICE=/dev/dri/no-such-card "$BIN" --spell-test --client /mnt/projects/EverQuestP1999 \
      --eqe1 /mnt/projects/EverQuest_ETC1/willeq.eqe1 --plan "$plan" --frames-dir "$run" --fps 30 --seconds "$secs" \
      ${sim:+--sim-hz $sim} --seed 1 --headless --camera "$cam" > "$run/run.log" 2>&1 )
  echo "$tag rc=$?"
done
