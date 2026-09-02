# T312 — RendererEventReceiver's cross-thread state: the heap corruption and the scalar races

**Slot: slot2. Branch `task/T312` cut from `fable`. x86 only. No Pi, no eqemu
for the fix; ONE live keyboard session for the acceptance leg (see below).**

Closes **tier 1 items 1 and 2** of `refactor/deferred.md`'s "THE ONE CLASS-A
LIST" — the worst open defect in the tree. Deliberately ONE unit: all of it
lives in the same object and the same ownership question.

## The defect

`bridgeQueue_` is a plain `std::vector<RendererEvent>` with **writers on the
render thread and a drain on the game thread**:

| site | thread | operation |
|---|---|---|
| `src/client/graphics/irrlicht_renderer.cpp:305-351` (`OnEvent`, decoded hotkey actions) | render | `push_back` |
| `src/client/graphics/renderer_frame.cpp:944` (`pushBridgeAction`) | render | `push_back` |
| `src/client/graphics/renderer_frame.cpp:513` (`getBridgeQueueSize`, a log line) | render | `size()` |
| `src/client/input/graphics_input_handler.cpp:86` (`drainBridgeActions`) | **game** | `swap` |

A reallocating `push_back` racing the `swap` is **heap corruption**, not a
wrong value. TSAN has never seen it because `--script` runs inject no Irrlicht
input — a real keyboard session is the only thing that reaches it.

Same object, same unit, scalar races (dropped/doubled input, not corruption):
- `getMouseDeltaX/Y()` (`irrlicht_renderer.cpp:453-462`) is a read-and-reset of
  two words. **Note this is worse than a race — it has TWO consumers, one per
  thread**: `renderer_player_physics.cpp:111-112` (render) and
  `graphics_input_handler.cpp:67-68` (game). Whichever reads first STEALS the
  delta from the other. Say in your Review whether both consumers are supposed
  to exist; if one is vestigial that is a finding, and deleting it may be the
  real fix. Do not delete it without saying why.
- `keyIsDown_[]` / `keyWasPressed_[]` (`include/client/graphics/irrlicht_renderer.h:436-437`),
  written in `OnEvent` and read via `isKeyDown`/`wasKeyPressed`.

## Ownership is already settled — do not re-open it

The architecture is: **render decodes, game executes.** T241 settled the one
question this entry used to carry (chat key events) by making them
render-thread-only; `pendingKeyEvents_` and the three chat flags are now
single-threaded and are NOT in scope. There is no design decision left here.

## Work

1. **Put `bridgeQueue_` behind a mutex** (`GameStateBridge` already has one to
   copy the discipline from) **or a real SPSC queue** — your call, argued in the
   Review. Every one of the four sites above goes through it, `getBridgeQueueSize`
   included; a lock-free-looking `size()` on a vector another thread is
   `swap`ping is the same bug in a smaller font.
2. **Fix the two-word RMW** on the mouse deltas, and resolve the double-consumer
   question above.
3. **Fix the key arrays.** `std::atomic<bool>` per entry is acceptable if you
   show the array is only ever touched element-wise; say so explicitly.
4. **Leave `actionQueue_` alone unless you prove it has the same shape** — it is
   drained at `irrlicht_renderer.h:384`. If it IS cross-thread, fix it here and
   say so; if it is render-only, write one comment at the declaration saying
   that, so the next reader does not re-derive it.

## Acceptance — the hard part

The existing suite cannot see this bug; do not let a green `ctest` stand in for
evidence.

- **A test that fails before your fix.** A targeted stress harness driving
  `push_back` and `drainBridgeActions` from two threads under **TSAN**, showing
  a report on the pre-fix code and none after. This is the deliverable that
  matters — a fix with no failing-before test leaves no regression net (the
  standing project rule).
- **Full `ctest -j6`** (NOT -j24 — the visual gate's CPU ceiling flakes above
  that), zero new failures against `refactor/test_baseline.txt`.
- **One live keyboard session** with a real X server: bind-heavy play for a few
  minutes (targeting keys, autoattack, hail, clear-target — the actions that
  actually route through `bridgeQueue_`), clean exit. This is a smoke test, not
  proof; say so.
- `scripts/refactor/check_file_sizes.sh` no new FAILs on files you touch.

## Territory

`include/client/graphics/irrlicht_renderer.h`, `src/client/graphics/irrlicht_renderer.cpp`,
`src/client/graphics/renderer_frame.cpp`, `src/client/input/graphics_input_handler.cpp`,
`src/client/graphics/renderer_player_physics.cpp`, plus your new test.
**Slot3 is in `src/client/graphics/ui/` this dispatch (T313) — stay out of it.**
