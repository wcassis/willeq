# T255 — bank bag moves carry no contents; only the front cursor bag's contents are parsed (T252 leftovers)

Slot: slot3. x86. OWNS eqemu briefly (noclobber) for one live bank move.
Class A. Territory: src/client/inventory/inventory_manager.* (bank ranges,
`takeBagContents`/`putBagContents` generalisation, ItemPacket cursor-queue
parsing), tests/test_eq_commands_equip.cpp + inventory tests. Do NOT touch
combat/eq_movement (T254 slot2), eq_bridge.cpp (T253 slot1).

## Gaps (T252 Review)
1. general↔BANK bag moves do not relocate contents client-side (T252
   generalised relocation to general-or-cursor parents only). eqemu bank
   content ranges: `CalcSlotId` for bank slots (titanium.cpp — find the
   bank bag base; Titanium bank = 2000-2007, bag contents 2031+ in steps
   of 10 client-side; server-side 6010+... verify both numberings and the
   translation T141/T158 established).
2. Only the FRONT cursor item's contents fit the cursor range; a queued
   bag behind the cursor loses its contents in the client model (server
   keeps them). Decide: parse contents per queued bag into a per-item
   container (small vector on the cached ItemInstance) so nothing is lost,
   or document the limit if the official client also only shows the front.

## Scope
1. Extend `takeBagContents`/`putBagContents` to bank parents; `/bank`
   move of a bag with contents → contents follow (client and server
   agree). Test red/green.
2. Item 2 per the decision above, with a test either way.
3. Live: bag with contents general → bank slot → DB rows in the bank
   content range; back again.

## Verification
Full ctest; Review; evidence review/T255/. Commit:
`T255: bank bag moves carry contents; queued cursor bags keep theirs`.

## Review (executor, 2026-08-26, slot3, task/T255-bank-bags on fable 3096dd80)

Evidence: /mnt/projects/fable/review/T255/ (README.md indexes it).

### Bank numbering, verified both ways

| where | bank slot | bag contents | source |
|---|---|---|---|
| Titanium wire / client model | 2000-2015 | 2031 + (bank-2000)*10 + idx | `common/patches/titanium_limits.h:219` (`BANK_BAGS_BEGIN = 2031`, `SLOT_COUNT = 10`); `titanium.cpp:3703-3704` (client->server) / `:3615-3616` (server->client); client `calcContainerSlotId()` |
| eqemu internal / DB `inventory.slot_id` | 2000-2023 | 6210 + (bank-2000)*200 + idx | `common/emu_constants.h:234` (`BANK_BAGS_BEGIN = CURSOR_BAG_END + 1`; general bags 4010-6009, cursor bag 6010-6209, RoF2 `SLOT_COUNT = 200`) |

Live: a Large Bag moved into bank 2000 carrying Rusty Short/Long Sword landed
at DB **6210/6211** (`db_after_run1.txt`, `db_after_run3.txt`); on zone-in the
server serialised that bag with its contents at wire **2031/2032**
(`live_run2_*:1307,1315`, `live_run4_*:1317,1325`) and the client's own
relocation wrote **2031/2032** going in and **251/252** coming back
(`live_run1_*:20871-20873`, `live_run4_*:21754-21756`). Back in general 22
(server 23) the DB rows were **4010/4011** again -- `db_after_run4.txt` equals
`db_staged.txt` row for row.

Server rule for the gesture: `Client::SwapItem` (`zone/inventory.cpp:1738-1747`)
requires a banker within `USE_NPC_RANGE2` for any bank endpoint; the live leg
spawned one with `#npctypespawn 1107` (Sorn_Copperhold, class 40) at
Testchar's position in befallen. No other bank-specific rule; the swap is the
same wholesale `InventoryProfile::SwapItem` T252 documented, contents inside
the `ItemInstance`.

### 1. Bank parents (Scope 1)

`isBagParentSlot()` now admits bank (2000-2015) and shared bank (2500-2501)
alongside general and cursor. Every T252 site that gated on
`isGeneralSlot()` -- `pickupItem`, both `placeItem` branches, both `moveItem`
direct branches -- gates on `isBagParentSlot()` instead; `takeBagContents`/
`putBagContents` needed no change (they already go through
`calcContainerSlotId`, which knew the bank ranges). No packet change.

