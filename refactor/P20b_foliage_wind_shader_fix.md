# P20b — Foliage alpha: fix the wind-shader MaterialType clobber

**Slot: slot1. OWNS EQEMU (sole owner). x86 only; do NOT touch the Pi.**

Completes P20 Part A. P20's follow-up investigation (see
`refactor/P20_live_visual_pair.md` Review, correction block) established
the true root cause of Will's opaque-tree-foliage sighting with hard
evidence; this unit fixes it. Do not re-derive the diagnosis.

## Trusted findings (from P20 — do not re-derive)

- On the live x86 GLES2 client (EDT_OGLES2), `AnimatedTreeManager` is
  DEAD CODE: `loadZoneStep11_Env()` (`renderer_zone_load.cpp` ~1986-2003)
  only initializes it under `EDT_BURNINGSVIDEO`. Trees are built as
  ordinary placeables via `ZoneMeshBuilder::buildTexturedMesh()`, which
  classifies alpha correctly (H02d Option A).
- The bug: a wind-shader pass then UNCONDITIONALLY overwrites
  `MaterialType` on tree materials with no masked-flag or ETC1-vs-RGBA
  classification, at TWO sites:
  `renderer_zone_stream.cpp:559-576` and
  `renderer_zone_load.cpp:2288-2334`. Masked RGBA foliage lands on the
  ETC1-companion alpha-test variant with no companion bound → GLES2
  driver's opaque-white 1×1 unit-1 fallback (never discards) → solid
  opaque quads.
- Ground truth: `nekpine.bmp` (gfaydark pine, NEKPINE1/2_ACTORDEF) is
  WLD Fragment 0x30 TransparentMasked and its DXT5 texture carries a
  genuine hard-edged alpha cutout (47.1% near-zero / 52.9% near-full).
  This is NOT the citywlivy painted-opaque class.
- P20's `animated_tree_manager.cpp` change (already merged) is a correct
  H02d mirror for the software-renderer path; leave it alone.

## The fix

1. At both wind-shader sites, replace the unconditional MaterialType
   override with H02d Option-A classification, preserving the wind
   behavior: masked+ETC1 → wind/companion-alpha variant (companion bound
   on unit 1); masked+RGBA → a wind variant that reads `uTexture.a` (add
   it if the wind shader family lacks one, mirroring how
   `zone_shader.cpp` structures base vs RGBA alpha-test variants);
   non-masked → plain wind/solid variant. Follow the existing shader
   naming/wiring conventions.
2. Audit for OTHER unconditional MaterialType overrides of the same
   class applied after mesh build on the live path (grep the
   renderer_zone_* TUs); fix only tree/wind sites — the known
   door_manager/entity_renderer `setShaderMaterialTypes()` 2-arg gap is
   OUT OF SCOPE (tracked in deferred.md; needs its own evidence).

## Evidence bar (this failed once — be exact)

- Before/after stills MUST be same-pose by construction: identical
  `--script` move file, identical wait timings, screenshot at the same
  script step; state the move file's content in the Review. P20's
  round-1 "after" evidence was retracted because the move paths
  differed — do not repeat that.
- Two scenes minimum: gfaydark pine cluster (nekpine — the proven
  ground-truth case) and the qeynos2/qeynos gates trees from Will's
  original video complaint.
- Verify in the before stills that the opaque quads are present, and in
  the after stills that foliage shows the leaf-shaped cutout with
  background visible through negative space. If ANY tree in frame stays
  rectangular, identify why (painted-opaque texture = by-design; name
  the texture) in the Review — no unexplained rectangles.
- Stills to `/mnt/projects/fable/videos/p20_live_visual/stills/`
  (p20b_ prefix); update
  `benchmarks/audits/p20_live_visual/EVIDENCE_MANIFEST.txt`.
- You do NOT declare visual acceptability — Will judges the stills.

## Rules

- eqemu per EXECUTOR_PROTOCOL.md; restore Testchar (zone_id=36,
  x=-77.9232, y=21.9086, z=3, h=190) and shut down cleanly.
- Territory: `src/client/graphics/**` (renderer_zone_stream.cpp,
  renderer_zone_load.cpp, zone_shader.cpp if a wind-RGBA variant is
  needed, plus headers). No driver changes; no goldens; no server code.
- Goldens must stay untouched: the visual gate runs the benchmark
  loader, which never executes the wind pass — if the gate moves
  anyway, STOP and report.
- STOP conditions: root cause turns out different from the trusted
  findings above (report, don't improvise); fix would require driver
  texture/state-cache changes (P18 territory); goldens move.

## Verification

- Full 6-step bar (EXECUTOR_PROTOCOL.md); known ctest exceptions:
  AudioManagerTest.PreloadCommonSounds,
  RDPServerFrameTest.UpdateFrameMultipleTimes.
- Same-pose before/after pairs per the evidence bar, both scenes.

## Review

(executor fills in: fix details per site, shader variant additions,
same-pose evidence paths + move-script contents, any by-design
rectangles explained, deviations)
