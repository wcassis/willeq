#!/usr/bin/env bash
# B1200 (#519) live leg (willeq/fable side): the same worn-item scene on willeq,
# to record that fable draws NOTHING today. Modeled on the B1176 two-client
# fable rig (refactor/B1176/leg/fable_leg.sh + fable_leg_remote.sh):
#   A = windowed willeq as a willeqwatch observer char, its screen captured by
#       willeq's own --dump-frames-dir (allowed on fable; never the official
#       client's capture path).
#   B = headless willeq as Testcaster (willeq_test, gm=1), the GM that runs the
#       scene commands from a --script tour via /say.
# Both run against the lane-5 eqemu stack. A stands where the B1134 observer
# stood, framed on B, and its frames show the empty worn-effect draw.
#
# EQUIP PREREQUISITE (same open sub-problem as the oracle leg): B is headless,
# so it cannot click to equip a `#summonitem`'d cursor item. The FIX leg gives
# B the worn item one of two ways before zone-in:
#   (a) seed it into B's equipment in the DB while B is logged out (eqemu's
#       equipment storage is the column/blob to be confirmed), or
#   (b) a willeq cursor-intent path (willeq has its own cursor_gesture system;
#       a scripted equip intent), or
#   (c) run B windowed with --egl-window and drive one double-click.
# Until that lands, B's tour below does `#summonitem` only (item on cursor, not
# equipped), so the leg still records the "no worn effect drawn" baseline.
#
# usage: worn_fable_leg.sh <out_dir> <build_dir> <a_char>
#   build_dir: /home/user/willeq-builds/<slug>/build (the rbuild tree)
set -u
WT=$(cd "$(dirname "$0")/../../.." && pwd)
OUT=$1 BUILD=$2 ACHAR=$3
ITEM=8364   # Talisman of Vah Kerrath (all-class, spellanim 73); alt 10877
W=1280 H=800
BIN=$BUILD/bin/willeq
[ -x "$BIN" ] || { echo "no binary: $BIN" >&2; exit 65; }

cd "$WT"
export EQEMU_LANE=5
source scripts/eqemu/env.sh
source scripts/eqemu/restore_lib.sh

# --- bring the stack up if it is down (a prior oracle leg leaves it down) ----
WE_UP=0
if ! eqemu_stack_ready; then
  bash "$WT/scripts/eqemu/up.sh" qeynos2 >>"$OUT/stack-up.log" 2>&1 || exit 65
  WE_UP=1
fi
eqemu_stack_ready || { echo "stack not ready" >&2; exit 65; }
trap '[[ ${WE_UP:-0} == 1 ]] && bash "$WT/scripts/eqemu/down.sh" >>"$OUT/stack-down.log" 2>&1 || :' EXIT

# --- seed A and B at the B1134 geometry in qeynos2 ---------------------------
ZID=$(eqemu_sql "SELECT zoneidnumber FROM zone WHERE short_name='qeynos2' LIMIT 1;")
ZID=${ZID//[[:space:]]/}
[ -n "$ZID" ] || { echo "no qeynos2 id" >&2; exit 65; }
# A (observer) where B1134's observer stood; B (Testcaster) where its caster stood.
eqemu_sql "UPDATE character_data SET zone_id=$ZID, x=-67.6, y=434.4, z=3.75, heading=160 WHERE name='$ACHAR';" \
  || { echo "A seed failed" >&2; exit 65; }
eqemu_sql "UPDATE character_data SET zone_id=$ZID, x=-74, y=428, z=3.75, heading=160 WHERE name='Testcaster';" \
  || { echo "B seed failed" >&2; exit 65; }

# --- ship the tour + remote driver to the ThinkPad and run the leg -----------
# (The remote half is the B1176 fable_leg_remote.sh pattern: cfgA.json carries
# the willeqwatch password read at run time and is never pulled or committed;
# cfgB.json is willeq_test; --dump-frames-dir on A only; tcpdump -i any for the
# wire, veth copies dropped.) B's tour:
#   /say #set time 11 0 ; /say #weather 0 ; /say #summonitem $ITEM ; wait; /q
mkdir -p "$OUT"
echo "item=$ITEM a_char=$ACHAR b_char=Testcaster zid=$ZID" > "$OUT/rig.txt"
echo "NOTE: remote run + frame pull is the B1176 fable_leg_remote.sh pattern;" \
     "the FIX leg wires it (see refactor/B1176/leg/fable_leg_remote.sh)." >&2
echo "stack left up by this script: $( [[ ${WE_UP:-0} == 1 ]] && echo yes || echo no )"
