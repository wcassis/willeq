# tests/cmake/eqe1.cmake -- included from tests/CMakeLists.txt.
# EQE1 archive loader tests.
# Moved verbatim from tests/CMakeLists.txt by J20 (no target, source or
# link changes). Included in the tests/ directory scope, so every variable
# set in the top-level preamble is in effect here.

# Test EQE1 flat pack loader (standalone, no GTest — uses real .eqe1 files)
ADD_EXECUTABLE(test_eqe1_loader
    test_eqe1_loader.cpp
    ${CMAKE_SOURCE_DIR}/src/client/graphics/eqe1_loader.cpp
)
TARGET_INCLUDE_DIRECTORIES(test_eqe1_loader PRIVATE ${CMAKE_SOURCE_DIR}/include)
ADD_TEST(NAME test_eqe1_loader COMMAND test_eqe1_loader)

# B930 (#248): the STALE pack must fail loudly by pack name + version.
# Standalone like test_eqe1_loader — it drives the B930 diagnosis on the
# loader directly (no GL, no GTest). RED on origin/fable: EQE1Loader has no
# getFormatVersion()/holdsTruncatedName() and diagnoseStaleEQE1Pack() does
# not exist, so the target does not compile and this ctest fails.
ADD_EXECUTABLE(test_eqe1_stale_pack
    test_eqe1_stale_pack.cpp
    ${CMAKE_SOURCE_DIR}/src/client/graphics/eqe1_loader.cpp
)
TARGET_INCLUDE_DIRECTORIES(test_eqe1_stale_pack PRIVATE ${CMAKE_SOURCE_DIR}/include)
ADD_TEST(NAME test_eqe1_stale_pack COMMAND test_eqe1_stale_pack)

# B930 (#248) + B1017 (#338): tools/verify_eqe1 prints the version on every
# line and --require-v3 flags ONLY a v1/v2 pack that HOLDS A 31-CHAR NAME
# (the truncation candidate; a structurally-clean v1/v2 pack -- every name
# fits the field -- reports OK). Pure python + the built tool. The tool
# lives in the build tree UNDER TEST (${CMAKE_BINARY_DIR}/bin), which is
# what the script is pointed at through WILLEQ_BUILD_DIR: CI (B1012)
# configures per-ref trees at build/ref/<ref-slug>, so a hardcoded
# <repo>/build/bin default — the pre-B1012 layout — found nothing there and
# this leg silently SKIPped (exit 125) in every CI pipeline until the B930
# follow-up pinned the path here. The tool is part of the default `all`
# target, so the build stage has already produced it before ctest runs
# (there is no add_test build-dependency keyword; the pre-B1017
# `BUILD_DEPENDS` keyword here was argv, not a dependency). The test is
# registered only when the target exists (EQT_GRAPHICS=OFF trees, e.g.
# build/hygiene, have no tool); within that the script still SKIPs (125) if
# the binary is somehow missing.
IF(TARGET verify_eqe1)
    ADD_TEST(NAME test_eqe1_verify_stale_pack
        COMMAND python3 ${CMAKE_SOURCE_DIR}/scripts/ci/test_eqe1_verify_stale_pack.py
    )
    SET_TESTS_PROPERTIES(test_eqe1_verify_stale_pack PROPERTIES
        SKIP_RETURN_CODE 125
        ENVIRONMENT "WILLEQ_SOURCE_DIR=${CMAKE_SOURCE_DIR};WILLEQ_BUILD_DIR=${CMAKE_BINARY_DIR}")
ENDIF()

# H02: golden-frame/metric/memory CI gate on top of H01's x86 GLES2 build
# (build-gles2/, gitignored, produced by scripts/build-gles2-x86.sh). Not a
# GTest binary — it's a script that drives gles2_zone_benchmark directly.
# Registered unconditionally (not gated on an EQT_GLES2 CMake option, since
# build-gles2/ is a separate, independently-configured build tree from this
# one) — the script itself detects a missing benchmark binary and exits 125,
# which SKIP_RETURN_CODE below turns into a clean ctest SKIP rather than a
# failure, so the default `build/` + ctest suite stays fully independent of
# whether build-gles2/ has ever been built in this worktree.
ADD_TEST(NAME visual_gate COMMAND ${CMAKE_SOURCE_DIR}/scripts/ci/visual_gate.sh)
# T261: RUN_SERIAL. The gate's p95 CPU-ms assertion (visual_gate.py, ceiling
# = 2x the blessed baseline) was a steady_clock wall-time measurement of the
# benchmark's CPU draw phase (tools/hwcaps/zone_bench_metrics.cpp cpuDrawMs),
# so it was load-sensitive: under `ctest -j24` it shares llvmpipe's CPU with
# ~23 other tests and tripped the befallen ceiling in T219/T220/T239/T258
# while passing every time alone. The image/metric/memory assertions never
# failed under load -- only the timing one. RUN_SERIAL gives the gate the
# machine to itself; ctest still runs everything else in parallel around it
# (measured T261: suite wall-time unchanged within noise, see
# refactor/T261_visual_gate_serial.md).
#
# T291 (Will's ruling, docs/decisions.md 2026-08-26) attacked the same
# flakiness at the source instead: the gate now asserts on PROCESS CPU time
# (getrusage) rather than wall clock, and the benchmark's --camera-pose path
# finally honours --warmup so the measured frames are warm rather than
# carrying every one-time cost. RUN_SERIAL is KEPT anyway, deliberately.
# T291 measured that it is no longer strictly necessary -- three full
# `ctest -j24` runs with it removed passed the gate every time and cut suite
# wall time from 102.8 s to 62.6-71.6 s -- but the remaining sensitivity is
# NOT scheduling, it is memory-bandwidth/cache contention inflating the
# benchmark's real CPU cycles, which no clock change can remove: under a
# deliberately oversubscribed `cmake --build -j48` the befallen p95 reached
# 9.98 ms against its 10.95 ms ceiling (91%). Per Will's standing decision
# that load time is secondary to correctness, ~35 s of suite time is the
# right price for that margin. Dropping it is a one-line change if the
# tradeoff is ever revisited -- see refactor/T291_visual_gate_cpu_metric.md.
SET_TESTS_PROPERTIES(visual_gate PROPERTIES SKIP_RETURN_CODE 125 RUN_SERIAL TRUE)

