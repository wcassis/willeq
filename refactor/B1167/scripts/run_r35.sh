#!/usr/bin/env bash
# B1167 (#486): render record 35 (spell 253, Whirling Wind, record 104) on --camera b1130
# (or b1118) at an explicit caster level, B1157/B1166's sweep layout otherwise (t_begin 1.5 s,
# cast 1750 ms, target other at distance 0, fps 30, sim 60 Hz, seed 1); render ON the ThinkPad
# into the NFS evidence dir, measure HERE with B1155's harness_measure_b1130.py (b1130, the
# kB1130TargetAnchor) or B1139's harness_measure.py (b1118). Extra env for the renderer is
# passed through RENV (e.g. RENV='WILLEQ_FX_SLOTMASK=...').
#
# usage: run_r35.sh <slug> <tag> <camera b1130|b1118> <caster_level>
set -u
SLUG=$1; TAG=$2; CAM=$3; LVL=$4
WT=/mnt/projects/fable/main/wt/$SLUG
EV=/mnt/projects/fable/review/B1167/worker/runs/$TAG
BIN=/home/user/willeq-builds/$SLUG/build/bin/model_viewer
TP=${TP:-172.16.200.205}
case "$CAM" in b1130) SECS=16.8 ;; b1118) SECS=14.8 ;; *) echo bad cam >&2; exit 2 ;; esac
rm -rf "$EV"; mkdir -p "$EV/frames" "$EV/measure"
printf 't_begin_s\tspell_id\ttarget\tdistance_units\tcast_ms\tcaster_level\tpackets\n1.5\t253\tother\t0\t1750\t%s\n' "$LVL" > "$EV/plan"
ssh "$TP" "cd '$WT' && env ${RENV:-} '$BIN' --spell-test --client /mnt/projects/EverQuestP1999 \
  --eqe1 /mnt/projects/EverQuest_ETC1/willeq.eqe1 --plan '$EV/plan' --frames-dir '$EV/frames' \
  --fps 30 --seconds $SECS --sim-hz 60 --seed 1 --headless --camera $CAM > '$EV/run.log' 2>&1" < /dev/null \
  || { echo "render failed: $EV/run.log"; exit 1; }
if [ "$CAM" = b1130 ]; then
  FX_KEEP=1 python3 -P "$WT/refactor/B1155/scripts/harness_measure_b1130.py" "$EV/frames" "$EV/measure" fixed_npc \
    < /dev/null > "$EV/measure.log" 2>&1 || { echo "measure failed"; exit 1; }
  cat "$EV/measure/b1130_rows.tsv"
else
  FX_KEEP=1 FX_SPELLS=auto python3 -P /mnt/projects/fable/review/B1139/scripts/harness_measure.py "$EV/frames" "$EV/measure" \
    < /dev/null > "$EV/measure.log" 2>&1 || { echo "measure failed"; exit 1; }
  cat "$EV/measure/summary.tsv"
fi
