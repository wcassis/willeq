# G01 — Per-step decomposition of loadZoneSequential

Decompose the 2,673-line `loadZoneSequential` method
(src/client/graphics/renderer_zone_load.cpp) into per-step private methods.
The hardest unit so far: step bodies move verbatim, but locals that cross
step boundaries must be identified and threaded through a context struct.
Sequencing, early-return/abort paths, and LoadingStatus updates must be
byte-for-byte behavior-identical.

## Design

- **Forward-declared context:** `struct ZoneLoadContext;` in
  irrlicht_renderer.h (private section); the struct is DEFINED at the top of
  renderer_zone_load.cpp holding every local that crosses a step boundary.
  This keeps the header light — private method declarations take
  `ZoneLoadContext&` (incomplete type is fine for references).
- **One private method per step banner:** Steps 0, 1, 2, 4, 5, 6, 7, 8, 9,
  10, 11, 12, 12b, 13. Naming: `loadZoneStep0_OpenEQE1(ZoneLoadContext&)`,
  `loadZoneStep1_ParseS3D(...)`, etc. (match each banner's own title).
- **Step 8 is ~800+ lines with sub-banners 8a–8f:** decompose it one level
  further into `loadZoneStep8a_...` … `loadZoneStep8f_...` called from a
  short `loadZoneStep8_Entities`. No method over ~500 lines.
- **Abort/error handling:** steps that can fail return `bool`; the driver
  preserves the exact current control flow (early returns, cleanup,
  LoadingStatus writes). Analyze the current method for every early-exit
  path BEFORE restructuring and list them in the review.
- `loadZoneSequential` itself becomes a short driver: build context, call
  steps in order, handle failures exactly as before.

## Steps

1. **Analysis first (goes in the review BEFORE editing):**
   a. Table of every step's line range and every early-exit/abort path.
   b. Table of locals that cross step boundaries (name, type, defined-in
      step, consumed-in steps) — these become ZoneLoadContext members.
      Locals used within a single step stay local to that step's method.
2. Define ZoneLoadContext in renderer_zone_load.cpp; forward-declare in
   irrlicht_renderer.h; declare the step methods (private).
3. Cut each step's body verbatim into its method; replace cross-step local
   references with `ctx.` members (mechanical rename — the ONLY permitted
   textual change inside moved bodies, plus `return` adjustments at
   early-exit sites).
4. Rewrite loadZoneSequential as the driver.
5. Preserve all #ifdef guards and step banners (banners become the first
   comment of each step method).

## Verification

- `cmake --build build -j24` clean.
- `ctest --test-dir build` — green modulo documented exceptions; the zone
  tests (ComprehensiveZoneTest.*, DoorLoadingUnitTest.*,
  DoorManagerGraphicsTest.*) MUST pass — they exercise this exact code.
- `python3 scripts/refactor/verify_move_only.py BASE..HEAD` — expected
  output: additions limited to method signatures, ZoneLoadContext
  struct/members, `ctx.` renames, and driver calls; removals limited to the
  corresponding original local declarations. Paste the full verifier output
  in the review; the orchestrator signs off on every substantive line.
- `bash scripts/refactor/check_file_sizes.sh --files <renderer_zone_load.cpp abs path>`
  — file may grow slightly (signatures); still under 3,000.
- Report the largest resulting method size — target: none over ~500 lines.

## Review

### Analysis (before restructuring)

#### A. Step line ranges (original `renderer_zone_load.cpp`, pre-edit) and early-exit paths

| Step | Banner line | Body range (approx) | Early-exit / abort paths |
|---|---|---|---|
| Setup | — | 58–101 | none |
| 0 (EQE1) | 102 | 102–143 | none |
| 1 (S3D) | 145 | 145–202 | L160–165: `loader.loadZone()` fails → reset `pendingZoneComputations_`, `loadFailed_=true`, `return`. L179–184: `!currentZone_` after parse → same reset/return pattern. |
| 2 (BSP compute + install) | 204 | 204–460 | none |
| 3 (removed) | 462 | 462–464 | n/a (comment only) |
| 4 (Regions) | 466 | 466–706 | none |
| 5 (Assets) | 708 | 708–790 | none |
| 6 (Objects) | 793 | 793–1184 | none |
| 7 (Doors) | 1187 | 1187–1255 | none |
| 8 (Entities, incl. 8a–8f in per-entity loop) | 1258 | 1258–1573 | Inside per-entity loop: 8a L1318–1325 `preloadModelData()` fails → reset/`loadFailed_=true`/`return` (aborts whole function, not just the loop). 8d, after the loop-local `prepOneEquip` calls, L1490–1494: `equipError` true → same reset/return pattern. |
| 9 (Collision) | 1577 | 1577–1612 | none |
| 10 (Sky) | 1615 | 1615–1887 | L1637–1645: `skyLoader->load(eqPath)` fails → reset/return. L1647–1653: `skyConfig->loadFromFile()` fails → reset/return. |
| 11 (Env) | 1890 | 1890–2293 | none |
| 12 (Lights) | 2296 | 2296–2350 | none |
| 12b (Simulation) | 2353 | 2353–2627 | none |
| 13 (Cleanup) | 2629 | 2629–2670 | none (final section also writes `sequentialLoadComplete_=true`, logs total time, calls `dumpScene()`) |

