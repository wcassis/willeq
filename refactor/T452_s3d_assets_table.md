# T452 — the armor-texture lookup reads a table the shipped db never had (B468)

Class A (unconfirmed live), slot2, no server. Read B468 (T450's find:
`getAssetLocation()` / `getAssetsInArchive()` select from `s3d_assets`,
created only under `--s3d-index`, which nothing passes; reached from
`RaceModelLoader::getArmorTexture()` at `mesh_building.cpp:210` and
`:273`), T450's §Review (the db pin recipe `scripts/build_willeq_db.sh`,
`DbReproducible`, the input table in `main()`), the EQE1 asset rule
(all-or-nothing; where armor textures actually live in the pack — read
the pack builder and `equipment_textures` table), and the armor-texture
callers.

1. Measure offline: with the shipped db, what does `getArmorTexture()`
   return for a real armor material on a real race (a headless test on
   the pack + db)? If the EQE1 pack already resolves armor textures by
   another route and the `s3d_assets` query is a dead fallback, delete
   the query, the `--s3d-index` mode and the table code (stale doc =
   defect), and pin. If the query is the live route and it fails, the
   builder generates the index from the inputs it already has (no new
   external file), the db is rebuilt and re-pinned under T450's recipe
   (one commit: db + sha256), and a test proves the lookup succeeds.
2. Either way: a pre-fix failing test on the exact call
   `getArmorTexture()` makes, against the shipped db.
3. Docs: the builder header, CLAUDE.md's db table list, module_map.

## Proof

Build (background), `ctest -j6` (no expected reds), pre-fix/post-fix,
`DbReproducible` green with the new pin if rebuilt, visual gate
standalone + ARM (if armor textures start resolving, an image golden
MAY move — that is a STOP with the contact sheet, not a bless), file
sizes, doc nets. Strike B468; new items B469+ (T451/T431 allocate from
B468+ — say which you used).

STOP: an image golden moves.

## Review

**Branch** `task/T452-s3d-assets`, base `d88b9063` (fable tip, verified at
checkout). Slot2, `build/` RelWithDebInfo, GLES2 Irrlicht from the slot's own
`third_party/irrlicht-gles2-x86`. No server; T431 holds the eqemu lock and
nothing here opened a connection.

### The measurement, and the answer to B468's open question

**What `getArmorTexture()` returns on the shipped db: `nullptr`, for every
name, always.** Not inferred -- the query itself does not run:

    $ sqlite3 data/willeq.db "SELECT archive FROM s3d_assets WHERE name='qcfch0101.bmp'"
    Error: in prepare, no such table: s3d_assets

`getAssetLocation()` therefore returns `found=false` from a MISSING TABLE for
every input, and `getArmorTexture()` returns `nullptr` before it opens
anything. That has been true since the route was written on **2026-03-21**
(`ca267770`, "Switch to global WilleqDatabase instance, on-demand lookups") --
about six months.

**B468's unmeasured half is now measured, and it is a LIVE defect, not dead
code.** Against `/mnt/projects/EverQuestP1999`:

| body-texture variant | where it lives | already loaded? |
|---|---|---|
| 00-04 | `global_chr.s3d` | yes, `globalTextures_` |
| 05-09 | nowhere (no such art) | n/a |
| 10-16 (robes) | rewritten by `mesh_building.cpp` to `clk<nn><page>.bmp`, in `global_chr.s3d` (42 pages) | yes |
| **17-23** | **`global17_amr.s3d` .. `global23_amr.s3d`, 1,784 textures** | **NO -- nothing else in the client opens these seven archives** |

`mesh_building.cpp:210` builds the name `{race}{part}{variant:02d}{page:02d}.bmp`
from the server's `bodyTextureVariant` and asks `isTextureAvailable()`. For
tier 17-23 the merged map cannot have it and `getArmorTexture()` was the only
other route, so the override was declined and **the whole Kunark/Velious armor
tier rendered as the base skin**. `loadArmorTextures()`, whose comment still
said "global17-23_amr.s3d contain high-tier armor textures", had been gutted
to a flag-setter in the same 2026-03 commit.

