#!/bin/bash
# B1153 (#473): tour-7 on --camera b1118 (the B1142 "new" rig: ToTD hidden, FX
# field unoccluded; the 93 reference is its 3.049, NOT the old rig's 2.938).
# Copy of refactor/B1151/scripts/tour7.sh, paths retargeted. RUN ON THE THINKPAD.
set -u
R=/home/user/src/willeq-wt/kb-b1153-retire/refactor/B1153
F=/tmp/b1153-frames
mkdir -p "$F"
while read spell target cast; do
  printf 't_begin_s\tspell_id\ttarget\tdistance_units\tcast_ms\tcaster_level\n1.5\t%s\t%s\t0\t%s\t60\n' "$spell" "$target" "$cast" > "$F/tour_$spell.plan"
  FX_SPELLS=auto NO_X264=1 PLAN="$F/tour_$spell.plan" SECONDS_=10 \
    $R/scripts/run_scene.sh ${TAG:-b1153}_tour_$spell 30 --camera b1118 < /dev/null
done <<'EOF2'
38 other 2500
41 other 4000
54 self 2500
93 other 2500
253 self 3000
458 self 3000
656 self 3000
EOF2
