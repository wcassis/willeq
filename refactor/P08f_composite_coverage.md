# P08f — Composite coverage: teach the builder zone archives, generate the missing sheets

Approved by Will 2026-07-26 ("go ahead with the builder-extension unit").
Slot: wt/slot1, branch task/P08f-composite-coverage, cut from fable
(f29ad39). Tool + assets unit — NO production renderer changes (P08d's
runtime already consumes sheets generically, including nonzero uniform
variants). Composites are LIVE on the orangepi preset (P08d, blessed);
this unit raises effective coverage from ~10-21% of real zone rosters
toward all no-override NPCs.

## Motivation (P08d coverage audit)

The 210 existing sheets cover the 42 global player-race model defs × 5
variants. `entity_composite_builder` has a hardcoded global-only archive
list, so zone-native NPC models (the CAZ/INN/BEA-shaped majority of
befallen/qeynos2/gfaydark rosters) are structurally unbuildable →
~10-21% eligibility. P08c's measured entity win (33.1%/56.0% p95) scales
with coverage.

## Design

### A. Builder extension (tools/entity_composite_builder.cpp)

1. Add zone-archive support: `--zone <shortname>` opens the zone's
   character archives ({zone}_chr.s3d and any numbered/sibling variants
   the loader itself consults — mirror RaceModelLoader's archive search
   order rather than inventing one), enumerates the race models inside,
   and builds sheets per (model, gender, variant) exactly like the
   global path. `--all-zones` iterates docs/reference/
   pre_luclin_zones.json. Keep global behavior byte-identical.
2. Sheet naming/keying must match what the RUNTIME looks up
   (race_model_loader.cpp: `{modelname}_0_{textureVariant}` JSON +
   EQE1 entry) — verify against the runtime code, not assumption; if a
   zone model's name collides with a global model name but has different
   content, detect and report (do not silently overwrite global sheets).
3. **Animated-texture exclusion (the Z35-class builder gap, now
   enforced)**: detect materials with multi-frame/animated textures
   during sheet build and SKIP those models entirely (they would bake
   frame 0 and freeze). Emit them in the report as
   ineligible-by-animation; do not bake silently.
4. Variant enumeration for zone models: build the variants that actually
   exist in the archive textures (the {race3}{part2}{variant2}{page2}
   naming), not a fixed 0-4 sweep; report per-model variant sets.

### B. Generation + audit