### 2. Queued cursor bags (Scope 2) -- DECISION: keep every queued bag's contents

Not documented as a limit; fixed. The official client's behaviour for a
queued bag's contents is not observable from here, but the SERVER's model is
unambiguous (each cursor item is a full `ItemInstance` with `m_contents`,
serialised at slot 33 with sub-items in CharInventory) and the client must
not lose items the server still has. Design:

- Only the FRONT bag's contents occupy the cursor bag range 331-340.
- Each bag queued behind it holds its contents in
  `queuedCursorBagContents_` (`std::map<const ItemInstance*, TakenBagContents>`,
  keyed by the deque-owned instance -- pointer-stable), filled by the new
  `ingestParsedItem()` shared tail of `processCharInventory()`/`processItemPacket()`.
- `promoteCursorFrontContents()` moves the new front's held contents into
  331-340 on every front pop (`placeItem` empty branch, `popCursorItem`,
  `placeOnMatchingStack`, `removeItem(cursor)`, `destroyCursorItem`);
  `dropCursorFrontContents()` discards a destroyed/removed front bag's range
  first. The swap branch of `placeItem` promotes nothing: the displaced item
  becomes the front and its contents are the swapped-in range (T252).
- `dumpInventory()` shows the held count per queued item.

### 3. Two defects found on the way (both fixed, both pinned by tests)

- **Cursor bag contents were never parsed at all.** The server serialises a
  cursor item at ITS cursor slot 33; `parseSubItems()` fed that raw slot to
  `calcContainerSlotId()`, which knows only client 30, so every cursor bag's
  contents keyed onto `SLOT_INVALID` (and one of them was then `setItem`'d at
  slot -1). T252's Review statement that the parser "writes every cursor
  bag's contents to 331-340" was a read of the code, not a measurement -- it
  wrote none of them. `parseItem()` now normalises 33 -> 30 for the parent
  and `parseSubItems()` drops (WARN) any sub-item without a client slot.
  Pinned by `CharInventory_SingleCursorBag_ContentsLandInCursorRange`.
- **Bank bags always loaded EMPTY client-side.** Live run 2 showed eqemu
  delivers bank items at zone-in as individual OP_ItemPackets, each bag
  serialised WITH contents (`live_run2_*:1294-1316`), not via CharInventory --
  and `processItemPacket()` never parsed sub-items. So a bank->general move
  had nothing to relocate on the client (the server moved them regardless;
  `db_after_run2.txt` shows 4210/4211 under server slot 24 while the client
  log has no relocation line). `processItemPacket()` now parses sub-items and
  `ingestParsedItem()` treats the packet as authoritative for that bag:
  the parent's stale range is lifted out first, so `setItem()`'s same-id
  stack merge can never double a count. Pinned by
  `ItemPacket_BankBagWithContents_ContentsLandInBankRange`. Run 4 (after the
  fix) shows the relocation 2000 -> 22 firing (`live_run4_*:21754-21758`).

### Red/green, measured

`tests/test_eq_commands_equip.cpp`, 43 -> 52 tests:

| test | red | green |
|---|---|---|
| DirectMove_BagToEmptyBank_ContentsFollow | `red_before_fix_all8.txt` FAIL | pass |
| DirectMove_BagFromBankToGeneral_ContentsFollow | FAIL | pass |
| DirectSwap_GeneralBagOnBankBag_BothContentsFollowTheirBags | FAIL | pass |
| CursorMove_BagGeneralToBankAndBack_ContentsRideTheCursor | FAIL | pass |
| CharInventory_SingleCursorBag_ContentsLandInCursorRange | FAIL | pass |
| CharInventory_TwoQueuedCursorBags_EachKeepsItsOwnContents | FAIL | pass |
| ItemPacket_BagQueuedBehindCursorFront_KeepsContentsUntilPromoted | FAIL | pass |
| CursorSwap_DisplacedBagOntoOccupiedQueue_QueuedBagContentsSurvive | FAIL | pass |
| ItemPacket_BankBagWithContents_ContentsLandInBankRange | `red_itempacket_bank_before_fix.txt` FAIL (against the intermediate T255 code, written after live run 2) | pass |

