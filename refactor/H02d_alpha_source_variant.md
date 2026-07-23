# H02d — Alpha-source-aware shader selection (Option A) + diagnostics

Will approved Option A from the alpha-architecture investigation (see the
investigation report summarized in this plan's Background): GLES2 alpha-test
shaders gain a variant that reads the MAIN texture's own alpha (`uTexture.a`)
for textures from the RGBA/DDS pipeline; the companion (`uAlphaTexture`,
unit 1) variant remains for ETC1 textures only. This matches desktop-GL's
known-good `EMT_TRANSPARENT_ALPHA_CHANNEL_REF` semantics and eliminates the
companion-bug class on the RGBA path. Diagnostic pre-steps land first so the
fix is verified by observation.

## Background (from the read-only investigation — trust these findings)

- 189d5d1's opaque-white 1×1 unit-1 fallback is live in COpenGLES2Driver
  (docker/irrlicht-drm/COpenGLES2Driver.cpp:1090-1101,~1159) — masked
  materials without a companion read alpha=1.0 → render solid. Deliberate
  fallback, not a stale binding.
- H02c's companion fix failed via use-after-erase: it read
  `entry.rgbaPixels` after `decodedEntries_.erase(decodedIt)` in
  uploadTexture() — garbage companion → over-discard → vanishing foliage.
  That approach is DROPPED (Option B rejected).
- Alpha-test shader variants: zone_shader.cpp:150-219 (base + atlas) — all
  currently sample uAlphaTexture with the same UV varying as the main
  texture.
- Three additional companion-less alpha-test sites (same solid-render bug
  class): zone_geometry.cpp buildTexturedMeshFromUploaded (~554),
  buildCompositeMesh (~648, ~750). In scope: they should select the correct
  variant too.
- The debug shader (zone_shader.cpp:425-467) reads only uTexture.a — it
  cannot visualize the companion. Modes: 7=alpha, 11=rawTexAlpha.
- Benchmark cannot set shader debug modes today.
- citywlivy.bmp (qeynos ivy): WLD-flagged masked but its texels are painted
  opaque (no real alpha) — under Option A it keeps rendering solid, same as
  today. Expected; document, don't "fix".

## Part 1 — Diagnostic pre-steps (first commit)

1. New debug mode 12 in FRAGMENT_SHADER_DEBUG_SRC: displays
   `texture2D(uAlphaTexture, vTexCoord).r` (grayscale). Update the mode-name
   table in irrlicht_renderer.cpp's /shaderdebug handler ("companionAlpha").
2. `--shader-debug N` flag on gles2_zone_benchmark: sets
   zoneShader->setShaderDebug(N) and swaps the benchmark's per-pixel
   materials to the debug program for the run (mirror the renderer's
   swapToDebugShader approach within zone_bench_loader's material handling).
3. BEFORE-fix evidence: capture the gfaydark foliage pose
   (820.0,84.9,110.0,0,-5) at modes 0/7/11/12 → PNGs into
   refactor/artifacts/H02d/. Expected per the investigation: mode 7/11 show
   the real leaf alpha pattern (~47% transparent); mode 12 shows uniform
   white (opaque fallback bound).

## Part 2 — Option A implementation

1. zone_shader.cpp: add alpha-test variant(s) reading `uTexture.a`
   (non-atlas AND atlas forms as needed), registered as a distinct material
   type (e.g. shaderMaterialAlphaTestRGBA_). Threshold semantics identical
   to the companion variant.
2. ConstrainedTextureCache: expose which pipeline produced a texture (ETC1
   vs direct-RGBA) — it already knows internally; add a query
   (e.g. bool isETC1Texture(name) or an AlphaSource enum).
3. Selection at ALL alpha-test material assignment sites:
   buildTexturedMesh, buildTexturedMeshFromUploaded, buildCompositeMesh (×2)
   in zone_geometry.cpp — ETC1 → companion variant (unchanged behavior);
   RGBA → new main-alpha variant (no companion needed; do NOT build one).
4. Re-apply H02c's WLD Fragment-0x30 TransparentMasked propagation
   (wld_loader.h/cpp → zone_geometry textureMasked() OR'd into the
   alpha-test decision) — harvest from branch task/H02c-foliage-alpha
   (git show/cherry-pick the relevant hunks); do NOT bring over its
   uploadTexture companion-building code.
5. ETC1 path byte-for-byte untouched (companion build, NEAREST filtering,
   shader) — its behavior must not change.

## Verification

- AFTER captures at the same pose, modes 0/7/11/12: mode 0 shows leaf-shaped
  cutouts (foliage present with transparency — NOT vanished, NOT solid);
  7/11 unchanged vs before; 12 unchanged (still white — RGBA path no longer
  consults the companion).
- `visual_gate.sh --bless` (goldens legitimately change) → plain run PASS;
  determinism ×2 byte-identical; regenerate contact sheet (NOT approved —
  Will re-reviews).
- qeynos2 + befallen frames inspected: ivy solid as documented-expected;
  no regressions.
- Default build + FULL ctest unregressed (production shader/mesh code
  changed — mandatory). build-gles2 clean. gles2_shader_diagnostic still
  builds.
- File sizes ok on changed files.

## Review

### Shader variant source

Added a third alpha-test material family alongside the existing solid /
companion-alpha pair, in both heavy (per-pixel player light) and lightweight
forms, matching `ZoneShaderManager`'s existing GLES2/desktop-GL split:

- `FRAGMENT_SHADER_ALPHA_RGBA_SRC` (heavy) / `FRAGMENT_SHADER_LIGHTWEIGHT_ALPHA_RGBA_SRC`
  (lightweight) — GLES2 only, `src/client/graphics/zone_shader.cpp`. Identical
  to the existing `..._ALPHA_SRC_BASE` / `..._LIGHTWEIGHT_ALPHA_SRC` shaders
  except the discard test reads `texColor.a` (the main texture's own alpha)
  instead of a second `texture2D(uAlphaTexture, ...)` sample — no `uAlphaTexture`
  uniform is declared in these variants at all, so unit 1 is never consulted.
- Desktop-GL (non-GLES2) build: reuses the existing `FRAGMENT_SHADER_ALPHA_SRC`
  unchanged as the RGBA variant's source, since that shader already reads
  `texColor.a` directly with no companion — it's the thing Option A is
  matching semantically, so no new desktop shader text was needed.
- New material type IDs: `materialAlphaTestRGBA_` / `materialLWAlphaTestRGBA_`,
  registered via `ShaderCallback` (the same generic callback class used for
  solid/alpha — it resolves `uAlphaTexture`'s location per-program and the
  `locAlphaTex_ >= 0` guard already present in `OnSetConstants` naturally
  no-ops for the RGBA program, where that uniform doesn't exist). Compile
  failure is non-fatal (`LOG_WARN`, unlike solid/alphaTest which are fatal to
  `available_`) — callers fall back to fixed-function
  `EMT_TRANSPARENT_ALPHA_CHANNEL_REF`, which was never affected by the
  companion-unit bug in the first place, so the fallback is safe.
- `getActiveAlphaTestRGBA()` added to `ZoneShaderManager`, mirroring
  `getActiveAlphaTest()`'s lightweight/per-pixel resolution logic exactly.
- Debug shader (`FRAGMENT_SHADER_DEBUG_SRC`): added mode 12
  ("companionAlpha"), gated behind the same pre-existing unconditional
  `if (texColor.a < 0.5) discard;` at the top of `main()` — so mode 12 only
  displays `uAlphaTexture.r` for fragments whose *main*-texture alpha already
  passed. This was a deliberate choice (not a rearchitecture of the debug
  shader's control flow): it still cleanly demonstrates the diagnostic signal
  (uniform white within the surviving region — see evidence below), and mode
  9/10/UV/depth-style modes have the same pre-existing property.

### Selection wiring (Option A)

`ConstrainedTextureCache` gained the alpha-source query the plan asked for:
`bool isETC1Texture(const std::string&) const` plus a GPU-texture-pointer
overload, backed by a new `CachedTexture::isETC1` bool (set `true` only in
`uploadCompressedETC1()`; defaults `false`, so `uploadTexture()`'s direct-RGBA
path needs no change there). Also added a pointer-overload of
`getAlphaTexture()` for the one site with no texture name available (see
below).

`ZoneMeshBuilder::setShaderMaterialTypes()` gained a third parameter,
`alphaTestRGBAType` (default `-1`, so existing 2-arg callers — door/tree/entity
builders, which construct their own `ZoneMeshBuilder` instances — degrade
safely to the fixed-function fallback rather than silently doing nothing).
Wired the 3-arg call at every zone-geometry-building call site that already
passed `getActiveAlphaTest()`: `renderer_zone_load.cpp` (steps 4 and 6),
`renderer_zone_draw.cpp` (`createZoneMesh`), `renderer_zone_stream.cpp`
(background region builds + streamed object builds), and
`zone_bench_loader.cpp` (both the zone-region and zone-object builder
instances). Left `door_manager.cpp` / `animated_tree_manager.h` /
`entity_renderer.h`'s own 2-arg wrappers untouched — out of the plan's site
list, and safe by the fallback design above.

#### Per-site variant assignment table (all 4 sites, `zone_geometry.cpp`)

| Site | Alpha-source signal | ETC1 → | Direct-RGBA → |
|---|---|---|---|
| `buildTexturedMesh` (~391) | `constrainedCache_->isETC1Texture(texName)` | `shaderMaterialAlphaTest_` (unchanged) + bind `getAlphaTexture(texName)` on unit 1 | `shaderMaterialAlphaTestRGBA_` (else fixed-function), unit 1 left untouched |
| `buildTexturedMeshFromUploaded` (~554) | same, `texName` resolved via `geometry.textureNames()[texIdx]` (previously unused in this function) | same | same |
| `buildCompositeMesh`, first buffer (~694) | `constrainedCache_->isETC1Texture(compositeTexture)` (pointer overload — no single originating name for a merged composite texture) | same, via `getAlphaTexture(compositeTexture)` pointer overload | same |
| `buildCompositeMesh`, overflow buffer (~825, hit at the 65535-vertex split) | same as above | same | same |

`buildAtlasedMesh`'s fallback-buffer hasAlpha assignment (~1370, harvested
from H02c below) was deliberately **not** given the Option A treatment — it's
outside the plan's four-site list and, like H02c found, is currently dead
code (no caller builds via `buildAtlasedMesh` today per `grep`); flagging
this inconsistency for whoever eventually re-wires atlas building, same as
H02c did.

### WLD harvest (from `task/H02c-foliage-alpha`, `18f37f7`)

Cherry-picked (via `git show 18f37f7 -- <paths> | git apply`, clean apply, no
conflicts since this branch's parent for these files is identical to H02c's):
`include/client/graphics/eq/wld_loader.h`, `src/client/graphics/eq/wld_loader.cpp`,
and `src/client/graphics/eq/zone_geometry.cpp`'s `buildTexturedMesh` +
`buildAtlasedMesh` `hasAlpha` OR-in (`WLD_MATERIAL_TYPE_TRANSPARENT_MASKED`
propagated from Fragment 0x30 through `WldTextureBrush::materialType` →
`ResolvedMaterialList::textureMasked` → `ZoneGeometry::textureMasked()`).
Did **not** harvest `constrained_texture_cache.cpp`'s companion-building diff
from that commit — confirmed by inspection it's the exact
read-`entry.rgbaPixels`-after-`decodedEntries_.erase(decodedIt)` use-after-erase
the plan's Background describes; it was never brought over.

### Evidence: BEFORE vs AFTER (gfaydark, pose 820.0,84.9,110.0,0,-5)

Captured via the new `--shader-debug N` flag, modes 0/7/11/12, saved to
`refactor/artifacts/H02d/{BEFORE,AFTER}_gfaydark_mode{0,7,11,12}_f0.png`
(gitignored, left on disk per the H02c precedent).

- **Mode 0 (production render)**: BEFORE shows the pine canopy as a solid
  dark rectangular block (no leaf silhouette at all — matches the
  investigation's "renders fully solid" diagnosis). AFTER shows the canopy
  with correct leaf-shaped cutouts, matching modes 7/11's alpha pattern —
  sky and a second tree trunk visible through the gaps that were previously
  hidden behind the solid block. Not vanished, not solid: present with
  transparency, as required.
- **Modes 7/11 (`uTexture.a` / `texColor.rgb,a`)**: byte-identical MD5
  between BEFORE and AFTER captures — unaffected by the shader-selection
  change, as expected (they only ever read the main texture's own alpha).
- **Mode 12 (`uAlphaTexture.r`, unit 1)**: byte-identical MD5 between BEFORE
  and AFTER — uniform white in both, confirming (a) before the fix, the
  companion-based shader was reading the driver's opaque-white 1x1 fallback
  (not a stale binding, matching `COpenGLES2Driver::applyMaterialRenderState`'s
  deliberate `getOpaqueAlphaFallback()` call when unit 1 has no texture bound),
  and (b) after the fix, this is genuinely moot — the RGBA variant never
  samples unit 1 at all, so mode 12's diagnostic value is now purely
  historical/comparative for this texture.
- An earlier BEFORE capture (taken before the evidence-first sequencing was
  corrected — see Deviations) showed a *partially*-cut canopy in mode 0,
  which contradicted the "solid block" prediction. Traced to a stale
  incremental-build artifact from git-stash-juggling Part 1 vs Part 2 file
  sets (confirmed by a from-scratch clean rebuild of the `gles2_zone_benchmark`
  target, which reproducibly gave the true solid-block BEFORE image and
  3/3 stable `visual_gate` PASS/FAIL results before and after each isolated
  change) — not a contradiction of the investigation's premise. Recorded here
  since it cost real time and is a reusable lesson: after any `git stash`
  juggling of a CMake C++ target's source files, force-delete that target's
  `.o` files before trusting a rebuild's output.

### Re-bless / verification results

1. `visual_gate.sh --bless`: regenerated all 9 goldens + manifest + contact
   sheet. Plain `visual_gate.sh` (no `--bless`): **PASS**, run twice
   back-to-back with byte-identical `ci_artifacts` PNGs (md5-verified against
   the golden). `textureCacheBytesUsed` **unchanged** for all 3 zones (this
   fix reassigns shader material types, it never builds new textures);
   `p95CpuMsBaseline` shifted by a few ms in the noise, well inside the gate's
   2x threshold.
2. qeynos2 (`citywlivy.bmp` ivy) and befallen re-inspected across all 3
   designated frames each: ivy renders identically solid before/after (no
   real alpha texels in that DXT1-recompressed source, as H02c documented —
   expected, not a bug); befallen has no masked textures in its 3 frames, no
   change. The pre-existing out-of-scope "flat pale/white shape" artifact
   H02c flagged in gfaydark_f0/f1/f2 and qeynos2_f2 is still present,
   byte-identical, confirmed unrelated to this fix.
3. Default build (`cmake --build build -j24`, clean full rebuild): builds
   clean. Full ctest: **1224/1225 passed**; the sole failure,
   `AudioManagerTest.PreloadCommonSounds`, is the pre-existing documented
   ALSA flake (`refactor/test_baseline.txt`). The `visual_gate` ctest entry
   **passed** inside the full run.
4. `build-gles2/`: clean full rebuild (`gles2_zone_benchmark`,
   `gles2_shader_diagnostic` targets explicitly verified building green from
   zero `.o` files; the pre-existing `model_viewer` EGL link failure in this
   tree is unrelated — same failure reproduces on plain `fable` HEAD in this
   environment and doesn't gate this unit).
5. `scripts/refactor/check_file_sizes.sh --files <all changed .cpp/.h>`:
   exit 0. All WARN-tier files (`zone_shader.cpp`, `wld_loader.cpp`,
   `zone_geometry.cpp`, `constrained_texture_cache.cpp`,
   `renderer_zone_load.cpp`, `zone_bench_loader.cpp`, `irrlicht_renderer.cpp`,
   `gles2_zone_benchmark.cpp`) were already over the 1500-line threshold
   before this unit; none approach the 3000-line FAIL line.

### Deviations from the plan

1. **Evidence-first sequencing was violated on the first pass and then
   corrected.** All of Part 1 and Part 2 were implemented together before
   any diagnostic capture, contrary to the plan's explicit ordering. This was
   caught before any commit; recovered by using `git stash` to separate the
   pure-Part-2 files back to base, manually reverting the Part-2-only hunks
   in the three mixed files (`zone_shader.h`/`.cpp`, `zone_bench_loader.cpp`),
   capturing true BEFORE evidence against a from-scratch clean rebuild,
   confirming predictions, committing Part 1 alone, then reapplying Part 2
   from the same authored diff and capturing AFTER evidence. Both commits'
   diffs were verified against the original combined diff to confirm nothing
   was lost or duplicated in the split.
2. Atlas alpha-test variants (`ATLAS_FRAGMENT_SHADER_ALPHA_RGBA_SRC` and
   friends) were **not** added — the plan said "non-atlas AND atlas forms as
   needed," and none of the four in-scope selection sites use the atlas
   shader path (only `buildAtlasedMesh`, out of scope and currently unused,
   does). Flagging in case atlas building is ever re-wired.
3. `buildCompositeMesh`'s two sites needed a pointer-based reverse lookup
   (`isETC1Texture(ITexture*)`, `getAlphaTexture(ITexture*)`) since there is
   no single originating texture name for a merged composite sheet; both are
   currently dead code (no caller — same status H02c found for
   `buildAtlasedMesh`), so this is unexercised in practice but kept in sync
   per the plan's "select the correct variant too" instruction.
4. Created no new artifacts directory conventions beyond H02c's
   `refactor/artifacts/<unit>/` precedent.
