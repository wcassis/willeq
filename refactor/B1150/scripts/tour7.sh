#!/bin/bash
# B1150 (#470): the seven tour spells (93 38 54 656 41 253 458) in the
# harness, one run each, tour geometry (--camera b1118, unoccluded, target at
# distance 0, caster level 60) -- the B1142 lane-5 rows are the baseline
# (review/B1142/gate/regression_table.tsv; the 93 reference is 3.049, NOT
# 2.938). Measured by harness_measure.py with FX_KEEP (run_scene.sh).
# usage: tour7.sh <tag>
# B1169 (#488): two tours of one binary are byte-identical frame for frame
# (--seed 1 = --fx-seed 1, the fixed solid draw order); prove "before/after
# identical" with scripts/tools/fx_frame_identity.py on the kept frames
# (tools/model_viewer/README.md "Determinism"), not by eye or by table cells.
# RUN ON THE THINKPAD (the rbuild tree and the client files live there).
tag=$1; R=/mnt/projects/fable/refactor/B1150; S=$R/scripts
F=/tmp/b1150-frames
mkdir -p $R/plans/tour
# spell, target (B1149's rule: self/group -> self), cast_ms (the record's
# representative's cast_ms from review/B1130/record_table.tsv: 38 -> record 0
# rep 93, 54/656 -> record 23 rep 372, 41 -> record 20 rep 341, 458 -> record
# 33 rep 382; 253 is its own record's representative).
rows="93 other 1500
38 other 1500
54 other 1500
656 other 1500
41 other 1500
253 self 1750
458 other 3150"
echo "$rows" | while read sid tgt cms; do
  p=$R/plans/tour/t${sid}.tsv
  printf 't_begin_s\tspell_id\ttarget\tdistance_units\tcast_ms\tcaster_level\n1.5\t%s\t%s\t0\t%s\t60\n' $sid $tgt $cms > $p
  secs=$(python3 -c "print(round(1.5 + $cms/1000.0 + 11.5, 1))")
  n=${tag}_tour_${sid}
  FX_SPELLS=auto NO_X264=1 PLAN=$p SECONDS_=$secs $S/run_scene.sh $n 30 --seed 1 < /dev/null
  rm -rf "$F/$n" "$F/$n.measure"
done
echo tour7 $tag done
