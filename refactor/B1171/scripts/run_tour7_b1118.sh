#!/usr/bin/env bash
# B1171 (#490): tour-7 on --camera b1118 (B1166's run_tour7_b1118.sh layout: the
# seven tour spells 93 38 54 656 41 253 458, target distance 0, cast_ms = the
# record representative's, t_begin 1.5 s, fps 30, seed 1) with the caster level
# read from the B1118 oracle leg each spell's oracle rows come from
# (scripts/fx/capture_level.py b1118 <spell>: review/B1118/spell_fx_table.tsv
# `video` column -> oracle_leg / oracle_druid / oracle_enc plan.tsv + steps.tsv),
# not the literal 60 the earlier runners wrote. before = fable's binary with
# B1166's literal-60 plans; after = this branch with the capture's level. The
# level is logged per run and the run's timing.tsv level is checked against the
# leg's (a mismatch is refused, not measured). Measured HERE with B1139's
# harness_measure.py (FX_KEEP=1 FX_SPELLS=auto).
# usage: run_tour7_b1118.sh <before|after>
set -u
VAR=${1:?before|after}
case "$VAR" in before) SLUG=kb-b1171-before ;; after) SLUG=kb-b1171-sweep-level ;; *) exit 2 ;; esac
WT=/mnt/projects/fable/main/wt/$SLUG
LVL="python3 -I /mnt/projects/fable/main/wt/kb-b1171-sweep-level/scripts/fx/capture_level.py"
EV=/mnt/projects/fable/review/B1171/worker/tour7_$VAR
BIN=/home/user/willeq-builds/$SLUG/build/bin/model_viewer
RM=/mnt/projects/fable/review/B1139/scripts/harness_measure.py
TP=${TP:-172.16.200.205}
mkdir -p "$EV/plans" "$EV/frames" "$EV/measure"
fail=0
while read -r sid tgt cms; do
  tag=tour_$sid
  plan="$EV/plans/$tag.plan"
  if ! olevel=$($LVL b1118 "$sid" 2>> "$EV/sweep.log"); then
    echo "[$tag] NO ORACLE LEVEL -- not run" >> "$EV/sweep.log"; fail=$((fail + 1)); continue
  fi
  plevel=$olevel; [ "$VAR" = before ] && plevel=60  # B1166's literal
  printf 't_begin_s\tspell_id\ttarget\tdistance_units\tcast_ms\tcaster_level\n1.5\t%s\t%s\t0\t%s\t%s\n' "$sid" "$tgt" "$cms" "$plevel" > "$plan"
  secs=$(awk -v c="$cms" 'BEGIN{printf "%.1f", 1.5 + c/1000 + 11.5}')
  fd="$EV/frames/$tag"; rm -rf "$fd"; mkdir -p "$fd"
  leg=$(awk -F'\t' -v s="$sid" '$1 == s && $4 == "oracle" {split($5, v, "/"); print v[1]; exit}' /mnt/projects/fable/review/B1118/spell_fx_table.tsv)
  echo "[$tag] oracle leg $leg oracle_level $olevel plan_level $plevel rendering $(date -u +%T)" >> "$EV/sweep.log"
  if ! ssh "$TP" "cd '$WT' && WILLEQ_DRM_DEVICE=/dev/dri/no-such-card '$BIN' --spell-test \
      --client /mnt/projects/EverQuestP1999 --eqe1 /mnt/projects/EverQuest_ETC1/willeq.eqe1 --plan '$plan' \
      --frames-dir '$fd' --fps 30 --seconds $secs --seed 1 --headless --camera b1118 > '$fd/run.log' 2>&1" < /dev/null; then
    echo "[$tag] RENDER FAILED" >> "$EV/sweep.log"; fail=$((fail + 1)); continue
  fi
  hlevel=$($LVL harness "$fd" --plan "$plan" 2>> "$EV/sweep.log")
  if [ "$VAR" = after ] && ! $LVL check "$olevel" "$hlevel" >> "$EV/sweep.log" 2>&1; then
    echo "[$tag] harness_level $hlevel REFUSED vs oracle_level $olevel" >> "$EV/sweep.log"; fail=$((fail + 1)); continue
  fi
  m="$EV/measure/$tag"; rm -rf "$m"
  if FX_KEEP=1 FX_SPELLS=auto python3 -P "$RM" "$fd" "$m" < /dev/null > "$EV/measure_$tag.log" 2>&1; then
    echo "[$tag] harness_level $hlevel done $(date -u +%T) -> $(awk -F'\t' -v s="$sid" '$1 == s' "$m/summary.tsv")" >> "$EV/sweep.log"
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
