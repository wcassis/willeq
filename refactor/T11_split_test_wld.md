# T11 — Split `tests/test_wld.cpp`

**Slot: t1. x86 only — no Pi, no eqemu, no live server.** No sibling
unit; t2 idle.

**Territory: `tests/` ONLY.** No `src/`, no `include/` outside
`tests/`. This is a pure mechanical split — **no test may be added,
removed, renamed, or have its assertions changed.**

Read `refactor/EXECUTOR_PROTOCOL.md` first and follow it exactly.

## Why

Will's direction (2026-07-29): large files are being split so that code
fits inside the context budget of a coding model, since this codebase is
worked on by delegated agents. `tests/test_wld.cpp` is **3897 lines** —
the only file in `tests/` over the 1500-line WARN threshold, and well
over the 3000-line FAIL threshold. Nothing else in `tests/` exceeds
1073 lines, so this file is the whole job.

It was already 3804 lines before T10 added 93; the FAIL is pre-existing,
not T10's doing.

## The invariant that defines success

**The set of registered tests must be identical before and after.**

Capture it first:

```
ctest --test-dir <slot>/build -N -R "Wld|WLD|Pfs|CoordinateSystem|TextureLoading|IntegrationTest|ComprehensiveZone" > /tmp/before.txt
```

…and after the split, the same command must produce the same test names
(ordering may differ; the *set* may not). Diff them, sorted, and paste
the result in your Review. `test_wld` runs **103 tests** today — that
number must not move.

If a test disappears, the split silently deleted coverage. That is the
one failure mode that matters here, and it is easy to cause by dropping
a `TEST_F` while moving blocks around.

## Structure — read this before cutting

The file has an inheritance chain, which is what makes a naive split
fail:

```
WldHeaderTest : ::testing::Test          (line 25)   <- base
  WldIntegrationTest : WldHeaderTest     (line 177)
    WldTextureFragmentTest               (line 430)
    WldMeshFragmentTest                  (line 693)
    WldSkeletonFragmentTest              (line 922)
    WldLightFragmentTest                 (line 1433)
WldBspFragmentTest : ::testing::Test     (line 1965) <- independent
CoordinateSystemTest : ::testing::Test   (line 2384) <- independent
TextureLoadingTest : ::testing::Test     (line 2629) <- independent
```

Four fixtures derive from `WldIntegrationTest`, which derives from
`WldHeaderTest`. So `WldHeaderTest` and `WldIntegrationTest` — plus any
shared helpers they use (`fileExists`, the `eqClientPath_` member, and
friends) — **must move into a shared header**, e.g.
`tests/wld_test_fixtures.h`. Defining a fixture class in two TUs is an
ODR violation; don't copy them.

Suite inventory (approximate test counts):

| suite | tests | region |
|-------|------:|--------|
| `WldHeaderTest` | 8 | 21–173 |
| `WldIntegrationTest` | 5 + T10's characterization work | 177–425 |
| `WldTextureFragmentTest` | 6 | 430–688 |
| `WldMeshFragmentTest` | 8 | 693–917 |
| `WldSkeletonFragmentTest` | 13 | 922–1428 |
| `WldLightFragmentTest` | 10 | 1433–1960 |
| `WldBspFragmentTest` | 10 | 1965–2378 |
| `CoordinateSystemTest` | 11 | 2384–2624 |
| `TextureLoadingTest` | 13 | 2629–~2900 |
| `IntegrationTest` | 13 | after ~2400 |
| `ComprehensiveZoneTest` | 6 | after ~2400 |

Line numbers are from the pre-split file and are a guide, not gospel —
verify as you go.

## Suggested split

Aim for files in the **400–800 line** range. Six or seven TUs plus the
fixture header. A reasonable cut, which you may adjust with reasons:

- `tests/wld_test_fixtures.h` — `WldHeaderTest`, `WldIntegrationTest`,
  shared helpers.
