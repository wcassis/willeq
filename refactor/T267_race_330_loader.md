# T267 — race 330 (playable Froglok) cannot load: loader hardcodes the monster archive and stoi()s the s3d_file name (T265)

Slot: slot1. x86. OWNS eqemu briefly (noclobber; T266 slot2 may hold it —
bounded wait) for the pixel check. Class A. Territory:
src/client/graphics/eq/race_codes.cpp `getRaceModelFilename()`,
src/client/graphics/eq/race_model_loader.cpp (s3d_file branch + directory
scan regex), tests. Do NOT touch renderer_effects.cpp / the EQE1 resolver
(T266 slot2), action/bridge/eq_*.cpp (T264 slot3), race_models.json / db
(T265 merged — the data is now correct: 330 → globalpcfroglok_chr).

## Defect (T265 Review)
1. `getRaceModelFilename()` hardcodes 330 → `globalfroglok_chr.s3d` (holds
   FRG, the monster froglok), ignoring the row's `s3d_file`.
2. `race_model_loader.cpp`'s s3d_file branch does `stoi("pcfroglok")` for
   any non-numeric `global*` name → exception/zero → loads nothing.
3. The directory scan's regex skips `global[a-z]+_chr` archives.
So 330 renders a placeholder even with correct data.

## Scope
1. Make the row's `s3d_file` authoritative (drop the 330 hardcode; keep the
   generic `global{N}_chr` numeric path working); parse non-numeric global
   names as plain archive names; let the scan include `global[a-z]+_chr`.
   Check no other hardcoded race→archive mapping in `getRaceModelFilename`
   contradicts a row (T265's table) — fix any that do.
2. Test: race 330 resolves to `globalpcfroglok_chr` and the loader finds
   FRM/FRF (offline, S3D data present); a numeric global name still works.
   Red/green.
3. Live: `#npctypespawn` a race-330 NPC (or `#race 330` on Testchar if the
   GM command works) with `--eqe1-path` → placeholder=false, non-zero
   vertices, 0 magenta, screenshot. Delete the spawn row after (T263's
   `zone_state_spawns` mechanism); restore Testchar.

## Verification
Full ctest; visual gate; Review; evidence review/T267/. Commit:
`T267: race 330 loads the playable froglok archive`.

## Review (2026-08-26, slot1, off fable 1e06b145)

Evidence: /mnt/projects/fable/review/T267/ (README.md indexes every file).

### The fix (three places, all in this unit's territory)
1. `race_codes.cpp` `getRaceModelFilename()`: the 330 -> `globalfroglok_chr.s3d`
   hardcode is gone (that archive holds FRO/FRG, the monster frogloks -- s3d_dump
   confirms; FRM/FRF are in `globalpcfroglok_chr.s3d` only). The function now
   returns the race row's `s3d_file` whenever it names a per-race global archive
   (`global[a-z]+_chr.s3d`, new helper `isRaceSpecificGlobalArchive()`); rows
   naming `global_chr.s3d` / `global{N}_chr.s3d` / zone archives keep the
   Luclin-convention `global{code}_chr.s3d` name (that is what the archive
   index and the new-models path key on, and what the 38 pre-existing
   `RaceModelLoaderTest` cases pin). Effect on `graphics_archive_index.cpp`:
   330 is now indexed to `globalpcfroglok_chr.s3d` instead of the FRG archive.
2. `race_model_loader.cpp` s3d_file branch: only an all-digit stem is a numbered
   global; any other `global*` name (e.g. `globalpcfroglok`) goes through
   `loadModelFromCachedChr()` like a zone archive. Previously `stoi("pcfroglok")`
   threw, globalNum stayed 0 and the branch loaded nothing.
3. `model_loading.cpp` `searchZoneChrFilesForModel()`: the `global[a-z]+_chr`
   skip regex is removed (and `<regex>` with it -- that was its only use in the
   TU). The scan is the new-models-mode last resort; it can now reach the one
   archive that holds FRM/FRF.

### Other hardcodes vs T265's rows (checked, not changed)
- `getRaceModelFilename()` had exactly ONE race hardcode (330). Gone.
- `getRaceCode()`'s fallback switch (reached only when the SQLite instance is
  null -- test_graphics runs that way) contradicts the rows wholesale: it is a
  pre-JSON numbering (17 ORC vs row GOL, 35 RAT vs EEL, 72 ELE vs SHIP, 13 WOL
  vs AVI, 14 BEA vs WER, 46 GOB vs row 40, 85 DRA vs row 49, 127/240 INV vs
  IVM, ...). It is inert at runtime (the db is always open) and 38 tests pin
  its values; out of this unit's scope, flagged for a decision (delete it or
  regenerate it from the rows).
- `getFallbackRaceCode()` / `getZoneSpecificRaceCode()` hardcodes (71/15/80/82
  -> HUM etc.) are the same class; untouched.

### Tests (red/green measured)
`tests/test_race_330_loader.cpp` (new TU, EDT_NULL device + the shipped
`data/willeq.db` as the global race source + the real client tree; skips
without either):
- `Race330Test.ModelFilenameFollowsTheRow`: 330 -> `globalpcfroglok_chr.s3d`
  for both genders.
