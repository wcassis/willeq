# J20 — split `tests/CMakeLists.txt` into per-domain fragments

Batch J unit. Contract: `refactor/J_file_size_reclamation.md`, section
"J20 — split `tests/CMakeLists.txt`".

Branch `task/J20-tests-cmake-split`, forked from `fable` at `53d819bd`, then
**rebased onto `f215651a`** (J07 merged mid-unit). J07 touched the **root**
`CMakeLists.txt`, not `tests/CMakeLists.txt`, so the rebase was conflict-free.
Re-verified on the rebased tip: build **exit 0**, `ctest -N` still **3,499**
and `cmp`-identical to the pre-change baseline, `ctest -j6`
**3,498 / 3,499** with the same single documented failure.

## What was done

`tests/CMakeLists.txt` (2,850 lines — 150 from the 3,000-line hard cap) was
carved into **eighteen** per-domain fragments under `tests/cmake/`, included
from the top-level file, which keeps its preamble and becomes a list of
`INCLUDE()` lines.

Every fragment holds whole top-level blocks moved **verbatim**. No target was
renamed, no source list edited, no link library changed, no test added or
removed, and no drive-by dedup performed.

### Resulting line counts

| file | lines |
|---|---|
| `tests/CMakeLists.txt` (was **2,850**) | **135** |
| `tests/cmake/core_utils.cmake` | 88 |
| `tests/cmake/network.cmake` | 92 |
| `tests/cmake/midi.cmake` | 48 |
| `tests/cmake/state.cmake` | 58 |
| `tests/cmake/commands.cmake` | 532 |
| `tests/cmake/spells.cmake` | 90 |
| `tests/cmake/world_geometry.cmake` | 76 |
| `tests/cmake/data_formats.cmake` | 79 |
| `tests/cmake/database.cmake` | 75 |
| `tests/cmake/archives.cmake` | 41 |
| `tests/cmake/graphics.cmake` | 156 |
| `tests/cmake/zone_integration.cmake` | 182 |
| `tests/cmake/action_handlers.cmake` | 265 |
| `tests/cmake/ui_rendering.cmake` | 261 |
| `tests/cmake/renderer_guards.cmake` | 256 |
| `tests/cmake/audio.cmake` | 249 |
| `tests/cmake/eqe1.cmake` | 89 |
| `tests/cmake/packets_skills.cmake` | 202 |

Largest fragment 532 (`commands.cmake`); top-level 135, well under the 1,500
soft limit. 166 `ADD_EXECUTABLE` blocks distributed; the top-level file now
contains none.

`check_file_sizes.sh --files` over all 19 files: **19 ok, 0 warn, 0 fail**.
All new files are mode 644.

## Why INCLUDE and not ADD_SUBDIRECTORY

`INCLUDE()` executes the fragment in the **same directory scope**, so the
preamble's `INCLUDE_DIRECTORIES()`, `ADD_DEFINITIONS`-equivalents, the
`WILLEQ_REPO_ROOT` define, `FMT_LIB`, the SQLite flags and — critically —
`SET(CMAKE_GTEST_DISCOVER_TESTS_DISCOVERY_MODE PRE_TEST)` (T163) all remain in
effect for every target, with `CMAKE_CURRENT_SOURCE_DIR` still `tests/`.
`ADD_SUBDIRECTORY()` would have created a new scope and new binary dirs and
changed both relative source paths and the ctest working directory. The
T163 `PRE_TEST` block and the T36 `WILLEQ_REPO_ROOT` block were left in the
top-level file untouched and unsummarised, as the plan required.

No use of `CMAKE_CURRENT_LIST_DIR` / `CMAKE_CURRENT_LIST_FILE` exists anywhere
in the file, so nothing depends on the physical file location.

## Ordering is load-bearing

Fragment include order is the original file order. Three fragments set a
variable their own targets consume, and each variable's whole set→use span
falls inside one fragment (checked before choosing the cut points):

