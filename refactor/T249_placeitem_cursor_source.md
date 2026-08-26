# T249 — placeItem() validates against cursorSourceSlot_, which the server never sees (T236 finding)

Slot: slot3. x86. OWNS eqemu briefly (noclobber) for one live swap. Class A.
Territory: src/client/inventory/inventory_manager.* `placeItem` swap
branch, tests/test_eq_commands_equip.cpp + inventory tests. Do NOT touch
combat/eq.cpp (T247), audio (T248).

## Divergence
T236 (`review/T236/`, Review): for a cursor item picked up FROM an
equipment slot, `placeItem()`'s swap branch validates the displaced item
against `cursorSourceSlot_` — but eqemu validates displaced items only when
the SOURCE of the move is an equipment slot (`inventory_profile.cpp:365`),
and the cursor is not one. So the client refuses swaps the server would
apply (client stricter than server). Decision (orchestrator): match the
server — the client must never refuse what the server accepts, nor apply
what it rejects.

## Scope
1. Make `placeItem()`'s displaced-item check use the server's rule: the
   move's source slot is the CURSOR (30), so `IsSlotAllowed` on the
   displaced item is trivially true; keep the check that the CURSOR item
   fits the destination (that one the server does enforce).
2. Tests: cursor item from an equipment slot placed onto an occupied
   general slot → swap applies (was: refused); cursor item that does not
   fit the destination → refused as before. Red/green.
3. Live: pick up a weapon from Primary to cursor, place on an occupied
   general slot → server applies (DB rows swap), client shows the same.

## Verification
Full ctest; Review; evidence review/T249/. Commit:
`T249: cursor placements follow the server's swap rule`.

## Review (executor, 2026-08-26, slot3, branch task/T249-cursor-swap off fable f461aaf3)

### 1. The fix (Scope 1)

`InventoryManager::placeItem()` (`src/client/inventory/inventory_manager.cpp`)
swap branch: the displaced-item check against `cursorSourceSlot_` is
REMOVED, with a comment citing the server rule. eqemu's
`InventoryProfile::SwapItem` (`common/inventory_profile.cpp:359-365`)
validates the displaced item against the move's SOURCE slot; for a cursor
placement the source is the cursor (30), and `IsSlotAllowed()` is
unconditionally true for it (`common/item_instance.cpp:892`, `slot_id >
EQUIPMENT_END`). The server never sees `cursorSourceSlot_`. The forward
check (`canPlaceItemInSlot(cursorItem, targetSlot)`) is the one the server
enforces (`inventory_profile.cpp:325`) and is unchanged; so is `moveItem()`'s
T236 direct slot<->slot displaced check (a different source slot, one the
server does validate). Net: -7 lines of logic, +12 lines of comment.

### 2. Red/green (Scope 2), measured

`tests/test_eq_commands_equip.cpp`, three new tests (35 -> 38):

- `review/T249/red_eq_commands_equip.txt` -- against the OLD code:
  **2 FAILED** (`CursorSwap_CursorItemFromEquipSlot_DisplacedNotChecked
  AgainstSource_StillMoves` via `/equip 30 22`, and
  `CursorSwap_DirectPlaceItem_FromEquipSlot_OntoOccupiedGeneral_Swaps` via
  `placeItem()` directly -- the drag-and-drop path); the guard test
  `CursorSwap_CursorItemDoesNotFitDestination_StillRefused_NoIntent_
  NoMutation` (Milk on the cursor -> Primary holding a sword: refused by
  both `placeItem()` and `/equip`'s pre-flight, nothing sent, nothing
  mutated) passed before and after, as it must.
- `review/T249/green_eq_commands_equip.txt` -- 38/38 after the fix.

### 3. Live leg (Scope 3)

Lock `T249 2026-08-26` (noclobber, exit 0; no contention seen), `up.sh
befallen`, Testchar (GM), one scripted run of `build-gles2/bin/willeq
--eqe1-path ... --log-level=DEBUG --script` (`review/T249/
live_client_testchar_debug.log`, exit 0).

