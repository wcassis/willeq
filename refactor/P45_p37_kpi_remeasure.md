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

### Phase 1 (prep only — Pi NOT contacted, per the hard gate)

**Builds**: `git log -1` at branch tip `a805e01` includes P37 (`0019ba6`-era
merge) and P42 (`4781815`). Existing `build-arm-noble/`+`build-gles2/` trees
predated both (Jul 27-28, merges landed Jul 29-30) — force-rebuilt both from
scratch rather than trusting the post-crash stale trees (per the plan's
warning). `bash scripts/build-arm-noble.sh` and `bash scripts/build-gles2-x86.sh`
both completed clean. Sizes: `build-arm-noble/bin/gles2_zone_benchmark`
53,446,880B, `build-arm-noble/bin/willeq` 353,957,184B,
`build-gles2/bin/gles2_zone_benchmark` 14,388,464B,
`build-gles2/bin/willeq` 79,047,000B — no 0-byte stubs, no truncated trees.

**P37 exclusion — code-level check**: `race_model_loader.cpp`'s
`preloadModelData()` (lines ~879-904) confirmed present at this tip: on a
composite-JSON hit, cross-references `combinedGeometry->textureMasked()`
against the composite's included textures; if any is WLD-masked, leaves
`modelData->compositeData` unset (falls through to the per-part
`buildMeshFromGeometry()` path, which itself assigns `EMT_TRANSPARENT_ALPHA_CHANNEL_REF`
for masked materials per `mesh_building.cpp` line ~269-273). Both pieces of
P37's fix are live at this branch tip, unmodified.

**P37 exclusion — x86 smoke evidence** (`build-gles2/bin/gles2_zone_benchmark`,
`--zone befallen --eqe1-path /mnt/projects/EverQuest_ETC1 --composite-scenegraph
--log-level=INFO`, entity-counter/log evidence per the plan's allowance, no
pixels needed):
- `--test-races 34,17,35,67,89,53,33,31 --entities 8` (P08f's befallen
  zone-native set: BAT/ORC/RAT/ZOM/IMP/MIN/GHU/FPM): races 34 (BAT,
  `batft0001.bmp`), 17 (ORC, `orclg0002.bmp`), 67 (ZOM, `zomch0001.bmp`) all
  logged `composite EXCLUDED ... falling back to the per-part path`; races
  35/89/53/33/31 all logged `composite found`. Matches P37's ground-truth
  table exactly (BAT/ORC/ZOM masked-excluded, RAT/IMP/MIN/GHU/FPM
  unaffected) — **masked races get multibuffer, unmasked still composite,
  confirmed live**.
- Default race cycle (`--entities 12`, no `--test-races` — the
  `kDefaultRaceIds` cycle `gles2_zone_benchmark` uses when the flag is
  omitted, EQ race IDs 1-12: Human/Barbarian/Erudite/WoodElf/HighElf/
  DarkElf/HalfElf/Dwarf/Troll/Ogre/Halfling/Gnome): **all 12 logged
  `composite found`, zero exclusions.**

**CRITICAL FINDING — the canonical e205 replay does not exercise P37's fix
at all.** P14/P39/P41's own scripts (`refactor/scratch/p14_pi_occluder_bf472.py`,
`p39_pi_active_verify.py`, `p41_pi_run.py`) — and the blessed 32.313/32.5ms
p95 baselines they produced — all invoke `gles2_zone_benchmark` with
`--entities 205 --seed 42` and **no `--test-races`**, so the benchmark's
"e205 stress load" is 205 entities cycling through the generic player-race
IDs 1-12 confirmed above, never BAT/CAZ/DEN/ORC/ZOM/SKE (the exact races
P37 excludes). Reusing that same invocation shape for "exactly as the client
ships it" comparability (as the plan directs) means **Phase 2's headline
p95/draws_avg numbers are predicted to land statistically indistinguishable
from P14 (32.313ms)/P39 (32.5ms)** — not because P37 is KPI-neutral in
production, but because the standardized benchmark stand-in's synthetic
roster structurally cannot contain the affected races. This must be reported
as a **benchmark-coverage gap**, not a clean bill of health, if Phase 2 comes
back flat. (This gap predates P45 — P08f's own `--test-races` flag exists
precisely because the default cycle was never zone-representative — but P37
is the first unit where the gap directly hides the thing being measured.)

**Predicted deltas**:
- *As-configured (default race cycle, matches P14/P39/P41 exactly)*:
  ~0 draws_avg / p95 delta expected. Sanity check (P11b's missing
  `--eqe1-path` lesson): draws_avg should land near P14/P39's own
  composite-era figure (P11b: 287.7 avg, composite-animated), NOT the
  multibuffer figure (710.1) — if it lands near 710 instead, that's a
  silent composite no-op in THIS run, not a P37 effect, and must be
  re-checked before trusting p95. Built into `p45_pi_kpi_remeasure.py`'s
  `sanity_check()`.
- *Hypothetical, if befallen's real zone-native roster were exercised*
  (order-of-magnitude only, not a staged invocation): P37 excludes 5 of
  befallen's 19 zone-model races (BAT, CAZ, DEN, ORC, ZOM —
  `refactor/P08d_npc_composite_landing.md`'s roster enumeration; SKE is
  additionally excluded globally but isn't in befallen's own chr archive
  per that table) = 26.3%. Blending P11b's composited (6.26) vs
  multibuffer (18.42) draws/entity at that fraction:
  `6.26*(1-0.263) + 18.42*0.263 ≈ 9.46 draws/entity`, a **+51% increase**
  over all-composite. Against P11b's entities_visible avg (34.7): roughly
  `+111 draws/frame` on top of the ~288 composite-era total (**+~39%
  draws**). P14's margin at budget-256 was already thin (0.99ms under the
  33.3ms line) with GPU sync at 49.6% of the p95 frame — a submission-side
  draws increase of this magnitude, IF it ever hit production's real
  spawn-based entity mix, could plausibly erode or cross that margin, but
  this is a rough scaling argument, not a measurement, and is explicitly
  NOT what Phase 2's 2 planned invocations will test (they reuse the
  default-cycle "canonical replay" for baseline comparability, per the
  plan). Flagged for Will/orchestrator: closing this gap for real would
  need an explicit `--test-races`-based invocation and is out of this
  unit's 2-invocation budget unless the headline actually regresses.

**Phase 2 staging**: `refactor/scratch/p45_pi_kpi_remeasure.py` — mirrors
P14/P39/P41's exact shipped-preset invocation (`--animate --merge-zone-draws
--composite-scenegraph --eqe1-path`, production config args from the
manifest, `--entities 205 --seed 42`, 800x600, `befallen_seed42_1000f.path`,
1000 frames, `--gl-counters`). `--occluder-budget` intentionally omitted
(post-P42 the tool resolves 256 — the P14-blessed value — from
`ConstrainedRendererConfig` by default; passing it explicitly would be
redundant). 2 invocations planned (run1 + run2 for jitter), `sanity_check()`
gates trust in draws_avg before reporting p95, optional 3rd
`--test-races 34,17,35,67,89,53,33,31` attribution invocation documented but
NOT auto-run — only if the headline regresses and counters already collected
are ambiguous, per the plan.

**Not done this phase (by design)**: no Pi contact of any kind — no ssh, no
read-only checks. Phase 2 (the actual KPI runs) is gated on the
orchestrator's explicit go-ahead once P43 releases the Pi.
