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

---

## Review

**Status: done.** All three defects fixed, each with a test that fails on the
pre-fix code. Evidence in `review/T312/` (see its `README.md`).

### Verification

| step | result |
|---|---|
| 1. `cmake --build wt/slot2/build -j24` | **PASS** (exit 0). The tree needed a re-`cmake` first to pick up the new test target. |
| 2. `ctest --test-dir wt/slot2/build --output-on-failure -j6` | **PASS** — 3361/3362, sole failure `AudioManagerTest.PreloadCommonSounds` (the documented ALSA host quirk). Zero new failures vs `refactor/test_baseline.txt`. Suite is now **3362** (3359 + this unit's 3). |
| 3. `scripts/build-gles2-x86.sh` | **PASS** (exit 0). |
| 4. Visual gate | **PASS** (inside ctest, 20.1 s). Goldens untouched. |
| 5. `scripts/build-arm-noble.sh` | **PASS** (exit 0). |
| 6. `scripts/refactor/check_file_sizes.sh fable` | **No new FAIL.** `irrlicht_renderer.cpp` was ALREADY failing at base (3237 > 3000) and is now 3247; the header's WARN was already 2395 and is now 2441. See "not fixed" below. |
| Live keyboard session | **PASS**, see §4. |

### 1. The synchronisation shape: a mutex, not an SPSC queue

`bridgeQueue_` is now guarded by a `mutable std::mutex bridgeMutex_`, and all
four sites go through the three locked accessors on the receiver. The 15 raw
`bridgeQueue_.push_back(...)` calls inside `OnEvent` were rewritten to call
`pushBridgeAction(...)`, and the `bridgeQueue_.size()` in `OnEvent`'s trace
line to `getBridgeQueueSize()`, so **there is no raw access to the member
anywhere in the tree** — the invariant is enforceable by grep, not by care.

Why a mutex and not a real SPSC ring:

* The critical sections are a `push_back`, a `swap` and a `size()`. Contention
  is essentially nil — a handful of events per frame against a drain that runs
  once per game tick — so there is nothing to win.
* A ring needs a fixed capacity and therefore a **drop policy for a full
  queue**. Silently dropping a keypress is a worse bug than the one being
  fixed, and no drop policy is obviously right here.
* `getBridgeQueueSize()` cannot be made correct lock-free on a vector at all.
  The plan's "the same bug in a smaller font" is exactly right, and a mutex is
  the only shape that covers it without deleting the log line.
* `GameStateBridge` already uses this discipline, so this is the house pattern,
  not a new one.

The mutex is only ever held across those three operations — never across a
callback, an allocation the caller controls, or any renderer work — so it
cannot deadlock against anything.

`actionQueue_` was checked and is **render-thread only**: filled in `OnEvent`,
drained solely at `renderer_frame.cpp:510` (`processFrame`), both on the render
thread. Per the plan's item 4 it is left unlocked with a comment at the
declaration saying so, and saying what would have to change if a game-thread
drain is ever added.

### 2. The mouse-delta double consumer: the game-thread caller was DELETED

**Verdict: only the render-thread consumer should exist. The game-thread one
was not merely redundant — it was write-only.**

The chain, traced end to end:

* `GraphicsInputHandler::updateFromEventReceiver()` called `getMouseDeltaX/Y()`
  and stored the result in `m_state.mouseDelta{X,Y}` (`InputState`).
* `InputState` is reachable only via `InputHandler::getState()`.
* **`getState()` has zero callers.** Not in `src/`, not in `tests/` (the only
  hit is `action_test_fakes.h` implementing the pure virtual). The whole
  `InputState` snapshot is write-only; `resetMouseDeltas()` likewise has no
  caller. `CameraController::update()`, the other function taking mouse deltas,
  is also never called.

So the game-thread poll could only ever *steal* the delta from
`IrrlichtRenderer::updatePlayerMovement()`, the real mouse-look. This is the
T230/T235 two-consumer shape a third time, and it is **user-visible**: any game
tick landing between two frames while the button is held ate that frame's
camera motion. The fix is the two-line deletion, with a comment at the call
site and at the accessor declaration recording that these are render-thread
only and why a second consumer must not be re-added.

`test_bridge_queue_race.cpp`'s `MouseDeltaOwnershipTest` pins it and needs no
sanitizer: pre-fix it reads 0/0 where 100/50 were pending, every run.

With that consumer gone, `lastMouseX_/lastMouseY_` are single-threaded and stay
plain `int`s (documented). `mouseX_/mouseY_` are still written on the render
thread and read on the game thread by `getMouseX/Y()`, so they became relaxed
`std::atomic<int>`, as did `clickMouseX_/Y_` and the two button-down bools —
the same T225 discipline already applied to the one-shot flags. `getMouseDeltaX/Y`
now take **one** load of the shared word, so the returned delta and the new
baseline cannot disagree.

### 3. The key arrays

`keyIsDown_[]` and `keyWasPressed_[]` are now `std::atomic<bool>` arrays with
relaxed access. **The array is only ever touched element-wise** — the audit is
complete and short: the constructor's zeroing loop, `OnEvent`'s two indexed
writes and two indexed reads (the Alt check), and the two accessors. Nothing
copies, memsets, passes, or returns the array as a whole, so per-element atomics
are exactly the granularity of the sharing and cost nothing on x86.
`std::atomic<bool>` has no aggregate initializer, so the `= {false}` at the
declaration moved into the existing constructor loop, which already zeroed both
arrays.

`wasKeyPressed()` became a single `exchange(false)`, so a press is observed
exactly once — the same argument T225 made for the button flags. Its only
caller is `GraphicsInputHandler`'s Jump check on the game thread.

### 4. Evidence

**Before, TSAN** (separate `build-tsan/` tree, `-fsanitize=thread -g -O1`, the
P23/T35 precedent; the tree is gitignored and not committed):

* `EveryPushedActionIsDrainedExactlyOnce` — **9 data-race reports** across
  `emplace_back`, `_M_realloc_insert`, `size()`, `_Vector_impl_data::_M_copy_data`
  and, twice, `operator delete`; the run then aborts with
  `allocation-size-too-big`. That last one is the point: the vector's own
  bookkeeping had been corrupted, which is what "heap corruption, not a wrong
  value" means in practice. `review/T312/tsan_BEFORE_bridgequeue.log`.
* The same test in the **plain, non-sanitizer** build **segfaults** (exit 139)
  within a second. The defect does not need a sanitizer to be real.
* `KeyStateSurvivesConcurrentPolling` — 1 report, `irrlicht_renderer.cpp:242`
  (`keyWasPressed_[...] = true`) against the game-thread poll.
  `review/T312/tsan_BEFORE_keyarrays.log`.
* `GameThreadPollDoesNotStealMouseLookDelta` — plain gtest failure, 0 vs 100.
  `review/T312/gtest_BEFORE_mousedelta.log`.

**After:** same TSAN tree, same three tests — **0 warnings, 3 passed, exit 0**
(`review/T312/tsan_AFTER_all_three.log`); plain build 3/3 in 130 ms.

Host note: TSAN needs `setarch -R` here, or it dies with
`FATAL: ThreadSanitizer: unexpected memory mapping` before running anything.

**Live keyboard session** (smoke test, explicitly not proof). Xvfb `:95`,
`build-gles2/bin/willeq --egl-x11 -r 1024 768 --eqe1-path /mnt/projects/EverQuest_ETC1
--no-audio --log-module=INPUT:DEBUG`, account `willeq_test`/Testchar, zone
qeynos2, `#set time 11 0`. 340 rounds over ~200 s of F1/F8/Tab/q/h/c/Escape/backtick
plus w/a/s/d/space and held-button mouse-look, then `/quit`. Result: **2599
bridge-queue drains, 16 499 input-trace lines, exit 0, no crash**, and the game
thread received exactly what was sent — 677 Hail, 677 ToggleAutoAttack, 339
ClearTarget, 339 ToggleAutorun, 338 Consider (2 per round for the doubled keys,
1 for the singles, last round truncated).
`review/T312/live_session_bridge_action_counts.txt`.

### Found and deliberately NOT fixed

1. **`InputState` and `InputHandler::getState()` are entirely dead.** Not just
   the mouse deltas — no caller reads any field of the snapshot, in any of the
   four handler implementations, and `resetMouseDeltas()` is likewise
   uncalled. Deleting the interface members is a cross-cutting change well
   outside this unit's territory. **Board it.** The same is true of
   `CameraController::update()`, whose mouse-delta parameters have no live
   caller.
2. **`wasLeftButtonClicked()` / `wasLeftButtonReleased()` have the same
   double-consumer shape as the mouse deltas** — a destructive `exchange`
   consumed on both threads, with the game-thread side writing into the dead
   `InputState`. T225 settled that "both threads are legitimate consumers", but
   that ruling predates the observation in point 1: the game-thread side is not
   a consumer at all. It is atomic, so it is not a *race* — it is silent
   click-eating of the same class T230/T235/this unit each found. Out of scope
   here (the plan names the mouse deltas specifically); **board it as the fourth
   instance of the pattern.**
3. **`irrlicht_renderer.cpp` was already over the 3000-line FAIL threshold at
   base** (3237) and this unit adds 10 lines; the header's WARN was already at
   2395 and adds 46, most of them the invariant comments this plan asked for.
   No new FAIL, but nothing improved either. Splitting `RendererEventReceiver`
   into its own TU is the obvious move and is a clean, self-contained follow-up
   — it is now a genuinely cohesive object with a documented threading
   contract. Not done here: it would have made the diff unreviewable against
   the actual fix.
4. **`currentMode_`** is read in `OnEvent` and has no writer anywhere (only its
   initializer), and `getCurrentMode()` has no caller. Left alone — it is not a
   race, it is dead configuration.

### Deviations from the plan

* **None on substance.** One addition: the plan named the vector and the two
  scalar groups; the audit of the same object also turned `mouseX_/mouseY_`,
  `clickMouseX_/Y_` and `leftButtonDown_/rightButtonDown_` into relaxed atomics,
  because they are read cross-thread by the non-destructive getters and were
  plain scalars. That is the same defect class in the same object and would
  otherwise have left TSAN reports behind after the named fixes landed.
* The `build-tsan/` tree is a local artefact (gitignored, `build-*/`), not
  committed, per the P23 instruction.
* **Site note for the next unit that wants eqemu:** the stack was found already
  running (a full spire launcher fleet, up since Aug 30) with **no lock held**.
  `up.sh` on top of that starts a *second* loginserver and zone that cannot
  bind, and the client then gets "could not find world server WillEQ" — the
  fresh loginserver has no world registered. `down.sh` then `up.sh` fixes it.
  Worth boarding: a prior session left the server up without the lock.
* **Xvfb has no window manager on this host** (no openbox/fluxbox/twm
  installed), so `xdotool windowactivate` fails with "windowmanager claims not
  to support _NET_ACTIVE_WINDOW" and `xdotool key --window <id>` (XSendEvent) is
  silently ignored by Irrlicht. `xdotool windowfocus --sync <id>` followed by
  plain `xdotool key` (XTEST) works and is what T309's recipe relies on;
  `mousemove_relative` takes no `--window`. Two windows carry the title
  `WillEQ - Testchar`; focusing both is the reliable move.
* Testchar was found at the T311-documented inherited pose
  (`zone_id=2, x=-74, y=428, z=3.751, h=160`), moved by the session's own
  movement keys, and **restored to that pose**, not to the dispatch's
  befallen coordinates — per the T311 note now in the protocol. eqemu shut
  down cleanly, lock released.
