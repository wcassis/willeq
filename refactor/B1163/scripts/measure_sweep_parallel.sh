#!/bin/bash
# B1163 (#482): measure the 40-record b1118 sweep in RUN in parallel (8 at a time).
# Skips records that already have a summary.tsv (the sequential pass did r0/r1/r2).
set -u
VPY=/home/user/.venvs/b1155/bin
export PATH="$VPY:$PATH"
EV=/home/user/kb-evidence/kb-b1163-missile-fu
FR=$EV/frames
MS=$EV/measure
S=/mnt/projects/fable/review/B1139/scripts/harness_measure.py
mkdir -p "$MS"

measure_one() {
  local d="$1"
  local n; n=$(basename "$d")
  if [ -f "$MS/$n.summary.tsv" ]; then return 0; fi
  if FX_SPELLS=auto FX_KEEP=1 "$VPY/python3" -I "$S" "$d" "$MS/$n.measure" > "$MS/$n.summary.tsv" 2> "$MS/$n.measure.log"; then
    echo "MEASURED $n"
  else
    echo "FAILED $n"
  fi
}
export -f measure_one
export MS S VPY

# 8 at a time
ls -d "$FR"/r*_* | sort -t_ -k1.2 -V | xargs -P 8 -I {} bash -c 'measure_one "$@"' _ {}

# assemble the combined summary (stable record order)
echo "spell" > "$MS/all_summary.tsv"
for d in $(ls -d "$FR"/r*_* | sort -t_ -k1.2 -V); do
  n=$(basename "$d")
  [ -f "$MS/$n.summary.tsv" ] && cat "$MS/$n.summary.tsv" >> "$MS/all_summary.tsv"
done
echo "=== PARALLEL DONE ==="
echo "measured: $(grep -c '^[0-9]' "$MS/all_summary.tsv") rows"
