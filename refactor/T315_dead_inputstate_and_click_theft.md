# T315 — The dead `InputState`, the clicks it eats, and a sweep of the pattern behind all four

**Slot: slot3. Branch `task/T315` cut from `fable`. x86 only. No Pi.
No eqemu is required — slot1 owns it. Live leg only if the lock is free; label
live vs headless evidence explicitly.**

Closes **items 30 and 31** of `refactor/deferred.md`'s class-A list (boarded
`03a3b2ca` from T312's findings) and does the sweep those two earn.

## Defect — a live path silently eats mouse clicks

`wasLeftButtonClicked()` / `wasLeftButtonReleased()`
(`src/client/graphics/irrlicht_renderer.cpp:488-495`) are atomic
read-and-reset one-shots with **two callers on two threads**:

| caller | thread | what it does with the result |
|---|---|---|
| `src/client/graphics/renderer_frame.cpp:527-528` | render | **the real consumer** — drives static-UI hit-testing |
| `src/client/input/graphics_input_handler.cpp:79-80` | game | writes `m_state.leftButtonClicked/Released` — **into a struct nothing reads** |

Atomic, so it is not a race. It is worse in a quieter way: whenever the game
thread wins the flag, **the click is consumed and no UI ever sees it.**

*Impact:* intermittently dropped mouse clicks in a real session — a click on an
inventory slot, a loot row, a spell gem that simply does nothing. Exactly the
failure mode T312 measured for the mouse delta, where a stolen delta froze the
camera for a frame.

⚠ **T225 ruled "both threads are legitimate consumers" here. Do not cite that
ruling — it predates the discovery that one of the two consumers writes into a
struct with no readers.** Verify that for yourself before you delete anything;
if `InputState` has grown a reader since T312 measured it, STOP and report.

## Item 30 — the sink is entirely dead

`InputHandler::getState()` has **zero callers** in `src/` or `tests/`
(T312, verified by grep); `resetMouseDeltas()` likewise;
`CameraController::update()` — the other delta consumer — is never called.
⚠ **`getState()` is a COMMON METHOD NAME** — `FrameBudgetGovernor::getState()`
and `SkeletalAnimator::getState()` are both live and unrelated. Scope your grep
to the input handler types; do not delete by name match.

## Work

1. **Delete the game-thread theft.** `graphics_input_handler.cpp:79-80` (and
   the mouse-delta sibling if any residue survives T312). The render-thread
   consumer at `renderer_frame.cpp:527` is the one that must keep working.
2. **Delete `InputState` and its dead accessors** across all four handlers —
   or, if you find a live reader, wire it and say which gesture proves it.
   Whichever you choose, the tree must not keep a struct that exists only to be
   written to.
3. **THE SWEEP — the reason this is one unit and not two.** This is the
   **fourth** instance of the same pattern (T230, T235, T312's mouse delta,
   this). State the rule and then enforce it:
   > *A destructive read-and-reset accessor with more than one caller is a
   > defect by construction, unless the accumulator is per-consumer.*
   Enumerate **every** read-and-reset accessor on `RendererEventReceiver` and
   its siblings — the `exchange(-1)` spell-gem/hotbar pair, the three chat
   flags, `getCameraZoomDelta`/`getMusicVolumeDelta`/`getEffectsVolumeDelta`,
   T313's new wheel accumulator, and anything else of that shape — and give a
   table: accessor | callers | threads | verdict. Fix any that has more than
   one caller; for the rest, the table IS the deliverable.
4. **Pin the rule so a fifth instance cannot land silently.** A test that
   enumerates these accessors and fails when one grows a second caller is the
   goal (`tests/test_duplicate_state_stores.cpp`'s `kKnownRemaining` is the
   precedent for a guard that cannot drift from its doc). If a compile-time or
   grep-based guard is more honest than a runtime test, do that instead and
   argue it.

## Acceptance

