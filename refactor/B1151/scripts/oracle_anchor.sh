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
# has no fixed rig of that kind. Dir prefix: e<record>, or the capture
# column's name for the records B1130 captured under it (r51 -> record 0,
# r134 -> record 2, r131 -> record 1, r132 -> record 7, r103 -> record 9,
# r43 -> record 10, r40 -> record 12, r35 -> record 14, r139 -> record 20,
# r142 -> record 25, r84 -> record 40).
#
# usage: oracle_anchor.sh   (RUN ON THE THINKPAD)
# output: refactor/B1151/oracle_anchor/<dir>_<STAGE>.tsv + rig.tsv + .log
set -u
R=/home/user/src/willeq-wt/kb-b1151-frames/refactor/B1151
B=/mnt/projects/fable/review/B1130
A=$B/scripts/anchor.py
O=$R/oracle_anchor; mkdir -p $O
echo -n "record\ttargettype\trig\tdir\n" > $O/rig.tsv
rm -f $O/miss.log
while IFS=$'\t' read rec tt caps cap; do
  case $tt in self|group|group-teleport) pref=fixed_self;; *) pref=fixed_npc;; esac
  d=""; kind=""
  for pre in "e${rec}" "$cap"; do
    for k in $pref close_*; do
      [ "$k" = close_* ] && break
      if [ -f $B/measure/${pre}_${k}/frames.tsv ]; then d=$B/measure/${pre}_${k}; kind=$k; break 2; fi
    done
    # the close fallback: the first close_<kind> in the captures column whose
    # measure dir exists
    oldIFS=$IFS; IFS=','
    for c in $caps; do
      case $c in close_*) if [ -f $B/measure/${pre}_${c}/frames.tsv ]; then d=$B/measure/${pre}_${c}; kind=$c; break 2; fi;; esac
    done
    IFS=$oldIFS
  done
  [ -n "$d" ] || { echo "$rec: no measure dir ($tt, caps: $caps, cap: $cap)" >> $O/miss.log; continue; }
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
  python3 $A $d 640,598 209 SOURCE > $O/${base}_SOURCE.tsv 2>&1
  python3 $A $d $LF $LM LANDING > $O/${base}_LANDING.tsv 2>&1
  echo -e "$rec\t$tt\t$kind\t$base" >> $O/rig.tsv
  echo "$(date -u +%FT%TZ) $rec $kind ok" >> $O/log
done < <(awk -F'\t' 'NR>1 && $1!="51"{print $1"\t"$11"\t"$12"\t"$3}' $B/record_table.tsv)
echo "oracle_anchor done"
