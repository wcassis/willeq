# H02b — Re-frame the golden set with representative scenes

Orchestrator visual review rejected the first golden set: qeynos2 frames show
fog/ground/sky with no city geometry (9-10 draws), gfaydark clips through a
tree, befallen views the crypt from outside its shell. The gate machinery
(H02) is sound; the CAMERA PLACEMENT is not. Produce goldens that actually
guard the renderer.

## Design

1. **Prefer the navmesh camera path** (`zone_bench_camera`'s
   `generateFromNavmesh`) — it follows walkable ground through the zone,
   which is exactly "what a player sees." Check whether navmesh files exist
   for candidate zones (EQT_HAS_NAVMESH build + nav assets; investigate what
   the benchmark needs and what exists on disk). If navmesh paths work,
   designated frames come from points along that path.
2. **Fallback: automated interesting-view search.** If navmesh isn't
   available for a zone, add a small opt-in benchmark mode that samples many
   candidate camera poses (e.g., grid over walkable surface positions ×
   several headings), renders each once, scores by draw count × distinct
   colors × non-sky pixel fraction, and reports the top poses. Pick
   designated poses from the top scorers, then hard-code them in the
   manifest as explicit `--camera-pose x,y,z,heading,pitch` flags (add flag
   if needed) — deterministic and self-documenting. Scoring/search runs at
   bless time only, never in the gate.
3. **Acceptance bar per golden frame** (write these numbers in the review):
   ≥40 draws OR ≥2,000 polys; ≥1,500 distinct colors; ≤60% of pixels sky/void
   color; and a human-readable one-line description of what's visible
   ("qeynos2 gate plaza with buildings both sides"). All three zones, 3
   frames each. Keep qeynos2/gfaydark/befallen unless one simply cannot
   produce good frames — then substitute and document.
4. Rebless via `visual_gate.sh --bless`; regenerate manifest baselines and
   contact sheet; verify plain gate passes; keep the negative test evidence
   from H02 valid (re-run the perturbation check once against the new set).

## Verification

- All 9 frames meet the numeric acceptance bar; review lists per-frame
  stats + descriptions.
- `visual_gate.sh` passes; determinism re-confirmed on the new poses (two
  runs byte-identical).
- Default build + ctest unregressed.

## Review

(executor fills in)
