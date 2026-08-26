# T284 — the composite scan bulk-opens every pack in the tree (T281 finding; bears on decision #3)

Slot: slot2. x86 for the fix and proof; **Pi measurement is NOT in this
unit** (see below). NO eqemu. Class A. Territory: `loadZoneStep0_EQE1`'s
composite bulk-open and `RaceModelLoader::preloadModelData`'s composite
path (renderer_zone_load* / race_model_loader.cpp), the index-driven open
(`EQE1TextureIndex::resolve` + `openArchiveContaining` — T207/T266), tests.
Do NOT touch inventory/world containers (T282 slot3), practice points /
loot UI (T283 slot1).

## Finding (T281 Review)
`loadZoneStep0_EQE1` opens EVERY `.eqe1` in the composite directory. With
the composite dir pointed at the shipped ETC1 tree that is **1506 packs /
4.8 GB of mmap** — precisely T195/T197's address-space exhaustion, and the
stated reason Will turned composite entities OFF on the orangepi (T198,
decision #3). T281 has now made the index know the 498 sheet packs, so a
name → one pack open is finally possible.

## Scope
1. Replace the bulk scan with the index-driven open: a composite sheet
   name resolves through `EQE1TextureIndex` to its owning pack, which is
   opened on demand (T266's late-open path already does exactly this for
   the entity case — reuse it, do not fork a second implementation).
   Rule R1/R2 still governs: never open a pack nothing asked for.
2. Measure on x86 with `EQT_EQE1_REQUEST_LOG_DIR`: packs opened and bytes
   mapped, composites ON, before vs after. That number is the deliverable.
3. Tests: a composite name resolves and opens exactly one pack; a name in
   no pack still takes the documented FATAL (decision #2). Red/green.
4. Visual: with composites ON on x86, entities still render correctly
   (before/after crops; the composite atlas is what T191/T192 chased).

## STOP / hand-back
Do **not** change the orangepi preset or re-enable composites on the Pi —
that is Will's decision #3. Deliver the x86 numbers and say plainly whether
the address-space argument still holds, then hand it back.

## Verification
Full ctest; visual gate; Review with the before/after mapped-bytes table;
evidence review/T284/. Commit:
`T284: composite sheets open by index, not by scanning the tree`.

---

## Review (executor, slot2, 2026-08-26, off fable `edcf5e3e`)

Evidence: `/mnt/projects/fable/review/T284/` (README.md indexes it).

### Headline: packs opened and bytes mapped, composites ON

Measured on x86 with `gles2_zone_benchmark --entity-viewer --entity-viewer-composite
--eqe1-path /mnt/projects/EverQuest_ETC1 --load-mem-audit` (race 1/Human, zone
befallen) -- the only x86 harness that drives the composite path with no server.
Numbers come from the run's own `[LOAD-MEM-AUDIT] eqe1_mapped=` counter and its
composite open line; VmPeak sampled from `/proc/<pid>/status` by
`review/T284/measure.sh`.

| composite packs | before | after |
|---|---|---|
| packs opened for composites | **498** | **1** |
| bytes mapped for composites | **66,330,912** | **174,856** |
| total EQE1 mapped at `bench_11_entities` | 179,425,931 | **113,269,875** |
| process VmPeak | 5,517,208 kB | **5,392,364 kB** |

- 66,156,056 B less mapped (**-99.74 %** of the composite mapping).
- Every one of the five entity-viewer races opened exactly **1** pack
  (`review/T284/after/results.txt`); the before run opened 498 for each.
- The zone's own 28 EQE1 archives (113,095,019 B) are unchanged -- this unit
  touches only the composite set.

**The production before-state is larger than the benchmark's.** The benchmark's
scan filtered to `*_hs_def_*` / `*_actordef_*`; `loadZoneStep0_EQE1()`'s did
**not** -- it opened every `.eqe1` in the composite directory, which in the
shipped layout (`compositeEntityPath == eqe1Path`, `zone_bench_loader.cpp:730`
and `willeq.json`'s ETC1 tree) is the whole tree. Measured directly
(`review/T284/mmap_address_space_probe.txt`, re-runnable `mmap_probe.py`):

| | packs | bytes | address space |
|---|---|---|---|
| production step-0 composite scan | **1506** | **4,765,273,751** | **4,676,044 kB of VmSize** |
| 32-bit ARM user ceiling (T195) | | | 3,145,728 kB |

### Does the address-space argument still hold? **No -- it is gone.**

T195 measured the Pi dying with `std::bad_alloc` at **VmSize 3.13 GB** while
step 0 mapped the composite directory; T198 turned composites off on the
`orangepi` preset for that reason (decision #3). The mapping that caused it was
**1.49x the entire 32-bit user address space on its own** (4,676,044 kB vs
~3,145,728 kB) -- it could never have fit. After this unit the composite path
maps **one pack per sheet actually used**: 174,856 B for a Human, 349,704 B for
the largest of the five races measured. Even a zone whose every spawned race
took a distinct sheet would map single-digit MB.

So the specific argument that closed decision #3 no longer describes the code.
It does **not** follow that composites are safe to switch on for the Pi, and
this unit does not switch them on: the `orangepi` preset is untouched (STOP
honored), no Pi measurement was taken (not in scope), and the other Pi cost --
resident memory for the decoded sheets, and whatever T197's atlas findings still
imply -- is unmeasured here. What changed is that the *reason on record* is now
obsolete; whether to re-open decision #3 is Will's call, and it needs a Pi run,
not this table.

### What changed

1. **`loadZoneStep0_EQE1()` (`renderer_zone_load.cpp`)**: the directory scan is
   **deleted, not replaced**. Nothing takes its place because nothing has to --
   `RaceModelLoader::preloadModelData()` already requests the sheet by name
   through `ConstrainedTextureCache::getOrLoad()`, which misses on the open set
   and calls the resolver installed a few lines above: `openArchiveContaining()`
   (T207's index lookup + T266's rule-R2 late-open). A composite sheet is never
   a zone candidate, so it always takes rule R2 and opens exactly one pack. A
   name in no pack still resolves to nothing and stays the all-or-nothing FATAL
   (decision #2). This is the reuse the plan asked for: no second implementation.
2. **`openCompositeSheetEQE1()` (`renderer_effects.cpp`)**: the one case the
   index cannot answer -- a `--composite-entities` directory that is **not** the
   indexed `--eqe1-path`. `data/eqe1_texture_index.json` is built over the eqe1
   tree, so such packs are absent from it and `resolve()` answers "no archive has
   this", which decision #2 turns into `exit(1)`. The old scan hid that by
   opening the whole directory; T281 flagged it as "525 names one directory
   layout away from a crash". No scan and no index is needed for it: a composite
   sheet pack is **self-named** (pack stem == its single entry), so the requested
   name names its pack. Existence-checked first so an absent name leaves no
   negative entry in `eqe1Loaders_`. `getOrOpenEQE1()` was split into
   `getOrOpenEQE1FromDir(name, dir)` so this shares the open/register/negative-
   cache logic rather than copying it.
3. **`zone_bench_loader.cpp`**: its own (filtered) copy of the scan is replaced
   by the same rule -- `EQE1TextureIndex::resolve` + open-one-pack, installed as
   the cache's resolver. Keeping the benchmark on production's rule is the point:
   it is the measurement instrument, and a fork here would measure something
   production does not do. The index path is `WILLEQ_EQE1_INDEX` (env), else
   `data/eqe1_texture_index.json`, else the configure-time
   `WILLEQ_EQE1_INDEX_REAL_PATH` -- the same CWD fallback shape as T270's
   `WILLEQ_DB_REAL_PATH`. Added to the two sibling tools that compile this TU
   (`gles2_shader_diagnostic`, `gles2_composite_audit`) so they build.

### Tests (red/green)

`tests/test_eqe1_composite_open.cpp`, two levels:

- `EverySheetNameHasExactlyOneOwner` (index level, no assets): all 498 shipped
  sheet packs are the sole owner of their own name, so `resolve()` can only ever
  name one pack for a sheet. Green before and after -- it pins the property the
  fix depends on, and would catch a builder change that started sharing sheet
  names.
- `SingleRaceCompositeRunOpensOnePack` (integration, shells out to
  `gles2_zone_benchmark` like `test_entity_viewer_regression`): a single-race
  composite run must open <= 8 packs on demand, >= 1, and must still log
  `buildCompositeMesh` (so it cannot pass by silently not compositing).
  - **RED** at baseline (`review/T284/red_baseline_composite_open_test.txt`):
    `no 'EQE1-COMPOSITE: N pack(s) opened on demand' line -- the composite path
    still scans the sheet directory (it reported 'Composite EQE1: 498 archives
    loaded')`.
  - **GREEN** after (`review/T284/green_fixed_composite_open_test.txt`): 2/2.

The "name in no pack still takes the FATAL" half of the plan's item 3 is already
pinned by `test_eqe1_texture_index.cpp`'s `NameInNoArchiveResolvesNothing`
(decision #2); nothing in this unit changes that branch, and the new
`openCompositeSheetEQE1()` fallback returns nullptr for a name with no pack file,
so it stays fatal.

### Visual

Composite leg, five races (SKE 60, ZOM 70, BAT 34, ORC 54, HUM 1), four angles
each, captured identically before and after: `review/T284/before/` and
`review/T284/after/`. **All 20 PNG pairs are byte-identical (md5)** and every
`magenta_in_bbox` value is unchanged. Identity is the claim here -- the rendered
result must not change, only which packs get mapped -- but it is not a
self-assessment of the pixels; Will judges those.

Visual gate: **PASS**, `tests/goldens/` byte-untouched.

### Verification

| step | result |
|---|---|
| `cmake --build build -j24` | exit 0 |
| `ctest` | **3148 / 3149**; sole failure `AudioManagerTest.PreloadCommonSounds` (documented ALSA host quirk) |
| `scripts/build-gles2-x86.sh` | exit 0 |
| visual gate | **PASS**, goldens untouched |
| `scripts/build-arm-noble.sh` | exit 0 |
| `check_file_sizes.sh fable` | no NEW FAIL (see below) |

### Deviations / notes

- **Production's `loadZoneStep0_EQE1()` is not exercised on x86.** It needs a
  zone load, which needs eqemu, which this unit does not have. The benchmark
  mirrors it (same index, same `resolve()` rule, same one-pack outcome) and the
  deleted code's replacement -- `openArchiveContaining()` as the cache's
  resolver -- is the path T207/T266 already shipped and measured in production.
  Flagged rather than absorbed: a live composite zone-in on x86 would close it.
- `check_file_sizes.sh` reports `tools/hwcaps/zone_bench_loader.cpp` FAIL at 3280
  lines. **Pre-existing**: it is 3232 lines on `fable` (already over the 3000
  threshold); this unit adds 48. Not a new FAIL, and the change is a
  like-for-like replacement inside an existing function, so no new TU. The other
  other FAIL (`src/client/eq.cpp`, 3944 lines), the WARNs and the inventory /
  trainer files listed are other units', not this unit's -- `fable` advanced
  from `edcf5e3e` to `491d0c57` (T282/T283 merges) mid-unit and the branch was
  not rebased.
- `orangepi` preset untouched; composites not re-enabled on the Pi; no Pi run.
  No eqemu. No golden re-bless.
- Baseline ctest at the branch point was 3123/3124 (T281); the 3149 total is
  T282/T283's merged tests plus this unit's 2.
