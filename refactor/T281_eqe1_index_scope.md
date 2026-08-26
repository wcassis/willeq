# T281 — data/eqe1_texture_index.json is scope-short: 1008 archives vs the tree's 1506 (T279 hand-back)

Slot: slot2. x86. NO eqemu. Class A. Territory: tools/eqe1_texture_index_builder.cpp,
data/eqe1_texture_index.json, the resolver's use of it
(`EQE1TextureIndex::resolve` — T266 added late-open on top of it), tests.
Do NOT touch inventory (T282 slot3), trainer (T280 slot1).

## Finding (T279 Review)
The committed index covers **1008 archives / 50,447 names**; the live tree
(`/mnt/projects/EverQuest_ETC1`, now complete after T279) has **1506 /
50,972**. The index predates the ~498 equipment-sheet packs P16/P48 added.
T266's late-open resolves a name through this index, so a name that lives
ONLY in an unindexed pack cannot be late-opened — it takes the
all-or-nothing FATAL instead (decision #2). That is the same crash class
T266 just fixed, hiding behind a stale data file.

## Scope
1. Rebuild the index over the whole tree; commit it with the T262/T274-style
   diff proof (archives/names before vs after, and a spot check that an
   existing name's owner list is unchanged, not just longer).
2. Establish whether any of the 525 newly-indexed names were previously
   FATAL-able: cross-check the new names against what the resolver could
   already reach (candidate sets + the old index). Report the count — if it
   is non-zero, that is a live crash that T266's fix could not reach.
3. Guard it: a test that the shipped index's archive count equals the pack
   count in the configured EQE1 dir (skip cleanly when the dir is absent,
   `test_eqe1_texture_index`'s precedent), so a future pack addition cannot
   silently outrun the index again.
4. Note the index's size/load cost before and after (it is read at startup).

## Verification
Full ctest; visual gate; Review; evidence review/T281/. Commit:
`T281: the EQE1 name index covers every shipped pack`.

---

## Review (executor, slot2, 2026-08-26, off fable `59b1e114`)

Evidence: `/mnt/projects/fable/review/T281/` (README.md indexes it).

### 1. Rebuild + diff proof

`eqe1_texture_index_builder --eqe1-path /mnt/projects/EverQuest_ETC1`,
read-only over the shared tree (1506 opens, nothing written there).

| | before | after |
|---|---|---|
| archives | 1008 | **1506** (+498) |
| unique names | 50,447 | **50,972** (+525) |
| total entries | 182,468 | 182,993 |
| names in >1 archive / max owners | 21,356 / 691 | 21,356 / 691 (unchanged) |
| file size | 1,838,948 B | 1,931,639 B (+92,691 B, +5.0%) |
| archives unreadable | — | 0 |

The proof the plan asked for (`review/T281/index_diff_proof.txt`, re-runnable
`index_diff_proof.py`) compares owner lists as **archive names**, because
every raw index number in the file changed — the 498 new packs sort in
throughout the alphabetical `archives` array, which is why git shows the JSON
as a whole-file rewrite. Result:

- **50,447 / 50,447 pre-existing names: owner list byte-identical.** Not
  "extended" — 0 purely-extended, 0 lost, 0 reordered. The new packs each hold
  only their own name, so they collide with nothing.
- 0 archives removed; 498 added.
- Named spot check (`bwdhe0005` 9 owners, `fishe0301` 39, `detail_atlas` 2,
  `ui_atlas` 1) and a seeded random sample of 25: all identical.
- The 525 new names have **zero** owners among the 1008 previously-indexed
  archives.

### 2. How many of the 525 were previously FATAL-able: **0 live, 525 latent**

Full derivation in `review/T281/fatalable_analysis.md`.

All 525 live only in the 498 new packs, which are the composite entity sheets
(489 `<model>_hs_def_<g>_<v>`, 9 `<model>_actordef_0_<v>`; pack name == entry
name, one sheet per pack, 27 with an `_alpha` companion). Absent from the
index means `resolve()` answers rule 1 — "opens NOTHING" — the branch
decision #2 turns into `constrained_texture_cache.cpp:230`'s `exit(1)`. T266's
late-open resolves *through* this index, so it could not have covered them.

They were nevertheless unreachable in that state, for one reason: the only
producer of these names is `RaceModelLoader::preloadModelData()`
(`race_model_loader.cpp:923`, `compositeBaseName`), gated on
`compositeEntityPath_`, which is set only at `irrlicht_renderer.cpp:2306`
behind `enableCompositeEntities && !compositeEntityPath.empty()` — the same
pair that makes `loadZoneStep0_EQE1()` (`renderer_zone_load.cpp:156`)
**bulk-open every `.eqe1` in that directory** at step 0, before entity prep.
The shipped arrangement puts the sheet JSONs beside the packs in the ETC1 tree
(`zone_bench_loader.cpp:730` wires `compositeEntityPath = eqe1Path`), so the
owning pack was always already open and the lookup never reached the index.
Cross-checked: `config/eqe1_archives.json` names no sheet pack in either
`zoneArchives` or its 33 `globalArchives`; all 500 sheet `.json` files have
their `.eqe1` beside them (the only two unmatched JSONs are
`composite_index` and `p08f_zone_signature_registry`, not sheets); with
composites off (`orangepi`, T198) the names are never requested at all.

So the honest number is **0 crashes available today, 525 names one directory
layout away from one** — point `--composite-entities` at a JSON-only sheets
directory and every composite race becomes an `exit(1)` instead of the
intended fallback to the multi-buffer path.

**Finding for the orchestrator, deliberately NOT acted on here (out of
territory):** that bulk scan opens *every* pack in the composite directory.
With the composite dir set to the ETC1 tree that is 1506 packs / 4.8 GB of
mmap — T195/T197's address-space exhaustion, i.e. why composites are off on
the Pi (T198). The index-driven replacement (resolve the sheet name, open the
one pack it names) was impossible while the index did not know the sheet packs
existed. It is possible now. Worth its own unit.

### 3. Guard

`Eqe1TextureIndexShipped.CoversEveryPackInTheTree` in
`tests/test_eqe1_texture_index.cpp`: compares the shipped index's archive
**name set** against the `.eqe1` stems on disk (set equality, not just the
count, so an equal-sized but different tree is caught), reports missing and
extra separately with a sample and the exact rebuild command, and
`GTEST_SKIP`s when the tree is absent — same precedent as the existing
shipped-index test and `test_entity_viewer_regression`'s `kEqe1Path`. Tree
path is `/mnt/projects/EverQuest_ETC1`, overridable with `WILLEQ_EQE1_PATH`.

- RED against the old index (`review/T281/red_old_index_coverage_test.txt`):
  `498 pack(s) in the tree are absent from the shipped index ... abh_hs_def_0_0,
  ... (498 total)` plus `1008 vs 1506`.
- GREEN against the rebuilt index; the file's other 5 tests unchanged and
  still pass (`bwdhe0005` still resolves to `bwd_chr`, `ownerCount` still 9).

### 4. Index size / load cost

`review/T281/index_load_cost.txt`, three interleaved trials of 10 reps each on
the x86 dev VM (probe source kept at `load_cost_probe.cpp`):

| | old (1008/50,447) | new (1506/50,972) |
|---|---|---|
| `load()` best of 10 | 148.8 / 143.3 / 155.7 ms | 161.1 / 160.0 / 156.4 ms |
| mean of 10 | 170.5 / 158.7 / 165.6 ms | 169.3 / 189.5 / 200.2 ms |
| RSS after first load | +35.02 MB | +35.33 MB |

Run-to-run spread is ±25 ms on the same file, and the means overlap in both
directions across trials, so the +1.0% in names is **not resolvable** as a
load-time change; the honest statement is "within noise, +~320 KB resident".
It is read once per process (`renderer_zone_load.cpp:142`, guarded by
`isLoaded()`), not per zone.

### Verification

| step | result |
|---|---|
| `cmake --build build -j24` | exit 0 |
| `ctest` | **3123 / 3124**; sole failure `AudioManagerTest.PreloadCommonSounds` (documented ALSA host quirk) |
| `scripts/build-gles2-x86.sh` | exit 0 |
| visual gate | **PASS**, `tests/goldens/` byte-untouched |
| `scripts/build-arm-noble.sh` | exit 0 |
| `check_file_sizes.sh` | T281's 4 source files: 4 ok, 0 warn, 0 fail |

### Deviations / notes

- `fable` advanced from `59b1e114` to `1b67f864` (T280 merge + T283 plan)
  mid-unit. The branch was NOT rebased. Consequence: `check_file_sizes.sh
  fable` reports T280's files, including its pre-existing
  `command_processor.cpp` FAIL at 3002 lines — nothing to do with T281. The
  `--files` run over T281's own files is the meaningful one and is clean.
- Comment counts updated to match the rebuilt tree
  (`eqe1_texture_index.cpp` "~1008" → "~1506"; the header's
  "21356 of 50447" → "of 50972"; the builder's "182468 ... 50447"
  → "182993 ... 50972"). No behaviour change.
- No eqemu, no Pi, no golden re-bless.
