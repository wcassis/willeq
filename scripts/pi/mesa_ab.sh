#!/usr/bin/env bash
# B932: the Orange Pi A/B of stock Mesa vs the LLVM-less Mesa, rendered client.
#
# A is a published ARM build dir (bin/willeq + config/ + data/ + BUILD_INFO,
# as scripts/pi/publish_arm_build.sh lays them out). B is built here from A:
# the SAME binary with one change -- DT_RPATH=<prefix>/lib, written by
# scripts/pi/mesa_nollvm_select.sh -- and config/ data/ symlinked to A's.
# The LLVM-less prefix must already be installed on the Pi
# (scripts/pi/build_mesa_nollvm.sh, then rsync to --prefix).
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
#     point, which is how the loaded driver is PROVEN (not assumed);
#   * level 60 (growthplane kicks an under-level character; the snapshot
#     restore puts the row back).
# The setcap'd copy on the Pi keeps its DT_RPATH (absolute, no $ORIGIN), which
# secure-execution mode honours; LD_LIBRARY_PATH would be ignored there.
#
# Usage: scripts/pi/mesa_ab.sh --a DIR [--zones z1,z2] [--rounds 2]
#            [--out DIR] [--owner NAME] [--prefix /home/user/mesa-nollvm]
# Then:  python3 scripts/pi/mesa_ab_table.py --ab-dir <out>
# Exit: 0 every leg exited 0, 1 otherwise (legs keep going either way).
set -u
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
A=""; ZONES="growthplane,timorous,iceclad,gfaydark,qeynos2"; ROUNDS=2; OUT=""; OWNER="b932-mesa"
PREFIX="/home/user/mesa-nollvm"
while [ $# -gt 0 ]; do
    case "$1" in
        --a) A="$2"; shift 2 ;;
        --zones) ZONES="$2"; shift 2 ;;
        --rounds) ROUNDS="$2"; shift 2 ;;
        --out) OUT="$2"; shift 2 ;;
        --owner) OWNER="$2"; shift 2 ;;
        --prefix) PREFIX="$2"; shift 2 ;;
        *) echo "unknown option: $1" >&2; exit 64 ;;
    esac
done
A="$(readlink -f "$A")"
[ -x "$A/bin/willeq" ] || { echo "need --a <build dir with bin/willeq>" >&2; exit 64; }
[ -n "$OUT" ] || OUT="/mnt/projects/fable/review/B932/ab-$(date -u +%Y%m%dT%H%M%SZ)"
mkdir -p "$OUT"
B="$OUT/build-nollvm"
mkdir -p "$B/bin"
ln -sfn "$A/config" "$B/config"
ln -sfn "$A/data" "$B/data"
{ cat "$A/BUILD_INFO"; echo "b932_variant=DT_RPATH=$PREFIX/lib (scripts/pi/mesa_nollvm_select.sh)"; } > "$B/BUILD_INFO"
bash "$REPO/scripts/pi/mesa_nollvm_select.sh" "$A/bin/willeq" "$B/bin/willeq" "$PREFIX" > "$OUT/select.txt" 2>&1 \
    || { cat "$OUT/select.txt"; exit 68; }
TOUR="$OUT/tour.txt"
printf '/loc\nwait 240\n/loc\n/q\n' > "$TOUR"
{
    echo "A=$A sha256=$(sha256sum "$A/bin/willeq" | cut -d' ' -f1) $(grep '^sha=' "$A/BUILD_INFO")"
    echo "B=$B sha256=$(sha256sum "$B/bin/willeq" | cut -d' ' -f1) rpath=$PREFIX/lib"
    echo "zones=$ZONES rounds=$ROUNDS lane=${EQEMU_HOST:-local} start_utc=$(date -u +%FT%TZ)"
} > "$OUT/ab_meta.txt"
cat "$OUT/ab_meta.txt"

FAILS=0
for r in $(seq 1 "$ROUNDS"); do
    if [ $((r % 2)) -eq 1 ]; then ORDER="A B"; else ORDER="B A"; fi
    for side in $ORDER; do
        dir="$A"; [ "$side" = B ] && dir="$B"
        leg="$OUT/r${r}_${side}"
        echo "[mesa_ab $(date -u +%T)] round $r side $side -> $leg"
        bash "$REPO/scripts/pi_rendered_zone_sweep.sh" --zones "$ZONES" --out "$leg" --pi-dir "$dir" \
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
