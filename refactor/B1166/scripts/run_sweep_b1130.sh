#!/usr/bin/env bash
# B1166 (#485): the b1130 40-record sweep, B1157's layout
# (refactor/B1157/scripts/run_sweep_b1130.sh: same plans -- t_begin 1.5 s, the
# record's representative spell_id / cast_ms / level from
# review/B1130/record_table.tsv, target distance 0 -- fps 30, sim 60 Hz, seed 1,
# fixed_npc -> --camera b1130, fixed_self -> --camera b1130-self), run from
# dev-u24: rendered ON the ThinkPad straight into the NFS evidence dir, measured
# HERE with the variant's OWN harness_measure_b1130.py (before = fable 4895adc2's
# copy, target anchor 940,652 / 325; after = this branch's, the anchor of
# tools/model_viewer/fx_target_anchor.h).
#
# usage: run_sweep_b1130.sh <before|after> [record ...]
# Progress: $EV/sweep.log; exit status $EV/sweep.exit (0 = all rendered + measured).
set -u
VAR=${1:?before|after}; shift
case "$VAR" in
  before) SLUG=kb-b1166-before ;;
  after)  SLUG=kb-b1166-target-fit ;;
  *) echo "variant must be before|after" >&2; exit 2 ;;
esac
WT=/mnt/projects/fable/main/wt/$SLUG
EV=/mnt/projects/fable/review/B1166/worker/b1130_$VAR
BIN=/home/user/willeq-builds/$SLUG/build/bin/model_viewer
CLIENT=/mnt/projects/EverQuestP1999
PACK=/mnt/projects/EverQuest_ETC1/willeq.eqe1
TABLE=/mnt/projects/fable/review/B1130/record_table.tsv
TP=${TP:-172.16.200.205}
mkdir -p "$EV/plans" "$EV/frames" "$EV/measure"
if [ $# -gt 0 ]; then recs=("$@"); else
  recs=(); while IFS= read -r rec; do
    [ "$rec" = record ] && continue; [ "$rec" = 51 ] && continue; recs+=("$rec")
  done < <(awk -F'\t' '{print $1}' "$TABLE")
fi
n=0; fail=0
for rec in "${recs[@]}"; do
  n=$((n + 1))
  line=$(awk -F'\t' -v r="$rec" '$1 == r {print $1"\t"$6"\t"$7"\t"$12"\t"$11"\t"$10"\t"$9}' "$TABLE")
  IFS=$'\t' read -r rec spell_id name captures targettype cast_ms caster_level <<< "$line"
  # B1171 (#490): column 9 is the CLASS level the spell is learned at (1 for r0), not
  # the caster's; the oracle capture ran `#level 60`. The plan casts at the level the
  # record's capture executed (its plan.tsv + steps.tsv). The committed tables of this
  # card were rendered at column 9 (refactor/B1171/README.md).
  caster_level=$(python3 -I "$(dirname "$0")/../../../scripts/fx/capture_level.py" b1130 "$rec") \
      || { echo "[$n] record $rec: no oracle #level" >> "$EV/sweep.log"; fail=$((fail + 1)); continue; }
  kind=fixed_self; case "$captures" in *fixed_npc*) kind=fixed_npc ;; esac
  cam=b1130-self; [ "$kind" = fixed_npc ] && cam=b1130
  tgt=other; case "$targettype" in self|group|group-teleport) tgt=self ;; esac
  secs=$(awk -v c="$cast_ms" 'BEGIN{printf "%.1f", 1.5 + c/1000 + 13.5}')
  tag="${kind}_r${rec}"
  mroot="$EV/measure/$tag"
  if [ -s "$mroot/b1130_rows.tsv" ]; then echo "[$n] $tag: already measured" >> "$EV/sweep.log"; continue; fi
  plan="$EV/plans/r${rec}.plan"
  printf 't_begin_s\tspell_id\ttarget\tdistance_units\tcast_ms\tcaster_level\tpackets\n1.5\t%s\t%s\t0\t%s\t%s\n' \
      "$spell_id" "$tgt" "$cast_ms" "$caster_level" > "$plan"
  fd="$EV/frames/$tag"; rm -rf "$fd"; mkdir -p "$fd"
  echo "[$n/${#recs[@]}] $tag (spell $spell_id $name, $tgt, cast ${cast_ms}ms, $secs s, $cam) rendering $(date -u +%T)" >> "$EV/sweep.log"
  if ! ssh "$TP" "cd '$WT' && '$BIN' --spell-test --client '$CLIENT' --eqe1 '$PACK' --plan '$plan' \
      --frames-dir '$fd' --fps 30 --seconds $secs --sim-hz 60 --seed 1 --headless --camera $cam \
      > '$fd/run.log' 2>&1" < /dev/null; then
    echo "[$n] $tag: RENDER FAILED (see $fd/run.log)" >> "$EV/sweep.log"; fail=$((fail + 1)); continue
  fi
  mkdir -p "$mroot"
  if FX_KEEP=1 python3 -P "$WT/refactor/B1155/scripts/harness_measure_b1130.py" "$fd" "$mroot" "$kind" \
      < /dev/null > "$mroot/measure.log" 2>&1; then
    echo "[$n] $tag: done $(date -u +%T)" >> "$EV/sweep.log"
  else
    echo "[$n] $tag: MEASURE FAILED (see $mroot/measure.log)" >> "$EV/sweep.log"; fail=$((fail + 1))
  fi
done
echo "done: ${#recs[@]} records, $fail failed" >> "$EV/sweep.log"
echo "$fail" > "$EV/sweep.exit"
exit "$fail"
