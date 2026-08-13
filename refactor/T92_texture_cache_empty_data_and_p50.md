# T92 — Who is handing the texture cache zero bytes? (plus the P50 query gap)

**Slot: slot2 or whichever is free. Headless first. Live leg only to
reproduce in `qeynos2` if the headless path cannot — eqemu lock normally.**

**Territory: `src/client/graphics/constrained_texture_cache.cpp` and its
callers, `tests/`, `refactor/`, `/mnt/projects/fable/review/T92/`.**

Read the P50 entry in `refactor/deferred.md` (search `P50 (2026-07-31)`),
then this plan in full.

## Part 1 — the real defect: empty buffers, not failed decodes

T85's `qeynos2` capture logged **30** of these at WARN:

```
Constrained cache: decode failed for 'hum_hs_def_0_0' (0 bytes)
Constrained cache: decode failed for 'qcm_hs_def_0_0' (0 bytes)
Constrained cache: decode failed for 'rat_hs_def_0_0' (0 bytes)
... 12 distinct names, mostly `*_hs_def_0_0` head textures, plus
    'ship_actordef_0_0'
```

The message is **misleading**. At `constrained_texture_cache.cpp:234-235`
the guard is `processTextureData(data, ...)` failing — but `data.size()` is
**0**. Nothing failed to decode; **the caller supplied an empty buffer.** The
defect is upstream of this function.

Establish:
- **Which caller** passes empty `data` (`getOrLoad(name, data)` has callers in
  `animated_texture_manager.cpp` and the entity/race/equipment loaders — find
  the one responsible for `*_hs_def_0_0` head textures).
- **Why** it is empty. Does the S3D/archive read return nothing, is the name
  wrong, is it a race with a background prep worker, or is the entry genuinely
  absent from the archive?
- Whether these names are even correct — `hum_hs_def_0_0`, `qcm_hs_def_0_0`
  and friends look like composite/variant keys. If a key is being constructed
  wrongly, the lookup would legitimately find nothing.

**Fix the message regardless of the cause.** A log that says "decode failed"
when the input was empty sent this investigation the wrong way once already;
distinguish empty-input from genuine decode failure at the call site.

## Part 2 — what this does and does not explain

**Do not assume this is the magenta/checkerboard defect.** These are character
head and actor textures, which would produce untextured NPCs, **not** magenta
walls and floors. State what the failure actually accounts for.

Separately: zone geometry logs its texture failures at **`LOG_DEBUG`**
(`zone_geometry.cpp:357`, `loadTextureFromBMP returned null`) while model
failures log at `LOG_WARN`. That inconsistency is why the wall magenta has
never been named. **Raise the zone-geometry texture failure to `WARN`** with
the texture name, then a single `qeynos2` run will say what is actually
failing on the walls. That is a small, clearly-scoped change and it is the
highest-value thing in this unit.

Also seen in the same capture, at DEBUG:
`buildTexturedMesh: texture 'crate1.bmp' has empty data` (also `crate2-4`,
`phook.bmp`, `pchain.bmp`) — same empty-buffer signature on the object path.
Note whether it shares a cause with Part 1.

## Part 3 — P50, small and separate

`ConstrainedTextureCache`'s by-name query methods — `hasAlpha(name)`,
`getTexture(name)`, `getAlphaTexture(name)`, `isETC1Texture(name)` — look up
`cache_` by plain name, but `uploadTexture()` stores masked variants under the
mangled key `cacheKey(name, true)` = `name + "\x01masked"`. A plain-name query
for a masked variant therefore always misses.

**P50's own entry records that this does not affect rendering correctness** —
`MaterialType` selection is driven by the WLD `textureMasked()` flag at
mesh-build time, never by these methods, and no known production caller
depends on them for a decision that matters. So this is API hygiene, not a
rendering fix. **Do not inflate it, and do not claim it fixes anything
visual.**

Take option (a) from the P50 entry: add an `applyPaletteMask` parameter to the
four query methods, mirroring the existing `getOrLoad()`/`uploadTexture()`
pattern. Update callers. If any caller turns out to *depend* on the current
missing behaviour, that is a finding — report it rather than papering over it.

