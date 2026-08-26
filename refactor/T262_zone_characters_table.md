# T262 — zone_characters table has no reader (reachability #14, T261)

Slot: slot1. x86. NO eqemu (a benchmark/zone load with the EQE1 request
log is the evidence). Class A. Territory: src/client/willeq_database.*
`getZoneCharacters`, tools/willeq_db_builder.cpp zone_chars path, the
EQE1 archive resolver (`openArchiveContaining` / `eqe1_texture_index` /
renderer_effects archive selection — T207/T199/T214 territory), tests.
Do NOT touch the zone-load snapshot / region lookup (T260 slot2),
inventory (T259 slot3).

## Finding
`WilleqDatabase::getZoneCharacters()` is the only reader of
`zone_characters` (2188 rows: zone_name, model_code, source_file) and has
ZERO callers. The table was meant to tell the loader which `*_chr` archive
holds a zone's character models (the `source_file` hint). Today T199's
spawn-driven archive selection + T207's name→archive index decide this at
runtime, and T214's audit found ZERO NOT-FOUND — so the table may be
redundant.

## Scope — decide by evidence
1. Compare: for each zone, the set of archives T207's index would open for
   that zone's `zone_characters` model codes vs. the `source_file` hints.
   If the index already resolves every code (expected after T214's
   NOT-FOUND=0), the table is redundant → DELETE table, builder path,
   reader, tests, and the `*_chr.txt` source parsing if nothing else uses
   it. Record the comparison.
2. If any code resolves ONLY via the hint, wire `getZoneCharacters()` into
   the resolver as a pre-open hint for the current zone (rule R1/R2 in
   deferred.md STATUS — it must not open archives nothing requests), test
   it, and show the EQE1 request log before/after for that zone.

## Verification
Full ctest; visual gate (loader touched); Review; evidence review/T262/.
Commit: `T262: zone_characters table deleted (index resolves every code)`
or `T262: zone_characters hints wired into the EQE1 resolver`.

## Review (2026-08-26, slot1, off fable 9ded15dd)

Evidence: /mnt/projects/fable/review/T262/ (README.md indexes every file).

### Decision: option 1 -- DELETE. Plus one data fix the comparison surfaced.

`zone_characters` is gone (table, builder loader, reader, struct, prepared
statement); `data/willeq.db` rebuilt without it. The comparison found exactly
one thing the hints knew that the runtime did not, and it is a hole in
`race_models.json`, not a reason to keep the table: races 195/196/198 (BWD,
GDR, PRI -- the three dragons) had NO `s3d_file`, so the T199 candidate set
never contained their archive and every one of their textures would have
been NOT-FOUND. Fixed at the source (`s3d_file` + `also_in`), which holds in
every zone, where the hint would have covered only skyfire and veeshan.

### How the comparison was done (`compare_hints_vs_runtime.py`)
The plan framed it as "T207's index vs the hints", but the hint is a MESH
archive (`{x}_chr.s3d`), and the runtime resolves meshes and textures by
different mechanisms, so both were modelled, for all 2188 rows:
- mesh: `race_models.s3d_file` -> `{zone}_chr` -> `global_chr`, `global2..7_chr`
  -> directory scan of every `*_chr.s3d` (`searchZoneChrFilesForModel`, skips
  `global[a-z]+_chr`). Which archive holds which `<CODE>_HS_DEF` came from
  `s3d_dump` over all 419 `*_chr*.s3d` (2881 HS_DEF + 24 ACTORDEF headings).
- textures: the textures the chosen mesh's WLD references (per-model list
  from the same dump), resolved through `data/eqe1_texture_index.json` against
  the zone's T199 candidate set (`{zone}`,`{zone}_obj`,`{zone}_chr` + the 33
  `globalArchives` + the row's race `s3d_file`), in configured order --
  exactly `openArchiveContaining()`.

