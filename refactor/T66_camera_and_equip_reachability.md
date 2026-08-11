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

### Where each command was registered, and why that registry

**`/camera`** — added to `IrrlichtRenderer::processSlashCommand()`
(`src/client/graphics/irrlicht_renderer.cpp`), the renderer-side registry
(dispatch "registry 3" per T29's naming) that `/screenshot`, `/sort`,
`/portal`, etc. already live in. This is the ONLY registry that runs on the
render thread with the render context current — camera state
(`cameraMode_`) is renderer-owned, and `setCameraMode()` is the exact
function the plan named as the seam. Reached exactly the way `/screenshot`
is: unrecognized in registry 1 (`EverQuest::m_command_registry`) and
registry 2 (`action::CommandProcessor`), so `Application::
dispatchToActionOrRenderer()` fire-and-forgets it onto the bridge as a
`RendererCommand` event, drained on the render thread by
`IrrlichtBridge::applyEvent()`, which calls `renderer_->
processSlashCommand()` — unchanged dispatch path, no new plumbing added.

**`/equip`** — added as `EverQuest::RegisterEquipmentCommands()`
(`src/client/eq_commands.cpp`), called from `RegisterCommands()` alongside
`RegisterVendorCommands()`. This is registry 1, the same registry `/vendor`,
`/buy`, `/sell` (T48) and `/bank`, `/train` (T50) live in — the plan's
"scope discipline" fence names `eq_commands*.cpp` as this unit's territory,
and `/equip` is a plain game-thread command with no renderer dependency
(`SendMoveItem` is pure network logic), so it belongs with its siblings
rather than in the renderer registry. `inventory_manager.h`/
`inventory_constants.h` were un-gated from `#ifdef EQT_HAS_GRAPHICS` in this
file (both are graphics-independent — confirmed against `eq.cpp`'s own
unconditional include of both) so `/equip` compiles and works in an
`EQT_GRAPHICS=OFF` build too, matching the T29/T43/T50 precedent of not
gating graphics-independent code behind that flag. This is a genuine (if
narrow) deviation from a literal "touch nothing outside the command layer"
reading — flagged here: it is an `#include`-placement change only, zero
behaviour change for any existing `EQT_HAS_GRAPHICS=ON` build (which is
100% of this unit's verification bar), and was necessary for `/equip` to be
reachable in a no-graphics build at all.

### STOP-condition check: no capability turned out to need changes outside the command layer

Both `setCameraMode()`/`getCameraModeString()`/`cycleCameraMode()` and
`SendMoveItem()` were callable exactly as the plan asserted, with no gaps
found in the underlying implementation. Neither command needed the STOP
condition. The one adjustment (`/equip`'s include un-gating, above) was
inside the command layer, not the capability itself.

### `/camera` round-trip evidence (headless, end to end)

`tests/test_irrlicht_camera_command.cpp`, 12 cases, all passing. This is a
genuinely headless, end-to-end test with **no Irrlicht device at all**: a
default-constructed, never-`Init()`'d `IrrlichtRenderer` is safe to
construct/destroy/drive (constructor is empty; every function this command
touches — `setCameraMode()`, `cycleCameraMode()`, `getCameraModeString()` —
only reads/writes `cameraMode_` and null-guards `entityRenderer_`; the
destructor's `shutdown()` null-guards every member it touches). The test
therefore exercises the REAL `processSlashCommand()` dispatch into the REAL
setter/getter, not a seam or mock of either.

Coverage:
- `DefaultModeIsFollow` — asserts the declared default before any command.
- `NoArgReportsCurrentModeViaChat` — `/camera` with no argument →
  `"Camera mode: Follow"` via the same `chat()`/`SystemMessage` path
  `/screenshot` uses.
- `SetThenReadRoundTrips` (parameterized ×8): set via `/camera <arg>`, read
  back with a **separate, later** no-arg `/camera` call (proves the mode
  persisted in renderer state, not just in the immediately preceding
  reply), for all three modes using the EXACT strings `getCameraModeString()`
  produces (`"Free"`, `"Follow"`, `"First Person"` — the plan's literal bar),
  plus compact/case-insensitive aliases (`"free"`, `"FOLLOW"`,
  `"firstperson"`, `"fp"`, `"first-person"`) that still round-trip against
  the canonical getter string.
- `CycleAdvancesFreeFollowFirstPersonFree` — `/camera cycle` three times
  from the Follow default walks Follow→FirstPerson→Free→Follow, matching
  `cycleCameraMode()`'s own switch exactly.
- `UnknownModeName_PrintsErrorMessage_DoesNotChangeMode` — an unrecognized
  argument leaves `cameraMode_` unchanged and names the bad argument in the
  reply.

This directly answers T63's original blocker: a script can now do
`/camera "First Person"` (or `/camera fp`), assert the mode via a
subsequent `/camera` read, act, and assert the mode again — no more "T63
could not produce any first-person evidence."

### `/equip`'s validation behaviour and test coverage

`tests/test_eq_commands_equip.cpp`, 12 cases, all passing, using the same
`MakeEq()`/`TestBridge`/`ProcessChatInput` seam T48/T50 established.

Validation order: (1) both tokens must be present and parse as integers
with no trailing garbage (same consumed-length discipline as T48's
`ParseVendorSlot`) — else `"Usage: /equip <fromSlot> <toSlot> ..."`, no
intent pushed; (2) each parsed slot must be a *recognized* inventory
location — equipment/general/bag/cursor/bank/shared-bank/bank-bag/
shared-bank-bag/trade/trade-bag (`IsRecognizedInventorySlot()`, a
free-function mirror of `InventoryManager::isValidSlot()`'s exact
OR-chain — that method is `private`, an internal helper for the UI's own
drag/drop path, so this reproduces its logic over the same public
`inventory_constants.h` predicates rather than widening its access for one
caller) — else `"Invalid source/destination slot: N"`, no intent pushed.
Passing validation pushes `MoveItemIntent{fromSlot, toSlot, quantity=0}`
(0 = move entire stack, `MoveItemIntent`'s own documented default) onto
the bridge — the SAME struct the inventory UI's drag-and-drop gesture
pushes, drained by the same unmodified `ProcessBridgeIntents()` →
`SendMoveItem()` dispatch — and a confirmation naming both slots (via
`getSlotName()` when the slot is a canonical equipment slot, e.g.
`"Primary (13)"`, else just `"slot N"`).

Covered directly: no-args and missing-second-slot usage errors;
non-numeric and trailing-garbage token rejection; out-of-range source and
destination slot rejection (message names the offending slot number);
negative-slot rejection (confirms `SLOT_INVALID`'s own sentinel value
isn't accidentally treated as valid just because it parses); the plan's own
worked example (`/equip 22 13`, general→primary) asserted both for the
exact `MoveItemIntent` fields pushed and for the slot-name confirmation
text; the reverse direction (unequip, primary→general); cursor→secondary
(pick-up-then-equip); and a non-equipment pair (bank→general) confirming
`/equip` is a generic slot-mover, not equip-only, per the plan's "design it
around slots" instruction.

### The deferred live leg — stated plainly

**`/equip`'s live end-to-end leg is outstanding.** This unit ran under NO
EQEMU (sibling T65 held the lock this round, by the plan's own design), so
nothing here has watched a real `HC_OP_MoveItem` packet leave the wire, and
no real server has been asked to actually equip a real item. Everything
above stops at the command's reachability into the intent system — the
hop from `MoveItemIntent` through `ProcessBridgeIntents()` into
`SendMoveItem()` and the existing `ZoneProcessMoveItem` reply handler is
pre-existing, untouched code, exercised for real by neither this unit nor
its predecessor (nothing constructed `MoveItemIntent` from a command
before this unit, so `/equip` specifically has never been driven live).
**Do not read the passing unit tests as "`/equip` works live"** — they
prove the wiring, not the wire. Recorded on the board
(`refactor/deferred.md`, instance 8): the next unit that takes the eqemu
lock should equip a real weapon via `/equip` and confirm server ack plus a
visible appearance change before this instance is called closed, the way
T52 closed zoning.

`/camera` has no such gap — it is renderer-only, proven headlessly end to
end above, with no server dependency at any point.

### Deviations

1. Un-gated `inventory_manager.h`/`inventory_constants.h` from
   `#ifdef EQT_HAS_GRAPHICS` in `eq_commands.cpp` (see "Where each command
   was registered" above) — an include-placement change so `/equip`
   compiles/works in a no-graphics build, zero behaviour change for the
   `EQT_GRAPHICS=ON` tree this unit's entire verification bar runs against.
2. `InventoryManager::isValidSlot()` is `private`; rather than widen its
   access or reach into the class, `/equip`'s validation reproduces its
   exact logic as a free function (`IsRecognizedInventorySlot()`) over the
   same public predicates. If `isValidSlot()`'s logic ever changes, this
   copy needs updating too — flagged here rather than silently risking
   drift.
3. `/camera`'s mode-name parser accepts case-insensitive input and a few
   compact aliases (`firstperson`, `fp`, hyphen/underscore/space-insensitive
   matching) beyond the exact strings `getCameraModeString()` produces —
   the plan's bar ("accept the mode names getCameraModeString() already
   produces") is met exactly (see the `ExactGetterStrings` test-suite
   instantiation), and the aliases are additive, tested separately
   (`CompactAliases`), and do not change what the getter itself reports.

### Verification bar

1. `cmake --build build -j24` — PASS, clean build, no warnings from the new
   code.
2. `ctest --test-dir build --output-on-failure` — 1996/1997 passed; the
   sole failure is the pre-existing documented ALSA host exception
   (`AudioManagerTest.PreloadCommonSounds`). No regressions; 24 new tests
   (12 `/camera`, 12 `/equip`) all pass.
3. `bash scripts/build-gles2-x86.sh` — PASS.
4. Visual gate — PASS (`qeynos2`, `gfaydark`, `befallen` all verified);
   goldens untouched (this unit made no rendering changes).
5. `bash scripts/build-arm-noble.sh` — PASS, `willeq` and tool binaries
   built successfully for ARM.
6. `bash scripts/refactor/check_file_sizes.sh fable` (run from this slot on
   this task branch) — 3 files changed, 0 FAIL, 2 WARN (`include/client/eq.h`
   at 2064 lines, `irrlicht_renderer.cpp` at 2977 lines) — both pre-existing
   oversized legacy files (tracked separately in `refactor/F_file_split.md`),
   not newly pushed over either threshold by this unit's small additions.

### eqemu / Pi discipline

No eqemu use (NO EQEMU per the plan; T65 held the lock). No Pi use (T61
held it). Neither was needed or touched.

Board (`refactor/deferred.md`, append, never delete): add both to the
**"PATTERN: real features reachable only through a UI gesture"** entry as
instances 7 and 8, with the same framing the existing six use — the
capability existed, the producer did not. Note that instance 7 (camera)
blocked T63 from producing any first-person evidence at all, and instance
8 (equip) forced T63 into direct database writes. Both are concrete
evidence that the pattern is still producing, which that entry explicitly
says it expects.
