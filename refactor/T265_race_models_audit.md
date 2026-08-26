# T265 — audit every race_models.json row the way T262 audited three (+ race 330)

Slot: slot1. x86. NO eqemu (data-level; the pixel checks are owed to a
later eqemu unit). Class A. Territory: data/race_models.json source,
tools/willeq_db_builder race_models path, data/willeq.db (rebuild with
T246/T262-style proof), tests (`WilleqDatabaseShippedContentTest`). Do NOT
touch the EQE1 resolver code, inventory, or anything T264 (slot3) may
delete — T264 is a reachability sweep across src/; keep to data + builder.

## Method (T262's comparison, applied to ALL rows)
For every race id in race_models.json (and every id eqemu's `Race::` enum
can send for Titanium — the board's T213 note lists the 209 client model
codes): (1) does the row exist; (2) is `s3d_file` non-empty and does that
archive exist in BOTH the S3D tree (/mnt/projects/EverQuestP1999) and the
EQE1 tree; (3) does the archive contain the model code's mesh (T262's
`s3d_dump` scan); (4) do all referenced textures resolve through T207's
index against the T199 candidate set for at least one zone where the race
spawns (spawn tables in the eqemu DB dump if available offline; else the
zone list in `also_in`). Produce a table: race | code | s3d_file | mesh
found | textures resolved | verdict.

## Fixes
- Empty/wrong `s3d_file` → fix at the source like T262 (the archive that
  holds mesh + textures).
- Race 330 (the old "placeholder cube in gfaydark" item): decide by the
  table — T215 found it has had a row since 35c51be2; verify the row
  resolves; if it does, close the item with the evidence.
