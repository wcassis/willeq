#!/usr/bin/env bash
# B1194 (#513): prove a gitlab-runner VM can run rbuild's non-GL steps, with
# timings, like-for-like against the ThinkPad run (refactor/B1194/README.md
# "The proof"). Runs from the b1194-proof-* CI jobs on the runner VM itself --
# the only way to execute commands there from the board (no inbound ssh).
#
# The commands are rbuild's own (scripts/hermes/rbuild), with the source being
# this job's checkout instead of the NFS worktree symlink:
#   configure : CI's Release/GLES2 recipe (rbuild `configure`), -j6 jobs
#   target    : cmake --build --target test_packet  (rbuild `--target test_packet`)
#   ctest     : ctest -R 'PacketTest'              (rbuild `test -R 'PacketTest'`)
#
# The tree is build/b1194 (kept by GIT_CLEAN_FLAGS' `-e build` for re-polls of
# the same ref). Output is teed to /mnt/projects/fable/review/B1194/<host>/
# build_proof-<UTC>.txt so the timings outlive the job log.
set -uo pipefail
cd "${CI_PROJECT_DIR:?CI_PROJECT_DIR}"

HOST=$(hostname)
OUT=/mnt/projects/fable/review/B1194/"$HOST"
mkdir -p "$OUT" 2>/dev/null || OUT="$(mktemp -d)"
TS=$(date -u +%Y%m%dT%H%M%SZ)
T=build/b1194
JOBS=6   # rbuild's RBUILD_JOBS default: like-for-like
# .goldfarm's before_script sets CCACHE_CMAKE_ARGS in its own shell but does not
# export it, so a `bash script` subshell re-derives it the same way.
if command -v ccache >/dev/null 2>&1; then
    export CCACHE_CMAKE_ARGS="-DCMAKE_C_COMPILER_LAUNCHER=ccache -DCMAKE_CXX_COMPILER_LAUNCHER=ccache"
else
    export CCACHE_CMAKE_ARGS="-UCMAKE_C_COMPILER_LAUNCHER -UCMAKE_CXX_COMPILER_LAUNCHER"
fi

{
    echo "# B1194 build proof: $HOST at $TS (sha $(git rev-parse --short HEAD))"
    echo "nproc=$(nproc)  free=$(free -g | sed -n 2p)  load=$(cut -d' ' -f1-3 /proc/loadavg)"

    t0=$(date +%s)

    # 1. The patched GLES2 Irrlicht (CI's own recipe from .build_tree; warm for
    #    this ref via GIT_CLEAN_FLAGS -e third_party/irrlicht-gles2-x86).
    if [ ! -f third_party/irrlicht-gles2-x86/lib/libIrrlicht.a ] \
       || ! bash scripts/ci/irrlicht_input_stamp.sh verify third_party/irrlicht-gles2-x86/irrlicht_input_stamp.txt; then
        echo "== Irrlicht bootstrap (build-gles2-x86.sh --irrlicht-only, JOBS=$JOBS) on $HOST"
        s=$(date +%s); JOBS=$JOBS nice -n 10 bash scripts/build-gles2-x86.sh --irrlicht-only
        rc=$?
        echo "IRR_LICHT_BOOTSTRAP_S=$(( $(date +%s) - s )) (exit $rc)"
        [ $rc = 0 ] || exit 1
    else
        echo "== Irrlicht bootstrap: warm (stamp verified)"
    fi

    # 2. configure -- rbuild's exact line (scripts/hermes/rbuild rb_configure).
    s=$(date +%s)
    cmake -S . -B "$T" -G Ninja -DCMAKE_BUILD_TYPE=Release -DEQT_BUILD_TESTS=ON \
        -DEQT_GRAPHICS=ON -DEQT_GLES2=ON -DEQT_DRM=OFF \
        -DEQT_GLES2_IRRLICHT_DIR="$PWD/third_party/irrlicht-gles2-x86" \
        $CCACHE_CMAKE_ARGS 2>&1 | tail -5
    rc=$?
    echo "CONFIGURE_S=$(( $(date +%s) - s )) (exit $rc)"
    [ $rc = 0 ] || exit 1

    # 3. --target test_packet -- rbuild's exact line (rb "build" action).
    s=$(date +%s)
    cmake --build "$T" --target test_packet -j "$JOBS" 2>&1 | tail -5
    rc=$?
    echo "BUILD_TEST_PACKET_S=$(( $(date +%s) - s )) (exit $rc)"
    [ $rc = 0 ] || exit 1

    # 4. ctest -R 'PacketTest' -- rbuild's exact line (rb "test" action).
    s=$(date +%s)
    ctest --test-dir "$T" -R 'PacketTest' --output-on-failure -j "$JOBS"
    rc=$?
    echo "CTEST_TEST_PACKET_S=$(( $(date +%s) - s )) (exit $rc)"
    ctest --test-dir "$T" -R 'PacketTest' -V 2>/dev/null | grep -E '^[0-9]+/[0-9]+ Test:' | sed 's/^/  /' || true

    if command -v ccache >/dev/null 2>&1; then
        echo "== ccache on $HOST after the build"
        ccache -s 2>/dev/null | head -8
    fi
    echo "TOTAL_S=$(( $(date +%s) - t0 ))"
} 2>&1 | tee "$OUT/build_proof-$TS.txt"

# Exit 1 when a step failed so the failure is VISIBLE (the jobs carry
# allow_failure: true, so a bad proof never sinks the pipeline).
grep -q "BUILD_TEST_PACKET_S=.* (exit 0)" "$OUT/build_proof-$TS.txt" \
    && grep -q "CTEST_TEST_PACKET_S=.* (exit 0)" "$OUT/build_proof-$TS.txt"
