# P47 — Magenta entity viewer: a trivially-readable instrument for masked-alpha verification

**Slot: slot2. x86 ONLY — no Pi, no eqemu.** Will's design
(2026-07-31): "render the model+textures with a solid magenta
background so it's trivial to detect alpha transparency gaps." This
replaces the failed close-up/diff-crop evidence approach (5 rejected
rounds — see P44/P44b/P46/P46b/P46c/P46d chain).

## Deliverable shape

A benchmark-only `--entity-viewer` mode in gles2_zone_benchmark:
- Loads NO zone geometry. Clear color solid magenta (255,0,255).
- Renders ONE race model LARGE — filling most of the frame — at a
  small set of fixed angles (front/back/left-side minimum), fixed
  deterministic pose (use the proven frame-indexed animation clock;
  P44b showed the no-animate path takes a stale masked check, so
  route through the animated path at a fixed frame).
- PNG dump per angle. Resolution 800x600 (or higher if trivial).
- Reading rule: magenta visible INSIDE the model silhouette = alpha
  gap. No crops, no diff overlays, no before/after required.

## Work

1. The viewer mode (benchmark-only source). Texture modes: RGBA
   (primary — exercises P46 Part A + P46d, x86-representative) and,
   if cheap, ETC1 with the P46b promotion path bound (companion
   alpha, Pi-representative). Label each output with its mode.
2. Renders for: the masked ground-truth races SKE (21), ZOM (70),
   BAT (34), ORC (17) + unmasked control Human (1). (P37's table is
   ground truth; add other masked races only if free.)
3. Contact sheet per repo convention:
   benchmarks/contact_sheets/p47_entity_viewer/index.html
   (self-contained), stills also to
   /mnt/projects/fable/videos/p47_viewer/ (NFS).
4. **Regression pin**: a ctest test using the viewer — magenta-pixel
   count inside the model bbox must be >0 and stable(ish) for masked
   races, EXACTLY 0 for the unmasked control. Asset-gated with skip
   guards like the other EQ-asset tests. This makes alpha breakage
   detectable forever without human eyes.
5. Executor self-check before delivering: view your own renders —
   the model must be LARGE and clearly lit (neutral/unlit built-in
   shading, NOT the point-blank player-light promotion unless doing
   the ETC1 leg, which should be labeled); textures at this size
   must look right part-by-part.

## Rules

- Benchmark/tools + tests source only; NO production source changes;
  goldens untouched (gate PASS); no presets.
- Full bar: GLES2 ctest baseline (~1616/1617 + your new test), visual
  gate PASS, ARM cross-build clean, file sizes (zone_bench_loader
  pre-existing FAIL — flag only; put substantial new viewer code in
  its own TU rather than growing that file).
- Commits: viewer mode / renders+contact sheet / regression test.

## Review

(executor fills in)
