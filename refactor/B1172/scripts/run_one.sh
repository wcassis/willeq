#!/usr/bin/env bash
# B1172 (#491): render ONE job (a row of make_jobs.py's list) with the
# variant's model_viewer ON the ThinkPad straight into the NFS evidence dir,
# then measure it HERE: b1118 rows with B1139's harness_measure.py
# (FX_KEEP=1 FX_SPELLS=auto, as B1157/B1166), b1130 rows with this tree's
# refactor/B1155/scripts/harness_measure_b1130.py (as B1157/B1166).
# usage: run_one.sh <before|after> <tag> <camera> <seconds> <sim_hz|-> <kind> <plan_text>
set -u
VAR=$1 TAG=$2 CAM=$3 SECS=$4 HZ=$5 KIND=$6 PLAN=$7
# the variant's own tree: before = fable 070a9215 (wt/kb-b1172-anchor), after = this branch
WT=/mnt/projects/fable/main/wt/kb-b1172-model-height; [ "$VAR" = before ] && WT=/mnt/projects/fable/main/wt/kb-b1172-anchor
EV=/mnt/projects/fable/review/B1172/worker/runs_$VAR
BIN=/home/user/willeq-builds/kb-b1172-model-height/model_viewer.$VAR
RM=/mnt/projects/fable/review/B1139/scripts/harness_measure.py
TP=${TP:-172.16.200.205}
mkdir -p "$EV/plans" "$EV/frames" "$EV/measure"
m="$EV/measure/$TAG"
if [ -s "$m/summary.tsv" ] || [ -s "$m/b1130_rows.tsv" ]; then echo "[$TAG] already measured" >> "$EV/sweep.log"; exit 0; fi
printf '%b\n' "$PLAN" > "$EV/plans/$TAG.plan"
fd="$EV/frames/$TAG"; rm -rf "$fd"; mkdir -p "$fd"
hz=""; [ "$HZ" != "-" ] && hz="--sim-hz $HZ"
echo "[$TAG] rendering $(date -u +%T)" >> "$EV/sweep.log"
if ! ssh "$TP" "cd '$WT' && WILLEQ_DRM_DEVICE=/dev/dri/no-such-card '$BIN' --spell-test \
    --client /mnt/projects/EverQuestP1999 --eqe1 /mnt/projects/EverQuest_ETC1/willeq.eqe1 \
    --plan '$EV/plans/$TAG.plan' --frames-dir '$fd' --fps 30 --seconds $SECS $hz --seed 1 --headless \
    --camera $CAM > '$fd/run.log' 2>&1" < /dev/null; then
  echo "[$TAG] RENDER FAILED" >> "$EV/sweep.log"; exit 1
fi
rm -rf "$m"; mkdir -p "$m"
if [ "$KIND" = b1118 ]; then
  FX_KEEP=1 FX_SPELLS=auto python3 -P "$RM" "$fd" "$m" < /dev/null > "$m/measure.log" 2>&1
else
  FX_KEEP=1 python3 -P "$WT/refactor/B1155/scripts/harness_measure_b1130.py" "$fd" "$m" "$KIND" < /dev/null > "$m/measure.log" 2>&1
fi
rc=$?
[ $rc -eq 0 ] && echo "[$TAG] done $(date -u +%T)" >> "$EV/sweep.log" || echo "[$TAG] MEASURE FAILED" >> "$EV/sweep.log"
exit $rc
