#!/bin/bash
# B1181 fix leg (t_613a8781): regenerate the acceptance-B census plans,
# EXACTLY the run_b1186.sh plan recipe (tour7 + b1130 sets, same spells,
# same anchors, same secs formula, same capture_level oracle levels).
set -eu
WT=/home/user/src/willeq-wt/kb-b1181-item-missile
EV=/home/user/kb-evidence/kb-b1181-item-missile/gate_census
PL=$EV/plans
mkdir -p "$PL"
LVL="python3 -I $WT/scripts/fx/capture_level.py"
TABLE=/mnt/projects/fable/review/B1130/record_table.tsv

# rows.tsv: tag cam secs drm olevel sid kind   (measured facts for the README)
ROWS=$EV/rows.tsv
: > "$ROWS"

for line in "93 other 1500" "38 other 1500" "54 other 1500" "656 other 1500" "41 other 1500" "253 self 1750" "458 other 3150"; do
  IFS=' ' read -r sid tgt cms <<< "$line"
  tag=tour_$sid
  olevel=$($LVL b1118 "$sid")
  secs=$(awk -v c="$cms" 'BEGIN{printf "%.1f", 1.5 + c/1000 + 11.5}')
  printf 't_begin_s\tspell_id\ttarget\tdistance_units\tcast_ms\tcaster_level\n1.5\t%s\t%s\t0\t%s\t%s\n' "$sid" "$tgt" "$cms" "$olevel" > "$PL/$tag.plan"
  printf '%s\tb1118\t%s\t1\t%s\t%s\tsummary\n' "$tag" "$secs" "$olevel" "$sid" >> "$ROWS"
done

for rec in $(awk -F'\t' 'NR > 1 && $1 != 51 {print $1}' "$TABLE"); do
  line=$(awk -F'\t' -v r="$rec" '$1 == r {print $1"\t"$6"\t"$7"\t"$12"\t"$11"\t"$10}' "$TABLE")
  IFS=$'\t' read -r rec sid name captures ttype cms <<< "$line"
  kind=fixed_self; case "$captures" in *fixed_npc*) kind=fixed_npc ;; esac
  cam=b1130-self; [ "$kind" = fixed_npc ] && cam=b1130
  tgt=other; case "$ttype" in self|group|group-teleport) tgt=self ;; esac
  tag="b1130_r${rec}"
  olevel=$($LVL b1130 "$rec")
  secs=$(awk -v c="$cms" 'BEGIN{printf "%.1f", 1.5 + c/1000 + 13.5}')
  printf 't_begin_s\tspell_id\ttarget\tdistance_units\tcast_ms\tcaster_level\n1.5\t%s\t%s\t0\t%s\t%s\n' "$sid" "$tgt" "$cms" "$olevel" > "$PL/$tag.plan"
  printf '%s\t%s\t%s\t0\t%s\t%s\tb1130\n' "$tag" "$cam" "$secs" "$olevel" "$sid" >> "$ROWS"
done

echo "plans: $(ls "$PL" | wc -l)  rows: $(wc -l < "$ROWS")"
cat "$ROWS"
