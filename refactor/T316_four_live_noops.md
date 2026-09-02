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

---

## Review (executor, slot2, 2026-09-02)

**Three of the four fixed and pinned separately. Autorun STOPPED** — not
because it is large, but because the only live consumer of an autorun flag is
in `irrlicht_renderer.*`/`renderer_player_physics.cpp`, and the plan's own
instruction for that case is "coordinate rather than edit". Detail below.

| # | item | outcome | pin |
|---|---|---|---|
| 1 | autorun key | **STOPPED** — reported, not edited | — |
| 2 | `/mem` partial name | FIXED | `test_eq_action_handler_spell_pet.cpp` (4 new), `test_spell_database.cpp` (2) |
| 3 | reply-to-tell sentinel | **action FIXED, key still unwired** | `test_zone_chat.cpp` (1 e2e), `test_action_dispatcher.cpp` (2), `test_input_action_bridge.cpp` (2) |
| 4 | interact world object | FIXED | `test_interact_world_object.cpp` (4, new binary), `test_action_dispatcher.cpp` (3), `test_input_action_bridge.cpp` (2) |

### 1. Autorun — STOPPED, and why (this is the finding, not an excuse)

Measured, not argued:

- **`EverQuest::StartMoveForward()`, `StartMoveBackward()`, `StartTurnLeft()`,
  `StartTurnRight()` have ZERO callers** anywhere in `src/`, `tests/` or
  `tools/`. Only the `Stop*` half is reachable, from
  `EqActionHandler::stopAllMovement()`. The game-side keyboard-movement system
  (`UpdateKeyboardMovement()` and the `PlayerState` move flags) can be turned
  **off** and never **on**. That is the reachability pattern again, on the very
  API the stub sits next to.
- **A handler-side autorun cannot work in graphics mode.**
  `UpdateKeyboardMovement()` writes position through `SetPosLocal()`, which is
  documented as deliberately firing **no** `PlayerMoved` event — so the renderer
  never sees the movement, keeps its own camera where it was, and its
  authoritative `PlayerPositionChanged` intent overwrites the game position on
  the next frame. Implementing autorun in `EqActionHandler` would produce
  invisible movement plus a fight with the renderer.
- **The one live autorun consumer already exists and is never written:**
  `renderer_player_physics.cpp:63`,
  `playerMovement_.moveForward = forward || playerMovement_.autorun;`
  `PlayerMovementState::autorun` (`irrlicht_renderer.h:300`) has no writer
  anywhere. So the feature is one setter away — in slot3's files this dispatch.
- **A local flip at `irrlicht_renderer.cpp:311` would be wrong** even ignoring
  territory: that switch runs before the `chatFocused` filter in
  `GraphicsInputHandler`, so typing a backtick in chat would start you running.
  The toggle has to come back from the game side after that filter, i.e. a new
  bridge event -> `IrrlichtBridge` -> a renderer setter.

