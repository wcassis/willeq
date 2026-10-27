#!/bin/bash
# B1151 (#471): the ORACLE per-frame motion rows for the 40 sweep records,
# from the B1130 measure dirs. The instrument is
# review/B1130/scripts/anchor.py -- the same per-frame rows its strips use
# (per active frame: cov, centroid h/dx, h_p5..p95, extent bands), SOURCE
# against the pre-cast baseline and LANDING against the tail baseline.
#
# MROOT (env, e.g. /home/user/b1151-oracle-frames): the per-frame PNGs were
# pruned from the NFS review copy (its measure dirs hold only the TSVs), so
# with MROOT set they are regenerated into $MROOT/<base> from each record's
# capture video with the tool that created the original dirs
# (review/B1122/scripts/fx_measure.py -- deterministic: same video + the
# meta.json args -> the same f*.png / frames.tsv / meta.json). With MROOT
# unset a frame-less dir is a miss (see miss.log).
#
# Rig per B1130's process_record.sh: fixed_self/fixed_npc caster feet 640,598
# model 209; fixed_npc target = kB1130TargetAnchor, read from its one definition
# tools/model_viewer/fx_target_anchor.h since B1166 (#485) (the oracle r51
# target's measured feet/height, refactor/B1166/README.md sec. 1). B1151's
# committed rows were measured at B1130's 940,652 / 325 (an extent that began at
# the name label): FX_TFEET=940,652 FX_TMPX=325 reproduce them. SOURCE is always caster
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
read -r TFEET TMPX < <(python3 - "$(dirname "$(realpath "$0")")/../../../tools/model_viewer/fx_target_anchor.h" <<'PY'
import re, sys
a = re.search(r'kB1130TargetAnchor\{([\d.]+)f, ([\d.]+)f, ([\d.]+)f\}', open(sys.argv[1]).read())
print(f'{float(a.group(1)):g},{float(a.group(2)):g}', f'{float(a.group(3)):g}')
PY
)
TFEET=${FX_TFEET:-$TFEET}; TMPX=${FX_TMPX:-$TMPX}
[ -n "$TFEET" ] && [ -n "$TMPX" ] || { echo "oracle_anchor.sh: no kB1130TargetAnchor" >&2; exit 2; }
set -u
R=/home/user/src/willeq-wt/kb-b1151-frames/refactor/B1151
B=/mnt/projects/fable/review/B1130
A=$B/scripts/anchor.py
MROOT=${MROOT:-}   # local frame root for the pruned per-frame PNGs (see header)
O=$R/oracle_anchor; mkdir -p $O
printf 'record\ttargettype\ttrig\tdir\n' > $O/rig.tsv
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
  src=$d
  if ! ls $d/f*.png >/dev/null 2>&1; then
    [ -n "$MROOT" ] || { echo "$rec: $base has no f*.png and MROOT unset" >> $O/miss.log; continue; }
    src=$MROOT/$base
    mkdir -p $src
    if ! ls $src/f*.png >/dev/null 2>&1; then
      read -r off tb ta tend crop vid < <(python3 - "$d/meta.json" "$B" <<'PY'
import json, sys
m = json.load(open(sys.argv[1]))
c = m['crop']
print(m['offset'], m['t_begincast'], m['t_action'], m['t_end'],
      f'{c[0]},{c[1]},{c[2]},{c[3]}', sys.argv[2] + '/' + m['video'])
PY
      )
      python3 /mnt/projects/fable/review/B1122/scripts/fx_measure.py \
        "$vid" "$off" "$tb" "$ta" "$tend" "$crop" "$src" --label "$base" \
        > "$O/${base}.regen.log" 2>&1 \
        || { echo "$rec: $base regen FAILED (see $O/${base}.regen.log)" >> $O/miss.log; continue; }
    fi
  fi
  if [ "$kind" = fixed_npc ]; then LF=$TFEET; LM=$TMPX
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
  python3 $A $src 640,598 209 SOURCE > $O/${base}_SOURCE.tsv 2>&1
  python3 $A $src $LF $LM LANDING > $O/${base}_LANDING.tsv 2>&1
  echo -e "$rec\t$tt\t$kind\t$base" >> $O/rig.tsv
  echo "$(date -u +%FT%TZ) $rec $kind ok" >> $O/log
done < <(awk -F'\t' 'NR>1 && $1!="51"{print $1"\t"$11"\t"$12"\t"$3}' $B/record_table.tsv)
echo "oracle_anchor done"
