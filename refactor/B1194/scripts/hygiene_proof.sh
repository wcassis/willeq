#!/usr/bin/env bash
# B1194 (#513): prove a gitlab-runner VM runs the hygiene net identically to
# the ThinkPad's `rbuild <wt> hygiene`, with timings. Runs from the
# b1194-hyg-* CI jobs on the runner VM.
#
# The two commands are rbuild's own hygiene line (scripts/hermes/rbuild, the
# "hygiene" case): an EQT_GRAPHICS=OFF tree + ctest -L hygiene, here into
# build/b1194-hygiene (kept by GIT_CLEAN_FLAGS' `-e build`) instead of rbuild's
# $broot/<slug>/hygiene. -j6 = rbuild's RBUILD_JOBS default (like-for-like).
#
# Output is teed to /mnt/projects/fable/review/B1194/<host>/hygiene_proof-<UTC>.txt
# (the full ctest result list -- the comparison set for the README).
set -uo pipefail
cd "${CI_PROJECT_DIR:?CI_PROJECT_DIR}"

HOST=$(hostname)
OUT=/mnt/projects/fable/review/B1194/"$HOST"
mkdir -p "$OUT" 2>/dev/null || OUT="$(mktemp -d)"
TS=$(date -u +%Y%m%dT%H%M%SZ)
H=build/b1194-hygiene
JOBS=6
# .goldfarm's before_script sets CCACHE_CMAKE_ARGS in its own shell but does not
# export it, so a `bash script` subshell re-derives it the same way.
if command -v ccache >/dev/null 2>&1; then
    export CCACHE_CMAKE_ARGS="-DCMAKE_C_COMPILER_LAUNCHER=ccache -DCMAKE_CXX_COMPILER_LAUNCHER=ccache"
else
    export CCACHE_CMAKE_ARGS="-UCMAKE_C_COMPILER_LAUNCHER -UCMAKE_CXX_COMPILER_LAUNCHER"
fi

{
    echo "# B1194 hygiene proof: $HOST at $TS (sha $(git rev-parse --short HEAD))"
    echo "nproc=$(nproc)  load=$(cut -d' ' -f1-3 /proc/loadavg)"
    t0=$(date +%s)
    s=$(date +%s)
    cmake -S . -B "$H" -DCMAKE_BUILD_TYPE=Release -DEQT_GRAPHICS=OFF -DEQT_BUILD_TESTS=ON \
        $CCACHE_CMAKE_ARGS >/dev/null
    echo "HYGIENE_CONFIGURE_S=$(( $(date +%s) - s )) (exit $?)"
    s=$(date +%s)
    ctest --test-dir "$H" -L hygiene -j "$JOBS" --output-on-failure
    rc=$?
    echo "HYGIENE_CTEST_S=$(( $(date +%s) - s )) (exit $rc)"
    echo "TOTAL_S=$(( $(date +%s) - t0 ))"
    [ $rc = 0 ]
} 2>&1 | tee "$OUT/hygiene_proof-$TS.txt"
