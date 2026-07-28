# P21 — Door/entity masked-RGBA alpha gap (setShaderMaterialTypes)

**Slot: slot1. OWNS EQEMU (sole eqemu owner this batch). x86 only; do
NOT touch the Pi.** Siblings: P22 (slot2, GLES2 driver territory), P23
(slot3, particles/stability territory) — file territories are disjoint;
respect them.

Same bug class P20b just fixed for trees, in the remaining two sites:
`door_manager.h` and `entity_renderer.h` `setShaderMaterialTypes()` are
still 2-argument (companion alpha-test + solid) with no RGBA alpha-test
variant. Any masked WLD material whose texture arrives via the RGBA/DDS
pipeline (not ETC1) gets the companion variant with nothing bound on
unit 1 → GLES2 opaque-white fallback → renders solid.

## Trusted background (do not re-derive)

- H02d Option A: RGBA-pipeline textures must read their OWN alpha
  (`uTexture.a`); companion `uAlphaTexture` (unit 1) is ETC1-only. The
  RGBA alpha-test shader variants ALREADY EXIST (`zone_shader.cpp`,
  `materialAlphaTestRGBA_`/`materialLWAlphaTestRGBA_`) — this unit is
  plumbing, not shader authoring.
- P20b's fix pattern (mirror it): per-material classification
  masked+ETC1 → companion bound / masked+RGBA → RGBA variant /
  non-masked → solid. See `renderer_zone_stream.cpp:560-624` and the
  P20b Review for the reference implementation.
- `citywlivy.bmp` class (WLD-masked but painted opaque) renders solid
  BY DESIGN — name, don't fix, if encountered.

## Work

1. Ground truth first: determine whether any masked-RGBA door or entity
   materials actually occur in the classic zones we test (grep the WLD
   material data via the s3d/wld tools for door + character archives in
   befallen/qeynos2/gfaydark; document what you find). Real occurrences
   → they're your evidence scenes. None found → the fix is
   latent-correctness; say so plainly and rely on code-level evidence
   (a forced test case is acceptable if cheap, e.g. a known masked
   texture on a door in some classic zone — search more zones before
   concluding "none").
2. Extend `setShaderMaterialTypes()` in `door_manager.h`/`.cpp` and
   `entity_renderer.h`/`.cpp` to carry the RGBA alpha-test variant
   (3-arg, matching `ZoneMeshBuilder`), classify per-material at the
   material-assignment sites, and wire the extra argument at all call
   sites (grep `setShaderMaterialTypes` across `src/client/graphics`).
3. Evidence: if a real in-game case exists, same-pose-by-construction
   before/after stills per the P20b evidence bar (identical script,
   same step, stills to `/mnt/projects/fable/videos/p20_live_visual/
   stills/` with `p21_` prefix, manifest updated). If latent-only:
   before/after of the forced case, clearly labeled as forced.

## Rules

- eqemu per EXECUTOR_PROTOCOL.md; restore Testchar (zone_id=36,
  x=-77.9232, y=21.9086, z=3, h=190); shut down cleanly.
- Territory: `src/client/graphics/door_manager.*`,
  `src/client/graphics/entity_renderer.*` (+ headers under
  `include/client/graphics/`), call-site wiring lines only in other
  files. Do NOT touch `zone_shader.cpp` (variants exist), the GLES2
  driver (P22's territory), or particle files (P23's).
- Goldens untouched (visual gate PASS); STOP if the gate moves.
- Composite-entity interaction: composite meshes bypass per-material
  texture swaps by design — if classification interacts with the
  composite path, STOP and report rather than special-casing.

## Verification

- Full 6-step bar (known ctest exceptions: AudioManagerTest
  .PreloadCommonSounds, RDPServerFrameTest.UpdateFrameMultipleTimes).
- Ground-truth finding documented either way (real vs latent).

## Review

(executor fills in)
