#!/usr/bin/env bash
# B1180 (#499): re-run the b1118 sweep row-anchors.
#
# The row's frames already exist on the ThinkPad (the gate streamed them once);
# this re-streams each row, re-measures (regenerating the FX_KEEP per-spell
# frames the anchor reads), runs B1130's anchor fit for SOURCE and LANDING,
# then frees the local blobs (the full frames stay on the ThinkPad).
#
# usage: reanchor_b1118.sh            # every row missing either anchor file
#        reanchor_b1118.sh 93 38 ...  # just those rows
set -u
TP=${TP:-thinkpad}
SLUG=kb-b1180-sprite-width
GATE=/home/user/kb-frames/$SLUG/gate/after
EV=/home/user/kb-evidence/$SLUG/gate/after
RM=/mnt/projects/fable/review/B1139/scripts/harness_measure.py
ANCHOR=/mnt/projects/fable/review/B1130/scripts/anchor.py
LOG="$EV/reanchor.log"

missing=()
for d in "$EV"/measure/b1118_r*/; do
  t=$(basename "$d")
  [ -s "$d/anchor_SOURCE.tsv" ] && [ -s "$d/anchor_LANDING.tsv" ] && continue
  missing+=("$t")
done
if [ $# -gt 0 ]; then
  missing=()
  for sid in "$@"; do missing+=("b1118_r$sid"); done
fi

fail=0
for t in "${missing[@]}"; do
  sid=${t#b1118_r}
  fd="$EV/frames/$t"; m="$EV/measure/$t"
  echo "[$t] stream $(date -u +%T)" >> "$LOG"
  mkdir -p "$fd" "$m"
  rsync -a --delete "$TP:$GATE/frames/$t/" "$fd/" < /dev/null || { echo "[$t] rsync FAILED" >> "$LOG"; fail=$((fail+1)); continue; }
  if FX_KEEP=1 FX_SPELLS=auto python3 -P "$RM" "$fd" "$m" < /dev/null > "$m/measure_reanchor.log" 2>&1; then
    for st in SOURCE LANDING; do
      python3 "$ANCHOR" "$m/fable_${sid}" 640,605 235 "$st" < /dev/null > "$m/anchor_${st}.tsv" 2>&1 || echo "[$t] anchor $st FAILED" >> "$LOG"
    done
    echo "[$t] done $(date -u +%T)" >> "$LOG"
  else
    echo "[$t] MEASURE FAILED (see $m/measure_reanchor.log)" >> "$LOG"; fail=$((fail+1))
  fi
  rm -rf "$fd" "$m/video.mkv" "$m"/fable_* 2>/dev/null
done
echo "reanchor finished $(date -u +%FT%T): rows=${#missing[@]} fails=$fail" >> "$LOG"
tail -1 "$LOG"
