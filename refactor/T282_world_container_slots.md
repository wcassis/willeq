# T282 — the client cannot put items into a world container (T278 hand-back)

Slot: slot3. x86. OWNS eqemu (noclobber) for the live combine. Class A.
Territory: src/client/inventory/inventory_manager.* (`isValidSlot` and the
world-container slot range 4000-4009), the `/equip`/move command path where
the destination is validated, tests. Do NOT touch the EQE1 index (T281
slot2), trainer (T280 slot1).

## Gap (T278 Review)
`InventoryManager::isValidSlot()` rejects 4000-4009, so `/equip <from> 4000`
never sends and the WORLD form of `/combine` (forge/oven on the ground)
cannot be driven from the client at all — T278 could only prove the carried-
container form end to end.

## Scope
1. Accept the world-container range where the server does. Read eqemu's
   rules first (`zone/inventory.cpp` / `object.cpp`: which slots a world
   container exposes, what it validates on a move in, and what it sends
   back) — T273/T277's lesson: verify against the server, do not trust a
   member name or a comment.
2. Make sure the container's contents model is populated and cleared on the
   right packets (T278 fixed `ZoneProcessClearObject`; T255 fixed the bag
   parse — reuse `takeBagContents`/`putBagContents` if the shape fits).
3. Tests: move into 4000-4009 sends the right packet; move out; the model
   follows the server's confirmation, not optimistically. Red/green.
