# Tests CMakeLists.txt

# Include Google Test
INCLUDE(GoogleTest)

# T163 (2026-08-21): move gtest test discovery OUT of the parallel build.
#
# The default DISCOVERY_MODE is POST_BUILD: CMake attaches a post-build
# command to every test target that runs the freshly linked binary with
# --gtest_list_tests *as part of the build*. With the 112
# GTEST_DISCOVER_TESTS() calls below and `cmake --build build -j24`, that is
# up to 112 process launches competing with 24 compile/link jobs -- and the
# GoogleTest module's discovery timeout defaults to 5 SECONDS. Three units in
# three days (T159, T161, T162) plus T79's 3x before them saw the first
# from-scratch -j24 build die inside GoogleTestAddTests.cmake with no compiler
# error; T162 captured the verbatim reason: "Error running test executable ...
# test_login_world_seam ... Process terminated due to timeout". An immediate
# rebuild was always clean, because by then nothing else was competing.
#
# The aggravating factor is that this checkout lives on an NFS mount: each
# test binary is 70-80 MB, and discovery exec's it immediately after the
# linker wrote it, while up to 24 other links are saturating the same mount.
# Measured on an idle host, the slowest --gtest_list_tests of all 118 test
# binaries is 0.21s; sampled continuously through a full from-scratch -j24
# build it peaked at 1.07s. Neither is near 5s, which is why the failure is
# intermittent rather than constant -- it needs a cold read-back of a
# just-linked binary through a loaded NFS mount on top of the CPU contention.
#
# PRE_TEST moves discovery to ctest time, where it runs serially and with no
# compile/link load at all, so the contention is removed rather than merely
# given more slack (which is all T79's per-target raised timeouts did -- see
# the removal note further down for why those never covered the right targets
# anyway). One variable applies to every GTEST_DISCOVER_TESTS() call in this
# directory, so there is nothing to remember at the 112 call sites.
#
# Version floor: DISCOVERY_MODE and this variable were both added in CMake
# 3.18, NOT 3.20 as T79's comment stated -- that mistake is what made T79
# reject this fix. The oldest CMake that has to configure this project is
# 3.18.4 (debian:bullseye, docker/Dockerfile.arm-cross; every other path is
# ubuntu:24.04 / 3.28). So no build path is left behind and no version guard
# is needed. CMAKE_MINIMUM_REQUIRED stays at 3.14 deliberately: raising it
# would flip ~15 CMake policies to NEW across a 1700-line CMakeLists for no
# reason connected to this fix. On a hypothetical pre-3.18 CMake this
# variable is simply ignored and discovery falls back to POST_BUILD, which is
# today's behaviour -- degraded, never broken. The warning below makes that
# visible instead of silent.
#
# What PRE_TEST does NOT change: gtest_discover_tests() still sets each
# test's WORKING_DIRECTORY to CMAKE_CURRENT_BINARY_DIR (<build>/tests) in
# both modes, so the T36 note directly below still holds, and `ctest -R
# <name>` still selects individual tests.
IF(CMAKE_VERSION VERSION_LESS 3.18)
    MESSAGE(WARNING
        "CMake ${CMAKE_VERSION} predates DISCOVERY_MODE (3.18+); gtest test "
        "discovery falls back to POST_BUILD and the -j24 discovery-timeout "
        "failure T163 fixed can recur. Use CMake 3.18 or newer.")
ENDIF()
SET(CMAKE_GTEST_DISCOVER_TESTS_DISCOVERY_MODE PRE_TEST)

# T452 (2026-09-07): DISCOVERY_TIMEOUT 120 on every GTEST_DISCOVER_TESTS()
# call in this directory (292 of them, across tests/CMakeLists.txt and the
# nineteen tests/cmake/*.cmake fragments).
#
# T163 above moved discovery OUT of the parallel build, which removed the
# contention this project generates for itself. It does NOT remove the
# contention from the OTHER worktree slots: three executors build
# concurrently by design, and a `ctest` in slot2 runs its PRE_TEST discovery
# while slot1 and slot3 are saturating 24 cores and the same NFS mount. The
# GoogleTest module's default DISCOVERY_TIMEOUT is 5 SECONDS, and under that
# cross-slot load a random test binary exceeds it on nearly every run --
# ctest then reports "Process terminated due to timeout" against an innocent
# test and stops at the FIRST such failure, so the run proves nothing and
# costs a full suite each time.
#
# 120 s is slack, not a fix for a slow binary: the measured worst
# --gtest_list_tests over all binaries is 0.21 s idle and 1.07 s under this
# project's own -j24 build (T163's measurements). A discovery that genuinely
# takes over two minutes is a real defect and still fails.
#
# There is no directory-level variable for this -- unlike DISCOVERY_MODE,
# the GoogleTest module reads DISCOVERY_TIMEOUT only as a per-call keyword
# (`if(NOT arg_DISCOVERY_TIMEOUT) set(arg_DISCOVERY_TIMEOUT 5)`), so it is
# spelled at every call site. A NEW test target must carry it too; the
# keyword goes last, after PROPERTIES, which is a multi-value keyword that
# terminates at the next recognised one.

