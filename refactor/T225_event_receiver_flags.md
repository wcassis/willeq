# T225 — RendererEventReceiver read-and-reset flags: the 17 residual TSAN warnings

Slot: slot2. x86. NO eqemu needed for the fix; the TSAN tour needs a zone-in —
take the eqemu lock per protocol only for that run and release it. Class A
(T114 inventory #10; T220 measured the residual: `review/T220/tsan_after_*.txt`).
Do NOT touch include/client/bridge/*, src/client/bridge/*, game_state*,
eq_*.cpp, spell_manager.cpp, combat.cpp (T221), eq_player_info.cpp or
wld_loader.cpp (T224).

## Defect
`RendererEventReceiver` (include/client/graphics/... — grep) holds ~15 plain
`bool`/int fields that the RENDER thread sets from Irrlicht input events and
the GAME thread reads-and-resets (key/mouse flags). Only one is `std::atomic`.
TSAN reports 17 distinct races on them under T220's tours.

## Scope
1. Read T114's write-up (refactor/T114_worker_render_race_audit.md, inventory
   #10) and T220's Review for the exact fields and stacks.
2. Fix with the codebase's discipline: prefer routing the flags through the
   existing intent queue (GameStateBridge intents, drained on the consumer
   thread) if the consumers are already intent-driven; otherwise make each
   read-and-reset flag a `std::atomic<bool>` with `exchange(false)` at the
   reader (relaxed is fine for independent flags; document why). Do NOT add
   a mutex. Keep the change mechanical; no behaviour change.
3. Unit test where one exists for the receiver; otherwise none — the
   acceptance is TSAN.
4. TSAN: build a TSAN tree exactly as T220 did (its README documents the
   command), run T220's DENSE tour script from review/T220/ unchanged.
   Expect 0 warnings; if any remain, list them with stacks — they are new
   inventory, not yours to fix.
5. Normal build: full ctest, visual gate, ARM build.

## Verification
Review in this file; evidence review/T225/ (tsan_before = T220's file,
tsan_after yours). Commit: `T225: RendererEventReceiver flags are atomic -- zero TSAN warnings on the tour`.

## Review (executor, slot2, 2026-08-25)

**Result: the dense TSAN tour reports ZERO warnings (T220's after-run: 17).**
TSAN writes no report file at all when there is nothing to report, so the
evidence is the absence of `tsan_after_dense_report.*` in `review/T225/`
plus the proof the run was real (below). Nothing else changed: build,
suite, visual gate and ARM all green.

### Design: atomics with `exchange()`, not intents -- and why

The plan's preference was intent routing IF the consumers were already
intent-driven. They are not, and the reason is structural, not incidental:
every one of the seven flagged flags is consumed on BOTH threads.

| flag | set (render thread, `OnEvent`) | consumed on render thread | consumed on game thread |
|---|---|---|---|
| `leftButtonClicked_` / `leftButtonReleased_` | mouse events | `renderer_frame.cpp:527-528` (`processFrameInput`, UI hit-testing) | `graphics_input_handler.cpp:71-72` (`updateFromEventReceiver` -> InputActionBridge) |
| `enterKeyPressed_` / `slashKeyPressed_` | RETURN / `/` key, or `OpenChat`/`OpenChatSlash` hotkey | `renderer_frame.cpp:844,853` (chat focus) | `graphics_input_handler.cpp:147,150` |
| `escapeKeyPressed_` | ESC | `renderer_frame.cpp:915` (clear target / close UI) | `graphics_input_handler.cpp:153` |
| `spellGemCastRequest_` / `hotbarActivationRequest_` | `SpellGemN` / `HotbarN` hotkeys | `renderer_frame.cpp:830,839` | `graphics_input_handler.cpp:190,202` |

That is the T220 stack pair exactly (`tsan_before_dense_report` #1: read at
`irrlicht_renderer.cpp:472` from `GraphicsInputHandler::updateFromEventReceiver
<- InputActionBridge::update <- Application::processInput <- gameThreadLoop`;
previous write at `:473` from `IrrlichtRenderer::processFrameInput <-
processFrame <- Application::render`, main thread). Routing a flag through
the intent queue would give it ONE consumer and silently change which side
gets the press -- a behaviour change the plan forbids. Two racing
consumers is the existing, shipped semantics ("first poll wins"); the
defect is only that the read-then-clear was two separate non-atomic steps,
so both threads could observe the same press, or a set could land between a
thread's read and its clear and be lost. `std::atomic<T>::exchange()` makes
read-and-reset one indivisible step, so exactly one consumer sees each
press, and nothing else moves.

Memory order is `relaxed` on every access, deliberately: each flag is a
self-contained one-shot with no data published "behind" it that the reader
then dereferences -- the reader acts on the returned value alone. There is
nothing for an acquire/release pair to order, so a fence would be cost
without meaning (this matters on the Cortex-A7, where `exchange` with
acq_rel emits `dmb`). The same note lives on the fields in the header.

No mutex (per plan). No behaviour change: the writers in `OnEvent`
(`enterKeyPressed_ = true`, `spellGemCastRequest_ = 3`, ...) are untouched
source -- `std::atomic`'s `operator=` turns each into a seq_cst store
(stronger than the relaxed reader needs, but the writer side is one store
per keypress, not a hot path, and leaving the writer text untouched keeps
the diff mechanical).

Two additional plain cross-thread fields in the same class were converted
under the same discipline, though TSAN did not flag them on the tour
(`--script` drives commands, not Irrlicht input, so the paths that read
them were not concurrently exercised): `quitRequested_` (render sets,
game thread polls in `GraphicsInputHandler::isActive`/`update`) and
`chatInputFocused_` (render sets, game thread reads at
`graphics_input_handler.cpp:44,85,158`). Both are plain load/store (no
reset), so `load`/`store` relaxed. Deviation from "the 17 flags only":
flagged here, +2 fields, same class, same pattern, zero risk.

### Left as inventory (NOT fixed -- out of the plan's "flags" scope, and a mutex is forbidden)

The class also has cross-thread accesses that are NOT single-word flags and
that atomics cannot fix. TSAN did not report them on the tour because the
scripted tour injects no Irrlicht key/mouse events, so they never ran
concurrently. They are real and need their own unit:

- `pendingKeyEvents_` (`std::vector<KeyEvent>`): pushed in `OnEvent`
  (render), popped/cleared from BOTH `renderer_frame.cpp:850,859,866-867,914`
  (render) and `GraphicsInputHandler::popKeyEvent/clearPendingKeyEvents`
  (game thread). A `vector` push racing a `vector::erase(begin())` is a
  heap corruption, not a lost keypress.
- `bridgeQueue_` (`std::vector<RendererEvent>`): pushed in `OnEvent` and
  `renderer_frame.cpp:920` (render), swap-drained in
  `graphics_input_handler.cpp:86` (game thread); `getBridgeQueueSize()`
  read at `renderer_frame.cpp:513`.
- `mouseX_/mouseY_/lastMouseX_/lastMouseY_` via `getMouseDeltaX/Y()`: a
  two-word read-modify-write consumed on both threads
  (`renderer_player_physics.cpp:107-108` render,
  `graphics_input_handler.cpp:67-68` game). Making the four ints atomic
  would not make "delta = cur - last; last = cur" atomic.
- `keyWasPressed_[]` via `wasKeyPressed(KEY_SPACE)` at
  `graphics_input_handler.cpp:158` (game) vs `OnEvent` (render); and
  `keyIsDown_[]` via `isKeyDown()`.

The right shape for those is a single swap-drained queue behind the
existing `GameStateBridge` intent path (one consumer each), which IS a
behaviour decision (which thread owns chat key events) and therefore not
this unit's. Recorded here for the orchestrator to plan.

### Evidence (`/mnt/projects/fable/review/T225/`)

| Step | Result |
|---|---|
| 1. `cmake --build slot2/build -j24` | PASS (`build_full.log`) |
| 2. `ctest --test-dir slot2/build` | **2787/2788**, sole failure the documented ALSA `AudioManagerTest.PreloadCommonSounds` (`ctest_full.txt`); `visual_gate` PASSED inside the suite (34.0 s), goldens untouched |
| 3. `scripts/build-gles2-x86.sh` | PASS (`build_gles2_x86.log`) |
| 4. Visual gate | PASS (as ctest #2785) |
| 5. `scripts/build-arm-noble.sh` | PASS (`build_arm_noble.log`, `Built target willeq`) |
| 6. `check_file_sizes.sh fable` | 1 WARN + 1 FAIL, **both pre-existing**: `irrlicht_renderer.cpp` is 3054 lines at fable `c87b553b` (already > 3000); this unit takes it to 3052 (-2). Not a new FAIL. (`check_file_sizes.txt`) |
| 7. TSAN dense tour | **0 warnings** (T220 after: 17). `tsan_after_dense.log` exit 0, 111/111 `CMD outcome=applied`, 40x `Targeting: Guard_Hewet000`, 10x `AFK enabled`, 10x `Clicking door`, clean `/camp` + `Application shutdown complete`. Binary is `slot2/build-tsan/bin/willeq` (`ldd` shows `libtsan.so.2`), configured with T114/T220's exact flags (`configure_tsan.log`, `build_tsan.log`), run via `run_tsan_x86.sh` (T220's driver, paths only), tour `tsan_tour_dense.txt` byte-identical to T220's, same Mesa-only suppressions. |

Before file: `tsan_before_dense_report.892231_T220` (a copy of T220's
after-run, 17 warnings, all `RendererEventReceiver::*`).

eqemu: lock taken (absent beforehand), `up.sh qeynos2`, Testchar staged to
T220's door-20 spot (`db_before.txt` -> `db_staged.txt`), restored after
(`db_restored.txt`: zone 36, -77.9232/21.9086/3, h190), `down.sh` clean,
lock deleted. Incident: T224 (slot3) overwrote the lock file's contents
at 18:37:56 while this unit held it; the server was not touched by them;
contents rewritten and the lock deleted at shutdown as normal.
