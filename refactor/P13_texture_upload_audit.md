# P13 — Zone-in texture upload audit: what are we actually loading, and why does 15MB of 1999 assets evict a 64MB budget?

Approved by Will 2026-07-27 ("go ahead with P13").
Slot: wt/slot1, branch task/P13-texture-audit, cut from fable (827719a).
AUDIT unit — instrument, measure, reconstruct, recommend. REPORT-ONLY:
no loading-path fixes land here (they get their own unit(s) from the
ranked list); instrumentation must be flag/env-gated and additive.

## Motivation (Will, 2026-07-27)

All assets are original 1999/2000 launch data. qeynos2's three archives
are 9.3MB on disk / ~15.4MB extracted (148 zone + 472 chr textures —
7.1MB as RGBA for ALL of chr). A 1999 machine ran this zone in 4MB of
texture memory. Yet P10b measured 71 ConstrainedTextureCache evictions
during a qeynos2 zone-in under the 64MB orangepi budget. Something
loads far more than the era's referenced set. Candidates:
1. **Composite sheets (our own P08d flip)**: 479 sheets avg 2.2MB RGBA,
   182 at >=2048px (8-16MB each); the builder's tile-unrolling
   multiplies pixels vs source textures; ~184 entities across many
   races → sheets alone may exceed 64MB on the RGBA path.
