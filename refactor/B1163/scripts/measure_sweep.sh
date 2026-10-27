#!/bin/bash
# B1163 (#482): measure the 40-record b1118 sweep (D). RUN ON THE HOST.
# Frames were rsynced from the ThinkPad; the B1139 instrument runs HERE.
# The measure env is /home/user/.venvs/b1155 (cv2 5.0.0 + numpy 2.5.4 + scipy
# 1.18.1) -- the SAME env B1155/B1157 measured with, so the numbers reproduce
# B1157's b1118_table (r0/93 verified: 1.993/+6.633/0.385/+0.433/0.86).
# FX_SPELLS=auto: one spell per record dir.
set -u
VPY=/home/user/.venvs/b1155/bin
export PATH="$VPY:$PATH"
EV=/home/user/kb-evidence/kb-b1163-missile-fu
FR=$EV/frames
MS=$EV/measure
S=/mnt/projects/fable/review/B1139/scripts/harness_measure.py
mkdir -p "$MS"
rm -f "$MS"/*
echo "spell" > "$MS/all_summary.tsv"
for d in $(ls -d "$FR"/r*_* | sort -t_ -k1.2 -V); do
  n=$(basename "$d")
  if FX_SPELLS=auto FX_KEEP=1 "$VPY/python3" -I "$S" "$d" "$MS/$n.measure" > "$MS/$n.summary.tsv" 2> "$MS/$n.measure.log"; then
    cat "$MS/$n.summary.tsv" >> "$MS/all_summary.tsv"
    echo "MEASURED $n"
  else
    echo "FAILED $n (see $MS/$n.measure.log)"
  fi
done
echo "=== DONE ==="
