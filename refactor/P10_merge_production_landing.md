# P10 — Land the cross-region zone draw merge in production (flag-gated)

Approved by Will 2026-07-26 ("go ahead with the zone-merge production landing").
Slot: wt/slot1, branch task/P10-merge-landing, cut from fable (a7eabc4).
The largest landing of the campaign. Flag-gated, default OFF; the
orangepi preset flip is a SEPARATE final commit gated on Will's bless
(P08d pattern). Benchmark-proven basis: P09 A-gate (client-side index
gather ~400x cheaper than per-region draws on Lima), P09b/P09c
correctness (merged vs baked-unmerged pixel-identical to 1-LSB at 15/15
poses), P09c Pi numbers (befallen zone-only p95 173.8→25.5ms, draws
253→7.9; combined e205 279.4→65.8ms).

## Design (follow the validated sketch — P09 review D11 + P09c updates)

### A. Architecture (mirror the prototype's semantics exactly)

1. **Reference implementation**: tools/hwcaps/zone_bench_merge.cpp/.h —
   the semantics to reproduce: shared per-material world-space vertex
   data; per-frame CPU gather of visible+resident regions' indices into
   runs keyed by FULL SMaterial equality (operator==, the P09b lesson) ×
   adaptive-lighting tier; one client-side-index glDrawElements per run;
   indices emitted in EXACTLY the order the unmerged path would submit
   (deterministic; z-fight parity; Q02's name-hash keys unchanged).
2. **Flag**: ConstrainedRendererConfig::mergeZoneDraws (default false)
   + JSON plumbing + a /mergedraws debug toggle (the /sort pattern) for
   live A/B. Flag OFF = byte-untouched existing path (prove it).
3. **Fix site**: renderer_zone_draw.cpp drawZoneGeometrySorted() grows a
   merged submission branch consuming the SAME sortedDrawEntries_
   pipeline (collection/culling/lighting decisions unchanged upstream).
   New machinery in new TUs (renderer_zone_draw.cpp is size-capped).
4. **The hard part — mesh-cache/streaming reconciliation** (the reason
   this is a design landing, not a port): production builds region
   meshes lazily (ConstrainedMeshCache, 24MB budget, eviction) and
   streams uploads via the GPU upload thread; the prototype baked
   everything at load. Per the sketch, reconcile with per-material
   shared VBOs + region-range allocation (region resident → its ranges
   live in the shared VBOs; evicted → ranges freed; gather only touches
   resident regions, which composes naturally with the existing
   residency logic). Fragmentation policy per the sketch (compaction on
   the loading/background thread, or allocator with free lists — choose,
   justify, measure memory overhead vs the 24MB budget). If shared-VBO
   eviction turns out to be fundamentally incompatible with the cache
   design, STOP and report a design memo — do not half-land (an
   acceptable reduced first landing, IF the memo justifies it: merge
   only for zones where the cache is inactive/fully-resident — the
   orangepi preset's golden zones may all fit — with the cache-active
   path falling back to unmerged; report which zones that covers).
5. **Interactions checklist (each proven, not asserted)**: PVS/frustum/
   occlusion culling (gather = resident ∩ visible), count AND radius
   adaptive-lighting modes (runs break at tier boundaries; per-region
   decisions unchanged), front-to-back material-group order, zone
   objects (in scope only if the prototype covered them — otherwise
   document that objects stay unmerged and count their draws),
   /sort + portal/stencil paths (merged branch composes or cleanly
   defers to unmerged when those modes are active — document which).

### B. Correctness evidence (production has no golden harness — build the A/B)

6. **In-app A/B**: x86 GLES2 real client (build-gles2 willeq, surfaceless
   EGL, no X needed) against the eqemu automation (scripts/eqemu/up.sh,
   H03's sanctioned flow; casterella config pattern). At 3+ fixed poses
   in a golden zone: /mergedraws off → capture, on → capture, pixel-diff.
   Investigate what screenshot capability exists (command, RDP frame
   dump, or a small additive debug dump — smallest thing that works).
   Expected delta: ONLY the P09c FP class (scattered alpha-test-edge
   pixels + z-tie flips; the baked-positions transform delta) — quantify
   per pose; anything structural = stop and fix. If server automation is
   unavailable/broken, STOP and report rather than shipping without A/B.
7. **Draw-count evidence**: a per-zone INFO line (P08d adoption-line
   pattern): unmerged-would-be draws vs merged runs per frame (sampled),
   so live sessions show the reduction without instrumentation builds.
8. Full suite: ctest (known exceptions), visual gate PASS goldens
   UNTOUCHED (gate exercises the benchmark, not production), x86 GLES2 +
   ARM builds, file-size check. Flag-off byte-identity: code inspection
   + one A/B-off-vs-pre-branch capture at a fixed pose (identical).

### C. Perf evidence (bounded — the benchmark numbers are the case)

9. Optional single Pi session (P02 protocol, <=6 invocations) ONLY if
   the executor can drive the real client on the Pi non-interactively;
   otherwise SKIP — Will's live play after the bless is the real-world
   check, and the P09c benchmark numbers stand as the projection. Do
   not burn budget forcing this.

### D. Bless package for Will

10. The A/B captures + per-pose FP-delta quantification + the existing
    P09c FP-delta contact sheet (benchmarks/contact_sheets/p09c_fp_delta/)
    + the draw-count INFO evidence + memory-overhead numbers →
    benchmarks/contact_sheets/p10_merge_landing/. Preset flip
    (orangepi mergeZoneDraws=true) as the SEPARATE final commit; Will
    decides bless/flip after reviewing (and ideally after a live Pi
    session with /mergedraws).

## Out of scope

- Entity/object draw merging beyond what the prototype covered.
- Occlusion redesign; lighting changes; the --animate unit; goldens;
  benchmark changes (reference only).
- Removing the unmerged path (it stays as the flag-off path and the
  fallback for incompatible modes).

## STOP conditions

- Mesh-cache reconciliation infeasible within the sketch → design memo +
  (optionally) the reduced fully-resident-zones landing per A4.
- A/B shows structural (non-FP-class) diffs that resist quick
  attribution → stop with evidence (P09b/P09c precedent).
- Server automation broken → stop; no landing without in-app A/B.

## Review

(executor fills in)