# T36: repo root as a compile-time define, for EVERY test target in this
# directory and below.
#
# gtest_discover_tests() sets each test's ctest WORKING_DIRECTORY to the
# executable's own binary dir (<build>/tests), NOT the repo root. Any test
# that resolved a data path relatively therefore looked for it under
# <build>/tests/ and silently GTEST_SKIP()'d when it wasn't there -- which
# is every freshly created build/ dir, since build/ is gitignored. That hid
# the golden session replay, the primary regression oracle for every
# source-touching unit, behind a "Skipped" line that reads exactly like the
# legitimate "no capture recorded" skip.
#
# The pattern itself already existed (test_zone_lines.cpp's RepoPath(),
# added with its own per-target define and the comment "to stay independent
# of ctest's working directory") -- it just was never generalised. This
# makes it available everywhere so the next test that needs a repo file
# inherits it instead of rediscovering the trap.
ADD_COMPILE_DEFINITIONS(WILLEQ_REPO_ROOT="${CMAKE_SOURCE_DIR}")

# Common include directories for tests
INCLUDE_DIRECTORIES(
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/third_party
)

# Default EQ client path (resolved at configure time, overridable via EQ_CLIENT_PATH env var)
ADD_COMPILE_DEFINITIONS(EQ_CLIENT_DATA_PATH="${CMAKE_SOURCE_DIR}/data/eq_client")

# P39: path to the real shipped constrained-presets JSON, so tests can load
# it directly (not a synthetic copy) and assert against production values.
ADD_COMPILE_DEFINITIONS(WILLEQ_CONSTRAINED_PRESETS_JSON="${CMAKE_SOURCE_DIR}/config/constrained_presets.json")

# SQLite flags for WilleqDatabase (needed by any target pulling in willeq_database.cpp/sqlite3.c)
ADD_COMPILE_DEFINITIONS(SQLITE_THREADSAFE=1 SQLITE_OMIT_LOAD_EXTENSION)

# Helper variable for fmt library
IF(fmt_FOUND)
    SET(FMT_LIB fmt::fmt)
ELSE()
    SET(FMT_LIB ${FMT_LIBRARY})
ENDIF()

# ---------------------------------------------------------------------------
# J20 (2026-09-02): the test targets live in per-domain fragments under
# tests/cmake/. This file was 2,850 lines -- within 150 of the 3,000-line hard
# cap in scripts/refactor/check_file_sizes.sh -- and every Batch J unit appends
# to it. The fragments are INCLUDE()d, not ADD_SUBDIRECTORY()d, so they share
# this directory scope: the include directories, compile definitions and
# variables set above apply to every target in them, exactly as before.
#
# Order is the original file order and is load-bearing where a fragment sets a
# variable its own targets consume (ACTION_LAYER_STATE_SOURCES in commands,
# TEST_PATHFINDER_* in world_geometry, AUDIO_COMMON_TEST_* in audio).
#
# ADD A NEW TEST TARGET TO THE FRAGMENT FOR ITS DOMAIN, NOT TO THIS FILE.
# ---------------------------------------------------------------------------

INCLUDE(cmake/core_utils.cmake)
INCLUDE(cmake/network.cmake)
INCLUDE(cmake/midi.cmake)
INCLUDE(cmake/state.cmake)
INCLUDE(cmake/commands.cmake)
INCLUDE(cmake/spells.cmake)
INCLUDE(cmake/world_geometry.cmake)
INCLUDE(cmake/data_formats.cmake)
INCLUDE(cmake/database.cmake)
INCLUDE(cmake/archives.cmake)
INCLUDE(cmake/graphics.cmake)
INCLUDE(cmake/zone_integration.cmake)
INCLUDE(cmake/action_handlers.cmake)
INCLUDE(cmake/ui_rendering.cmake)
INCLUDE(cmake/renderer_guards.cmake)
INCLUDE(cmake/audio.cmake)
INCLUDE(cmake/eqe1.cmake)
INCLUDE(cmake/packets_skills.cmake)
INCLUDE(cmake/hygiene.cmake)