- **A test that fails before the fix**: two consumers contending for one
  one-shot flag, asserting the UI-side consumer sees every click. T312's
  `tests/test_bridge_queue_race.cpp` is the precedent and the deterministic
  0-vs-100 delta test is the shape to copy.
- The sweep table, in the Review.
- The guard from step 4, passing.
- Full `ctest -j6` (**NOT -j24**), zero new failures vs `refactor/test_baseline.txt`.
- Visual gate PASS, goldens untouched.
- `check_file_sizes.sh --files` no new FAILs.

## Territory

`src/client/input/`, `include/client/input/`,
`src/client/graphics/irrlicht_renderer.cpp`,
`include/client/graphics/irrlicht_renderer.h`,
`src/client/graphics/renderer_frame.cpp`, plus your tests.
⚠ Concurrent: slot1 T311 Leg B (`eq_bridge.cpp`, `eq_movement.cpp`,
`eq_entities.cpp`, `spell_cast_checks.cpp`, `entity_renderer.h`), slot2 T314
(`src/client/spell/`). No overlap — but **Batch J's J11 will split
`irrlicht_renderer.cpp`**, so keep your diff tight and do not reorganise it.

---

## Review

**Status: done.** The click theft is fixed with a test that fails 0-vs-100 on the
pre-fix tree, `InputState` is gone, the sweep is complete, and a guard pins the
rule. Evidence: `review/T315/gtest_BEFORE_click_theft.log`,
`review/T315/gtest_AFTER_click_theft.log`.

### Verification

