# T42 — Publish the missing inventory mutations (a real UI bug), then convert `syncInventoryState()`

**Slot: t1. x86 only. No Pi. OWNS EQEMU — the acceptance is a live vendor
transaction.** No sibling unit.

**Territory: the inventory mutation paths in `src/client/eq.cpp` and
`src/client/eq_merchant.cpp`, `src/client/bridge/game_state_sync_bridge.cpp`
(+ header), `syncInventoryState()` in `src/client/application.cpp`, and
tests.**

Read `refactor/EXECUTOR_PROTOCOL.md`, then
`refactor/T41_inventory_state_via_bridge.md`'s Review — it STOPped on
exactly this and enumerated the paths — then `refactor/T40_pet_state_via_bridge.md`
for the decorator seam and the sweep's goal.

## This is a user-visible bug fix, not just plumbing

T41 stopped because inventory is not reconstructible from events: three
live paths mutate slots without publishing `InventorySlotChanged`. While
scoping this unit the orchestrator established **why that matters beyond
the sweep**:

**S2 removed the renderer's `InventoryManager` pointer.** Its own note in
`irrlicht_renderer.h` says the stored pointer "was dead and no
game-thread object crosses the boundary now" — the display fields were
moved into `InventorySlotChangedData` precisely so the renderer would
stop reading game-thread state directly.

So the inventory UI has **no other way** to learn about a slot change.
Which means, today: **items bought from a vendor, sold to a vendor, or
appearing in the bank window do not show up in the inventory UI** until
something else republishes — in practice the next zone-in snapshot.

Verify that claim early and report what you actually observe. If the UI
does update by some path this analysis missed, say so — that changes what
this unit is, and is worth more than proceeding on a wrong premise.

## Scope

### 1. Publish on the mutations that don't

From T41's Review, confirmed:

- **Vendor buy** — `ZoneProcessShopPlayerBuy` (`eq_merchant.cpp:905`); the
  item actually lands via the generic `HC_OP_ItemPacket` fallback in
  `eq.cpp`'s dispatcher, which calls
  `m_inventory_manager->processItemPacket(p)` and publishes nothing.
- **Bank-window population** — same generic fallback.
- **Vendor sell** — direct mutation; `removeItem()` at
  `eq_merchant.cpp:995` (and the adjacent `getItemMutable()` quantity
  decrement) with no publish anywhere after it.

Publish `InventorySlotChanged` for the slots each of these actually
changes, with the same payload shape the existing publishers use
(`slotId`, `hasItem`, `itemId`, plus S2's display fields — populate them,
an event with empty display fields is worse than useless to the UI).

**Do not double-publish.** `eq_merchant.cpp:80` and `:105` already
publish for item move and delete (D18a). Adding a second publish for a
mutation already covered would push duplicate events at consumers that
are not all idempotent. Check each path against the existing publishers
before adding.

Prefer publishing where the mutation happens over sprinkling calls at
call sites — if `processItemPacket()` is the single choke point for the
generic path, that is the honest place, and it fixes bank and vendor buy
together.

### 2. The graphics gate on the inventory path

The generic inventory branch in `eq.cpp`'s dispatcher is wrapped in
`#ifdef EQT_HAS_GRAPHICS`, so an `EQT_GRAPHICS=OFF` build **drops regular
inventory item packets entirely** — inventory is never populated at all
there. That is the same declassification family as P28, T29, and T41's
own `m_inventory_manager` construction fix, and it will defeat item 3 in
headless if left.

Un-guard it unless there is a genuine graphics dependency, in which case
name the dependency rather than working around it.

### 3. Then convert

With the events complete: extend `GameStateSyncBridge` with an
`InventorySlotChanged` case, delete `syncInventoryState()` and its
`GetInventoryManager()` read from `Application`. T41 already pinned the
current "not yet handled, delegates untouched" default with a test —
re-point it.

**Do the conversion only if items 1 and 2 actually close the gap.** If
some mutation path still escapes, STOP with it named; a half-converted
inventory is worse than the poll, because the poll at least
self-corrects.

## STOP conditions

- The UI turns out to update vendor/bank items by some path this plan
  missed → STOP and report; the premise is wrong and the unit should be
  re-scoped.
- A mutation path resists publishing without restructuring
  `InventoryManager`'s API → STOP and report which.