| variable | set | used | fragment |
|---|---|---|---|
| `ACTION_LAYER_STATE_SOURCES` | 373 | 394–609 | `commands` (369–895) |
| `TEST_PATHFINDER_SOURCES` / `_LIBS` | 1033/1038 | 1041–1049 | `world_geometry` (981–1051) |
| `AUDIO_COMMON_TEST_SOURCES` / `_LIBS` | 2327/2344 | 2349–2567 | `audio` (2327–2570) |

`FMT_LIB` is set in the preamble and used throughout — unchanged.

This is recorded in a comment block in `tests/CMakeLists.txt`, together with
the instruction that new test targets go in the domain fragment, not the
top-level file.

## Proof 1 — the fragments are verbatim

Concatenating the top-level preamble (lines 1–102 of the original) with each
fragment's body (skipping its 6-line provenance header), in include order,
and diffing against the original ignoring blank lines:

```
diff orig_nonblank recon_nonblank   -> exit 0, 2,654 == 2,654 lines
```

Byte-identical. The only content lost anywhere is trailing blank lines at
fragment ends.

## Proof 2 — the `ctest -N` set is identical

`ctest -N` captured on the same build tree before the change and after a
rebuild:

```
before: Total Tests: 3499
after:  Total Tests: 3499
```

Sorted name lists compared with `cmp`:

- 3,491 of the 3,499 names are **byte-identical** (`cmp` exit 0 on the
  filtered lists, 3,491 == 3,491).
- The remaining 8 differ, and they are **not** a J20 effect — see below.
  After redacting the volatile substring, `cmp` on the full sorted lists is
  **exit 0 (3,499 == 3,499)**, and `cmp` on the lists in **ctest order**
  (unsorted) is also **exit 0**, so nothing was reordered either.

`cmake --build build --target help`, sorted: `cmp` **exit 0**, 1,235 == 1,235
lines. No target added, removed or renamed.

### The 8 nondeterministic names — a pre-existing trap, worth recording

`test_irrlicht_camera_command` has a `TEST_P` suite
(`CameraCommandRoundTripTest.SetThenReadRoundTrips`, instantiated as
`ExactGetterStrings/` and `CompactAliases/`) whose parameter type has no
`PrintTo`/`operator<<`. GoogleTest therefore names each case by **hex-dumping
the parameter object's bytes**, and those bytes include a pointer:

```
.../72-byte object <30-E9 86-9F BB-64 00-00 04-00 ...>
```

The pointer is re-randomised on every process start. Running the unmodified
binary's `--gtest_list_tests` twice in a row produces **16 differing lines out
of 8 names**:

```
build/bin/test_irrlicht_camera_command --gtest_list_tests | grep 72-byte | sort  (run 1)
build/bin/test_irrlicht_camera_command --gtest_list_tests | grep 72-byte | sort  (run 2)
diff -> 16 changed lines
```

So these 8 ctest test **names** are not stable identifiers at all: they change
whenever discovery is re-run, which happens whenever the binary is relinked.
`ctest -R` cannot reliably select them. This is a real (small) defect in
`tests/test_irrlicht_camera_command.cpp` — it wants a `PrintTo` or a name
generator — but it is **out of J20's scope** (no behaviour change, no test
edits) and is left for the board.

⚠ **Note for later Batch J units and anyone diffing `ctest -N`:** these 8
lines will differ on any run that relinks that binary. Redact
`72-byte object <...>` before comparing, or the diff reads like a change you
did not make.

Second trap found while chasing this: **two consecutive `ctest -N` runs on the
same tree agree**, because with `DISCOVERY_MODE PRE_TEST` ctest reuses the
generated `<target>[1]_tests.cmake` file unless the binary is newer. A
"`ctest -N` is stable" check is therefore vacuous; test the binary directly.

## Proof 3 — a genuinely from-scratch configure + build

A separate tree was configured from nothing with the same options
(`EQT_GRAPHICS=ON EQT_GLES2=ON EQT_DRM=OFF EQT_BUILD_TESTS=ON`), built with
`-j24`, and its `ctest -N` compared (redacted, sorted) against the *before*
baseline:

```
cmake -S . -B build-j20scratch ...      exit 0
cmake --build build-j20scratch -j24     exit 0
ctest --test-dir build-j20scratch -N    exit 0, Total Tests: 3499
cmp before_redacted scratch_redacted    exit 0
```