Shape of the fix for whoever picks it up (all outside this unit's territory):
new `AutorunChanged{bool}` game event published by `EqActionHandler` and
consumed by `IrrlichtBridge` into `IrrlichtRenderer::setAutorun()`;
cancel-on-backward in `updatePlayerMovement()`; `stopAllMovement()` clears it.

**Semantics, and what decided them.** I did *not* settle these by argument and
I did not settle them against the oracle either: the official client needs a
live eqemu to reach a zone, and **eqemu was held by slot1 (T311 Leg B) for this
whole dispatch**, so the oracle could not be checked cheaply. Stating that
plainly rather than inventing a citation. The conservative set I would
implement (conservative = biases toward the character *stopping*, since a stuck
autorun runs you off a cliff): cancel on the toggle key again, on the backward
key, on `stopAllMovement()`/`/stopall`, on zoning and on death. Zoning and
death currently have **no hook at all** for this — `moveForward` is never
cleared on either — which is a second reason the item is bigger than the stub
line suggests.

### 2. `/mem <name>` — FIXED

`WilleqDatabase::findSpellsByNameLike(partial, limit)`: `SELECT id, name FROM
spells WHERE name LIKE ?1 ESCAPE '\' ORDER BY length(name), name LIMIT ?2`.

- The pattern is **bound**, never concatenated.
- `%`, `_` and `\` in the player's text are escaped before the pattern is
  built. Measured against the shipped `data/willeq.db` (8066 rows): exactly
  **1** spell name contains a literal `%` ("Damage Test %") and **5** contain a
  literal `_`. Unescaped, either character would return the row cap. The test
  asserts every returned name literally contains the typed character and that
  the result count stays under the cap — content-independent, so it does not
  rot if the table changes.
- Ordered shortest-name-first so an (almost) exact hit sorts to the front;
  capped at `SpellDatabase::kMaxNameSearchResults = 50`.
- Only `id`+`name` are selected; `SpellDatabase::findSpellsByName()` routes
  every id back through `getSpell()` so results come out of the existing LRU
  cache rather than a second 90-column SELECT.
- **Multiple matches: unchanged behaviour, deliberately.** `/mem`'s consumer
  (`EqActionHandler::memorizeSpellByName`, ported verbatim from the game's own
  `/mem` in T101) already did exact -> single partial -> "Multiple matches
  found:" + first five + "... and N more". That was already the reference
  behaviour; it was simply never reachable because the partial search returned
  nothing. It is now reachable and untouched.

*User-path pin:* `MemorizeSpellByName_PartialName_ResolvesAndMemorizes` drives
`EqActionHandler::memorizeSpellByName()` with a **lowercased partial** of a real
spell name against the real DB, first asserting the exact-match query returns
`nullptr` for that same string (i.e. this is exactly the input that used to
fail). Memorisation is timed (`SpellManager::updateMemorization` finishes it on
a later tick), so the assertion is `isMemorizing()` plus the
`SpellMemorizeVisualStarted` bridge event naming the resolved spell — not the
gem contents.

### 3. Reply-to-tell — action fixed; the key is still not wired, and that is a
### separate, larger finding

`ActionDispatcher::openReplyToTell()` now returns
`Success("/tell <Name> ")` — the raw wire name, because that text is meant to
be sent back as a command and that is the name the server matches, exactly what
`replyToLastTell()` passes to `SendChatMessage()` — or
`Failure("You have no one to reply to.")`, the same wording `/reply` refuses
with (T222). New pure query `IActionHandler::getLastTellSender()`; the store it
reads (`EverQuest::m_last_tell_sender`) has existed since T18, so nothing new
was invented. The plan's note was right: the "when ChatState is added to
GameState" premise was stale *and* was never the blocker.

**Found while doing it — the action has no producer and its result has no
consumer.** Both are in T315's territory this dispatch, so both are reported,
not touched:

1. `HotkeyAction::ReplyToTell` (R key, `hotkey_manager.cpp:508`) has **no
   `case` in `IrrlichtRenderer`'s HA switch** (`irrlicht_renderer.cpp:297-392`)
   and there is no `RendererAction::ReplyToTell` enumerator.
   `ConsoleInputHandler` never raises `InputAction::ReplyToTell` either. So in
   production **nothing produces the action** — pressing R does nothing at all,
   which is one layer below the defect the board recorded.
2. `InputActionBridge::setActionCallback()` has **no production caller** (only
   `test_input_action_bridge.cpp` sets one), so every `ActionResult` message
   from the keybind path — this prefill included — reaches only a `LOG_DEBUG`.
   Nothing can open a chat input with it until a consumer exists.

So the action is now correct and pinned, but the *user-visible* half needs a
renderer-side leg. Boarded on item 13 in `refactor/deferred.md` with both
specifics.

### 4. Interact world object — FIXED

`interactNearestWorldObject()` now follows `clickNearestDoor()`'s shape.
The nearest-object query could not go where the door one goes: world objects
live in `EverQuest::m_world_objects`, not in `GameState`, so there is nothing on
`m_state` to query — the old TODO's premise ("when ObjectState is added to
GameState") was never the blocker. It is a pure query on the handler,
`IActionHandler::findNearestWorldObject(float& outDistance)`, the same shape
T222 used for `hasTellToReplyTo()`. Returns drop id `0` for "nothing in range"
(eqemu allocates ground-spawn ids from 1). Range is **50 units, taken from
`IrrlichtRenderer::getNearestWorldObject()`'s own default** rather than
invented; the server re-validates the click anyway.

**Found and fixed alongside (same defect class, same function family):**
`interactNearest()`'s comment has always said "door, object, or NPC" and the
renderer's generic interact tries door-then-object, but the dispatcher's
unified interact **never checked objects** — standing at a forge with no door
and no NPC nearby, it refused. It now ranks the object by real distance against
the door and the NPC. (This is why `findNearestWorldObject()` reports a
distance rather than just an id.)

*User-path pin:* `tests/test_interact_world_object.cpp` (new binary) replays
**T144's real captured forge `OP_GroundSpawn` bytes** into a real `EverQuest`,
then drives `ActionDispatcher::interactNearestWorldObject()` — success with the
forge 3 units away, `Failure("No object nearby")` with an empty world and with
the same forge 51 units away. `test_input_action_bridge.cpp` covers the same
two outcomes from `InputAction::InteractWorldObject`, i.e. from the key press.

### Verification

| step | result |
|---|---|
| `cmake --build build -j24` | **PASS** (EXIT=0) |
| `ctest -j6` | **3404 / 3405** — sole failure `AudioManagerTest.PreloadCommonSounds`, the documented ALSA host quirk and the only entry in `refactor/test_baseline.txt`. Suite grew 3396 -> 3405 (+9 net registrations from this unit's tests). |
| `scripts/build-gles2-x86.sh` | **PASS** (EXIT=0) |
| Visual gate | **PASS**, `=== VISUAL GATE: PASS ===`; `git status` shows **no golden touched** |
| `scripts/build-arm-noble.sh` | **PASS** (EXIT=0) |
| `check_file_sizes.sh fable` | **no new FAIL.** The one FAIL (`irrlicht_renderer.cpp`, 3273 > 3000) is on a file this unit never opened; the two WARNs (`action_dispatcher.cpp` 2039->2081, `eq_action_handler.cpp` 2202->2248) were already over the 1500 line at the base rev. |

Evidence is **headless throughout** — eqemu was held by slot1 for the whole
dispatch, so there is no live leg and nothing here is labelled as one.

### Deviations

- **Autorun not implemented** (the plan's sanctioned STOP). Reason and the
  exact remaining work are in section 1 and on item 10 of
  `refactor/deferred.md`.
- **Reply-to-tell delivered at the action layer only**, not "opens the chat
  input prefilled": the key has no producer and the result has no consumer, both
  in T315's files. Section 3.
- **`interactNearest()` was touched** although the plan named only
  `interactNearestWorldObject()`. Same function, same defect, ten lines apart,
  and its own comment already promised the behaviour. Pinned.
- No file in `src/client/input/`, `eq_movement.cpp`, `eq_bridge.cpp`,
  `eq_entities.cpp`, `spell_cast_checks.cpp`, `irrlicht_renderer.*` or
  `renderer_frame.cpp` was modified.

### Deliberately not fixed (found, boarded, out of scope)

- `EverQuest::StartMove*`/`StartTurn*`: four public methods, zero callers
  (section 1).
- `InputActionBridge::setActionCallback()`: no production caller, so every
  keybind-path `ActionResult` message is discarded after a debug log.
- `HotkeyAction::ReplyToTell` has no `RendererAction` mapping — the R key is
  inert before it reaches any of this.
