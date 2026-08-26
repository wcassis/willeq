# T274 — "Froglok (Playable)" is printed to players; two zero-caller race getters (T271)

Slot: slot1. x86. NO eqemu. Class A, tiny. Territory:
config/race_models.json (name column only), tools/willeq_db_builder (no
change expected — just rebuild data/willeq.db with the T246/T265 proof),
src/client/graphics/eq/race_codes.cpp (`getRaceName`, `getGenderName`
deletion + their decls), tests (`test_race_names.cpp`,
`WilleqDatabaseShippedContentTest`). Do NOT touch spellbook (T272 slot2),
read-book (T273 slot3).

## Items
1. Race 330's `name` is `"Froglok (Playable)"`; T271 made names db-only so
   `/who` now prints that to players. Titanium shows "Froglok" for both.
   Fix the row's display name (keep any disambiguation in a comment or a
   separate column, NOT in `name`); check every other row for the same
   pattern (parenthetical annotations, "(Playable)", "(Monster)", codes in
   the name) — T265's table is in review/T265/audit_table_after.md. Rebuild
   the db with the row-level diff proof.
2. `EQT::Graphics::getRaceName()` and `getGenderName()` have zero callers
   (T271). Delete both with the grep proof; keep `EverQuest::GetRaceName()`
   (the reachable one) and its test.
3. Update `test_race_names.cpp` so the sweep still pins name(id) for all
   rows via the surviving function.

## Verification
Full ctest; Review; evidence review/T274/. Commit:
`T274: race display names are clean; dead race getters deleted`.

## Review (executor, 2026-08-26, slot1, off fable 3cf0d399)

Evidence: /mnt/projects/fable/review/T274/ (README.md indexes every file).

### Item 1 — "Froglok (Playable)" was the ONLY annotated display name

Swept all 190 `race_models` rows for the whole annotation class, not just
`(Playable)`: parentheses, brackets, underscores, digits, leading/trailing
space, empty names, and duplicate names.

| finding | rows |
|---|---|
| name carries a parenthetical annotation | **1** — 330 `"Froglok (Playable)"` |
| `(Monster)` / `(Playable)` anywhere else | 0 |
| brackets, underscores, digits, a race code in the name | 0 |
| empty / leading / trailing whitespace | 0 |
| duplicate names | 1 pair — 36 and 415 are both `"Rat"`, which is CORRECT: 415 is T213's alias of 36 (same creature, no distinct Titanium model) |
| examined and KEPT | 195 `"Black/White Dragon"` — the slash is part of the creature's name (eqemu `BlackAndWhiteDragon`, code BWD), not an annotation about the data |

Fix: row 330's `name` is now `"Froglok"` (matching row 26, the monster
froglok — Titanium prints "Froglok" for both). The disambiguation moved into
the row's existing `note` field, which nothing reads at runtime and nothing
displays. No new column was needed.

### Db regeneration proof (row-level)

Rebuilt with `willeq_db_builder --eq-path /mnt/projects/EverQuestP1999
--data-path data --config-path config --output data/willeq.db`
(`builder_output_after.txt`: race_models 190 entries, 32889 total).

`db_diff_before_vs_after.txt` diffs EVERY table's full row set, old vs new:

```
race_models: before=190 after=190 removed=1 added=1
   - (330, 'FRM', 'Froglok (Playable)', 'FRF', '', '', 'globalpcfroglok_chr.s3d', ..., 0.8, 1, 0)
   + (330, 'FRM', 'Froglok',            'FRF', '', '', 'globalpcfroglok_chr.s3d', ..., 0.8, 1, 0)
```

Every other table (spells 8066, eqstrings 5901, dbstrings 8305, item_models
3661, equipment_models 1000, equipment_textures 2623, race_data 475,
sound_assets 2165, login_strings 470, music_files 24, animation_sounds 9,
metadata, sqlite_stat1) is `removed=0 added=0`; no table, index or column
added or dropped. Exactly one row, exactly one column changed. File size
unchanged at 3,854,336 bytes.

### Red / green (measured, both directions)

RED is measured against the SHIPPED db, i.e. the json edit not yet applied —
the standing trap that the runtime reads races from SQLite, so a json edit
alone changes nothing.

- `red_before_db_rebuild.txt` (new tests, old db): FAILED
  `RaceNameFromDbTest.PlayableFroglokPrintsAsFroglok` and
  `RaceNameFromDbTest.NoRowNameCarriesAnAnnotation` — the sweep names exactly
  one offending row, `race 330 name "Froglok (Playable)"`.
