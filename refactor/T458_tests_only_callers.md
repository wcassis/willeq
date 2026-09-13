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

## Review

Resumed 2026-09-13 in **`wt/slot2`** (the pause was taken in slot1).
`task/T458-tests-only-callers` was at `f7d35cd7` (the Review-only pause
commit, verified) and was rebased onto `fable` **`9d646a5a`** with no conflict.
`CMAKE_BUILD_TYPE=RelWithDebInfo` and a slot-local `EQT_GLES2_IRRLICHT_DIR`
were checked before any work. Board range: **B600-B607** (dispatch
B600-B619; the plan's "B504+" predates the renumbering).

### What shipped

1. **36 census rows deleted, 46 overloads, with the 64 test cases that only
   exercised them** -- the 31-row set the pause proved, re-proven on the
   rebased tree, plus the five rows its deletion exposed (B503).
2. **`ZeroCallers` ratchets the tests-only bucket too.** `--check` now fails on
   a tests-only function missing from `EXEMPT_TESTS_GROUPS` and on a stale
   entry there, beside the existing zero-caller half. 291 rows classified:
   **36 `seam`, 145 `observer`, 110 boarded as B600-B606**. `--tests-only`
   selects that half alone. The ctest line did not change.
3. **A census rule fix** -- the plan's STOP shape ("reached through something
   the census does not model: fix the rule"), found by the triage (B607).
4. **Board B600-B607**, doc strikes, `DocClaims` retired-name rows.

### The census, re-taken

The tree moved (T456-T465) since the pause, so nothing from the pause was
applied without re-measuring.

| | pause (`a7e94739`) | rebased (`9d646a5a`) | + rule fix | after round 1 | **after round 2 (fixed point)** |
|---|---|---|---|---|---|
| functions declared (`include/`+`src/`) | 6,651 | 6,700 | 6,700 | 6,669 | **6,664** |
| public/protected non-virtual candidates | 4,430 | 4,475 | 4,475 | 4,444 | **4,439** |
| **zero callers** | 417 | 416 | 415 | 416 | **415** |
| **reached only from `tests/`** | 322 | 323 | 322 | 295 | **291** |
| reached only from `tools/`+`benchmarks/` | 36 | 36 | 36 | 36 | **36** |

Rebased vs pause: `CameraController::getActualFollowDistance` left the zero
bucket (T459 deleted it), `TextBatch::getQuadCount` left the tests bucket, and
`BoidsManager::getCreatureMaterial` (T463's eviction test seam) and
`EntityRenderer::loadEquipmentModels` (T456) entered it.

**Every one of the 31 rows was re-proven on the rebased tree before any
deletion**: still in the tests-only bucket, and a `grep -rnw` over `src/
include/ tools/ benchmarks/ docker/` showed only the definition, a comment or a
string literal (`"Escape"` is a key name in `hotkey_manager.cpp`, `"split"` a
command name, `Split plane normal` a comment). **No production caller had
appeared; none left the set.** None of the rows sits in T466's or T467's
territory, so **no row was deferred**.

### The rule the census was missing (B607)

Triaging the residue found `SerializeUiPreferences` classed tests-only while
`src/client/graphics/ui/ui_preferences.cpp:350` calls it:
`outFile << SerializeUiPreferences(p);`. `classify_occurrence` accepts a lead
of type characters as a definition, and `<`, `>` and spaces are all type
characters -- so a call after a stream operator scored as a **definition**, the
direction T455 said the census must never get wrong. Two rules added: a
**whitespace-bounded** `<<`/`>>` in the lead is a use, and so is a lead with no
identifier at all (`    , f(x)` continuing an argument list).

Re-measuring every bucket after the change moved exactly two rows:
`SerializeUiPreferences` out of tests-only, and **`getCreatureTypeName` out of
the zero bucket -- a T455 `EXEMPT` row boarded under B490 while
`flock_controller.cpp:530` calls it** (`ss << getCreatureTypeName(...)`); its
stale exemption was removed. **The first cut matched a bare `>>` and hid four
genuinely dead functions** whose return types close a nested template
(`InventoryManager::getBankItems/getSharedBankItems/getSellableItems`,
`SkyLoader::getCelestialBodiesForSkyType`: grep shows only their definitions).
That was caught only because the whole census was diffed after the edit; the
whitespace bound fixed it and they are back in the zero bucket.

### Deletions

**Round 1 -- the 31 proven rows (40 overloads):**

| where | rows | removed |
|---|---|---|
| `include/common/util/strings.h` + `src/common/util/strings.cpp` | 12 | `Contains` (2), `ToInt`, `ToFloat`, `ToUpper`, `Escape`, `Join` (2), `NumberToWords`, `Repeat`, `Replace`, `Split` (2), `BeginsWith`, `EndsWith` |
| `include/common/util/random.h` | 2 | `Roll(int)`, `Roll(double)`, `Roll0` |
| `include/client/position.h` + `src/client/position.cpp` | 7 | `IsWithinAxisAlignedBox` (2), `DistanceNoZ` (2), `IsPositionEqual` (3), `IsPositionWithinSimpleCylinder` (2), `CalculateHeadingAngleBetweenPositions`, `IsWithinCircularArc`, `IsWithinSquare` |
| `include/common/packets/gm.h` | 3 | `EQT::Position::ExtractCoord`, `ExtractHeading`, `ExtractAnimation` (the whole `Position` namespace) |
| `include/client/audio/water_sounds.h`, **`src/client/audio/water_sounds.cpp` (file deleted)**, its `tests/cmake/audio.cmake` line | 7 | the whole `class WaterSounds`; `WaterState` and `WaterSoundIds` stay (`WaterIn` is live) |

**Round 2 -- the B503 cascade the round-1 census reported (5 rows, 6 overloads):**

| row | why it died |
|---|---|
| `Strings::ConvertToDigit` (+ the `NUM_TO_ENGLISH_X/Y` tables) | its only caller was `NumberToWords` -- predicted at the pause |
| `Random::Real` (+ `real_dist`, `real_param_t`) | its only caller was `Roll(double)` -- predicted |
| `Strings::IsNumber`, `Strings::IsFloat` | their only callers were `ToInt` / `ToFloat` -- **not** predicted |
| `EQ::ValueWithin` (2 overloads, `include/common/util/data_verification.h`) | **not** predicted: it looked live only because `position.cpp` declared its OWN local `ValueWithin` template (used by `IsWithinAxisAlignedBox`), and the census matches by name. The local template, `position_eps` and `M_PI` in `position.cpp` went with it (orphans, not census rows) |

A third census pass produced **no new row**: `--check` exit 0. Fixed point.

**Test cases deleted -- 64, suite 4,794 -> 4,730 expected:**

| binary | before -> after | cases |
|---|---|---|
| `test_strings` | 22 -> 2 | `StringsTest.` BeginsWith_PositiveMatch, BeginsWith_NegativeMatch, EndsWith_PositiveMatch, EndsWith_NegativeMatch, Contains_PositiveMatch, Contains_NegativeMatch, ToUpper, Split, Split_EmptyString, Split_NoDelimiter, Join, Join_Empty, ToInt, ToFloat, Replace, Repeat, Escape, NumberToWords, IsNumber, IsFloat |
| `test_data_verification` | 17 -> 6 | `DataVerificationTest.` ValueWithin_Int_InRange, ValueWithin_Int_OutOfRange, ValueWithin_Int_NegativeRange, ValueWithin_Float_InRange, ValueWithin_Float_OutOfRange, IsValidPosition, IsValidHeading, IsValidHP, EdgeCase_ZeroRange, EdgeCase_MaxInt, EdgeCase_InfinityFloat |
| `test_position` | 26 -> 13 | `PositionTest.` DistanceNoZ_IgnoresZ, CalculateHeading_East, CalculateHeading_North, CalculateHeading_SamePosition, IsPositionEqual_SamePosition, IsPositionEqual_ClosePositions, IsPositionEqual_DifferentPositions, IsWithinCircularArc_Test, IsWithinSquare_Inside, IsWithinAxisAlignedBox_Inside, IsWithinAxisAlignedBox_Outside, IsPositionWithinSimpleCylinder_Inside, IsPositionWithinSimpleCylinder_Outside |
| `test_random` | 21 -> 12 | `RandomTest.` Roll_AlwaysPass, Roll_NeverPass, Roll_FiftyPercent, Roll_Double_AlwaysPass, Roll_Double_NeverPass, Roll0_InRange, Roll0_Zero, Real_InRange, Real_CustomRange |
| `test_weather_audio` | 27 -> 20 | `WaterSoundsTest.` EntrySoundFile, TreadSoundFiles, UnderwaterLoopFile, EntrySoundId, TreadSoundIds, UnderwaterLoopId, TreadSoundCount |
| `test_packet_structs` | 44 -> 40 | `PacketStructsTest.` PositionHelpers_ExtractCoord, PositionHelpers_ExtractNegativeCoord, PositionHelpers_ExtractHeading, PositionHelpers_ExtractAnimation |

Source diff: 1,162 lines out, 584 in, and every insertion is in a script, a doc, the
board or a replacement comment -- **no production source gained a line of
logic**. The client binary loses no linked symbol it used: `water_sounds.cpp`
was in no client target, and every other deleted function had no production
caller.

### The residue: seam / observer / boarded

The pause's per-owner triage was re-taken on the rebased tree
(`review/T458/owner_triage.tsv` is the pause's; the rebased equivalent is
regenerated by `check_zero_callers.py --bucket all`). Each of the 291 rows is
listed individually in `EXEMPT_TESTS_GROUPS`; the table was generated from a
hand classification and validated (every census row classified exactly once,
nothing classified that the census does not report) before it was written.

| category | rows | rule |
|---|---|---|
| `seam` | 36 | a hook on a production class that exists so a test can drive or observe private state: every `Test_*` / `*ForTest` / `*ForSeam`, `LoginWorldComponent::ReplayLoginPacket/ReplayWorldPacket` (the session-replay injectors), `PacketSender::ReplaySuppressedSendCount`, the debug-draw seed/count pairs (`FrameComponent`, `OverlaysComponent`, `PlayerCameraComponent`), `RaceModelLoader::variantLoadTrace` (T340's documented seam), `RenderBarrier::inRender` ("diagnostic only (tests)"), and the three `*EqStrIds()` lists and `ComputeHelpCoverage` (declared "for the db census test") |
| `observer` | 145 | a const accessor on a class with live production callers -- the test's window onto the reached path (`CombatState::targetHPPercent`, `SoundBuffer::getDuration`, the 12 `LoginWorldComponent` getters). **A class-level argument, not a per-row proof**: an observer can read a field only a boarded mutator writes (B601), and `WriteOnlyFields` is the net for that |
| boarded | 110 | B600 audio layer never constructed (28), B601 GameState mutators (20), B602 EntityManager API (11), B603 gameplay-manager control API (24), B604 `handleInterrupt` (1), B605 input/command API (8), B606 renderer/asset/data API (18) |

**No test-support helper moved under `tests/`.** Every candidate is a seam that
needs the private state it exposes (the plan's third option did not apply to
any row).

Two findings from the triage are worth reading on the board rather than here:
**B600** -- `CreatureSounds`, `UISounds`, `PlayerSounds` and `WeatherAudio` have
`.cpp` files in no client target and `CombatMusicManager` is compiled in but
constructed nowhere, which `src/client/audio/CLAUDE.md` described as live
sound categories (corrected); and **B604** -- `HC_OP_InterruptCast` is routed to
a log line and `SpellManager::handleInterrupt()` has only test callers
(class A lead, not confirmed live).

### Territory

No delete-set row was in T466's or T467's files, so **nothing was deferred**.
Rows beside that territory were classified in the script only, with no source
edit: `OverlaysComponent` x4 (`seam`, `overlays/` is T467's),
`HotkeyManager::isMovementKey/modeNameToEnum` and `HotkeyBinding::toString`
(B605; T466 owns `hotkey_manager.cpp`), and the environment managers'
accessors (`observer`). `SerializeUiPreferences` (T467's `ui_preferences.*`)
left the tests bucket through the rule fix and needed nothing. **Merge note:**
T466/T467 can move rows between buckets; a `ZeroCallers` NEW/STALE line at
their merge is the table doing its job, not a defect in this one.

**Rebased mid-verification onto T467's merge (coordinator order).** T467
merged (`50e7efb3`, then T468's plan `e4ca9374`) while this unit was verifying.
The branch was committed (`b3a8ddf6`) and rebased onto `e4ca9374`; the one
conflict was `refactor/BOARD.md`, append-vs-append (T467's B590 section against
this unit's B600-B607) and both sides were kept, each under its own
"Added 2026-09-13" header. **The census re-taken on the rebased tree is
unchanged -- 415 / 291 / 36, `--check` exit 0** (T467's four new declarations
land in no bucket), so no row T467 moved (`overlays_env_managers.cpp`, the
environment-manager construction) needed reclassifying. Everything in *Proof*
below marked "post-rebase" was re-run on the rebased tip.

**Deferred to T468** (the coordinator's territory update: `loadGlobalAssets()`,
`ensureDoorTextureGuard()`, `DoorManager`, the zone-load door step and
`setupInstantScene()`, board B590). **No row in that area was deleted or
edited by this unit** -- none was in the delete set. The census rows there are
left exactly as they were for T468 to decide, whichever way it resolves B590:

| row | bucket | where it is recorded |
|---|---|---|
| `IrrlichtRenderer::loadGlobalAssets` | zero | `EXEMPT_GROUPS`, B494 (T455's row, unchanged) |
| `DoorManager::isDoorMeshBuilt` | zero | `EXEMPT_GROUPS`, B494 (unchanged) |
| `DoorManager::rebuildPlaceholderDoors` | zero | `EXEMPT_GROUPS`, B494 (unchanged) |
| `DoorManager::recomputeAllBspRegions` | zero | `EXEMPT_GROUPS`, B494 (unchanged) |
| `DoorManager::hasDoor` | tests-only | `EXEMPT_TESTS_GROUPS`, `observer` |
| `EntityRenderer::loadEquipmentModels`, `EntityRenderer::loadGlobalCharacters` | tests-only | `EXEMPT_TESTS_GROUPS`, B606 -- listed here because B606's text cites `loadGlobalAssets()`; if T468 makes `loadGlobalAssets()` the single construction path and it calls these, they leave the tests bucket and the table will say so (STALE) |

`ensureDoorTextureGuard()` and `setupInstantScene()` are in no census bucket
(both have production callers), so there is no row to defer for them.

### Proof

| step | result |
|---|---|
| checkout / rebase | tip `f7d35cd7` verified; `git rebase fable` (`9d646a5a`) clean |
| baseline build of the rebased tree (background, exit file) | **exit 0**; `ctest -N` **4,794** |
| **`ZeroCallers` tests-only half RED on the pre-fix tree** | **exit 1** -- `tests-only: 31 unlisted, 0 stale`, naming exactly the 31 delete-set rows; zero half green (415). Taken on the real undeleted tree after the tables were written, so no patch revert was needed |
| round 1 / round 2 census | round 1: 5 new rows (the cascade), 0 stale; round 2: **`--check` exit 0** -- `zero-caller census clean: 415 ... tests-only census clean: 291 (boarded 110, observer 145, seam 36)` |
| `DocClaims` | first run **FAIL** on B607's own tombstone of `Strings::IsNumber`; marked `RETIRED-OK`; **exit 0** (54 retired names, 11 exempted) |
| `DocCounts` / `DocCitations` / `CommentClaims` | **exit 0** each; re-run post-rebase, **exit 0** each (DocCitations 1,341 citations, 0 fail) |
| `check_file_sizes.sh fable` | 14 files checked, 0 warn, 0 fail; re-run post-rebase, identical |
| build after deletions, pre-rebase | first attempt **exit 2** -- deviation 4 (my concurrent `build-gles2-x86.sh` deleted the Irrlicht archive it was linking); re-run after the Irrlicht rebuild **exit 0**; `build/bin` ELF scan 354 files, 0 bad |
| `ctest -j6`, pre-rebase | **4,730 / 4,730**, exit 0 (4,794 - 64, exactly). Per-binary: `test_strings` 2, `test_random` 12, `test_position` 13, `test_packet_structs` 40, `test_weather_audio` 20, `test_data_verification` 6 |
| `scripts/build-arm-noble.sh`, pre-rebase | **exit 0** |
| census post-rebase | unchanged, **`--check` exit 0** (415 / 291 / 36) |
| **build post-rebase** (`26be22f6`) | **exit 0** |
| **`ctest -j6` post-rebase** | **4,739 / 4,739**, exit 0 -- the +9 are T467's (`test_environment_manager_construction` 4, `test_ui_preferences_identity` 5), so (4,794 + 9) - 64. No `visual_gate` load miss |
| **`scripts/build-arm-noble.sh` post-rebase** | **exit 0**, `build-arm-noble/bin/willeq` written 23:04 |
| `scripts/build-gles2-x86.sh` post-rebase | **exit 0** (run after the main build finished, not beside it -- deviation 4) |
| **visual gate standalone post-rebase** | **PASS**, all six verifications; CPU p95 4.955 / 6.533 / 5.978 ms against ceilings 10.108 / 10.672 / 10.948 (host load ~29 from other slots). **No golden moved**: `git status -- data tests` clean |
| G05/G06 goldens | not run: no renderer TU was touched (`renderer_frame_render`, `renderer_player_physics`, `zone_dispatch`, `bridge_component_intents`, `command_processor_registration`, `application` are all outside the diff) |

### Deviations

1. **The census classifier was changed** (the plan's STOP shape, handled by
   its own instruction: fix the rule). Every bucket was re-measured after the
   change and the result is in the table above, including the stale T455
   exemption it exposed and the four rows a first cut hid.
2. **36 rows deleted, not 31**, and **64 test cases, not ~20**: the pause
   predicted two cascade rows and there were five; it also undercounted
   `WaterSoundsTest` (7 cases, not 8) and `PositionHelpers_*` (4, not 3) and
   did not list the `ValueWithin` cases at all.
3. **Board numbers B600-B607**, from the dispatch, not the plan's B504+.
4. **My first `scripts/build-gles2-x86.sh` run was started while the
   verification build was still linking, and it deleted this slot's patched
   Irrlicht out from under it** (`build-gles2-x86.sh` rebuilds
   `third_party/irrlicht-gles2-x86/` on every run, not only with
   `--irrlicht-only`): the build exited **2** with only
   `No rule to make target .../libIrrlicht.a` link failures -- no compiler
   error, no undefined reference -- and the gles2 script itself died on
   `rm: ... Directory not empty`. Recovered by running the gles2 script alone
   (it rebuilds Irrlicht), then the main build. Protocol steps 1 and 3 must run
   in order in one slot; the fact that step 3 deletes step 1's link input is
   worth a line in `EXECUTOR_PROTOCOL.md` (not edited by this unit).
5. **The plan's fourth tests-only category was needed** (`seam`), and a fifth
   distinction (`observer`) replaced the plan's "delete it with its tests" for
   145 rows -- the pause's argument, adopted.
