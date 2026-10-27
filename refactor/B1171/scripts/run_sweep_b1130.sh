#!/usr/bin/env bash
# B1171 (#490): the b1130 40-record sweep at the level the ORACLE capture ran.
# B1166's layout (refactor/B1166/scripts/run_sweep_b1130.sh: the record's
# representative spell_id / cast_ms / targettype from review/B1130/record_table.tsv,
# t_begin 1.5 s, target distance 0, fps 30, sim 60 Hz, seed 1, fixed_npc ->
# --camera b1130, fixed_self -> --camera b1130-self) with ONE change: the plan's
# caster_level is the `#level N` the record's capture executed
# (scripts/fx/capture_level.py b1130 <rec>: review/B1130/oracle/<capture>/plan.tsv
# + steps.tsv), never the table's `level` column (the class level, 1 for r0).
# The level is written into the plan, the sweep log line and (by the harness)
# timing.tsv + run.log; after the render the run's timing.tsv level is checked
# against the capture's and a mismatch is refused (not measured).
# Rendered ON the ThinkPad into the NFS evidence dir, measured HERE with
# refactor/B1155/scripts/harness_measure_b1130.py (the B1166 anchor).
#
# usage: run_sweep_b1130.sh <after|control> [record ...]
#   after   = this branch's binary, level from the capture (the re-run)
#   control = this branch's binary, B1166's own plan (table level) -- proves the
#             binary change is logging only: frames must equal B1166's after run
set -u
VAR=${1:?after|control}; shift
SLUG=kb-b1171-sweep-level
WT=/mnt/projects/fable/main/wt/$SLUG
EV=/mnt/projects/fable/review/B1171/worker/b1130_$VAR
B1166=/mnt/projects/fable/review/B1166/worker/b1130_after
BIN=/home/user/willeq-builds/$SLUG/build/bin/model_viewer
CLIENT=/mnt/projects/EverQuestP1999
PACK=/mnt/projects/EverQuest_ETC1/willeq.eqe1
TABLE=/mnt/projects/fable/review/B1130/record_table.tsv
LVL="python3 -I $WT/scripts/fx/capture_level.py"
TP=${TP:-172.16.200.205}
mkdir -p "$EV/plans" "$EV/frames" "$EV/measure"
LOG="$EV/sweep${SHARD:+.$SHARD}.log"  # SHARD=<name>: one log per concurrent shard
if [ $# -gt 0 ]; then recs=("$@"); else
  recs=(); while IFS= read -r rec; do
    [ "$rec" = record ] && continue; [ "$rec" = 51 ] && continue; recs+=("$rec")
  done < <(awk -F'\t' '{print $1}' "$TABLE")
fi
n=0; fail=0
for rec in "${recs[@]}"; do
  n=$((n + 1))
  line=$(awk -F'\t' -v r="$rec" '$1 == r {print $1"\t"$6"\t"$7"\t"$12"\t"$11"\t"$10"\t"$3}' "$TABLE")
  IFS=$'\t' read -r rec spell_id name captures targettype cast_ms capture <<< "$line"
  kind=fixed_self; case "$captures" in *fixed_npc*) kind=fixed_npc ;; esac
  cam=b1130-self; [ "$kind" = fixed_npc ] && cam=b1130
  tgt=other; case "$targettype" in self|group|group-teleport) tgt=self ;; esac
  secs=$(awk -v c="$cast_ms" 'BEGIN{printf "%.1f", 1.5 + c/1000 + 13.5}')
  tag="${kind}_r${rec}"
  mroot="$EV/measure/$tag"
  if [ -s "$mroot/b1130_rows.tsv" ]; then echo "[$n] $tag: already measured" >> "$LOG"; continue; fi
  if ! olevel=$($LVL b1130 "$rec" 2>> "$LOG"); then
    echo "[$n] $tag: NO ORACLE LEVEL (capture $capture) -- not run" >> "$LOG"; fail=$((fail + 1)); continue
  fi
  plan="$EV/plans/r${rec}.plan"
  if [ "$VAR" = control ]; then cp "$B1166/plans/r${rec}.plan" "$plan"; else
    printf 't_begin_s\tspell_id\ttarget\tdistance_units\tcast_ms\tcaster_level\tpackets\n1.5\t%s\t%s\t0\t%s\t%s\n' \
        "$spell_id" "$tgt" "$cast_ms" "$olevel" > "$plan"
  fi
  fd="$EV/frames/$tag"; rm -rf "$fd"; mkdir -p "$fd"; cp "$plan" "$fd/plan.tsv"
  echo "[$n/${#recs[@]}] $tag (spell $spell_id $name, $tgt, cast ${cast_ms}ms, $secs s, $cam) capture $capture oracle_level $olevel plan_level $(awk -F'\t' 'NR==2{print $6}' "$plan") rendering $(date -u +%T)" >> "$LOG"
  if ! ssh "$TP" "cd '$WT' && '$BIN' --spell-test --client '$CLIENT' --eqe1 '$PACK' --plan '$plan' \
      --frames-dir '$fd' --fps 30 --seconds $secs --sim-hz 60 --seed 1 --headless --camera $cam \
      > '$fd/run.log' 2>&1" < /dev/null; then
    echo "[$n] $tag: RENDER FAILED (see $fd/run.log)" >> "$LOG"; fail=$((fail + 1)); continue
  fi
  hlevel=$($LVL harness "$fd" 2>> "$LOG")
  if [ "$VAR" = after ] && ! $LVL check "$olevel" "$hlevel" >> "$LOG" 2>&1; then
    echo "[$n] $tag: harness_level $hlevel REFUSED vs oracle_level $olevel -- not measured" >> "$LOG"
    fail=$((fail + 1)); continue
  fi
  mkdir -p "$mroot"
  if FX_KEEP=1 python3 -P "$WT/refactor/B1155/scripts/harness_measure_b1130.py" "$fd" "$mroot" "$kind" \
      < /dev/null > "$mroot/measure.log" 2>&1; then
    echo "[$n] $tag: harness_level $hlevel done $(date -u +%T)" >> "$LOG"
  else
    echo "[$n] $tag: MEASURE FAILED (see $mroot/measure.log)" >> "$LOG"; fail=$((fail + 1))
  fi
done
echo "done: ${#recs[@]} records, $fail failed" >> "$LOG"
echo "$fail" > "$EV/sweep${SHARD:+.$SHARD}.exit"
exit "$fail"