- `Race330Test.NumberedGlobalRowsKeepTheConventionName`: 128 (row
  global4_chr) -> globalikm/globalikf; 1 (row global_chr) -> globalhum.
- `Race330LoaderTest.LoadsFrmAndFrfFromGlobalPcFroglok`: old-models mode,
  `ensureModelDataLoaded(330, 0/1)` returns FRM / FRF model data with
  vertices.
- `Race330LoaderTest.NumberedGlobalRowStillLoads`: 128 still loads IKM via
  the numeric path.
RED before the fix (`test_red_before_fix.txt`): 2/4 fail -- filename was
`globalfroglok_chr.s3d`, and 330 male "loaded nothing". GREEN after
(`test_green_after_fix.txt`): 4/4. Not unit-tested: the scan-regex removal
(only reachable in new-models mode for a race whose code is known but whose
archive is neither the convention name nor a global/zone archive -- no such
race exists with the db loaded).

### Verification
- build: ok (`build_after_fix.log`).
- ctest -j24: **2977/2978** (`ctest_full.txt`; the one failure is the
  documented ALSA AudioManagerTest.PreloadCommonSounds). Baseline was 2973
  at T265 + 4 new here + T264's additions are NOT in this branch (cut before
  T264 merged at 23251315).
- `build-gles2-x86.sh`: ok. Visual gate: **PASS**, goldens untouched
  (`visual_gate.txt`; `git status` shows only the four source files + the new
  test).
- `check_file_sizes.sh --files` on the five touched files: 0 fail (2
  pre-existing WARNs, model_loading.cpp 1815 and tests/CMakeLists.txt 2174,
  both already over 1500 before this unit; this unit removed 4 lines from the
  former and added 15 to the latter). Note: `check_file_sizes.sh fable` from
  this branch now diffs against a fable that has since advanced (T264), so it
  lists T264's files too -- the explicit-file run is the meaningful one.
- Not run: build-arm-noble.sh (plan's bar is full ctest + visual gate).

### Live (eqemu, lock taken noclobber after T266 released it; released after)
T266 had not merged, so a post-load spawn of a race absent at zone load is
still FATAL in this binary. Two runs (README in review/T267/): run1 GM-spawns
two REAL race-330 npc_types (22117 Soulbinder_Jubbl male, 35057 Scout_Miigle
female) at Testchar's befallen spot -- region SIZE_MAX, where post-load
entities never build (T263), so no texture lookup, no FATAL -- and camps;
run2 zones in with both in the spawn list AT LOAD:

    preloadModelData: archive cache MISS for globalpcfroglok_chr.s3d -- loading from disk (race=330 gender=0)
    race=330 MATCHED character='FRM_HS_DEF' ... code=FRM vertices=1169 anims=1 vpieces=101
    race=330 MATCHED character='FRF_HS_DEF' ... code=FRF vertices=1159 anims=0 vpieces=99
    decoded 12 textures for race=330 gender=0 (notFound=0 null=0 empty=0); preload took 378ms success=true
    EQE1Loader: mmap'd globalpcfroglok_chr.eqe1; EQE1-USAGE: globalpcfroglok_chr hits=23
    [NPC] 'Soulbinder_Jubbl000' placeholder=false   [NPC] 'Scout_Miigle000' placeholder=false

`froglok330_stageA_testchar_spot_befallen_800x600.png`: both frogloks
rendered from the playable archive, 0 magenta of 480000. FATAL lines: 0.
Stage B (a cleaner frame from the safe point) failed: a putrid skeleton
killed Testchar (level 2, #invul off in run2) -- that frame is not evidence.

Hygiene: server down.sh; with mariadb alone, deleted the two persisted
zone_state_spawns rows (npc 22117/35057, spawn2_id=0, zone 36), deleted
Testchar's EMPTY run2 corpse (id 32, 0 items; T266 WRITE 7 precedent),
restored Testchar to zone 36 / -77.9232 / 21.9086 / 3 / h190 (she had
respawned at bind in qeynos); inventory and level unchanged. Lock deleted.

### Deviations
- Branch cut from fable 1e06b145 (the T267-plan tip named by the dispatch);
  fable advanced to 23251315 (T264) during the unit. No overlap with T264's
  territory (action/bridge/eq_*); orchestrator merges.
- run2 stage B killed Testchar (my #invul was only on in run1). Cleaned up
  as above; one extra DB write (the empty corpse).
- Plan's Scope 1 said "make s3d_file authoritative" -- implemented as
  authoritative for per-race global archives only (see "The fix" 1); making
  it authoritative for every row would have re-pointed the archive index for
  the 13 playable races at global_chr/global{N}_chr and broken 38 pinned
  tests for no gain (the loader's step 1 already honours those rows).
- Scan-regex removal has no unit test (no reachable case with the db loaded);
  it is exercised by nothing live either. Kept because the plan asks for it
  and the skip was wrong by construction.