- `green_after_db_rebuild.txt` (same binary, rebuilt db): both green. The
  T271 pin `ShiftedIdsAnswerTheRow` failed here as designed (it asserted the
  old string) and was updated to `"Froglok"` — that is the pin doing its job,
  not a regression.
- `red_race_names_before_db.txt` / `red_shipped_content_before_db.txt`: the
  FINAL binaries re-run against the old db (swapped in, then restored and
  `cmp`-verified) — 3 failures and 1 failure respectively. So the new tests
  fail on the pre-T274 data and pass on the post-T274 data with no other
  change.
- `test_race_names_after.txt` 11/11, `test_willeq_database_after.txt` 17/17.

### Item 2 — the two dead getters, with the grep proof

`grep_zero_callers.txt` greps `src include tests tools` at the base rev
3cf0d399 and in the working tree. Before the deletion the ONLY hits for
`EQT::Graphics::getRaceName` / `getGenderName` outside their own definition
and declaration were: one stale comment in `entity_renderer.cpp:123`, one
stale comment in `tests/CMakeLists.txt`, and `test_race_names.cpp` itself.
Zero production callers; `getGenderName` had no caller at all, not even a
test. Both deleted from `race_codes.cpp` and `race_codes.h` (a comment in the
header records why). `getRaceScale()` and `getClassName()` in the same file DO
have callers and are untouched. `EverQuest::GetRaceName()` (eq_strings.cpp) is
the surviving, reachable name function — 4 call sites.

Both stale comments were corrected rather than left pointing at a deleted
symbol.

### Item 3 — the sweep still pins name(id) for every row

`test_race_names.cpp` after T274:
- `EveryRowServesItsOwnName` (unchanged) still walks ids 0..600, finds all 190
  rows and asserts `EverQuest::GetRaceName(id) == row.name` for each.
- `NamePinsTheContradictedIds` is new: the 8 ids the deleted pre-JSON switch
  got wrong, moved off the deleted graphics twin onto `EverQuest::GetRaceName`
  so the numbering can still never drift back.
- `GraphicsNameAndScaleFollowEveryRow` -> `GraphicsScaleFollowsEveryRow`
  (scale half kept, name half gone with the function);
  `GraphicsNameIsUnknownWithNoDb` and `GraphicsNamePinsTheContradictedIds`
  deleted. `getRaceScale`'s two tests are untouched, which is why the target
  stays graphics-gated (CMake comment updated to say so).
- New `PlayableFroglokPrintsAsFroglok` and `NoRowNameCarriesAnAnnotation` (the
  class sweep, so a future annotated row is caught without a new test).
- New `WilleqDatabaseShippedContentTest.T274RaceDisplayNamesCarryNoAnnotation`
  catches the same defect straight out of the shipped db — the case where
  someone edits the json and forgets to rebuild, or rebuilds from a stale json.

Net: names are still pinned for all 190 rows, by one function instead of two.

### Verification

| step | result |
|---|---|
| `cmake --build build -j24` | PASS (`build_x86_gles2.txt`) |
| `ctest --output-on-failure` | **3025/3026** (`ctest_full.txt`); the one failure is the documented ALSA host quirk `AudioManagerTest.PreloadCommonSounds` |
| `scripts/build-gles2-x86.sh` | PASS (`build_gles2_x86_script.txt`) |
| visual gate | PASS (`visual_gate_rerun.txt`). **First run FAILED** (`visual_gate.txt`) on `befallen: p95 CPU ms 46.869 exceeds ceiling 40.144` — a CPU-time ceiling, not a pixel comparison; zero image mismatches in either run. Re-ran clean. Goldens untouched (`git status` shows only the 8 files of this unit). |
| `scripts/build-arm-noble.sh` | PASS (`build_arm_noble.txt`) |
| `check_file_sizes.sh` | `check_file_sizes.txt`, run against the base rev 3cf0d399: 4 ok, 1 FAIL — `entity_renderer.cpp` 3340 lines, which is **pre-existing and unchanged** (3340 lines at 3cf0d399 too; this unit's edit there is a one-word comment fix). No new FAIL, no new WARN. |

### Deviations / notes

- `check_file_sizes.sh fable` (the protocol's literal form) reported 12 files
  because `fable` advanced 3 commits (T272/T275 merges) while this unit ran;
  it was reporting those merges in reverse. Re-run against the actual base rev
  3cf0d399, which is what this branch changed.
- `deferred.md` was NOT touched: its T271 entry names both of this unit's
  items as IN PROGRESS T274, and the file is being edited by the parallel
  T272/T275 merges. Left for the orchestrator to close on merge.
- `race_data` (475 rows) was checked as a possible second name source — it
  stores raw field blobs, no names. `race_models.name` remains the only race
  display-name source in the tree.