Two supporting measurements:

- Each `*_amr.s3d` holds its own variant and nothing else -- with **one
  exception**: `haft2202.bmp` is in `global20_amr.s3d`, not `global21..23`.
  (Censused all seven: 289/291/289/290/291/289/45 matching textures, plus
  `palette.bmp` and the archive's own `.wld` in each.)
- **All 1,784 names are in `data/eqe1_texture_index.json` and all seven
  archives have `.eqe1` packs** (0 missing). So making the lookup succeed
  cannot turn a silent fallback into `ConstrainedTextureCache::getOrLoad()`'s
  FATAL + `exit(1)` on an EQE1 miss. Checked before writing the fix.

### The decision -- and a deliberate deviation from the plan

The plan offered two routes. **Neither was taken**, and the deviation is
flagged rather than absorbed:

- *Populate the table* would put a ~50k-row scan of every S3D into the one
  shipped file that has a reproducibility pin, to duplicate a name->archive
  index the client **already ships and already uses**
  (`data/eqe1_texture_index.json` / `EQE1TextureIndex`, 50,972 entries, T207).
  B468 itself says not to grow the database for an unproven consumer.
- *Delete the route* would leave the armor tier broken permanently.

Instead the archive is **derived from the texture name**:
`ArmorArchivesForTextureName()` (`src/client/graphics/eq/model_loading.cpp`)
reads the four digits before the extension, and if the variant is 17-23
returns `global<variant>_amr.s3d` **followed by the other six** -- the fallback
exists precisely because of `haft2202.bmp`, and a test asserts it. A name that
is not a 17-23 armor texture returns an empty list, so the common case opens
nothing at all and is cheaper than the SQL query it replaces.

No database, no index file, nothing new shipped, and it works in both EQE1 and
S3D-only mode. **`data/willeq.db` was NOT rebuilt and its pin is untouched**:
T450's `input_digest` only gained the `s3d_index` input when `--s3d-index` was
passed, which no recipe does, so removing the flag cannot move the shipped
bytes. `DbReproducible` confirms it (Passed, 2.17 s, in the full run).

### What was deleted

| what | where |
|---|---|
| `getAssetLocation()`, `getAssetsInArchive()`, `struct AssetLocation`, `stmtGetAsset_`, `stmtGetAssetsInArchive_` | `src/client/willeq_database.cpp`, `include/client/willeq_database.h` |
| `loadS3dIndex()` + the `s3d_assets` DDL and its two indices | `tools/willeq_db/db_loaders_json.cpp` (607 -> 443 lines), `tools/willeq_db/db_builder.h` |
| the `--s3d-index` flag, its usage line, its input row and its load block | `tools/willeq_db_builder.cpp` |
| `tools/s3d_index_builder.cpp` + its CMake target | its `.json` fed only that flag; its `.bin` has **no runtime reader anywhere** (grepped `src/ include/ tests/ tools/ benchmarks/ docker/ scripts/`) |
| `loadArmorTextures()` and `armorTexturesLoaded_` | a flag-setter with one caller |
| `armorTextureIndex_` and `struct ArmorTextureRef` | a map with **no writer at all**, which made `MemoryStats::armorTextureCount` read 0 forever; it now reports `armorTextureCache_.size()` |

`armorTextureCache_` now stores a NULL for a cached miss (without it, a
17-23 name no archive has re-probes ~21 MB of S3D on every mesh build). All
three readers of that map were checked and guarded: `getMergedTextures()`
(new `if (!tex) continue`), `releaseMap()` and `sumMapBytes()` already skipped
nulls.

### Tests -- red before, green after

Pre-fix tree built by `git apply -R` of an exported patch (never `git stash`)
restoring `willeq_database.{h,cpp}`, plus a scaffold restoring the old
`getArmorTexture()` body so the new tests could reach the OLD behaviour. The
restored post-fix `model_loading.cpp` was diffed against the exported patch and
is byte-identical.

| test | pre-fix | post-fix |
|---|---|---|
| `WilleqDatabaseShippedContent.EveryTableTheClientQueriesExists` | **FAILED** -- "queries table 's3d_assets', which the shipped data/willeq.db does not contain" | Passed |
| `ArmorTextureArchiveTest.HighTierArmorTextureLoads` | **FAILED** -- `hufch1701.bmp` -> nullptr | Passed |
| `ArmorTextureArchiveTest.EveryTierLoadsAtLeastOneTexture` | **FAILED** -- tier 17 -> nullptr | Passed |
| `ArmorTextureArchiveTest.TheMisplacedTextureIsFoundInTheWrongArchive` | **FAILED** -- `haft2202.bmp` -> nullptr | Passed |
| `ArmorTextureArchiveTest.AMissIsRememberedAndNotReprobed` | Passed (vacuously -- it returned nullptr for everything) | Passed |
| `ArmorTextureArchiveTest.BaseTierIsNotServedFromAnArmorArchive` | Passed (same) | Passed |
| `ArmorArchiveDerivation.*` (4 cases) | n/a -- new code | Passed |

`EveryTableTheClientQueriesExists` is the general net and the one that would
have caught this six months earlier: it takes the table census from
`src/client/willeq_database.cpp` **itself** (every `FROM`/`INTO` identifier)
and checks each against `sqlite_master` in the shipped file, so a future query
naming a table the builder does not create is a red test rather than a
"not found" the caller cannot tell from an honest miss.

### Extra leg -- gtest DISCOVERY_TIMEOUT (no board item; orchestrator-requested)

`DISCOVERY_TIMEOUT 120` added to **292** `GTEST_DISCOVER_TESTS()` calls -- all
of them, across `tests/CMakeLists.txt` and 17 of the 19 `tests/cmake/*.cmake`
fragments (`eqe1.cmake` and `hygiene.cmake` have none). **There is no
helper or directory-level variable to use**: unlike `DISCOVERY_MODE`, the
GoogleTest module reads `DISCOVERY_TIMEOUT` only as a per-call keyword
(`if(NOT arg_DISCOVERY_TIMEOUT) set(arg_DISCOVERY_TIMEOUT 5)`), so it is
spelled at every site. The keyword goes last, after `PROPERTIES`, which is a
multi-value keyword that terminates at the next recognised one -- the two
calls carrying `PROPERTIES LABELS "live"` were checked. Rationale is written
out at the T163 block in `tests/CMakeLists.txt` (a NEW target must carry it
too) and noted in `refactor/EXECUTOR_PROTOCOL.md`'s load-artefact paragraph:
a discovery failure on this tip is no longer a load artefact.

### Verification

| step | result |
|---|---|
| `cmake --build build -j24` (background, exit file) | **exit 0**, 346 s. No exit 143 at any point, so no truncation scan owed |
| `ctest --test-dir build -j6 --output-on-failure` | **4,682 / 4,682, 0 failed** (123 skipped) |
| `visual_gate` inside that `-j6` run | Passed, 22.05 s -- did not need the standalone re-run |
| `visual_gate` standalone | **PASS**; p95 CPU ms qeynos2 5.650/10.108, gfaydark 6.517/10.672, befallen 9.952/10.948 |
| `bash scripts/build-gles2-x86.sh` | **exit 0**, 460 s |
| `bash scripts/build-arm-noble.sh` | **exit 0**, 280 s; `build-arm-noble/bin/willeq` is ELF 32-bit ARM EABI5 |
| `check_file_sizes.sh fable` | 10 files checked, **10 ok, 0 warn, 0 fail** (largest touched: `src/client/willeq_database.cpp` 701) |
| `check_doc_counts.py` | OK: 17 counts over 33 doc sites, every stated count matches |
| goldens | **untouched** -- no golden, no `data/willeq.db`, no `*.sha256` in the diff. The STOP did not fire |

Host load at the ctest runs: `uptime` 1-min 6.59 rising to 14.12 (three-slot
wave). No CPU-p95 artefact appeared even so.

**Two real reds on the first full run, both mine, both fixed and re-run
green:**
- `check_build_tree_orphans` FAILED naming `s3d_index_builder` -- deleting the
  target left the binary in `build/bin`, which cmake never prunes. Exactly
  what that net is for. Removed.
- `DocCitations` FAILED on `refactor/BOARD.md:8071` citing
  `client/graphics/eq/equipment_textures.h` -- my own new B469 text wrote an
  *include* path, which the checker reads as a repo path. Rewritten as the
  repo-relative `include/...` path.

The second full run after both fixes is the 4,682/4,682 above.

### Board

- **B468 STRUCK** -- confirmed a live defect and fixed.
- **B469** (new, class B) -- `equipment_textures` has **2,623 rows in the
  shipped database and no reader anywhere**. Found while closing B468: it is
  the same name->archive shape `s3d_assets` had, for the 17 `*equip*.s3d`
  held-item archives, and `WilleqDatabase` names it in no SQL at all. Merely
  dead rather than broken, but it ships bytes nobody reads inside the one
  pinned file. Boarded with the warning B468 taught: "nothing reads it" and
  "nothing CAN read it because the route is broken" look identical from a grep.
- **B470** (new, class B) -- `getArmorTexture()` opens and fully inflates a
  whole 4.1 MB armor archive per lookup, and an entity needs ~12 from the same
  one. **Measured: ~1.0 s cold (NFS), ~5 ms warm.** The per-lookup shape was
  kept on purpose -- it is the shape the code already had, and both obvious
  fixes hold ~4 MB of raw BMP resident per archive, which is a real cost on
  the Orange Pi where composite entities are already off for memory pressure.
  Not a regression: the pre-2026-03 code opened all seven eagerly for a ~60 MB
  spike, and the code this replaces resolved nothing at all.

**B469 and B470 are the numbers I allocated.** T451 and T431 also allocate
from B468+, so the orchestrator should check for a collision at merge.

### Docs

- `CLAUDE.md` -- a new paragraph under the derived-asset table: **every table
  the builder can create now ships**, what `s3d_assets` was, why it was never
  in the file, and that the census test now prevents the class.
- `docs/module_map.md` -- the `willeq_db_builder` row records the deleted
  fourth loader.
- `tools/willeq_db_builder.cpp` header -- the eleven-table list, with the
  twelfth's tombstone replacing T450's "not in the shipped file" note.
- `tools/willeq_db/db_loaders_json.cpp` header -- three loaders, not four.
- `scripts/build_willeq_db.sh` -- "the ten inputs above are ALL of them".
- `include/client/willeq_database.h` -- a tombstone where `AssetLocation` was,
  pointing at `EQE1TextureIndex` so nobody reintroduces a second index.
- `tests/CMakeLists.txt` + `refactor/EXECUTOR_PROTOCOL.md` -- the
  DISCOVERY_TIMEOUT leg.

### Surprises

1. **The one misplaced file.** `haft2202.bmp` in `global20_amr.s3d` is the
   only counterexample to a clean name->archive rule, and I had already
   written the strict derivation before censusing all seven archives. Without
   that census the fix would have shipped with a silent one-texture hole.
2. **A second dead table in the same file** (B469) -- `equipment_textures`,
   which is written, shipped, pinned, and read by nothing.
3. **`armorTextureIndex_` had no writer at all**, so `armorTextureCount` had
   been reporting 0 for six months alongside the broken lookup.
4. **`s3d_index_builder`'s two outputs were both orphans** -- the `.json` fed
   only a flag nothing passed, and the `.bin` (whose header advertises a
   runtime saving of "100-200 ms per archive open on ARM") has never had a
   single reader.
5. The `check_build_tree_orphans` net caught the stale binary within one ctest
   run of the deletion. It works.