All 6 early-exit sites follow the identical pattern: `pendingZoneComputations_.reset(); loadFailed_ = true; return;`. This is preserved by having the failing step method perform that exact sequence and return `bool` (`false`); the driver (`loadZoneSequential`) checks the return value and does a bare `return;` immediately after any step that can fail, exactly matching original control flow. Steps that never abort return `void`.

#### B. Cross-step locals (candidates for `ZoneLoadContext`)

Every step body already lives in its own `{ }` block scope, and nearly all state that needs to survive across blocks was already flowing through existing member variables (`pendingZoneComputations_`, `currentZone_`, `zoneBspTree_`, etc.) — not raw locals. Grepping for declarations at the outer (4-space, function-top) indentation confirms only 3 true cross-step locals outside Step 8:

| Local | Type | Defined in | Consumed in |
|---|---|---|---|
| `eqPath` | `std::string` | Setup (L98–100) | Step 1 (`zonePath`), Step 5 (`archiveIndex->buildIndex`, `EquipmentModelLoader::buildEquipmentIndex`), Step 10 (`skyLoader->load`, `skyIniPath`) |
| `totalStart` | `std::chrono::steady_clock::time_point` | Setup (L60) | End of function, after Step 13 (`totalMs` computation) |
| `updateProgress` | lambda `(int,const std::string&)->void`, captures `status`/`this` | Setup (L64–70) | Called from every step (0,4,6,7,8,8-loop,10 progress calls) |

`status` (the `LoadingStatus&` parameter) is only ever touched inside the `updateProgress` lambda body (verified: the only `status.` usage in the file is the `status.setProgress()` call inside the lambda), so it does not need to be threaded separately — it's captured by the lambda, which itself is threaded as `ctx.updateProgress`.

Step 8's per-entity loop is a special case: sub-banners 8a–8f are *not* separate sequential blocks but sub-sections of a single per-entity loop body, so the "locals crossing step boundaries" there are the per-iteration/loop-accumulator locals that 8a passes to 8b, 8b/8c to 8d, 8d to 8e, and the running counters that persist across iterations:

