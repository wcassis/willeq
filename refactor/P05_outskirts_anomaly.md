# P05 — The outskirts anomaly: fps inversely correlated with scene complexity

Will's field observation (2026-07-24): >45fps WITH per-pixel lighting in
mid-city qeynos2, but LOWER fps on the outskirts where the scene is
simpler. His suspicion from past testing: excess draw calls and/or Irrlicht
scene-graph components eating the render budget — "deleted nodes? filtered
nodes still taking time?" Draw-call reduction is a standing critical thread
(it motivated composite entities). This unit finds where the render budget
actually goes in simple-scene/low-fps areas.

## Hypotheses

- **A. PVS breadth:** open outskirts see MORE regions (long sight lines) →
  more draws/materials despite visually simpler content. Measurable
  directly (region/draw counts per pose).
- **B. Scene-graph traversal overhead:** Irrlicht's drawAll/
  OnRegisterSceneNode walks EVERY node each frame — hidden entities, doors,
  objects, decayed-corpse leftovers — cost scaling with node COUNT, not
  visible complexity. Outskirts poses may coincide with high node counts
  (spawn clusters, ground spawns). Also check node lifecycle: are removed
  entities' nodes actually dropped from the graph or just hidden
  (remember: invisible nodes skip OnAnimate but are still traversed)?
- **C. Per-frame CPU work scaling with total-not-visible sets** elsewhere:
  entity interpolation/name tags (sim_entity_sync), door manager, detail
  system, animated textures — audit what iterates over ALL vs VISIBLE.
- **D. Fill/overdraw:** outskirts terrain = large triangles, full-screen
  ground+sky overdraw vs city's depth-testing? Would show as GPU-bound
  (gpu_ms), distinguishable from B/C (cpu_ms).

## Method

1. **Find the poses**: use --pose-search plus manual picks to get 3
   "mid-city dense" and 3 "outskirts simple" qeynos2 poses; confirm the
   anomaly reproduces in the benchmark (it may be entity/game-state
   dependent — if benchmark-only poses do NOT reproduce it, that ITSELF
   localizes the cause to live-game components (entities/doors/sim), and
   the investigation shifts to the full client under the recording tour on
   the Pi with the frame-timing instrumentation).
2. Per-pose breakdown: draws, state changes, polys, regions visited,
   cpu_ms vs gpu_ms split (the benchmark has all of this), at per-pixel
   lighting (production config).
3. **Node-traversal instrumentation** (flag-gated): count scene nodes
   visited vs rendered per frame in the production path (RenderPassTimer /
   drawAll — post-F18 the portal path; entities are separate smgr nodes) —
   the direct test of hypothesis B. Include counts of hidden/zero-poly
   nodes visited.
4. Code audit for C: grep/trace the per-frame loops (processFrame*
   sub-passes, entity_renderer update paths, sim outputs applied) for
   all-vs-visible iteration; file:line list.
5. Pi runs under P02 protocol for the real numbers; llvmpipe runs for
   CPU-side amplification where useful.

## Deliverables

- The anomaly reproduced (or localized to live-game components) with
  numbers; hypothesis verdicts A-D.
- Where the budget goes in outskirts poses: a ranked cost table.
- Fix candidates ranked by estimated fps recovery, feeding the draw-call
  reduction thread — NO fixes in this unit.

## Review

(executor fills in)
