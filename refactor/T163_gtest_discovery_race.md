# T163 — The gtest-discovery build failure, root-caused and fixed

**Slot: slot1. x86. NO eqemu, NO lock. Pi/ARM: read-only investigation only —
do NOT run a Pi build.**

**Territory: `tests/CMakeLists.txt`, `CMakeLists.txt`, `cmake/`,
`scripts/build-*.sh` (only if a build path needs a matching change),
`refactor/`. No production source, no tests.**

## The symptom, seen three times in three days

The first `cmake --build build -j24` fails inside `GoogleTestAddTests.cmake`
with **no compiler error**; an immediate rebuild is clean. Hit by **T159**,
**T161** and **T162** (T162's was on `test_login_world_seam`). T79 saw the same
thing 3× in one day on `test_spell_manager` and reported it as
`"Error running test executable"` — a **discovery-time timeout, not a compile
failure**.

**Do not treat this as flaky-and-move-on.** Four units have now paid for it, and
each retry costs a full link. It is also actively misleading: a build error that
names a CMake file makes an executor go looking for a code defect that is not
there.

## What is already known — start here, do not re-derive

- `tests/CMakeLists.txt` calls **`GTEST_DISCOVER_TESTS` 121 times**, once per
  test target.
- The default `DISCOVERY_MODE` is **`POST_BUILD`**: CMake runs each freshly
  linked test binary with `--gtest_list_tests` **as part of the build**. Under
  `-j24` that is up to 121 process launches competing with 24 compile jobs on
  the same host — a heavy binary can exceed the module's **5-second default**
  timeout.
- T79's mitigation was `DISCOVERY_TIMEOUT 60` on **six** targets only
  (`test_spell_manager`, `test_graphics`, `test_eq_action_handler_spell_pet`,
  `test_eq_action_handler_tradeskill`, `test_skill_possession_gate`,
  `test_skill_gate_bypasses`). Its own comment says this "doesn't remove the
  contention, it just gives it more slack" — and the three fresh sightings are
  on targets **without** the raised timeout, which is exactly what that comment
  predicted.
- T79 rejected the real fix for a stated reason: **`DISCOVERY_MODE PRE_TEST` is
  a CMake 3.20+ keyword and this project declares
  `CMAKE_MINIMUM_REQUIRED(VERSION 3.14)`.** The dev host runs **3.28.3**.

## The job

**1. Confirm the mechanism before changing anything.** A timeout under load and
a genuine race are different bugs with different fixes. Get the actual failure
text (`GoogleTestAddTests.cmake` reports the executable and the reason) — from
`build/`'s logs if a stale one survives, or by reproducing under load. Say which
you established and how. **If the evidence says it is not the timeout, follow
the evidence** and report that instead; the rest of this plan assumes T79's
diagnosis and the plan is not the authority on the answer.

**2. Fix it globally, not per-target.** `CMAKE_GTEST_DISCOVER_TESTS_DISCOVERY_MODE`
set to `PRE_TEST` applies to every `gtest_discover_tests()` call after it, moving
discovery out of the build entirely and into ctest. One line beats 121.

**Resolve the 3.14-vs-3.20 question rather than assuming it away.** Establish the
oldest CMake that actually has to configure this project — the dev host, the
GLES2 script tree, and the docker images under `docker/` (`Dockerfile.arm-cross`
is the likely floor; `Dockerfile.arm64-noble` is not). Then either raise
`CMAKE_MINIMUM_REQUIRED` if nothing real is left behind, or **guard the setting**
(`if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.20)`) and keep today's behaviour plus
timeouts as the fallback. Guarding is the safe answer if any build path is
older; say which you chose and what evidence decided it.

**3. Check what PRE_TEST changes.** Discovery moves to test time, so the failure
mode moves too. Confirm: `ctest -R <name>` still works, the suite still reports
the same total, and — the one that matters — **cross-compiled ARM test binaries
are not made to run on the x86 host.** Check whether the ARM build paths enable
`EQT_BUILD_TESTS` at all before worrying, and report the answer either way.
Also confirm the `WORKING_DIRECTORY` behaviour noted at `tests/CMakeLists.txt:9`
survives the change — a test that resolves data paths relative to its working
directory will fail loudly if that moves.

**4. Once the global mode is in, the six `DISCOVERY_TIMEOUT 60` overrides are
probably dead weight.** Remove them if PRE_TEST makes them meaningless, or keep
them with a comment saying why. Do not leave them unexplained.

## Acceptance

- **Three consecutive from-scratch `-j24` builds with zero discovery failures.**
  A from-scratch build is the reproduction condition — an incremental build
  relinks too little to contend. This is probabilistic evidence, so state the
  number of trials rather than claiming the race is impossible.
- Full suite total **unchanged at 2595/2596** (the discovery mechanism must not
  silently drop tests — a suite that shrinks is the main hazard of this change).
- Report the build wall-clock before and after; removing 121 binary launches
  from the build should be visible, and if it is not, that is worth a sentence.

## Verification

Full protocol bar. Baseline **2595/2596** at tip `d72162a4`, sole expected
failure the documented ALSA `AudioManagerTest.PreloadCommonSounds`.

## STOP conditions

- The oldest supported CMake cannot be established from the repo (report what
  you found; do not guess a floor).
- The mechanism turns out not to be discovery-under-load — report the real one.

## Review

(executor fills this in)