| step | result |
|---|---|
| 1. `cmake --build wt/slot3/build -j24` | **PASS** (exit 0). Re-`cmake` first for the new target. |
| 2. `ctest --test-dir wt/slot3/build --output-on-failure -j6` | **PASS** — 3369/3370. Sole failure `AudioManagerTest.PreloadCommonSounds`, the documented ALSA host quirk. Zero new failures vs `refactor/test_baseline.txt`. Suite 3366 -> **3370** (this unit's 4). |
| 3. `scripts/build-gles2-x86.sh` | **PASS** (exit 0). |
| 4. Visual gate | **PASS** (test #3367, inside ctest, 20.5 s). Goldens untouched. |
| 5. `scripts/build-arm-noble.sh` | **PASS** (exit 0). |
| 6. `scripts/refactor/check_file_sizes.sh fable` | **No new FAIL.** `irrlicht_renderer.cpp` was already failing at base (3273 > 3000) and is 3274 (net +1, a comment rewrite); the header's WARN was already 2452 and is 2462. Everything this unit actually touched is `ok`. |

### 1. Was `InputState` genuinely dead? Verified independently, YES

Re-checked before deleting anything, not taken from T312:

* `IInputHandler::getState()` — **zero call sites** anywhere in `src/`,
  `tests/` or `tools/`. The only hits for the name are the pure virtual, the
  four `override` definitions, and `TradeManager::getState()` /
  `FrameBudgetGovernor::getState()` / `SkeletalAnimator::getState()`, which are
  unrelated types (the plan's warning was warranted — a bare name grep is
  useless here; the search was scoped to the handler types).
* `resetMouseDeltas()` — zero call sites; only the pure virtual, three
  definitions and the test fake's counter (which no test ever read).
* No field of `InputState` is read anywhere. `FakeInputHandler::state` in
  `tests/action_test_fakes.h` exists so the fake compiles against the
  interface; no test touches it.
* The movement half was a duplicate as well as dead: `renderer_player_physics.cpp`
  polls the identical HotkeyManager bindings through
  `RendererEventReceiver::isKeyDown()` on the render thread, non-destructively.
  That is the path that actually moves the character.

So the whole snapshot went: the struct, both interface members, all four
implementations' copies, `NullInputHandler`'s six `setMove*/setStrafe*/setTurn*`
setters (also zero callers — the `setMoveForward` hits in `eq_movement.cpp` are
`GameState`'s player object, a different class), and the fake's members.

### 2. The click theft — before/after

`ClickOwnershipTest.GameThreadPollDoesNotStealUIClick` posts 100
press/release pairs, runs one game tick between the event and the frame (what
happens whenever a game tick lands between two render frames), and counts what
the render-thread consumer sees:

| | clicks seen by the UI-side consumer | releases |
|---|---|---|
| **before** | **0 / 100** | **0 / 100** |
| **after** | 100 / 100 | 100 / 100 |

Deterministic, no sanitizer, no device. The game tick is *always* faster than
the frame here, which is why it is 0 and not a fraction — but the second test,
`ConcurrentGameThreadPollLosesNoClick` (a real game thread polling flat out
against 2000 render-thread clicks), shows the realistic figure: **1994/2000
before, 2000/2000 after.** That is the shape a player experiences — most clicks
work, a few silently do nothing, and it is unreproducible on demand.

The deleted caller is `graphics_input_handler.cpp`'s two lines writing
`m_state.leftButtonClicked/Released`; the surviving consumer is
`renderer_frame.cpp:527-528`, which drives `pendingUIClick_`, the static-UI hit
test and `handleMouseTargeting()`.

T225's ruling is superseded in place, in the comment on the flags themselves,
with the reason (the second "consumer" wrote to a struct with no readers).
Nothing cites it as still standing.

### 3. THE SWEEP

> **The rule: a destructive read-and-reset accessor with more than one caller
> is a defect by construction, unless the accumulator is per-consumer.**

Every destructive read-and-reset accessor on `RendererEventReceiver` (there are
no sibling event-receiver classes in `src/`; `zone_line_editor.cpp` in `tools/`
has its own unrelated one-flag receiver, out of the client's threading model).
Call sites are production `src/` only.

| accessor | callers | thread(s) | verdict |
|---|---|---|---|
| `wasKeyPressed()` | 1 — `graphics_input_handler.cpp:162` (Jump) | game | OK (T312) |
| `getMouseDeltaX()` | 1 — `renderer_player_physics.cpp:111` | render | OK — 2nd caller deleted by T312 |
| `getMouseDeltaY()` | 1 — `renderer_player_physics.cpp:112` | render | OK — same |
| `wasLeftButtonClicked()` | **2 -> 1** — `renderer_frame.cpp:527` | **render + game -> render** | **DEFECT, FIXED (T315)** |
| `wasLeftButtonReleased()` | **2 -> 1** — `renderer_frame.cpp:528` | **render + game -> render** | **DEFECT, FIXED (T315)** |
| `drainActions()` | 1 — `renderer_frame.cpp:510` | render | OK — render-only queue |
| `drainBridgeActions()` | 1 — `graphics_input_handler.cpp:94` | game | OK — mutex-guarded (T312) |
| `getSpellGemCastRequest()` | 1 — `graphics_input_handler.cpp:194` | game | OK — the render-thread discard was already removed |
| `getHotbarActivationRequest()` | 1 — `graphics_input_handler.cpp:206` | game | OK — same |
| `getCameraZoomDelta()` | 1 — `renderer_frame.cpp:821` | render | OK |
| `getMusicVolumeDelta()` | 1 — `renderer_frame.cpp:836` | render | OK |
| `getEffectsVolumeDelta()` | 1 — `renderer_frame.cpp:840` | render | OK |
| `getMouseWheelDelta()` (T313) | 2 sites, **1 consumer** — `renderer_frame.cpp:546` / `:549` | render | OK — **documented exception**: the two sites are the two arms of one `if/else` in `processFrameInput()`, mutually exclusive (UI consumes the notch, else-arm drains it so it cannot accumulate while the new UI is off) |
| `popKeyEvent()` | 1 — `renderer_frame.cpp:900` | render | OK (T241) |
| `enterKeyPressed()` | 1 — `renderer_frame.cpp:877` | render | OK (T241) |
| `slashKeyPressed()` | 1 — `renderer_frame.cpp:886` | render | OK (T241) |
| `escapeKeyPressed()` | 1 — `renderer_frame.cpp:948` | render | OK (T241) |

Deliberately **not** in the table, because they are non-destructive and a second
reader is harmless: `isKeyDown`, `getMouseX/Y`, `isLeftButtonDown`,
`isRightButtonDown`, `getClickMouseX/Y`, `quitRequested`, `isChatInputFocused`,
`getBridgeQueueSize`, `hasPendingKeyEvents`, `getCurrentMode`.

**One defect found by the sweep beyond the two the plan named: none.** Every
other accessor is already single-consumer. That is a useful result — the fourth
instance was the last one.

### 4. The guard

`tests/test_read_and_reset_ownership.cpp`, `ReadAndResetSweepGuard`. A runtime
test, not a compile-time or grep guard, and the argument for that choice: the
thing being asserted is a *count over the whole source tree*, which no
declaration can express, and a shell grep in CI is invisible to an executor
running `ctest`. It scans every `.cpp/.h` under `src/`, strips `//` and `/* */`
comments (necessary — each of these four fixes left a prose mention of its own
accessor behind, and an uncomment-aware count would report them as callers),
and compares the number of `->accessor(` / `.accessor(` sites against the
`kSweep` table above. The table's `note` column travels with the assertion, so
the guard cannot drift from its documentation (the
`test_duplicate_state_stores.cpp` `kKnownRemaining` precedent). Adding a second
caller to any of the 17 fails the suite with a message naming the rule.

A second test, `NoInputStateSnapshotStructReturns`, fails if the word
`InputState` reappears in any of the four handler headers or three handler
TUs — the deletion is easy to undo as "harmless bookkeeping", and the harm is
exactly what this unit measured.

Both guards fail on the pre-fix tree (see the BEFORE log), so they are pinned,
not decorative.

### Found and deliberately NOT fixed

1. **Console keyboard mode has never moved the character.** `ConsoleInputHandler`'s
   WASD/QE cases set `InputState` movement flags, and nothing read them — in any
   build, ever. The cases were deleted with the struct rather than left looking
   functional; console movement still works through the typed move commands in
   `processCommand()`. Wiring keyboard-mode movement is a feature, not a fix, and
   needs a test that observes the character actually moving. **Worth boarding.**
2. **`CameraController::update()` is still never called** (T312 noted it; still
   true — `cameraController_` is constructed at `irrlicht_renderer.cpp:1652` and
   nothing else in `src/` touches the object). It is the other mouse-delta
   consumer that does not exist. Outside this unit's territory
   (`camera_controller.*`); left alone.
3. **`currentMode_` is still dead configuration** — read in `OnEvent`, written
   by nothing but its initializer, `getCurrentMode()` uncalled. Not a race, not
   this unit.
4. **`irrlicht_renderer.cpp` remains over the 3000-line FAIL threshold** (3274,
   base 3273). Item 32 on the board — splitting `RendererEventReceiver` into its
   own TU — is now even more obviously right: the object has a fully documented
   threading contract and a dedicated test file. Not done here; the plan
   explicitly said to keep the diff tight because Batch J's J11 will split this
   file.
5. **No live session.** eqemu was not required by the plan and slot1 owns the
   lock; all evidence above is headless and explicitly labelled as such. The
   deterministic 0-vs-100 result does not need a session to be believed, but a
   live click-through of inventory/loot/spell gems would be a worthwhile smoke
   test for whoever next holds the server.

### Deviations from the plan

* **None on substance.** Two notes:
  * The plan's step 2 said "delete `InputState` ... or, if you find a live
    reader, wire it". There is no live reader; deletion it is. The deletion
    reached slightly further than the struct itself — `NullInputHandler`'s six
    movement setters and `ConsoleInputHandler`'s keyboard-movement cases were
    its only writers and had to go with it, or the tree would have kept code
    whose only effect was to fill a struct that no longer exists.
  * `#include "client/input/hotkey_manager.h"` was dropped from
    `graphics_input_handler.cpp`: the deleted movement scan was its only user.
