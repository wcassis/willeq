# P41 — Lighting-model lever: characterize for Will's decision (+ Pi ride-alongs)

**Slot: slot1. OWNS THE PI (P02 protocol; budget ≤10 invocations).
x86 for builds/stills. No eqemu.** Sibling P42 (slot2) changes
driver/benchmark/config files — you build YOUR OWN binaries from YOUR
branch; no coordination needed.

P38 measured lightweight-vs-per-pixel lighting = −15% total / −22%
GPU, the largest lever found. But per-pixel adaptive lighting is
BLESSED visual territory (Q03: GLS level 9, ×3 boost, count-mode —
Will personally tuned this). This unit CHARACTERIZES the trade for
Will's decision — it does NOT flip anything.

## Work
1. **Pi numbers, both loads**: lightweight vs per-pixel A/B at
   realistic (e5) AND stress (e205) on the canonical befallen replay,
   shipped config otherwise. (~4 invocations.)
2. **Visual A/B for Will**: x86 stills at the Q03 golden poses
   (befallen GLS fixture — the exact scenes Will blessed: lightstone/
   greater-lightstone lighting) rendered per-pixel vs lightweight,
   same-pose by construction. Honest crops of where they differ most.
   Stills to /mnt/projects/fable/videos/p41_lighting/ + committed
   manifest under benchmarks/audits/p41_lighting/. You do NOT declare
   acceptability; note objectively where lightweight loses (per-vertex
   banding on large polys is the 1999-era signature — Will may
   actually LIKE that era-wise; report, don't spin).
3. **Ride-along A (P40 closure, 2 inv)**: clean 400x300 vs 800x600
   A/B, shipped flags, canonical replay — draws/tris counters. Closes
   the resolution question definitively.
4. **Ride-along B (audit item 5, 1-2 inv)**: eglSwapInterval(0)
   diagnostic (local prototype, reverted) — quantify the swap/pacing
   floor's share at realistic load.
5. Deliverable: numbers tables + stills manifest + a short options
   note (keep per-pixel / flip to lightweight / per-tier knob),
   appended to refactor/perf_truth_audit.md as a dated section.

## Rules
- NO preset/config flips; NO golden changes (gate PASS); all
  prototypes local+reverted; commit = docs, manifest, CSVs, plan
  Review.
- Pi protocol absolute; pristine exit.

## Verification
- Bar narrowed (measurement unit): builds clean, ctest baseline, gate
  PASS, ARM clean. Tables + stills + invocation count reported.

## Review
(executor fills in)