## Tests

- A test that a masked-variant upload is findable through each of the four
  query methods with the new parameter, and still not found without it.
- A test covering the empty-buffer path once its cause is known — at minimum,
  that empty input is reported distinctly from a decode failure.
- **Prove the tests can fail**: break the behaviour temporarily, show red,
  restore, show green. Report that you did it.

## What you may not do

- Do not claim the Part 1 failures explain the magenta walls without evidence.
- Do not restructure the texture cache. If a fix needs that, **STOP and
  report** — it is its own unit.
- Do not remove `ConstrainedTextureCache`. It is live, widely used
  (`application.cpp`, `sky_renderer`, `door_manager`, `zone_shader`,
  `animated_texture_manager`, `weather_effects_controller`,
  `renderer_zone_merge`) and is **not** the `ConstrainedMeshCache` that T67
  removed.

## Verification

Full build and suite (expect 2070/2071 plus new tests; sole pre-existing
failure the documented ALSA `AudioManagerTest.PreloadCommonSounds`). Visual
gate — **a golden change here is a real signal**, since this path feeds what
is drawn: say so and explain why.

Findings to `/mnt/projects/fable/review/T92/` with a README. Open any image
you produce and describe what is actually visible; describe, do not grade.

## Review

**Part 1 — the caller and the mechanism.** The 30 `decode failed for
'*_hs_def_0_0' (0 bytes)` WARNs (12 distinct names, plus `ship_actordef_0_0`)
all come from exactly one call site:
`RaceModelLoader::preloadModelData()`'s composite-sheet trigger,
`race_model_loader.cpp:936` (line numbers as of this unit's start):

```cpp
std::vector<char> emptyData;  // No raw data — EQE1 lookup provides the data
constrainedTextureCache_->getOrLoad(compositeBaseName, emptyData,
                                     "composite_sheet", "preloadModelData:compositeLookup");
```

`compositeBaseName` is built as `modelName + "_0_" + textureVariant` (e.g.
`hum_hs_def_0_0`) and is looked up first as a **JSON composite manifest**
(`compositeEntityPath_/<name>.json`, `loadFromJson()`), which is entirely
independent of EQE1. When that JSON manifest exists, this call deliberately
passes an *empty* `data` buffer, on the documented assumption that
`getOrLoad()`'s `findCompressedEntry(name)` will resolve the actual pixel
bytes from an EQE1 flat pack. That assumption only holds when an EQE1
archive is actually configured for the run (`--eqe1-path`,
`IrrlichtRenderer::getOrOpenEQE1()` short-circuits to `nullptr` immediately
when `config_.constrainedConfig.eqe1Path` is empty). Traced against real
T76/T85 qeynos2 captures (`/mnt/projects/fable/captures/t76_before_idle_gfx.log`,
willeq config `t76_willeq.json` — no `eqe1_client_path`/`--eqe1-path` set):
`eqe1Loaders_` stays empty for the whole run, so `findCompressedEntry()`
returns `nullptr` without ever hitting the `FATAL`/`exit(1)` branch (that
branch only fires once at least one EQE1 loader is actually open — see
`snapshotEQE1Loaders()`), and the empty buffer falls straight into
`processTextureData()`, which correctly reports it cannot decode zero bytes.
**Nothing failed to decode anywhere; the composite-sheet lookup is EQE1-only
by construction and this run had no EQE1 archive configured**, exactly as
the plan suspected. This is upstream-of-the-cache caller behavior, not a
texture-cache bug — the composite JSON manifests are discovered and used
independently of whether the (separate) EQE1 flat-pack directory is
configured, so the two can silently disagree.

Not fixed here (the plan scopes this unit to the message, not a behavior
change to the caller, and PROHIBITS restructuring the cache): a real fix
would gate the composite-sheet lookup on `snapshotEQE1Loaders()` being
non-empty (or an equivalent "EQE1 configured" check) before ever calling
`getOrLoad()` with an empty buffer, so a composite JSON manifest with no
matching EQE1 archive falls straight to the per-part decode path instead of
generating a guaranteed-to-fail lookup. Flagging as a finding for a future
caller-side unit, per the plan's "report rather than paper over" instruction
for anything beyond message hygiene.

**Message fix**: `getOrLoad()` (`constrained_texture_cache.cpp`) now checks
`data.empty()` before calling `processTextureData()` and logs a distinct
`"Constrained cache: empty input data for '{}' — no bytes were supplied to
decode (not a decoder failure; check the caller: ...)"` WARN, naming the
three most likely upstream causes. The genuine-decode-failure message is
unchanged in spirit (`"decode failed for '{}' ({} bytes supplied)"`) but now
can only ever fire when `data.size() > 0`, so the two cases are structurally
impossible to conflate again.

**Part 2 — what this does and does not explain.** Confirmed: these are
`RaceModelLoader`/entity composite-sheet textures (character heads,
`ship_actordef`), which would produce **untextured entities**, not magenta
walls or floors — the plan's caution held. **No evidence connects Part 1 to
the magenta/checkerboard wall defect**; that defect (if still live — see
below) is a `ZoneMeshBuilder::buildTexturedMesh()` / `loadTextureFromBMP()`
matter, a completely different call path with its own texture map, never
touched by the composite-sheet code.

The zone-geometry log-level fix: `zone_geometry.cpp`'s
`buildTexturedMesh()`, the `loadTextureFromBMP() returned null` branch, was
`LOG_DEBUG` — invisible at every level actually run in production or in any
of the review captures inspected, while `RaceModelLoader`'s equivalent model
failures already logged at `LOG_WARN`. Raised to `LOG_WARN` (texture name
and byte count already in the message). Verification run: `gles2_zone_benchmark
--zone qeynos2 --eq-path /mnt/projects/EverQuestP1999 --entities 0/6 --log-level=WARN`
(and again at `--log-level=DEBUG`) produced **zero** `buildTexturedMesh`
texture-failure lines for qeynos2 in this environment — all zone-region
textures loaded successfully. **This means qeynos2's magenta/checkerboard
wall defect, if it is currently reproducible at all, is not caused by a
zone-geometry texture load failure in this build/dataset** — the raised WARN
gives a future run a real signal instead of a whispered one, but this run
found nothing to report for qeynos2. (The benchmark tool's `--entities N`
path also never triggered the Part 1 composite-sheet lookup at all — it uses
a different entity-loading path than the live client's
`RaceModelLoader::preloadModelData()`, so it could not be used to
re-reproduce Part 1's WARNs; Part 1 is evidenced entirely from the real
T76/T85 qeynos2 client captures instead.) The `crate1-4.bmp`/`phook.bmp`/
`pchain.bmp` "has empty data" DEBUG lines the plan also named
(`buildTexturedMesh`, the sibling branch to the one raised here) were not
reproduced in this session — no capture file accessible to this unit
contains them, and this environment's own qeynos2 benchmark run produced
none. They share the same **surface signature** as Part 1 (empty `data` at
the point of use) but a different **mechanism is plausible, not confirmed**:
Part 1's cause is a specific EQE1-only lookup convention in
`RaceModelLoader`; the object/crate path has no such convention (objects are
loaded from `_obj.s3d` via a plain textures map, no composite/EQE1 dance),
so if their data is genuinely empty it is more likely a missing/mismatched
S3D archive entry than the same root cause. Not conclusively determined —
flagged rather than guessed at.

**Part 3 (P50)**: Implemented option (a) — added `applyPaletteMask = false`
to `hasAlpha()`, `getTexture()`, `getAlphaTexture(name)`, and
`isETC1Texture(name)`; each now looks up `cache_` via the same
`cacheKey(name, applyPaletteMask)` `getOrLoad()`/`uploadTexture()` already
use. Callers audited (`grep` for all four methods' by-name overloads across
`src/client/graphics/` and `include/client/graphics/`): every existing call
site either (a) is a zone-geometry call that never masks (`zone_geometry.cpp`
— default `false` is byte-identical to before), or (b) uses the
`ITexture*`-reverse-lookup overloads (`entity_renderer.cpp`,
`renderer_zone_load.cpp`, `renderer_zone_stream.cpp`), which scan `cache_` by
pointer and were never affected by the key-mangling gap in the first place.
**One caller did turn out to depend on the missing behavior**:
`zone_geometry.cpp`'s own `loadTextureFromBMP()` (the one call site in the
whole codebase where `applyPaletteMask` is in scope at the moment `hasAlpha()`
is called) was calling `hasAlpha(name)` with no mask argument immediately
after uploading under `cacheKey(name, applyPaletteMask)` — for a
masked-only upload this always missed. Fixed to `hasAlpha(name,
applyPaletteMask)`. **This is reported, not silently absorbed, per the
plan's instruction — but it does not change any observed rendering**: the
result only feeds `texturesWithAlpha_`, which is unconditionally OR'd with
the WLD-derived `textureMasked()` flag at the actual `MaterialType`-selection
site (`buildTexturedMesh`'s `isMaskedMaterial || texturesWithAlpha_.find(...)`)
— for any texture WLD already marks masked, `isMaskedMaterial` alone already
forces the alpha-test material, so this fix can only ever ADD entries to
`texturesWithAlpha_` (never remove any), and only matters for the edge case
of a masked-uploaded texture name later referenced by an *unmasked* geometry
index. Visual gate ran clean (goldens unchanged) after this fix, consistent
with P50's original finding that no caller depends on these methods for a
decision that matters.

**Tests** (`tests/test_constrained_texture_cache.cpp`, 5 new, all pass;
`GTest::gtest_main` + `willeq_core`, `EQT_GRAPHICS AND IRRLICHT_LIBRARY`
gated like the sibling headless graphics tests):
- `GetTextureRespectsApplyPaletteMask` / `MaskedOnlyUploadNotFoundWithoutParameter`
  — real `getOrLoad()`/`uploadTexture()` round-trip on synthetic 24bpp BMPs,
  EDT_NULL device. Masked and unmasked variants of the same name resolve to
  distinct GPU textures; the masked-only variant is invisible to a
  no-argument query.
- `HasAlphaRespectsApplyPaletteMask` — `registerTexture()` used to plant
  masked/unmasked entries with opposite `hasAlpha` flags at the exact keys
  `uploadTexture()` would use; confirms the query only sees the matching
  variant.
- `ETC1QueriesRespectApplyPaletteMask` (separate fixture,
  `ConstrainedTextureCacheETC1Test`) — the one test that needs a *real* GL
  context, because `CachedTexture::isETC1`/`alphaTexture` are only ever set
  by the genuine ETC1 upload path (`uploadCompressedETC1`,
  `glCompressedTexImage2D`). Uses a real `EDT_OGLES2` surfaceless-EGL device
  (same backend `./build/bin/willeq` uses headlessly by default) against the
  real `/mnt/projects/EverQuest_ETC1/qeynos2_obj.eqe1` archive — the same
  asset `test_eqe1_loader.cpp` already depends on — uploading its known
  `fire1` alpha-companion entry as the masked variant only. Both
  `isETC1Texture`/`getAlphaTexture` find it with `applyPaletteMask=true` and
  correctly report absent without it. Self-skips (does not fail) if the
  archive or an EGL surfaceless context is unavailable.
- `EmptyInputReportedDistinctlyFromDecodeFailure` — drives real stdout
  through the threaded logger (`SetLogLevel(LOG_WARN)`,
  `testing::internal::CaptureStdout()`, `FlushThreadLog()` + a bounded poll
  for the 50ms background drain cycle) and asserts the empty-buffer case
  logs "empty input" and never "decode failed", while a genuine 64-byte
  non-BMP/DDS payload logs "decode failed" and never "empty input".

**Proof the tests can fail**: broke each fixed behavior in turn, rebuilt,
confirmed RED, restored, rebuilt, confirmed GREEN (all runs on
`test_constrained_texture_cache`):
1. `getTexture()` reverted to ignore `applyPaletteMask` (`internalKey = name`)
   → `GetTextureRespectsApplyPaletteMask` and
   `MaskedOnlyUploadNotFoundWithoutParameter` FAILED. Also reverted the
   empty-input WARN back to the old conflated message → 
   `EmptyInputReportedDistinctlyFromDecodeFailure` FAILED (3/5 red). Restored
   → 5/5 green.
2. `hasAlpha()` and `isETC1Texture()` reverted the same way (`internalKey =
   name`) → `HasAlphaRespectsApplyPaletteMask` and
   `ConstrainedTextureCacheETC1Test.ETC1QueriesRespectApplyPaletteMask`
   FAILED (2/5 red). Restored → 5/5 green.

**Verification.** Full build (`cmake --build build -j24`): clean. Full suite
(`ctest --test-dir build`): **2075/2076 passed** (99%), sole failure the
documented ALSA `AudioManagerTest.PreloadCommonSounds` — matches the
baseline exactly, no regressions, 5 new tests all green (also reconfirmed
isolated: `ctest -R ConstrainedTextureCache` → 5/5). `build-gles2-x86.sh`:
clean. Visual gate (`scripts/ci/visual_gate.sh`): **PASS, goldens
unchanged**. `build-arm-noble.sh`: clean cross-compile,
`build-arm-noble/bin/willeq` produced. `check_file_sizes.sh fable`: 0 FAIL
(3 pre-existing WARN files >1500 lines, none newly introduced by this
unit's edits — `constrained_texture_cache.cpp`/`zone_geometry.cpp` were
already over the threshold before this unit; the diff also surfaced
`race_model_loader.{h,cpp}`, `eq_action_handler.cpp`, `variant_loading.cpp`,
and several `refactor/*.md` files as "changed vs fable" — this is base-branch
drift (fable's tip has advanced past this branch's fork point with T90/T91/
T93 already merged), not anything this unit touched; confirmed via `git
status`/`git diff --stat fable` that this unit's actual working-tree changes
are exactly `constrained_texture_cache.{h,cpp}`, `zone_geometry.cpp`,
`tests/CMakeLists.txt`, and the new `tests/test_constrained_texture_cache.cpp`).

**Visual gate as signal**: a golden change here would have been meaningful,
since `loadTextureFromBMP()`/`buildTexturedMesh()` and the P50 query methods
sit directly on the zone-texture render path. None occurred — consistent
with this unit's own findings that (a) Part 1 never touches zone geometry at
all (it's an entity/composite-sheet-only code path) and (b) the P50 fix is
provably redundant with the WLD-derived `textureMasked()` flag for every
case that reaches a `MaterialType` decision today. An unchanged visual gate
here is real evidence for "hygiene, not a rendering fix," not merely an
absence of measurement.

**No live eqemu run**: this unit's diagnosis is evidenced entirely from
existing real captures (T76/T85, both genuine live qeynos2 client sessions
against eqemu) plus headless code-path tracing and a headless
`gles2_zone_benchmark` reproduction attempt (see Part 2) — eqemu was not
brought up for this unit; the lock was never taken.

**Deviations from the plan**:
- Plan's "expect 2070/2071 plus new tests" verification count is stale
  (fable has advanced past T92's fork point since the plan was written);
  actual pre-existing baseline at this unit's fork point was 2071 tests
  (2076 total minus the 5 new ones), 2070 passing + 1 documented ALSA
  failure — consistent with the plan's intent, just a different absolute
  number due to intervening merged units.
- Did not reproduce the `crate1-4.bmp`/`phook.bmp`/`pchain.bmp` "has empty
  data" lines the plan quotes from T85's capture (see Part 2) — no
  accessible capture file contained them and this session's own qeynos2
  benchmark run produced none. Noted as unconfirmed rather than fixed or
  dismissed.
- Found but did not fix (prohibited scope: caller-behavior change, not
  message hygiene): `RaceModelLoader::preloadModelData()`'s composite-sheet
  lookup should gate on `snapshotEQE1Loaders()` being non-empty before
  calling `getOrLoad()` with an empty buffer, to stop generating a
  guaranteed-failing call when a composite JSON manifest exists but no
  matching EQE1 archive is configured. Filed to `refactor/deferred.md`.