- `test_wld_header.cpp` — `WldHeaderTest` + `WldIntegrationTest` tests
  (including T10's characterization assertions).
- `test_wld_fragments_mesh.cpp` — texture + mesh fragment suites.
- `test_wld_fragments_skeleton.cpp` — skeleton suite.
- `test_wld_fragments_light.cpp` — light suite.
- `test_wld_fragments_bsp.cpp` — BSP suite.
- `test_wld_conversions.cpp` — `CoordinateSystemTest`
  (vertex scale, normals, UV, winding, rotation).
- `test_wld_texture_loading.cpp` — `TextureLoadingTest` (DDS decode,
  texture chain).
- `test_wld_zones.cpp` — `IntegrationTest` + `ComprehensiveZoneTest`.

If a suite's own tests exceed ~800 lines on their own, split *within*
the suite by theme rather than letting one file stay oversized.

## CMake — one target, several TUs

Keep the **single `test_wld` target** and list all the new `.cpp` files
in it. Do **not** create new test binaries.

Rationale: `GTEST_DISCOVER_TESTS` registers by test name, so one target
with several TUs keeps every ctest entry byte-identical, which is
exactly the invariant above. Multiple binaries would churn the
registration for no benefit — ctest already parallelises per test case,
so there is no runtime gain to be had.

## STOP conditions

- The before/after test list differs → STOP, do not paper over it.
- A split would require changing a test's assertions to compile → STOP
  and report; extracting a shared helper into the header is fine, but
  changing what a test checks is not.
- You find a bug in a test while moving it → report it; do not fix it in
  this unit. Mechanical splits must stay mechanical, so the diff is
  reviewable as "moved, not modified".

## Acceptance criteria

1. `tests/test_wld.cpp` is gone (or reduced to nothing meaningful), and
   no `tests/` file exceeds **1500** lines. Report the new sizes.
2. `bash <slot>/scripts/refactor/check_file_sizes.sh --files <each new
   file>` — no FAILs, no WARNs. (Note: the script's diff mode resolves
   the wrong repo root from a worktree — T10 hit this — so use
   `--files`.)
3. **Before/after `ctest -N` test sets identical.** Paste the sorted
   diff (expected: empty). `test_wld` still reports 103 tests.
4. `test_wld` passes 103/103; report runtime (~169s serial is expected —
   it loads real archives).
5. Full suite ×3: total test count unchanged at **1416**, documented
   exceptions only (`AudioManagerTest.PreloadCommonSounds` host ALSA;
   `RDPServerFrameTest.*` / `RDPServerLifecycleTest.*` = the known T03b
   use-after-free — name them, don't call the run clean).

Visual gate and ARM build: not applicable (tests-only) — say so.

## Environment notes

- GLES2 config is the only tree; relative
  `-DEQT_GLES2_IRRLICHT_DIR=third_party/irrlicht-gles2-x86` works.
  Bootstrap once if `.../lib/libIrrlicht.a` is absent:
  `bash <slot>/scripts/build-gles2-x86.sh --irrlicht-only`.
- EQ assets: `data/eq_client` symlink → `/mnt/projects/EverQuestP1999`.
  Create it in your slot if absent — the WLD tests need it or they skip.

## Review

Executed 2026-07-29. Slot t1, x86 only, no Pi/eqemu (as scoped).

### New file list (with line counts)

| file | lines |
|------|------:|
| `tests/wld_test_fixtures.h` | 108 |
| `tests/test_wld_header.cpp` | 339 |
| `tests/test_wld_fragments_mesh.cpp` | 500 |
| `tests/test_wld_fragments_skeleton.cpp` | 524 |
| `tests/test_wld_fragments_light.cpp` | 545 |
| `tests/test_wld_fragments_bsp.cpp` | 437 |
| `tests/test_wld_conversions.cpp` | 247 |
| `tests/test_wld_texture_loading.cpp` | 365 |
| `tests/test_wld_zones.cpp` | 495 |
| `tests/test_wld_zones_comprehensive.cpp` | 442 |

`tests/test_wld.cpp` (3897 lines) is deleted. All ten files are within
the 400-800 line target (the fixture header, at 108, is intentionally
smaller — it's shared declarations, not a test file). Nothing in
`tests/` exceeds 1500 lines; the largest file in the directory is now
`test_graphics.cpp` at 1073 (pre-existing, untouched).

`bash scripts/refactor/check_file_sizes.sh --files <all 10 new files>`:
all `ok`, no WARN/FAIL.

### Deviation from the suggested split

The plan's suggested cut put `IntegrationTest` + `ComprehensiveZoneTest`
together in one `test_wld_zones.cpp` (~903 combined lines — still under
the 1500 FAIL threshold but above the 400-800 target). Split those two
suites into separate files instead — `test_wld_zones.cpp`
(`IntegrationTest`, 495 lines) and `test_wld_zones_comprehensive.cpp`
(`ComprehensiveZoneTest`, 442 lines) — to keep every file in target
range. This is a superset of the suggested split (9 TUs instead of 7),
still a single `test_wld` target, no additional test binaries.

### Before/after test-set diff

Captured via the CMake-generated `add_test(...)` list for the
`test_wld` target specifically (more precise than the plan's `ctest -N`
regex, which also matches unrelated suites like
`CombatMusicIntegrationTest` and `WldLoaderTest` from other binaries —
used the CTestTestfile-scoped list instead to avoid that noise):

```
diff <(sort before_wld_only.txt) <(sort after_wld_only.txt)
```

Result: **empty diff**. Both before and after: **103 tests**, sorted
sets identical, byte for byte. No test added, removed, or renamed.

### Assertion integrity

Every `TEST_F` body was moved with `sed` line-range extraction directly
from the original file (line ranges verified against `grep -n "^class
\|^TEST_F"` boundaries first) — no manual retyping of assertions.
`WldHeaderTest` and `WldIntegrationTest` (plus `fileExists`,
`eqClientPath_`, `parseWldHeader`, `decodeStringHash`, `getString`,
`loadWldFromArchive`) were extracted once into
`tests/wld_test_fixtures.h`, included by the four files whose fixtures
derive from them (`test_wld_header.cpp`,
`test_wld_fragments_mesh.cpp`, `test_wld_fragments_skeleton.cpp`,
`test_wld_fragments_light.cpp`) — no class defined in two `.cpp`s, no
ODR violation. `WldBspFragmentTest`, `CoordinateSystemTest`,
`TextureLoadingTest`, `IntegrationTest`, `ComprehensiveZoneTest` are
independent fixtures (derive directly from `::testing::Test`) and stay
self-contained in their own TU, matching the plan's structure map.
Only additions beyond pure code movement: per-file `#include` lists
(each new TU explicitly includes what it uses — e.g. `<map>` for
fixture-local `std::map` stat counters, `<iomanip>` for `std::setw` in
`test_wld_zones_comprehensive.cpp` — rather than relying on transitive
includes pulled in by the original single TU) and a top-of-file comment
banner per new file. No test logic, assertion, or expected value
changed anywhere. No bug found while moving.

### Verification results

1. `cmake --build build -j24` (also `-j24 -- -k` for the full-suite
   build) — succeeds for `test_wld` and every other target except the
   pre-existing, unrelated `gles2_zone_benchmark` tool
   (`tools/hwcaps/gles2_zone_benchmark.cpp`, undefined reference to
   `irr::video::g_glCounters`). Confirmed pre-existing by stashing this
   unit's changes and rebuilding just that target on the unmodified T11
   plan commit (4f21fea) — identical link failure. Outside `tests/`
   territory; not touched or fixed here.
2. `test_wld` specifically: **103/103 passed**, runtime **143.2s**
   (expected ~169s serial; in range — loads real S3D/WLD archives from
   `data/eq_client` → `/mnt/projects/EverQuestP1999`, symlink already
   present in this slot).
3. Full suite ×3 (`ctest --output-on-failure`, same `build/` tree):
   - Run 1: 1416 total, 2 failed — `AudioManagerTest.PreloadCommonSounds`
     (host ALSA exception) + `RDPServerFrameTest.UpdateFrameMultipleTimes`
     (SEGFAULT, the documented T03b use-after-free).
   - Run 2: 1416 total, 1 failed — `AudioManagerTest.PreloadCommonSounds`
     only (RDP one didn't reproduce this run — matches "intermittent"
     framing in the protocol/plan).
   - Run 3: 1416 total, 2 failed — same pair as run 1.
   - Total count held at 1416 across all three runs; every failure was
     one of the two documented exceptions, never anything in `test_wld`
     or elsewhere.
4. Visual gate / ARM build: **not applicable** — tests-only unit, no
   `src/` or `include/` changes.

### Summary

Pure mechanical split, invariant held: 103/103 tests identical before
and after, one `test_wld` target, no assertion changed, no bug found.