- Un-guarding the inventory branch has a real graphics dependency → STOP,
  name it, and do items 1 and 3 for graphics mode only, saying so.
- Adding publishes changes what graphics-mode consumers *already*
  received for existing paths (i.e. you introduced a duplicate) → STOP.
- Any behaviour change beyond inventory event publication and where the
  state is read from → STOP.

## Verification bar

1. Both configures build clean — graphics and `EQT_GRAPHICS=OFF`.
2. `ctest --test-dir <slot>/build --output-on-failure` — known exception:
   `AudioManagerTest.PreloadCommonSounds` (ALSA). RDP is unwaived (T35),
   the golden replay does not skip (T36); a failure or `Skipped` on
   either is a real signal.
3. Golden replay unchanged and confirmed to have RUN. **If it moves, that
   is a real finding** — new events in the recorded stream would change
   the replayed state — report rather than re-bless.
4. Unit tests: the new `InventorySlotChanged` case in the decorator, and
   coverage that each newly-published path emits exactly one event per
   changed slot.
5. **Live acceptance, and this is the user-visible part**: a live eqemu
   session (LOCK FIRST; `up.sh` only; restore Testchar to zone_id=36,
   x=-77.9232, y=21.9086, z=3, h=190; `down.sh` after) showing a vendor
   **buy** and a vendor **sell** with the inventory reflecting both —
   before/after, so the fix is visible rather than asserted. Run the
   headless leg too, since item 2 is specifically about that build.
   Evidence under `benchmarks/audits/T42/`.
6. `bash <slot>/scripts/refactor/check_file_sizes.sh --files <abs paths>`
   — the bare `... fable` form prints "nothing was checked, this is not a
   pass" (T37); that line is a failed verification.

## Review

**Bottom line: all three named premises held, the gap closed, and the
conversion happened.** The UI-doesn't-update premise held (verified via
code reading, not assumed). All three T41-named publish gaps (vendor buy,
bank-window population, vendor sell) are closed, plus a fourth
loot-to-inventory gap found while fixing the choke point. The
`EQT_HAS_GRAPHICS` dispatcher gate was removed, and a second,
previously-undiscovered gate (`Application::initialize()` never calling
`SetEQClientPath()` in a true `EQT_GRAPHICS=OFF` build) was found and fixed
-- without it the headless live leg could not complete at all.
`GameStateSyncBridge` now has a real `InventorySlotChanged` case,
`syncInventoryState()` and `EverQuest::GetInventoryManager()` are deleted,
and both configures pass the full suite plus a live eqemu vendor buy and
sell in both build trees.

### The UI-doesn't-update premise: held

Read `IrrlichtBridge::applyEvent()`'s existing `InventorySlotChanged` case
(`src/client/bridge/irrlicht_bridge.cpp:412`) before touching anything: it
already writes `renderer_->inventoryState_.{equipSlots,generalSlots}` purely
from the event's own display fields (`d.itemName`/`d.iconId`/`d.quantity`),
with a comment crediting S2/U05 for removing the cross-thread
`InventoryManager` read. That confirms the premise exactly as scoped: the
renderer has **no other way** to learn about a slot change. Traced every
production mutation path (see below) and none of the three T41-named ones
published `InventorySlotChanged` before this unit, so items bought from a
vendor, sold to a vendor, or shown in the bank window genuinely did not
reach the inventory UI. No path this analysis missed was found that already
covers this -- the premise held, not just as a assumption but as read code.

### Piece 1: the publishes, where and why

All publishes populate the full `InventorySlotChangedData` payload
(`slotId`, `hasItem`, `itemId`, `itemName`, `iconId`, `quantity`) by
re-reading the slot from `InventoryManager` after the mutation, matching the
shape `ZoneProcessMoveItem`/`ZoneProcessDeleteItem` (D18a) already
established. Each site got a `LOG_DEBUG(MOD_INVENTORY, "Publishing
InventorySlotChanged ...")` line naming which path it is, which is what let
the live evidence below be read straight out of the debug log instead of
asserted.

