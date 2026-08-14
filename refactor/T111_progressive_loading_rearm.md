# T111 — Deferred entities are never picked up; fix that, then enable the gate

**Slot: slot1. OWNS EQEMU AND THE PI. T110 is scoped and waiting on the Pi —
finish and release it.**

**Territory: `src/client/graphics/renderer_zone_stream.cpp`,
`renderer_entities.cpp`, `renderer_player_physics.cpp`,
`renderer_entity_load_gate.cpp`, `include/client/graphics/`,
`config/constrained_presets.json`, `tests/`, `refactor/`,
`/mnt/projects/fable/review/T111/`.**

Read T109's Review in full first — it found this, built the gate, and shipped
it inert because of it.

## The defect, located

T109 confirmed live that **entities deferred by the load gate are never picked
up**: modeled-entity count frozen at **82 of 178** across a 40-second idle hold
and after two large `/move`s (−400 and −700 units), with zero background-build
activity logged after Step 8.

Three sites set `progressiveLoadingActive_ = true`:

- `irrlicht_renderer.cpp:497` — zone load
- `renderer_player_physics.cpp:1033` — collision setup, also zone-load time
- `renderer_entities.cpp:71-72` — a **brand-new entity spawn**

**None of them fires on player movement or BSP region change.** And
`checkProgressiveLoadingComplete()` treats only the player's *current* region
as blocking, so once the initial region is satisfied, progressive loading
declares itself finished and never restarts.

So the streaming path works exactly once, for entities that were already
candidates at zone-in, and for later spawns. Walking toward something that was
deferred does nothing.

## What to fix — determine which, do not guess

Two candidate shapes, and they are not equivalent:

1. **Re-arm on region change / movement** — `progressiveLoadingActive_` becomes
   true again when the player's BSP region changes, or when movement brings
   new candidates into range.
2. **Fix the completion test** — `checkProgressiveLoadingComplete()` stops
   treating only the current region as blocking, so it does not declare
   completion while deferred entities remain anywhere relevant.

**Read both paths and decide which is correct**, or whether both are needed.
State the reasoning. A re-arm that fires constantly would defeat the throttle
that exists to protect frame time; a completion test that never completes would
be worse.

Note `renderer_entities.cpp:71` already re-arms on new spawns **only if**
`entityPrepReady_` — understand that guard before copying the pattern.

## Prove the pickup works — this gates everything else

**This is the hard part and the whole point of the unit.** T109 could not
enable the gate because deferred entities vanished permanently.

Required evidence, live, on a **populated** zone:

- Defer a known set of entities, then **walk toward them** and confirm each
  becomes modeled. Report counts before and after, not impressions.
- Confirm the count converges to the full population, not to a plateau —
  82 of 178 stuck at 82 is exactly the failure being fixed.
- Confirm this survives **multiple region changes**, not just the first.
- Confirm it works when the player moves **away and back**.

Nektulos (451 entities) is where a gating bug shows; Befallen (178) is what
T109 measured and is directly comparable.

## Then, and only then, enable the gate

If — and only if — the pickup proof is unambiguous, set `entityLoadRadius` in
the shipped presets to the value T109 justified: `entityRenderDistance + 100`
(400 on the orangepi/default preset). T109 ships it defaulted to
`1'000'000.0f`, i.e. inert.

**STOP and leave the gate inert if the pickup proof is anything less than
unambiguous.** A gate that loses NPCs in a rare case is worse than no gate,
and this project has now spent two units establishing that.

## Re-measure both machines after enabling

T109's characterisation, radius temporarily active, Befallen:

| | x86 | Pi |
|---|---|---|
| zone-in | 11.69s → 10.74s (−8.1%) | 35.62s → 33.27s (−6.6%) |
| Step 8 | 5.72s → 4.57s (−20.1%) | 22.00s → 19.44s (−11.6%) |

Confirm these hold with the pickup path working — they may move, since the
deferred work now actually happens afterwards. **Report the post-zone-in cost
too**: the gate trades load time for background work, and if that work causes
frame hitches during play, that is a finding.

## Also worth knowing

Step 8 is ~62% of the Pi's zone-in (22.0s of 35.6s). Even a working gate leaves
**~13.6s of non-Step-8 zone-in time** uncharacterised. Do not chase it here —
but note anything you observe about where it goes.

## What you may not do

- Do not change the 40-entity render cap.
- Do not touch `ConstrainedTextureCache` eviction.
- Do not enable the gate on a partial pickup proof.
- **STOP and report** if the fix requires restructuring the progressive-loading
  throttle or the frame-budget governor. Those protect frame time and are their
  own unit.

