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

(executor fills in: shader variant source, selection wiring, evidence
captures with descriptions, per-site variant assignment table, WLD harvest
details, deviations)
