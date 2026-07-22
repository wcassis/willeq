# F01 — Split RegisterCommands into eq_commands.cpp

Pilot unit of Batch F. Move the slash-command registration cluster out of
`src/client/eq.cpp` (19,676 lines) into a new TU. Pure move — no behavior
change, no interface change, no reformatting of moved code.

## Scope (line refs @ commit a0c02cd — verify by method name, not line number)

Move these `EverQuest` method definitions from eq.cpp to
`src/client/eq_commands.cpp`:
- `RegisterCommands` (~lines 7709–10075, ~2,366 lines of lambda registrations)
- `runPmem` and `runLoadDiagnostics` (~7594–7709, the two diagnostic helpers
  in the same cluster — confirm exact names/signatures by reading that range)

## Steps

1. Read the cluster boundaries in eq.cpp (grep for `::RegisterCommands`,
   `::runPmem`, `::runLoadDiagnostics`; find where each definition ends).
2. Create `src/client/eq_commands.cpp`. Top of file: a brief comment noting it
   holds EverQuest slash-command registration, then the include block —
   start from the includes eq.cpp has and prune to what the moved code needs
   (compiler-driven; boilerplate include changes are allowed by the verifier).
3. Move the method definitions verbatim (cut from eq.cpp, paste unchanged).
4. If moved code references file-local helpers from eq.cpp (`static` functions
   or anonymous-namespace symbols):
   - used ONLY by moved code → move them too (list each in the review);
   - used by BOTH files → leave in eq.cpp, remove `static`/move the
     declaration into a new `src/client/eq_internal.h` shared by both TUs.
     List each such symbol in the review — the verifier will flag these
     lines and they need orchestrator sign-off.
5. Add `src/client/eq_commands.cpp` to the same source list as eq.cpp in
   CMakeLists.txt.

## Verification

- Build clean: `cmake --build build -j24` (use a 600000 ms timeout; if it
  still times out, re-run — it resumes incrementally).
- `ctest --test-dir build` — zero NEW failures vs `refactor/test_baseline.txt`
  (pre-existing failures listed there are expected).
- `python3 scripts/refactor/verify_move_only.py fable..HEAD` — PASS, or
  substantive lines limited to the de-static-ed helpers documented in review.
- `bash scripts/refactor/check_file_sizes.sh fable` — eq_commands.cpp will
  WARN (>1,500); that is accepted for this unit. It must not FAIL (>3,000).
- eq.cpp shrinks by roughly 2,400+ lines.

## Review

**Step 1 — boundaries confirmed by grep/read, not by the plan's approximate
line numbers:**
- `EverQuest::runPmemDiagnostics` (plan called it `runPmem`; actual name in
  code is `runPmemDiagnostics`) — begins line 7594 @ fable.
- `EverQuest::runLoadDiagnostics` — begins line 7689 @ fable.
- `EverQuest::RegisterCommands` — begins line 7709 @ fable, ends line 10069
  (closing `}`), followed by the `#endif` (line 10070) that closes the
  `#ifdef EQT_HAS_GRAPHICS` guard wrapping all three functions (opened line
  7590, immediately after `ProcessChatInput` ends at line 7588).
- Exact cut range: eq.cpp lines 7590–10070 inclusive (2,481 lines), moved
  verbatim into `eq_commands.cpp`. Seam in eq.cpp verified clean (blank line
  then the pre-existing commented-out `ConnectToUCS` block continues
  unchanged).

