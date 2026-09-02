# T316 — Four live no-ops: keys and commands that report success and do nothing

**Slot: slot2. Branch `task/T316` cut from `fable`. x86 only. No Pi.
eqemu is held by slot1 (T311 Leg B) — build the acceptance headless and take a
live leg only if the lock frees. Label live vs headless evidence explicitly.**

Closes **tier 2 items 10, 11, 13 and 14** of `refactor/deferred.md`'s class-A
list. One unit because they are one defect class: **a user gesture that returns
Success and changes nothing.** Four small fixes, four separate pins — do not let
them turn into one big one.

## 1. The autorun key is a live no-op (item 10)

`EqActionHandler::toggleAutorun()` (`src/client/eq_action_handler.cpp:139-142`)
is a comment and a `LOG_DEBUG`. The path above it is **fully live**: three
default bindings (`src/client/input/hotkey_manager.cpp:472-474` — Grave,
Numpad+, NumLock) → `bridgeQueue_` → `GraphicsInputHandler`
(`graphics_input_handler.cpp:116`) → `InputActionBridge`
(`input_action_bridge.cpp:70-72`) → `ActionDispatcher::toggleAutorun()`
(`action_dispatcher.cpp:121-126`, which returns `Success()`) → the stub.
Console input reaches it too (`console_input_handler.cpp:336`).

*Impact:* three keys a player will actually press do nothing and report success.

**Implement it.** Autorun is sustained forward movement that survives key
release and is cancelled by the player. Decide and state: what cancels it
(a backward key? any movement key? `/stopall`?), and whether it survives
zoning and death. **Match the official client where you can check cheaply** —
the standing project rule is that the oracle decides, not argument. Where you
cannot check, say so and pick the conservative option.
⚠ `stopAllMovement()` sits directly below the stub and is the natural cancel
hook. `SetPositionState`/movement state is **slot1's territory this dispatch**
(T311 Leg B is in `eq_movement.cpp`) — if your implementation needs to reach
into movement state beyond the handler, coordinate rather than edit.

## 2. `/mem <name>` partial-name search is exact-match only (item 11)

`SpellDatabase::findSpellsByName()`
(`src/client/spell/spell_database.cpp:174-186`) forwards to `getSpellByName()`
and returns 0 or 1 result: `// TODO: Add a LIKE query to WilleqDatabase for
partial name search`.

*Impact:* memorising by partial name fails unless the player types the spell
name exactly.

Add the `LIKE` query to `WilleqDatabase` and use it. **Bind the parameter — do
not build the SQL by concatenation**, and escape `%` and `_` in the user's
string so a player typing `%` does not match every spell. Decide what happens
on multiple matches (list them? take the best? the real client's behaviour is
the reference) and pin whichever you choose.

## 3. `ReplyToTell` returns a sentinel and opens nothing (item 13)

`ActionDispatcher::openReplyToTell()` (`src/client/action/action_dispatcher.cpp:684-692`)
returns `ActionResult::Success("open_reply")` with
`// TODO: Implement when ChatState is added to GameState`.

**The comment's premise is stale**: `GameState` is the single store since T288.
What actually needs a home is **the last tell sender's name** — store it where
the tell arrives, expose it, and have the action open the chat input prefilled
with `/tell <name> `. If no tell has been received, the action must say so
rather than reporting success.

## 4. `InteractWorldObject` always fails (item 14)

`ActionDispatcher::interactNearestWorldObject()` (`action_dispatcher.cpp:519-526`)
returns `Failure("Use clickWorldObject with drop ID")` —
`// TODO: Implement when ObjectState is added to GameState`.

*Impact:* the interact key cannot open a forge, a chest or any ground container;
only the explicit drop-id path works, and no keybind reaches it.

**The working template is ten lines above it**: `interactNearestDoor()`
(`:505-517`) takes `m_state.doors().getNearestDoor(player.x(), player.y(),
player.z())`, fails cleanly when there is none, and only then calls the handler.
Do the same against world objects. If no equivalent nearest-object query exists
on the state, add one **next to the door one** and say so.

## Acceptance — four pins, one per defect

Every fix must be tested **through the path a user actually reaches** (the
standing project rule — a fix pinned only at the leaf leaves no regression net):

- autorun: through the keybinding/intent path, asserting movement persists after
  release AND that the cancel gesture stops it;
- `/mem`: a partial name matching several spells, plus a `%` in the input;
- reply-to-tell: after a received tell, and the no-tell-yet case;
- interact: with an object in range, and the empty case failing cleanly.

Plus: full `ctest -j6` (**NOT -j24**), zero new failures vs
`refactor/test_baseline.txt`; visual gate PASS with goldens untouched;
`check_file_sizes.sh --files` no new FAILs.

⚠ **If any of the four turns out to be bigger than it looks, STOP that one, do
the others, and report the remainder** — do not let one defect swallow the unit.

## Territory

`src/client/eq_action_handler.cpp`, `src/client/action/action_dispatcher.cpp`,
`src/client/spell/spell_database.cpp`, `include/client/willeq_database.h` +
its implementation, plus your tests.
⚠ Concurrent: slot1 T311 Leg B (`eq_movement.cpp`, `eq_bridge.cpp`,
`eq_entities.cpp`, `spell_cast_checks.cpp`), slot3 T315 (`src/client/input/`,
`irrlicht_renderer.*`, `renderer_frame.cpp`). **`src/client/input/` is T315's —
your autorun work must not edit the hotkey path.**
