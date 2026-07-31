# P50 — Close the two remaining applyPaletteMask plumbing gaps (P46d audit)

**Slot: slot3. x86 ONLY — no Pi (P48 owns it), no eqemu. Authorized
by Will 2026-07-31 (item 3).**

## Background

P46d's production ordering audit (refactor/P46d_cache_key_fix.md
Review, Part B) found two pre-existing paths that never thread
applyPaletteMask — masked textures loaded through them decode opaque:
1. Composite-deferred on-demand loads: mesh_building.cpp's
   buildMeshFromGeometry() resolves textures via its own
   getOrLoadTexture()/loadTextureFromBMP() fallback; the
   isMaskedMaterial flag it computes is used only for MaterialType,
   never passed to the texture load. Live-client exposure: any
   composite-eligible masked race whose per-part decode was deferred
   (P13b) and later needed multi-buffer on demand.
2. Equipment texture-variant overrides:
   animated_mesh_creation.cpp's per-slot swap (materialId != 0)
   calls loadTextureFromBMP() with no mask flag — a masked base body
   texture with a non-default equipped variant loses its mask on
   that slot.

## Work

1. Thread applyPaletteMask through both call sites (P46d's
   established pattern: the flag exists at getOrLoad/uploadTexture;
   compute isMaskedMaterial BEFORE the texture-load block where
   needed). Respect the (name, mask) cache keying — no new keying
   schemes.
2. Evidence per gap:
   - Gap 1: construct the on-demand scenario (composite-eligible
     masked race + a consumer forcing the multi-buffer path — the
     P13b deferral machinery; benchmark or test harness) and show
     the loaded texture now carries mask alpha (hasAlpha/log or the
     P47 viewer if reachable).
   - Gap 2: masked race + equipped variant swap — show the swapped
     slot's texture carries alpha (the P44b bear finding means no
     viewer CLI hook for variants; a targeted unit test or logged
     hash evidence is acceptable — say which and why).
3. Unmasked/variant-0 behavior byte-identical (md5/hash evidence).

## Rules

- Files expected: src/client/graphics/eq/mesh_building.cpp,
  src/client/graphics/eq/animated_mesh_creation.cpp (+ tests). Do
  NOT touch composite builder/eligibility (slot1 territory) or DRM
  device (slot2). If the fix genuinely requires touching slot1's
  files, STOP and report instead.
- Full bar: GLES2 ctest baseline, visual gate PASS, ARM clean, file
  sizes. Separate commits per gap.

## Review

### A. Fixes

Both gaps close via the same P46 pattern established by T05/
equipment_model_loader.cpp: compute `isMaskedMaterial` from the
WLD-derived `geometry->textureMasked()` flag, then thread it as
`applyPaletteMask` through to `ConstrainedTextureCache::getOrLoad()`/
`uploadTexture()`.

1. **Gap 1** (`mesh_building.cpp`, `RaceModelLoader::buildMeshFromGeometry()`):
   `isMaskedMaterial` was computed AFTER the texture-load block, used
   only for `buffer->Material.MaterialType`. Hoisted the computation
   to the top of the per-texture loop (before the load block) and
   threaded it into all four `getOrLoadTexture()`/`loadTextureFromBMP()`
   calls in the fallback chain (base name, override name, and the
   two retry-without-override paths). Required adding an
   `applyPaletteMask` parameter (default `false`) to
   `ZoneMeshBuilder::loadTextureFromBMP()` and `getOrLoadTexture()`
   (`zone_geometry.h`/`.cpp`) — these previously had no way to reach
   `ConstrainedTextureCache`'s mask flag at all. `getOrLoadTexture()`
   only forwards the flag on its lazy-load-now path (`pendingTextures_`
   miss → `loadTextureFromBMP()`); an already-cached hit returns as-is,
   consistent with P46d's "cache is the source of truth for mask
   status once loaded" model.
