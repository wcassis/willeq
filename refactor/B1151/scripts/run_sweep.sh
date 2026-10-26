#!/bin/bash
# B1151 (#471): the 40 B1130 records' representatives in the harness, one run
# each (tour geometry: --camera b1118, unoccluded, target 'other' at distance
# 0 like the lane tour, or self for a self/group spell), caster level 60,
# measured by harness_measure.py with FX_KEEP so anchor.py can read the
# frames; frames deleted after. Copy of refactor/B1150/scripts/run_sweep.sh
# (paths only: the runs land under this branch's refactor/B1151/, the frames
# in /tmp/b1151-frames, the model_viewer from the rbuild tree via
# run_scene.sh). ~2 h at 30 fps.
# usage: run_sweep.sh <tag>
# RUN ON THE THINKPAD (the rbuild tree and the client files live there).
set -u
tag=$1; R=/home/user/src/willeq-wt/kb-b1151-frames/refactor/B1151; S=$R/scripts
F=/tmp/b1151-frames
mkdir -p $R/plans/sweep
python3 - <<'PY' > $R/plans/sweep/list.tsv
import csv
for r in csv.DictReader(open('/mnt/projects/fable/review/B1130/record_table.tsv'), delimiter='\t'):
    if r['record'] in ('51',): continue
    tt = r['targettype']
    self_ = tt in ('self', 'group', 'group-teleport')
    print(r['record'], r['spell_id'], 'self' if self_ else 'other', r['cast_ms'], sep='\t')
PY
while IFS=$'\t' read rec sid tgt cms; do
  p=$R/plans/sweep/r${rec}_${sid}.tsv
  printf 't_begin_s\tspell_id\ttarget\tdistance_units\tcast_ms\tcaster_level\n1.5\t%s\t%s\t0\t%s\t60\n' $sid $tgt $cms > $p
  secs=$(python3 -c "print(round(1.5 + $cms/1000.0 + 11.5, 1))")
  n=${tag}_r${rec}_${sid}
  FX_SPELLS=auto NO_X264=1 PLAN=$p SECONDS_=$secs $S/run_scene.sh $n 30 --seed 1 < /dev/null
  # B1151 motion signatures (the issue's gate): per-frame centroid height +
  # lit extent from the FX_KEEP frames, measured with the B1130 instrument
  # (review/B1130/scripts/anchor.py: per active frame, cov, centroid h/dx,
  # h_p5..p95, extent bands) BEFORE the frames are deleted. Rig = b1118:
  # feet 640,605, model 235 (the fx_shape.py geometry,
  # review/B1139/scripts/harness_measure.py).
  O=$R/runs/$n
  for st in SOURCE LANDING; do
    python3 /mnt/projects/fable/review/B1130/scripts/anchor.py \
      "$F/$n.measure/fable_${sid}" 640,605 235 $st > "$O/anchor_${st}.tsv" 2>&1
  done
  rm -rf "$F/$n" "$F/$n.measure"
done < $R/plans/sweep/list.tsv
echo sweep $tag done