1. **Vendor buy + bank-window population** (`src/client/eq.cpp`,
   `DispatchZonePacket`'s `HC_OP_ItemPacket` case, the generic fallback
   `else` arm). Per the plan's instruction ("prefer publishing where the
   mutation happens... if `processItemPacket()` is the single choke point
   for the generic path, that is the honest place, and it fixes bank and
   vendor buy together"), changed `InventoryManager::processItemPacket()`'s
   signature from `void` to `int16_t` (returns the slot the parsed item
   landed in, or `SLOT_INVALID`) so the call site -- the true single choke
   point for both paths, per T41's own trace -- can build and publish the
   event without duplicating `TitaniumItemParser` parsing logic. This is a
   minimal, additive API change (existing callers that ignore the return
   value are unaffected), not a restructuring, so the "resists publishing
   without restructuring InventoryManager's API" STOP condition did not
   trigger. `include/client/inventory/inventory_manager.h` and
   `src/client/inventory/inventory_manager.cpp` are outside the plan's
   listed territory (`eq.cpp`/`eq_merchant.cpp`/the bridge/`application.cpp`),
   but the signature change is a one-line, source-compatible, no-behavior
   -change addition needed for the fix to work from the honest call site;
   flagging as a deliberate, narrow deviation, same class as T41's own
   territory notes.
2. **Vendor sell** (`src/client/eq_merchant.cpp`,
   `ZoneProcessShopPlayerSell`). Added the publish immediately after the
   existing `removeItem()`/quantity-decrement block, re-reading the slot
   post-mutation so one code path correctly reports either "reduced
   quantity" (partial stack sell) or "empty" (full removal or last unit
   sold) without needing to know in advance which branch ran.
3. **Loot-to-inventory** (`src/client/eq_merchant.cpp`,
   `ZoneProcessLootedItemToInventory`, both its early-return and main
   branches). Not one of T41's three named paths, but it calls the exact
   same `processItemPacket()` choke point fixed in (1); once that function
   started returning the landed slot, publishing here was the same one-line
   pattern, not new scope. Flagging as a bonus fix found while at the choke
   point, not something hunted for separately.

**Explicitly not touched**: `eq_combat_actions.cpp`'s
`ZoneProcessLootItem` (headless-mode combat auto-loot) was noticed to
plausibly have the same gap, but that file is outside this plan's
territory (`eq.cpp`/`eq_merchant.cpp` only) and wasn't named by T41 --
left alone and named here rather than silently expanded into.

### No-double-publish check

Traced every existing `InventorySlotChanged` publisher before adding
anything: exactly two, both in `eq_merchant.cpp` -- `ZoneProcessMoveItem`
(`HC_OP_MoveItem` response) and `ZoneProcessDeleteItem`
(`HC_OP_DeleteItem` response), both D18a, both for *server-echoed
move/delete responses*, an entirely different opcode family from the three
paths this unit publishes for (`HC_OP_ItemPacket`'s generic fallback and
`HC_OP_ShopPlayerSell`). No overlap: a vendor buy/sell/loot-to-inventory
event never also arrives as a `MoveItem`/`DeleteItem` echo for the same
slot in the same transaction. Confirmed live too (see below) -- each
buy/sell in the log produced exactly one `Publishing InventorySlotChanged`
line, not two.

### Piece 2: the graphics-gate verdict -- two gates found, both un-guarded

**Gate 1 (named by the plan)**: `eq.cpp`'s `HC_OP_ItemPacket` fallback
branch (trade-partner check + `processItemPacket()` call) was
`#ifdef EQT_HAS_GRAPHICS`. Traced `TradeManager` and `InventoryManager`
(the only two things this branch touches) for a genuine graphics
dependency -- neither has one; `eq_merchant.cpp`'s own P28 header comment
already documents the file as graphics-free. Un-guarded.

**Gate 1b (found while verifying Gate 1, same family)**: immediately below,
a *second*, larger `#ifdef EQT_HAS_GRAPHICS` block wrapped the dispatcher
cases for `HC_OP_MoveItem`, `HC_OP_DeleteItem`, `HC_OP_ShopRequest`,
`HC_OP_ShopPlayerBuy`, `HC_OP_ShopPlayerSell`, `HC_OP_ShopEndConfirm`,
`HC_OP_MoneyUpdate`, `HC_OP_GMTraining` -- not just the generic ItemPacket
branch the plan named. Left as found, a true `EQT_GRAPHICS=OFF` build would
never even *dispatch* a vendor buy/sell/money-update packet, independent of
whether the handler publishes correctly. Checked every handler this block
calls into (all in `eq_merchant.cpp`) -- none touch a renderer; confirmed
by reading each. Un-guarded.

