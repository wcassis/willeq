# F00 — Repair link-broken test targets and stale tests

Restore the test suite to a state where "green" is meaningful. Test and build
files ONLY — production code must not change. Baseline context:
`refactor/F_file_split.md` §"Test baseline" and `refactor/test_baseline.txt`.

## Steps

1. **Locate the WilleqDatabase implementation.** Grep for the definition of
   `EQT::WilleqDatabase::getDbString` / `getMusicFile` (likely one .cpp under
   `src/common/` or `src/client/`). Note which source file(s) provide it and
   any transitive sources it needs.

2. **Fix the 13 link-broken targets** in `tests/CMakeLists.txt` by adding the
   missing source file(s) to each target's source list:
   test_string_database, test_eff_loader, test_zone_music,
   test_day_night_audio, test_sound_assets, test_zone_sound_emitters,
   test_sound_effects, test_spatial_audio, test_graphics, test_weather_audio,
   test_music_loopback_compare, test_combat_music, test_asset_loading.
   Verify each failure is actually this link error before fixing; if a target
   has a different cause, fix that cause the same minimal way and note it.

3. **Rewrite test_spell_database against the current API.** Read
   `include/client/spell/spell_database.h`. Map each obsolete call
   (`loadFromFile`, `getAllSpellIds`, `getSpellsForClass`,
   `getSpellsByLevelRange`, `getBeneficialSpells`, …) to the current API,
   preserving each test's intent. If a tested capability no longer exists,
   delete that test case and list it in the review section.

4. **Investigate the 6 genuinely-failing tests** (list in
   `refactor/test_baseline.txt`). For each: if it fails from a stale hardcoded
   expectation (e.g. LoggingTest.ModuleCount_MatchesExpected counting modules),
   update the expectation. If it fails for environment reasons (missing EQ
   client assets, machine-specific paths), do NOT hack around it — document
   the cause in the review section and leave it failing. If it implicates a
   real production bug, do NOT fix production code — document and leave.

5. **Regenerate the baseline.** After fixes:
   `ctest --test-dir build 2>&1 | grep -E '\(Failed\)|\(Not Run\)'` →
   overwrite `refactor/test_baseline.txt` (strip leading whitespace). Every
   remaining line must have a documented cause in your review section.

## Acceptance criteria

- All 14 previously-unbuildable test targets compile and link.
- ctest failure count strictly decreases; every remaining failure documented.
- `git diff --stat` shows changes ONLY under `tests/`, `refactor/`
  (this file + test_baseline.txt). Zero changes under `src/` or `include/`.
- verify_move_only.py is NOT applicable to this unit (not a move); skip it.

## Review

**Executed on branch `task/F00-test-repair` (slot1), from `fable` @ c19a34a, later
rebased onto `fable` @ c2a49cf (F01 landed on `fable` while this unit was in
progress — no functional conflicts, purely a rebase to keep the diff clean).**

### Step 1 — Locate WilleqDatabase implementation

`EQT::WilleqDatabase::getDbString` / `getMusicFile` (and all other
`WilleqDatabase` methods) are implemented in `src/client/willeq_database.cpp`.
It depends on `third_party/sqlite3.c` (amalgamation) / `third_party/sqlite3.h`,
and needs `SQLITE_THREADSAFE=1 SQLITE_OMIT_LOAD_EXTENSION` compile
definitions plus linking `dl` (dlopen, required by SQLite on Unix) and the fmt
library (LOG_* macros used in willeq_database.cpp). This mirrors the pattern
already used for `willeq_core` and the `gles2_*` tools in the top-level
`CMakeLists.txt`.

### Step 2 — Fix the 13 link-broken targets

Confirmed by rebuilding each target individually before editing: all 13 were
undefined-reference link errors for `EQT::WilleqDatabase::*` symbols (no
target had a different root cause). Fixes in `tests/CMakeLists.txt`:

- Added `${CMAKE_SOURCE_DIR}/third_party` to the file-scope `INCLUDE_DIRECTORIES()`
  and a file-scope `ADD_COMPILE_DEFINITIONS(SQLITE_THREADSAFE=1 SQLITE_OMIT_LOAD_EXTENSION)`
  so every test target in this file can safely pull in `sqlite3.c`.
- `test_string_database`, `test_eff_loader`: added `willeq_database.cpp` +
  `sqlite3.c` sources and `dl` to link libs.
- `AUDIO_COMMON_TEST_SOURCES` (shared source list): added `willeq_database.cpp`
  + `sqlite3.c`. This one change fixes 9 of the 13 targets that already used
  this variable: `test_zone_sound_emitters`, `test_day_night_audio`,
  `test_zone_music`, `test_sound_assets`, `test_sound_effects`,
  `test_spatial_audio`, `test_weather_audio`, `test_combat_music`,
  `test_music_loopback_compare`. `dl` was already in `AUDIO_COMMON_TEST_LIBS`
  for UNIX.