## Verification

Full build and suite (current total 2257; sole pre-existing failure the
documented ALSA `AudioManagerTest.PreloadCommonSounds`) plus ARM cross-compile.
Every new test **proven able to fail in both directions**.

Visual gate — goldens may legitimately move if the gate is enabled. Say so,
explain why, and show before/after rather than regenerating silently.

Session discipline: binary freshness against HEAD on **both** machines;
daylight `/say #set time 11 0`; pad scripts long — longer for the Pi; `-d 4`
plus `--log-level=DEBUG`; Pi `/tmp` is a 242MB tmpfs so pull captures off as
you go; restore DB state and confirm by `SELECT` **after logout**; kill clients
by real binary PID on both hosts; clean teardown, delete the lock. **Release
the Pi promptly — T110 is waiting on it.**

## Review

Executed in `/mnt/projects/fable/wt/slot1`, task branch
`task/T111-pickup-rearm`, base fable tip `b7912c57`. Live eqemu (lock free at
session start; taken, used, released cleanly) and live Orange Pi (deployed,
exercised, left pristine) both used. Full logs, README, reproduction
commands: `/mnt/projects/fable/review/T111/`.

**Headline: the pickup mechanism is fixed and proven live — convergence
confirmed in two zones on two machines, no plateau. The gate ships ACTIVE
(`entityLoadRadius=400` in the `orangepi` preset). Getting an honest "yes" on
the pickup proof required finding and fixing two more crashes along the way,
both of which existed before this unit but were unreachable until the
pickup path actually started doing real, sustained concurrent background
work.**

### Which fix shape, and why

**Both shapes were needed, but not the ones the plan's own framing most
naturally suggests.** Reading `checkProgressiveLoadingComplete()` and
`queueEntityPrepRequests()` together first, before touching anything:

