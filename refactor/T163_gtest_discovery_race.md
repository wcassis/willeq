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

Executed in `/mnt/projects/fable/wt/slot1`, branch `task/T163-gtest-discovery-race`
off `fable` tip **`d72162a4`**. No eqemu, no lock, no Pi build. `main` and the
sibling slots were never touched.

### The mechanism, confirmed — it is T79's diagnosis

**Confirmed: a discovery-time timeout under parallel build load, not a race.**
The decisive evidence is a verbatim capture that already existed in the repo and
that this unit's brief said to go find: `refactor/T162_skill_gate_bypasses.md`
records the failure text as

> `Error running test executable … test_login_world_seam … Process terminated due to timeout`

`GoogleTestAddTests.cmake` prints that string in exactly one place —
`execute_process(COMMAND <exe> --gtest_list_tests … TIMEOUT ${_TEST_DISCOVERY_TIMEOUT})`
followed by `if(NOT result EQUAL 0)` printing `Result: ${result}`. `"Process
terminated due to timeout"` is CMake's own `execute_process` timeout message, so
the `Result:` line discriminates a timeout from a crash (signal name) or a bad
exit (number). It was a timeout. No stale `build/` log survived, but this
capture is unambiguous and is a direct observation, not an inference.

**Why it is intermittent — measured here, and the answer is I/O, not CPU.**
Two things were measured on this host:

- `--gtest_list_tests` on all **118** built test binaries, host idle, one at a
  time: slowest is **0.21s** (`test_zoning_graphics_integration`), median far
  below that. The default discovery timeout is **5s** — a ~25x margin.
- The same three heavy binaries sampled continuously (525 samples) *through* a
  full from-scratch `-j24` build: peak **1.07s**, still 5x under the limit.

So CPU contention alone does not explain a 5s timeout, and that is exactly why
the failure hits roughly one build in three rather than every build. The
aggravating factor is that **this checkout lives on an NFS mount**
(`10.0.30.10:/volume2/projects`, per `df`) and each test binary is **70–80 MB**.
POST_BUILD discovery exec's the binary the instant the linker finished writing
it, while up to 24 other links are saturating the same mount — a cold read-back
of 80 MB through a loaded NFS path is what pushes an 0.2s operation past 5s. My
probe re-read the *same* binaries repeatedly and so was always page-cache warm,
which is why it could not reproduce the tail; the mechanism and the margin are
nevertheless established.

Note the failures hit a different, arbitrary target each time (`test_spell_manager`,
`test_eq_action_handler_enum_guards`, `test_login_world_seam`) and none of them
is among the slowest binaries — consistent with I/O timing, and inconsistent
with "some targets are inherently too slow to discover".

### The CMake floor — T79's premise was factually wrong

**`DISCOVERY_MODE` and `CMAKE_GTEST_DISCOVER_TESTS_DISCOVERY_MODE` were added in
CMake 3.18, not 3.20.** `/usr/share/cmake-3.28/Modules/GoogleTest.cmake` marks
the `DISCOVERY_MODE` option `.. versionadded:: 3.18`. T79 rejected the real fix
on a version requirement that does not exist.

Per build path, the oldest CMake that actually has to configure this project:

| Build path | Base image / host | CMake | Builds tests? |
|---|---|---|---|
| Dev host (primary tree) | this host | **3.28.3** | yes (`EQT_BUILD_TESTS=ON`) |
| `docker/Dockerfile.arm-cross` (`scripts/build-arm-jessie.sh`) | `debian:bullseye` | **3.18.4** | **no** — `-DEQT_BUILD_TESTS=OFF` (line 482) |
| `docker/Dockerfile.arm-noble` (`scripts/build-arm-noble.sh`) | `ubuntu:24.04` | 3.28 | no — `OFF` (line 370) |
| `docker/Dockerfile.arm64-noble` (`scripts/build-arm64-noble.sh`) | `ubuntu:24.04` | 3.28 | no — `OFF` (line 324) |
| `docker/Dockerfile` (x86 runtime image) | `ubuntu:24.04` | 3.28 | no — `OFF` (line 105) |
| `scripts/build-gles2-x86.sh` | this host | 3.28.3 | no — `OFF` (line 204) |