# T168: the zero-caller-sender gate. Fails if any EverQuest::Send*/Request*
# method has no production call site -- the condition that held for 28 of 97
# senders when the arc opened, and that roughly one in two turned out to be
# broken on the wire. Pure source scan: no build artefact, no server, no
# display, so it runs in every configuration. See
# refactor/T168_sender_triage.md and the script's own docstring for what it
# deliberately does NOT catch.
ADD_TEST(NAME check_sender_reachability
    COMMAND python3 ${CMAKE_SOURCE_DIR}/scripts/ci/check_sender_reachability.py)

# T175: the stale-binary gate. Fails if any executable in build/bin is produced
# by no current CMake target -- the condition four deleted-source test binaries
# held on 2026-08-14, still running and still passing a week after their targets
# and sources were removed (cmake never prunes bin/). Asks the CMake file API
# (codemodel-v2) which artifacts the configured targets claim under bin/, and
# flags any file that is not one. Pure: no server, no display, no network. It
# inspects the build tree it is run against, so it SKIPs (125) when there is no
# configured tree -- the CMAKE_BINARY_DIR here IS such a tree at ctest time. See
# refactor/T175_build_tree_hygiene.md and the script's docstring.
ADD_TEST(NAME check_build_tree_orphans
    COMMAND python3 ${CMAKE_SOURCE_DIR}/scripts/ci/check_build_tree_orphans.py
            ${CMAKE_BINARY_DIR})
SET_TESTS_PROPERTIES(check_build_tree_orphans PROPERTIES SKIP_RETURN_CODE 125)

# T108: regression test -- totalEstimate must not credit eqe1MappedBytes
# (address-space/mapped size, not RSS) as if it were resident. Drives the
# already-built gles2_zone_benchmark (same tool + same computeMemorySnapshot()
# the memory gate uses -- tools/hwcaps/zone_bench_memory.cpp) with real EQE1
# archive data. SKIPs (125, same convention as visual_gate above) if the
# benchmark wasn't built (EQT_GRAPHICS off) or no EQE1 archive data is present
# on this machine -- see the script's own docstring.
IF(TARGET gles2_zone_benchmark)
    ADD_TEST(NAME test_eqe1_totalestimate_accounting
        COMMAND python3 ${CMAKE_SOURCE_DIR}/scripts/ci/test_eqe1_totalestimate_accounting.py
                $<TARGET_FILE:gles2_zone_benchmark>)
    SET_TESTS_PROPERTIES(test_eqe1_totalestimate_accounting PROPERTIES SKIP_RETURN_CODE 125)
ENDIF()

# B852 (issue #98): the offline zone-content sweep's smoke leg.
# Will, 2026-09-20: "velious and kunark zones are part of the willeq target,
# so we need to verify that content works." The full 118-zone sweep is a
# human/orchestrator tool (scripts/ci/zone_content_sweep.sh, docs/ci.md);
# ctest carries the 3-zone smoke (one classic, one Kunark, one Velious) so a
# loader regression that breaks any expansion is caught on every push. The
# script drives gles2_zone_benchmark (build/ or build-gles2/, whichever the
# tree has) over the smoke list and fails on any non-clean TSV row.
# Missing benchmark or missing EQ assets: exit 125 -> ctest SKIP, printed
# loudly to stderr (the card's "SKIPs loudly" rule); CI's test job sets
# WILLEQ_REQUIRE_ASSETS=1 and re-makes the symlinks, so a red there is real.
# Label `slow`: three full zone loads (llvmpipe) are a matter of tens of
# seconds idle but more under load; RUN_SERIAL keeps it off the parallel
# pool, same rationale as visual_gate's T261 note above.
ADD_TEST(NAME zone_content_sweep_smoke
    COMMAND ${CMAKE_SOURCE_DIR}/scripts/ci/zone_content_sweep_smoke.sh)
SET_TESTS_PROPERTIES(zone_content_sweep_smoke PROPERTIES
    LABELS "slow"
    SKIP_RETURN_CODE 125
    RUN_SERIAL TRUE
    TIMEOUT 900)