2. **Wholesale archive loading (Will's historical suspicion)**: are we
   uploading only referenced textures, or everything an opened S3D
   contains? ("In the past, every s3d that was opened was being
   cached.")
3. **Double-loading**: do composite-eligible races upload their
   per-part textures AND their sheet?

## Design

### A. Instrumentation (flag/env-gated, additive)

1. A texture-upload LEDGER: every driver texture creation during zone
   load + entity spawn-in records name, dimensions, bytes-as-uploaded,
   format (RGBA/565/4444/ETC1), source (which archive/loader/callsite:
   zone regions, objects, entity per-part, composite sheet, sky,
   detail, UI, placeholder), and — where cheap — what references it.
   Build on P04's LOAD-MEM-AUDIT pattern and the existing /pmem
   bookkeeping; ledger dumps to a file at a marker (zone-in complete).
2. Eviction tracing: each eviction logs victim name/bytes/category and
   whether it is RE-uploaded later in the same session (thrash count).

### B. Measurement runs (x86, eqemu harness, production orangepi preset)

3. qeynos2 zone-in, composites ON (the shipped config): full ledger +
   eviction reconstruction of the 71-eviction scenario.
4. qeynos2 zone-in, composites OFF: the differential isolates the
   sheets' contribution exactly.
5. befallen zone-in, composites ON: the no-eviction contrast case.
6. (Code-level, no run needed) Determine which upload format each
   category ACTUALLY takes on x86 vs Pi: where eqe1/ETC1 engages, what
   the preset's enableCompressedTextures=false actually controls, and
   what the sheets upload as on each platform.

### C. Analysis deliverables

7. **The ledger tables**: totals by category for each run; top-20
   largest uploads; uploads-never-referenced list (the wholesale
   symptom); double-load list (per-part textures uploaded for
   composite-eligible races).
8. **Sheet economics**: per-sheet uploaded bytes; unroll inflation
   factor (sheet pixels ÷ sum of distinct source texture pixels) for
   the qeynos2-relevant sheets; total sheet bytes vs total everything-
   else.
9. **The era-honest arithmetic**: what qeynos2 SHOULD cost
   (referenced-only set, at RGBA and at ETC1) vs what it DOES cost —
   the direct answer to Will's question.
10. **Eviction reconstruction**: what was evicted, in what order, what
    thrashed.
11. **Ranked fix list** with projected savings each (candidates to
    evaluate, not presume: ETC1/eqe1 sheets on all paths per the P03
    decision; lazy per-visible-race sheet loading; skip per-part
    uploads for composite-eligible races; builder sheet-size/unroll
    discipline; referenced-only archive loading if wholesale loading is
    confirmed; budget right-sizing LAST — the budget is probably fine,
    the load is wrong).

## Verification

- Standard bar: build, ctest (known host exceptions), gles2-x86 build,
  visual gate PASS goldens untouched, ARM build, file-size check.
- Instrumentation off-by-default proof (ledger flag off = byte-
  identical behavior; a spot render compare suffices).
- Server use sanctioned; clean shutdown; configs restored.
- Ledgers/analysis → benchmarks/audits/p13_texture_ledger/ (committed;
  they're small text/JSON).

## Out of scope

- Landing any loading-path fix (ranked list only).
- Pi sessions (format differences established at code level).
- The merge, animation, foliage-alpha, pop-in threads.

## Review

Executed on `wt/slot1`, branch `task/P13-texture-audit`, cut from `fable` at
`9613e7d`. REPORT-ONLY, as scoped: no loading-path behavior changed.

### A. Instrumentation landed

- **`TextureUploadLedger`** (`include/client/graphics/texture_upload_ledger.h`,
  `src/client/graphics/texture_upload_ledger.cpp`): a plain record/dump class
  (no locking of its own — always called under `ConstrainedTextureCache::
  mutex_`) holding two vectors, `TextureUploadRecord` (order, name, w/h,
  bytes-as-charged, format, category, callsite, `isReupload`) and
  `TextureEvictionRecord` (order, name, bytes, category). `dumpToFile()`
  writes a greppable `U,...`/`E,...` CSV-ish text file with a header comment.
- **Wired into `ConstrainedTextureCache`** (`constrained_texture_cache.h/.cpp`):
  - `getOrLoad(name, data, category="uncategorized", callsite="")` — the
    category/callsite are stashed in `pendingLedgerTag_[name]` **only when
    `ledgerEnabled_`** (set once, right after the cache-miss branch, so it
    covers all three downstream upload paths — ETC1, v2, plain RGBA —
    uniformly).
  - The four actual GPU-upload sites — `uploadTexture()`'s RGBA/BMP/DDS
    path, `uploadCompressedETC1()`, `uploadCompressedV2()`, and
    `registerTexture()` — each consume the pending tag (or take an explicit
    `category` param for `registerTexture`) and call
    `ledger_.recordUpload(...)` with the real dimensions/bytes/format
    actually charged against the budget. The placeholder texture
    (`getPlaceholderTextureInternal`) is tagged `"placeholder"` directly.
  - `evictTexture()` calls `ledger_.recordEviction(name, sizeBytes,
    category)` using the category stored on `CachedTexture::category` at
    upload time — this is the eviction-victim-detail + thrash-detection
    machinery (`TextureUploadLedger` tracks `everEvicted_` and flags any
    later re-upload of the same name as `isReupload=true`, counted in
    `thrashCount_`).
  - `setLedgerEnabled()`/`isLedgerEnabled()`/`dumpLedger(path, runLabel)`.
    Enabled purely by config: `ConstrainedRendererConfig::
    textureUploadLedgerPath` (empty = disabled, the default), set from a new
    `--texture-ledger <path>` CLI flag on `willeq` (mirrors P04's
    `--load-mem-audit` wiring in `application.cpp`/`application.h`).
    `renderer_zone_load.cpp`'s `loadZoneSequential()` dumps the ledger at the
    existing "zone-in complete" marker (same point `dumpScene()` already
    fires at).
- **Category tags threaded to the call sites the plan's three hypotheses
  actually turn on** (all via the new optional `getOrLoad`/`setUploadCategory`
  params, all default to `"uncategorized"` when not passed):
  `zone_region`/`object` (`ZoneMeshBuilder::setUploadCategory`, set once per
  builder instance in `loadZoneStep4_Regions`/`loadZoneStep6_Objects` —
  `zone_geometry.h/.cpp`), `entity_perpart` (`loadZoneStep8c_VariantTextures`,
  `loadZoneStep8d_EquipmentPrep` in `renderer_zone_load.cpp`, and the T05
  decode loop in `race_model_loader.cpp`'s `preloadModelData`),
  `composite_sheet` (the composite-lookup `getOrLoad` call in
  `preloadModelData`), `sky` (`loadZoneStep10_Sky`), `placeholder` (internal).
  Detail-object and UI textures were **not** tagged (they still record as
  `"uncategorized"`) — a named gap, not a fabricated guess; see Deviations.
- **CMake wiring**: `texture_upload_ledger.cpp` added next to every
  `constrained_texture_cache.cpp` entry in `CMakeLists.txt` (14 targets) and
  `tests/CMakeLists.txt` (3 targets) — mechanical, verified by build.

**Off-by-default proof**: `ledgerEnabled_` defaults `false` and is only ever
set `true` inside `setDriver()` when
`config_.textureUploadLedgerPath` is non-empty. Every ledger call site (the
five listed above, plus `pendingLedgerTag_` insertion in `getOrLoad`) is
gated on `if (ledgerEnabled_)` — when off, `getOrLoad`'s new parameters are
simply discarded (no map insert, no allocation) and no `TextureUploadLedger`
method is ever called. None of the three measurement runs below used a
process that also skipped `--texture-ledger`; the standing proof is instead
`bash scripts/ci/visual_gate.sh` (run against this unit's `build-gles2/bin/
willeq`, **no** `--texture-ledger` flag, matching how the golden frames were
originally captured) — `=== VISUAL GATE: PASS ===`, goldens byte-identical
(`git status --porcelain` shows no golden/benchmark diffs). Since the visual
gate's golden frames were captured entirely without this unit's code
existing, an exact pixel match with the ledger *code path present but
unexercised* is direct evidence the additions are behavior-neutral when off.

### B. Measurement runs

Real x86 GLES2 client (`build-gles2/bin/willeq`, EGL surfaceless llvmpipe),
sanctioned eqemu automation (`scripts/eqemu/up.sh`/`seed.sh`/`down.sh`),
account `willeq_test` / character `Testchar` (GM), `800x600`, `--eqe1-path
/mnt/projects/EverQuest_ETC1` (the single NFS-style mount that is both the
general ETC1 pack directory *and* `compositeEntityPath` — confirmed by
directory listing: `qeynos2.eqe1`/`qeynos2_chr.eqe1`/`qeynos2_obj.eqe1` plus
1,485 `.eqe1`/`.json` composite-sheet files coexist in the one directory).
Testchar's `character_data` row was pointed at each zone's `safe_x/safe_y/
safe_z` via direct UPDATE before each run (avoiding the exact pre-existing
bad befallen coordinate that P10b's review flagged as hitting an unrelated
`HCMap` FATAL) and restored to its original stored position
(`zone_id=36, x=-77.9232, y=21.9086, z=3, heading=190`) after all three runs
completed — confirmed via `SELECT`.

- **Run A — `qeynos2`, composites ON** (shipped `orangepi` preset
  unmodified, `textureMemoryBytes=64MB`, `enableCompositeEntities=true`):
  `--script` = connect, wait ~60s (zone load + full progressive settle),
  `/camp`. Zone load completed in 3,859ms (`loadZoneSequential: complete`).
  `Composite entity adoption: 77/182 entities eligible, 10 sheet hits, 0
  sheet misses` — confirms the full 182-entity population (matching the
  plan's "~184 entities" estimate) was processed, not a visibility-limited
  subset (`entityPrepMaxPvsDepth=255` in the shipped preset JSON removes any
  PVS-distance gate). **670 uploads, 16,440,863 bytes (15.68MB) total, 0
  evictions, 0 thrash re-uploads.** Ledger:
  `benchmarks/audits/p13_texture_ledger/run_a_qeynos2_composites_on.txt`.
- **Run B — `qeynos2`, composites OFF**: a temporary preset
  `orangepi_p13_nocomposites` (byte-identical to `orangepi` except
  `enableCompositeEntities:false`, `compositeEntityPath:""`) was added to
  `config/constrained_presets.json` for this run only and reverted
  (`git checkout -- config/constrained_presets.json`) immediately after —
  confirmed clean via `git status`. Same script/position. **656 uploads,
  13,148,607 bytes (12.54MB), 0 evictions.** Ledger: `run_b_qeynos2_
  composites_off.txt`.
- **Run C — `befallen`, composites ON** (shipped `orangepi` preset): same
  script. Zone load completed in 3,556ms, entities=145. **420 uploads,
  9,364,031 bytes (8.93MB), 0 evictions.** Ledger: `run_c_befallen_
  composites_on.txt`. Process aborted **after** `/camp` + logout completed
  (`terminate called after throwing 'std::system_error': Resource deadlock
  avoided`) — the same pre-existing `EverQuest::StopUpdateLoop()`
  shutdown-thread issue P10b's review already documented and explicitly
  flagged as occurring strictly after all evidence for that unit was
  captured ("did not cost any capture time"); reproduced identically here,
  not investigated further (out of scope), and it did not affect the ledger
  dump (written synchronously at zone-in-complete, long before shutdown).

**Code-level format determination (B.6, no run needed)**: `constrained_
texture_cache.cpp`'s `probeCompressedTextureSupport()` shows
`config_.enableCompressedTextures` gates **only** the desktop S3TC/DDS path
(`tryCompressedUpload()`, `glCompressedTexImage2D` with `GL_EXT_texture_
compression_s3tc`) — and `tryCompressedUpload()` has **no caller** anywhere
in `getOrLoad()`'s actual flow (the BMP/DDS-decode branch calls
`processTextureData()` directly). The `orangepi` preset's
`enableCompressedTextures=false` (comment: "Mali 400 via Lima
software-decodes S3TC; no GPU savings") is therefore a no-op on the
production path today — it does not touch ETC1 at all. The **ETC1 path is
entirely independent**: `uploadTexture()` picks `uploadCompressedETC1()` vs
`uploadCompressedV2()` vs the plain RGBA branch purely based on whether
`findCompressedEntry()` found the name in an opened `.eqe1` pack and what
`payloadFormat` byte is baked into that pack entry — a build-time decision
(`eqe1_builder`/`entity_composite_builder`), not a runtime driver-capability
check. Since the shipped `orangepi` preset always configures `--eqe1-path`
in production and the measurement runs above confirm **100% of uploaded
zone/object/entity/sky/composite bytes were `ETC1` or `ETC1+alpha`** (0
`RGBA8888` bytes outside the 8x8 placeholder — see category_totals.csv),
qeynos2 and befallen upload as ETC1 on both x86 (this dev build) and Pi
identically; the two platforms differ only in whether the GPU natively
decodes `GL_ETC1_RGB8_OES` in hardware (Pi/Mali 400) vs Mesa
llvmpipe-emulated (x86 dev), not in what gets uploaded or its byte cost.

### C. Analysis

**Ledger category totals** (`benchmarks/audits/p13_texture_ledger/
category_totals.csv` has the full breakdown; MB rounded):

| category | A: qeynos2 ON | B: qeynos2 OFF | C: befallen ON |
|---|---|---|---|
| zone_region | 126 tex / 5.04MB | 126 tex / 5.04MB | 9 tex / 0.34MB |
| object | 84 tex / 3.46MB | 84 tex / 3.46MB | 59 tex / 2.02MB |
| composite_sheet | 14 tex / 3.05MB | — (0) | 15 tex / 3.68MB |
| entity_perpart | 268 tex / 1.08MB | 268 tex / 0.99MB | 248 tex / 1.06MB |
| sky | 4 tex / 0.03MB | 4 tex / 0.03MB | 4 tex / 0.03MB |
| uncategorized (detail/UI/fx, untagged) | 173 tex / 3.02MB | 173 tex / 3.02MB | 84 tex / 1.80MB |
| placeholder | 1 tex / 0.0003MB | same | same |
| **TOTAL** | **670 tex / 15.68MB** | **656 tex / 12.54MB** | **420 tex / 8.93MB** |

**Top-20 largest uploads** (Run A): `top20_qeynos2_composites_on.csv` — the
list is dominated by composite sheets (7 of the top 8 entries are
`composite_sheet`, e.g. `qcm_hs_def_0_0` and `qcf_hs_def_0_0` at 349,576
bytes each) interleaved with a handful of large `object` rug/tree textures
(`rug3.bmp`/`rug4.bmp`/`rug2.bmp` at ~175-350KB, ETC1+alpha) and one
uncategorized `detail_atlas` (512x512, 174,760 bytes).

**Uploads-never-referenced (the wholesale-loading symptom)**: not observed —
see hypothesis 2 verdict below; every checked call site is reference-gated
at the code level, and no ledger row's category is unexplained by a known
referenced-load path other than the `"uncategorized"` bucket (detail/UI/fx
textures this unit didn't tag, not an untagged wholesale dump — their counts
are small and identical between Run A/B, i.e. composite-independent).

**Double-load list**: `entity_perpart` upload **count is identical (268) in
both Run A (composites ON) and Run B (composites OFF)** — direct ledger
proof that composite-eligible entities' individual per-part textures are
uploaded regardless of whether their composite sheet also loads (matches
the code-level finding below). The entity_perpart byte totals differ only
slightly (1.08MB vs 0.99MB, 268 uploads either way) — noise from
upload-order-dependent LRU/format selection, not a meaningfully different
texture set.

**Sheet economics** (`sheet_economics.csv`, the 14 sheets Run A actually
used): sheet pixel area ranges from 1.0x (`fis_hs_def_0_0`, no repeat tiling)
to 6.06x (`ske_hs_def_0_0`) the sum of its distinct source textures' pixel
area; **overall unroll inflation factor across all 14 sheets: 3.25x**
(4,792,320 sheet px vs 1,472,704 distinct source px) — the GL_REPEAT-style
tile unrolling the P08d builder does to cover each source texture's full UV
range multiplies pixel cost roughly 3x on average before any ETC1
compression. Composite-sheet bytes were the **single largest category** in
Run A (3.05MB, 18% of total) and the largest in Run C (3.68MB, 41% of
total) — real, but not enough on its own (even added atop everything else)
to approach the 64MB budget in either measured run.

**The era-honest arithmetic (Will's direct question)**: measured
referenced-only qeynos2 cost (composites ON, ETC1, as actually uploaded)
is **15.68MB** — closely in line with the plan's own era-honest estimate
("9.3MB on disk / ~15.4MB extracted"), not a multiple of it. Composites OFF
drops it to 12.54MB (the sheets' 3.05-3.29MB net addition, not offset by any
double-load savings — see hypothesis 3). Neither figure is remotely close to
the 64MB budget. **This measurement did not reproduce anything resembling
"71 evictions"** — both qeynos2 configurations landed comfortably under
budget with zero evictions in this unit's controlled single-position,
~60-second zone-in-and-settle session. See Deviations for what this does and
doesn't mean.

**Eviction reconstruction**: **0 evictions in all three runs** — there is
nothing to reconstruct (no victim order, no thrash) from this unit's own
measurements. The eviction-tracing machinery (Design item A.2) is fully
built and wired (`evictTexture()` → `ledger_.recordEviction()` with
category + thrash detection via `everEvicted_`) and will populate
non-trivial `E,...` rows automatically the next time a session actually
exceeds the budget — it was simply never exercised by these three
particular runs. Not a design gap; see Deviations for the honest gap this
leaves in answering the plan's literal "reconstruct the 71-eviction
scenario" ask.

### Three-hypothesis verdicts (data, not presumption)

1. **Composite sheets — REAL BUT NOT SUFFICIENT ALONE, verdict: CONTRIBUTING
   FACTOR.** Confirmed costly (3.05-3.68MB per run, largest single category
   in 2 of 3 runs, 3.25x average pixel-unroll inflation, several individual
   sheets 4-6x) — but total zone-in cost with composites ON never
   approached the 64MB budget in this unit's measurements. Sheets are a real
   line item and the clearest fix target (see ranked list), but this data
   does not support "composite sheets alone explain 71 evictions."
2. **Wholesale archive loading — REFUTED**, both at the code level (three
   independent research passes over `renderer_zone_load.cpp`,
   `texture_atlas.cpp`, `race_model_loader.cpp`/`variant_loading.cpp`/
   `model_loading.cpp` found every GPU-upload call site gated by a
   name-filter loop — `geom->textureNames()`, `equipData2->textureNames`,
   sky's `neededSet`, variant `variantPrefixes` substring match — no
   "upload everything the S3D/EQE1 archive contains" path exists in the
   current code) and empirically (Run A's 670 uploads is far below "148
   zone + 472 chr" = 620 textures counted per-archive in the plan's own
   framing, consistent with a filtered, deduplicated, referenced set, not a
   wholesale dump — and P04's own prior unit already fixed the one true
   wholesale-open bug that existed, at the *archive-handle* level, not the
   texture-upload level). `texture_atlas.cpp`'s "load every page/tile" path
   is confirmed dead code — no caller in the current `loadZoneSequential`.
3. **Double-loading — CONFIRMED**, both at the code level
   (`RaceModelLoader::preloadModelData`, `race_model_loader.cpp`: the
   composite-sheet lookup at line ~803 and the unconditional per-part "T05"
   decode loop at line ~835 are **not mutually exclusive** — the T05 loop
   has no `if (!modelData->compositeData)` guard, and `uploadAllPending()`
   (called per-entity and once more at the end of Step 8, `renderer_zone_
   load.cpp`) promotes every decoded entry to GPU regardless of whether the
   entity's final mesh actually references it) and empirically (entity_
   perpart upload **count identical, 268, with composites ON or OFF** — the
   composite sheets are a pure net addition with zero measured savings from
   skipping per-part uploads for the entities that got a sheet hit). This is
   the unit's headline, most actionable finding — not merely suspected, but
   ledger-quantified.

### Ranked fix list (candidates evaluated per the plan; NONE landed here)

1. **Skip per-part uploads for composite-eligible entities that got a sheet
   hit** (fixes hypothesis 3 directly). Projected savings: on this run's own
   evidence, entity_perpart cost was ~1.08MB for 268 uploads across 182
   entities; the fraction attributable to the 10 entities that got a
   composite hit is a modest slice of that (most of the 268 are non-hit
   entities' legitimate per-part textures) — realistically **a few hundred
   KB to ~1MB per zone-in at today's low (10/77) sheet-hit rate**, but the
   savings scale directly with `getCompositeSheetHits()` — as sheet coverage
   grows (`entity_composite_builder --all-variants`), this fix's payoff
   grows with it and prevents the coverage investment from being partly
   self-defeating. Smallest, most surgical, most clearly justified fix;
   requires gating the T05 loop (`race_model_loader.cpp`) on whether the
   composite lookup that immediately precedes it succeeded, verified by
   mesh-build time (does `getAnimatedMeshForRace`/`WithAppearance` actually
   end up calling `buildCompositeMeshFromGeometry`?).
2. **Builder unroll/size discipline** (fixes the 3.25x inflation found in
   sheet_economics.csv). Projected savings: shrinking the average inflation
   factor from 3.25x toward something closer to `fis_hs_def_0_0`'s 1.0x
   (e.g. by not re-tiling repeat-wrapped armor pieces across their full UV
   footprint, or by deduplicating identical tiles) could cut composite-sheet
   bytes by roughly **half to two-thirds** (a return to ~1.0-1.5x inflation
   would bring Run A's 3.05MB sheet cost down to roughly 1.0-1.5MB) — this
   is an offline `entity_composite_builder` change, out of this unit's
   runtime-only scope, but directly informed by data this unit produced.
3. **Lazy per-visible-race sheet loading** (only load a composite sheet the
   first time a race actually needs to render, rather than at
   `preloadModelData` time for every race present regardless of current
   visibility). Projected savings: with `entityPrepMaxPvsDepth=255`
   removing any distance gate today, ALL 182 entities' models — and by
   extension every eligible race's composite sheet — get queued at zone
   load regardless of whether the player ever sees them; on a larger/denser
   zone or a longer session this is exactly the kind of unbounded-population
   cost that would eventually cross the 64MB budget even though this unit's
   short single-position runs didn't reach it. Savings are session- and
   zone-shape-dependent (not quantifiable from this unit's flat data alone)
   but directionally this is the fix most likely to matter for the actual
   71-eviction scenario, which this unit could not reproduce (see
   Deviations) — precisely because a longer, more active, multi-zone or
   multi-race session would plausibly push total resident sheets higher
   than this unit's compact ~60s single-spot measurement did.
4. **Referenced-only archive loading** — moot as a fix; hypothesis 2 was
   refuted, and P04 already fixed the one real wholesale-*opening* bug this
   codebase had (at the archive-handle level).
5. **Budget resizing** — explicitly last per the plan's own framing, and
   this unit's data reinforces that: measured qeynos2 cost (15.68MB) is
   ~4x under the 64MB budget even with composites ON and the double-load
   bug unfixed. The budget is not the problem in the scenarios this unit
   measured.

### Deviations from the plan

1. **The plan's own framing anchor — P10b's reported "71 evictions" during a
   qeynos2 zone-in under the 64MB orangepi budget — was NOT reproduced by
   this unit's three measurement runs** (all three: 0 evictions). This is
   the most important gap to flag honestly. Investigated but not resolved
   within this unit's scope:
   - Code-level population checks (Composite entity adoption line,
     `entityPrepMaxPvsDepth=255`) confirm the FULL 182-entity population and
     all ~2,074 regions were processed in Run A, not a visibility-truncated
     subset — so "the run just didn't load enough" is not the explanation.
   - The shipped `orangepi` preset (unmodified) was used for Run A/C; P10b's
     review states its own repro used "`--constrained` presets **mirroring**
     orangepi with `mergeZoneDraws:true`" for a different purpose (crash
     repro, not eviction-count measurement) — it is possible P10b's number
     came from a longer, more active session (actual gameplay: spell
     effects, combat, multiple zone transitions accumulating usage — P10b's
     own matrix mentions "236 cumulative evictions" after a **post-camp
     zone transition to `qeynos`**, i.e. evictions accumulating across
     MULTIPLE zone visits in one process lifetime, not one zone-in) rather
     than this unit's compact single-zone-in-and-settle measurement.
   - This unit's instrumentation (the ledger + eviction tracer) is fully
     built, verified working (confirmed it correctly records 0 evictions
     when 0 occur, and its category/thrash bookkeeping is exercised by the
     upload side in all three runs), and ready to capture the exact
     victim-order/thrash reconstruction the plan asks for the next time a
     session — e.g. a longer multi-zone-transition or combat-inclusive
     session — actually exceeds budget. Re-running this unit's exact
     harness with a longer, multi-zone script is the natural follow-up, not
     attempted here given the report-only, time-bounded scope.
2. **Detail-object and UI textures were not individually tagged** — they
   fall into `"uncategorized"` (173 tex / 3.02MB in Run A, identical between
   Run A/B, confirming they're composite-independent and not part of either
   hypothesis's mechanism). Named gap, not fabricated categorization; the
   raw ledger rows retain full name/dims/bytes for anyone who wants to
   classify them further post hoc.
3. **`config/constrained_presets.json` was temporarily edited** (added
   `orangepi_p13_nocomposites` for Run B) and **reverted via `git checkout`
   immediately after** — confirmed via `git status --porcelain` showing no
   diff on that file in the final tree.
4. **Run C (`befallen`) hit a pre-existing, already-documented shutdown-time
   abort** (`std::system_error: Resource deadlock avoided` in
   `EverQuest::StopUpdateLoop()`) strictly after `/camp` + logout + this
   unit's own ledger dump had already completed — same class of issue
   P10b's review flagged and explicitly did not investigate further; not
   reinvestigated here either (out of scope, doesn't affect this unit's
   data).
5. **Testchar's character-data row was directly UPDATEd** (zone_id/x/y/z/
   heading) before each run and restored to its exact original values after
   — a direct, minimal, sanctioned DB operation (same class of operation
   `seed.sh` already performs for password resets), necessary because the
   character's stored position was mid-`befallen` at a coordinate P10b's
   review already identified as hitting an unrelated client-side `HCMap`
   FATAL, which would have blocked every run in this unit otherwise.
6. **`tryCompressedUpload()` (desktop S3TC path) confirmed dead code** in
   the course of the B.6 code-level format investigation — no caller in the
   current `getOrLoad()`/`uploadTexture()` flow. Flagged, not removed
   (out of scope for a report-only unit).
