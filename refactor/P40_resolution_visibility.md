# P40 — Resolution-dependent visibility anomaly (root-cause)

**Slot: slot2. x86 ONLY — no Pi, no eqemu.** Sibling: P39 (slot1,
config plumbing for the occluder key) — do not touch
constrained_renderer_config parse/preset files; yours is the
visibility/culling pipeline.

P38 found: rendering befallen at 400x300 produced MORE draws and
triangles than 800x600 (same replay, same config). Resolution should
not change what is VISIBLE. Root-cause it.

## Work

1. Reproduce on x86 (llvmpipe, the benchmark + committed
   `--gl-counters` from P38): draws/tris per frame at 800x600 vs
   400x300 vs one more point (e.g. 1024x768) on the canonical befallen
   replay. Confirm the anomaly and its direction/monotonicity.
2. Root-cause: plausible mechanisms to check (verify, don't assume):
   (a) the occlusion-culling system rasterizes occluders in
   screen-space — lower res → coarser occlusion buffer → fewer pixels
   pass the occluder test → less culling → more draws (if so, this is
   a DESIGN property with a tunable, not a bug — say so and
   characterize the tradeoff); (b) FOV/aspect or frustum derivation
   depending on resolution incorrectly; (c) LOD/screen-size thresholds
   inverted; (d) something else the counters point to.
3. Fix if it's a genuine bug (wrong math); REPORT-ONLY with a
   characterization + recommendation if it's a design property
   (e.g. occlusion buffer should be fixed-size independent of screen
   resolution — propose, don't implement).
4. Quantify the Pi-relevant implication either way (the Pi ships
   800x600; does this matter at shipped res, or only for hypothetical
   res changes?).

## Rules
- Territory: occlusion/PVS/visibility code (read), fix only if
  genuine-bug class; benchmark/counter usage. NOT config parse files
  (P39's), NOT the driver, NOT goldens (gate must PASS if you change
  anything).
- STOP: fix would change culling behavior at the SHIPPED resolution
  (that would move blessed Pi behavior — report with numbers instead).

## Verification
- Full bar if code changed (ctest baseline, gate PASS, ARM clean,
  file sizes); if report-only, build+repro evidence suffices.
- The three-resolution counter table + the named mechanism.

## Review
(executor fills in)
