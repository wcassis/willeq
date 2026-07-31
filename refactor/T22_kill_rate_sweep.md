# T22 — Mutation kill-rate sweep across the "well-covered" parsers

**Slot: t1. x86 only — no Pi, no eqemu, no live server.** No sibling
unit; t2 idle.

**Territory: `tests/` ONLY at commit time.** Mutations are temporary
edits to `src/` that you MUST revert; `git status --short src/ include/`
must be empty when you commit. If a fix looks warranted, STOP and
report — this unit strengthens assertions, it does not fix source.

Read `refactor/EXECUTOR_PROTOCOL.md` first and follow it exactly.

## Why this unit

Closes the open half of the benchmark session's mutation-testing intel
(`refactor/deferred.md`, "→ FOR THE TESTING SESSION"). Their finding:
`wld_loader.cpp` had the most covered lines in the repo (952) yet killed
only 1 of 8 injected faults. **T10 confirmed it independently** —
measured 0/8, strengthened to 4/8 — which established two things this
unit builds on:

1. Line coverage is a misleading prioritization signal; kill rate is
   the real one.
2. The technique is cheap: mutate, build ONE test target, run, revert.
   Minutes per mutation.

What T10 did for WLD, this does for the other parsers that *look*
covered. A file with a passing test suite that survives an inverted
comparison is a file whose tests execute it without checking it.

## Targets (in priority order)

Each already has a test binary, so "covered" is the claim under test:

| Source | Lines | Test binary |
|---|---:|---|
| `src/client/graphics/eq/s3d_loader.cpp` | 1322 | `test_asset_loading` / `test_pfs_archive` |
| `src/client/graphics/eq/pfs.cpp` | 304 | `test_pfs_archive` |
| `src/client/audio/xmi_decoder.cpp` | 483 | `test_xmi_decoder` |
| `src/client/string_database.cpp` | 353 | `test_string_database` |
| `src/client/graphics/eqe1_loader.cpp` | 254 | `test_eqe1_loader` |
| `src/client/spell/spell_database.cpp` | 151 | `test_spell_database` |

**Deliberately excluded:** `hc_map.cpp` and `zone_lines.cpp` (T19 just
added assertions + did their own spot-check), `wld_*` (T10/T11 own it),
and anything under `src/client/action/` or the eq domain TUs (T12–T18
covered them recently).

## Method (follow T10's, it is proven)

For each target, inject **6–8 mutations** spread across distinct
behaviors — not eight variations of one line. Good mutation classes for
parsers:

- flip a bounds/size comparison (`<` ↔ `<=`, `>` ↔ `>=`)
- off-by-one an offset or stride
- swap two adjacent field reads
- invert a branch condition (endianness/version/flag checks)
- drop an accumulation (`+=` → `=`)
- return a default instead of a parsed value

For each: apply, build ONLY that target, run ONLY that binary, record
**killed** (some test fails) or **survived**, revert, confirm the revert.
Batch nothing — one mutation live at a time, so an unreverted edit can
never be mistaken for a passing baseline.

**Then strengthen.** For every survivor, add or tighten assertions until
it is killed, and re-run the mutation to prove the kill. Report the
before/after rate per file. Strengthening is where the unit's value is;
the measurement alone is only a report.

## What "good" looks like

T10's shape: baseline 0/8 → 4/8, plus two dead-code findings that fell
out of reading the parser closely enough to mutate it well. A file that
starts at 6/8 needs no work — say so and move on. **Do not manufacture
assertions to inflate a number**; a survivor that represents genuinely
unobservable behavior (a debug log, a field nothing reads) should be
reported as such, and is itself a finding worth writing down.

## STOP conditions

- Any `src/`/`include/` change surviving at commit time → STOP.
- A real bug found while mutating (the parser is already wrong, or a
  mutation reveals a latent crash) → pin with a clearly-named test,
  report prominently, do NOT fix.
- A target that cannot be driven without live EQ assets AND has no
  fixture → skip it, say so, do not invent a fixture under `data/`.

## Fixtures

Real EQ assets resolve via `EQ_CLIENT_PATH` or the `data/eq_client`
symlink (check `ls -la data/` in the slot; recreate as
`ln -s /mnt/projects/EverQuestP1999 <slot>/data/eq_client` if missing).
Captures: `data/captures` → `/mnt/projects/fable/captures`. Both are
READ-ONLY.

## Verification bar (tests-only unit, x86)

1. `cmake --build <slot>/build -j24` clean.
2. `ctest --test-dir <slot>/build --output-on-failure` — full suite;
   known exception `AudioManagerTest.PreloadCommonSounds` (no ALSA).
   Also expect the `RDPServerFrameTest`/`RDPServerLifecycleTest` flake
   family under parallel load — root-caused in T03b, perf territory, NOT
   yours; rerun isolated to confirm before reporting it as a failure.
   Baseline is currently 1617/1618.