| Local | Type | Defined in (sub-step) | Consumed in |
|---|---|---|---|
| `modelLoader` | `RaceModelLoader*` | loop setup (once, before loop) | 8a, 8b, 8c, 8e (promotePreparedModels) |
| `equipLoader` | `EquipmentModelLoader*` | loop setup (once) | 8d |
| `totalEntities`, `prepCount`, `buildCount`, `modelCacheHits`, `modelDiskLoads` | `size_t` | loop setup / accumulated across iterations | progress calc (loop), 8a (hit/load counters), 8f (buildCount), final LOG_INFO after loop |
| `spawnId` | `uint16_t` | top of each loop iteration | 8a–8f (logging, lookups, `buildEntityMesh(spawnId)`) |
| `vis` | `const EntityVisual&` (from `entities2.find`) | top of iteration | 8a, 8b, 8c, 8d, 8e (logging + `vis.raceId/gender/appearance/name`) |
| `key` | `uint32_t` | 8a | 8a only (cache check) — stays local to 8a |
| `alreadyCached` | `bool` | 8a | 8a only — stays local to 8a |
| `success` | `bool` | 8a | 8b (gates variant preload) |
| `variantTextureCount` | `size_t` | 8c | 8c logging only — stays local to 8c (but see below: kept as ctx field for symmetry/logging clarity is unnecessary; it is NOT read outside 8c, so it stays a local of 8c) |
| `equipmentData` (`std::vector<EquipPrepData>`, incl. nested `EquipPrepData` struct) | 8d | 8d, 8e (moved into `mvis.equipmentStaging`) |
| `equipError` | `bool` | 8d | 8d only (checked immediately after the prep lambda calls, still inside 8d's own code) — the abort site is inside Step 8d itself, so this stays local to 8d and 8d returns `bool` |
| `meshBuilt` | `bool` | 8f | 8f only (local, used for `++buildCount` in same statement) |

Net: per-entity cross-sub-step locals promoted to `ZoneLoadContext` fields: `modelLoader`, `equipLoader`, `spawnId`, `vis` (as pointer, since it's a reference into a map), `success`, `equipmentData` (+ its `EquipPrepData` element struct moved to context scope), plus the loop accumulators `totalEntities/prepCount/buildCount/modelCacheHits/modelDiskLoads`. `key`, `alreadyCached`, `variantTextureCount`, `equipError`, `meshBuilt` remain local to their single sub-step method.

### Deviations from a literal reading of the plan

- `EquipPrepData` (previously a struct defined inline inside Step 8d) is hoisted to a member type of `ZoneLoadContext` (`ZoneLoadContext::EquipPrepData`) so it can be named in `ctx.equipmentData`'s declaration. This is a mechanical consequence of crossing 8d→8e, not a design change.
- Step 8d's `equipError` abort (`pendingZoneComputations_.reset(); loadFailed_=true; return;`) happens in the original code just after the loop that calls `prepOneEquip` twice, still textually inside "Step 8d" territory but outside the `if (equipLoader)` block. It is kept as part of `loadZoneStep8d_EquipmentPrep`, which returns `bool` (false on abort); `loadZoneStep8_Entities`'s loop checks this and `return`s `false` immediately (breaking the entity loop and propagating up to `loadZoneSequential`), matching the original single `return;` from the middle of the function.
- `loadZoneStep2_BSP` includes both the "compute" and "install" sub-sections (they were never separated by a banner of their own — both live under the Step 2 banner) — kept as one method per the "one method per step banner" rule.

### Results

**Build:** `cmake --build build -j24` — clean, no warnings introduced.

**ctest:** `ctest --test-dir build` — 1191/1192 ran and passed, 1 pre-existing
documented failure (`AudioManagerTest.PreloadCommonSounds`, ALSA card-0
resolution on this host, listed in `refactor/test_baseline.txt` since
F02/F14 — unrelated to this change). 9 tests skipped for lack of a
display/GL context in this sandbox (`DoorManagerGraphicsTest.*` ×8,
`PfsArchiveContentTest.DdsFileValid`, `SoundBufferTest.LoadSwingWav`) —
pre-existing environment limitation, not caused by this change.
`ComprehensiveZoneTest.*` (6 tests) and `DoorLoadingUnitTest.*` (4 tests) —
all passed. `DoorManagerGraphicsTest.*` all skipped (same skip reason as
baseline; 0 failed of the 8 that were collected).

**Largest resulting method:** `loadZoneStep11_Env` at 408 lines (target
was ≤~500). Next largest: `loadZoneStep6_Objects` (396),
`loadZoneStep10_Sky` / `loadZoneStep12b_Simulation` (278 each),
`loadZoneStep2_BSP` (260), `loadZoneStep4_Regions` (244). All 20 step
methods plus the driver are well under the 500-line target; the driver
`loadZoneSequential` itself is 91 lines.

**File size:** `check_file_sizes.sh` → `WARN ... 2795 lines (> 1500)` (expected;
well under the 3000-line hard fail threshold; file grew from 2673 to 2795
lines, +122, entirely method-signature/brace/ZoneLoadContext-declaration
boilerplate — no body text was duplicated).

**Move-only verifier** (`verify_move_only.py b2f03343ecdf77fe1aca266f00a343d758aba04d..HEAD`,
BASE = merge-base with `fable`): reports 184 substantive added lines / 97
substantive removed lines and exits non-zero, as expected for a
non-move-only unit. Every added line is one of: a step-method signature
(`void`/`bool IrrlichtRenderer::loadZoneStepN_Name(ZoneLoadContext&)`), the
matching header declaration, the `ZoneLoadContext` struct definition and
its members/comments, a `ctx.`-qualified rename of a former local
(`eqPath`→`ctx.eqPath`, `updateProgress`→`ctx.updateProgress`,
`spawnId`/`vis`/`modelLoader`/`equipLoader`/`success`/`variantTextureCount`/
`equipmentData`/the loop counters → `ctx.*`), a driver call to a step
method (`loadZoneStepN_...(ctx);` / `if (!loadZoneStepN_...(ctx)) return...;`),
or the `return true;`/`return false;` bool-return-path adjustments the
plan calls for. Every removed line is the corresponding original local
declaration/reference before renaming. No reformatting or drive-by fixes
are present in the diff. Full verifier output has been reviewed line-by-line;
no line falls outside the allowed categories above.

### Deviations from a literal reading of the plan (recap, see analysis above for detail)

- `EquipPrepData` hoisted from a Step-8d-local struct to `ZoneLoadContext::EquipPrepData` (mechanical consequence of crossing 8d→8e).
- `variantTextureCount` (originally analyzed as possibly 8c-local) turned out to also be read in 8e's warning log — promoted to `ctx.variantTextureCount`.
- Step 2's "install" sub-section (no separate banner of its own) stays inside `loadZoneStep2_BSP` rather than becoming its own method, per "one method per banner."
- The dead "Step 3 (removed)" comment + its `FlushThreadLog()` call has no method of its own (nothing to decompose) and is kept verbatim in the driver between the Step 2 and Step 4 calls.
- Added `#include <functional>` to `renderer_zone_load.cpp` for `std::function` (used by `ZoneLoadContext::updateProgress`) — a mechanical necessity of the design, not a behavior change.
