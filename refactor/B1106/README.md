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

All raw evidence: `/home/user/kb-evidence/kb-b1106-impact-dropped/`
(`analysis_prefix.md`, `analysis_postfix.md`, both client logs, both frame
sets, the three post-fix sheets `lane1_postfix/spell_{93,54,253}_sheet.png`).

**(A) Deciding trace — the drop, at the worker level (lane 1, same rig both
runs, surfaceless llvmpipe x86, `--dump-frames-dir --dump-every 1`,
`SPELL:DEBUG,GRAPHICS:DEBUG`):**

| | PRE-FIX (`~/willeq-prefix-bin` = 3ff4bcec, origin/fable) | POST-FIX (HEAD) |
|---|---|---|
| steady-state frame delta | 117 slow-frame warnings, 19 of them > 1000 ms (all 1487-1536 ms); all 3 cast windows inside that > 1000 ms band | render loop unchanged: 66 warnings, all 1443-1735 ms (11 in the cast window) |
| what the sim worker does on those slow frames | early-return (pre-fix `sim_particles.cpp:45-49`): the drained command is **dropped** | clamped physics delta (1.0 s) + **command queue still executed** |
| player (spawn 258) "SimWorker: Created spell effect" | **0** | **6** |
| player impact + spellComplete (93/54/253) | enqueued (SpellAction) but never created | created, one pair per cast |

QA note (t_4ba84f88): the earlier "**0 frames > 1000 ms**" cell was wrong —
the fix never touches the render loop, and the render-loop's own PERF
warnings (the same metric the pre-fix cell uses) show 66 frames
1443-1735 ms post-fix, 11 of them inside the cast window. What the fix
changes is what the SIM WORKER does on those frames: the physics delta is
clamped to [0, 1] s and the command queue is still executed instead of
discarded. The rig's persistently slow frames are exactly what makes the
0 → 6 creation receipts the discriminator: post-fix the effects are created
on 1.5 s frames, whereas pre-fix a `CreateSpellEffect` is created only when
its draining frame happens to land under 1.0 s.

Post-fix receipts: `SpellComplete id=3 caster=258` + `Impact id=4 target=258`
at 07:52:57.632 (cast 93 @ 07:52:56.589), the same pair for 54 @ 07:53:03.668
and 253 @ 07:53:11.243. The only effects the PRE-FIX run created were 4
`CastSpray` for a zone-in NPC (caster=87) landing on the rare <1 s frames —
the frame-timing dependence made explicit. Hypothesis **(b) is PROVEN**;
(a) is ruled out for the render path by `test_spell_impact_visual` (worker
spawns on-target particles at normal deltas) + T386/T395.

**(B) Fix** — `sim_particles.cpp:45-59`: the `deltaTime` guard now clamps the
PHYSICS delta to [0,1] and falls through to the command loop instead of
returning. One function touched; the command queue it carries is executed.

**(C) Device-free test** `SlowFrameCommandTest` (3 cases;
`tests/test_b1106_slow_frame_commands.cpp`, real `SpellVisualFX` +
`ParticleManager` + `SimulationWorker`, the `test_caster_own_landing` seam):
- RED pre-fix (origin/fable): `test_prefix_red.md` —
  `Expected LiveEffectEntities().count(kTarget) == 1, is: 0` for the
  deltaTime=2.0f pump.
- GREEN post-fix: `test_postfix_green.log` — `100% tests passed, 0 tests
  failed out of 3` (Impact/SpellComplete slow-frame + zero-delta case).

**(D) After-sheets** attached: `spell_93/54/253_sheet.png` (post-fix, 4 frames
per cast). Note the rig's own capture cadence is ~1.5 s (glReadPixels+PNG
encode on surfaceless llvmpipe; B1104 measured the same), so the sheets
sample at -0.47 s / +1.04 s / +2.56 s around each cast and do not land inside
the 1.6-2.0 s effect's peak window — they are evidence that the world is
stable and unchanged, NOT the discriminator; the worker creation receipts
above are. A burst captured at this cadence is equally "invisible" pre-fix
and post-fix.

**(E) No golden/preset/manifest/`spell_effects.json` changes** —
`git diff origin/fable..HEAD --stat`: `sim_particles.cpp`,
`tests/test_b1106_slow_frame_commands.cpp`, `tests/cmake/spells.cmake`,
`refactor/B1106/README.md` only. No temporary counter was added (the existing
`SimWorker: Created spell effect` DEBUG receipt at `sim_particles.cpp:210`
served as the drop/created discriminator).

**(castGlow, per the operator's mid-run note):** `#castspell N` is an instant
cast, so no BeginCast event arms the player's castGlow stage in these runs
(it fires only for the zone-in NPC, and created fine in both). The player's
instant casts ride impact (effect_flag 0) + spellComplete (effect_flag 4),
both on the SAME `particleInput.commands` queue the castGlow command joins —
so B1105's dropped castGlow is the same mechanism and this fix covers it.

**(F) Regression net:** `SlowFrameCommandTest` 3/3 pass (above); `hygiene`
label passes 45/48 — the 3 failures are pre-existing rbuild-mirror
environment gaps (`generate_textures` / `willeq_db_builder` / pack not built
in the mirror tree), not touched by this change and not on the changed TUs.
The graphics-gated sim/particle nets (`test_spell_impact_visual`,
`test_caster_own_landing`, `test_sim_bridge_component`, ...) register as
`NOT_BUILT` in the mirror's tree and will run on CI's `EQT_GRAPHICS=ON`
build (acceptance F's pipeline is the authority).