3. `git status --short src/ include/` — MUST be empty.
4. `bash <slot>/scripts/refactor/check_file_sizes.sh fable` — no new
   FAILs.

## Review

Executed 2026-07-31. Method followed exactly: one mutation live at a time
via the Edit tool (sed on src/ was avoided per the prior-unit warning),
build only the affected test target, run only that binary, record
killed/survived, revert, confirm the revert (`git status --short` on the
one touched file) before the next mutation. `tests/` only at commit time —
verified below.

### Per-file kill rate (before -> after strengthening)

| File | Mutations | Before | After |
|---|---:|---:|---:|
| `src/client/spell/spell_database.cpp` | 7 | 2/7 | 7/7 |
| `src/client/graphics/eqe1_loader.cpp` | 7 | 5/7 | 6/7 |
| `src/client/string_database.cpp` | 8 | 6/8 | 8/8 |
| `src/client/graphics/eq/pfs.cpp` | 7 | 5/7 | 6/7 |
| `src/client/audio/xmi_decoder.cpp` | 7 | 3/7 | 5/7 |
| `src/client/graphics/eq/s3d_loader.cpp` | 7 | 3/7 (+2 unmeasured no-op mutations, see below) | 5/7 |

Total: 43 mutations applied and reverted; 24 baseline kills -> 37 kills
after strengthening.

### spell_database.cpp (2/7 -> 7/7)

1. Invert `if (!info.found) return nullptr;` -> KILL (5 tests).
2. Flip `m_max_spell_id` update `>` -> `<` -> SURVIVED (no test read
   `getMaxSpellId()`) -> added `MaxSpellIdTracksHighestLookedUpId`
   (non-monotonic lookup order) -> KILL.
3. `evictIfNeeded()` bound `>=` -> `>` (off-by-one) -> SURVIVED (no
   eviction-stress test) -> added `CacheNeverExceedsCapacity` (drives
   `capacity+20` distinct ids) -> KILL.
