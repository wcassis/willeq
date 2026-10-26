#!/bin/bash
# B1151 (#471): the eight scenes whose render state CHANGED with the +0x64
# depth-write wiring (commit 10c826a3, the DepthWindow sub-passes): the seven
# sweep records with +0x64 == 0 (1, 11, 35, 40, 41, 42, 43 --
# edd_census.py's word over the same file; the official client's
# ZWRITEENABLE default is ON, EQGraphicsDX9 0x1005f7a8/0x1005f82c) and tour
# spell 253 (record 35). Run on the ThinkPad AFTER the main sweep finished,
# so every row in the gate table is measured on the final binary. The other
# 39 scenes are render-identical on both binaries (their .on sub-windows are
# empty: same pack order, same single mask-OFF draw). Re-runs the exact run
# names, plans and anchor rows of run_sweep.sh / tour7.sh.
# usage: rerun_depth64.sh
set -u
R=/home/user/src/willeq-wt/kb-b1151-frames/refactor/B1151; S=$R/scripts
F=/tmp/b1151-frames
L=$R/plans/sweep/list.tsv
for rec in 1 11 35 40 41 42 43; do
  row=$(grep -P "^$rec\t" $L)
  sid=$(cut -f2 <<<"$row"); tgt=$(cut -f3 <<<"$row"); cms=$(cut -f4 <<<"$row")
  p=$R/plans/sweep/r${rec}_${sid}.tsv
  printf 't_begin_s\tspell_id\ttarget\tdistance_units\tcast_ms\tcaster_level\n1.5\t%s\t%s\t0\t%s\t60\n' $sid $tgt $cms > $p
  secs=$(python3 -c "print(round(1.5 + $cms/1000.0 + 11.5, 1))")
  n=b1151_r${rec}_${sid}
  FX_SPELLS=auto NO_X264=1 PLAN=$p SECONDS_=$secs $S/run_scene.sh $n 30 --seed 1 < /dev/null
  O=$R/runs/$n
  for st in SOURCE LANDING; do
    python3 /mnt/projects/fable/review/B1130/scripts/anchor.py \
      "$F/$n.measure/fable_${sid}" 640,605 235 $st > "$O/anchor_${st}.tsv" 2>&1
  done
  rm -rf "$F/$n" "$F/$n.measure"
done
# tour 253 (record 35) -- same plan the tour wrote
p=$R/plans/tour/t253.tsv
secs=$(python3 -c "print(round(1.5 + 1750/1000.0 + 11.5, 1))")
n=b1151_tour_253
FX_SPELLS=auto NO_X264=1 PLAN=$p SECONDS_=$secs $S/run_scene.sh $n 30 --seed 1 < /dev/null
O=$R/runs/$n
for st in SOURCE LANDING; do
  python3 /mnt/projects/fable/review/B1130/scripts/anchor.py \
    "$F/$n.measure/fable_253" 640,605 235 $st > "$O/anchor_${st}.tsv" 2>&1
done
rm -rf "$F/$n" "$F/$n.measure"
echo "RERUN_DEPTH64_DONE $(date -u +%FT%TZ)"