4. Live: `/clickobject` a world forge (T278's qeynos2 drop_id 202), move two
   components in, `/combine`, product appears, `/closecontainer`. DB proof
   both sides. Restore rows. ⚠ eqemu empties a world container on the first
   open by a new user (T278) — do not pre-fill `object_contents`.

## Verification
Full ctest; Review; evidence review/T282/. Commit:
`T282: items can be placed into a world container`.

---

## Review (executor, 2026-08-26, slot3, branch `task/T282-world-container`, base `59b1e114`)

### 1. What the server actually does with 4000-4009

Read off `/mnt/projects/akk-stack/code` before touching anything (T273/T277:
verify wire truth, never a member name or a comment). Six rules, all cited:

| # | Rule | eqemu source |
|---|---|---|
| R1 | 4000-4009 pass the Titanium wire **untranslated** in both directions | `common/patches/titanium.cpp` `ServerToTitaniumSlot` / `TitaniumToServerSlot`, final `WORLD_BEGIN..WORLD_END` arm; range from `titanium_limits.h` (`invslot::WORLD_BEGIN = 4000`, `invtype::WORLD_SIZE = 10`) |
| R2 | The server accepts the range unconditionally | `zone/inventory.cpp` `Client::IsValidSlot()` |
| R3 | ...but only **Step 3** of `Client::SwapItem()` knows what to do with it, and Step 3 is gated on `m_tradeskill_object != nullptr` | `zone/inventory.cpp` "Step 3: Check for interaction with World Container" |
| R4 | With no container open the move reaches Step 5's `m_inv.SwapItem()`, whose `_PutItem(4000, inst)` finds `CalcSlotId(4000) == INVALID_INDEX` (`CalcSlotId` has **no** WORLD branch, only `CalcBagIdx` does), takes the "must be within a bag" else-arm, gets a null parent bag and calls `MarkDirty(inst)`. **The item is destroyed, `SwapItem` still returns `true`, and nothing is sent back.** | `common/inventory_profile.cpp` `CalcSlotId`, `_PutItem`, `SwapItem` |
| R5 | Taking an item **out** lands via `PutItemInInventory(dst, inst, false)`, whose `m_inv.PutItem()` calls `DeleteItem(slot_id)` **first** — an occupied destination is silently destroyed. The cursor is safe (`PushItemOnCursor`). | `zone/inventory.cpp` `Client::PutItemInInventory`, `common/inventory_profile.cpp` `PutItem` |
| R6 | **NO DROP is allowed.** The "No Drop Hack" block lists trade and shared-bank destinations only; `WORLD_BEGIN..WORLD_END` is not in it — which is what makes NO DROP quest combines possible. | `zone/inventory.cpp` `Client::SwapItem`, the `ndh_inst` block |

Two more that shaped the fix:

* **Neither direction is confirmed.** Both world-container arms of Step 3
  `return true` without queueing anything to the client (the put's only
  client traffic is a possible `SendCursorBuffer` for a cursor source). The
  plan's item 3 asked the model to "follow the server's confirmation, not
  optimistically" — **there is no confirmation to follow**, so the model has
  to carry the move locally. Recorded as a deviation, not silently absorbed.
* **The open burst uses bag indices, not slot ids.** `zone/object.cpp`
  `Object::HandleClick` sends `SendItemPacket(i, inst,
  ItemPacketWorldContainer)` with `i` the container's own index **0-9**.

### 2. Scope delivered

1. **`isValidSlot()` accepts `isWorldContainerItemSlot()`** (R1/R2). This
   alone is what T278 handed back.
2. **`moveItem()` gained the three rules the server enforces on top of it**
   (R3/R4/R5), refusing before any mutation or packet, each with an
   explanatory message. The R4 refusal is the important one: without it
   `/equip 28 4000` with no forge open is a silent item-delete.
3. **`canPlaceInWorldContainer()` corrected against the server.** The NO
   DROP refusal was a client-side invention (R6) and is gone. The
   container-in-container refusal stays — eqemu does not check it, but
   `Object::PutItem` stores the bag by value and `SaveWorldContainer`
   persists one flat level of `object_contents`, so a nested bag's contents
   would not survive; noted in the code as deliberately stricter than the
   server.
4. **`worldContainerOpen_`**, set from the only writer of
   `EverQuest::m_active_tradeskill_object_id`: the server's
   `OP_ClickObjectAction` (open and close), the local `SendCloseContainer`
   (eqemu never echoes `open=0` — T278), and `ClearWorldObjects()`.
5. **`processItemPacket()` maps `ITEM_PACKET_WORLD_CONTAINER` (107) index
   0-9 into `WORLD_BEGIN + index`.** Untranslated, index 0/1/2 **is**
   equipment slot 0/1/2, so an open forge holding anything would have
   overwritten the player's charm, ears and head. New constant
   `EQT::ITEM_PACKET_WORLD_CONTAINER`.
6. **`describeSlot()` names world slots** ("tradeskill container slot 1
   (4000)"), so every refusal above reads as English.

### 3. Unplanned defect found and fixed: `processItemPacket()` was 2 bytes out

`EverQuest::DispatchZonePacket()` hands `processItemPacket()` a packet that
still carries its 2-byte **opcode** (and the loot path in `eq_merchant.cpp`
forwards the same one). It read the packet type at offset 0 and the
serialized item at offset 4 — two bytes early on both — in a file where
`ZoneProcessVendorItemToUI()` and `ZoneProcessTradePartnerItem()` both
correctly skip `2 + 4`.

It survived because the stray `\0\0` lands on instance field 0 and nothing
else. But **instance field 0 is the stack count** (`titanium.cpp`
`SerializeItem`), `from_chars()` failed on it, and every stacked item
arriving as an `OP_ItemPacket` — looted stacks, vendor buys, the stacked
contents of a world container — silently collapsed to a quantity of 1. The
packet type was likewise garbage, which is why nothing could dispatch on it
and why item 5 above needed this fixed first.

Fixed, pinned by `WorldContainerSlots.StackedItemPacketKeepsItsStackCount`.
The four direct-call sites in `tests/test_eq_commands_equip.cpp` went
through one helper (`ItemPacketPacket`), which encoded the wrong contract;
the helper now builds the packet the dispatcher really produces. All 57
`EqCommandsEquipTest` cases pass unchanged otherwise.

### 4. Red / green (measured, same test file both sides)

`tests/test_tradeskill_combine.cpp`, 12 new `WorldContainerSlots` cases
built against the **pre-T282 sources** (only the new enum constant kept, so
it compiles) vs the T282 sources:

| | result |
|---|---|
| RED (`review/T282/red_test_tradeskill_combine.txt`) | **14 passed, 9 FAILED** of 23 |
| GREEN (`review/T282/green_test_tradeskill_combine.txt`) | **23/23** |
| `test_eq_commands_equip` (`green_test_eq_commands_equip.txt`) | **57/57** |

The 3 cases that were already green in RED are the ones that pin behaviour
this unit deliberately did **not** change (a bag is still refused; an
ordinary 103 ItemPacket still lands at its own slot; `/closecontainer`
still clears the active id).

### 5. Live leg (eqemu, qeynos2 world forge drop_id 202)

`review/T282/live_world_combine2.log`, lock taken atomically and released,
server up/down logged, every DB row restored.

| leg | command | result |
|---|---|---|
| A | `/equip 28 4000` with nothing open | refused: *"You must have a tradeskill container open to use tradeskill container slot 1 (4000) -- /clickobject a forge or oven first."* — **no packet sent** (this is R4; the old code would have had no opinion) |
| B | `/clickobject 202` | `Opening tradeskill container: drop_id=202 type=17` |
| C | `/equip 28 4000`, `/equip 29 4001` | `SendMoveItem called: fromSlot=28 toSlot=4000`, `fromSlot=29 toSlot=4001` — **the range reaches the wire, untranslated** |
| D | `/equip 4001 22` (22 occupied) | refused: *"Taking an item out of a tradeskill container needs an empty destination (or the cursor): slot 22 already holds Scaled Curskin."* (R5) |
| E | `/equip 4001 29` then `/equip 29 4001` | both applied — out and back in |
| F | `/combine` | `Sending tradeskill combine for container slot 1000` → **"You have fashioned the items together to create something new!"** → `Boots of Ro` (3156) parsed onto the cursor, and `ClearObject: emptying world container slots (active container 202)` — T278's reading of `OP_ClearObject` confirmed live (window stays open, slots empty) |
| G | `/closecontainer` | second `ClearObject`, active container 0 |
| H | `/equip 22 4000` | refused again |

**DB proof** (`db_before.txt` / `db_after_run.txt` / `db_restored.txt`):
server slots 29 (`12304` Mold of Ro Boots) and 30 (`12305` Bar of Ronium)
are **gone** after the combine and a new row `6011 = 3156 Boots of Ro`
(cursor limbo) appears; `object_contents` is empty both before and after.
`diff db_before.txt db_restored.txt` is **EMPTY**.

### 6. Deviations

1. **"the model follows the server's confirmation, not optimistically"
   (plan item 3) is not achievable** — the server answers a world-container
   put or take with nothing at all (§1). The model applies both locally;
   what it *does* follow from the server is the open burst (item packet 107)
   and `OP_ClearObject`. Flagged, not absorbed.
2. **The first live tour was scripted with the wrong slot numbering** and is
   kept as `live_world_combine.log` rather than discarded. Titanium client
   general slots 22-29 are server 23-30 and **client slot 30 is the cursor**;
   the tour said `/equip 29 4000` meaning the DB's slot 29 and actually moved
   the DB's slot 30, then `/equip 30 4001` put the *cursor* item (a Wooden
   Fishing Tackle) into the forge. The combine correctly answered "You cannot
   combine these items in this container type!" because a third, wrong item
   was in the container. No client defect — an operator error in the tour,
   recorded because the same trap will catch the next unit.
3. **`takeBagContents`/`putBagContents` were not reused** (plan item 2). They
   address a bag's *contents range* under a parent slot; a world container's
   ten slots are top-level ids, not a bag range, and
   `clearWorldContainerSlots()` (given its caller by T278) already covers the
   clear side. Nothing to reuse without inventing a fit.
4. **`check_file_sizes.sh fable` reports one FAIL
   (`src/client/action/command_processor.cpp`, 3002 lines) that is not
   this unit's.** `fable` moved to `edcf5e3e` (T280/T281 merged) while this
   branch stayed on its dispatched base `59b1e114`, so the diff includes the
   siblings' files. Re-run with `--files` over T282's own seven files:
   **4 ok, 3 warn, 0 fail**, and all three warns (`packet_structs.h`,
   `eq_entities.cpp`, `inventory_manager.cpp`) were already over 1500 before
   this unit (2201/2712/2397 → 2207/2731/2502).
5. **`visual_gate` failed inside an EARLY full `ctest` run** on `p95 CPU ms`
   ceilings for `gfaydark` and `befallen` — host load from the parallel
   slots, not pixels. It is **PASS** standalone (`visual_gate.log`) and
   **PASS** again inside the final full `ctest` (`ctest_full.txt`), goldens
   untouched. Recorded because a CPU-time gate run alongside two other
   executor slots is not a trustworthy signal.

### 7. Verification

| step | result |
|---|---|
| `cmake --build build -j24` | PASS (`build_full.log`) |
| `ctest --test-dir build --output-on-failure` | **3134/3135** — the one failure is the documented ALSA host quirk `AudioManagerTest.PreloadCommonSounds` (`ctest_full.txt`). `visual_gate` passes inside this run too. |
| `scripts/build-gles2-x86.sh` | PASS (`build_gles2_x86.log`) |
| Visual gate | **PASS**, goldens untouched (`visual_gate.log`) |
| `scripts/build-arm-noble.sh` | PASS (`build_arm_noble.log`) |
| `check_file_sizes.sh` | 0 FAIL over T282's files (`check_file_sizes.txt`); see deviation 4 |

### 8. Owed / found, not fixed

* **`Object::Close()` leaves `object_contents` rows behind.** `PopItem()`
  calls `Object::Save()` (which writes the `object` row) but only
  `Object::PutItem()` calls `SaveWorldContainer()`, so the *contents* table
  keeps the last-saved snapshot after a close. Observed in the first tour
  (three stale rows with the put's timestamp). Server-side, harmless to the
  client, recorded so the next unit does not mistake it for a client leak.
* The `ItemInstance::ClearByFlags` fall-through T278 recorded is unchanged
  and was not re-tested: this unit never pre-filled `object_contents`.