**Step 2/3 — new file:** `src/client/eq_commands.cpp` created with a top
comment block describing its contents, followed by the same include block
lines 1–79 of eq.cpp copied verbatim (no pruning was possible — the
2,300-line `RegisterCommands` lambda cluster touches nearly every subsystem
eq.cpp includes: inventory, hotbar, hotkeys, detail/seasonal/weather
controllers, spell/skill/buff managers, audio, chat buffer, command
registry, JSON config, etc. Confirmed by grepping the moved block for each
subsystem's identifiers before deciding not to prune — all were referenced).
Then the verbatim moved body (2,481 lines).

**Step 4 — shared/file-local helper check:** grepped the moved block for
every `static`-at-file-scope symbol defined in eq.cpp (`EXP_FOR_LEVEL`,
`MAX_EXP_LEVEL`, `CalculateExpProgress`, `getDoorTypeFromName`,
`getDamageVerb`) and for the free function `eqcrypt_block`. **None are
referenced by the moved code.** The only cross-file symbol touched is
`EverQuest::s_debug_level`, a class static member (declared in `eq.h`,
defined in `eq.cpp`), which is directly usable from any TU that includes
`eq.h` — no `static`/anonymous-namespace issue, no `eq_internal.h` needed.
**No helpers were moved or de-static-ed; `eq_internal.h` was not created.**

**Step 5:** `CMakeLists.txt` — added `src/client/eq_commands.cpp` immediately
after `src/client/eq.cpp` in `CLIENT_CORE_SOURCES`.

**Line counts:** eq.cpp 19,676 → 17,195 (−2,481). eq_commands.cpp: 2,568
total (8-line header comment + 79-line include block + 2,481-line moved
body).

**Verifier output summary:**
- Build (`cmake --build build -j24 -- -k`): `willeq_core`, `willeq`, and all
  buildable test targets compile/link clean (only pre-existing warnings,
  e.g. deprecated `DES_ncbc_encrypt`, `void*` pointer arithmetic — untouched
  by this move). The only build failures are the 14 pre-existing broken
  test targets in `refactor/test_baseline.txt` (test_string_database,
  test_spell_database, test_graphics, test_asset_loading, test_eff_loader,
  test_zone_sound_emitters, test_day_night_audio, test_zone_music,
  test_sound_assets, test_sound_effects, test_spatial_audio,
  test_weather_audio, test_combat_music, plus their NOT_BUILT ctest
  entries). Zero new failures.
- `ctest --test-dir build`: failures match `refactor/test_baseline.txt`
  exactly (LoggingTest.ModuleCount_MatchesExpected,
  ComprehensiveZoneTest.ClassicZones/OverallSummary,
  DoorLoadingUnitTest.MultipleZonesHaveObjectGeometries,
  UISoundsIntegrationTest.SoundIdsExistInSoundAssets/SoundFilenamesMatchSoundAssets,
  plus the 14 NOT_BUILT entries). One transient extra failure was observed on
  the first run, `RDPServerFrameTest.UpdateFrameMultipleTimes (SEGFAULT)`;
  bisected by stashing this unit's changes and rebuilding/re-running the
  same test in isolation against unmodified `fable` — it segfaulted once and
  passed on 4 subsequent re-runs with the exact same binary, confirming a
  pre-existing flaky test unrelated to this move (this unit never touches
  RDP code). Re-ran full `ctest` with this unit's changes restored: result
  matched baseline exactly, RDP test passed. Not a new failure.
- `verify_move_only.py fable..HEAD`: run after committing (see below);
  reported PASS — no substantive/non-move lines beyond the mechanical cut
  and the new file's boilerplate include block and header comment.
- `check_file_sizes.sh fable`: the script itself has no repo-root
  auto-detection override, so it was run with explicit `--files` (equivalent
  result) against both changed files. Raw output: `eq.cpp` FAILs the script's
  blanket >3,000-line threshold at 17,195 lines — expected and accepted, since
  `docs/decisions.md` states legacy files are exempt from the ceiling until
  their own split unit lands (eq.cpp's remaining splits are tracked as later
  Batch F units, not this one). `eq_commands.cpp` WARN (2,568 > 1,500 warn
  threshold, < 3,000 fail threshold) — expected and accepted per the plan's
  own acceptance text ("eq_commands.cpp may WARN but must not FAIL"), which
  is satisfied.

**Deviations:** none in scope/behavior. Two naming corrections vs the plan's
prose (plan said "runPmem", actual symbol is `runPmemDiagnostics"; plan's
line estimates were approximate, actual boundaries differ slightly — both
expected per the plan's own "verify by method name, not line number" note).
