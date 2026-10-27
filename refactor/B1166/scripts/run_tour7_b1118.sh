#!/usr/bin/env bash
# B1166 (#485): tour-7 on --camera b1118 (refactor/B1157/scripts/run_sweep_b1118.sh
# `tour` mode: the seven tour spells 93 38 54 656 41 253 458, target distance 0,
# caster level 60, cast_ms = the record representative's, t_begin 1.5 s, fps 30,
# seed 1), before (fable 4895adc2) and after (this branch), rendered on the
# ThinkPad into the NFS evidence dir and measured HERE with B1139's
# harness_measure.py (FX_KEEP=1 FX_SPELLS=auto) -- the proof that the b1118
# camera did not move: the frames are compared byte for byte by
# compare_tour7.py and the summary rows side by side.
# usage: run_tour7_b1118.sh <before|after>
set -u
VAR=${1:?before|after}
case "$VAR" in before) SLUG=kb-b1166-before ;; after) SLUG=kb-b1166-target-fit ;; *) exit 2 ;; esac
WT=/mnt/projects/fable/main/wt/$SLUG
EV=/mnt/projects/fable/review/B1166/worker/tour7_$VAR
BIN=/home/user/willeq-builds/$SLUG/build/bin/model_viewer
RM=/mnt/projects/fable/review/B1139/scripts/harness_measure.py
TP=${TP:-172.16.200.205}
mkdir -p "$EV/plans" "$EV/frames" "$EV/measure"
fail=0
while read -r sid tgt cms; do
  tag=tour_$sid
  plan="$EV/plans/$tag.plan"
  printf 't_begin_s\tspell_id\ttarget\tdistance_units\tcast_ms\tcaster_level\n1.5\t%s\t%s\t0\t%s\t60\n' "$sid" "$tgt" "$cms" > "$plan"
  secs=$(awk -v c="$cms" 'BEGIN{printf "%.1f", 1.5 + c/1000 + 11.5}')
  fd="$EV/frames/$tag"; rm -rf "$fd"; mkdir -p "$fd"
  echo "[$tag] rendering $(date -u +%T)" >> "$EV/sweep.log"
  if ! ssh "$TP" "cd '$WT' && WILLEQ_DRM_DEVICE=/dev/dri/no-such-card '$BIN' --spell-test \
      --client /mnt/projects/EverQuestP1999 --eqe1 /mnt/projects/EverQuest_ETC1/willeq.eqe1 --plan '$plan' \
      --frames-dir '$fd' --fps 30 --seconds $secs --seed 1 --headless --camera b1118 > '$fd/run.log' 2>&1" < /dev/null; then
    echo "[$tag] RENDER FAILED" >> "$EV/sweep.log"; fail=$((fail + 1)); continue
  fi
  m="$EV/measure/$tag"; rm -rf "$m"
  if FX_KEEP=1 FX_SPELLS=auto python3 -P "$RM" "$fd" "$m" < /dev/null > "$EV/measure_$tag.log" 2>&1; then
    echo "[$tag] done $(date -u +%T) -> $(awk -F'\t' -v s="$sid" '$1 == s' "$m/summary.tsv")" >> "$EV/sweep.log"
  else
    echo "[$tag] MEASURE FAILED" >> "$EV/sweep.log"; fail=$((fail + 1))
  fi
done <<'ROWS'
93 other 1500
38 other 1500
54 other 1500
656 other 1500
41 other 1500
253 self 1750
458 other 3150
ROWS
echo "$fail" > "$EV/sweep.exit"
exit "$fail"
