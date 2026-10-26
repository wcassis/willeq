#!/bin/bash
# B1153 (#473): the six override spells on --camera b1118 with the
# spellOverrides block STRIPPED from config/spell_effects.json (the no-override
# half of step 1). Same plans as tour7.sh. RUN ON THE THINKPAD (config must be
# stripped + model_viewer rebuilt on the shared worktree first).
set -u
R=/home/user/src/willeq-wt/kb-b1153-retire/refactor/B1153
F=/tmp/b1153-frames
mkdir -p "$F"
while read spell target cast; do
  printf 't_begin_s\tspell_id\ttarget\tdistance_units\tcast_ms\tcaster_level\n1.5\t%s\t%s\t0\t%s\t60\n' "$spell" "$target" "$cast" > "$F/noov_tour_$spell.plan"
  FX_SPELLS=auto NO_X264=1 PLAN="$F/noov_tour_$spell.plan" SECONDS_=10 \
    $R/scripts/run_scene.sh b1153_noov_b1118_$spell 30 --camera b1118 < /dev/null
done <<'EOF2'
38 other 2500
41 other 4000
54 self 2500
253 self 3000
458 self 3000
656 self 3000
EOF2
echo "noov b1118 tour done"
