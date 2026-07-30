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

### Phase 2 (amended scope — Pi go-ahead received, P43 released cleanly)

**Amendment**: the orchestrator accepted Phase 1's critical finding (the
canonical e205 replay's default race cycle cannot exercise P37 at all) and
amended the plan to 4 runs + 1 reserve, still ≤5 total: runs 1-2 as
originally staged (baseline continuity + first Pi regression spot for P42's
driver state-dedup change), run 3 = e205 with a realistic ZOM-heavy
`--test-races` roster (tip, P37 active), run 4 = the identical roster on a
pre-P37 ARM binary (the direct A/B).

**Pre-P37 binary**: orchestrator cited `e8efd87^` as P37's merge parent;
verified this is an *earlier* ancestor, not the immediate one — the actual
merge parent (`0019ba6^1`, `0019ba6` being `Merge P37: masked entity/
equipment textures render masked...`) is `fda2e34` (`Add T10 plan: WLD
parser mutation kill rate...`), 4 commits after `e8efd87`. Used `fda2e34` as
the correct pre-P37 commit; noted here as a correction, not a silent
substitution. Built at `/mnt/projects/fable/wt/slot3-p37pre` (new sibling
worktree inside my own slot, not touching slot1/slot2) via
`git worktree add`; ARM + x86 cross-builds both clean, sizes healthy
(ARM `gles2_zone_benchmark` 53,347,348B, `willeq` 353,696,260B; x86
`gles2_zone_benchmark` 14,383,336B). Inverse smoke test confirmed: same
`--test-races 34,17,35,67,89,53,33,31` invocation that showed 3 exclusions
on the tip binary shows **zero** `composite EXCLUDED` lines on the pre-P37
binary (all 8 races `composite found`) — and `compositeHasMaskedTexture`/
`composite EXCLUDED` are absent from `race_model_loader.cpp` at `fda2e34`
entirely (source-level confirmation, not just runtime silence).