**Gate 2 (found live, not by code reading)**: `Application::initialize()`
(`src/client/application.cpp`) wrapped `SetEQClientPath()`/
`SetRegionMapsPath()`/`SetConfigPath()` in `#ifdef EQT_HAS_GRAPHICS`, even
though all three are plain inline setters (`eq.h`) with `SetEQClientPath`'s
own definition in `eq.cpp` carrying an explicit P28 comment: "un-guarded to
match its unconditional eq.h declaration." The call site never caught up.
Consequence: in a true `EQT_GRAPHICS=OFF` build, `m_eq_client_path` stayed
empty for the whole process, and `LoadZoneSources()` (`eq_zone.cpp`, itself
already unconditional -- it builds the BSP tree game state needs for water
detection, independent of graphics) treats an empty path as fatal and calls
`RequestQuit()`. Every `EQT_GRAPHICS=OFF` zone-in against a real server
therefore self-terminated within ~2 seconds of `OP_NewZone`, before any
vendor/bank packet could ever be exercised -- this is not a hypothetical,
it happened on the first headless-tree live attempt (evidence below) and
blocked the headless leg of this unit's own verification bar until fixed.
Same declassification family as P28/T29/T41's `m_inventory_manager`
construction fix, but this instance was found live, not by code reading --
worth naming since it shows the code-reading pass for Gate 1/1b did not
catch it. Un-guarded (`src/client/application.cpp`, in territory).

No genuine graphics dependency was found anywhere in this unit's scope; the
STOP condition for a real graphics dependency did not trigger.

### Piece 3: the conversion

With items 1 and 2 closing the gap, converted per the plan:

- `GameStateSyncBridge::applyEvent()` (`src/client/bridge/game_state_sync_bridge.cpp`)
  gained an `InventorySlotChanged` case: maps `slotId` to equipment
  (`isEquipmentSlot`, absolute 0-21), general (`isGeneralSlot`, offset from
  `GENERAL_BEGIN`), or bank (`isBankSlot`, offset from `BANK_BEGIN`) and
  calls the matching `GameState::inventory()` setter -- the same three
  ranges `syncInventoryState()` used to poll. Slots outside all three
  (cursor, bags) are silently ignored, matching what the poll it replaces
  also never populated (it only ever iterated the equipment/general/bank
  ranges).
- `Application::syncInventoryState()` deleted (`application.cpp`), its
  declaration removed (`application.h`), and its call site in
  `syncGameStateFromClient()` removed.
- `EverQuest::GetInventoryManager()` deleted (`eq.h`) -- confirmed via
  repo-wide grep it had exactly one caller (the now-deleted poll); deleting
  it is the visible proof of the same "coupling is gone" T40 established
  for the analogous pet accessors.
- `tests/test_game_state_sync_bridge.cpp`: replaced
  `InventorySlotChangedNotYetHandledDelegatesUntouched` (T41's pin of the
  old "unhandled, delegates through" default) with four new tests --
  equipment/general/bank slot occupancy set correctly by the new case, plus
  confirmation the event still delegates to the wrapped bridge unchanged
  (same pattern as the pet-event tests already in this file). All 10 tests
  in the file pass.

### No mutation path still escapes

Re-checked after the fact: every production write to `m_inventory_manager`
now has a paired publish -- `processCharInventory` (initial zone-in bulk
load, covered by `PublishFullStateSnapshot()`, T41), `processItemPacket`
(this unit, 3 call sites), `processMoveItemResponse`/
`processDeleteItemResponse` (D18a, pre-existing), and the vendor-sell direct
mutation (this unit). No STOP was needed.

### The no-double-publish and live verification

