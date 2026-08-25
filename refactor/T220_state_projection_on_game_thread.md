# T220 — Option A stage 1: apply state projections on the game thread, prove the race is gone

Slot: slot1. x86 only. OWNS eqemu (one-client live run is enough; no second
client needed). Decision: docs/decisions.md 2026-08-25 GameState projection
entry; Will approved A1 (2026-08-25): stage 1 = this unit, stage 2 = fold the
transforming projections into source-side writes, stage 3 = swap the six
references and delete Application::m_gameState. THIS UNIT IS STAGE 1 ONLY.

## The race (verified 2026-08-25 at 3f5eaf30)
Graphics build: `render()` -> `EverQuest::PreRenderTick` (application.cpp:1424,
eq_bridge.cpp:295 "runs on render thread") -> `ProcessBridgeEvents()`
(eq_bridge.cpp:1437) -> `GameStateSyncBridge::applyEvent` MUTATES
`Application::m_gameState` on the RENDER thread. Meanwhile
`gameThreadLoop()` -> `processInput` -> InputActionBridge / ActionDispatcher /
CommandProcessor READ the same object on the GAME thread. No lock on any
state object (include/client/state/ has only the EventBus subscriber mutex).
Headless is clean: it drains from `updateGameState()` (application.cpp:1384).

## Constraint that shapes the fix
`GameStateSyncBridge` is a DECORATOR: after projecting into GameState it
forwards every event to `m_wrapped` (IrrlichtBridge in graphics builds),
which MUST keep running on the render thread. So the drain cannot simply
move — it must SPLIT:
- GAME thread: drain the EverQuest->bridge event queue, apply the state
  projection (GameStateSyncBridge's switch), then hand the same events to a
  render-side queue.
- RENDER thread (PreRenderTick): drain that render-side queue into
  IrrlichtBridge::applyEvent exactly as today.
Preserve the D24 per-spawnId EntityMoved coalescing (game_state_bridge.h:73-81)
for the renderer side; do not change event ORDER as seen by the renderer.
Zone-load/unload ordering: read application.cpp:1576's comment (CleanupZone
events applied only by PreRenderTick) and eq.cpp:3766-3789 before touching
anything; the zone-unload handoff must still be renderer-safe.

## Scope
1. Implement the split above with the smallest change to the bridge
   classes (a second queue, or a "projected" flag + two drain entry points).
   Headless path stays a single game-thread drain (ConsoleBridge is
   game-thread already).
2. TSAN: CMakeLists.txt:716 has the TSAN/ASAN invocation used by T114; read
   refactor/T114_worker_render_race_audit.md Part 1 for how TSAN + llvmpipe
   zone-in was made to work. BEFORE the fix: build TSAN, run a scripted
   zone-in + a few `/afk`, `/door`, `/target` commands (they read
   Application::m_gameState on the game thread) and CAPTURE the report
   showing the GameState write/read race (this is the defect evidence — if
   TSAN does NOT report it, say so and explain; do not fabricate). AFTER the
   fix: same script, zero reports involving `state::` objects. Other
   pre-existing TSAN reports (T114 listed several) are out of scope — list
   them, don't fix them.
3. Unit test: whatever new drain entry point exists gets a test in
   tests/test_game_state_sync_bridge.cpp or a bridge test proving events are
   projected once and forwarded once, in order.
4. Live (non-TSAN, normal build): zone in, `/afk` on/off, `/door`, `/target`
   — all still work (log lines), no regression. Visual gate PASS.

## Territory
include/client/bridge/*.h, src/client/bridge/*.cpp, src/client/eq_bridge.cpp
(ProcessBridgeEvents/PreRenderTick), src/client/application.cpp (the two
drain call sites only), tests. Do NOT touch GameStateSyncBridge's switch
cases, action_dispatcher.cpp, command_processor.cpp, or any eq_*.cpp state
write.

## STOP
If the split cannot preserve renderer event order or the zone-unload
handoff without a lock on GameState, STOP and report the exact conflict —
that is a strategy question for Will, not an executor call.
