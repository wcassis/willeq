#!/bin/bash
# B1151 (#471): the ORACLE per-frame motion rows for the 40 sweep records,
# from the B1130 measure dirs (the full ones, with frames, live on the
# ThinkPad's NFS: /mnt/projects/fable/review/B1130/measure). The instrument
# is review/B1130/scripts/anchor.py -- the same per-frame rows its strips use
# (per active frame: cov, centroid h/dx, h_p5..p95, extent bands), SOURCE
# against the pre-cast baseline and LANDING against the tail baseline.
#
# Rig per B1130's process_record.sh: fixed_self/fixed_npc caster feet 640,598
# model 209; fixed_npc target feet 940,652 model 325. SOURCE is always caster
# units; LANDING is target units on fixed_npc, caster units on fixed_self and
# the close rigs (their per-capture feet/model from review/B1130/geom/e<N>.json,
# read off the close_settle stills with the ruler -- see process_record.sh).
# No --exclude: the harness side (b1118, occluder off) has no UI rectangles,
# so both sides are measured without exclusions.
#
# Rig choice per record: fixed_npc for 'other', fixed_self for self/group,
# the close_<kind> named in the record_table captures column when the record
# has no fixed rig of that kind.
#
# usage: oracle_anchor.sh   (RUN ON THE THINKPAD)
# output: refactor/B1151/oracle_anchor/<dir>_<STAGE>.tsv + rig.tsv + .log
set -u
R=/home/user/src/willeq-wt/kb-b1151-frames/refactor/B1151
B=/mnt/projects/fable/review/B1130
A=$B/scripts/anchor.py
O=$R/oracle_anchor; mkdir -p $O
echo -n "record\ttargettype\trig\tdir\n" > $O/rig.tsv
while IFS=$'\t' read rec tt caps; do
  case $tt in self|group|group-teleport) pref=fixed_self;; *) pref=fixed_npc;; esac
  d=$B/measure/e${rec}_${pref}
  kind=$pref
  if [ ! -f $d/frames.tsv ]; then
    # the close fallback: the first close_<kind> in the captures column whose
    # measure dir exists
    oldIFS=$IFS; IFS=','
    for c in $caps; do
      case $c in close_*) if [ -f $B/measure/e${rec}_${c}/frames.tsv ]; then kind=$c; d=$B/measure/e${rec}_${c}; break; fi;; esac
    done
    IFS=$oldIFS
  fi
  [ -f $d/frames.tsv ] || { echo "$rec: no measure dir ($tt, caps: $caps)" >> $O/miss.log; continue; }
  base=$(basename $d)
  if [ "$kind" = fixed_npc ]; then LF="940,652"; LM=325
  elif [ "$kind" = fixed_self ]; then LF="640,598"; LM=209
  else # close_<kind>: per-capture geometry from the ruler (process_record.sh)
    read -r LF LM < <(python3 - "$B/geom/e${rec}.json" "$kind" <<'PY'
import json, sys
g = json.load(open(sys.argv[1]))
e = g['cast_' + sys.argv[2]]
print(e['feet'], e['mpx'])
PY
)
  fi
  python3 -I $A $d 640,598 209 SOURCE > $O/${base}_SOURCE.tsv 2>&1
  python3 -I $A $d $LF $LM LANDING > $O/${base}_LANDING.tsv 2>&1
  echo -e "$rec\t$tt\t$kind\t$base" >> $O/rig.tsv
  echo "$(date -u +%FT%TZ) $rec $kind ok" >> $O/log
done < <(awk -F'\t' 'NR>1 && $1!="51"{print $1"\t"$11"\t"$12}' $B/record_table.tsv)
echo "oracle_anchor done"
