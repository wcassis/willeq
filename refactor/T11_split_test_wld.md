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

Per the protocol, plus:
- The new file list with line counts.
- The sorted before/after test-set diff.
- Confirmation that no assertion was changed — the diff should read as
  pure movement plus the fixture-header extraction.