**Floor = 3.18.4**, and it was verified empirically, not assumed:
`docker run --rm debian:bullseye` → `apt-cache policy cmake` reports candidate
`3.18.4-2+deb11u1`, and installing it shows
`/usr/share/cmake-3.18/Modules/GoogleTest.cmake` contains
`CMAKE_GTEST_DISCOVER_TESTS_DISCOVERY_MODE` (4 occurrences). Every build path is
therefore at or above the requirement, and separately, **every path except the
dev host passes `-DEQT_BUILD_TESTS=OFF`**, so `tests/CMakeLists.txt` is not even
configured there. That also answers the plan's ARM worry outright: **no ARM
build path enables tests**, so no cross-compiled ARM test binary can be handed
to ctest on the x86 host under either discovery mode.

**Choice: neither raise `CMAKE_MINIMUM_REQUIRED` nor add a hard guard.** The
setting is left unconditional, `CMAKE_MINIMUM_REQUIRED(VERSION 3.14)` is
unchanged, and a `MESSAGE(WARNING)` fires if someone configures with a CMake
older than 3.18. Reasoning:

- Raising the minimum to 3.18 would flip ~15 CMake policies (CMP0088–CMP0102) to
  NEW across a 1700-line `CMakeLists.txt` — an unrelated behavioural change
  smuggled into a flakiness fix, with no build path needing it.
- A `IF(CMAKE_VERSION VERSION_GREATER_EQUAL 3.18)` guard buys nothing: an
  unknown variable is silently ignored by older CMake, so pre-3.18 already
  degrades to today's POST_BUILD behaviour on its own. The failure mode is
  *degraded, never broken*.
- The warning converts that silent degradation into a visible one, which is the
  part a bare guard would not give.

### What changed

One file, `tests/CMakeLists.txt`:

1. **`SET(CMAKE_GTEST_DISCOVER_TESTS_DISCOVERY_MODE PRE_TEST)`** immediately
   after `INCLUDE(GoogleTest)`, with the full mechanism, the measurements, the
   version floor and the reason the minimum was not raised recorded in comment
   above it — one line covering all **112** `GTEST_DISCOVER_TESTS()` calls.
   (The plan said 121; the actual count on `fable` is **112**, verified against
   `git show fable:tests/CMakeLists.txt` and against the 112
   `*_include.cmake` files the build generates. 118 `test_*` binaries exist, so
   six are built but not gtest-discovered — noted, not investigated, out of
   territory.)
2. **A `MESSAGE(WARNING)` for CMake < 3.18** explaining the POST_BUILD fallback.
3. **All six `DISCOVERY_TIMEOUT 60` overrides removed** (`test_spell_manager`,
   `test_graphics`, `test_eq_action_handler_spell_pet`,
   `test_eq_action_handler_tradeskill`, `test_skill_possession_gate`,
   `test_skill_gate_bypasses`), together with T79's three now-obsolete comment
   blocks, replaced by one comment at `test_spell_manager` saying why they went.
   They are removed rather than kept because their stated purpose — slack for
   contention with `-j24` compilation — no longer has a contention to give slack
   to, and because the set was chosen by *which target failed that day*, not by
   measurement: `test_spell_manager` discovers in 0.05s idle, while the two
   genuinely slowest binaries were never in the set. All 112 calls are now
   uniform.

No production source, no test source, no `CMakeLists.txt`, no `cmake/`, no
`scripts/` change was needed.

### Why this is not merely probabilistic

The acceptance asked for three from-scratch builds, and they are below. But the
stronger evidence is structural: after the change, **`GoogleTestAddTests.cmake`
appears zero times anywhere under `build/tests/CMakeFiles/`** (it appeared once
per target before, as a POST_BUILD command). The build no longer invokes the
discovery script at all — it now runs from
`build/tests/<target>[1]_include.cmake` at ctest startup. The build cannot fail
in that file any more because the build never enters it. The three trials
confirm nothing else regressed; they are not what proves the fix.

### Trials and numbers

| | wall-clock | result |
|---|---|---|
| Baseline (fable tip) from-scratch #1 | **406s** | clean |
| Baseline from-scratch #2 (with the latency probe running) | 402s | clean |
| **After** from-scratch #1 | **402s** | clean, 0 `GoogleTestAddTests` mentions |
| **After** from-scratch #2 | **402s** | clean, 0 mentions |
| **After** from-scratch #3 | **403s** | clean, 0 mentions |