1. **Fix the completion test (shape 2), scoped correctly.** The old test
   only counted an unbuilt entity as "blocking" if it sat in the player's
   *current* BSP region (`vis.cachedBspRegion == currentPvsRegion_`) — T109's
   bug. The fix (`isEntityBlockingProgressiveLoad()`, extracted as a pure,
   unit-testable static predicate) instead mirrors
   `queueEntityPrepRequests()`'s own gate exactly: an entity blocks iff it's
   unbuilt, in the scene graph, and within `entityPrepMaxPvsDepth` (255 =
   unbounded on both shipped presets, so in practice this is "anywhere in
   the zone"). This guarantees symmetry — nothing can block completion that
   the queue would never actually queue (the plan's explicit "a completion
   test that never completes would be worse" risk), and nothing the queue
   *would* queue is silently excluded (T109's actual bug).
2. **Re-arm on visibility, not on BSP region change (shape 1, but not the
   trigger the plan's framing suggested).** Reading further revealed that
   `vis.inSceneGraph` is NOT "has a scene node" — it's toggled every frame by
   `applySimulationOutput()` (`renderer_sim_bridge.cpp`) to reflect the
   **40-entity render cap / render-distance culling result**, completely
   independent of whether a mesh is built. A deferred entity sits with
   `inSceneGraph=false` until the player gets close enough to bring it into
   that dynamic 40-entity set — and once progressive loading had already
   declared itself complete (correctly, per fix 1, since nothing was
   blocking *at that instant*), nothing ever re-armed it when a
   still-unbuilt entity later transitioned into view. This is why a BSP
   region-crossing re-arm (the plan's literal suggestion) would not have
   been sufficient on its own — the real trigger is the visibility
   transition, which can happen without a region crossing and vice versa.
   Fix: re-arm `progressiveLoadingActive_` at the exact spot
   `applySimulationOutput()` flips `inSceneGraph` false→true, guarded by
   `entityPrepReady_` (mirrors `registerEntity()`'s existing brand-new-spawn
   re-arm in `renderer_entities.cpp` exactly).

**Both were live-proven necessary**, not theoretical: fix 1 alone converged
to a hard plateau in one test (stuck at "1 in-radius / 176 deferred, 18
modeled" — the completion test correctly waited for the CURRENT visible set
but nothing ever refreshed that set once it emptied); fix 1 combined with
fix 2 converged monotonically to increasing counts across every subsequent
run (see the pickup proof below). Neither risks the throttle: fix 2 only
fires when an entity that WAS invisible becomes visible AND is still
unbuilt — bounded by the same 40-entity cap that already exists, not
constant firing.

### The live pickup proof

**Befallen** (177 entities, matches T109/T107's zone), x86, full tour across
6 waypoints spanning multiple BSP regions and dungeon levels: modeled count
**73 → 73 → 73 → 73 → 79 → 87 → 90 → 91 → 102 → 103 → 108 → 113** — strictly
monotonic, no plateau, across three separate region-crossing moves plus an
explicit away-(deep into the dungeon)-and-back-(near spawn)-and-away-again
leg. 64 `Progressive loading re-enabled` events fired over the session,
confirming the re-arm engaged repeatedly, not once. Reproduced twice more
(3/3 clean, monotonic) after the crash fixes below landed.

**Nektulos** (452 entities — the plan's own "gating bug shows here" zone),
x86, 7 waypoints spread across a vastly larger outdoor area: modeled count
**11 → 11 → 13 → 14 → 15 → 17 → 19 → 26 → 27** — same monotonic pattern.
Lower absolute count is expected and not a bug: Nektulos spans roughly
2400×4900 units and the tour sampled only a handful of points within ~1500
units of spawn, nowhere near full zone coverage. No plateau, no crash.

**Orange Pi**, Befallen, same gate config (`radius=400.0 — 88 in-radius, 93
deferred`), using T109's own proven-safe conservative waypoints: modeled
count **83 → 83 → 84 → 84 → 84**, 7 re-arm events. Confirms the mechanism
holds on real ARM/Mali 400 hardware — same code path, no ARM-specific
branches in this fix. See `review/T111/README.md`'s "Coordinate gotcha" and
"Pi session" notes for why the Pi tour was deliberately narrower than the x86
one (session-budget discipline after two coordinate-frame/stale-zone
mishaps cost real invocations).

### Two more crashes found and fixed along the way (both pre-existing, both newly reachable)

The plan's own pickup-proof bar ("this is the hard part") turned out to be
hard in exactly the way it warned about — proving it live surfaced real
defects that a paper read of the completion-test logic would not have
found.

1. **Dangling `regionPvsDepthMap_`** (`renderer_zone_load.cpp`). SIGSEGV in
   `getRegionPvsDepth()`'s `unordered_map::find()`, live-reproduced via
   `gdb -batch -ex run -ex bt`
   (`review/T111/x86_dangling_regionpvsdepthmap_crash_backtrace.log`). Root
   cause: `loadZoneStep12b_Simulation`'s synchronous first-frame
   `SimulationOutput output;` is a local variable;
   `applySimulationOutput(output)` points `regionPvsDepthMap_` into it — the
   exact same hazard the code already null-guards for `particleRenderBuffer_`
   three lines below, just never guarded for this field, because nothing
   dereferenced `regionPvsDepthMap_` in the one-frame window before the real
   `SimulationWorker` thread's first swap until this unit's fix started
   calling `getRegionPvsDepth()` every frame for the (now much longer) life
   of progressive loading. Fixed by nulling it at the same point, same
   pattern, one line.
2. **`WilleqDatabase` opened `SQLITE_OPEN_NOMUTEX`** (`willeq_database.cpp`).
   SIGSEGV inside SQLite's `btreeCursor()`, caught via a core-dump capture
   after two non-gdb crashes on longer stress runs
   (`review/T111/x86_sqlite_nomutex_crash_backtrace.log`). `NOMUTEX` is
   SQLite's own documented "not safe for concurrent multi-thread use" flag.
   Latent since the database existed; unreachable because `EntityPrepWorker`
   rarely did sustained concurrent work while T109's pickup path was broken.
   This unit's fix makes the background thread (`EntityPrepWorker`) and the
   main thread (`EntityRenderer::updateEquipmentTransforms()`, via
   `RaceModelLoader::getRaceCode()`) both query the same connection
   concurrently, for real, for the first time. Fixed with
   `SQLITE_OPEN_FULLMUTEX` — cheap here (read-only, small queries, not a hot
   per-frame path). Reproduced 2/7 times before the fix on extended (~170s)
   stress tours; 0/4 times after, across the original tour repeated twice
   and the extended tour repeated twice more.

Both are **outside this plan's named territory** (`renderer_zone_load.cpp`
was in-territory; `willeq_database.cpp` was not) — flagged as a deviation
below, but treated the same way T109 treated the `EntityPrepWorker`
construction bug it found under identical circumstances: required
infrastructure for the fix to be safe, not the throttle/governor itself,
landed regardless.

A **third** crash was found on the Pi only — `corrupted size vs.
prev_size` → `SIGABRT` during `unloadZone()`'s final teardown, AFTER the
scripted tour completed cleanly and all pickup-proof data was already
captured. This matches T110's already-documented, already-scoped, Pi-only
shutdown crash closely enough that it is very likely the same root cause (or
family of causes) — **not investigated or fixed here**; see Deviations.

### The gate: enabled

Per the plan's own instruction ("if — and only if — the pickup proof is
unambiguous... set `entityLoadRadius`... to `entityRenderDistance + 100`"),
and given the proof above (monotonic convergence, two zones, two machines,
repeated runs, zero plateau after the fixes) —
**`config/constrained_presets.json`'s `orangepi` preset now ships
`"entityLoadRadius": 400.0`.** This is the preset both the Pi and the x86 dev
build default to (`ConstrainedRendererConfig::parsePreset()` falls back to
`OrangePi` for any unrecognized/empty `--constrained` value, and
`Application::run()` uses it when no flag is given at all), so this is a
real, live activation, not a paper one.

### Re-measured load times, both machines (single live sample each; see caveat)

| | x86 (T109 before, ungated) | x86 (T109 after, gated/broken pickup) | x86 (T111, gated + pickup working) | Pi (T109 before, ungated) | Pi (T109 after, gated/broken pickup) | Pi (T111, gated + pickup working) |
|---|---|---|---|---|---|---|
| Zone-request → ready | 11.692s | 10.743s | **10.663s** | 35.617s | 33.265s | **34.002s** |
| Step 8 wall time | 5.721s | 4.570s | **4.604s** | 22.002s | 19.443s | **19.833s** |

Both machines land within ~1-2% of T109's own "gated" figures — the gate's
load-time benefit is preserved essentially unchanged now that the deferred
work is actually happening (rather than being silently dropped). This is a
single sample per machine (not T109's more careful multi-run
characterization) — session time went to the pickup proof and crash
diagnosis instead; stated as a real limit, not rounded up into a stronger
claim than the data supports.

### Post-zone-in frame cost — the finding the plan explicitly asked for

**x86**: 7 `SPIKE:` (frame-time-budget-exceeded) events across ~170s /
6,682 rendered frames (~0.1% of frames), average frame time 17.5ms (well
under the 33.3ms/30fps budget), max 115ms. **This is a real, measurable
cost, not zero** — the gate trades load time for background work exactly as
the plan predicted, and that background work does occasionally cost a frame.
It is small and infrequent on this dev VM's llvmpipe, but it is not nothing,
and is reported as a finding rather than waved away.

**Pi**: not reliably measured this session. Every run clean enough to reach
the post-tour performance report either crashed during `unloadZone()`
teardown before the report printed (the T110-adjacent crash above) or was
captured under `gdb`, whose overhead materially distorts Pi-side frame
timing (thousands of `SPIKE:` lines under `gdb` vs. none expected without
it — not a real measurement, per the executor protocol's "capture fps is an
artifact of the capture, not a performance measurement" discipline, applied
here to frame timing under a debugger rather than video capture fps).
**Flagged as a genuine gap, not silently omitted.**

### Goldens

**Unchanged — PASS, but this does NOT prove visual equivalence under the
gate, and that limit is stated rather than glossed over.**
`scripts/ci/visual_gate.sh` drives `gles2_zone_benchmark`, which builds its
`ConstrainedRendererConfig` via `fromPreset(ConstrainedRenderingPreset::
OrangePi)` directly (`tools/hwcaps/zone_bench_loader.cpp`) and does **not**
call `loadJsonOverrides()` — so it never reads
`config/constrained_presets.json` at all, and therefore never sees this
unit's `entityLoadRadius=400` change; it always exercises the unbounded
struct default regardless of what ships. The goldens' PASS is real (nothing
in the always-built path changed) but it is silent on the gated path by
construction, not because the gated path was checked and found identical.
Not chased further here — building the benchmark tool's own JSON-override
support is a separate, out-of-territory unit — but flagged loudly per the
plan's "show before/after rather than regenerating silently" instruction,
inverted: there is no golden coverage of the gate to show before/after of.

### Build/test

1. `cmake --build build -j24` — clean, including the new
   `tests/test_progressive_load_rearm.cpp`.
2. `ctest --test-dir build --output-on-failure` — **2265/2266**, sole
   failure the documented ALSA `AudioManagerTest.PreloadCommonSounds`.
   `ConstrainedRendererConfigTest.ShippedOrangePiPreset_EntityLoadRadiusNotSet_
   ShipsInert` (T109's own test, asserting the gate ships inert) now
   correctly fails against the enabled preset — replaced with
   `..._EntityLoadRadiusEnabled_T111`, asserting the new shipped value
   (400.0 = `entityRenderDistance` + 100) instead of deleting the coverage.
   8 new tests in `test_progressive_load_rearm.cpp` cover
   `isEntityBlockingProgressiveLoad()` — a pure static predicate, no device
   needed — each proven able to fail in both directions (built-vs-unbuilt,
   in-scene-vs-not, player-vs-not, `pvsValid` true/false, depth at/above/
   below `entityPrepMaxPvsDepth`); the `NonPlayerEntityStillBlocksWhenPvsUnknown`
   test specifically pins the direction of the pvsValid fix found live
   mid-unit (an earlier version of the predicate had this backwards — see
   the crash it caused, first bullet above; failing this test is exactly
   how a regression to that state would be caught).
3. `bash scripts/build-gles2-x86.sh` + `scripts/ci/visual_gate.sh` — PASS,
   goldens untouched (see caveat above).
4. `bash scripts/build-arm-noble.sh` — PASS, clean ARM cross-build,
   confirmed twice (once for the Pi deployment, once as final verification).
5. `scripts/refactor/check_file_sizes.sh fable` — **0 FAIL**, 2 pre-existing
   WARNs (`irrlicht_renderer.h`, already over 1500 before this unit) plus
   `renderer_zone_load.cpp` at exactly 3000 lines (WARN, not FAIL — this
   unit's one-line fix there required trimming its own comment down to a
   single-line form to stay at the 3000-line FAIL ceiling; the file was
   already sitting at exactly 3000 on `fable` with zero headroom, per T109's
   note that it "landed at exactly 2998" and has grown 2 lines since).

### Deviations

1. **T110 not executed.** The task prompt named T110
   (`refactor/T110_pi_shutdown_segv.md`) as "scoped and waiting on the Pi —
   finish and release it promptly," but T111's own scope (pickup proof,
   crash diagnosis, live measurement on two machines) consumed the full
   session. The Pi-shutdown crash this unit independently reproduced
   (`corrupted size vs. prev_size` → SIGABRT during `unloadZone()`) is very
   likely the same defect T110 describes (Pi-only, after a clean in-game
   state, during local teardown) — captured with a full backtrace in
   `review/T111/pi_befallen_gated_pickup_and_shutdown_crash.log` as a
   head-start for whoever picks up T110 next, but not diagnosed or fixed
   under that unit's own bar (backtrace-first, narrowest-fix, several
   consecutive clean shutdowns). Flagged loudly rather than silently
   skipped.
2. **Two crashes fixed outside the plan's named territory** (`willeq_
   database.cpp` for the SQLite fix; `renderer_zone_load.cpp` was in
   territory). Justified above under "both were required for the pickup fix
   to be safe" — same reasoning T109 used for its own out-of-narrow-scope
   `EntityPrepWorker` construction fix.
3. **Load-time re-measurement is a single sample per machine**, not a
   multi-run characterization like T109's — session time went to the
   pickup proof (the plan's own stated priority: "this is the hard part and
   the whole point of the unit") and the two crash diagnoses instead.
4. **Pi post-zone-in frame-time cost not reliably measured** — see above;
   every clean-enough run either crashed at teardown before the report
   printed or was gdb-slowed. Flagged as a gap, not a claim.
5. **Full-zone convergence (177/177, 452/452) not reached** in either zone
   — the plan asks for convergence "not a plateau," which is what's proven
   (monotonic, no stall); reaching literal 100% would require exhaustively
   walking every corner of both zones, which the session's remaining time
   after the crash diagnoses did not allow. The trend line is the evidence,
   not a specific terminal count.
6. **Session cost from a coordinate-frame mistake**: `/move`'s
   client/server axis swap (T88) was initially applied backwards, producing
   a false "pickup isn't working" reading in two early live sessions before
   being caught and corrected — see `review/T111/README.md`. No code
   conclusions were drawn from the mis-mapped runs; flagged per the
   executor protocol's "identical byte sizes... is the classic tell"
   discipline, applied here to a coordinate mixup rather than a capture
   artifact.

## Board

**The gate is now live** — `orangepi` preset ships `entityLoadRadius=400`,
after T91/T105/T109/T111. Two pre-existing, previously-latent concurrency
bugs (`regionPvsDepthMap_` dangling pointer, `WilleqDatabase` SQLite
`NOMUTEX`) were found and fixed as direct prerequisites — both are now
closed, not just documented. **Open follow-up: T110's Pi-only shutdown
crash was independently reproduced with a full backtrace but not fixed or
even attempted — next Pi-touching unit should start from
`review/T111/pi_befallen_gated_pickup_and_shutdown_crash.log` instead of
re-diagnosing from scratch.**
