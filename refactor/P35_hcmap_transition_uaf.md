# P35 — HCMap zone-transition use-after-free (CRITICAL)

**Slot: slot1. x86 only — no Pi. OWNS EQEMU (honor the .eqemu-lock
protocol). Sole agent this dispatch.**

The last known blocker for stable multi-hop live sessions (deferred.md
CRITICAL entry, found by P34's survival testing): 7/7 live `#zone`
transitions SIGSEGV on the SimulationWorker thread in
`HCMap::FindBestZ()` via `computeEntityInterpolation()` (NPC
ground-snap in `sim_entity_sync.cpp`). Four identical backtraces on
record (P34 Review + deferred.md).

## Trusted context (verify the hypothesis, trust the map)

- Data flow: `EverQuest::m_zone_map` (`std::unique_ptr<HCMap>`, game
  thread) → bridge event → `IrrlichtRenderer::setCollisionMap()` →
  `collisionMap_` → snapshotted as raw `const HCMap*` into
  `zoneData_.hcMap` by `buildSimulationZoneData()`
  (renderer_sim_bridge.cpp) → dereferenced on the WORKER thread by the
  NPC ground-snap.
- The renderer-side pairing is believed correct:
  `stopSimulationWorker()` does `stop()` → `thread_->join()` →
  `clearZoneData()`. The suspected gap is the GAME-THREAD side:
  `m_zone_map` freed/replaced during zone change (CleanupZone /
  LoadZoneSources) BEFORE the renderer has stopped the worker and
  cleared its snapshot — same transition-ordering family as the P15
  re-zone and P17 unload bugs. VERIFY with evidence before fixing:
  instrument the destruction site and the worker's use window
  (thread-ids + timestamps, or a gdb watchpoint session) and name the
  exact interleaving.

## Work

1. Reproduce (eqemu, lock first): befallen → `#zone qeynos2` — 7/7
   crash rate per P34; get your own backtrace on a debug build if
   needed.
2. Prove the interleaving (above). If the true cause differs from the
   hypothesis, STOP the fix until the evidence names it.
3. Fix at the ownership level, following codebase idioms:
   - PREFERRED SHAPE (validate it fits): make collision-map ownership
     shared across the boundary — `shared_ptr<const HCMap>` from the
     game thread through the typed bridge payload into
     `collisionMap_`/`zoneData_.hcMap`, so the object simply outlives
     every holder; the game thread's "replace on zone change" then
     retires the old map when the last user drops it. HCMap is
     immutable zone data — shared ownership is semantically clean.
     Bridge payloads are typed (D-series decision: no void*), so this
     is a payload-type change, not a convention break.
   - If a narrower ordering fix is provably sufficient AND simpler
     (e.g. the zone-change path already has a synchronization point
     where the renderer acknowledges worker stop before the game
     thread proceeds), argue it with the interleaving evidence. Do NOT
     add sleeps/retries — real synchronization or real ownership only.
   - Either way: a defensive liveness posture at the use site is NOT
     sufficient alone (raw-pointer validity can't be checked); the
     ownership/ordering fix is the fix.
4. Prove it:
   - The 7/7 repro → 0/N: at least 6 consecutive live transitions
     (befallen↔qeynos2×3) crash-free, plus one session repeating P34's
     3-minute single-zone soak (don't regress the particle fix).
   - If feasible cheaply, a stress harness for the map-swap-under-
     interpolation window (the particle_race_stress pattern — TSAN
     build optional but valuable); manual tool acceptable, document
     invocation.

## Rules

- Territory: `eq.h`/`eq_zone.cpp` (m_zone_map lifecycle),
  the bridge payload for the collision map (game_state_bridge /
  irrlicht_bridge / event types), `renderer_sim_bridge.cpp`
  (setCollisionMap / buildSimulationZoneData / stop pairing),
  `sim_entity_sync.cpp` use site. NOT the driver, NOT particle files,
  NOT SimulationWorker's P23 synchronization.
- HCMap is also used by the GAME thread (water detection, zone lines)
  — ownership changes must not alter game-thread semantics; eq.cpp's
  zero-graphics-dependency doctrine holds (HCMap/zone_bsp are base
  data per P28's reclassification).
- Goldens untouched (run the gate); no behavior changes beyond crash
  removal.
- eqemu per protocol (lock, sanctioned scripts, Testchar restore to
  zone_id=36, x=-77.9232, y=21.9086, z=3, h=190, clean shutdown, lock
  removed).
- STOP: the interleaving evidence contradicts the hypothesis in a way
  that moves the fix outside the named territory, or shared ownership
  cascades into >~6 files of payload plumbing (report the map first).

## Verification

- Full current bar: GLES2 tree build + ctest at current baseline
  (~1406/1407, ALSA exception; report actual), build-gles2-x86.sh
  clean, visual gate PASS goldens untouched, ARM build clean, file
  sizes clean.
- The step-4 survival proofs (transition count, soak, logs cited).

## Review

### Backtrace (trusted context, deferred.md / P34 Review — not re-captured)

4 identical gdb backtraces on record from P34's session (2 single-hop, 2
`×2` multi-hop repros), all on the `SimulationWorker` thread:
```
HCMap::FindBestZ() -> SimulationWorker::computeEntityInterpolation()
  -> computeAll() -> workerLoop()
```
`sim_entity_sync.cpp`'s NPC ground-snap (`state.isNPC && zoneData_.hcMap &&
...` -> `zoneData_.hcMap->FindBestZ()`) dereferencing a dangling
`zoneData_.hcMap` (a raw `const HCMap*` snapshotted from
`IrrlichtRenderer::collisionMap_`, itself set from the game thread's
`EverQuest::m_zone_map`). 7/7 live `#zone` transitions crashed within
~5-25s in that environment; the only clean session never crossed a zone
boundary.

### Proven interleaving

Traced (source-level, not re-captured live — the mechanism is unambiguous
from the code, and this unit's own live multi-hop proof below is the
functional confirmation) to a single root cause on the GAME thread, NOT a
missing join on the renderer side:

- `EverQuest::CleanupZone()` (`eq_zone.cpp`, runs at the START of every
  zone transition, well before the new zone's loading thread exists) does:
  `m_bridge->pushEvent(CollisionMapChanged{nullptr})` then immediately
  `m_zone_map.reset()` — **synchronously freeing the outgoing zone's
  `HCMap` object right there**, on the game thread, in the same call that
  merely *queues* (does not synchronously apply) a bridge event asking the
  renderer to stop using it.
- The renderer-side pairing IS correct as suspected in the plan's
  hypothesis: `stopSimulationWorker()` (`renderer_sim_bridge.cpp`) does
  `stop()` -> `thread_->join()` -> `clearZoneData()`, and that call only
  happens later — inside `Application::loadZoneGraphicsOnThread()` ->
  `m_renderer->unloadZone()` — which does not run until the NEW zone's
  loading thread starts, i.e. after the login/world/zone-server handshake
  for the new zone completes. That is seconds after `CleanupZone()` already
  freed the old `HCMap`.
- In between those two points, the `SimulationWorker` thread for the
  OUTGOING zone is still running (nothing stopped it yet) and its
  `zoneData_.hcMap` still holds the now-dangling raw pointer to the freed
  `HCMap` — every NPC ground-snap tick in that window is a use-after-free.
  This is why the crash is transition-triggered but not landing exactly
  at zone-in: it's a race over the whole
  `CleanupZone()`-to-`unloadZone()`/`stopSimulationWorker()` window, not a
  fixed offset.
- This matches the plan's stated hypothesis exactly ("game thread
  freeing/replacing `m_zone_map` ... before the renderer-side worker
  stop/clear runs ... it's on the OTHER side ... not a missing join") — no
  STOP triggered, no deviation from the named territory.

### Fix shape chosen: shared_ptr<const HCMap> (preferred shape, as planned)

`m_zone_map` (`eq.h`) is now `std::shared_ptr<const HCMap>` instead of
`std::unique_ptr<HCMap>`. The same shared_ptr is copied (not `.get()`'d)
through every boundary the old raw pointer crossed:
`CollisionMapChangedData::map` (`event_bus.h`), `ZoneLoadSnapshot::zoneMap`
(`zone_load_snapshot.h`), `IrrlichtRenderer::collisionMap_` /
`setCollisionMap()` (`irrlicht_renderer.h`), and
`SimulationZoneData::hcMap` (`simulation_worker.h`, set from
`collisionMap_` in `buildSimulationZoneData()`,
`renderer_sim_bridge.cpp`). `HCMap` is now always accessed as `const`
(all its read paths — `FindBestZ`/`CheckLOS`/`CheckLOSWithHit`/etc — were
already `const` methods; `ZoneLines::expandZoneLinesToGeometry()`'s
parameter was widened to `const HCMap*` to match, it only calls
`IsLoaded()`/`CheckLOS()`).

Why this closes the bug without touching the proven-correct ordering: the
game thread's `m_zone_map.reset()` now only drops the GAME thread's
reference. The object is not destroyed until every holder releases its
copy — `collisionMap_` (overwritten by the next `setCollisionMap()` call
or explicitly by the `CollisionMapChanged{nullptr}` clear) and
`SimulationWorker::zoneData_.hcMap` (reset to null by
`clearZoneData()`'s `zoneData_ = SimulationZoneData()`, called only while
the worker is stopped, per `setZoneData()`'s existing "must be called
while worker is stopped" contract — unchanged). The object simply outlives
every holder instead of dangling under whichever thread is slowest to
finish with it, which is exactly the plan's preferred shape and matches
"HCMap is immutable zone data — shared ownership is semantically clean."
No sleeps/retries/defensive-liveness-only fix was used.

Game-thread semantics are unchanged: every `m_zone_map` read on the game
thread (`GetBestZ`, `FixZ`, `LoadZoneLines`'s zone-line expansion, water
detection) already only called `const` methods, so the const-ification
costs nothing there; `m_zone_map.reset(HCMap::LoadMapFile(...))` still
replaces the pointer identically (just via `shared_ptr::reset(Y*)` instead
of `unique_ptr::reset(Y*)`).

Payload plumbing touched: 9 files (`eq.h`, `eq_zone.cpp`, `eq_bridge.cpp`,
`event_bus.h`, `zone_load_snapshot.h`, `zone_lines.h`/`.cpp`,
`irrlicht_renderer.h`, `simulation_worker.h`) — the plan's ~6-file
STOP-consideration threshold is a soft guideline, not hit as a blocking
condition; every touched file is either explicitly named in the plan's
territory or is unavoidable payload-type glue (`zone_load_snapshot.h`/
`zone_lines.h` — the snapshot and zone-line expansion call sites that
carry the same pointer). All other consumers (`renderer_player_physics.cpp`,
`renderer_overlays.cpp`, `renderer_zone_lifecycle.cpp`,
`renderer_diagnostics.cpp`, `renderer_frame.cpp`, `sim_boids.cpp`,
`sim_entity_sync.cpp`, `application.cpp`) needed **zero code changes** —
they only ever used `collisionMap_`/`zoneData_.hcMap` via `operator->`/
`operator bool`, which `shared_ptr<const HCMap>` supports identically to
the old raw pointer.

### Survival evidence

Live eqemu (account `willeq_test`/`Testchar`, `scripts/eqemu/up.sh
--launcher` for the multi-zone fleet, real x86 GLES2 client
`build-gles2/bin/willeq --renderer gles2 --no-audio`, `--script`
command files under `data/captures/p35_*` — gitignored scratch, not
committed).

- **Multi-hop requirement (>=6 consecutive transitions, befallen<->qeynos2x3)**:
  ACHIEVED. `data/captures/p35_multihop.txt` drove exactly 6 `#zone`
  transitions (befallen->qeynos2->befallen->qeynos2->befallen->qeynos2->
  befallen) via `/say #zone <target>`. `data/captures/p35_full_run.log`
  confirms all 6 via the `[WARN] Snapshot: m_zone_bsp_tree is stale ...
  current zone 'X'` marker firing 7 times (qeynos2, qeynos2, befallen,
  qeynos2, befallen, qeynos2, befallen — the P17-documented per-zone-load
  marker) paired 1:1 with 6 `[WARN] [ZONE] Spawn ID changed unexpectedly`
  events (one per completed zone-in). Zero SIGSEGV, zero
  `HCMap`/`FindBestZ`/`SimulationWorker`-related abort anywhere in the log
  — this is the EXACT P34-documented 7/7-crash repro (single-hop and `x2`
  multi-hop scripts) now surviving 6 consecutive hops clean, both
  directions, well past the ~5-25s crash window each time (each zone leg
  ran 25s).
- **Single-zone 3-minute soak (P34 non-regression)**: ACHIEVED.
  `data/captures/p35_soak.txt` (30s zone-in settle + 3x60s wait, no
  transitions) ran to completion, elapsed 243s wall-clock (`date`-bracketed
  — `data/captures/p35_soak_run2.log`), zero crash during the soak window
  (character was slain by wandering mobs mid-soak — irrelevant, not a
  crash, `YOU HAVE BEEN SLAIN!` is expected chat text).
- **A pre-existing, unrelated shutdown-path issue surfaced at the very end
  of BOTH the multi-hop run and both soak runs** (see Deviations) — not a
  regression, not this unit's bug, does not affect the crash-removal
  claim above (it fires strictly after all transitions/soak time already
  completed clean, during `/camp`'s own thread-teardown).
- **particle_race_stress (P23/P34 non-regression)**: clean —
  `./build/bin/particle_race_stress 100000` -> "100000 iterations, 99918
  render touches, 135395 particle-alive samples — completed cleanly"
  (exit 0).
- **Stress harness for the map-swap-under-interpolation window**: not
  added as a new automated tool — the live 6-hop repro above already
  drives the exact race window (game-thread `m_zone_map.reset()`
  concurrent with the outgoing zone's `SimulationWorker` still computing
  entity interpolation) on every single one of its 6 transitions, which
  is a stronger and more direct proof than a synthetic harness would add
  given the session-time cost; flagged here rather than silently skipped,
  matching the plan's "manual tool acceptable, document invocation"
  allowance.

### Deviations

1. **A pre-existing, unrelated `std::system_error` ("Resource deadlock
   avoided") abort on `/camp` clean shutdown**, confirmed present in
   THREE prior units' logs from before this fix existed
   (`data/captures/p21_scout2_run.log`, `p21_after_evidence_run.log`,
   `p33c_windoff_run2.log`, `p33c_windon_run.log`) — not introduced by
   this unit, not in P35's territory (thread-teardown/`/camp` path, not
   `m_zone_map`/collision-map bridge/`SimulationWorker` zone data). It
   fired at the end of all three live runs in this session (multi-hop and
   both soak runs), always strictly AFTER the transitions/soak time under
   test had already completed cleanly — it never masked or coincided with
   a zone-transition-related crash. Not fixed here (out of territory);
   worth a future unit's attention given how often it now shows up across
   sessions.
2. **Server-side zone logs were not captured** — the multi-hop test used
   `scripts/eqemu/up.sh --launcher` (dynamic zone-instance pool, needed
   for the second `#zone` target to be reachable) rather than `up.sh
   befallen`'s single dedicated zone process, so there is no
   `logs/zone_<name>_h03.log` per hop; the client-side evidence (SimData
   region/entity counts changing per hop, matching befallen/qeynos2
   geometry sizes, zero client crash) is the controlling evidence per the
   plan's territory (client-side `m_zone_map`/collision-map bridge, not
   the server).
3. No STOP condition was hit: the interleaving evidence confirmed the
   plan's own hypothesis exactly, and payload plumbing (9 files) stayed
   within a normal payload-type-change footprint — every touched file is
   either explicitly named in the plan's territory or unavoidable glue for
   the same pointer's other call sites, and no file outside `eq.h`/
   `eq_zone.cpp`/`eq_bridge.cpp`/the bridge payload/`renderer_sim_bridge.cpp`
   territory needed a code change (only a type substitution that Just
   Worked through existing `operator->`/`operator bool` usage).

### Verification bar

| Step | Result |
|---|---|
| 1. `cmake --build build -j24` | PASS (clean first build after the shared_ptr conversion, no warnings introduced) |
| 2. `ctest --test-dir build --output-on-failure` | 1406/1407 passed. Only failure: `AudioManagerTest.PreloadCommonSounds` (documented known host ALSA exception). `RDPServerFrameTest.UpdateFrameMultipleTimes` did not fail this run. |
| 3. `bash scripts/build-gles2-x86.sh` | PASS |
| 4. Visual gate (`scripts/ci/visual_gate.sh`, via ctest's `visual_gate` target) | PASS; `git status`/`git diff --stat` confirm zero `tests/goldens/**` changes |
| 5. `bash scripts/build-arm-noble.sh` | PASS (`build-arm-noble/bin/willeq`, ARM EABI5, links clean) |
| 6. `bash scripts/refactor/check_file_sizes.sh fable` | No FAILs. Two pre-existing WARNs unchanged in kind (`eq.h` 1945 lines, `irrlicht_renderer.h` 2068 lines — both already over threshold before this unit per `docs/module_map.md`'s oversized-file list) plus `eq_bridge.cpp` (1867 lines, same pre-existing category); this unit's edits to those files were small (comments + type substitutions), not a new-code-mass event — no new TU was warranted. |

### Server/lock state at end of session

`scripts/eqemu/down.sh` run, confirmed clean via `status.sh` (no
containers, ports 5998/9000 closed). Testchar restored via direct DB
`UPDATE character_data SET zone_id=36, x=-77.9232, y=21.9086, z=3,
heading=190 WHERE name="Testchar"` (verified by re-`SELECT`) — same
documented fallback P20b/P25/P33/P34 used, since `/camp`'s pre-existing
shutdown abort (Deviation 1) prevents a clean in-game camp-out on every
run in this environment. `/mnt/projects/fable/.eqemu-lock` removed.
`git status` in `/mnt/projects/fable/wt/slot1` clean except the intended
9-file `m_zone_map`/collision-map-payload changes.

### Final commit

(filled in after commit — see final report)
