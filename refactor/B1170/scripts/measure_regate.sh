#!/usr/bin/env bash
# B1170 (#489): measure the re-gate runs whose frames differ before/after
# (regate/identity_tip.tsv), both sides, with the instruments B1157 used:
# B1139's harness_measure.py (b1118 rows, FX_KEEP=1 FX_SPELLS=auto) and
# B1155's harness_measure_b1130.py (b1130 rows, kind fixed_npc / fixed_self).
# RUN ON dev-u24 (cv2 in the user site: python3 -P / plain python3).
# usage: measure_regate.sh <identity.tsv> <evidence_root> <side_before> <side_after>
set -u
ID=$1; EV=$2; SIDES="$3 $4"
R39=/mnt/projects/fable/review/B1139/scripts
B55=/mnt/projects/fable/main/wt/kb-b1170-missile-exact/refactor/B1155/scripts/harness_measure_b1130.py
awk -F'\t' 'NR > 1 && $4 != 0 {print $1}' "$ID" | while read -r tag; do
  for side in $SIDES; do
    fd=$EV/frames/$side/$tag; m=$EV/measure/$side/$tag
    mkdir -p "$fd" "$m"
    rsync -a "172.16.200.205:/home/user/kb-frames/kb-b1170-missile-exact/$side/$tag/" "$fd/" < /dev/null
    case "$tag" in
      b1118_*) FX_KEEP=1 FX_SPELLS=auto python3 -I "$R39/harness_measure.py" "$fd" "$m" < /dev/null > "$m/measure.log" 2>&1 ;;
      b1130_fixed_npc_*) python3 "$B55" "$fd" "$m" fixed_npc < /dev/null > "$m/measure.log" 2>&1 ;;
      b1130_fixed_self_*) python3 "$B55" "$fd" "$m" fixed_self < /dev/null > "$m/measure.log" 2>&1 ;;
    esac
    echo "$tag $side rc=$?"
  done
done
