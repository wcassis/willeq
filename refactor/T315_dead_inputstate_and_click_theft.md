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