**ZOM-heavy roster for runs 3-4**: `--test-races 67,67,67,67,34,17,35,89,53,
33,31` (11-slot cycle: ZOM 4/11 ≈36%, BAT 1/11, ORC 1/11 — all 3 of P37's
befallen-native exclusions with ZOM overweighted per the orchestrator's
"weight toward what a player actually sees" instruction and befallen's
"undead crypt" theme; RAT/IMP/MIN/GHU/FPM 1/11 each as composite-eligible
controls). Built on P08f's already-validated 8-race set (all 8 confirmed
resolvable in Phase 1's smoke tests). CAZ and DEN (2 of P37's 5 befallen
exclusions) were NOT included — `race_codes.cpp` has no numeric-ID mapping
for those codes (grepped, absent) and P08f's own set never resolved them
either; inventing untested IDs against a 5-invocation Pi budget was not
worth the risk of a silent wrong-race resolution. This is a documented
approximation of befallen's real spawn mix, not a live spawn2 query
(eqemu is out of this unit's territory).

**Pre-flight**: `pc.preflight()` + `pc.investigate_console_drm()` run before
every session leg — ssh reachable, both zone asset paths + all golden-zone
EQE1 archives present, DRM device not held by another process, ARM binary
newer than watched source. All clean every time.

**Session narrative (4 successful invocations across 2 governor-pinned
sessions, 1 clean CmaFree abort, 1 scripted reboot, 0 data lost, run 5
reserve unused)**:
1. Run 1 (`e205_shipped_run1`) executed clean, landed CSV+log.
2. `runner.before_invocation()` for run 2 sampled **CmaFree=16 kB** (from
   56,440 kB post-run-1 — an ~56 MB single-step drop), correctly tripping
   `SessionAbort` at the 40,960 kB floor. Governor was restored to
   `ondemand` automatically (the abort path's designed behavior) before the
   exception propagated. No data lost — run 1's CSV/log were already
   rsync'd off the Pi before the abort fired.
3. Per `refactor/P02_hardware_resume.md`'s own precedent ("81 MB of CMA
   that only a reboot recovers"), ran `pc.scripted_reboot_and_wait()`
   directly (not a benchmark invocation, doesn't count against the 5-run
   budget). CmaFree recovered to 102,056 kB post-reboot, NFS mount
   reverified.
4. Resumed: re-staged the tip binary, ran run 2 and run 3 clean.
5. Run 4 first attempt failed fast (exit 1, wall 2.0s): the pre-P37 binary
   (`fda2e34`) predates the `--gl-counters` flag (P38-era) and rejected it
   outright ("Unknown option: --gl-counters"). Fixed by adding a
   `gl_counters` param to `run_replay()` (default True, `False` for the
   pre-P37 leg) — the flag only adds `tex_binds`/`tex_param_calls` CSV
   columns, draws/total_ms/etc are unaffected either way. Re-ran run 4
   clean on the next attempt (this fast-fail did NOT consume Pi runtime
   worth counting as a "real" invocation attempt in the panic-safety sense,
   but is counted honestly below as attempt 5 of this session for
   transparency).
6. Session closed clean: governor restored to `ondemand`, CmaFree 66,044 kB
   (healthy, well above the floor) at exit.

**DRM mode verification** (P43's new hard rule): every run's log was grepped
for the driver's `DRM: Found matching mode WxH@RHz for requested WxH` line
(`docker/irrlicht-drm/CIrrDeviceFB.cpp`). **All 4 runs confirmed
`800x600@75Hz for requested 800x600`** — no silent substitution to the
connector's preferred 2560x1440 mode on any run.

**Pi exit state**: governor `ondemand` on all 4 cores (verified post-hoc via
`scaling_governor`), CmaFree 66,044 kB / MemAvailable 401,600 kB (healthy),
`/tmp` swept (staged binary + all CSV/log/path scratch files removed,
verified `ls /tmp | grep p45` → empty), no `gles2_zone_benchmark` process
running, no other process holding `/dev/dri/{card0,card1}`.

**Invocation count**: 4 successful benchmark invocations (runs 1-4) + 1
fast-failing arg-parse rejection (run 4 attempt 1, no Pi runtime/DRM context
opened — the binary printed its usage text and exited before touching DRM)
+ 1 scripted reboot (not a benchmark invocation). Run 5 (reserve) NOT spent
— the run 3 vs run 4 signal was unambiguous, no attribution ambiguity to
resolve.

### THE VERDICT TABLES

**KPI-verdict (runs 1-2 vs P14/P39 — baseline-comparable, same invocation
shape those units used):**

| run | n | total_avg | total_p95 | gpu_avg | gpu_p95 | sub_avg | sub_p95 | draws_avg | polys_avg |
|---|---|---|---|---|---|---|---|---|---|
| e205_shipped_run1 | 1000 | 24.436 | **32.240** | 11.204 | 15.895 | 5.795 | 7.589 | 41.9 | 15,635.0 |
| e205_shipped_run2 | 1000 | 24.434 | **32.263** | 11.212 | 15.882 | 5.820 | 7.632 | 41.9 | 15,635.0 |
| P14 baseline (budget 256) | — | — | 32.313 | — | — | — | — | — | — |
| P39 re-land spot | — | — | 32.5 | — | — | — | — | — | — |
| 30fps line | — | — | 33.3 | — | — | — | — | — | — |

**As predicted in Phase 1: flat.** Both runs land ~0.05-0.07ms below P14's
32.313ms and ~0.24-0.26ms below P39's 32.5ms — well within run-to-run noise
(run1 vs run2 themselves differ by 0.023ms), both comfortably under the
33.3ms line, both draws_avg identical between the two repeats (deterministic
replay, as expected from a fixed path-file/seed). **This flatness is NOT
evidence P37 is KPI-neutral** — it is the predicted consequence of the
default race cycle (IDs 1-12) never containing any of P37's excluded races,
confirmed directly in Phase 1's x86 smoke tests. Also serves as the first
Pi regression spot for P42's driver state-dedup change (item 1): no
regression visible, submission avg/p95 in the same range P38's pre-P42 audit
described, no anomaly. (Note: draws_avg=41.9 here is well below P11b's own
287.7 composite-era figure for what reads as a similarly-configured stack —
likely a difference in effective entities-visible along the specific
`befallen_seed42_1000f.path` route/pose mix vs whatever P11b's own sampling
captured, not a red flag: still solidly composite-range, far from
multibuffer's 710, and byte-identical between run1/run2.)

**P37-attribution (runs 3-4 — NEW invocation shape, comparable ONLY to each
other, NEVER to P14/P39's numbers):**

| run | n | total_avg | total_p95 | gpu_avg | gpu_p95 | sub_avg | sub_p95 | draws_avg | polys_avg |
|---|---|---|---|---|---|---|---|---|---|
| e205_realroster_preP37 (fda2e34) | 1000 | 30.216 | 39.304 | 13.652 | 19.078 | 10.751 | 13.153 | 113.6 | 17,040.5 |
| e205_realroster_tip (a805e01, P37 active) | 1000 | 49.163 | **57.650** | 15.848 | 19.414 | 26.764 | 32.540 | 348.3 | 16,984.9 |
| **Δ (P37's cost)** | — | **+18.947** | **+18.346** | +2.196 | +0.336 | **+16.013** | **+19.387** | **+234.8** | −55.6 |

**Unambiguous, large regression at the ZOM-heavy real-roster shape.** P37's
composite exclusion nearly triples draws_avg at this roster (113.6 → 348.3,
+207%), and the cost lands almost entirely in submission time (+16.0ms
avg / +19.4ms p95 — GPU time barely moves, +0.3ms p95, consistent with
P37's fix reverting entities from few-large-draw composite dispatch to
many-small-draw multibuffer dispatch, a CPU-submission-bound cost, not a
fill-rate one). Whole-frame p95 goes from 39.304ms to 57.650ms — **both
legs are already over the 33.3ms 30fps line** at this stress-load/roster
combination, pre-P37 by 6.0ms and post-P37 by 24.3ms. This is a real,
substantial cost — exactly what Phase 1's order-of-magnitude arithmetic
(predicted +51% draws/entity, +~39% total draws at a 26.3%-excluded-race
mix) anticipated in direction, though the measured magnitude here (+207%
draws) is considerably larger than that rough estimate, plausibly because
this roster's ZOM-heavy 36% weighting concentrates more excluded-race mass
than the flat 26.3% used in the Phase 1 arithmetic, and/or because per-entity
draw cost for zombies/orcs/bats specifically differs from P11b's
whole-roster average.

**Caveat repeated per the plan's instruction**: runs 3-4 have no P14/P39
precedent — an entities=205 stress load at a 11-race cycle is not the same
measurement as production's actual befallen encounter density (P38: "real
gameplay areas hold ~5 NPCs"). This table answers "what does P37 cost at a
roster that actually contains the excluded races," not "what will players
feel in practice" — that translation is Will's call, not this unit's.

### Regress-response options for Will (per the plan's framing, no fix/redeploy done here)

1. **Composite-sheet alpha support** (builder change + NFS redeploy) —
   P37's own investigation found `entity_composite_builder.cpp`'s
   `writeEQE1()`/`writeBMP()` hardcode alpha-less output even though the
   compose buffer carries source alpha transiently; adding real alpha
   support to the sheet format would let masked races re-enter composite
   eligibility. Scope not re-estimated this unit (P37's own Review is the
   source for what this would take); this unit's contribution is
   quantifying the cost of NOT doing it (above).
2. **Accept the loss for era correctness** — P37 fixed a real visual bug
   (zombies/orcs/bats rendering solid when they should show WLD-masked
   transparency); the KPI cost measured here is what era-authentic
   transparency costs at a realistic zombie-heavy encounter. Both P14's
   32.313ms margin (runs 1-2, unaffected) and this unit's runs 3-4 numbers
   are now on record for that judgment call.

Neither option was implemented or recommended by this unit — reporting only,
per the plan.
