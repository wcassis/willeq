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

(executor fills in: backtrace, the proven interleaving, fix shape
chosen + why, survival evidence, deviations)
