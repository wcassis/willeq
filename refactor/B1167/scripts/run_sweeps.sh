#!/usr/bin/env bash
# B1167 (#486): the before/after gate renders -- the b1130 40-record sweep (B1166's
# run_sweep_b1130.sh layout: t_begin 1.5 s, the record's representative spell / cast_ms from
# review/B1130/record_table.tsv, target distance 0, secs 1.5 + cast + 13.5, fps 30, sim 60 Hz,
# seed 1, fixed_npc -> --camera b1130, fixed_self -> --camera b1130-self) but at caster level 60
# EXPLICITLY (the B1130 captures' #level 60; B1166 FOLLOW-UP 1, B1171 #490), tour-7 on --camera
# b1118 (B1166's run_tour7_b1118.sh rows) and the 40 records on --camera b1118 (B1157's
# run_sweep_b1118.sh plans: level 60, secs 1.5 + cast + 11.5). Rendered ON the ThinkPad into the
# NFS evidence dir, measured HERE: b1130 by B1155's harness_measure_b1130.py (fixed_npc /
# fixed_self), b1118 by B1139's harness_measure.py (FX_KEEP=1 FX_SPELLS=auto).
#   before = fable f61fafb4 (wt/kb-b1167-before), after = this branch (wt/kb-b1167-record104).
# usage: run_sweeps.sh <before|after> [parallel=3]
set -u
VAR=${1:?before|after}; PAR=${2:-3}
case "$VAR" in before) SLUG=kb-b1167-before ;; after) SLUG=kb-b1167-record104 ;; *) exit 2 ;; esac
WT=/mnt/projects/fable/main/wt/$SLUG
EV=/mnt/projects/fable/review/B1167/worker/sweeps_$VAR
BIN=/home/user/willeq-builds/$SLUG/build/bin/model_viewer
TABLE=/mnt/projects/fable/review/B1130/record_table.tsv
RM=/mnt/projects/fable/review/B1139/scripts/harness_measure.py
MB=/mnt/projects/fable/main/wt/kb-b1167-record104/refactor/B1155/scripts/harness_measure_b1130.py
TP=${TP:-172.16.200.205}
mkdir -p "$EV/plans" "$EV/frames" "$EV/measure"
one() { # tag camera spell target cast_ms secs kind
  local tag=$1 cam=$2 sid=$3 tgt=$4 cms=$5 secs=$6 kind=$7
  local plan="$EV/plans/$tag.plan" fd="$EV/frames/$tag" m="$EV/measure/$tag"
  if [ -s "$m/b1130_rows.tsv" ] || [ -s "$m/summary.tsv" ]; then return 0; fi
  printf 't_begin_s\tspell_id\ttarget\tdistance_units\tcast_ms\tcaster_level\n1.5\t%s\t%s\t0\t%s\t60\n' "$sid" "$tgt" "$cms" > "$plan"
  rm -rf "$fd" "$m"; mkdir -p "$fd" "$m"
  if ! ssh "$TP" "cd '$WT' && WILLEQ_DRM_DEVICE=/dev/dri/no-such-card '$BIN' --spell-test \
      --client /mnt/projects/EverQuestP1999 --eqe1 /mnt/projects/EverQuest_ETC1/willeq.eqe1 --plan '$plan' \
      --frames-dir '$fd' --fps 30 --seconds $secs --sim-hz 60 --seed 1 --headless --camera $cam > '$fd/run.log' 2>&1" < /dev/null; then
    echo "[$tag] RENDER FAILED" >> "$EV/sweep.log"; return 1
  fi
  if [ "$kind" = b1118 ]; then
    FX_KEEP=1 FX_SPELLS=auto python3 -P "$RM" "$fd" "$m" < /dev/null > "$m/measure.log" 2>&1
  else
    FX_KEEP=1 python3 -P "$MB" "$fd" "$m" "$kind" < /dev/null > "$m/measure.log" 2>&1
  fi || { echo "[$tag] MEASURE FAILED" >> "$EV/sweep.log"; return 1; }
  echo "[$tag] done $(date -u +%T)" >> "$EV/sweep.log"
}
jobs_list=()
while IFS=$'\t' read -r rec sid name caps ttype cms; do
  [ "$rec" = record ] && continue; [ "$rec" = 51 ] && continue
  kind=fixed_self; case "$caps" in *fixed_npc*) kind=fixed_npc ;; esac
  cam=b1130-self; [ $kind = fixed_npc ] && cam=b1130
  tgt=other; case "$ttype" in self|group|group-teleport) tgt=self ;; esac
  jobs_list+=("${kind}_r$rec $cam $sid $tgt $cms $(awk -v c="$cms" 'BEGIN{printf "%.1f", 1.5 + c/1000 + 13.5}') $kind")
  jobs_list+=("b1118_r${rec}_$sid b1118 $sid $tgt $cms $(awk -v c="$cms" 'BEGIN{printf "%.1f", 1.5 + c/1000 + 11.5}') b1118")
done < <(awk -F'\t' '{print $1"\t"$6"\t"$7"\t"$12"\t"$11"\t"$10}' "$TABLE")
for r in "93 other 1500" "38 other 1500" "54 other 1500" "656 other 1500" "41 other 1500" "253 self 1750" "458 other 3150"; do
  set -- $r
  jobs_list+=("tour_$1 b1118 $1 $2 $3 $(awk -v c="$3" 'BEGIN{printf "%.1f", 1.5 + c/1000 + 11.5}') b1118")
done
fail=0
for j in "${jobs_list[@]}"; do
  one $j &
  while [ "$(jobs -rp | wc -l)" -ge "$PAR" ]; do wait -n || fail=$((fail + 1)); done
done
while [ "$(jobs -rp | wc -l)" -gt 0 ]; do wait -n || fail=$((fail + 1)); done
echo "done: ${#jobs_list[@]} runs, $fail failed" >> "$EV/sweep.log"
echo "$fail" > "$EV/sweep.exit"
