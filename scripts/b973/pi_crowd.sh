#!/usr/bin/env bash
# B973 (GitLab #291): the Orange Pi post-load crowd build runs --
# scripts/pi_rendered_zone_sweep.sh (the rendered client on the Pi, the local
# eqemu lane, its own memory cgroup, the shipped LLVM-less Mesa) once per
# variant, with scripts/b973/make_tour.py's tour (the crowd is spawned AFTER
# zone-in, no zone hop). Variants: new<N> (the B973 scheduler) and legacy<N>
# (WILLEQ_ENTITY_BUILD_LEGACY=1). Both run with WILLEQ_ENTITY_BUILD_TRACE=1.
# Each sweep call takes and releases both locks itself (eqemu first, Pi
# second) and restores the character with the world down.
#
# Usage: scripts/b973/pi_crowd.sh --pi-dir /mnt/projects/fable/ci-arm/<sha>
#          [--zone qeynos2] [--variants new200,legacy200] [--out DIR]
#          [-r "1280 720"] [--build 120] [--env K=V]...
set -u
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
ZONE="qeynos2"
VARIANTS="new200,legacy200"
PI_DIR=""
OUT=""
RES="1280 720"
BUILD_S=120
EXTRA=()
SWEEP_EXTRA=()
while [ $# -gt 0 ]; do
    case "$1" in
        --zone) ZONE="$2"; shift 2 ;;
        --variants) VARIANTS="$2"; shift 2 ;;
        --pi-dir) PI_DIR="$2"; shift 2 ;;
        --out) OUT="$2"; shift 2 ;;
        -r) RES="$2"; shift 2 ;;
        --build) BUILD_S="$2"; shift 2 ;;
        --env) EXTRA+=( --env "$2" ); shift 2 ;;
        # hermes-ext lane (no local eqemu stack): forward to the sweep rig,
        # which then requires EQEMU_HOST to name the VM.
        --allow-vm-lane) SWEEP_EXTRA+=( --allow-vm-lane ); shift ;;
        *) echo "unknown option $1" >&2; exit 3 ;;
    esac
done
# B987's QA finding F1 (same class): --build feeds the arithmetic
# cap=$((BUILD_S + 420)) below; a raw token (0_1, 0720-as-octal, ...) would
# print "integer expression expected" and hand the sweep a broken cap.
# Validate BEFORE any resource (plain ASCII decimal, 0..86400 -- the one
# definition in scripts/b964/observation_controls.py).
RE_DURATION='^(0|[1-9][0-9]*)$'
if ! LC_ALL=C grep -qE "$RE_DURATION" <<< "$BUILD_S" || [ "$BUILD_S" -gt 86400 ]; then
    echo "bad duration '$BUILD_S' (plain ASCII decimal 0..86400, no underscores/signs/leading zeros)" >&2
    exit 3
fi
[ -n "$PI_DIR" ] || { echo "--pi-dir is required" >&2; exit 3; }
[ -n "$OUT" ] || OUT="/mnt/projects/fable/review/B973/pi/crowd-$(date -u +%Y%m%dT%H%M%SZ)"
mkdir -p "$OUT"
# Safe points (server frame, the zone table on the local lane; B964 read them
# 2026-10-01): the sweep boots the stack only inside its own lock.
case "$ZONE" in
    qeynos2) SAFE="-74 428 3" ;;
    gfaydark) SAFE="10 -20 0" ;;
    *) echo "no safe point for $ZONE" >&2; exit 3 ;;
esac
rc_all=0
for variant in ${VARIANTS//,/ }; do
    mode="$(sed -E 's/^([a-z]+)[0-9].*$/\1/' <<< "$variant")"
    n="$(sed -E 's/^[a-z]+([0-9]+)$/\1/' <<< "$variant")"
    dir="$OUT/$ZONE/$variant"
    mkdir -p "$dir"
    # shellcheck disable=SC2086
    python3 "$REPO/scripts/b973/make_tour.py" --safe $SAFE --count "$n" --build "$BUILD_S" --out "$dir/tour.txt"
    envs=( --env WILLEQ_ENTITY_BUILD_TRACE=1 "${EXTRA[@]}" )
    case "$mode" in
        new) ;;
        legacy) envs+=( --env WILLEQ_ENTITY_BUILD_LEGACY=1 ) ;;
        *) echo "bad variant $variant" >&2; rc_all=1; continue ;;
    esac
    cap=$(( BUILD_S + 420 ))
    echo "[b973 pi_crowd] $ZONE/$variant -> $dir"
    bash "$REPO/scripts/pi_rendered_zone_sweep.sh" --zones "$ZONE" --out "$dir" --pi-dir "$PI_DIR" \
        -r "$RES" --tour "$dir/tour.txt" --cap "$cap" --stall 240 --no-retry --owner b973 \
        --stop-point never "${SWEEP_EXTRA[@]}" "${envs[@]}" > "$dir/sweep.out" 2>&1
    rc=$?
    echo "[b973 pi_crowd] $ZONE/$variant rc=$rc"
    [ "$rc" -eq 0 ] || rc_all=1
    [ "$rc" -eq 64 ] && { echo "[b973 pi_crowd] a lock is held -- stopping" >&2; exit 64; }
done
exit "$rc_all"
