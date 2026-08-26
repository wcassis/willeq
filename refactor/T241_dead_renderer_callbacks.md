# T241 — delete Application::connectRendererCallbacks() and the dead game-thread key readers (T235 findings)

Slot: slot1. x86. NO eqemu. Class A, reachability pattern #13. Territory:
src/client/application.cpp `connectRendererCallbacks` (+ header),
include/client/graphics/irrlicht_renderer.h / output/graphical_renderer.h
callback setters it targets, src/client/input/graphics_input_handler.cpp
dead consumers, include/client/graphics/renderer_event_receiver* (only the
accessors that lose their last caller), tests. Do NOT touch
input_action_bridge / action_dispatcher hotbar arms (T239 slot3),
eq_action_handler.cpp / combat.* (T240 slot2), renderer_frame.cpp's
processChatInput body (T235 merged — leave it).

## Findings (T235 Review, review/T235/)
1. `Application::connectRendererCallbacks()` is unreachable: nothing
   derives from `output::GraphicalRenderer`, so its `dynamic_cast` always
   fails. Every callback it would set (target, door, chat-submit, …) and
   `IrrlichtRenderer`'s parallel setters have no other caller; live routes
   are bridge intents. Verify each with grep before deleting; delete the
   method, the setters, the callback members, and any `std::function`
   typedefs left orphaned.
2. `enterKeyPressed` / `slashKeyPressed`: the GAME-thread consumers in
   `GraphicsInputHandler` (`OpenChat`/`OpenChatSlash` actions) have no
   downstream consumer; the render thread handles chat opening. Delete the
   game-thread side.
3. `popKeyEvent` / `clearPendingKeyEvents` game-thread callers
   (`graphics_input_handler.cpp` ~242/257) have no production caller.
   Delete; if that leaves the render thread as the ONLY user of
   `pendingKeyEvents_`, say so — it removes one item from T225's vector
   race inventory (deferred.md).
For each deletion record the grep proving zero callers (before) and the
build proving nothing needed it (after).

## Verification
Full ctest; visual gate (renderer headers touched); Review with the
line-count delta; evidence review/T241/. Commit:
`T241: unreachable renderer callback plumbing and dead game-thread key readers deleted`.

## Review (executor, 2026-08-26, slot1, off fable 64241f57)

Evidence: /mnt/projects/fable/review/T241/ (README.md indexes every file;
`before_grep.txt` / `after_grep.txt` are the per-symbol reference lists,
same script both times).

### Deletions, with before-grep / after-build per item

| # | deleted | before-grep (callers outside the definition) | after |
|---|---|---|---|
| 1 | `Application::connectRendererCallbacks()` (decl + body + call site + orphaned `graphical_renderer.h` include) | 1 caller (`Application::initialize`); `: public GraphicalRenderer` / `: public output::GraphicalRenderer` -- **0 hits**, so the `dynamic_cast` could never succeed | builds |
| 2 | `output::GraphicalRenderer` callback block: `DoorInteractCallback`, `TargetCallback`, `MovementCallback` + `PlayerPositionUpdate`, `ChatSubmitCallback`, `PetCommandCallback`, `InventoryActionCallback` -- typedefs, virtual setters, `m_*Callback` members | only #1 called any setter; no override of any of them anywhere | builds |
| 3 | `IrrlichtRenderer::setTargetCallback`/`targetCallback_`, `setDoorInteractCallback`/`doorInteractCallback_`, `setChatSubmitCallback`/`chatSubmitCallback_` (+ the out-of-line definition in renderer_entities.cpp) and their `using` typedefs; the guarded `if (cb_) cb_(..)` invocations in renderer_overlays.cpp (target, door) and renderer_frame.cpp (door) | setters: **0 callers** (`chatSubmitCallback_` was never even invoked) | builds; every invocation site already pushes the bridge intent on the next line (`TargetIntent`, `DoorInteractIntent`), which stays |
| 4 | **(deviation, same pattern, same header)** `setLootCorpseCallback`, `setVendorToggleCallback`, `setBankerInteractCallback`, `setTrainerToggleCallback`, `setReadItemCallback` (body was `(void)callback`), `setWorldObjectInteractCallback`, `setZoningEnabledCallback` -- typedefs, setters, members, and their guarded invocations in renderer_overlays.cpp / renderer_frame.cpp | **0 callers** for all seven setters (src, include, tests, tools, benchmarks) | builds; each invocation site keeps its `LootCorpseIntent` / `VendorToggleIntent` / `BankerInteractIntent` / `TrainerToggleIntent` / `WorldObjectInteractIntent` / `ZoningEnabledIntent` push |
| 5 | `GraphicsInputHandler::updateFromEventReceiver()` reads of `enterKeyPressed()` / `slashKeyPressed()` and the `InputAction::OpenChat` / `OpenChatSlash` they set | `InputAction::OpenChat`: GIH + one test list; `OpenChatSlash`: GIH only -- **no consumer** (`hasAction`/`consumeAction` never asked for them) | builds; the render thread's `processChatInput()` is the sole reader |
| 6 | **(deviation)** the `escapeKeyPressed()` read next to them and `InputAction::CloseChat` | `CloseChat`: GIH only, **no consumer**. T235's table said the render thread had no Escape reader; that was wrong -- `renderer_frame.cpp` (`processChatInput`, "Not in chat" branch) exchanges the same flag to clear the target. The game-thread read was therefore a genuine raced-and-lost (a stolen Escape = a target-clear that never happens), not merely a dead second reader | builds |
| 7 | `IInputHandler::hasPendingKeyEvents()` / `popKeyEvent()` / `clearPendingKeyEvents()` and `input::KeyEvent`; the overrides in `GraphicsInputHandler`, `ConsoleInputHandler`, `NullInputHandler`, and `tests/action_test_fakes.h` | **0 callers** through the interface (every hit was a definition or an override) | builds |
| 8 | `InputAction::OpenChat/OpenChatSlash/CloseChat` enumerators | see 5/6; `tests/test_input_action_bridge.cpp` used `OpenChat` as an example non-bridge action -- swapped for `ToggleHUD`, assertion unchanged | builds, test passes |