The T163 discovery path is unaffected by the split. The scratch tree was
deleted afterwards (20 GB; the volume is at 93%).

## Verification

| step | result |
|---|---|
| `cmake --build build -j24` (before, baseline) | **exit 0** |
| `cmake --build build -j24` (after) | **exit 0** |
| `ctest --test-dir build -j6 --output-on-failure` | **3,498 / 3,499**; sole failure `AudioManagerTest.PreloadCommonSounds` — the one documented baseline failure. Zero new failures. |
| `ctest -N` set equality | **proven** (above) |
| target-list equality | **`cmp` exit 0**, 1,235 lines |
| from-scratch configure + build + `ctest -N` | **exit 0 / 0 / identical** |
| `bash scripts/build-gles2-x86.sh` | **exit 0** |
| `bash scripts/ci/visual_gate.sh` | **PASS**, zero image mismatches, CPU p95 4.72 / 5.08 / 5.06 ms against ceilings 10.11 / 10.67 / 10.95. **Goldens untouched** (`git status` shows only `tests/CMakeLists.txt`, `tests/cmake/` and this file) |
| `bash scripts/build-arm-noble.sh` | **exit 0** |
| `check_file_sizes.sh --files` (19 files) | **19 ok, 0 warn, 0 fail** |

`verify_move_only.py` does not apply (CMake is not a C++ TU); Proof 1 is its
stand-in and is stronger — a literal line-for-line reconstruction.

## Notes on how targets are defined (for later J units)

- **CMake commands in this file are UPPERCASE.** `grep -n add_executable`
  returns nothing, silently. Always `grep -i`.
- 166 `ADD_EXECUTABLE` blocks. Several are wrapped in a top-level
  `IF(WITH_AUDIO)` / `IF(EQT_GRAPHICS)` / `IF(EQT_LIVE_TESTS)` guard, so the
  block boundary is the `IF(` line, not the `ADD_EXECUTABLE(` line — cutting
  at target lines would have split an `IF`/`ENDIF` pair. Cut points here were
  computed as lines where **both** paren depth and IF-nesting are zero, then
  walked back over the leading comment block.
- Many targets compile production sources directly instead of linking
  `willeq_core`. Every source list was moved character-for-character; none was
  touched.
- The two biggest single reasons the file grew are the per-target
  `IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY) TARGET_INCLUDE_DIRECTORIES(...)`
  stanza repeated ~60 times, and the long provenance comments above each
  target. Both were preserved verbatim; deduplicating the former is a real
  ~120-line saving but is a behaviour-adjacent edit and explicitly out of
  scope here.

## A process trap worth boarding: the session scratchpad is SHARED across slots

`/tmp/claude-1000/-mnt-projects-fable/<session>/scratchpad/` is **not per-slot**.
Three executors were writing into it simultaneously during this unit (J07's
`j07_build.log` and this unit's logs are minutes apart in the same directory),
and it also holds exit files left by *earlier* sessions on the same day.

That breaks the standard `--exit-file` pattern in a way that reads as success:
`scripts/wait_for.sh --exit-file .../gles2.exit` returned **MET, exit 0, after
0s** against a file written at **01:56** by an unrelated run, while this unit's
`build-gles2-x86.sh` was still compiling. The same happened with `arm.exit`
(02:01). Both were caught only because a 0-second "completion" for a
cross-compile is implausible; a 40-second one nearly was not — the visual gate
was launched on top of a still-running GLES2 build as a result, and had to be
re-run.

**Rule: `rm -f` the exit file immediately before launching, or write into a
unit-private subdirectory (`scratchpad/<UNIT>/`), and sanity-check the elapsed
time and the log's mtime before believing an exit code.** Generic names like
`build.log` / `gate.exit` are guaranteed collisions in a three-slot wave.

## Deviations

None from the plan's scope. Two additions beyond it: the nondeterministic
test-name finding above, which the plan's central proof would otherwise have
appeared to fail against, and the shared-scratchpad trap.

The visual gate was run twice — the first run overlapped a still-running
`build-gles2-x86.sh` because of the stale-exit-file trap. The result reported
here is the second, clean, non-overlapping run.
