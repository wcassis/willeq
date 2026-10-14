#!/bin/bash
# B924: the repeatable net (acceptance 3), one leg. Runs the hold rig then the
# bound assert. The hold rig takes/releases the lane lock itself.
#
# One leg per invocation (so both can run in parallel on separate lanes):
#   leg "uncapped"  : the default render rate; asserts growth <= the driver
#                     pseudo-VRAM ceiling (the bounded scene queue's capacity).
#                     GREEN on today's tree: the growth is driver-side and
#                     plateaus at ~2.24-2.31 GiB (refactor/B924/README.md).
#                     RED when an UNBOUNDED accumulation appears (a client leak
#                     that keeps climbing past the ceiling point-over-point).
#   leg "capped"    : WILLEQ_FPS_CAP=10; the driver's scene queue DRAINS, so
#                     gpu_driver stays ~flat (measured 19.2 MiB / -2.5 MiB,
#                     run2/run5). A TIGHT bound (default 64 MiB) turns this leg
#                     into a genuine client-leak tripwire: a leak that grows
#                     with frames will trip it.
#
# Usage: b924_net_leg.sh <uncapped|capped> <OUT_DIR>
set -eu
LEG="$1"
OUT="$2"
HOLD_S="${3:-420}"
REPO="/home/user/src/willeq-wt/kb-b924-llvmpipe-growth"
ZONE="${B924_ZONE:-gfaydark}"

case "$LEG" in
  uncapped)
    B924_BOUND_BYTES="${B924_BOUND_BYTES:-2883497060}"
    FPS_OPT=""
    ;;
  capped)
    B924_BOUND_BYTES="${B924_BOUND_BYTES:-67108864}"   # 64 MiB
    FPS_OPT="--fps-cap 10"
    ;;
  *) echo "unknown leg: $LEG" >&2; exit 64 ;;
esac

cd "$REPO"
EQEMU_LANE="${EQEMU_LANE:-1}" \
  B924_OUT="$OUT" \
  bash scripts/b924/b924_hold.sh --zone "$ZONE" --hold "$HOLD_S" \
      --out "$OUT" $FPS_OPT
RC=$?
[ "$RC" -eq 0 ] || { echo "hold rig rc=$RC" >&2; exit "$RC"; }

python3 scripts/b924/b924_check_bound.py "$OUT/$ZONE" "$B924_BOUND_BYTES"
RC=$?
if [ "$RC" -eq 0 ]; then
  echo "B924 net ($LEG): PASS (see $OUT/$ZONE/bound_check.txt)"
else
  echo "B924 net ($LEG): FAIL (see $OUT/$ZONE/bound_check.txt)" >&2
fi
exit "$RC"
