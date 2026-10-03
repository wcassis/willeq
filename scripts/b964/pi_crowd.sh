#!/usr/bin/env bash
# B964 (GitLab #282): the Orange Pi crowd runs -- scripts/pi_rendered_zone_sweep.sh
# (the rendered client on the Pi, the local eqemu lane, its own memory cgroup,
# the shipped LLVM-less Mesa) once per (zone, variant), with the crowd tour of
# scripts/b964/make_tour.py. Variants as scripts/b964/crowd_run.sh:
# <imp|off|base|tagnode><N>[near|far] (B975: `tagnode` = impostors on, name
# tags through the pre-B975 per-node path, WILLEQ_NAMETAG_BATCH=0). Each sweep call takes and releases both locks
# itself (eqemu first, Pi second) and restores the character with the world down.
#
# Safe points (server frame, the zone table's safe_x/safe_y/safe_z on the
# local lane, read 2026-10-01): kept here because the sweep boots the stack
# only inside its own lock.
#
# Usage: scripts/b964/pi_crowd.sh --pi-dir /mnt/projects/fable/ci-arm/<sha>
#          [--zones qeynos2,gfaydark,growthplane] [--variants imp50far,imp200far,off200far]
#          [--out DIR] [-r "1280 720"] [--hop-zone arena] [--frametiming]   (see make_tour.py)
#          [--owner B964]   (the lock owner tag)
#          [--allow-vm-lane]   (B975: forward to pi_rendered_zone_sweep.sh, whose
#          EQEMU_HOST must then name the eqemu VM)
set -u
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
ZONES="qeynos2,gfaydark,growthplane"
VARIANTS="imp50far,imp200far,off200far"
PI_DIR=""
OUT=""
RES="1280 720"
HOP_ZONE="arena"
OWNER="B964"
ALLOW_VM=0
TOUR_EXTRA=()
while [ $# -gt 0 ]; do
    case "$1" in
        --zones) ZONES="$2"; shift 2 ;;
        --variants) VARIANTS="$2"; shift 2 ;;
        --pi-dir) PI_DIR="$2"; shift 2 ;;
        --out) OUT="$2"; shift 2 ;;
        -r) RES="$2"; shift 2 ;;
        --hop-zone) HOP_ZONE="$2"; shift 2 ;;
        --frametiming) TOUR_EXTRA+=( --frametiming ); shift ;;
        --owner) OWNER="$2"; shift 2 ;;
        --allow-vm-lane) ALLOW_VM=1; shift ;;
        *) echo "unknown option $1" >&2; exit 3 ;;
    esac
done
[ -n "$PI_DIR" ] || { echo "--pi-dir is required" >&2; exit 3; }
[ -n "$OUT" ] || OUT="/mnt/projects/fable/review/B964/pi/crowd-$(date -u +%Y%m%dT%H%M%SZ)"
mkdir -p "$OUT"
safe_of() {
    case "$1" in
        qeynos2) echo "-74 428 3" ;;
        gfaydark) echo "10 -20 0" ;;
        growthplane) echo "3016 -2522 -19" ;;
        *) echo "" ;;
    esac
}
rc_all=0
for zone in ${ZONES//,/ }; do
    safe="$(safe_of "$zone")"
    [ -n "$safe" ] || { echo "no safe point for $zone" >&2; rc_all=1; continue; }
    for variant in ${VARIANTS//,/ }; do
        mode="$(sed -E 's/^([a-z]+)[0-9].*$/\1/' <<< "$variant")"
        n="$(sed -E 's/^[a-z]+([0-9]+).*$/\1/' <<< "$variant")"
        layout="$(sed -E 's/^[a-z]+[0-9]+//' <<< "$variant")"; [ -n "$layout" ] || layout=none
        dir="$OUT/$zone/$variant"
        mkdir -p "$dir"
        # shellcheck disable=SC2086
        python3 "$REPO/scripts/b964/make_tour.py" --safe $safe --count "$n" --layout "$layout" \
            --zone "$zone" --hop-zone "$HOP_ZONE" "${TOUR_EXTRA[@]}" --out "$dir/tour.txt"
        envs=()
        case "$mode" in
            imp) ;;
            off) envs+=( --env WILLEQ_IMPOSTORS=0 ) ;;
            base) envs+=( --env WILLEQ_BASELINE=1 ) ;;
            tagnode) envs+=( --env WILLEQ_NAMETAG_BATCH=0 ) ;;
        esac
        vm_args=()
        [ "$ALLOW_VM" -eq 1 ] && vm_args+=( --allow-vm-lane )
        echo "[pi_crowd] $zone/$variant -> $dir"
        bash "$REPO/scripts/pi_rendered_zone_sweep.sh" --zones "$zone" --out "$dir" --pi-dir "$PI_DIR" \
            -r "$RES" --tour "$dir/tour.txt" --cap 720 --stall 240 --no-retry --owner "$OWNER" \
            --stop-point never --char-level 60 --char-exp 616137000 "${envs[@]}" "${vm_args[@]}" \
            > "$dir/sweep.out" 2>&1
        rc=$?
        echo "[pi_crowd] $zone/$variant rc=$rc"
        [ "$rc" -eq 0 ] || rc_all=1
        [ "$rc" -eq 64 ] && { echo "[pi_crowd] a lock is held -- stopping" >&2; exit 64; }
    done
done
exit "$rc_all"
