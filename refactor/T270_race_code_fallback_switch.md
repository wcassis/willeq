# T270 — the pre-JSON race-code fallback switch contradicts the audited rows (T267 finding)

Slot: slot1. x86. NO eqemu. Class A. Territory:
src/client/graphics/eq/race_codes.cpp (`getRaceCode` DB-null fallback,
`getFallbackRaceCode`, `getZoneSpecificRaceCode`) and the 38 tests that pin
the old numbering, tests. Do NOT touch loot (T268 slot3), bank (T269 slot2).

## Finding
When the db lookup returns null, `getRaceCode()` falls back to a hardcoded
switch that predates race_models.json; it disagrees with T265's audited
rows (17 ORC vs GOL, 35 RAT vs EEL, 72 ELE vs SHIP, 13 WOL vs AVI, 46 GOB vs
the row, 127/240 INV vs IVM ...). Inert at runtime while the db loads, but
a silent wrong answer the moment it does not — and 38 tests assert the
wrong numbering.

## Scope — decide by the codebase rule (db is the source of truth)
1. Delete the fallback numbering: with no db row the honest answer is
   "unknown → placeholder", the same path T213 documents. If the loader
   REQUIRES a code for the placeholder path, return a single sentinel.
2. Regenerate the 38 pinned expectations from the shipped db (a table
   test reading willeq.db, like `WilleqDatabaseShippedContentTest`) so
   the tests assert the audited truth, not the old switch. Red/green:
   the old tests fail against the new code for the contradicting ids.
3. Grep for any OTHER hardcoded race→code/archive map (`case 330`,
   `"FRG"` etc. outside data) and list it.

## Verification
Full ctest; visual gate (race code path); Review; evidence review/T270/.
Commit: `T270: race codes come from the db only; pre-JSON fallback deleted`.

## Review (2026-08-26, slot1, off fable `4b113f01`)

Evidence: `/mnt/projects/fable/review/T270/` (README.md indexes every file).

### Scope 1 — the fallback numbering is deleted, and half of it was NOT inert
- `getRaceCode()`: db only. No row (or no db) -> `""`. Every caller already
  reads an empty code as "no model" and stops (`codesToTry.empty() -> return
  false`), which is the T213 placeholder path — no sentinel was needed.
- `getFallbackRaceCode()`: db only, serving the row's `fallback_code`
  (gendered M->F). **The plan called the switch "inert at runtime while the db
  loads"; the measurement says otherwise for this one.** Its hardcoded cases
  fired whenever a row carried NO `fallback_code`, db open or not — so race 61
  (SHA, Shark) fell back to DAM/DAF and race 62 (TUN, Tunare) to ERM/ERF, live.
  That is what made `RaceCodeFromDbTest.EveryRowServesItsOwnCodeAndFallback`
  fail in the RED run **with the db open** (`test_red_before_fix.txt`).
- `getZoneSpecificRaceCode()` is **deleted outright** (function, declaration,
  all four call sites in `model_loading.cpp` / `variant_loading.cpp`). It was
  never a db-null fallback: it ran unconditionally and took precedence over the
  row's code. Its entries were the same pre-JSON numbering — race 15 -> FCM/FCF
  (the row is BRM, Brownie), 61 -> NCM/NCF (SHA, Shark), 77 -> FCM/FCF (NGM,
  Neriak Citizen), 80 -> KCM/KCF (REA, Reanimated Hand). The one entry that
  agreed with the rows, race 71 -> QCM/QCF, was redundant: the row's own code
  IS QCM and `getGenderedRaceCode()` already produces QCF. Worst case removed:
  a shark in a Neriak zone was being resolved to a Neriak citizen model.

### Scope 2 — the 38 pinned expectations, regenerated from the db
`tests/test_graphics.cpp`'s 38 `RaceModelLoaderTest` cases (24 `GetRaceCode_*`
+ 13 `GetRaceModelFilename_*` + `GetRaceCode_Unknown`) restated the deleted
switch and ran with no db at all. Replaced by 7 tests in two fixtures:
- `RaceCodeNoDatabaseTest` (no db): all 104 ids the switch answered for now
  return `""`; `getRaceModelFilename()` returns `""`; the eight citizen
  fallback ids return `""`.
- `RaceCodeFromDbTest` (opens the shipped `data/willeq.db`, skips without it):
  `EveryRowServesItsOwnCodeAndFallback` is the table test — it walks all 190
  `race_models` rows and asserts `getRaceCode` == row code and
  `getFallbackRaceCode` == row `fallback_code` for both genders;
  `ContradictedIdsAnswerTheRow` pins the 17 ids where the switch disagreed,
  each carrying what it used to claim; `PlayableRaceModelFilenames` keeps the
  filename pins, now db-served (and adds 130 KEM, which the switch had no
  entry for at all).
`tests/test_asset_loading.cpp`'s `VerifyRaceCodeMappings` restated the same
table; it now compares `getRaceCode()` against the row for the same ids.

**Red/green (measured):** `test_red_before_fix.txt` — 4 of 7 fail against the
old code (3 no-db suites + the table test, the latter with the db open, see
above). `test_green_after_fix.txt` — 7/7.

