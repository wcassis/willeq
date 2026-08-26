# T242 — blackburrow / crushbone render blank in gles2_zone_benchmark (Batch H, never root-caused)

Slot: slot1. x86. NO eqemu (benchmark tool loads zones from assets). Class
B → A. Territory: tools/benchmark (gles2_zone_benchmark), zone loaders
under src/client/graphics/eq/ + renderer_zone_load*, benchmarks/ configs,
tests. Do NOT touch action/*, eq_action_handler*, combat.* (T239/T240),
renderer_frame.cpp input paths.

## Lead (deferred.md, Batch H / H02)
`gles2_zone_benchmark` renders blackburrow and crushbone BLANK (no
geometry) while other zones render; never root-caused; no later unit
touched it. Both are Titanium-era zones with S3D + EQE1 packs present
(verify: ls /mnt/projects/EverQuestP1999/blackburrow* and the EQE1 dir).

## Scope
1. Reproduce: run the benchmark on blackburrow and crushbone with
   `--eqe1-path` and a frame dump; confirm blank (pixel stats) vs. a
   control zone (qeynos2). Capture the loader log at DEBUG with
   `EQT_EQE1_REQUEST_LOG_DIR`.
2. Attribute: is it (a) zone-load failure (WLD parse error / fragment the
   loader rejects — grep the log for the first WARN/ERROR), (b) geometry
   loaded but culled (PVS/BSP region SIZE_MAX — the "Player BSP region is
   SIZE_MAX" board item may be the same bug; check the camera start
   position vs the zone's safe point), or (c) loaded and drawn but the
   benchmark's camera path (benchmarks/paths/*.path) points at nothing.
   State which with the log line / numbers.
3. Fix if narrow (a wrong safe-point/camera path, a fragment type the
   loader skips, a region lookup off-by-one). If it needs loader redesign,
   STOP after attribution.
4. If fixed: before/after frame crops with non-background pixel counts;
   visual gate PASS (are either goldens? check `scripts/ci/visual_gate*`);
   full ctest.

## Verification
Review; evidence review/T242/. Commit: `T242: blackburrow/crushbone render in the benchmark -- <cause>` or the attributed-not-fixed form.
