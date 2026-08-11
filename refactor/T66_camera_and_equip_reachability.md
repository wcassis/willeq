# T66 — Two capabilities exist and nothing can reach them: camera mode and equipping

**Slot: t3. x86 only. No Pi. NO EQEMU** — sibling T65 owns the server this
round. See "The deferred live leg" below; this is deliberate, not an
oversight.

**Territory: the command registries and their tests — `src/client/eq_commands*.cpp`,
`src/client/graphics/irrlicht_renderer.cpp`'s slash-command handler
(around `:1036`, where `/screenshot` lives),
`include/client/graphics/ui/command_registry.h` if registration belongs
there, and `tests/`.**

Do not touch `bone_attachment.h`, `character_model_view.cpp`,
`entity_equipment.cpp` or the capture tools — sibling T65 owns those. Do
not touch the GLES2 driver — sibling T61 owns it. `refactor/deferred.md`
is append-only and shared.

Read `refactor/EXECUTOR_PROTOCOL.md`, then `refactor/deferred.md`'s entry
**"PATTERN: real features reachable only through a UI gesture"** — this
unit adds instances 7 and 8 to a list the project has been tracking since
T26, and the entry records how T34, T48, T50 and T52 each closed one.

## Why this unit exists

T63 ran a live verification session and hit two hard walls, both the same
shape: **the capability is fully implemented, and no producer reaches it.**

**1. Camera mode.** `IrrlichtRenderer::cycleCameraMode()`
(`irrlicht_renderer.cpp:2591`) and `setCameraMode()` exist, with
`CameraMode::{Follow, FirstPerson, ...}` and `getCameraModeString()`. The
only path in is `HA::ToggleCameraMode` (`:296`) — a **raw Ctrl+F5 key
event**. A scripted or piped session can never enter first person.

Consequence: T63 could not produce **any** first-person evidence. Its
first-person-during-swing captures — a third of what Will asked for — are
simply missing, and it correctly STOPped rather than inventing a
scripting affordance. That question is still open and cannot be answered
until this lands.

**2. Equipping.** `EverQuest::SendMoveItem(fromSlot, toSlot, quantity)`
(`include/client/eq.h:1928`, opcode `HC_OP_MoveItem = 0x420f`) is the real
protocol path and it is implemented. Nothing invokes it from a command.

Consequence: T63 had to write `inventory.item_id` and `character_skills`
**directly into the database** and restart the client to get a character
holding a weapon at all — flagged as a deviation beyond its remit. Every
future equipment-rendering unit pays that same cost, and DB-poking is a
worse test than the real path because it bypasses the very packet flow a
real equip uses.

**Neither of these is a new feature.** Both are thin command wrappers over
functions that already work — the same move T34 made for `/loot`, T48 for
vendor buy/sell, and T52 for `/zoning`.

## Part 1 — `/camera`

Expose the existing camera-mode API through the command channel. Shape it
to be scriptable and deterministic:

- `/camera` with no argument should report the current mode
  (`getCameraModeString()`), so a script can assert where it is rather
  than assuming.
- `/camera <mode>` should set a **named** mode explicitly. Prefer explicit
  naming over exposing only `cycleCameraMode()` — a script that has to
  cycle blind and count is fragile, which is precisely the trap T51
  documented for `/attack`. Cycling may additionally be exposed if it is
  free.
- Accept the mode names `getCameraModeString()` already produces, so
  set-then-read round-trips.

`/screenshot` (`irrlicht_renderer.cpp:1036`) is the model — it is a
renderer-side slash command that already works from a script, and it shows
where a renderer-owned command is registered and how it replies via
`chat()`. Follow it.

**Note the thread constraint**: camera state is renderer-owned. `/screenshot`
already documents the same requirement (`saveScreenshot` must run on the
render thread). Do the same thing it does; do not invent a new path.

## Part 2 — `/equip`

Expose `SendMoveItem` so a session can equip from a script.

Design it around **slots**, since that is what the protocol speaks:
`/equip <fromSlot> <toSlot>` is honest and directly testable. A
friendlier item-name form is optional and secondary — if you add one,
build it on the slot form rather than duplicating the path.

Use `include/client/inventory/inventory_constants.h`'s `EquipmentSlot`
enum (`SLOT_PRIMARY = 13`, `SLOT_SECONDARY = 14`, …) and `getSlotName()`
for parsing and replies. **Do not invent slot numbering** — T57 established
that this enum is the canonical source and the UI already indexes by it.

Validate before sending: reject an out-of-range slot with a clear message
rather than emitting a malformed packet. Report the outcome via `chat()`
so a script can see what happened.

**Scope discipline.** This is a wrapper over an existing send. Do **not**
implement inventory management, swap semantics, stack splitting, or
client-side prediction of the result. The server is authoritative; the
command sends the move and the existing `ZoneProcessMoveItem` path handles
the response.

## The deferred live leg — state it, do not hide it

`/equip` cannot be *fully* proven without a server, and sibling T65 owns
the eqemu lock this round. That is a deliberate scheduling choice.

So: unit-test the wiring headlessly — command registered, arguments
parsed, invalid slots rejected, the right function invoked with the right
arguments (a seam or a test double, not a live send). Then **say plainly
in your Review that the live end-to-end leg is outstanding**, and record it
on the board so the next unit that takes the server picks it up. Do not
claim `/equip` works live when nothing has watched a packet go out.

`/camera` has no such limitation — it is renderer-only and can be proven
headlessly end to end. Do prove it: set each mode from a script, read it
back, and show the round-trip.

## STOP conditions

- Either command needing changes outside the command layer to work — that
  means the capability is not as reachable as this plan assumes; report it.
- Any urge to build inventory management behind `/equip`.
- Any urge to take the eqemu lock. T65 has it.

## Verification

Full protocol bar. Additionally: a scripted round-trip proving `/camera`
sets and reports each mode; unit tests for `/equip`'s parsing, validation
and dispatch; and an explicit statement of what remains unproven without a
server.

## Review

(executor fills: where each command was registered and why that registry;
the `/camera` round-trip evidence; `/equip`'s validation behaviour and
test coverage; exactly what is deferred to a live session; deviations)

Board (`refactor/deferred.md`, append, never delete): add both to the
**"PATTERN: real features reachable only through a UI gesture"** entry as
instances 7 and 8, with the same framing the existing six use — the
capability existed, the producer did not. Note that instance 7 (camera)
blocked T63 from producing any first-person evidence at all, and instance
8 (equip) forced T63 into direct database writes. Both are concrete
evidence that the pattern is still producing, which that entry explicitly
says it expects.
