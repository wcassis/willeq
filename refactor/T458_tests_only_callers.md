# T458 — functions reached only by tests (T455's second list), and the cascade to a fixed point (B503)

Class B, slot1, no server. Read T455's §Review and
`review/T455/tests_only_callers.tsv` (322 functions whose only callers
are under `tests/`), `scripts/refactor/check_zero_callers.py` (the
census, its exclusion rules, the EXEMPT/BOARD_REASONS tables), B503
(the census is one pass; deleting exposes more — iterate).

1. Extend the census with a `--tests-only` mode that reports functions
   with zero production callers (src/ include/ tools/ benchmarks/
   docker/) and ≥1 test caller, and iterate BOTH lists to a fixed
   point after each deletion round (B503).
2. For each tests-only function: it is production code nobody reaches
   — delete it WITH the tests that only exercise it (a test of dead
   code is a false net), or board it as a never-wired feature (the
   T39/B024 rule) with the evidence, and say the split. Test-support
   helpers that live in `src/`/`include/` only for tests move under
   `tests/` instead of being deleted.
3. The `ZeroCallers` net gains the tests-only ratchet (EXEMPT rows per
   boarded item), pre-fix red on the current tree, green after.
4. Docs: strike every deleted symbol; census numbers re-measured;
   G05/G06 deletions only.

## Proof

Build (background), `ctest -j6` (no expected reds; the case count
DROPS by exactly the deleted dead-code tests — list them), visual gate
standalone + ARM (nothing moves), file sizes, doc nets. New items
B504+ (T456/T457 allocate from B479+/B490+ — collisions renumbered at
merge; say which you used).

STOP: an image golden moves; a "tests-only" function turns out reached
through a table or a virtual the census does not model (fix the rule).

## Review (PAUSED)

