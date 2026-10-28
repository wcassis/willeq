#!/usr/bin/env bash
# B1178: re-extract the fixed_npc measure frame PNGs from the capture videos (they were
# deleted after the original B1130 measurement; fx_regen.py is the documented recipe).
# One output per capture under <out>/<cap>_fixed_npc/. Idempotent: skips if frames.tsv +
# a sample PNG already exist.
set -u
EV=/home/user/kb-evidence/kb-b1178-oracle-refs/b1130
S1122=/mnt/projects/fable/review/B1122/scripts
OUT=${1:-/home/user/kb-evidence/kb-b1178-oracle-refs/b1130_rex}
mkdir -p "$OUT"
extract() {
  cap="$1"; md="$EV/measure/${cap}_fixed_npc"; v="$EV/videos/${cap}/video.mp4"
  od="$OUT/${cap}_fixed_npc"
  if [ -s "$od/frames.tsv" ] && ls "$od"/f*.png >/dev/null 2>&1; then echo "skip $cap"; return; fi
  [ -s "$v" ] || { echo "NO VIDEO $cap"; return; }
  off=$(python3 -c "import json;print(json.load(open('$md/meta.json'))['offset'])")
  tbc=$(python3 -c "import json;print(json.load(open('$md/meta.json'))['t_begincast'])")
  tact=$(python3 -c "import json;print(json.load(open('$md/meta.json'))['t_action'])")
  tend=$(python3 -c "import json;print(json.load(open('$md/meta.json'))['t_end'])")
  crop=$(python3 -c "import json;c=json.load(open('$md/meta.json'))['crop'];print('%d,%d,%d,%d'%tuple(c))")
  lab=$(python3 -c "import json;print(json.load(open('$md/meta.json'))['label'])")
  python3 "$S1122/fx_measure.py" "$v" "$off" "$tbc" "$tact" "$tend" "$crop" "$od" --label "$lab" >/dev/null 2>&1 \
    && python3 "$S1122/fx_timing.py" "$od" 209 >/dev/null 2>&1
  echo "done $cap ($?)"
}
export -f extract; export EV S1122 OUT
for cap in e3 e5 e13 e15 e16 e18 e21 e22 e23 e24 e26 e27 e28 e29 e30 e31 e32 e33 e34 e35 e41 r40 r51 r131 r132 r134 r139 r142; do
  echo "$cap"
done | xargs -P4 -I{} bash -c 'extract "$@"' _ {}
echo "ALL DONE: $(ls -d $OUT/*_fixed_npc 2>/dev/null | wc -l) dirs"
