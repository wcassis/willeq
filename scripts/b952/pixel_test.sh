#!/usr/bin/env bash
# B952 (GitLab #270): the impostor pixel evidence -- full model vs impostor at
# 64/128/256 for every playable race x gender at 40/80/160 units.
#
# usage: pixel_test.sh <benchmark> <out-dir> <width> <height> [extra benchmark args...]
#
# Writes <out-dir>/impostor_pixel_<W>x<H>.csv, the 4x crop sheets
# (sheet_<W>x<H>_d<dist>_r<race>g<gender>.png: full model | 64 | 128 | 256)
# and <out-dir>/pixel_<W>x<H>.log. The background is the clear colour only (no
# zone is drawn), so every pixel that differs from it is the model's.
# B952_FULL_NOFOG=1 / B952_BB_FOG=1 are the decomposition diagnostics (see
# the benchmark's --impostor-pixel-test).
set -u
BIN="$1"; OUT="$2"; W="$3"; H="$4"; shift 4
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$HERE/../.." && pwd)"
EQ="${PI_EQ_PATH:-/mnt/projects/EverQuestP1999}"
EQE1="${PI_EQE1_PATH:-/mnt/projects/EverQuest_ETC1}"
DRM=()
[ "${B952_DRM:-0}" = 1 ] && DRM=(--drm)
mkdir -p "$OUT"
"$BIN" --zone qeynos2 --eq-path "$EQ" --eqe1-path "$EQE1" "${DRM[@]}" --res "$W" "$H" \
    --frames 20 --warmup 2 \
    --path-file "$REPO/benchmarks/paths/qeynos2_impostor_fixedpose_300f.path" \
    --adaptive-lighting --zone-pp-budget 20 --pp-budget-mode count --player-light-level 9 \
    --entities 28 --test-races 1,2,3,4,5,6,7,8,9,10,11,12,128,130 --entity-genders both --animate \
    --impostor-pixel-test "$OUT" "$@" > "$OUT/pixel_${W}x${H}.log" 2>&1
rc=$?
echo "pixel_test ${W}x${H} exit=$rc"
exit $rc
