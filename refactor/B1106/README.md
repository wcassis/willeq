# B1106 (#426): impact/spellComplete armed on the OP_Action yet nothing renders

Class A. PROVE-first card: settle **created-but-invisible (a)** vs
**dropped by the sim worker's deltaTime>1s early-return (b)** for the
Target stage of projectile spells (93/38/54/656), then fix the proven one.
Source: B1104 (#424) MR !268 `refactor/B1104/README.md` §6-7 F2.

## 1. What B1104 proved (quoted, `refactor/B1104/README.md` on
`task/kb-b1104-spell-effects-oracle`)

- "fable **received** all 7 self-cast `OP_Action` packets ... caster ==
  target == the local spawn 257. So this is NOT a network or server-shape
  problem."
- "Fable DOES receive the cast-ANIMATION `OP_Action` (effect_flag 0) for the
  self-cast, and the bridge DOES arm `createSpellComplete` + `createImpact` on
  exactly that packet (`irrlicht_bridge.cpp:1137-1139`, T395). So the packet
  is there and the call site exists — yet the frames show no impact burst.
  The particle command is subject to the sim worker's frame-time early-return
  (`sim_particles.cpp:45-49`: `if (deltaTime <= 0.0f || deltaTime > 1.0f)
  return;` before the command loop), which drops `CreateSpellEffect` on slow
  frames."

The arm: `src/client/bridge/irrlicht_bridge.cpp:1137-1139`
(`d.isCastAnimation` → `createSpellComplete` + `createImpact`),
`spell_packet_handlers.cpp:180` (`isCastAnimation = (action.effect_flag == 0)`).
The calls are `SpellVisualFX::createImpact` / `createSpellComplete`
(`spell_visual_fx.cpp:604/700`), which in GLES2 with the particle manager
enqueue a `ParticleCommand::CreateSpellEffect` via
`ParticleManager::createSpellEffect()` (`particle_manager.cpp:958-983`) —
`LOG_DEBUG(MOD_GRAPHICS, "ParticleManager: Enqueued CreateSpellEffect ...")`
at `:979` is the existing queue-side receipt.

## 2. The command path and where a command can die

```
game thread            render thread (processFrame, renderer_frame.cpp)
SpellAction OP_Action  applySimulationResults()   (frame N+1, :142)
  -> SpellCastVisual     processFrameVisibility()
     Complete (isCast      postSimulationInput(deltaTime)   (:151)
     Animation) ->        -> pi.commands = particleManager->drainCommands()
  irrlicht_bridge         (renderer_sim_bridge.cpp:447)
  :1137-1139 ->         worker thread
  createImpact/         computeParticles(input, output)   sim_particles.cpp:21
  createSpellComplete    :45  if (deltaTime<=0 || >1.0) return;  <-- DROPS
  -> enqueue            :52  command loop  (CreateSpellEffect arm :172)
  (particle_manager     :311 spawn phase  :426 physics  :541 lifecycle
   .cpp:977 pending_)   :689 build renderBuffer
```

Every `ParticleCommand` queued on the render thread is drained into the NEXT
`postSimulationInput()`, i.e. it rides that frame's `deltaTime`.
`renderer_frame.cpp:84-86` only WARNS above 100 ms — there is no cap on the
deltaTime the render loop passes down (`irrlicht_renderer.cpp:841`,
`frameTimeMs / 1000.0f`).

## 3. The hypothesis table (written BEFORE the runs)