- `test_graphics`, `test_asset_loading`: added `willeq_database.cpp` +
  `sqlite3.c` sources and `dl` to link libs.

All 13 now build and link. Full `cmake --build` of every target (including
`willeq` and all tools) is clean.

### Step 3 — Rewrite test_spell_database

`tests/test_spell_database.cpp` was rewritten from scratch against
`include/client/spell/spell_database.h`. The legacy API it tested
(`loadFromFile`, `getAllSpellIds`, `getSpellsForClass`, `getSpellsByLevelRange`,
`getBeneficialSpells`/`getDetrimentalSpells`, `getSpellsByEffect`,
`filterSpells`) is gone; the current `SpellDatabase` is a thin LRU cache in
front of single-row SQLite queries via the `WilleqDatabase` global instance
(`initFromDatabase()`, `getSpell(id)`, `getSpellByName(name)` exact match,
`hasSpell`, `getSpellCount`). `findSpellsByName()` also still exists but is
now just a one-result wrapper around the exact-match query (no `LIKE`).

The rewritten test opens the real `data/willeq.db` once in `main()` and
installs it as the `WilleqDatabase` global instance (added
`WILLEQ_DB_PATH` compile definition in `tests/CMakeLists.txt`, override via
`WILLEQ_DB_PATH` env var); every test `GTEST_SKIP()`s if that open failed,
matching the `hasEQClientFile()`-style guards already used elsewhere in this
suite.

**Kept, remapped to current API:**
- `LoadsSuccessfully`, `LoadsReasonableSpellCount` — `loadFromFile` → `initFromDatabase`.
- `FailsGracefullyOnMissingFile` — remapped to `WilleqDatabase::open()` on a
  bad path directly (renamed test suite to `WilleqDatabaseTest`, since
  `SpellDatabase` no longer takes a file path at all — the "missing file"
  concept now lives one layer down).
- `LookupByIdWorks`, `LookupByIdReturnsNullForInvalid`, `HasSpellWorks` —
  `getAllSpellIds()` replaced by a `scanSpellIds()` test helper that probes
  `hasSpell(id)` over an id range (spells run 3–8663 in the current DB) since
  there is no bulk-listing API any more.
- `SpellsHaveValidData` — same `scanSpellIds()` remap.
- `DurationCalculation` — same remap (searches for a spell with
  `duration_formula > 0` instead of using the removed `filterSpells()`).
- `EmptyStringSearchReturnsEmpty`, `GetSpellByInvalidNameReturnsNull` — unchanged in substance.
- Added `GetSpellByNameWorks` as the direct replacement for the deleted
  case-insensitive name test (see below) — round-trips a spell's exact name
  through `getSpellByName()`.

**Deleted (capability no longer exists — see comments left in the test file
at each deletion site for detail):**
- `LookupByNameCaseInsensitive` — `getSpellByName()` is `WHERE name=?` with no
  `COLLATE NOCASE`; case-insensitive lookup isn't supported any more.
- `FindSpellsByPartialName`, `IconicSpellsExist` — `findSpellsByName()` no
  longer does substring/partial matching, it's an exact-match wrapper.
- `SpellEffectsAreValid`, `EffectCountAndHasEffect` — `EQT::SpellInfo`
  (`include/client/willeq_database.h`) has no `effects` field at all, so
  `queryAndCache()` (`src/client/spell/spell_database.cpp`) never populates
  `SpellData::effects`. Every spell has empty effects via this API; nothing
  meaningful to assert. Not a production bug fixed here (test/CMake only) —
  flagged for whoever owns the spell-effects data path next.
- `FilterByClass`, `FilterByLevelRange`, `FilterBeneficialDetrimental`,
  `FilterByEffect`, `CustomFilter`, `SpellDataHelperMethods` — all called
  bulk/filter methods (`getSpellsForClass`, `getSpellsByLevelRange`,
  `getBeneficialSpells`, `getDetrimentalSpells`, `getSpellsByEffect`,
  `filterSpells`) that no longer exist. `SpellDataHelperMethods` additionally
  depended on `isDamageSpell()`/`isHealSpell()`, which read the same
  never-populated `effects` array.

All 11 remaining tests pass against the real `data/willeq.db`.

### Step 4 — Investigate the 6 genuinely-failing tests

- **`LoggingTest.ModuleCount_MatchesExpected`** — stale hardcoded expectation.
  `MOD_COUNT` is 21 (a module was added since the test was written expecting
  20). Updated the expected value in `tests/test_logging.cpp`. Fixed.
