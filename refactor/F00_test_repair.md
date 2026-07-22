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

(executor fills in)