| # | hypothesis | mechanism (file:line) | how it would show in a trace | how to settle |
|---|---|---|---|---|
| (a) | created-but-invisible: the effect IS created but nothing draws | position/size/camera/alpha/draw-order defect somewhere on the Target path (`spell_visual_fx.cpp:604`, the Impact preset, `UnifiedParticleRenderer`) | worker trace shows `SimWorker: Created spell effect` + live particles in the render buffer; dumped frames still blank | pixel census on dumped frames + worker-side particle count; if the worker had live particles and the frame is blank, the defect is render-side and gets named at file:line |
| (b) | dropped: the `CreateSpellEffect` command reached `pendingCommands_`, was drained into a frame whose `deltaTime > 1.0f`, and the early-return at `sim_particles.cpp:45-49` skipped the command loop at `:52` — the command is discarded, never deferred | `sim_particles.cpp:45` | `Enqueued CreateSpellEffect` (GRAPHICS DEBUG) with NO following `SimWorker: Created spell effect` on the same frame; the frame's deltaTime is the smoking gun; live particles = 0 afterwards | device-free test ticking the worker with deltaTime 2.0 then 0.016 + the live trace |

**Prior for this run: (b), for a measured reason.** B1104's fable capture was
the `--dump-frames-dir --dump-every 2` surfaceless-llvmpipe run. The 119
frame PNGs' own mtimes (B1104 evidence dir) show **110 of 118 gaps > 0.5 s,
56 of 118 > 1.0 s, max 14.0 s** (median 1.0 s) — i.e. in that rig almost
every post-cast frame had `deltaTime > 1.0f`, and the command queue is
drained on the frame AFTER the cast, which in that run was almost
certainly a >1 s frame. (a) has no such rig-specific reason: the same Target
stage rendered fine for observer clients in T386's live run after T395's
origin-burst fix (the test `test_spell_impact_visual.cpp` proves the worker
spawns on-target particles with normal 1/60 s deltas). PROVE FIRST: the
device-free test and the live trace decide; if the trace says (a), this card
fixes (a) and not the cheap one.

The B1104 client log names the frame times directly. `renderer_frame.cpp:85`
warns on every frame slower than 100 ms, and `fable_x86/fable_x86.log`
carries **89 such warnings, 88 of them 1462-1544 ms** (the run's steady
state: the script-paced tour's 1 s waits land mid-frame on this rig, so the
render loop's real delta was ~1.47 s on essentially every frame). The cast
93 packet is in the log at 05:32:00.025 (the `SpellAction` DEBUG receipt),
and the frames bracketing it (frame_000051 at 05:31:59.595,
frame_000052 at 05:32:01.110) show the ~1.5 s cadence around the exact
moment the `CreateSpellEffect` command was drained into the next
`postSimulationInput()`. A delta of ~1.47 s is inside the
`deltaTime > 1.0f` band — the one the early-return fires on.

## 4. Plan

1. This README (hypothesis table above) — committed before any run.
2. Device-free test (seam: `SimulationWorker::computeOnce`, the same seam
   `tests/test_spell_impact_visual.cpp` uses): enqueue the exact command
   `ParticleManager::createSpellEffect()` produces for
   `SpellVisualFX::createImpact()`, tick with deltaTime **2.0** then
   **0.016**, assert the effect exists and particles spawned. Must FAIL
   pre-fix (the 2.0 s tick is the early-return, which drops the command).
3. Fix: `sim_particles.cpp` — the early-return must not DROP commands:
   the command loop still runs; only the physics step uses a clamped
   deltaTime when the frame delta is out of the (0, 1] band.
4. LIVE, lane 1, B1104 recipe (x86 client on the build host, same tour):
   `--log-level=DEBUG --log-module=MOD_SPELL:DEBUG --log-module=MOD_GRAPHICS:DEBUG`
   + one temporary counter naming the dropped-command drop (removed before
   the MR). Per cast of 93 and 54: enqueue receipt → worker receipt (or not)
   → the frame's deltaTime → live particles. Then a fixed-run after-sheet
   with `--dump-frames-dir` for 93 and 54. World restored DOWN, lock
   released.

## 5. Acceptance mapping

- (A) §3 table + the deciding trace, quoted in §6.
- (B) fix matches the proven mechanism.
- (C) test fails pre-fix (log quoted), passes post-fix.
- (D) after-sheets for 93 + 54 attached.
- (E) no golden/preset/manifest/`spell_effects.json` changes; the temporary
  counter is removed in a dedicated commit.
- (F) final pipeline green.

## 6. Evidence (filled in as the unit runs)