- Races with NO possible model (post-Titanium, like T213's 415/457) →
  alias to the nearest model as T213 did, listed explicitly.
Rebuild willeq.db with the diff proof; extend `WilleqDatabaseShippedContentTest`
to pin "every race row's s3d_file exists and contains its code" so this
cannot regress silently.

## Verification
Full ctest; Review with the table; evidence review/T265/. Commit:
`T265: every race model row verified against the archives -- <n> fixed`.

## Review (2026-08-26, slot1, off fable 2dcabeb7)

Evidence: /mnt/projects/fable/review/T265/ (README.md indexes every file; the
full 190-row table is `audit_table_after.md`, the shipped-file table
`audit_table_before.md`).

### Counts
| | rows |
|---|---|
| rows audited (shipped race_models.json) | 187 |
| correct on every check (archive in both trees, holds the code, all textures resolve, also_in sane) | 182 |
| wrong | 5 (17, 72, 240, 330, 32) |
| fixed at the source | 4 (17 Golem, 72 Ship, 330 playable Froglok, 240 Teleport Man) |
| of which aliased (T213 pattern) | 1 (240 -> 127 Invisible Man, IVM) |
| unfixable | 1 (32 Ghost: GHM/GHF in NO archive; nothing correct to name) |
| rows added (classic ids with no row whose model code the client ships) | 3 (35 Eel EEL, 114 Ghost Ship GSP, 141 Boat BOAT) |
| rows after / verdict OK after | 190 / 189 (+ race 32 documented) |
| eqemu Race:: ids still without a row | 551: 14 classic..velious, 166 luclin..ldon, 371 post-Titanium |

### Method (the plan's four checks, all 187 rows, `audit_race_models.py`)
(1) row exists; (2) `s3d_file` non-empty, present in /mnt/projects/EverQuestP1999
AND in the EQE1 tree (the 1008 archives of `data/eqe1_texture_index.json`);
(3) the archive holds `<CODE>_HS_DEF` or `<CODE>_ACTORDEF` -- from T262's
s3d_dump scan of all 419 `*_chr*.s3d` (431 distinct codes); (4) every texture
that mesh references resolves through T207's index against the T199 candidate
set (`{zone}`,`{zone}_obj`,`{zone}_chr` + 33 globals + the row's archive) for
the best of the zones that expect the code -- the client's own `*_chr.txt`
lists (T262's 2188 rows) plus the zones named by `s3d_file`/`also_in`. Spawn
tables were not available offline (eqemu DOWN, no dump under akk-stack), so
the client's per-zone lists stood in for them. Extra: `also_in` entries exist
and hold the code; secondary codes exist somewhere.

Result on the shipped file: 184/187 archives exist in BOTH trees (the 3
misses are the empty rows); 182 rows hold their code and resolve every
texture in at least one expected zone; ZERO rows with an unresolved texture.

### The five wrong rows
- **17 Golem**: `code` GOM, `s3d_file` befallen_chr.s3d -- befallen holds GOL,
  not GOM. GOM is the Velious/PoP golem (9 archives: kael, skyshrine, sleeper,
  velketor, frozenshadow, thurgadinb, pofire); GOL the classic golem (43:
  befallen, cauldron, unrest, najena, ...). At runtime a neuter golem asks for
  "GOM", misses befallen/zone/globals, and the directory scan hands it the
  Velious mesh from a foreign archive whose textures are outside the T199
  candidate set. Fixed: code GOL; `also_in` rebuilt from the GOL holders.
- **72 Ship**: `s3d_file` timorous_chr.s3d holds LAUNCH/LAUNCHM (the Timorous
  Deep launches), no SHIP. SHIP_ACTORDEF is in oot_chr(+2), butcher, erudnext,
  erudsxing, freporte, qeynos _chr. Fixed: oot_chr.s3d + `also_in`.
- **330 playable Froglok** (the "placeholder cube in gfaydark" item): row exists
  since 35c51be2 but with NO `s3d_file`. FRM_HS_DEF/FRF_HS_DEF exist in exactly
  ONE archive, globalpcfroglok_chr.s3d (12/12 textures resolve; in the EQE1
  tree). Fixed at the source. **VERDICT: does not resolve today, and the data
  fix alone is not enough** -- two loader facts, both src/ (out of this unit's
  territory, T264 is sweeping src/), block it:
  1. `race_codes.cpp` `getRaceModelFilename()` hardcodes race 330 ->
     "globalfroglok_chr.s3d", which holds FRG (the monster froglok) only, so the
     archive-index path (`graphics_archive_index.cpp`) opens the wrong archive.
  2. `race_model_loader.cpp` step 1 (the `s3d_file` path) special-cases names
     starting with "global": "global" -> global_chr, otherwise
     `stoi(name.substr(6))` -> numbered global; "globalpcfroglok" throws, gets
     globalNum 0, and loads NOTHING. Then `searchZoneChrFilesForModel()` skips
     `global[a-z]+_chr` by regex. So no path reaches globalpcfroglok_chr.
  The item stays OPEN with the data half done; the fix is two lines (330 ->
  globalpcfroglok in `getRaceModelFilename`, and the non-numeric-global branch
  falling through to `loadModelFromCachedChr`), plus an eqemu pixel check.
- **240 Teleport Man**: `code` TPM, no `s3d_file`, `invisible: true`, scale 0.
  No TPM/TPF mesh in any archive (and `invisible` has no reader in src/ -- the
  column is written and loaded into RaceModelInfo, nothing consults it). It is
  an invisible trigger NPC; aliased to 127 Invisible Man (IVM/IVF, global_chr)
  with `alias_of`/`alias_reason` exactly as T213 did for 415/457.
- **32 Ghost**: `code` GHM/GHF, no `s3d_file`. No GHM mesh in any of the 419
  archives, and none of the 209 unclaimed scan codes is a ghost. What the
  official client shows for race 32 is unknown to this unit; left unchanged,
  documented as the one exception in the pinning test.

### Added rows (flagged: race<->code by model NAME, mesh verified, no spawn seen)
The 17 classic..velious eqemu ids with no row were matched against the 211
scan codes no row claims. Three have an unambiguous model: 35 GiantEel
(GetRaceIDName "Eel") -> EEL_HS_DEF in eel_chr.s3d (14/14 textures; the
client's hole/paw/tak* lists name eel_chr); 114 GhostShip -> GSP_ACTORDEF in
oot_chr/oot_chr2 (18/18); 141 Boat -> BOAT_ACTORDEF in global_chr (2/2). Scale
for the two boats copied from row 72 (2.0) -- a guess, flagged. The other 14
(0 Doug, 84 SnakeElemental, 97 DaisyMan, 115 Clam, 132 Draglock, 142
MinorIllusion, 143 Tree, 152 Bertoxxulous, 179/180 Unknown, 182 Faun, 186
Hippogriff, 192 ClockworkDragon, 197 RonnieTest) have no matching code and are
listed, not invented. The 166 luclin..ldon ids (199-366) are Luclin+ content:
Titanium ships their models (the unclaimed CST/RTN/TAR/... codes) but this
client targets P1999-era servers where they never spawn, and no authoritative
id->code source exists offline -- NOT mapped, listed in the table. The 371
post-Titanium ids have no model by definition; only ones seen spawning (T213's
415/457) get aliases.

### Not fixed, on the record (inert or unverifiable)
- 13 rows carry a `female_code`/`neuter_code` that exists in NO archive (FPF,
  BGF, DVF, HHF, NGF, EGF, CLN, KAN, GDF, IVF, SIF, GHF, TPF). The columns are
  inert: the runtime never reads them (`getGenderedRaceCode` derives F from the
  primary code). A female spawn of 55/65/117/147 (no `fallback_code`) would
  still miss -- a `fallback_code` cannot help because `getFallbackRaceCode`
  re-applies M->F to it. Listed under "OK (notes)"; needs a live female spawn
  to know what the official client does.
- 6 rows list `also_in` archives that do not hold the code (15, 42, 71, 112,
  118, 139); `also_in` has no reader, left alone.
- SHIP/GSP/BOAT are ACTORDEFs (non-skeletal). Whether `RaceModelLoader`
  builds a race mesh from an ACTORDEF is a loader question, not answered here.
- The T213 board note said "209 client model codes, all claimed" -- that was
  `s3d_contents_reference.json`; the s3d_dump scan of the actual archives has
  431 codes, 209 of them claimed by no row after this unit (the Luclin+ set
  T262 counted as 204 no-race codes plus the ones above).

### Changes
- `config/race_models.json` (`_version` 1.3): rows 17, 72, 240, 330 fixed; 35,
  114, 141 added; each carries a `note`/`alias_reason` naming the evidence.
  The builder's hand parser ignores unknown keys (verified by the rebuilt row
  values) -- note text avoids `}` and `"`, which that parser cannot survive.
- `data/willeq.db` rebuilt (`builder_output_after.txt`); row-level diff vs the
  shipped file (`db_diff_before_vs_after.txt`): race_models 4 changed / 3
  added, metadata build_date/total_entries, sqlite_stat1 count; every other
  table identical row-for-row, no schema/index change. Schema version not
  bumped (a v4 reader sees the same columns).
- `tests/test_willeq_database.cpp` + `tests/CMakeLists.txt` (pfs.cpp + zlib
  linked in): `WilleqDatabaseShippedContentTest.EveryRaceRowNamesAnArchiveHoldingItsCode`
  -- opens every row's `s3d_file` from the client tree and reads the model
  names straight from the WLD string hash (PfsArchive + the XOR key), requiring
  `<CODE>_HS_DEF`/`_ACTORDEF`; race 32 is the one listed exception and the
  test fails if it ever gains an archive without the list being updated. Plus
  `T265RaceRowFixesAreInTheDb` pinning the 7 rows by name. RED against the
  shipped db (`test_red_before_db_rebuild.txt`: 17, 72, 240, 330 and the
  missing 35/114/141), GREEN after (`test_green_after_db_rebuild.txt`: 16/16).
- Builder code untouched (no new columns needed).

### Verification
- build: ok (`cmake --build build -j24`).
- ctest -j24: **2973/2974** (`ctest_full.txt`; the one failure is the
  documented ALSA AudioManagerTest.PreloadCommonSounds; visual_gate PASSED
  21.15 s, goldens untouched -- it exercised the rebuilt db through a real
  zone load).
- `check_file_sizes.sh fable`: 1 file checked, 0 warn, 0 fail.
- Not run (plan's bar is "Full ctest"): build-gles2-x86.sh, build-arm-noble.sh.
- Not done (no eqemu): pixels for any of the 7 rows -- owed to the eqemu unit
  the plan names; race 330 additionally needs the two loader lines above.

### Deviations
- One early shell command in this unit used `cd` (protocol forbids it); it was
  a read-only python inspection, nothing depended on it, every later command
  passed paths as arguments.
- Rows added (35/114/141) and the 240 alias go beyond "fix empty/wrong
  s3d_file" -- they are the plan's "row exists" check and its T213 clause;
  each is flagged as an inference in the file itself.
- New `also_in` lists name only `_chr` archives (the scan's holders); older
  rows also list zone geometry archives. No reader either way.
