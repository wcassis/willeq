#!/usr/bin/env bash
# B952 (GitLab #270): the impostor measurement matrix, one run per line of
# impostor_run.sh, sequential (frame times are only comparable run to run
# when nothing else competes for the CPU/GPU).
#
# usage: matrix.sh <benchmark> <out-dir> <full|pi>
#   full: zones qeynos2 gfaydark x N 1/50/200 x size 64/128/256 x unique/shared
#         x far/mixed, plus the pre-B952 --impostor-fbo per baseline and the
#         --impostor-refresh runs (x86: 78 runs).
#   pi:   the reduced set (10 runs) that, with pixel_test.sh at 800x600 and
#         1280x720, fits the Pi protocol's twelve invocations per session.
set -u
BIN="$1"; OUT="$2"; SET="$3"
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
RUN="$HERE/impostor_run.sh"
mkdir -p "$OUT"
run() {
    bash "$RUN" "$BIN" "$OUT" "$@"
    # A render into an incomplete FBO leaves the impostor blank: every number
    # after that describes nothing, so the matrix stops (the Pi's first
    # shared-FBO session was voided by exactly this, before the MSAA fix).
    if grep -q -E 'incomplete_fbo=[1-9]' "$OUT/$1.log" 2>/dev/null; then
        echo "ABORT: $1 rendered into an incomplete FBO" >&2
        exit 3
    fi
}

if [ "$SET" = full ]; then
    for zone in qeynos2 gfaydark; do
        for n in 1 50 200; do
            for size in 64 128 256; do
                for mode in unique shared; do
                    for scene in far mixed; do
                        run "${zone}_n${n}_s${size}_${mode}_${scene}" "$zone" "$n" "$size" "$mode" "$scene"
                    done
                done
            done
        done
        run "${zone}_n200_s256_unique_far_fboper" "$zone" 200 256 unique far --impostor-fbo per
        run "${zone}_n200_s128_unique_far_refresh30" "$zone" 200 128 unique far --impostor-refresh 30
        run "${zone}_n200_s128_shared_far_refresh30" "$zone" 200 128 shared far --impostor-refresh 30
    done
elif [ "$SET" = pi ]; then
    # Ten runs; with the two pixel_test.sh runs that is the twelve the Pi
    # protocol allows in a session. Heaviest shared-FBO run first: an OOM or an
    # incomplete FBO shows on run one.
    run qeynos2_n200_s256_unique_far qeynos2 200 256 unique far
    run qeynos2_n200_s256_unique_far_fboper qeynos2 200 256 unique far --impostor-fbo per
    run qeynos2_n200_s128_unique_far qeynos2 200 128 unique far
    run qeynos2_n200_s64_unique_far qeynos2 200 64 unique far
    run qeynos2_n200_s128_shared_far qeynos2 200 128 shared far
    run qeynos2_n1_s128_unique_far qeynos2 1 128 unique far
    run qeynos2_n200_s128_unique_far_refresh30 qeynos2 200 128 unique far --impostor-refresh 30
    run qeynos2_n200_s128_unique_mixed qeynos2 200 128 unique mixed
    run gfaydark_n200_s128_unique_far gfaydark 200 128 unique far
    run gfaydark_n200_s128_unique_mixed gfaydark 200 128 unique mixed
else
    echo "usage: matrix.sh <benchmark> <out-dir> <full|pi>" >&2
    exit 1
fi