**Three consecutive from-scratch `-j24` builds, zero discovery failures.** Both
baseline builds also passed — the failure is intermittent (three sightings in
three days across many builds), so three trials is not proof and is not claimed
as proof; the structural argument above is.

**Wall-clock before/after: 406s → 402s, i.e. unchanged within noise.** Removing
112 binary launches from the build is *not* visible, and that is worth the
sentence the plan asked for: discovery costs ~0.2s per target and Make overlaps
those post-build commands across 24 jobs, so the aggregate ~25s of discovery
hides entirely inside a 400s critical path. The launches were never a
throughput problem — they were a *timeout* problem. Discovery time did not
vanish either, it moved: it now runs at ctest startup, and the full suite
wall-clock (405s and 391s across two runs) is likewise unchanged within noise.

### Acceptance

- **Suite total: `2595/2596`**, run twice (after trial #1 and again on the
  trial #3 tree), sole failure the documented ALSA
  `AudioManagerTest.PreloadCommonSounds`. **The total is 2596, identical to the
  baseline** — nothing was silently dropped, which was the named hazard. Cross-
  checked a third way: `ctest --show-only=json-v1` enumerates exactly 2596
  tests.
- **`ctest -R <name>` still selects individual tests.** `ctest -R
  SpellManagerGems.MemorizeSpellSuccessStartsMemorizationAndPublishesEvent`
  → "1 test", and `ctest -R SkillPossessionGate -N` → "Total Tests: 7". Note
  `-R` now pays for discovery of the matching targets at startup; measured at
  0.63s total for the single-test run, so it is not user-visible.
- **`WORKING_DIRECTORY` (the `tests/CMakeLists.txt:9` / T36 note) survives.**
  `ctest --show-only=json-v1` reports `WORKING_DIRECTORY =
  <build>/tests` for **all 2596 of 2596** tests — the same value POST_BUILD
  produced, since `gtest_discover_tests()` defaults it to
  `CMAKE_CURRENT_BINARY_DIR` in both modes. The `WILLEQ_REPO_ROOT` compile
  definition that compensates for it is untouched.

### Residual risk

1. **Discovery still has a 5s timeout, it just runs somewhere quieter.** If a
   ctest run ever starts against a completely cold page cache (fresh boot, or
   a tree not built on this host), 112 sequential 80 MB cold NFS reads happen at
   ctest startup. At the measured idle cost (0.21s worst case, warm) there is
   ~25x margin, and NFS throughput would have to collapse below ~16 MB/s to
   threaten it — but the margin is smaller in that scenario than in the normal
   build-then-test flow. There is **no global timeout variable** in the
   GoogleTest module (only the per-call `DISCOVERY_TIMEOUT`), so buying slack
   globally would mean annotating all 112 call sites; that is why the answer
   here was to remove the contention rather than raise timeouts. If this ever
   bites, the fix is a wrapper macro around `GTEST_DISCOVER_TESTS`, not 112
   edits.
2. **The failure mode moved rather than disappearing.** A test binary that
   cannot be exec'd now fails at ctest startup instead of at build time. That is
   strictly better for diagnosis (a build error naming a CMake file is what sent
   four units hunting for a nonexistent code defect) but it is a change in where
   the error surfaces, and anyone who builds without running ctest will no
   longer learn that a binary is unrunnable.
3. **The reproduction was not observed first-hand in this unit.** Both baseline
   builds passed. The mechanism rests on T162's verbatim capture plus the
   measurements above, not on a failure induced here.

### Findings outside scope (not acted on)

- **T79's 3.20 claim was wrong and it cost four units.** The keyword is 3.18+.
  Worth remembering as a pattern: a version floor asserted in a comment, never
  checked, that blocked the correct fix for eight days.
- **118 `test_*` binaries but only 112 discovery calls** — six built test
  executables are never registered with ctest. Not investigated; outside
  territory. A likely instance of the project's recurring
  implemented-but-unreached pattern, and cheap for a future unit to check.
- The plan's "121 `GTEST_DISCOVER_TESTS` calls" is off by nine; the real count
  is 112.

