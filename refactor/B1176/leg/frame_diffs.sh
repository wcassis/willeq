#!/bin/bash
# B1176 (#495): consecutive-frame pixel diffs (ImageMagick AE metric) for a
# fable --dump-frames-dir run, so the stable camera segments can be located
# without pulling the frames back (frames stay on the ThinkPad; only this
# TSV and the selected frames are pulled). The fable tour holds still
# between commands, so a stable segment's AE is small and a camera
# transition is a big one. usage: frame_diffs.sh <frames_dir> <out.tsv>
set -u
FR=${1:?frames dir} OUT=${2:?out tsv}
cd "$FR" || exit 65
files=$(ls frame_*.png 2>/dev/null | sort)
[ -n "$files" ] || { echo "no frames in $FR" >&2; exit 65; }
echo -e "index\tframe\tAE" > "$OUT"
i=0; prev=""
for f in $files; do
  if [ -n "$prev" ]; then
    ae=$(compare -metric AE "$prev" "$f" null: 2>&1 | tail -1)
    [ -z "$ae" ] && ae=-1
    echo -e "$i\t$f\t$ae" >> "$OUT"
  fi
  prev=$f; i=$((i+1))
done
echo "frames=$i"