Started 2026-09-08 in `wt/slot1`, branch `task/T458-tests-only-callers`
cut from `fable` at **a7e94739** (T455's merge, verified at checkout).
`CMAKE_BUILD_TYPE=RelWithDebInfo` and `EQT_GLES2_IRRLICHT_DIR` pointing at
this slot's own Irrlicht were both checked before any work.

**The orchestrator paused the session after the analysis leg and before
the first deletion was applied.** This commit therefore touches **no
source file** — only this plan. The tree at the commit is `fable`'s tip
plus this section, so "builds green, suite green" is inherited from the
merge rather than re-proven here (see *What is proven* below).

### What is done

**The census re-taken on this tip, and it reproduces T455 exactly.**
`scripts/refactor/check_zero_callers.py --bucket all` over the branch tip:

| bucket | rows |
|---|---|
| declared functions under `include/`+`src/` | 6,651 (4,430 public/protected non-virtual candidates, 3,943 distinct names) |
| files swept (`src/ include/ tests/ tools/ benchmarks/ docker/`) | 1,212 |
| **zero callers anywhere** | **417** |
| **reached only from `tests/`** | **322** |
| reached only from `tools/`+`benchmarks/` | 36 |

That is **iteration 0**, and it is the only iteration taken: no deletion
was applied, so there is no fixed point to report yet.

**A second measurement the plan did not ask for, and which the deletion
round should be driven from: the per-OWNER live/tests split**
(`review/T458/owner_triage.tsv`, 84 owning classes). For every class with
at least one tests-only function it counts how many of its
public/protected non-virtual functions have a **live production caller**.

The reason it matters is that the plan's per-row rule ("delete the dead
function with its tests — a test of dead code is a false net") is not
safe applied row by row. A tests-only *getter* on a class whose other
methods are live is usually the test's **observation window on a reached
code path**: `MusicPlayer::isPaused()`, `SoundBuffer::getDuration()`,
`CombatState::targetHPPercent()`. Deleting those does not remove a false
net — it removes a true one and leaves the live path unpinned. The rule
bites cleanly only where the OWNER has no live caller either. Three
classes are in that position (`WaterSounds` with 7 rows,
`ConsoleRenderer::setStatusUpdateInterval`, `WhoAllEntry::isLinkdead`),
and the rest of the 322 need the delete/board/seam split argued per
group, not per row.

A third category the plan's three-way split does not name, and which the
EXEMPT table will need: roughly twenty rows are **deliberate test seams**
on production classes — `ZoneComponent::Test_BeginGracefulZoneOutForSeam`,
`PacketSender::Test_SetBypassSendGuardForSeam`,
`EquipmentModelLoader::debugInjectCachedMeshForTest`,
`FrameComponent::seedFrustumDebugBoxForTest`, the `Test_GetLast*`
observers. These cannot "move under `tests/`" (they need the private
state they expose) and they are neither dead nor a never-wired feature.
They want their own category (`seam`), not a board entry.

**A delete set of 31 census rows / 38 overloads, hand re-grepped and
proven, NOT applied.** Every row was re-grepped `-w` across all six
directories independently of the script (board B166's rule). The full
table with the evidence is in `review/T458/README.md`; in summary it is
the remainder of the pre-fable eqemu utility layer T455 deleted 32 rows
of — twelve `Strings::` helpers whose only production occurrence is their
own out-of-line definition, `Random::Roll`/`Roll0`, the seven remaining
`position.h` free functions, `gm.h`'s three GM-position bitfield
extractors — plus the whole of `class WaterSounds`, whose seven methods
have no production caller at all (the one water sound the client plays
reaches `WaterSoundIds::WaterIn`, a constant, from `movement_state.cpp`).

One **B503 cascade** was identified in advance rather than discovered by
re-running: `Random::Real()` is reached only from `Roll(double)` inside
`random.h`, so it dies with the two `Roll` overloads and a second census
pass would report it.

### What is proven

| step | result |
|---|---|
| checkout tip = `a7e94739` | verified |
| `CMAKE_BUILD_TYPE` / `EQT_GLES2_IRRLICHT_DIR` | RelWithDebInfo, slot-local Irrlicht |
| baseline build (background, exit file, `wait_for.sh`) | **exit 0** (~36 min wall, host load 28–32 with three slots active) |
| `check_build_tree_orphans.py` over `build/` | **PASS** — 345 binaries, 345 target artifacts, 0 allowlisted. The stale `s3d_index_builder` T455 had to delete is gone; this slot inherited none |
| working tree | clean; `git status --short` empty before the commit |

### What is NOT proven

Everything else, because nothing was changed: no `ctest` run was taken on
this branch, no visual gate, no ARM build, no file-size check, no
`G05`/`G06` golden run, no doc net. None of them can have moved — the
commit is one markdown file — but they were not executed and must not be
read as green from this unit.

### Next step, in order

1. Apply the 31-row delete set from `review/T458/README.md` **with** the
   test cases that only exercise it: `WaterSoundsTest.*` (8 cases,
   `tests/test_weather_audio.cpp`), `PacketStructsTest.PositionHelpers_*`
   (3 cases, `tests/test_packet_structs.cpp`), and the `Strings` /
   `Random` / `position.h` cases. Drop `src/client/audio/water_sounds.cpp`
   from `tests/cmake/audio.cmake` and from the client target. Record each
   deleted case by name and the ctest count before/after.
2. Re-run the census; expect `Random::Real` and any `Strings` helper whose
   only caller was a deleted sibling (`ConvertToDigit` is the candidate,
   reached from `NumberToWords`) to appear. Iterate to a fixed point
   (B503).
3. Extend `check_zero_callers.py` with the `--tests-only` ratchet:
   `EXEMPT_TESTS` beside `EXEMPT_GROUPS`, with categories `boarded`,
   `seam` (the ~20 `Test_*`/`*ForSeam` rows) and `observer` (a tests-only
   accessor on a class with live callers — the observation window). Wire
   it into the `ZeroCallers` ctest test. Prove it RED on the pre-fix tree
   via an exported patch + `git apply -R` (**never `git stash`** — T306)
   and green after.
4. Board the residue as **B504+** (T456 allocates from B479+, T457 from
   B490+; collisions are renumbered at merge), one entry per subsystem,
   in the shape T455 used for B490–B502.
5. Docs: strike every deleted symbol; re-measure the census numbers in
   `CLAUDE.md`/`docs/module_map.md`; run the doc nets.
6. The full verification bar: build, `ctest -j6`, visual gate standalone,
   ARM, `check_file_sizes.sh fable`.

### Deviations

1. **Paused mid-unit on the orchestrator's order.** The plan's four steps
   are all unstarted in the tree; step 1's measurement and the delete
   set's evidence are complete and recorded here and in `review/T458/`.
2. **No board entries were allocated.** B504+ is still free from this
   unit; nothing was written to `refactor/BOARD.md`.