Kept (live): `RendererEventReceiver::enterKeyPressed/slashKeyPressed/
escapeKeyPressed/popKeyEvent/clearPendingKeyEvents/hasPendingKeyEvents` and
`pendingKeyEvents_` -- the render-thread chat path uses all of them.
`GraphicalMode::graphicalRenderer()` and the `GraphicalRenderer` class
itself (camera-mode accessors) -- out of scope, left alone. Nothing in the
plan's list turned out to have a live caller.

### pendingKeyEvents_ is now single-threaded
Writer: `RendererEventReceiver::OnEvent` (Irrlicht device run, render
thread). Readers: `IrrlichtRenderer::processChatInput()` only (render
thread). The three chat flags followed: `enterKeyPressed_` /
`slashKeyPressed_` / `escapeKeyPressed_` are plain `bool` again (were
`std::atomic<bool>` + `exchange`, T225) with the T225 comment replaced.
`refactor/deferred.md`: the T235 item marked CLOSED and the T225
vector-race entry annotated (`pendingKeyEvents_` struck; `bridgeQueue_`,
`getMouseDeltaX/Y`, `keyWasPressed_[]` remain).

### Line-count delta
`git diff --stat`: 14 source/test files, **+41 / -356** (net -315); the 14
files went 10821 -> 10506 lines (`before_grep.txt` / `after_grep.txt`
tails). Plus the two `deferred.md` annotations.

### Verification bar
1. `cmake --build slot1/build -j24` -- OK, 0 errors.
2. ctest: **2858/2859**, sole failure AudioManagerTest.PreloadCommonSounds
   (ALSA, documented) -- `ctest_full.txt`.
3. `build-gles2-x86.sh` -- exit 0 (`build_gles2_x86.txt`).
4. Visual gate **PASS**, goldens untouched (`visual_gate.txt`; `git status`
   shows only this unit's files).
5. `build-arm-noble.sh` -- exit 0 (`build_arm_noble.txt`).
6. `check_file_sizes.sh fable` -- 14 checked, 0 fail, 3 pre-existing WARNs
   (`check_file_sizes.txt`).

### Deviations
- #4 and #6 above: seven more `IrrlichtRenderer` callback setters and the
  Escape read deleted beyond the plan's literal list. Same files, same
  class, same zero-caller evidence; leaving them would have left 7 of the
  10 dead setters in the header the unit was cleaning. No overlap with
  T239 (input_action_bridge / action_dispatcher) or T240
  (eq_action_handler / combat).
- Atomic -> plain bool for the three chat flags (a type change, not a pure
  deletion) so the header does not carry a comment describing a
  cross-thread discipline that no longer exists.