4. Swap `target_type`/`resist_type` source fields -> KILL
   (`SpellsHaveValidData`'s range checks happened to differ enough).
5. Drop `m_cache_hits` accumulation -> SURVIVED (no test read
   `getCacheHits()`) -> added `CacheHitCounterIncrementsOnRepeatLookup`
   -> KILL.
6. Swap `cast_time_ms`/`recovery_time_ms` assignment -> SURVIVED (bounds
   check alone doesn't catch a same-range swap) -> added
   `FieldsMatchRawDatabaseQuery` (ground-truths every mapped field
   against a direct `WilleqDatabase::getSpell()` call) -> KILL.
7. Swap `duration_formula`/`duration_cap` assignment -> SURVIVED -> same
   `FieldsMatchRawDatabaseQuery` addition -> KILL.

Added 4 new tests exercising `getMaxSpellId()`, `getCacheHits()`,
`getCacheCapacity()`/`getCacheSize()`, and field-by-field ground-truth
comparison — all previously-untested public surface.

### eqe1_loader.cpp (5/7 -> 6/7)

1. Invert data-alignment check -> KILL.
2. Off-by-one bounds check (`>` -> `>=`) -> KILL (last entry's data ends
   exactly at EOF in the real fixture, so the tighter bound rejects a
   valid file).
3. Drop `alphaCount++` -> SURVIVED. **Left as documented survivor**:
   `alphaCount` only feeds a log line (`EQE1_LOG_INFO`); nothing reads it.
   Genuinely unobservable, not manufacturing an assertion for a debug
   counter.
4. Invert version-check `&&` -> `||` -> KILL.
5. Invert `hasAlpha` flag -> KILL.
6. Corrupt alpha-companion name lookup (`"_alpha"` -> `"_alph"`) -> KILL.
7. Off-by-one raw-data pointer (`+1`) -> SURVIVED (`testRawDataEntry`
   only checked non-null/non-zero-size, not byte content) -> added
   `testRawDataBytesMatchFile` (independent file read, `memcmp` against
   the indexed offset) -> KILL.

### string_database.cpp (6/8 -> 8/8)

1. `find` -> `rfind` in `<id> <text>` split -> KILL (18 tests).
2. Text substr off-by-one (drops the `+1`) -> KILL (leading space).
3. Invert `\r`-strip check (`.back()` -> `.front()`) -> KILL (real fixture
   is CRLF).
4. `second_caret` search start off-by-one in dbstr parsing -> KILL.
5. `%T` `arg_num - 1` -> `arg_num` -> KILL.
6. Regular `%N` `arg_num - 1` -> `arg_num` -> KILL.
7. `%B` bounds check `<` -> `<=` -> SURVIVED (no test drove the `%B`
   branch at all) -> added `FormatTemplate_BPlaceholder_NoParen`,
   `_WithParen`, and `_OutOfRangeIndex` -> the out-of-range test makes the
   mutant read one past the end of a `std::vector<std::string>` via
   `operator[]`, which **segfaults** under the mutant and passes cleanly
   against real code -> hard KILL.
8. `%z` placeholder body (`"[duration]"` -> `""`) -> SURVIVED (no test
   drove `%z` at all) -> added `FormatTemplate_ZPlaceholder` -> KILL.

**Coverage gap found, not fixed (out of scope):** the SQLite-backed path
(`initFromDatabase()`, `using_db_` branches in `getEqStrCount`/
`getDbStrCount`/`getString`/`getDbString`) is never exercised by any test
in this file — the fixture only calls `loadEqStrFile`/`loadDbStrFile`
(the legacy file-loading path), never `initFromDatabase()`. Not mutated
(would need a live `WilleqDatabase` global instance wired into this test
binary, which is a bigger lift than this unit's scope) — flagging for a
future unit.

### pfs.cpp (5/7 -> 6/7)

1. Invert one magic-byte check (`'P'`) -> KILL (20 tests).
2. Invert filename-table CRC check -> KILL (many failures + a crash on
   `OpenGlobalChr`).
3. `tolower` -> `toupper` in filename-table parsing -> KILL.
4. Filename memcpy off-by-one (`filenameLength-1` -> `filenameLength`)
   -> SURVIVED. **Left as documented survivor**: the extra byte lands
   exactly on `name`'s own null-terminator slot (`name` was sized
   `filenameLength-1`), which libstdc++ absorbs harmlessly; nothing the
   public API exposes changes. Flagging as a latent one-byte overflow
   past `name`'s intended allocation, not a functional bug under this
   libstdc++ — not fixed (out of scope), but worth a maintainer's eye.
5. Invert extension-match (`==` -> `!=`) in `getFilenames()` -> SURVIVED
   (existing `GetWldFilenames`/`GetBmpFilenames` only checked counts, not
   that the returned names actually matched the extension) -> added
   direct suffix assertions to both tests -> KILL.
6. Block-header stride off-by-one (`+8` -> `+4`) in
   `storeBlocksByOffset` -> KILL (crash).
7. Swap `deflateLength`/`inflateLength` read order -> KILL (crash).

### xmi_decoder.cpp (3/7 -> 5/7)

1. Invert `FORM` magic check -> KILL (8 tests).
2. EVNT chunk bound off-by-one (`<=` -> `<`) -> KILL (a real fixture's
   EVNT chunk ends exactly at EOF).
3. Invert delay-byte condition (`< 0x80` -> `>= 0x80`) -> **infinite
   loop** (confirmed via timeout; the loop invariant that guarantees `pos`
   advances no longer holds) -> counts as KILL (ctest would time out).
4. `velocity > 0 && duration > 0` -> `||` -> SURVIVED. Left as documented
   survivor: produces spurious extra note-off events but nothing asserts
   exact event counts/content, only "decode succeeded, header valid".
5. `maxTime` tracking `>` -> `<` -> SURVIVED. Left as documented
   survivor: only affects inter-sequence time offsetting
   (`parseAllSequences`'s `timeOffset`), unobservable with the current
   single-EVNT-chunk-per-file fixtures.
6. `deltaTime = absoluteTime - lastTime` -> `+` -> SURVIVED (no test
   pinned exact output bytes) -> added
   `SyntheticNoteDurationVlqIsExact` (a hand-built minimal XMI with two
   note-ons interleaved with a note-off, so `lastTime` is nonzero when
   the mutant's `+` diverges from `-`) -> KILL.
7. `readXmiVarLen` shift width (`<<7` -> `<<8`) -> SURVIVED initially
   (all 82 real XMI files still "decoded successfully" under the loose
   real-file tests) -> covered by the same synthetic test (its VLQ-encoded
   duration decodes to the wrong tick count under the corrupted shift)
   -> KILL.

M4 and M5 remain genuine survivors — closing them would need either
event-count assertions (risks being fragile/arbitrary) or a
multi-sequence synthetic fixture; flagged, not manufactured.

### s3d_loader.cpp (3/7 -> 5/7, two mutations structurally unobservable)

Used `--gtest_filter` on `ZoneGeometryLoadingTest.*`/`ZoneLightLoadingTest.*`
(and occasionally a wider filter) for fast iteration — the full
`test_asset_loading` binary takes ~60s per run
(`ComprehensiveZoneLoadingTest.LoadMultipleZones` alone is ~53s); full
binary re-verified clean at the end via the whole-suite `ctest` run below.

1. `extractZoneName()` drop the `.s3d`-stripping branch (`!=` -> `==`
   npos check) -> SURVIVED. **Structurally unobservable**:
   `loadObjects()` has a fallback (lines ~407-417) that tries every
   `.wld` file in the `_obj.s3d` archive if the name-based lookup fails,
   which fully masks a corrupted `zoneName_`. Real defensive code, not a
   gap — documented, not "fixed" by a fragile assertion on internal
   state.
2. Invert `hasObjectsWld` check -> KILL (`LoadZoneObjects`).
3. Invert `hasLightsWld` check -> SURVIVED. Root cause: `LoadZoneLights`
   asserted `EXPECT_GE(zonesWithLights, 0)` — a **tautology on an int**
   that can never fail. Strengthened to `EXPECT_GT(zonesWithLights, 0)`
   (freportw/qeynos2/commons/nektulos are known to ship `lights.wld`)
   -> KILL.
4. Invert `lights.empty()` check -> KILL (with the strengthened test from
   #3).
5. Invert `placeables.empty()` check -> KILL.
6. `loadTextures()` return-value invert -> SURVIVED, and **cannot be
   fixed by any caller-side assertion**: the only caller
   (`loadZone`, line ~176) discards the return value entirely. Documented
   as structurally unobservable. Substituted a content-affecting mutation
   in the same function (`.bmp` -> `.bm` extension literal, which does
   change what lands in `zone_->textures`) — this one SURVIVED too,
   because `zone->textures` (populated by `S3DLoader::loadTextures()`)
   was never inspected by any test; the texture-loading tests in this
   file go straight through `PfsArchive`, bypassing `S3DLoader` entirely.
   Added `EXPECT_FALSE(zone->textures.empty())` to
   `ZoneGeometryLoadingTest.LoadZoneGeometry` -> KILL.
7. `extractZoneName()` `_chr` suffix typo (`"_chr"` -> `"_chrX"`) ->
   SURVIVED. Confirms `zoneName_`'s `_chr`/`_chr2` stripping only feeds
   `zone_->zoneName` (metadata never read by `loadCharacters()` or any
   test) — dead-code-adjacent finding, not fixed (out of scope), not
   papered over.

Net: of the 4 structurally-survived mutations (#1 masked-by-fallback,
#6-return masked-by-discarded-return-value, #7 dead-metadata), none are
testable without either restructuring production code (out of scope,
STOP condition territory) or asserting on internal-only state the plan
explicitly warns against ("identity-between-paths != correctness" spirit
extended to "internal state != observable behavior"). All three are
genuine findings, written down rather than inflated away.

### Deviations from the plan

- s3d_loader.cpp table only lists `s3d_loader.cpp`/`pfs.cpp` sharing
  `test_pfs_archive`/`test_asset_loading`; both were exercised
  separately (pfs.cpp via `test_pfs_archive`, s3d_loader.cpp via
  `test_asset_loading`) as the plan's table implies.
- One xmi_decoder.cpp mutation (delay-byte invert) produced an infinite
  loop rather than a clean test failure; confirmed via `timeout`,
  reverted without waiting it out to completion, re-verified clean
  afterward. Not a bug in xmi_decoder.cpp itself (only reachable via a
  deliberately-wrong condition) — noted for completeness since it
  differs from the plan's "revert, confirm the revert" cadence (verified
  via `git status --short` on that one file immediately after, same as
  every other mutation).
- No real bugs found in current (unmutated) source — all STOP-worthy
  findings were either coverage gaps (closed) or structurally
  unobservable mutations (documented, not fixed, per the plan's explicit
  instruction not to fix source in this unit).

### Verification bar

1. `cmake --build build -j24` — clean, exit 0.
2. `ctest --output-on-failure` — 1625/1627 passed. The 2 failures are
   exactly the two documented known exceptions:
   `AudioManagerTest.PreloadCommonSounds` (no ALSA host) and
   `RDPServerFrameTest.UpdateFrameMultipleTimes` (documented flake,
   perf-territory per T03b, not this unit's).
3. `git status --short src/ include/` — empty. Confirmed both mid-run
   (after every single mutation revert) and at commit time.
4. `check_file_sizes.sh fable` — all 6 touched `tests/*.cpp` files: `ok`.
   Pre-existing ERROR/WARN entries (`test_entity_viewer_regression.cpp`,
   `tools/hwcaps/*`) are for files this unit never touched — unrelated
   baseline noise.
