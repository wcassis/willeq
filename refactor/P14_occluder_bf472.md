# P14 — bf472-targeted occluder-budget experiment (the GPU tail)

Approved by Will 2026-07-27 ("go ahead with all three").
Slot: wt/slot1, branch task/P14-occluder-bf472, cut from fable tip.
Measurement unit; OWNS THE PI (P15 owns eqemu; P16 needs neither).

## Motivation

P11b: full stack (merge + animated composites) puts befallen at avg
39.8fps / p95 28.3fps — 2.02ms over budget at p95, and the residual is
now GPU-BOUND (GPU sync 49.6% vs submission 26.5%), driven by bf472's
dense zone-geometry pose. P11's "occluder not worth landing" verdict
(0.9-2.7%) was measured on the whole-path average, WITHOUT the stack,
when CPU submission dominated — the question is different now: does a
raised occluder budget cut GPU fill at the dense-pose tail?

## Design

1. All runs: canonical befallen replay (--path-file), production config
   + --animate + calibrated N=205 + --merge-zone-draws +
   --composite-scenegraph (the P11b full-stack setup, reuse its driver
   script pattern), --occluder-budget swept {16 (default/control), 64,
   256, uncapped}. The knob exists (P11); expect zero benchmark code
   changes.
2. Per variant: whole-run avg/p95, GPU-sync column, submission column,
   sim/collect columns (occlusion is SOFTWARE/CPU — a raised budget
   buys GPU fill savings with CPU occlusion-test cost; measure BOTH
   sides), regions_occluded/regions_drawn, pinned-frame extraction with
   bf472 as the headline (bf355/626/791/917/987 for context).
3. x86 smoke first (one run per budget, engagement sanity:
   regions_occluded rises with budget); Pi P02 protocol, <=12
   invocations/session, reuse P11b's stack baseline CSV as the control
   if schema-identical (spend invocations on the new budgets).
4. Deliverables: the budget-response table (esp. bf472 GPU sync + p95);
   the net verdict (does any budget close the 2.02ms p95 gap, and at
   what CPU cost); recommended production occlusionMaxOccluderRegions
   value + the one-line landing description (REPORT ONLY — flip is
   Will's); if the lever is dead even at the tail, say so and name what
   the GPU tail actually needs instead (fill reduction elsewhere,
   resolution, etc. — from the data, not speculation).

## Verification
- Standard bar (build, ctest known exceptions, gles2-x86, gate PASS
  goldens untouched, ARM build — rebuild BEFORE Pi staging, file-size).
- Replay discipline; expected-column-delta documentation (occlusion
  changes drawn sets BY DESIGN; pose columns must match the path).
- CSVs → benchmarks/pi_runs/p14_occluder_bf472/.

## Out of scope
- Landing the production value (report); occlusion redesign; eqemu.

## Review
(executor fills in)