`red_before_fix_all8.txt`: 8 FAILED / 43 passed with the src/include changes
stashed (`stash.txt`). `green_after_fix.txt`: 52/52. `red_before_fix.txt` is
the first red (7 tests, before the single-bag pin was added).

### Live leg (Scope 3)

Lock `T255 2026-08-26` taken by noclobber after a 75 s bounded wait on T256
(`take_lock.sh`; T256 had shut the stack down cleanly). `up.sh befallen`,
Testchar, `build/bin/willeq --log-level=DEBUG --script --eqe1-path`, four
scripted runs, exit 0 each, one MoveItem packet each, `selected slot` /
`desync` / `invalid slot` / `putBagContents` WARN = 0 in every log.

| run | gesture | client | server DB after |
|---|---|---|---|
| 1 | `/equip 22 2000` (bag{2 swords} -> empty bank 2000) | Relocating 22 -> 2000, swords at 2031/2032 | 2000 = Large Bag, 6210/6211 = swords (`db_after_run1.txt`) |
| 2 | `/equip 2000 23` (bank bag -> general 23 holding a Curskin) -- PRE-ItemPacket-fix binary | swap sent; NO relocation (bag had loaded empty: bank ItemPacket sub-items unparsed) | 24 = Large Bag, 4210/4211 = swords, 2000 = Curskin (`db_after_run2.txt`) |
| 3 | `/equip 23 2000` (bag{2 swords} onto the Curskin in bank 2000) -- fixed binary | Swapping bag contents 23(2) <-> 2000(0); swords at 2031/2032 | 2000 = Large Bag, 6210/6211 = swords, 24 = Curskin (`db_after_run3.txt`) |
| 4 | `/equip 2000 22` (bank bag -> empty general 22) -- fixed binary | ItemPacket parsed swords at 2031/2032; Relocating 2000 -> 22; swords at 251/252 | 23 = Large Bag, 4010/4011 = swords == `db_staged.txt` (`db_after_run4.txt`) |

Teardown: slot 23 back to 12654, rows 4010/4011 deleted, Testchar re-pinned to
36 / -77.9232 / 21.9086 / 3 / 190 (the runs had nudged x/y to -77.875 /
21.875); `diff db_before.txt db_restored.txt` empty; `down.sh` clean; lock
deleted.

### Verification bar

| step | result |
|---|---|
| `cmake --build slot3/build -j24` | PASS, 0 errors (`build_full.txt`) |
| ctest full | **2946/2947** -- only `AudioManagerTest.PreloadCommonSounds` (ALSA quirk). +9 from this unit (`ctest_full.txt`). |
| build-gles2-x86 / visual gate / ARM | NOT RUN -- plan narrows to "Full ctest; Review; evidence"; non-graphics, non-platform logic (T250/T252 precedent). |
| check_file_sizes.sh fable | 0 FAIL; `inventory_manager.cpp` pre-existing WARN, now 2342 lines (+111). The `eq_bridge.cpp` / `eq_player_info.cpp` WARNs are not on this branch (3 files changed). |
| eqemu | lock taken/released, DB restored byte-for-byte. |

### Deviations / observations (not changed)

- The plan's item-3 leg was "general -> bank -> back again" as two moves; it
  took four because run 2 exposed the bank-ItemPacket defect mid-leg. Runs 3+4
  are the leg on the final binary; runs 1+2 are kept as the discovery record.
- Trade-window parents (3000-3007) are still not bag parents; unchanged, out
  of territory.
- `processItemPacket()` returns the wire slot (33 for cursor), unchanged; the
  new test asserts `isCursorSlot()` on it rather than 30.
- Testchar's inventory carries a stray DB row 6011 (Chef's Instant Meal in
  the server cursor-bag range with no bag on the cursor); the server delivers
  it as an ItemPacket at slot 30 every zone-in. Pre-existing, in `db_before`,
  restored as found.
- `refactor/deferred.md` not edited (parallel units live).