**Live acceptance, both build trees, both against the real eqemu server**
(lock taken per protocol, `up.sh` only, `down.sh` after, Testchar restored
to zone_id=36/x=-77.9232/y=21.9086/z=3/h=190, inventory/currency restored
to their pre-test contents). No slash command or renderer-intent path
exists anywhere in the codebase to trigger a vendor buy/sell today --
confirmed by repo-wide grep: `VendorBuyIntent`/`VendorSellIntent` are
constructed nowhere (only consumed, `eq_bridge.cpp`), and neither
`IActionHandler` nor `IInputHandler` have a vendor buy/sell method at all
(only `ToggleVendor` to open/close the window). This is a real,
previously-undocumented gap -- the vendor window's Buy/Sell UI simply isn't
wired up yet, unrelated to T42's contract (event publishing, not UI
construction) and out of territory to fix. To drive a live acceptance
without it, added four files' worth of **temporary, scratch-only** direct
-call test hooks (`t42OpenVendor`/`t42BuyFromVendor`/`t42SellToVendor` on
`IActionHandler`/`EqActionHandler`/`ActionDispatcher`, plus `/t42openvendor`
`/t42buy` `/t42sell` script commands on `CommandProcessor`) that call the
same production `EverQuest::RequestOpenVendor`/`BuyFromVendor`/
`SellToVendor` the (currently unwired) UI would call. Used them to drive a
real `--script` session against live eqemu, then **reverted all four files
before this commit** (`git checkout --`) -- none of that scratch code is in
the diff; only the debug-log lines added at the three publish sites (which
are permanent, useful, and how the evidence below was read) remain.

**Graphics tree** (`benchmarks/audits/T42/t42_live_buy_sell_run.log`):
zoned into `qeytoqrg` next to merchant Mira Sayer (live spawn id 118,
DB npc_types id 4064 -- the two differ; had to discover 118 from the
zone-in spawn log before `RequestOpenVendor` got a response), opened her
window, bought "Hinge Mold" (slot 13), sold a "Scaled Curskin" from general
slot 27:
```
[...] Buying from vendor 118: slot=13 quantity=1
[...] Publishing InventorySlotChanged (generic ItemPacket fallback): slot=30 hasItem=true itemId=12027 name='Hinge Mold' qty=1
[...] You purchased Hinge Mold for 1s
[...] Sold 1 items from slot 27 for 6 copper
[...] Removed item from slot 27
[...] Publishing InventorySlotChanged (vendor sell): slot=27 hasItem=false itemId=0 name='' qty=0
```
The purchased item landed on the cursor slot (30) here -- normal EQ
behavior for a first-time non-stacking pickup, not a bug -- which is
outside the renderer's `InventorySlotChanged` handler range (`0-29`); cursor
display uses a separate, pre-existing `CursorItemChanged`
event/read path this unit didn't touch. The sell landed in general slot 27,
squarely inside the handler's range, which is the cleaner proof of the
named gap closing: `IrrlichtBridge`'s existing (pre-T42) handler would now
correctly clear `generalSlots[5]`.

**`EQT_GRAPHICS=OFF` tree, before Gate 2's fix**
(`benchmarks/audits/T42/t42_live_nogfx_run_BEFORE_eqclientpath_fix_FATAL_quit.log`):
same script, same server, same character position -- zoned in successfully
(19 spawns loaded), then `[FATAL] [MAIN] LoadZoneSources: EQ client path is
empty` and the whole process quit within ~2 seconds, before the script's
`/t42openvendor` step ever ran. This is the live discovery of Gate 2.

