# T392 — sounds in the `snd*.pfs` archives never load (B200)

Class A, slot2, no server (the archives are on disk). Read the B200
board text (T386's narrowing: names ARE in `config/snd_index_cache.json`,
the lookup lowercases, the other failure exits of `loadSoundDataFromPfs`
log and none appears), `src/client/audio/audio_manager.cpp`, the PFS
reader it uses, T328's Review (the sound bank and how ids map to files),
and the audio tests (`tests/test_audio*`, `test_ui_sound*`).

1. Reproduce offline: a test that constructs `AudioManager` against the
   real client tree (`/mnt/projects/EverQuestP1999`) and calls
   `loadSound("BuyItem.WAV")` / `playSound(138)` headlessly (no device —
   whatever seam the existing audio tests use), asserting the PCM buffer
   is non-empty. It must FAIL today with the same "not found" line.
2. Instrument nothing permanent: find the branch that returns "not found"
   without logging — the index says archive X, so either the archive path
   the runtime opens differs from the builder's (`config/eqe1_archives`?
   `snd_index_cache` written with a different base path?), the entry name
   case differs inside the PFS directory, or the cache is consulted
   before the index is loaded. Say which, with the values.
3. Fix; the test goes green; add the same test over five more names from
   T386's list (SwordBlk, Swing, Spell_1, LevelUp, WaterIn) so a whole
   family is pinned, plus one negative (a name not in any archive still
   reports not found, once).
4. Strike B200; annotate B036/B172 (the sounds now audible are OWED a live
   ear — one T328 tone-probe row on fable for id 138, next lock unit).

## Proof

Build, `ctest -j6`, pre-fix/post-fix runs, file sizes. No graphics.

STOP: the archives on disk lack the files (then the index is wrong — say
where it came from).

## Review

Branch `task/T392-pfs-sounds-never-load`, base `e3c80278`. No server (the
eqemu lock was held by T391); the archives on disk were the oracle, and the
live ear is explicitly OWED.

### Root cause — not in the audio path at all

`AudioManager`, `PfsArchive` and `config/snd_index_cache.json` were all
CORRECT. The defect was one byte in `data/willeq.db`:

    sqlite> select hex(filename) from sound_assets where id=138;
    4275794974656D2E5741560D          -- "BuyItem.WAV" + 0x0D

**Every one of the 2,165 `sound_assets.filename` rows carried a trailing
carriage return.** `tools/willeq_db_builder.cpp` read Titanium's CRLF `*.txt`
files with a bare `std::getline` on `'\n'`, which leaves the `\r` attached to
the line and therefore inside the LAST field of every record. The player-facing
path is `PlaySoundIntent -> playSound(138) -> db->getSoundAsset(138) ->
loadSound("BuyItem.WAV\r")`; that lowercases to `buyitem.wav\r`, misses in
`pfsFileIndex_`, and takes the one exit in `loadSoundDataFromPfs` that does not
log — exactly the exit T386 narrowed to and could not explain.

**Why it stayed invisible for so long:** the failure line ENDS in the CR, so a
terminal renders `Failed to load sound: BuyItem.WAV (not found on disk or in
PFS)` with the filename looking perfect. T386 read the name off that line and
correctly concluded the name was right — the name in the LOG was right; the
name in the LOOKUP was not.

The by-NAME entry points were never broken: `getSoundBufferByName("BuyItem.WAV")`
loaded fine both before and after. Nothing in production reaches sounds by name,
which is why nothing caught it.

Four more tables carried the same taint and are fixed by the same change:
`eqstrings` (5,901 rows), `dbstrings` (8,305), `race_data` (475),
`login_strings` (470).

### Fix

1. `tools/willeq_db_builder.cpp`: a `readLine()` helper that strips a trailing
   `\r`, used at all EIGHT file-reading loops (`loadMusicIndex` already
   `trim()`ed, and was the one loader that had never had the bug).
2. Same file, `split()`: rewritten to keep a trailing EMPTY token. The old
   `istringstream`/`getline` form dropped it, which mattered the moment the CR
   stopped padding it — `dbstr_us.txt`'s 19 deliberately-blank strings
   (`142^12^`) went from 3 fields to 2 and were skipped by the
   `fields.size() < 3` guard. Caught by comparing row counts against the shipped
   db, not by a test; without it the fix would have silently deleted 19 rows.
3. `data/willeq.db` rebuilt with the fixed builder.

**The rebuilt db was diffed against the shipped one row by row before it was
installed:** schema `.schema`-identical, `metadata.version` unchanged at **7**
(this is a CONTENT fix, not a schema change — no `kWilleqDbExpectedVersion`
bump, no stale-tree hazard for other slots), `total_entries` 32,889 in both,
seven of the twelve tables bit-identical, and every difference in the other
five is CR-only. Command and diff script are in the unit's scratchpad.