### Two consumers were relying on the deleted switch (found by ctest, fixed)
1. `tools/hwcaps/gles2_zone_benchmark.cpp` (the entity viewer
   `test_entity_viewer_regression` shells out to) **never opened willeq.db** —
   every race code it used came from the switch. With the switch gone it loads
   nothing. It now calls `openWilleqRaceDatabase()` (new TU
   `tools/tool_race_db.{h,cpp}`; env `WILLEQ_DB_PATH` -> `./data/willeq.db` ->
   configure-time `WILLEQ_DB_REAL_PATH`), the same bootstrap
   `p50_mask_plumbing_probe.cpp` / `ui_inventory_capture.cpp` already do.
   `tools/model_viewer.cpp` had the identical gap and gets the same call.
   The test's race IDS were the old numbering too: `{21,"SKE"}` and
   `{17,"ORC"}` are BEH (Evil Eye) and GOL (Golem) in the audited rows; SKE is
   race 60 and ORC is race 54. Ids corrected, codes under test unchanged.
2. `tests/test_inventory_model_view.cpp`'s `RaceModelAnimationHeadlessTest`
   built a `RaceModelLoader` with no db, so it too was reading the switch. It
   now opens the shipped db in `SetUp()` like `Application::run()` does. Two of
   its cases then failed on a **stale premise**: they used race 330 as the
   "known missing race", which T265/T267 turned into a race that loads — they
   only kept passing because the fixture had no db. Both now use race 9999,
   which has no row by construction. Net: the suite went from 4 skipped + 1
   failed to **9/9 actually running**.

### Scope 3 — other hardcoded race maps found (listed, not changed)
| where | what | verdict |
|---|---|---|
| `race_codes.cpp` `getRaceScale()` | db-null fallback switch, same pre-JSON ids (13 "Wolf" 0.8 vs row AVI Aviak; 21 "Skeleton" vs BEH) | same class as the deleted ones; left because the plan's territory named only the three code functions. Recommend a follow-up deleting it (default 1.0) |
| `race_codes.cpp` `getRaceName()` | db-null fallback switch, same numbering | same; honest default `"Unknown (id)"` already exists |
| `eq_strings.cpp` `EverQuest::GetRaceName()` | race id -> display name, and it is **shifted**: 14 -> "Iksar" (row: WER Werewolf; Iksar is 128), 128 -> "Vah Shir" (KEM is 130), 130 -> "Froglok" (FRM is 330), 330 -> "Drakkin" | **user-visible wrong text**, game-side TU, outside this unit's territory. Highest-value follow-up of the three |
| `entity_renderer.cpp` `getColorForRace()` | race id -> placeholder cube colour, pre-JSON numbering (21 "Skeleton", 26 "Snake" vs row FRO, 33 "Ghost" vs GHU) | cosmetic, only reached for placeholder cubes |
| `entity_renderer.cpp:398` | `raceId == 127 \|\| raceId == 240` to skip placeholders | agrees with the rows (both IVM), but `race_models.invisible` is the column that means this |
| `entity_renderer.cpp:835/1294` | `raceId == 72 \|\| 73` boat collision | agrees with the rows (SHIP / LAUNCH) |
| `inventory_constants.cpp` `buildRaceString()` | race BITMASK -> item-usable abbreviations (HUM/BAR/ERU/FRG...) | different namespace (item race masks, not model codes); not a contradiction |
| `animation_mapping.cpp` | code-keyed animation-source table incl. `{"FRG","FRO"}` | keyed by CODE, not race id; unaffected |

### Verification
- `cmake --build build -j24`: ok (`build_after_fix.log`).
- `ctest -j24`: **2953/2954** (`ctest_full.txt`); the single failure is the
  documented ALSA `AudioManagerTest.PreloadCommonSounds`. (Count is 2954 vs
  T267's 2978 because this branch is cut from a later fable; the four
  `RaceModelLoaderTest` groups became 7 tests, and 38 pinned cases collapsed.)
- `scripts/build-gles2-x86.sh`: ok. Visual gate: **PASS**, goldens untouched
  (`visual_gate.txt`; `git status` shows only source/test files + the new TU).
- `scripts/build-arm-noble.sh`: ok (`build_arm_noble.log`, "Build complete").
- `check_file_sizes.sh fable`: 2 FAILs, **both pre-existing** —
  `gles2_zone_benchmark.cpp` 3033 -> 3037 and `model_viewer.cpp` 3236 -> 3242
  (2 lines of call + comment each; the bootstrap itself went into the new
  `tools/tool_race_db.cpp` precisely to avoid growing them). Both WARN files
  shrank: `model_loading.cpp` 1815 -> 1793, `variant_loading.cpp` 1626 -> 1607.

### Deviations
- **Territory widened, forced by the deletion.** The plan named
  `race_codes.cpp` + the pinned tests. Deleting the switch broke two consumers
  that were silently living off it (`gles2_zone_benchmark`, `model_viewer`) and
  one test fixture, so this unit also touches `tools/hwcaps/gles2_zone_benchmark.cpp`,
  `tools/model_viewer.cpp`, `tests/test_inventory_model_view.cpp`,
  `tests/test_entity_viewer_regression.cpp`, `CMakeLists.txt` and adds
  `tools/tool_race_db.{h,cpp}`. No overlap with T268 (loot) or T269 (bank).
- `getZoneSpecificRaceCode()` was deleted, not just db-gated. It has no db
  backing (there is no zone-specific column) and every entry it had was either
  wrong against the rows or redundant. Flagged rather than absorbed because the
  plan's Scope 1 spoke only of "the fallback numbering".
- `getRaceScale()` / `getRaceName()`'s fallback switches are the same defect
  class and were left in place to stay inside the plan's stated scope; they are
  listed above with a recommendation.
