# T402 — tracked caches (B248), dead shipped configs (B245), the ignored argument (B246)

Class B ×3, slot2, no server. Read the three board entries and T399's
§Review.

## Leg A — B248

`config/snd_index_cache.json` and `gfx_index_cache.json` are git-tracked
and rewritten by every client run, so any executor who runs the client
dirties the tree. Decide by reading how they are used: they are CACHES
(a miss means rescan — T399 kept that quiet on purpose). Move the WRITE
side to a build/run-local location (the data-root rule: `$WILLEQ_DATA_ROOT`
or the executable's dir — read `ResolveDataPath`'s write-side handling
T399 added for the caches) and stop tracking the files (`git rm --cached`,
`.gitignore`), OR keep them tracked as shipped seeds and make the client
write elsewhere. Say which and why; a fresh checkout must still start
fast (measure the cold rescan cost once and record it). Test: running
the headless client leaves `git status` clean (a source-reader/ctest pin
that the write path is not under `config/`). Strike B248.

## Leg B — B245

`config/chat_settings.json` and `config/ui_settings.json` are shipped and
read by nothing (six-directory census). Read what they CONTAIN: if a
setting in them names behaviour the client has (chat channel colours,
window positions), the file is the unreached feature, not dead data —
board that as its own item with the fields listed; delete the file only
if nothing in it corresponds to anything. Strike or annotate B245.

## Leg C — B246

`loadRaceMappings(jsonPath)` ignores its argument (race data comes from
SQLite) yet S03 refuses to start without `config/race_models.json` and
`renderer_init` LOG_FATALs on a return that only means "no database".
Remove the parameter and the file requirement; the fatal names the real
condition; delete `config/race_models.json` if nothing else reads it
(census) — or if it does carry data the db lacks, say so and STOP that
leg. Tests: startup without the file succeeds; startup without the db
fails with the db message. Strike B246.

## Proof

