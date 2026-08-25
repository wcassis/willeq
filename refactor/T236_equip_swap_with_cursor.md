# T236 — /equip swap-with-cursor: client applies a move the server rejects

Slot: slot3. x86. OWNS eqemu briefly (noclobber) — after T235 if contended.
Class A/B (T230 finding; same class as T82's "doMove swap branch never
validates the displaced item", deferred.md). Territory: InventoryManager
move/swap logic (src/client/inventory/*), the /equip command's pre-flight
(command_processor equip path — T141), eq_merchant.cpp MoveItem send only,
tests. Do NOT touch action/eq_action_handler* or test_eq_action_handler_*
(T234 unmerged), renderer_frame.cpp (T235).

## Defect
`/equip 30 22` (cursor → general slot 22, which was occupied) reported a
client-side swap; the server never applied it and the DB still showed the
old layout (`review/T230/` runs 3-4, db_* snapshots). T82 found the sibling:
`doMove()`'s swap branch never validates the displaced item against the
slot it lands in, the server rejects, and the client self-heals via
desync/resync (no corruption, but a lie in the UI until resync).

## Scope
1. Reproduce headless with a captured or constructed server rejection:
   cursor item + occupied target → client state after the move vs. the
   server's reply. Determine the exact rule eqemu applies
   (/mnt/projects/akk-stack/code/zone/inventory.cpp SwapItem / MoveItem:
   cursor rules, `IsValidSlot`, class/race/size checks for the displaced
   item).
2. Add the same validation to the swap branch's PRE-FLIGHT (T77 style:
   reject locally with the official message, send nothing) so the client
   never applies a move the server would reject. Keep the resync path.
3. Tests: displaced item invalid for its destination → local rejection, no
   packet; valid swap → packet as before. Red/green.
4. Live: the T230 fixture (`#summonitem` to cursor, `/equip 30 22` into an
   occupied general slot) → either the swap applies on BOTH sides (DB row
   moves) or the client refuses with a message and the DB is unchanged.

## Verification
Full ctest; Review; evidence review/T236/. Commit:
`T236: swap-with-cursor moves are validated before they are applied`.

## Review (executor, 2026-08-25, slot3, branch task/T236-equip-swap off fable ea2f7ca6)

### 1. The eqemu rule (Scope 1)

`Client::SwapItem()` (`zone/inventory.cpp:1601`) does no legality of its own
for a whole-item move (`number_in_stack == 0`); it delegates to
`EQ::InventoryProfile::SwapItem()` (`common/inventory_profile.cpp:260`),
which checks BOTH directions:

- source item: `IsSlotAllowed(destination_slot)` (`inventory_profile.cpp:325`),
  plus race/class (`:339`), deity (`:344`), level (`:349`) when the
  destination is an equipment slot;
- **displaced item** (whatever already sits in the destination):
  `IsSlotAllowed(source_slot)` (`inventory_profile.cpp:359`), plus the same
  race/class (`:373`), deity (`:378`), level (`:383`) checks **when the
  SOURCE is an equipment slot** (`:365`).

`ItemInstance::IsSlotAllowed()` (`common/item_instance.cpp:892-898`) is the
`Slots` bitmask for equipment slots and unconditionally true for any bag
slot (`SupportsContainers`) or any slot `> EQUIPMENT_END` -- general, cursor,
bank. Failure maps to "The selected slot was invalid." (swapNotAllowed),
"Your class, deity and/or race may not equip that item." or "You are not
sufficient level to use this item." (`zone/inventory.cpp:2097-2105`), and
`Handle_OP_MoveItem` (`zone/client_packet.cpp:10833`) then runs
`SwapItemResync` -- the "Inventory Desyncronization detected" the client
logs.

Consequences, both confirmed by the tests below:
- the ONLY swap the server can refuse on the displaced side is one whose
  source is an equipment slot (T82's `/equip 13 22` with a Skin of Milk in
  22 -- Milk is not allowed in Primary);
- a cursor-source swap can NEVER fail on the displaced side: the displaced
  item goes to the cursor and `IsSlotAllowed(slotCursor)` is always true.

### 2. The T230 finding is REFUTED by T230's own logs (not a defect)

The plan's premise -- "`/equip 30 22` reported a client-side swap; the server
never applied it and the DB still showed the old layout" -- is a misread.
Read the zone-in CharInventory of consecutive runs in `review/T230/`:

| run | zone-in general 22 | zone-in cursor front | `/equip 30 22` sent? |
|---|---|---|---|
| run 3 (`live_client_testchar_run3_debug.log:730-795`) | Scaled Curskin | **Wooden Fishing Tackle** | yes (`:12948`, `SendMoveItem 30 -> 22`) |
| run 4 (`live_client_testchar_run4_debug.log:732-797`) | **Wooden Fishing Tackle** | Scaled Curskin | yes (`:12782`) |

Run 3's swap (cursor Tackle <-> slot 22 Curskin) is exactly what run 4's
zone-in shows: the server APPLIED it and persisted it. Run 4's swap
(cursor Curskin <-> 22 Tackle) then put the Curskin back in server slot 23
and the Tackle on the cursor (server 33) -- which is precisely the layout
T230's staging edit found and documented ("curskin from 23 to 6011";
`db_staged.txt` shows `33 = 13702` Tackle). T230 expected the *meal* to
move; the meal was queued BEHIND the cursor front on both sides, so neither
side moved it. No server rejection, no message, no resync -- the server and
client agreed the whole time. `CursorSwap_IntoOccupiedGeneralSlot_StillMoves`
now pins this shape (cursor -> occupied general slot must keep going
through). Cross-checked live in section 5.

### 3. The real defect of this class (T82's), fixed (Scope 2)

`InventoryManager::moveItem()`'s direct slot<->slot branch validated only
`sourceItem -> toSlot` (`checkEquipDestination`, T77) and then swapped
locally, pushed `MoveItemIntent`, and let the server bounce it. Fix
(`src/client/inventory/inventory_manager.cpp`, before any mutation): when
both endpoints are real slots and `toSlot` is occupied, run
`checkEquipDestination(displacedItem, fromSlot)` -- the same existing
predicate (`ItemInstance::canEquipInSlot()` + `canEquipItem()`), which is
exactly the server's displaced-side rule (bitmask, then class/race/level;
passes for every non-equipment `fromSlot`, as `IsSlotAllowed()` does). On
failure: `Cannot swap: <displaced> cannot go in <fromSlot> -- wrong slot
type for this item (it would be displaced there by <source>).` (or the
"does not meet its class/race/level requirements" variant), nothing
mutated, nothing sent. No second legality copy; the resync path is
untouched. `placeItem()` (cursor-source) is untouched: per section 1 the
server cannot refuse its displaced side.

Noted, NOT changed (out of this defect's scope, flagged for a follow-up):
`placeItem()`'s swap branch checks the displaced item against
`cursorSourceSlot_` (where the cursor item was picked up FROM). The server
never sees that slot -- it checks the displaced item against the cursor,
always allowed -- so for a cursor item picked up from an equipment slot the
client is STRICTER than the server (refuses a swap the server would apply).
That is a refusal, not a lie, so it is harmless; it is the drag-and-drop
UI's behaviour and arguably desirable UX, but it is a client/server
divergence and should be decided deliberately.

### 4. Red/green (Scope 3), measured

`tests/test_eq_commands_equip.cpp`, five new tests (30 -> 35):

- `review/T236/red_eq_commands_equip_swap.txt` -- against the OLD
  `moveItem()`: **2 FAILED** (`Swap_DisplacedItemCannotGoInSourceEquipSlot_
  RejectedLocally_NoIntent_NoMutation`, `Swap_DisplacedItemFailsRestrictions
  ForSourceEquipSlot_RejectedLocally` -- both saw "Moving item", a
  `MoveItemIntent`, and the Milk/Blade already in Primary locally); the three
  guard tests (`Swap_DisplacedItemLegalForSourceEquipSlot_StillMoves`,
  `Swap_SourceIsGeneralSlot_DisplacedItemNotEquipChecked_StillMoves`,
  `CursorSwap_IntoOccupiedGeneralSlot_StillMoves`) passed before and after,
  as they must -- they pin what the server accepts.
- `review/T236/green_eq_commands_equip.txt` -- 35/35 after the fix.

### 5. Live leg (Scope 4)

Lock `T236 2026-08-25` (noclobber, taken after T235 released it), `up.sh
befallen`, Testchar (GM, warrior, level 2), one scripted run of
`build-gles2/bin/willeq --eqe1-path ... --log-level=DEBUG --script`
(`review/T236/live_client_testchar_debug.log`, exit 0). Testchar's live
layout was T230's restored one (server 23-30 = 8x Scaled Curskin, 33 =
Wooden Fishing Tackle on the cursor, 6011 = Chef's Instant Meal queued
behind it) -- so the cursor already carried an item and `#summonitem` was
not needed for the Scope-4 fixture (deviation: it would only have queued a
third item behind the Tackle, as it did in T230). To exercise the FIXED path
too, one documented inventory write staged a `Rusty Short Sword` (5013,
Primary|Secondary, warrior-usable) into server slot 13 before the run
(`db_before.txt` -> `db_staged.txt`).

| step | command | client | wire | server / DB |
|---|---|---|---|---|
| A | `/equip 13 22` (sword in Primary, Curskin in 22) | `Cannot swap: Scaled Curskin cannot go in Primary (13) -- wrong slot type for this item. (it would be displaced there by Rusty Short Sword).` (log `:14718`) | **nothing sent** | untouched |
| B | `/equip 30 22` (cursor Tackle, Curskin in 22) | `Moving item: slot 30 -> slot 22` (`:15521`) | ONE `SendMoveItem 30 -> 22` (`:15522`, the only one in the run) | **APPLIED**: `db_after_run.txt` shows server 23 = 13702 (Tackle), 33 = 12654 (Curskin) -- rows moved on both sides; no red message, no resync |
| C | `/equip 13 23` | same local refusal (`:16656`) | nothing sent | untouched |

`grep -c "SendMoveItem called"` = 1; no "selected slot", "Desync" or
"Invalid slot move" anywhere in the log. Step B is the T230 fixture shape
and lands on the plan's first branch ("the swap applies on BOTH sides, DB
row moves"), corroborating section 2 live. Steps A/C are T82's live shape
and now land on the second branch ("the client refuses with a message and
the DB is unchanged").

Teardown: slot-13 row deleted, rows 23/33 put back to `db_before.txt`
values, Testchar re-pinned to zone 36 / -77.9232 / 21.9086 / 3 / 190 (it had
not moved); `diff db_before.txt db_restored.txt` is empty; `down.sh`
clean (`eqemu_down.log`); lock deleted.

### Verification bar

1. `cmake --build build -j24` -- PASS (`review/T236/build_full.txt`).
2. `ctest --test-dir build --output-on-failure` -- **2826/2827**; sole
   failure `AudioManagerTest.PreloadCommonSounds` (documented ALSA host
   quirk). `review/T236/ctest_full.txt`.
3. `scripts/build-gles2-x86.sh` -- PASS (`build_gles2_x86.txt`).
4. Visual gate -- PASS, goldens untouched (`visual_gate.txt`; `git status`
   shows only the three source/test files changed). memoryCoverage SKIPped
   as always on this machine (no `data/eqe1_client`).
5. `scripts/build-arm-noble.sh` -- PASS, `build-arm-noble/bin/willeq` built
   (`build_arm_noble.txt`).
6. `check_file_sizes.sh fable` from slot3 -- 4 checked, 0 FAIL, 1 WARN
   (`inventory_manager.cpp` 2171 lines, pre-existing WARN; +31 lines here).
   The 4th file, `tests/test_eq_action_handler_transforms.cpp`, is NOT
   touched by this branch -- `fable` advanced (another merge) after this
   branch was cut from `ea2f7ca6`, so the diff-vs-`fable` set picks it up.

### Deviations

- Plan premise (section 2): the named defect does not exist; the unit fixed
  the T82 sibling the plan also names, and pinned the cursor-swap shape so a
  future "fix" cannot break it.
- No other file territory touched (`action/*`, `renderer_frame.cpp`,
  `eq_merchant.cpp` untouched).