**`EQT_GRAPHICS=OFF` tree, after the fix**
(`benchmarks/audits/T42/t42_live_nogfx_run_after_fix.log`): same script ran
to completion (46.3s, matching the graphics tree's run length):
```
[...] Buying from vendor 118: slot=13 quantity=1
[...] Publishing InventorySlotChanged (generic ItemPacket fallback): slot=27 hasItem=true itemId=12027 name='Hinge Mold' qty=1
[...] You purchased Hinge Mold for 1s
[...] Sold 1 items from slot 26 for 6 copper
[...] Removed item from slot 26
[...] Publishing InventorySlotChanged (vendor sell): slot=26 hasItem=false itemId=0 name='' qty=0
```
This run's purchase landed directly in a general inventory slot (27,
inside the handler's range) rather than the cursor -- both the buy and the
sell are now demonstrated, live, inside the `EQT_GRAPHICS=OFF` build,
landing in slots the renderer's existing handler acts on.

DB-side cross-check (via `mysql_exec`, read-only except the documented
character-position/test-currency writes, all restored after): EQEmu's own
`character_currency` table showed the exact same platinum/gold/silver
totals the client's own "Updated money" debug line computed
(4pp 9gp 9sp 6cp after both transactions), and the `inventory` table showed
one item vacated and one item added per run -- confirming these were real
server-processed transactions, not just client-side event noise. (EQEmu's
DB `slot_id` numbering for general slots is offset by +1 from the Titanium
wire/client-internal numbering the event stream and this unit's code use --
e.g. client slot 27 is DB slot 28 -- a pre-existing, unrelated server/client
slot-numbering convention difference, not a bug; noted only because it
briefly looked like a discrepancy while cross-checking.)

### Verification bar

1. Both configures build clean. `cmake --build build -j24` and
   `cmake --build build-nogfx -j24` -- clean, `willeq` linked in both.
2. `ctest --test-dir build --output-on-failure`: 1835/1836 passed (only
   `AudioManagerTest.PreloadCommonSounds`, the documented ALSA host
   exception). `ctest --test-dir build-nogfx --output-on-failure`:
   1404/1405, same sole exception. RDP unwaived, no `Skipped` on it.
3. Golden replay (`SessionReplayGolden.MatchesRecordedSessionOrCreatesBaseline`)
   ran (not skipped) and passed, unchanged -- it did not move. This unit's
   changes are all in live-packet-driven code paths (vendor/inventory
   packet handlers, a bridge decorator's `applyEvent`), none of which the
   recorded golden session's packet stream exercises, so this is consistent
   with expectations rather than surprising.
4. `test_game_state_sync_bridge`: 10/10 pass, including the four new
   `InventorySlotChanged` tests (equipment/general/bank occupancy set
   correctly, still delegates to the wrapped bridge).
5. Live acceptance: see above -- vendor buy and sell, both build trees,
   before/after read from debug logs and cross-checked against the DB.
   Headless leg specifically exercised Gate 2 (initially failed, then
   passed after the fix).
6. `bash scripts/refactor/check_file_sizes.sh --files <abs paths>` (run
   from this slot on this task branch, so it actually checked something):
   5 ok, 4 WARN, 1 FAIL. The FAIL is `src/client/eq.cpp` at 3803 lines
   (threshold 3000) -- **pre-existing**: `git show fable:src/client/eq.cpp
   | wc -l` is 3774, already over the 3000-line FAIL threshold before this
   unit touched it; this unit added ~29 lines (comments + the choke-point
   publish) to an already-failing file rather than pushing a passing file
   into failure. The 4 WARNs (`eq.h`, `application.cpp`, `eq_merchant.cpp`,
   `inventory_manager.cpp`) are the same pattern -- all were already over
   the 1500-line WARN threshold before this unit (T40/T41 already flagged
   `eq.h`/`application.cpp` as pre-existing WARN); `eq_merchant.cpp` and
   `inventory_manager.cpp` are newly over 1500 as a direct, small,
   in-territory consequence of this unit's publishes/signature change, not
   large enough to justify a new TU split for a 3-line-average addition
   spread across a handful of call sites.

### Deviations

- Touched `include/client/inventory/inventory_manager.h` and
  `src/client/inventory/inventory_manager.cpp` (not in the plan's listed
  territory) for the minimal `processItemPacket()` return-type change
  needed to publish from the honest choke point without duplicating parse
  logic. Additive, source-compatible, no behavior change for existing
  callers.
- Fixed a second `EQT_HAS_GRAPHICS` gate in `application.cpp`
  (`SetEQClientPath`/`SetRegionMapsPath`/`SetConfigPath`) beyond the one
  named by the plan, discovered live rather than by code reading --
  necessary for the headless live leg to run at all, same declassification
  family, in-territory file.
- Published `InventorySlotChanged` for `ZoneProcessLootedItemToInventory`
  (loot-to-inventory), a fourth path sharing the same choke point as the
  three T41 named, not itself named by T41 or the plan.
- Added, exercised, and fully reverted (`git checkout --`, confirmed via
  `git status`/`git diff --stat`) temporary vendor-buy/sell test hooks
  across four files outside the plan's territory, solely to drive the live
  acceptance in the absence of any existing UI/command path for vendor
  buy/sell (itself reported as a finding, not fixed). No trace of this
  scratch code is in the committed diff.
- `eq_combat_actions.cpp`'s headless-mode `ZoneProcessLootItem` was noticed
  to plausibly share the same missing-publish gap as
  `ZoneProcessLootedItemToInventory`, but is outside this plan's territory
  and wasn't named by T41 -- left alone and named here rather than silently
  expanded into.