### Numbers (`comparison_summary.txt`, `per_zone_hint_vs_t207.txt`)
| | rows |
|---|---|
| total rows / zones / distinct codes | 2188 / 254 / 278 |
| **code belongs to NO race in race_models** (unreachable by any spawn) | **1864** |
| rows whose hint names an `.eqg` (EQG model; this client loads S3D/WLD only) | 583 (14 of them spawnable: `bat`, whose race resolves to befallen_chr anyway) |
| rows whose hint names a file that does not exist (`tem_cht`, `dru_chr`, ...) | 12 (4 spawnable, all resolve via race `s3d_file`) |
| spawnable rows (code maps to a race) | 324 |
| spawnable, mesh found before the directory scan, all textures resolve | 317 |
| all rows: hint archive == the archive the runtime picks anyway / differs / hint archive does not even hold the mesh | 1059 / 488 / 728 (note: 641 hints point at a model the client cannot load) |
| spawnable, **textures resolve ONLY via the hint archive** | **6** = skyfire/veeshan x bwd/gdr/pri |
| spawnable, no S3D mesh anywhere | 1 (nektulos `ngf`; the hint `neriaka_chr` does not hold it either) |
| hint archive already in the zone's candidate set | 4 of 2188 |

The 1864 no-race rows are the Luclin+ model codes Titanium ships (`cst`, `rtn`,
`tar`, `box`, `brl`, ...: 204 codes) that `getRaceCode()` -- SQLite
`race_models` plus its 100-case hardcoded fallback, none of which name these
codes -- can never produce, so no spawn reaches them. The hint table's
knowledge of them is inert regardless of what the resolver does.

So: hints are never consulted, and for every spawnable row except the 6
dragon rows they name what the runtime already picks or an archive it does not
need. Case 2 of the plan ("resolves ONLY via the hint") is those 6 rows, and
they resolve only via the hint because `race_models.json` was blank for the
race -- wiring 2188 rows of per-zone hints into `openArchiveContaining()` to
patch three missing `s3d_file` values would have been the wrong fix.

### Changes
- `include/client/willeq_database.h` / `src/client/willeq_database.cpp`:
  `ZoneCharacterEntry`, `getZoneCharacters()`, `stmtGetZoneChars_` removed.
- `tools/willeq_db_builder.cpp`: `loadZoneCharacters()` (the only `*_chr.txt`
  parser in the tree) and its call removed. 1569 -> 1427 lines (clears the
  T258/T261 WARN).
- `config/race_models.json`: 195 BWD -> `bwd_chr.s3d`, 196 GDR -> `gdr_chr.s3d`,
  198 PRI -> `pri_chr.s3d` (the dedicated single-model archives; smallest
  owners; present in both the S3D tree and the EQE1 tree, and each holds all
  23/23/19 textures the mesh references), with `also_in` from the scan.
- `data/willeq.db` rebuilt (`builder_output_after.txt`). Row-level diff vs
  the shipped file (`db_diff_before_vs_after.txt`): `zone_characters` + its
  2 indexes gone; `race_models` 3 rows changed (the `s3d_file` column only);
  `metadata` build_date/total_entries; every other table identical row-for-row.
  Schema version NOT bumped: a v4 client that never selects from the table
  cannot tell the difference -- flagged, orchestrator's call.
- `tests/test_willeq_database.cpp`: `WilleqDatabaseShippedContentTest`
  x2 -- `ZoneCharactersTableIsGone`, `DragonRacesNameTheirChrArchive`.
  RED against the shipped db (`test_red_before_db_rebuild.txt`: 2 FAILED,
  s3dFile "" for all three), GREEN after (`test_green_after_db_rebuild.txt`:
  14/14).

### Verification
- build: ok (`cmake --build build -j24`).
- ctest -j24: **2960/2961** (`ctest_full.txt`; +2 new tests over T261's
  2958/2959; the one failure is `AudioManagerTest.PreloadCommonSounds`).
  visual_gate PASSED 20.9 s, goldens untouched. No loader CODE changed; the
  gate exercised the rebuilt db and race_models through a real zone load.
- `build-gles2-x86.sh`: ok. `build-arm-noble.sh`: ok (compiles the builder).
- `check_file_sizes.sh fable`: 0 FAIL. It reports 3 WARNs in files this unit
  did NOT touch (eq.h, application.cpp, eq_bridge.cpp) because `fable` moved
  on (T260 merged) after this branch was cut from 9ded15dd; the unit's own
  files are all `ok`.
- Not done (no eqemu): a live dragon spawn. The dragon fix is proven at the
  data level (rows, archives, texture ownership) and by the resolver's rule,
  not by pixels -- flagged.
- Deviation: one build invocation early in the unit piped `cmake --build`
  output through `tail` (protocol forbids it); the build was re-run clean by
  the later `build-gles2-x86.sh` / ARM steps and every ctest number above
  comes from unpiped runs.
