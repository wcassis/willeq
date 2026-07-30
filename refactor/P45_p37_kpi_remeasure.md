# P45 — Post-P37 Pi KPI re-measure (befallen de-composited attribution)

**Slot: slot3. Pi unit — but the Pi is currently OWNED by P43
(slot1). HARD GATE: complete Phase 1 (prep, no Pi contact of any
kind — not even a read-only ssh), then STOP and report READY-FOR-PI
to the orchestrator; touch the Pi only after the orchestrator
explicitly confirms P43 has released it. P02 protocol then applies
(budget ≤5 invocations). No eqemu.**

P37 excluded ~15 masked races from composite eligibility (SKE
globally + ZOM in befallen — befallen's own population). The blessed
befallen KPI (P14: whole-run p95 32.313ms, 0.99ms under the 33.3ms
30fps line; P39's re-land spot: 32.5ms p95) was measured with those
races composited. Their draws have reverted to multibuffer — the
margin is at risk. Measure, attribute, and report; the
regress-response decision (composite-sheet alpha support in the
builder + redeploy, vs accept for era correctness) is WILL'S CALL —
this unit changes nothing.

## Work

Phase 1 (NO PI):
1. Build the ARM binary at your branch tip (current fable incl. P37 +
   P42). Verify the P37 exclusion is active in it (code-level check +
   x86 smoke: masked races get multibuffer, unmasked still composite).
2. Stage the run plan: befallen e205 canonical replay (--path-file,
   benchmarks/paths/), shipped orangepi preset exactly as the client
   ships it (merge + composites + occluder-256 via the P39 JSON path;
   note the benchmark now resolves occluder 256 by default post-P42).
   Reuse scripts/pi/ + refactor/scratch/p41_pi_run.py idioms.
3. STOP. Report READY-FOR-PI with what you'll run and predicted
   deltas (draws/entity arithmetic from P37's eligibility change —
   P11b measured 6.26 draws/entity composited vs 18.42 multibuffer).

Phase 2 (after orchestrator go-ahead only):
4. Pre-flight CmaFree gate; then the KPI runs: 2 invocations (whole
   replay, shipped preset; repeat for jitter). Optional 1 attribution
   invocation only if the headline number regresses and per-race
   attribution is ambiguous from counters already collected.
5. Verdict table vs the 33.3ms line and vs P14's 32.313 / P39's 32.5
   p95 baselines: draws_avg, entity submission share, GPU/total
   avg+p95. Sanity: draws_avg must move in the direction the
   eligibility change predicts — if composites silently no-op'd
   (P11b's missing --eqe1-path lesson), catch it and re-run, don't
   report garbage.
6. If p95 crosses 33.3ms: report the two options with the numbers
   attached (builder sheet-alpha scope estimate from P37's
   investigation notes vs accepted loss size). NO fix, NO redeploy.

## Rules

- Pi protocol absolute (CmaFree gate, /tmp-only timed writes,
  drop_caches never reboot, pristine exit, governor ondemand).
- No production source changes; flag-gated instrumentation only if
  counters prove insufficient (unlikely — reuse existing).
- CSVs → benchmarks/pi_runs/p45_p37_kpi/; tables + verdict in this
  file's Review.
- Build-tree caution: post-crash 0-byte stub binaries possible —
  verify binary sizes.

## Verification

- Measurement bar: builds clean, no source diff (or gate+ctest if
  any), invocation count reported, Pi exit state reported.

## Review

(executor fills in)