5. Generate sheets for the three golden zones first (befallen, qeynos2,
   gfaydark), then `--all-zones` if runtime is reasonable (report time;
   it's offline x86 — hours are acceptable, days are not; if --all-zones
   is too slow, golden zones + a documented invocation for the rest).
   Output alongside the existing 210 at /mnt/projects/EverQuest_ETC1
   (RGBA .bmp + .eqe1 + .json per P08d's format expectations — match
   the existing sheets' exact format/layout conventions).
6. Structural audit of every NEW sheet (P08d's sheet_audit method,
   extended to the new set) — broken sheets deleted + reported, never
   left on NFS.
7. **Coverage re-audit**: recompute the eligibility table for the three
   golden-zone rosters (P08d's method) before vs after. Target: every
   no-override, non-animated-texture NPC model in those rosters has a
   sheet. Report the residual ineligible list with reasons (per-slot
   overrides / animated textures / unbuildable-other).

### C. Verification + evidence

8. Standard bar: build (the tool builds in the regular cmake build),
   ctest (known host exceptions), gles2-x86 build, visual gate PASS
   goldens untouched, ARM build, file-size check.
9. **Static render spot-checks** (the P08d bar, valid geometry only —
   NEVER --animate): pick 4-6 REPRESENTATIVE NEW zone models (mix of
   zones/body types incl. at least one non-humanoid if present), render
   composite vs multi-buffer via the benchmark's --composite-scenegraph
   route with close-up framing (P08d's no-overlap lesson: small entity
   counts, verify no co-location before comparing), pixel-diff each.
   Bar: pixel-identical (0 differing) per P08d precedent; any diff =
   investigate or mark that model's sheets ineligible + report. Contact
   sheet → benchmarks/contact_sheets/p08f_composite_coverage/ for
   Will's spot-review (no self-declared acceptability).
10. **Runtime adoption demonstration**: whatever cheap path exercises a
    zone-native model through the production composite lookup (benchmark
    entity placement if it can place zone models; otherwise a zone-load
    log capture showing the adoption INFO line counting the new
    eligibility on a golden-zone roster). Report the achieved adoption
    numbers.

## Out of scope

- Production renderer changes of any kind.
- Per-player/runtime baking; billboards; the --animate root-cause unit;
  fixing gles2_composite_audit's device bug (workaround sweep is fine).
- Baking animated-texture models (excluded by design, reported).
- Pi sessions (composites are read off NFS identically on x86; adoption
  proof on x86 suffices — the Pi check rides along with Will's next
  play session).

## Review

### Setup

Checked out `task/P08f-composite-coverage` from `fable` at `9f78284` (this plan's own commit —
the plan file only existed on the tip, per the checkout-first instruction). Slot `wt/slot1`.

### A. Builder extension (`tools/entity_composite_builder.cpp`)

**A1. Zone-archive support, global behavior byte-identical.** Added a NEW, separate code path
in `main()` gated on `--zone`/`--all-zones` (`zoneMode`) — the original global-only flow
(`--race`/`--all`) is untouched source, same call arguments, same functions. Zone-archive access
mirrors `RaceModelLoader::loadZoneModels` exactly (`model_loading.cpp:140`, confirmed by reading
the runtime code, not assumed): a SINGLE `{zone}_chr.s3d` file per zone, no numbered siblings —
zone chr archives are never split the way the 7 `global*_chr.s3d` files are. New `--zone
<shortname>` is repeatable (accumulates) and `--all-zones` reads the full roster from
`docs/reference/pre_luclin_zones.json` via `--zones-json <path>` (required with `--all-zones`,
since the tool has no fixed path to that file). New `--max-variant <N>` (default 9) bounds A4's
variant-existence probing; new `--report-json <path>` writes a structured per-zone,
per-model build report.

**Byte-identity proof**: rebuilt `HUM_HS_DEF` (all 5 variants, `--race HUM --all-variants --eqe1`)
via the UNCHANGED code path into a scratch dir and byte-compared (`cmp`) against the committed
sheets at `/mnt/projects/EverQuest_ETC1/hum_hs_def_0_{0..4}.{bmp,json,eqe1}` — **15/15 files
byte-identical** (5 variants × 3 file types).

**A2. Runtime keying + collision detection.** Verified against `race_model_loader.cpp:807-810`
(`RaceModelLoader::preloadModelData`): the runtime builds `compositeBaseName = modelName + "_0_" +
textureVariant` from the model's race name ALONE — no zone qualifier, gender always 0 (the race
name already encodes gender, e.g. HUM vs HUF). The builder's existing naming
(`toLower(model->name) + "_0_" + variant`) already matches this exactly; no change needed to the
naming scheme itself, only to how models are DISCOVERED. This is precisely why collision detection
matters: zone and global sheets share one flat output directory with no zone-qualified names.

Implemented a collision registry (`RegisteredModel`: source archive + a "content signature" —
the sorted set of body-part texture names a model's triangles actually reference, via
`collectSignature()`). Seeded from every global model (`registerArchives`, quiet, for
comparison only — global sheets are never rewritten by this path) plus, on top, an on-disk
sidecar (`p08f_zone_signature_registry.json` in the output dir) persisting zone-sourced
signatures ACROSS separate tool invocations — closes a real gap a purely in-memory registry
would leave (two separate runs, e.g. golden zones today / `--all-zones` next week, must still
detect a collision between a model built in an earlier run and one found in a later run). `--zone`
is also repeatable within one process specifically so that a single invocation covering multiple
zones gets full cross-zone collision detection without depending on the sidecar file at all.

For each zone model: same race name + matching signature + an existing sheet already on disk
(checked directly against `outputDir`'s actual `.bmp` files, NOT registry membership alone — see
the bug found and fixed below) → `duplicate`, skip. Same name + matching signature + NO sheet
yet → build it (belated coverage). Same name + DIFFERENT signature → `collision`, reported,
**not built** (never silently overwrites).

**Bug found and fixed during real generation**: the first collision-registry design treated ANY
model name found in a global archive as "already covered," but the 210 pre-existing sheets cover
only 42 of the ~59 distinct global race defs found in `global2-7_chr.s3d` (some global race defs,
e.g. `IMP_HS_DEF`, were apparently never included in whatever invocation originally produced the
210 set). This silently skipped real, buildable, previously-uncovered models (`IMP` in
befallen/qeynos2) as false "duplicates." Fixed by cross-checking registry membership against an
actual directory scan of existing `.bmp` files (`existingSheetRaces`) before deciding
duplicate-vs-build — re-verified empirically: `imp_hs_def_0_0` did not exist in
`composite_index.json` before the fix, exists (correctly built) after.

**Real collision caught in production data**: `WOL_HS_DEF` (Wolf) in `gfaydark_chr.s3d` has a
DIFFERENT texture signature than the existing `global6_chr.s3d` WOL model — correctly detected,
reported, and **not built** (the existing global WOL sheet is untouched; gfaydark's wolf NPCs
remain ineligible until someone investigates which content should actually ship, out of this
unit's scope per A2's "report, never silently overwrite").

**A3. Animated-texture exclusion.** Implemented in `processModel()` via a new
`checkAnimatedTextures` parameter (default `false`) — the ORIGINAL global call site
(`--race`/`--all`) is unchanged text, so it never performs this check, preserving A1's byte-
identical guarantee unconditionally rather than merely empirically. The zone-mode path always
passes `checkAnimatedTextures=true`. Detection reads `part->textureAnimations()` (aligned to
`part->textureNames()` by `tri.textureIndex`, from the WLD 0x30/0x31 material-list fragments,
NOT the simpler `S3DZone::characterTextures` map, which does not carry animation info for
character textures) — if ANY body-part texture a model's triangles reference is a multi-frame
animated material, the WHOLE model is skipped (both a per-model probe before the variant loop,
`detectAnimatedTexture()`, and a redundant in-`processModel` check for defense-in-depth) and
reported as `animatedSkip` with the offending texture name. **Result across all three golden
zones: 0 animated-texture skips** — none of the 76 newly-discovered zone-native models in
befallen/qeynos2/gfaydark use animated textures (empirically checked, not just theorized, unlike
P08d's equivalent finding which was budget-limited to 6 sampled races).

**A4. Variant discovery from archive contents.** `discoverVariants()`: for each of a model's
resolved body-part textures, probes `applyVariant(name, v)` for v=1..`--max-variant` and includes
v in the built set only if the archive actually contains a DISTINCT variant file (not the
fallback-to-base-texture case `entity_composite_builder`'s existing `applyVariant()` already
does when a numbered variant is missing) — avoids silently rebuilding N duplicate copies of
variant 0. Per-model discovered/built variant sets are recorded in the JSON report
(`variantsDiscovered`/`variantsBuilt` per race). Global path's fixed 0-4 `--all-variants` sweep
is untouched (A1).

### B. Generation + audit

**B5. Golden-zone generation.** All three golden zones (befallen, qeynos2, gfaydark) built in a
SINGLE invocation (`--zone befallen --zone qeynos2 --zone gfaydark --sheet-size 2048 --eqe1`,
output directly to `/mnt/projects/EverQuest_ETC1`) — one process run, not three, specifically so
the in-memory collision registry covers cross-zone collisions too (see A2). `--sheet-size 2048`
(vs the global path's untouched default 1024) was needed for 2 models (`GAR_HS_DEF`,
`ORC_HS_DEF` in befallen) whose textures don't fit in a 1024×1024 shelf-packed sheet — an
existing tool capability (`--sheet-size` already existed), not a builder change; the global path's
default remains 1024 (A1). **Result: 76 sheets built, 14 duplicates (already covered via a
global or earlier-zone sheet), 1 collision (`WOL_HS_DEF`, reported not built), 0 animated-texture
skips, 0 build failures. Elapsed: 49.9s** (offline, x86, well inside the "hours OK" budget — full
`--all-zones` was not attempted this unit; see Deviations). `--all-zones`'s invocation is
documented (`--all-zones --zones-json docs/reference/pre_luclin_zones.json --sheet-size 2048
--eqe1 --report-json <path>`) but not run.

**B6. Structural audit.** `refactor/scratch/p08f_sheet_audit.py` (ad hoc, not committed to
`tools/`, matching P08d's own precedent for this kind of one-off) checks per sheet: `.bmp`/
`.json`/`.eqe1` all exist, JSON parses with a non-empty texture map, BMP header dimensions match
the JSON's `sheetWidth`/`sheetHeight`, BMP isn't truncated below its expected raw size, EQE1 is
non-trivially sized with a readable header. **Result: 76/76 new sheets clean, 0 broken**
(`benchmarks/contact_sheets/p08f_composite_coverage/sheet_audit_new.json`); re-ran against the
FULL 286-sheet set (210 original + 76 new) for good measure — **286/286 clean, 0 broken**
(`sheet_audit_all286.json`). Nothing needed deleting.

**B7. Coverage re-audit.** Recomputed eligibility (source of truth: the build report's per-model
status, cross-checked against which sheets exist on disk) for the three golden-zone rosters,
before vs after:

| zone | roster size | before (sheets) | before % | after (sheets) | after % | residual ineligible |
|---|---|---|---|---|---|---|
| befallen | 19 | 2 (CAZ, INN) | 10.5% | 19 | **100.0%** | none |
| qeynos2 | 14 | 3 (BEA, CAZ, INN) | 21.4% | 14 | **100.0%** | none |
| gfaydark | 18 | 1 (INN) | 5.6% | 17 | **94.4%** | WOL_HS_DEF (collision vs global6_chr.s3d, different content under the same name) |

(`benchmarks/contact_sheets/p08f_composite_coverage/coverage_before_after.json`.) The
"before" numbers reproduce P08d's original audit exactly for befallen (2/19, 10.5%) and qeynos2
(3/14, 21%) once the IMP false-duplicate bug (A2) is fixed — confirms the coverage MEASUREMENT
methodology is consistent across units, not just the new build. Target met: every no-override,
non-animated-texture, non-colliding NPC model in all three golden-zone rosters now has a sheet.

### C. Verification + evidence

**Standard bar**: `cmake --build build -j24` clean; `ctest --output-on-failure`: 1223/1225
passed, the two failures (`AudioManagerTest.PreloadCommonSounds`,
`RDPServerFrameTest.UpdateFrameMultipleTimes`) exactly match the plan's documented known-host
exceptions; `scripts/build-gles2-x86.sh` clean (same pre-existing `-Wformat-truncation` warnings
from `logging.h` noted in P08d, unrelated to this unit); `visual_gate` PASS standalone (goldens
untouched — this unit renders no zone/gate-relevant geometry changes, tool-only); ARM
cross-compile (`scripts/build-arm-noble.sh`) clean, `gles2_zone_benchmark` cross-compiles with
the new `--test-races` flag. `scripts/refactor/check_file_sizes.sh --files <changed set>` (run
via `--files` with absolute paths — same documented reason as P08d: this environment's shell
tool resets cwd between calls and disallows `cd`, so the script's default `git diff` mode can't
resolve `$PWD`): `entity_composite_builder.cpp` 1585 lines (WARN, crossed 1500 newly — accepted,
WARN is advisory only per the check's own design), `gles2_zone_benchmark.cpp` 2778 lines (WARN,
already WARN at baseline 2761), `zone_bench_loader.cpp` **3000 lines exactly** (WARN, not FAIL —
baseline was 2999, one line under the 3000 FAIL ceiling; the initial addition pushed it to 3015,
a genuinely NEW FAIL, so the addition was trimmed — comments compacted, the raceIds-override and
per-entity EQ-position debug print reduced to their essential lines — until back at the ceiling
boundary; verified the trimmed tool still builds and runs correctly on both x86 and ARM after the
edit), `zone_bench_loader.h` 454 lines (ok). **No new FAILs.**

**C9. Static render spot-checks (5 models, no --animate).** Picked BAT_HS_DEF (befallen,
non-humanoid bat), ZOM_HS_DEF (befallen, humanoid undead), SPI_HS_DEF (gfaydark, non-humanoid
spider), ORC_HS_DEF (gfaydark, humanoid orc), WIL_HS_DEF (gfaydark, non-humanoid will-o-wisp) —
2 zones, 3 non-humanoid + 2 humanoid, all 5 newly-buildable-only-by-this-unit models (structurally
unbuildable before A1). Method: `gles2_zone_benchmark --entities 1 --test-races <id>
--orbit-anchor-surface --orbit-radius 6 --dump-frames <f>` for baseline (default entity draw
path, single entity, no co-location per P08d's z-fight lesson), camera path captured via
`--dump-path` and replayed byte-identically via `--path-file` with `--composite-scenegraph` for
the composite side (the real `compositeEntityPath` lookup routed through the SAME draw dispatch
as baseline — P08d's apples-to-apples fix, reused here). Framing note: the benchmark's
orbit-fallback camera orbits a zone/surface anchor point, not the placed entity itself, and these
are small creature models — needed a new one-line-per-entity EQ-position debug print (added to
`placeEntitiesAlongPath`, kept — see file-size section) plus iterating `--orbit-radius`/frame
index per model to find a frame with the entity actually in view; qeynos2's surface anchor point
(inside a walled rooftop/courtyard) never yielded a usable frame within a reasonable number of
attempts and was dropped in favor of a 5th befallen/gfaydark model — see Deviations.

**Result: all 5 pairs pixel-identical — 0/480,000 differing pixels, mean |Δ|=0.000, max
channel Δ=0** (`benchmarks/contact_sheets/p08f_composite_coverage/spot_check_diffs.json`).
Contact sheet with side-by-side images: `benchmarks/contact_sheets/p08f_composite_coverage/
index.html`. **Not self-declared acceptable — for Will's spot-review.**

**C10. Runtime adoption demonstration.** `gles2_zone_benchmark --zone befallen --composite-
entities --test-races 34,17,35,67,89,53,33,31 --entities 8` (8 zone-native befallen races, all
newly built this unit: BAT/ORC/RAT/ZOM/IMP/MIN/GHU/FPM) exercises the REAL production lookup
path (`RaceModelLoader::preloadModelData` → `compositeEntityPath_` → JSON+EQE1, confirmed by the
tool's own log lines, not the audit tool's separate dynamic-atlas mechanism — same distinction
P08d drew). **Result: 8/8 preloaded OK, 8/8 `compositeData=YES` with the correct
`compositeBmpPath` for every race** (`benchmarks/contact_sheets/p08f_composite_coverage/
adoption_befallen.log`) — 100% adoption for this previously-0%-covered race set, i.e. the
before/after in the B7 table (2/19 → 19/19 for befallen) demonstrated live through the actual
runtime composite-selection code, not just inferred from file presence on disk.

**Race-ID coverage gap found (out of scope, flagged for whoever needs it next)**: several
befallen/gfaydark race codes with brand-new sheets from this unit (`CAZ`, `DEN`, `GAR`, `CUB`,
`GOL`, `MIM`, `REA`, `TEN`, `ZOF`) have **no entry at all** in `race_codes.cpp`'s
`getRaceCode()` switch table — i.e. no numeric EQ race ID resolves to them in this codebase
today. This is a pre-existing gap in the race-ID table (not something this unit created or is
asked to fix — the plan scopes this unit to the composite BUILDER and sheet generation, not the
race-ID mapping), but it means those specific sheets, though correctly built and structurally
sound, may not be reachable via the production per-entity code path until the race-ID table is
extended. Not investigated further (budget); flagged here rather than silently assumed fine.

### Deviations from the plan

- `--all-zones` was not run — golden zones (49.9s for all three combined) are well inside the
  "hours OK" budget, but a full pre-Luclin roster sweep is a materially larger, multi-hour
  undertaking not attempted this pass; the invocation is documented (A1/B5) per the plan's
  explicit fallback ("golden zones + a documented invocation for the rest").
  `entity_composite_builder`'s zone-mode code path is fully general (works identically for any
  zone with a `_chr.s3d` file) and was validated on 3 zones; nothing zone-specific was
  hardcoded.
- C9's spot-check set is 5 models, not 6 — qeynos2 could not be framed usefully within a
  reasonable number of attempts (its `--orbit-anchor-surface` point lands inside a walled
  rooftop/courtyard area where the small creature entity never entered the camera's view across
  many radius/frame combinations tried); substituted a 5th befallen/gfaydark model instead of
  forcing a 6th from qeynos2. The plan's "4-6" range is satisfied; qeynos2 IS represented in the
  coverage-audit table (B7) and the C10 registry-collision finding (WOL) is from gfaydark, so
  coverage isn't purely befallen-and-gfaydark-only across the whole evidence package.
  `gles2_zone_benchmark`/`zone_bench_loader.cpp` gained a permanent one-line-per-entity EQ
  position debug print as a byproduct of this framing work (kept — genuinely useful for any
  future spot-check work, not removed after use), and a new `--test-races` flag +
  `BenchConfig::testRaceIds` (tool-only, default-empty, inert unless passed — the default
  player-race-cycling behavior for existing invocations without `--test-races` is unchanged).
- Animated-texture exclusion (A3) is applied ONLY in the new zone-mode path
  (`checkAnimatedTextures` defaults `false`, the global call site doesn't pass it) — this was a
  deliberate choice to make A1's byte-identical guarantee unconditional (source-identical call
  site) rather than merely empirically true for the one sheet byte-compared. Consequence: IF any
  of the 42 global race defs turns out to have an animated texture, the global path would still
  silently bake frame 0 for it (unchanged pre-existing behavior, not newly introduced or newly
  discovered — out of scope, same as P08d's equivalent finding).
  Zone-mode empirically found zero animated-texture models across all three golden zones.
- The race-ID table gap (`CAZ`/`DEN`/`GAR`/`CUB`/`GOL`/`MIM`/`REA`/`TEN`/`ZOF` unmapped in
  `race_codes.cpp`) is reported (C10) but not fixed — out of this unit's scope (builder + sheet
  generation, not the race-ID table), and fixing it would be a production-code change this
  tool-only unit is not authorized to make.
- `p08f_sheet_audit.py` is ad hoc in `refactor/scratch/`, not added to `tools/`, matching P08d's
  own precedent for this class of one-off verification script.
- No production renderer code was touched anywhere in this unit (verified: `git diff --stat`
  against `fable` touches only `tools/entity_composite_builder.cpp` and
  `tools/hwcaps/{gles2_zone_benchmark.cpp,zone_bench_loader.{cpp,h}}`). No `--animate` was passed
  to any command in this unit's evidence gathering. Generated sheets live on NFS
  (`/mnt/projects/EverQuest_ETC1`, including the new `p08f_zone_signature_registry.json` sidecar
  and the merged `composite_index.json`) and were never staged into git; only audit JSONs, the
  build report, and the contact sheet (images + HTML) under `benchmarks/` are committed.