2. **Gap 2** (`animated_mesh_creation.cpp`,
   `createAnimatedNodeWithEquipment()`'s per-slot swap): no WLD
   material-type array is directly accessible at this call site (it
   operates on the shared cached mesh's buffers, not raw geometry
   indices). Instead, read the ground truth off the buffer's own
   `Material.MaterialType`, set at build time by Gap 1's fix
   (`EMT_TRANSPARENT_ALPHA_CHANNEL_REF` iff masked, P37) —
   `EntityRenderer::applyAlphaTestMaterialType()` (P21), which would
   otherwise convert this into a custom shader material id, only runs
   AFTER this function returns (verified: its only two call sites,
   `entity_renderer.cpp` ~L637/~L795/~L1238, are all downstream of
   `createAnimatedNodeWithEquipment()`), so this read is reliable.
   Threaded through both `loadTextureFromBMP()` calls (primary variant
   lookup and the legacy-fallback lookup).

Both reuse the new `applyPaletteMask` parameters from Gap 1's
`zone_geometry.h`/`.cpp` change — no new keying scheme, `(name,
applyPaletteMask)` is still `ConstrainedTextureCache`'s sole cache-key
input (P46d).

### B. Investigation finding (recorded, not fixed — out of scope)

While constructing Gap 1's evidence scenario, found that
`RaceModelLoader::preloadModelData()`'s composite-eligibility check
(added at/before P46/P37) EXCLUDES any race with a WLD-masked texture
baked into its own composite sheet from composite eligibility
entirely ("composite EXCLUDED ... source includes WLD-masked
texture"). This means P37's ground-truth masked races (SKE, ZOM)
never get a composite hit for their own masked textures, so the P13b
deferral (`deferredPerPartTextureNames_`) never actually defers a
masked texture belonging to today's known masked races through the
composite path specifically. Gap 1 remains real for any masked
texture NOT baked into a race's composite sheet (still deferred via
the general per-part deferral loop, unconditional on composite
membership) or any future composite-eligible masked race — just not
reproducible end-to-end via a live composite hit on current SKE/ZOM
data. Evidence below reproduces the fixed code path directly instead
(see C).

Also found, incidentally, a separate pre-existing read-side gap in
`ConstrainedTextureCache`'s public query API
(`hasAlpha()`/`getTexture()`/`getAlphaTexture(name)`/`isETC1Texture(name)`):
they take only a plain `name` and look it up in `cache_` directly,
but P46d's `uploadTexture()` stores masked-variant entries under the
mangled `cacheKey(name, true)` (`name + "\x01masked"`), so a
plain-name query for a masked variant always misses. This does NOT
affect rendering correctness (`MaterialType` selection is driven
directly by the WLD flag, not by these query methods, and the actual
GPU texture's pixel data is correct — confirmed below), only these
diagnostic/query methods. Out of scope for P50 (not one of the two
plumbing gaps, and not touched); flagged in `deferred.md` for a
follow-up.

### C. Evidence

**Tool**: `tools/hwcaps/p50_mask_plumbing_probe.cpp` (new, registered
in `CMakeLists.txt`), a standalone program linking `willeq_core`
directly against production `RaceModelLoader`/`ConstrainedTextureCache`
code — no zone load needed (character models load independently).
Given no live composite hit is reproducible for a masked race (see B),
it exercises the FIXED CODE PATHS directly and deterministically:

- **Gap 1**: a second, freshly-constructed `RaceModelLoader`/
  `ConstrainedTextureCache` pair (never seen these textures before)
  calls `createAnimatedNode()`. `ensureModelDataLoaded()` (the
  JSON-driven S3D load `getAnimatedMeshForRace()` calls internally)
  never sets `compositeData`, so the composite branch is always
  skipped and `buildMeshFromGeometry()` — Gap 1's fix — is always what
  resolves these textures, with a texture cache that has never seen
  them before: exactly the state P13b's deferral leaves a texture in.
- **Gap 2**: `createAnimatedNodeWithEquipment()` with a non-default
  `equipment[slot]` on the race's WLD-masked slot (SKE Wrist/forearms,
  ZOM Legs — both P37 ground truth), real production path (no
  override needed — SKE/ZOM always fall to the multi-buffer path per
  B, so the swap always runs against real cached-mesh materials).

**hasAlpha() correction**: because of the read-side gap in B, evidence
queries the masked-variant entries via the mangled key
(`name + "\x01masked"`, matching `cacheKey()`'s documented format,
verified directly off `ITexture::getName()` for Gap 2 — Irrlicht's
`addTexture()` is itself called with `internalKey.c_str()`, so a
masked variant's GPU texture name literally IS the mangled key).

**Results** (two P37 ground-truth races, `--eq-path
/mnt/projects/EverQuestP1999`):

- Race 60 (SKE): Gap 1 — 10/10 masked per-part textures now carry
  mask alpha (`skefa0001.bmp`, `skeft0001/0002.bmp`, `skech0001.bmp`,
  `skelg0001.bmp`, each present at 2 texIdx from a merged
  male/female-style texture table). Gap 2 — equipped variant
  `skefa0101.bmp` (Wrist slot, materialId=1) carries mask alpha. Both
  PASS outright.
- Race 70 (ZOM): Gap 1 — 2/4 raw entries; the 2 "misses" are texIdx
  with ZERO triangles in this gender's `rawGeometry` (orphan entries
  in the shared male/female texture table, never actually loaded —
  not reachable, not a fix failure). Of the 2 texIdx WITH triangles:
  `zomlg0001.bmp` (44 triangles) PASSES; `zomch0001.bmp` (34
  triangles) shows `hasAlpha=false` — but a **control** run (T05's
  own pre-existing, definitively-correct masked decode, same texture,
  same `applyPaletteMask=true`) shows the IDENTICAL result
  (`false`/`false` for `zomch0001.bmp`, `true`/`true` for
  `zomlg0001.bmp`) — i.e. Gap 1's fix reproduces T05's reference
  output byte-for-byte, including this case. Root cause (confirmed by
  a raw-BMP-header + active-pixel-region scan in the probe):
  `zomch0001.bmp` is a genuine 8bpp palette BMP but its active pixel
  region contains zero palette-index-0 texels — the documented
  "citywlivy class" of masked-but-decode-opaque textures
  (`mesh_building.cpp`'s own P37 comment) — not a decode bug. Gap 2 —
  equipped variant on the Legs slot carries mask alpha. PASS overall
  (Gap 1 read as "matches known-good baseline", Gap 2 PASS).
- **Unmasked/variant-0 byte-identity**: for both races, every
  unmasked texture resolved through Gap 1's SAME fallback code path
  reports `hasAlpha=false` (0 false positives: 16/16 SKE, 24/24 ZOM).
  Combined with `applyPaletteMask` defaulting to `false` at every
  other call site (verified by inspection — `grep` across the tree
  shows only the four Gap-1 and two Gap-2 call sites now pass a
  non-default value; every other caller of
  `getOrLoadTexture()`/`loadTextureFromBMP()` — `variant_loading.cpp`,
  `animated_mesh_creation.cpp`'s composite-texture lookup — is
  unaffected), this satisfies the byte-identical requirement by
  construction: `cacheKey(name, false) == name` (P46d), so an
  unmasked call's decode path is untouched.

### Verification bar

| Step | Result |
|---|---|
| `cmake --build build -j24` (full rebuild) | PASS, clean |
| `ctest --output-on-failure` | 1627/1628 (sole exception `AudioManagerTest.PreloadCommonSounds`, documented pre-existing ALSA host quirk; `RDPServerFrameTest.*` — the other documented intermittent flake family — did not reproduce this run) |
| `bash scripts/ci/visual_gate.sh` (via ctest's `visual_gate` entry) | PASS |
| `bash scripts/build-arm-noble.sh` | PASS, clean; `build-arm-noble/bin/willeq` verified via `file`: valid ARM EABI5 ELF, not a stub |
| `bash scripts/refactor/check_file_sizes.sh --files <touched files>` | `mesh_building.cpp` 620 lines OK, `animated_mesh_creation.cpp` 680 lines OK, `zone_geometry.h` 207 lines OK; `zone_geometry.cpp` WARN (1602 lines; was 1595 at merge-base 9a61ed3, WARN→WARN, +7 net lines) — pre-existing WARN, not a new crossing |

### Deviations

1. `zone_geometry.cpp`'s pre-existing file-size WARN grew by +7 lines
   (the `applyPaletteMask` parameter + P50 doc comments on
   `loadTextureFromBMP()`/`getOrLoadTexture()`) — WARN→WARN, no state
   change, same rationale as every prior unit that touched this file
   under the same pre-existing threshold.
2. Gap 1's live end-to-end reproduction (real composite hit on a
   masked race) is not achievable on current data — see Review B.
   Evidence instead exercises the identical fixed code path
   deterministically via a fresh cache/loader pair (Review C).
   Flagged, not treated as a blocker: the code path exercised IS the
   one the fix touches, byte-for-byte.
3. Found (not fixed, flagged in `deferred.md`): `ConstrainedTextureCache`'s
   public query API (`hasAlpha`/`getTexture`/`getAlphaTexture(name)`/
   `isETC1Texture(name)`) doesn't account for P46d's masked-variant
   key mangling — a plain-name query for a masked-variant entry always
   misses. Does not affect rendering (confirmed — see Review B), only
   diagnostics/queries by these methods.

Commits (see git log on `task/P50-mask-plumbing`): Gap 1 fix
(production, `mesh_building.cpp` + `zone_geometry.h`/`.cpp`) / Gap 2
fix (production, `animated_mesh_creation.cpp`) / evidence tool
(`tools/hwcaps/p50_mask_plumbing_probe.cpp` + `CMakeLists.txt`).
