#!/usr/bin/env bash
# B1173 (#492): r7 (spell 40) with the target at B1166's two placements, with and
# without the Middle-stage (missile) emitters. Renders on the ThinkPad with the
# DIAGNOSTIC binary built from this branch + review/B1173/worker/diag_no_middle.patch
# (FX_DIAG_TARGET=x,y,z overrides the solved b1130 target point and skips the
# B1166 self-check; FX_DIAG_NO_MIDDLE=1 makes launchMissile create no emitters).
# The patch is NOT committed: the shipped code has neither knob.
#
# usage: run_r7_diag.sh <run> ...   run in: tip after_mid before_mid after_nomid before_nomid
# (tip = no diagnostic env at all: the binary behaves as the branch tip, self-check on)
set -u
WT=/mnt/projects/fable/main/wt/kb-b1173-r7-source
EV=/mnt/projects/fable/review/B1173/worker/r7_diag
BIN=/home/user/willeq-builds/kb-b1173-r7-source/build/bin/model_viewer_b1173diag
PLAN=/mnt/projects/fable/review/B1166/worker/b1130_after/plans/r7.plan
TP=${TP:-172.16.200.205}
# B1166's two solved target points: after = probe/after.log:125, before = probe/before.log:109.
AFTER=-8.888,0.226,0.924
BEFORE=-6.702,0,9.732
mkdir -p "$EV"
for run in "$@"; do
  case "$run" in
    tip)          tgt=;        mid= ;;
    after_mid)    tgt=$AFTER;  mid= ;;
    before_mid)   tgt=$BEFORE; mid= ;;
    after_nomid)  tgt=$AFTER;  mid=1 ;;
    before_nomid) tgt=$BEFORE; mid=1 ;;
    *) echo "unknown run $run" >&2; exit 2 ;;
  esac
  fd="$EV/frames/$run"; rm -rf "$fd"; mkdir -p "$fd"
  env_mid=""; [ -n "$mid" ] && env_mid="FX_DIAG_NO_MIDDLE=1"
  env_tgt=""; [ -n "$tgt" ] && env_tgt="FX_DIAG_TARGET=$tgt"
  ssh "$TP" "cd '$WT' && env $env_tgt $env_mid '$BIN' --spell-test --client /mnt/projects/EverQuestP1999 \
      --eqe1 /mnt/projects/EverQuest_ETC1/willeq.eqe1 --plan '$PLAN' --frames-dir '$fd' --fps 30 --seconds 17.0 \
      --sim-hz 60 --seed 1 --headless --camera b1130 > '$fd/run.log' 2>&1" < /dev/null
  echo "$run render exit $?"
  m="$EV/measure/$run"; mkdir -p "$m"
  FX_KEEP=1 python3 -P "$WT/refactor/B1155/scripts/harness_measure_b1130.py" "$fd" "$m" fixed_npc > "$m/measure.log" 2>&1
  echo "$run measure exit $?"
done
