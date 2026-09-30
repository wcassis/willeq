#!/usr/bin/env bash
# B932: the Orange Pi A/B of stock Mesa vs the LLVM-less Mesa, rendered client.
#
# --a is ONE published ARM build dir (scripts/pi/publish_arm_build.sh's
# layout). Since B935 it carries both sides: A = bin/willeq (the system Mesa,
# the sweep's WILLEQ_MESA=system opt-out) and B = bin/willeq-nollvm (the SAME
# binary with DT_RPATH=<prefix>/lib, the sweep's default), and the sweep
# deploys the pinned prefix to the Pi itself (scripts/pi/deploy_mesa_prefix.sh).
#
# Each leg is ONE ordinary scripts/pi_rendered_zone_sweep.sh invocation (its
# own locks, lane up, snapshot, restore, lane down), legs alternate A B / B A
# so drift over the session does not land on one side. Every leg:
#   * WILLEQ_BASELINE=1, --drm --renderer gles2 --constrained orangepi (the
#     sweep's own client line);
#   * idle at the zone's safe point (the sweep's default /loc-wait-/q tour),
#     SIGTERM 5 s after steady_60s, so the dumped frames of A and B show the
#     same place from the same pose;
#   * --snap-points startup,ready,steady_60s: /proc/<pid>/smaps + maps at each
#     point, which is how the loaded driver is PROVEN (not assumed); the
#     sweep fails a leg whose maps show the other Mesa (exit 67);
#   * level 60 (growthplane kicks an under-level character; the snapshot
#     restore puts the row back).
# The setcap'd copy on the Pi keeps its DT_RPATH (absolute, no $ORIGIN), which
# secure-execution mode honours; LD_LIBRARY_PATH and $ORIGIN are ignored there.
#
# Usage: scripts/pi/mesa_ab.sh --a DIR [--zones z1,z2] [--rounds 2]
#            [--out DIR] [--owner NAME]
# Then:  python3 scripts/pi/mesa_ab_table.py --ab-dir <out>
# Exit: 0 every leg exited 0, 1 otherwise (legs keep going either way).
set -u
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
A=""; ZONES="growthplane,timorous,iceclad,gfaydark,qeynos2"; ROUNDS=2; OUT=""; OWNER="b932-mesa"
while [ $# -gt 0 ]; do
    case "$1" in
        --a) A="$2"; shift 2 ;;
        --zones) ZONES="$2"; shift 2 ;;
        --rounds) ROUNDS="$2"; shift 2 ;;
        --out) OUT="$2"; shift 2 ;;
        --owner) OWNER="$2"; shift 2 ;;
        *) echo "unknown option: $1" >&2; exit 64 ;;
    esac
done
A="$(readlink -f "$A")"
[ -x "$A/bin/willeq" ] && [ -x "$A/bin/willeq-nollvm" ] \
    || { echo "need --a <build dir with bin/willeq and bin/willeq-nollvm (a B935+ publish)>" >&2; exit 64; }
[ -n "$OUT" ] || OUT="/mnt/projects/fable/review/B932/ab-$(date -u +%Y%m%dT%H%M%SZ)"
mkdir -p "$OUT"
TOUR="$OUT/tour.txt"
printf '/loc\nwait 240\n/loc\n/q\n' > "$TOUR"
{
    echo "A=$A/bin/willeq sha256=$(sha256sum "$A/bin/willeq" | cut -d' ' -f1) $(grep '^sha=' "$A/BUILD_INFO") mesa=system"
    echo "B=$A/bin/willeq-nollvm sha256=$(sha256sum "$A/bin/willeq-nollvm" | cut -d' ' -f1) $(grep '^willeq_nollvm_rpath=' "$A/BUILD_INFO")"
    echo "zones=$ZONES rounds=$ROUNDS lane=${EQEMU_HOST:-local} start_utc=$(date -u +%FT%TZ)"
} > "$OUT/ab_meta.txt"
cat "$OUT/ab_meta.txt"

FAILS=0
for r in $(seq 1 "$ROUNDS"); do
    if [ $((r % 2)) -eq 1 ]; then ORDER="A B"; else ORDER="B A"; fi
    for side in $ORDER; do
        mesa=system; [ "$side" = B ] && mesa=nollvm
        leg="$OUT/r${r}_${side}"
        echo "[mesa_ab $(date -u +%T)] round $r side $side (mesa=$mesa) -> $leg"
        bash "$REPO/scripts/pi_rendered_zone_sweep.sh" --zones "$ZONES" --out "$leg" --pi-dir "$A" --mesa "$mesa" \
            --owner "$OWNER" --no-retry --tour "$TOUR" --cap 300 --stall 120 \
            --env WILLEQ_BASELINE=1 --snap-points startup,ready,steady_60s \
            --char-level 60 --char-exp 620000000 \
            > "$leg.out" 2>&1
        rc=$?
        echo "[mesa_ab $(date -u +%T)] round $r side $side exit $rc; $(tail -1 "$leg.out")"
        [ "$rc" -eq 0 ] || FAILS=$((FAILS + 1))
    done
done
echo "end_utc=$(date -u +%FT%TZ) failed_legs=$FAILS" >> "$OUT/ab_meta.txt"
[ "$FAILS" -eq 0 ]
