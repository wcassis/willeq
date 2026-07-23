# P01 — Investigate: EQE1 slower than RGBA on Mali400 (+ lighting/culling questions)

Batch P (performance) opener, directed by Will. H04 measured EQE1/ETC1
consistently SLOWER than direct RGBA in p95 frame time on the Pi — but
Will's historical tests showed NO perf difference between ETC and RGBA
(memory footprint only). Explain the contradiction with controlled
experiments on the real hardware, and revisit Will's two open questions
from his last debugging era: (a) per-pixel lighting cost dominance (fps was
consistently high with per-pixel lighting DISABLED), (b) whether lighting
operations were being applied to geometry regardless of visibility /
whether scene-graph culling works as believed.

## Hypotheses to test (H1 is the orchestrator's prime suspect — try to
DISCONFIRM it, don't anchor)

- **H1 — H02d asymmetry:** H02d (merged 2026-07-23, BEFORE H04's runs) made
  RGBA alpha-test read the main texture's own alpha (ONE fetch/fragment)
  while ETC1 keeps the companion variant (TWO fetches). Historical ETC≈RGBA
  tests predate H02d, when BOTH paths sampled a unit-1 texture. Prediction
  if true: the delta concentrates in alpha-test-heavy scenes/frames
  (gfaydark canopy, befallen?); forcing both paths onto the same variant
  (or comparing pre-H02d builds) eliminates the delta; delta shrinks at
  lower resolution only if fragment-bound.
- **H2 — filtering/mipmap differences:** ETC1 main textures carry full mip
  chains + trilinear (per 00026f3/9189ad6 era fixes) and companions use
  NEAREST_MIPMAP_NEAREST; the RGBA path's mip/filter state on Pi (EQT_DRM
  build) may differ. Different sampling cost per fetch.
- **H3 — measurement confound:** load-order, cache residency, or thermal
  ordering in H04's runs (EQE1 ran after RGBA each pass?). Check H04's run
  script ordering and re-measure interleaved.
- **H4 — Lima ETC1 decode cost:** Lima may decompress ETC1 in a slow path.
  Least actionable; only reach for it if H1–H3 are excluded.

## Experiments (Pi, real hardware; strict I/O rules from
refactor/H04_pi_tier2.md apply — /tmp staging, governor pinning, cooldowns)

1. Reproduce the delta with interleaved A/B ordering (kills H3).
2. Per-frame analysis: correlate delta magnitude with alpha-test draw share
   per frame (the CSVs have per-frame draws; add a per-material-type count
   to the benchmark CSV if needed — flag-gated).
3. Resolution scaling: 400×300 vs 800×600 both paths — fragment-bound or
   not.
4. Variant forcing: smallest possible experiment that puts both paths on
   the SAME shader variant (e.g. temporarily force RGBA textures through
   the companion variant in a scratch build — experiment-only, never
   committed to production paths) → if delta vanishes, H1 confirmed.
5. Filter-state audit: log actual GL min/mag filters + mip presence for a
   sample of zone textures on both paths on the Pi (H2).
6. **Lighting cost curve (Will's question a):** benchmark `--lighting N`
   (0..8) sweep on one zone, both texture paths — quantify per-pixel
   lighting's share of frame time on Mali400 today. Compare against the
   historical perpixel benchmark data (docs/reference/orangepione/
   perpixel.txt, perpixellut.txt; tool gles2_shader_perpixel_benchmark).
7. **Lighting-vs-visibility (Will's question b):** using --no-frustum-cull /
   --no-pvs toggles and the draw/state counts, determine whether lighting
   work (uniform computation, light selection, per-vertex or per-pixel
   shader cost) scales with VISIBLE geometry or TOTAL geometry. Read the
   current post-Batch-F/G code (sim_visibility computeLightSelection,
   zone draw paths) to trace where light selection happens relative to
   culling; state clearly whether lighting is applied to culled geometry
   anywhere.

## Deliverables

Findings report in this file's Review section: the confirmed explanation
for the EQE1 delta (with numbers), answers to Will's two questions (with
numbers and code references), comparison table vs historical perpixel data,
and a prioritized list of fix candidates with estimated impact. NO
production fixes in this unit — investigation only (scratch builds for
experiments are fine in the worktree, never merged).

## Verification

- Every hypothesis explicitly confirmed/refuted with measurements.
- Pi left pristine (governor, /tmp) after every session.
- No production code changes committed (benchmark flag additions allowed if
  needed for experiment 2, with the usual bar).

## Review

(executor fills in)