- **`UISoundsIntegrationTest.SoundIdsExistInSoundAssets` /
  `SoundFilenamesMatchSoundAssets`** — not actually an environment failure as
  originally assumed; it's a test-file bug. Both tests call a helper
  `SkipIfNoAssets()` that calls `GTEST_SKIP()` — but `GTEST_SKIP()` only
  unwinds the function it's called *directly* in, not the caller. Calling it
  from a non-`TEST_F` helper is a no-op as far as the calling test is
  concerned, so when `data/eq_client/SoundAssets.txt` is absent (it is, in
  this environment) the test body ran anyway against a default-constructed/
  unloaded `SoundAssets`, comparing everything against `""`/`false` and
  failing instead of skipping. Fixed in `tests/test_ui_sounds.cpp` by adding
  an explicit `if (!assetsLoaded_) return;` after the `SkipIfNoAssets()` call
  in both `TEST_F` bodies (kept the helper for documentation/consistency but
  commented the gotcha). Both tests now correctly `SKIPPED` given the missing
  asset file — this is the environment-failure case, just surfaced
  correctly now.
- **`ComprehensiveZoneTest.ClassicZones`, `ComprehensiveZoneTest.OverallSummary`,
  `DoorLoadingUnitTest.MultipleZonesHaveObjectGeometries`** — genuine
  environment failures, left failing per instructions. `data/eq_client/`
  (the EQ Titanium client asset tree with zone `.s3d`/`.eqe1` files) does not
  exist in this checkout/build environment (`ls` confirms:
  `/mnt/projects/fable/wt/slot1/data/eq_client`: No such file or directory).
  All three tests report "(file not found)"/"SKIP (not found)" per-zone and
  then fail their final `EXPECT_GT(..., 0u)` assertion because zero zones
  loaded. No production or test code changed for these three; they will pass
  once run against a checkout with real client assets under `data/eq_client`.

One more failure surfaced only in a fully-parallel `ctest` run and is
unrelated to any change here: `RDPServerFrameTest.UpdateFrameAfterStart` /
`UpdateFrameMultipleTimes` segfaulted once under load, then passed cleanly
both filtered alone and in two subsequent full sequential re-runs. Treated as
pre-existing test-parallelism flakiness (not caused by, or related to, any
file touched in this unit) and not otherwise addressed.

### Step 5 — Regenerated baseline

`refactor/test_baseline.txt` now contains exactly the 3 documented
environment failures above (down from 6 failing + 14 not-built = 20 problem
lines in the original baseline). Two consecutive full sequential `ctest`
runs both produced the identical 3-failure set (1189/1192 passing), so this
is stable, not flaky.

### Deviations from the plan

- Step 4 uncovered a 7th real failure not in the original baseline:
  `EffLoaderMp3Test.DefaultMp3Index` in `test_eff_loader`, which only started
  running at all once Step 2 made `test_eff_loader` link. Its production
  code (`EffLoader::getMp3File()`, `src/client/audio/eff_loader.cpp`) reads
  music filenames from the `WilleqDatabase` global instance and no longer
  falls back to `mp3index.txt` at all (`loadMp3Index()` is now a no-op
  stub) — but the test never opened a `WilleqDatabase`. This is a test-setup
  gap of the same kind Step 2 was already fixing across the file (missing
  `WilleqDatabase` wiring), so it was fixed the same way: added a
  `::testing::Environment` in `tests/test_eff_loader.cpp` that opens
  `data/willeq.db` and installs it as the global instance for the whole
  binary (this file uses the default `GTest::gtest_main`, hence an
  `Environment` rather than a custom `main()` like `test_spell_database`),
  and guarded `DefaultMp3Index` with a skip if that open fails. Added
  `WILLEQ_DB_PATH` compile definition for `test_eff_loader` in
  `tests/CMakeLists.txt` (same pattern as `test_spell_database`).
  `InvalidMp3IndexReturnsEmpty` needed no change (returns `""` for
  out-of-range indices regardless of DB availability).
- The `UISoundsIntegrationTest` fix (Step 4) is a test-logic bug fix, not
  purely "update a stale expectation" or "leave it, it's environmental" as
  the plan's two examples describe — it's a third category (a broken
  `GTEST_SKIP()`-in-helper pattern) but stays within "test files only" and
  turns a false failure into a correctly-skipped environment case, so it
  was fixed rather than left failing or merely documented.
- `git diff --stat fable` initially showed unrelated changes to `src/client/eq.cpp`
  / `src/client/eq_commands.cpp` / top-level `CMakeLists.txt` / two other
  `refactor/*.md` files. Root cause: `fable` advanced (F01 unit landing) after
  this branch was cut from it but before this unit finished. Rebased
  `task/F00-test-repair` onto the current `fable` tip to eliminate the base
  drift; no conflicts (F01 never touched `tests/` or `refactor/test_baseline.txt`/
  `F00_test_repair.md`). Final `git diff --stat fable` is `tests/` +
  `refactor/test_baseline.txt` + `refactor/F00_test_repair.md` only, as required.
- `verify_move_only.py` — not applicable (no code moved), skipped per plan.