Build, `ctest -j6` (no expected failures beyond B240's gate delta),
pre-fix/post-fix, file sizes; no graphics change expected — if the
render init path changes, visual gate standalone.

STOP: race_models.json carries data the db does not.

## Review

Executed 2026-09-05/06 in `wt/slot2`, branch `task/T402-caches-dead-configs`,
base `4615da83` (verified at checkout). No server lock taken (T401 held it; it
has since merged and `fable` is at `4f5c9125` — my merge-base is unchanged).

| leg | commit | what |
|---|---|---|
| A (B248) | `5a178682` (+ fixup `286fc71b`) | index caches leave the checkout |
| B (B245) | `923fe626` | the two orphan config files deleted, B255 boarded |
| C (B246) | `7a1c5b39` | the ignored argument, the S03 check, the wrong fatal |

**No STOP fired.** Leg C's STOP ("race_models.json carries data the db does
not") is answered below: the file stays, for a different and stronger reason.

### Leg A — where the caches are written now

`eqt::ResolveCachePathForRead()` / `ResolveCachePathForWrite()`
(`include/client/data_paths.h`, `src/client/core/data_paths.cpp`) take the
first candidate that HAS the file / is WRITABLE, from:

1. `$WILLEQ_CACHE_DIR`
2. `$WILLEQ_DATA_ROOT/cache`
3. `<directory of the running executable>/cache` — i.e. `build/bin/cache`,
   `build-gles2/bin/cache`, `build-arm-noble/bin/cache`
4. `cache/` (CWD-relative last resort)

None is inside the checkout, and `build*/` is already gitignored. Both files
left the index (`git rm --cached`) and `.gitignore` gained them plus `/cache/`
so a stale copy in an existing worktree cannot come back. A miss is not an
error: `ForWrite` returning "" makes the save a `LOG_DEBUG` no-op.

**Keeping them as shipped seeds was rejected.** Each cache is keyed on ONE
absolute `eqPath` (`/mnt/projects/EverQuestP1999`, and the gfx one with a
trailing slash), and both are rejected outright when `eqPath_` does not match
byte for byte — so the seed is dead weight in any other tree, and in this one
it only ever survived because every run uses the same path. A tracked seed
also has to be re-written whenever it goes stale, which is the trap itself.

**Cold-rescan cost, measured (the plan's requirement).**

| cache | cold | warm | delta |
|---|---|---|---|
| `snd_index_cache.json` (17 `snd*.pfs`, 2,088 entries) | 613 ms | 221 ms | **~0.4 s** |
| `gfx_index_cache.json` (378 `*_chr.s3d`, 1 dir listing + filename arithmetic) | 31–57 ms | 12 ms | **~30 ms** |

Sound: `test_pfs_sound_load --gtest_filter=BuyItemLoadsFromPfs` end to end,
with and without the cache. Graphics: a purpose-built probe linking
`GraphicsArchiveIndex::buildIndex()` against the real EQ tree and the shipped
db (without the db it returns false and builds nothing — worth knowing). Both
on a warm NFS page cache; the FIRST touch of a cold NFS directory measured
17.4 s, which is the mount, not the scan.

**Surprise: `::access(dir, W_OK)` LIES on this NFSv3 mount.** The first
implementation used it and refused `build/bin/cache` — mode 0777, `mkdir`
succeeded, `touch` succeeded, `access` said no (the server's ACCESS RPC answers
for a different uid). Writability is now probed by create-and-unlink. Anything
that asks the filesystem a permission question on this dev host should assume
the same.

### Leg B — what the two dead files contained, and the decision

| file | contents | corresponds to anything? |
|---|---|---|
| `config/chat_settings.json` | 19 `channelFilters`, `showTimestamps`, chat window `x/y/width/height` | `channelFilters`: **no chat filtering exists** (zero hits). Geometry: no movable windows. `showTimestamps`: a `ChatPanelState`/`GameModeConfig` field **no file feeds** |
| `config/ui_settings.json` | per-resolution `x/y/w/h/visible` for 18 panels (bags, bank, buff, chat, group, hotbar, inventory, loot, pet, skills, spellbook, trade…), a `globalTheme` colour+layout block, `chatInput` colours/`maxHistory`/`blinkIntervalMs` | `UILayout` computes every rect from the screen size; there are no draggable windows; `globalTheme`, `maxHistory`, `blinkIntervalMs` appear in **no source at all** |

Provenance measured, not guessed: `git log -S'ui_settings.json' -- src include`
names **`0f25cb57` "U07c2: Delete WindowManager + all old UI window files
(~35K lines)"**. These are that deleted subsystem's saved state; they outlived
it by months and `CLAUDE.md` still called the first one live user settings.

**Decision: both DELETED.** They describe a UI the client no longer has, so
restoring them would configure windows that do not exist. The one live concept
they touch (`showTimestamps`) and the real gap they were masking are boarded as
**B255**: *the client persists no user settings at all*. The bytes stay
recoverable at `0f25cb57^`.

### Leg C — the ignored argument, and what happened to race_models.json

`bool loadRaceMappings(const std::string& jsonPath)` → `bool
loadRaceMappings()`; `renderer_init`'s fatal now reads *"Race model mappings
unavailable: no willeq.db is open (data/willeq.db failed to load)"*; S03's
`checkFile(... "config/race_models.json" ...)` is gone.

**`config/race_models.json` is NOT deleted** — the six-directory census
(`src/ include/ tests/ tools/ benchmarks/ scripts/ docker/`) shows it is the
**db builder's input**: `tools/willeq_db_builder.cpp:1353`
`loadRaceModels(db, configPath + "/race_models.json")` builds the `race_models`
table from it, and `scripts/generate_race_colors.py`,
`scripts/update_race_models_from_reference.py` and
`scripts/generate_s3d_reference_json.py` read it too. It is build-time source,
not a runtime file. That is not the plan's STOP (which was about the db
LACKING data the JSON has); it is the opposite finding — the db is built FROM
it, so only the RUNTIME requirement was ever wrong.

Proven live, no server:

| run | result |
|---|---|
| `config/race_models.json` moved away, graphics on | starts, graphics init completes, `[GRAPHICS] Race mappings using on-demand SQLite lookup`, runs the full 40 s |
| `data/willeq.db` moved away | fails naming the db: `failed to open data/willeq.db … (sqlite rc=14)`, then `[FATAL] [SPELL] Failed to load spell database` — **it dies before the renderer's check is reached**, so the corrected fatal is a defensive path, not the message a user sees |

### Tests

| test | pre | post |
|---|---|---|
| `DataPathRouting.IndexCachesAreNotWrittenUnderConfig` | **FAIL**, 4 sites | PASS |
| `PfsIndexCacheLocation.RescanWritesOutsideTheRepoConfigDir` | **FAIL** — it rewrote `config/snd_index_cache.json`, the exact executor trap | PASS |
| `CachePaths.WriteTargetIsNeverInsideTheCheckout` | new | PASS |
| `CachePaths.ExplicitCacheDirIsHonouredAndRoundTrips` | new | PASS |
| `CachePaths.DataRootCacheDirIsUsedWhenSet` | new | PASS |
| `DataPathRouting.EveryShippedConfigFileIsNamedByCode` | **FAIL**, naming both orphan files | PASS |
| `ApplicationInitOrderTest.ComposedStartupOrderMatchesThePreG04Golden` | **FAIL** (source 315 vs golden 314) | PASS |
| `RaceNameNoDatabaseTest.RaceMappingsFailOnTheDatabaseAndNothingElse` | new | PASS |
| `RaceNameFromDbTest.RaceMappingsBindToTheDatabase` | new | PASS |

Pre-fix reds were produced by exporting the source diff and `git apply -R`
(never `git stash`). Two of the new tests are red-by-compile before their
signature change, which is why Leg C's pre/post evidence rides on the golden.

The G04 statement-text golden **learned the deletion in place** (correction
32): entry 46 removed, 47–315 renumbered, header note added, `kGoldenSize`
315 → 314. No golden was regenerated.

`PfsIndexCacheLocation` forces the rescan by pointing `AudioManager` at an
ALIAS of the EQ tree (a temp directory of symlinks to the 17 `snd*.pfs`), so no
cache can match its `eqPath`; it then asserts that nothing under the repo's
`config/` appears, vanishes or changes.

`EveryShippedConfigFileIsNamedByCode` starts from the config DIRECTORY, not
from source literals — the only way to see a file nothing opens. B245 was
invisible to T399's census by construction.

### Verification

| step | result |
|---|---|
| `cmake --build build -j24` | **PASS** (exit 0) |
| `ctest --test-dir build -j6` | **4,142 / 4,144** — `visual_gate` and `PackReproducible`, both external (below) |
| `scripts/build-gles2-x86.sh` | **PASS** (exit 0) |
| visual gate standalone | **FAIL — not this branch** (below) |
| `scripts/build-arm-noble.sh` | **PASS** (exit 0) |
| `check_file_sizes.sh fable` | **18 checked, 18 ok, 0 warn, 0 fail** |
| DocCitations / DocClaims / DocCounts | all green after every leg |
| pixel goldens | untouched |

**The two reds are the shared EQE1 pack, not this branch.** `PackReproducible`:
the rebuild from this tree's inputs matches the pinned sha
(`e60962fb…`, 2,811,102 bytes) and the INSTALLED
`/mnt/projects/EverQuest_ETC1/willeq.eqe1` does not (`1078463f…`, 2,830,206
bytes, mtime 2026-09-05 23:09 — written by another slot; T400 owns the pack).
`visual_gate` fails only on `eqe1MappedBytes`, by **+19,343 in all three
memory-coverage zones** (qeynos2 119,944,431→119,963,774; gfaydark
118,610,523→118,629,866; befallen 113,095,019→113,114,362) — the signature of
one shared file every zone maps. Every other manifest field matches and all
three CPU p95 numbers are inside their ceilings (4.26/10.11, 5.16/10.67,
5.99/10.95). **Note for the board: B240's delta is no longer 239 bytes; the
same shared pack has moved again since T398, and the re-bless request is now
for +19,343.** Only Will re-blesses.

### Deviations and surprises

1. **`access(W_OK)` is unreliable on the NFS dev tree** (above). Cost one test
   run; the fix is create-and-unlink.
2. **A cache-name collision under `ctest -j6`.** `CachePaths.ExplicitCacheDir…`
   asserted a read MISS on `snd_index_cache.json` while a parallel
   `test_pfs_sound_load` was legitimately writing `<exe dir>/cache/` — the read
   side searches every candidate, so the "miss" became a hit. Fixed in
   `286fc71b` by round-tripping a name no other test writes. Green standalone
   and red under `-j6` is the tell for this class.
3. **The plan's Leg B option "the file is the unreached feature, board it"
   split in two.** The files describe a DELETED subsystem, so they were not
   worth keeping; but the feature gap behind them is real and is now B255. The
   B245 strike names both halves.
4. **Leg C's file survives for a reason the plan did not anticipate**: not
   "the db lacks its data" but "the db is BUILT from it". A runtime census
   alone would have deleted the db builder's input — `tools/` was the
   directory that mattered, exactly as B166 warned.
5. **Running the client no longer dirties the tree.** `git status` after a
   40 s graphics run and after the whole `ctest -j6` suite: clean.