Staging (three documented inventory writes, `db_before.txt` ->
`db_staged.txt`): Testchar's cursor was NOT empty at rest (T230/T236
layout: server 33 = Wooden Fishing Tackle, 6011 = Chef's Instant Meal),
and `pickupItem()` refuses while the cursor holds anything, so the two
cursor rows were parked in bank slots 2000/2001 for the run, and a Rusty
Short Sword (5013, Primary|Secondary) was inserted at server slot 13.

| step | command | client | wire | server / DB |
|---|---|---|---|---|
| 1 | `/equip 13 30` | `Picked up 'Rusty Short Sword' from slot 13` (log `:13894`); cursorSourceSlot_ = 13 | `SendMoveItem 13 -> 30` (`:13896`) | slot 13 row gone |
| 2 | `/equip 30 22` (cursor sword, Curskin in 22) | `Swapped items: cursor(from 13) <-> 22`, `Moving item: slot 30 -> slot 22` (`:15246-15247`) -- the OLD code refused here (Curskin "cannot go in Primary") | `SendMoveItem 30 -> 22` (`:15248`) | **APPLIED**: `db_after_run.txt` server 23 = 5013 (sword, was 12654), 33 = 12654 (Curskin on the cursor) |
| list | `/equip list` after step 2 | `Nothing currently equipped. ... Rusty Short Sword (slot 22) -> Primary (13)` (`:15251+`) | -- | matches the DB |

`grep -c "SendMoveItem called"` = 2 (exactly the two moves); 0 matches for
`desync`, `selected slot`, `invalid slot`, `SwapItemResync`, `not
sufficient`. Client and server show the same layout after the swap.

Teardown: sword row deleted, Curskin 33 -> 23, Tackle 2000 -> 33, meal
2001 -> 6011, Testchar re-pinned (zone 36 / -77.9232 / 21.9086 / 3 / 190; it
had not moved). `db_restored.txt` vs `db_before.txt`: identical in every
column except `guid`, which the server re-assigns to EVERY inventory row on
each character save (788-797 -> 468-477; T236's narrower snapshot did not
select it). It is server-owned bookkeeping, not inventory state, and was
left alone. `down.sh` clean (`eqemu_down.log`); lock deleted.

### Verification bar

1. `cmake --build build -j24` -- PASS (`review/T249/build_full.txt`).
2. `ctest --test-dir build --output-on-failure` -- **2916/2917**; sole
   failure `AudioManagerTest.PreloadCommonSounds` (documented ALSA host
   quirk). `review/T249/ctest_full.txt`. (Baseline in the protocol says
   2745/2746 at e70e3347; fable has grown since -- T247/T248's plans and
   intervening merges; nothing else fails.)
3. `scripts/build-gles2-x86.sh` -- PASS (`build_gles2_x86.txt`).
4. Visual gate -- PASS, goldens untouched (`visual_gate.txt`; `git status`
   shows only the two source/test files changed). memoryCoverage SKIPped
   as always on this machine (no `data/eqe1_client`).
5. `scripts/build-arm-noble.sh` -- PASS (`build_arm_noble.txt`; the first
   invocation hit the 600s tool ceiling and was re-run to completion).
6. `check_file_sizes.sh fable` from slot3 -- 2 checked, 0 FAIL, 1 WARN
   (`inventory_manager.cpp` 2176 lines, pre-existing WARN; +5 net lines).

### Noted, NOT changed (flag for the orchestrator)

After a cursor-source swap, `cursorSourceSlot_` still names the ORIGINAL
pickup slot while the DISPLACED item is now the cursor front (pre-existing;
the comment "cursorSourceSlot_ stays the same" is deliberate). Two places
consume that value for the displaced item: `relocateBagContents(
cursorSourceSlot_, target)` if the displaced item is a bag later placed in
a general slot (would relocate the wrong bag's contents), and
`returnCursorItem()` (no production callers). Neither is in this unit's
scope; neither affected the live leg (no bags involved).

### Deviations

- Staging parked Testchar's two resting cursor rows in bank 2000/2001 for
  the run (restored exactly), rather than using them -- the plan's fixture
  needs an EMPTY cursor to pick the weapon up into.
- No file outside the plan's territory touched.