### Tests

New `tests/test_pfs_sound_load.cpp` (199 lines, target `test_pfs_sound_load` in
`tests/cmake/audio.cmake`) — 15 tests, no device (`forceLoopback`), no server,
no graphics, against the REAL archives under `/mnt/projects/EverQuestP1999`.
Two seams the tests had to get right or they would have proved nothing:

- `EqClientPath()` **canonicalises** `data/eq_client`. The shipped
  `snd_index_cache.json` records the canonical path in its `eqPath` field and
  `AudioManager` rejects the cache on a byte mismatch, so a test using the
  symlink path silently takes the full-rescan branch — and would have rewritten
  the shipped cache with a slot-local path.
- `RepoRootCwd` chdirs to `WILLEQ_REPO_ROOT` for the fixture's lifetime, because
  the runtime resolves `config/snd_index_cache.json` RELATIVE to the process CWD
  and ctest runs tests from `<build>/tests`.

| Run | Result |
|---|---|
| Pre-fix (shipped `willeq.db` restored via `git show HEAD:data/willeq.db`) | **7 FAILED**, 8 passed |
| Post-fix | **15 passed** |

The seven reds pre-fix: `BuyItemLoadsById138`,
`NoSoundAssetFilenameHoldsAControlChar` (2,165 of 2,165 rows tainted),
`SwordBlkLoadsById120`, `SwingLoadsById118`, `Spell1LoadsById103`,
`LevelUpLoadsById139`, `WaterInLoadsById100`. The six family names are asserted
through BOTH entry points (by name and by id) precisely because only the id hop
was broken; two negatives (an unknown name, an unknown id) pin that not-found
still reports not-found.

### The standing suite waiver was a misdiagnosis (new B215, struck same commit)

Chasing why `AudioManagerTest.PreloadCommonSounds` was still red turned up that
the "ALSA host quirk" waiver — quoted as fact in `CLAUDE.md`, `README.md`,
`refactor/EXECUTOR_PROTOCOL.md`, `refactor/TESTING_CHARTER.md`,
`refactor/test_baseline.txt` and the BOARD header since 2026-07-22 — is wrong.
The host's missing ALSA card is real and noisy, but `initialize()` falls back to
the RDP loopback backend and SUCCEEDS; the other twenty `AudioManagerTest` cases
in the same binary have been passing all along, which disproves the waiver on
its face. The actual failure was `0 vs 0`: `preloadCommonSounds()` resolves
every id through `WilleqDatabase::globalInstance()`, and `test_sound_assets`
never opened the database.

Fixed by adding the `WilleqDbEnvironment` the other audio tests already use.
MEASURED both ways: with the environment added but the CR-tainted db in place it
is still `0 vs 0`, so BOTH fixes are required. **The suite now has no expected
failures**; all six documents were struck in the same commit.

### Verification

| Step | Result |
|---|---|
| `cmake --build build -j24` | EXIT 0 (one exit-143 timeout first; post-143 sweep found no zero-byte files, no `st??????` temps, both archives non-empty — 15 / 322 members — and 284 of 284 binaries valid ELF) |
| `ctest -j6` | **4,055 of 4,056**; the only red is `visual_gate`, on the documented `-j6` CPU-p95 artifact (`gfaydark` p95 10.917 vs ceiling 10.672) |
| `visual_gate` standalone | **PASS**, goldens untouched |
| `scripts/build-gles2-x86.sh` | EXIT 0 |
| `scripts/build-arm-noble.sh` | EXIT 0 |
| `check_file_sizes.sh` | 0 fail (`willeq_db_builder.cpp` 1,490; `test_pfs_sound_load.cpp` 199) |

**A trap worth recording:** `visual_gate` was RED at the tip before this unit
touched anything — `characterModelsBytes mismatch`, an identical 1,035,817-byte
deficit in all three zones. It failed the same way with the pre-fix db restored,
so it was not the db; rebuilding `build-gles2` made it green. That is the T304 /
T355 stale-tree shape, and the identical-deficit-in-every-zone signature is the
tell. Do not read it as a defect and do not go looking in the db.

### Deviations

- The plan expected the reproduction to print the same "not found" line. It does
  not: the by-NAME tests the plan specified pass on the unfixed tree, because
  the taint is on the id -> name hop. Every family member is asserted both ways
  so the pinned family is the one that actually broke.
- The plan scoped `src/client/audio/audio_manager.cpp`. Nothing in that file
  changed; the fix is in `tools/willeq_db_builder.cpp` and the data.
- Two items outside the plan's scope were taken because they were found by it
  and are cheap: B215 (fixed) and B216 (`race_data` — 475 rows built into every
  db and read by nothing; boarded, not touched).
- No live proof. **One T328 tone-probe row for id 138 on fable is OWED**, on the
  next unit that takes the eqemu lock. B036 and B172 both say so.
