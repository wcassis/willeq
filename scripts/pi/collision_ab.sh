#!/usr/bin/env bash
# B909: the Orange Pi A/B for the held-once collision triangles.
#
# A and B are two published ARM build dirs (bin/willeq + config/ + data/, as
# scripts/pi/publish_arm_build.sh lays them out). Each leg is ONE ordinary
# scripts/pi_rendered_zone_sweep.sh invocation (its own locks, lane up,
# snapshot, restore, lane down), so every leg is a complete, independent
# rendered run of every zone. Legs alternate A B / B A / A B ... so drift over
# the evening (Pi temperature, VM load) does not land on one side.
#
# Every leg runs identically:
#   * WILLEQ_BASELINE=1 (the 2026-09-21 audit rule), WILLEQ_LEDGER_OWNERS=spatial
#     (the sp.* owner rows);
#   * the tour enables the frame-timing breakdown (`/frametiming`, held until
#     connected: "Total Frame" and "Player Movement" in us every 60 frames);
#   * --drive-keys: scripts/pi/uinput_drive.py's fixed W/S/D/Space sequence
#     from 10 s after `ready`, so the renderer's BSP-filtered collision and
#     ground rays (the code B909 changed) actually run;
#   * level 60 (B854's eligibility-leg values; the snapshot restore puts the
#     row back), because growthplane kicks an under-level character (min 46).
#
# Usage: scripts/pi/collision_ab.sh --a DIR --b DIR [--zones z1,z2]
#            [--rounds 3] [--out DIR] [--owner NAME]
#   EQEMU_HOST selects the lane as everywhere else; the VM lane is passed
#   through with --allow-vm-lane.
# Exit: 0 every leg exited 0, 1 otherwise (legs keep going either way).

set -u
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
A=""; B=""; ZONES="timorous,iceclad,growthplane,gfaydark"; ROUNDS=3; OUT=""; OWNER="b909-pi-ab"
while [ $# -gt 0 ]; do
    case "$1" in
        --a) A="$2"; shift 2 ;;
        --b) B="$2"; shift 2 ;;
        --zones) ZONES="$2"; shift 2 ;;
        --rounds) ROUNDS="$2"; shift 2 ;;
        --out) OUT="$2"; shift 2 ;;
        --owner) OWNER="$2"; shift 2 ;;
        *) echo "unknown option: $1" >&2; exit 64 ;;
    esac
done
[ -x "$A/bin/willeq" ] && [ -x "$B/bin/willeq" ] || { echo "need --a and --b build dirs with bin/willeq" >&2; exit 64; }
[ -n "$OUT" ] || OUT="/mnt/projects/fable/review/B909/pi-ab-$(date -u +%Y%m%dT%H%M%SZ)"
mkdir -p "$OUT"
TOUR="$OUT/tour.txt"
printf '/frametiming\nwait 240\n/loc\n/q\n' > "$TOUR"
{
    echo "A=$(readlink -f "$A") sha256=$(sha256sum "$A/bin/willeq" | cut -d' ' -f1) $(grep '^sha=' "$A/BUILD_INFO" 2>/dev/null)"
    echo "B=$(readlink -f "$B") sha256=$(sha256sum "$B/bin/willeq" | cut -d' ' -f1) $(grep '^sha=' "$B/BUILD_INFO" 2>/dev/null)"
    echo "zones=$ZONES rounds=$ROUNDS lane=${EQEMU_HOST:-default} start_utc=$(date -u +%FT%TZ)"
} > "$OUT/ab_meta.txt"
cat "$OUT/ab_meta.txt"

VM_FLAG=()
[ "${EQEMU_HOST:-}" = "172.16.200.99" ] && VM_FLAG=(--allow-vm-lane)
FAILS=0
for r in $(seq 1 "$ROUNDS"); do
    if [ $((r % 2)) -eq 1 ]; then ORDER="A B"; else ORDER="B A"; fi
    for side in $ORDER; do
        dir="$A"; [ "$side" = B ] && dir="$B"
        leg="$OUT/r${r}_${side}"
        echo "[collision_ab $(date -u +%T)] round $r side $side -> $leg"
        bash "$REPO/scripts/pi_rendered_zone_sweep.sh" --zones "$ZONES" --out "$leg" --pi-dir "$dir" \
            --owner "$OWNER" --no-retry --tour "$TOUR" --drive-keys "${VM_FLAG[@]}" \
            --env WILLEQ_BASELINE=1 --env WILLEQ_LEDGER_OWNERS=spatial \
            --char-level 60 --char-exp 620000000 \
            > "$leg.out" 2>&1
        rc=$?
        echo "[collision_ab $(date -u +%T)] round $r side $side exit $rc; $(tail -1 "$leg.out")"
        [ "$rc" -eq 0 ] || FAILS=$((FAILS + 1))
    done
done
echo "end_utc=$(date -u +%FT%TZ) failed_legs=$FAILS" >> "$OUT/ab_meta.txt"
[ "$FAILS" -eq 0 ]
