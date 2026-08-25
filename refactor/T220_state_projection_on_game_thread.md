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

## Review

Executed in `/mnt/projects/fable/wt/slot1`, branch
`task/T220-game-thread-projection`, base fable tip `b9a888e1`. eqemu owned
for the whole unit (lock taken before `up.sh qeynos2`, deleted after
`down.sh`; Testchar restored to zone 36 / -77.9232, 21.9086, 3 / h190).
Evidence: `/mnt/projects/fable/review/T220/` (README there indexes every
file). STOP condition NOT hit -- the split preserves renderer order and the
zone-unload handoff with no lock on GameState; see "Design" below.

### Headline

The render-thread projection race is real, TSAN-caught, and gone. BEFORE:
`CombatState::setTarget()` written on the main/render thread
(`GameStateSyncBridge::applyEvent <- ProcessBridgeEvents <- PreRenderTick <-
Application::render`) vs `CombatState::hasTarget()` read on the game thread
(`cmdHail <- updateScript <- processInput <- gameThreadLoop`), same 2 bytes,
no lock either side -- `review/T220/tsan_before_GAMESTATE_RACE.txt`. AFTER:
two runs (the plan's tour and a dense 111-command tour), zero reports
naming any `state::` symbol; the only remaining reports are T114's
inventory #10 (`RendererEventReceiver` read-and-reset flags), 17 per run,
unchanged before/after, out of scope and listed. Suite 2770/2771 (ALSA
only), visual gate PASS (rerun -- first run tripped only the befallen p95
CPU-ms timing ceiling, same flake as T219), ARM build OK, goldens untouched.

### Design chosen for the split, and why

`GameStateSyncBridge` keeps `applyEvent()` (project + forward on the calling
thread) for the headless path and the unit tests, and gains two entry
points plus a second swap-vector FIFO (`m_forwardMutex`/`m_forwardQueue`):

- `projectPendingEvents()` -- GAME thread. `drainEvents()` (the base class's
  D24-coalescing drain of the queue EverQuest, the loading thread and the
  renderer's own SystemMessage pushes all feed), run the projection switch
  (now the private `projectEvent()` -- the switch body itself is untouched,
  only its enclosing function name changed), append the batch to
  `m_forwardQueue`.
- `forwardProjectedEvents()` -- RENDER thread. Swap out `m_forwardQueue`,
  re-run `coalesceEvents()` (made `protected` in `GameStateBridge`; several
  game-thread batches can accumulate between rendered frames and the
  renderer's D24 contract is at most one EntityMoved per entity per drain),
  `m_wrapped.applyEvent()` each. Never touches `m_state`.

`Application` -- the owner of both threads and of the concrete decorator --
drives the two stages: `updateGameState()` calls `projectPendingEvents()`
in graphics mode (the existing headless branch is untouched and now
`else`-chained), and `render()`'s normal-gameplay branch calls
`forwardProjectedEvents()` where it used to call `PreRenderTick()`.
`EverQuest::PreRenderTick()` is DELETED (its only production caller was
that line; leaving it would be a live-looking, unreached drain of the
wrong thread -- the reachability pattern this project keeps finding). Its
one test caller (`tests/test_beneficial_spell_casting.cpp`, a single-
threaded live harness) now calls `ProcessBridgeEvents()`, which is exactly
what PreRenderTick did. `EverQuest` remains agnostic of the decorator:
`ProcessBridgeEvents()` is unchanged and is the headless single-stage
drain.

Why not the alternatives: overriding `drainEvents()` in the decorator
(base is non-virtual, and ~100 test call sites drain the base queue
directly); adding virtual two-stage hooks to `GameStateBridge` (every
plain bridge would carry a no-op stage for the one subclass that needs
it); or a lock on GameState (the plan's STOP -- and it would only shrink
the window, not remove the cross-thread write).

Order/handoff argument (the STOP check): both hops are single-producer
FIFOs -- `eventQueue_` keeps its existing multi-producer push order under
`eventMutex_`, and the game thread is the ONLY writer of `m_forwardQueue`,
appending whole drained batches in drain order -- so the renderer sees
exactly the sequence it saw before. Nothing about zone loading changed:
`loadZoneGraphicsOnThread()` still calls `unloadZone()` synchronously on
the loading thread, and while `render()` is on an early-return branch
(handshake / loading-thread-active) nothing forwards, exactly as nothing
drained before (P17's comment updated to name the new function). The one
timing change is that the projection now lands as soon as the game thread
ticks instead of at the next rendered frame -- strictly earlier, and on
the thread that reads it; during a zone transition that means
`ZoneUnloading` clears entities/doors in the read model immediately rather
than after the new zone finishes loading, which is more correct, not less.

### TSAN -- how it was made to show the race (and why the first run did not)

TSAN tree: `slot1/build-tsan`, T114's config unchanged. Runs via
`review/T220/run_tsan_x86.sh` (ASLR off with `setarch -R`, Mesa-only
suppressions, `--log-level=INFO`).

The plan's tour (`/afk` x4, `/door` x3, `/target` x2 with 6-8 s spacing)
produced 17 warnings and NONE on GameState -- reported honestly, then
diagnosed rather than accepted. Two structural reasons: (1) TSAN is
per-address: `/target` reads entity NAMES while the render thread writes
entity POSITIONS (EntityMoved), `/door` reads door x/y/z while the render
thread writes `isOpen`; different bytes, no report possible. (2) TSAN is
happens-before based: between a render-thread write and a game-thread read
seconds later, the two threads exchange dozens of lock acquire/release
pairs (`eventMutex_`/`intentMutex_` every tick, the log-drain thread's
per-buffer mutexes), and any such chain orders the pair. With 9 reads in
two minutes, every read happened to be ordered against the last write.
So the second BEFORE tour targeted a same-address pair with the reads
dense: `/target Guard` (game thread publishes TargetChanged; render thread
applies `setTarget()` -> writes `m_targetId`) followed 0.2 s later by
`/hail` (game thread reads `hasTarget()` -> `m_targetId`), 40 times. That
caught it once -- enough; the report's two stacks are the exact pair the
plan names. The same dense tour AFTER: zero. Both AFTER runs also
re-executed every command with `outcome=applied` and shut down cleanly.

### Verification bar

| Step | Result |
|---|---|
| 1. `cmake --build slot1/build -j24` | exit 0 (`build_full.log`) |
| 2. ctest | **2770/2771**; sole failure `AudioManagerTest.PreloadCommonSounds` (ALSA, documented) (`ctest_full.txt`) |
| 3. `build-gles2-x86.sh` | exit 0 |
| 4. Visual gate | first run FAIL (befallen p95 CPU ms 48.0 > 40.1 ceiling -- timing only, no pixel diff), rerun **PASS**; goldens untouched |
| 5. `build-arm-noble.sh` | exit 0, `build-arm-noble/bin/willeq` rebuilt |
| 6. `check_file_sizes.sh fable` | 8 files checked: 4 ok, 4 WARN (pre-existing oversize eq.h/application.cpp/eq_bridge.cpp/test_game_state_sync_bridge.cpp), 0 FAIL |
| Unit test | 6 new `GameStateSyncBridgeSplitDrainTest` cases: project stage mutates state and forwards nothing; forward stage delivers each event once in push order and is destructive; forward stage never touches state; order across several project batches before one forward; D24 coalescing holds renderer-side across batches; single-stage `applyEvent()` unchanged |
| Live (graphics, normal build) | `/afk` on->off->on->off, `/door` x3 (`Clicking door 20`, renderer `Door 20 animation complete`), `/target Guard` -> `Guard_Hewet000`, `/hail`, `/screenshot`, `/camp`; exit 0 |
| Live (headless) | same tour, identical outcomes; exit 0 |

### Deviations (flagged)

- `include/client/eq.h` (not in the named territory) lost one declaration,
  `PreRenderTick`, replaced by a two-line comment -- unavoidable once the
  definition was deleted. `tests/test_beneficial_spell_casting.cpp` (tests
  are in territory) changed two call sites accordingly.
- `include/client/bridge/CLAUDE.md` gained a "Two-stage drain" section.
  The root `CLAUDE.md` threading table still says "Drain bridge events ->
  applyEvent()" under the render thread -- left alone (out of territory);
  worth a one-line touch at merge.
- `GameStateBridge::coalesceEvents` went private -> protected (in
  territory; needed for the renderer-side re-coalesce).
- The dense TSAN tour (`/target` + `/hail` x40) was added beyond the
  plan's command list because the plan's own tour could not surface the
  race; both tours' reports are archived.
