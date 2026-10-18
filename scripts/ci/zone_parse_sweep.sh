#!/usr/bin/env bash
# B928 (#246): the zone_parse per-load-point sweep the refactor/B928 README
# quotes. Run the gate's own benchmark invocation (`build-gles2` tree) over the
# five gate zones, printing the `[memledger]` line at each lifecycle point —
# the zone_parse=live/peak field is the per-step table, and ledger_peak is the
# whole-process high-water the early region-geometry release moves.
#
# Usage:  scripts/ci/zone_parse_sweep.sh <benchmark-binary> <eq-client> <eqe1-pack> <out-dir>
# The binary is the build-gles2 tree's gles2_zone_benchmark (see CLAUDE.md
# "Build Commands"); on the Pi use the published -nollvm binary and add --drm.
# This is a MEASUREMENT tool, not a gate: it prints, it never fails.
set -euo pipefail
BIN="${1:?benchmark binary}"
EQ="${2:?eq client path}"
EQE1="${3:?eqe1 pack path}"
OUT="${4:?output dir}"
shift 4
EXTRA=("$@")   # e.g. --drm on the Pi
ZONES=(qeynos2 gfaydark befallen iceclad timorous)
mkdir -p "$OUT"
for z in "${ZONES[@]}"; do
    echo "[b928] $z"
    timeout 900 "$BIN" --zone "$z" \
        --eq-path "$EQ" --eqe1-path "$EQE1" \
        --frames 5 --warmup 1 --res 800 600 \
        --objects --entities 5 --test-races 1,2,3,4,5 \
        --occluder-budget 16 \
        --memory-json "$OUT/$z.mem.json" \
        --log-level=INFO "${EXTRA[@]:+${EXTRA[@]}}" > "$OUT/$z.log" 2>&1 \
        || echo "[b928] $z rc=$? (nonzero: the zone did not load; see $OUT/$z.log)"
    grep -E 'memledger\] point=' "$OUT/$z.log" | \
        awk '{p=""; for(i=1;i<=NF;i++){if($i~/^point=/){p=$i}; if($i~/^ledger_peak=/){lp=$i}; if($i~/^zone_parse=/){zp=$i}}; print p, lp, zp}'
done
