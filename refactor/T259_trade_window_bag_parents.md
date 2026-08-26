# T259 — trade-window slots are not bag parents (T255 leftover)

Slot: slot3. x86. OWNS eqemu briefly (noclobber) for one two-client trade
of a bag with contents. Class A. Territory: src/client/inventory/
inventory_manager.* (trade-slot parent ranges), TradeState/trade_manager
only where contents must be surfaced, tests. Do NOT touch
willeq_db_builder / data/willeq.db (T258 slot1), combat/eq_movement (T254).

## Gap
T255's bag-content relocation covers general, cursor and bank parents;
trade-window slots (Titanium trade slots 3000-3007, contents 3031+ in
steps of 10 — verify in titanium_limits.h / titanium.cpp CalcSlotId) are
not treated as bag parents, so a bag placed in the trade window shows
empty client-side and its contents are not relocated on completion.

## Scope
1. Add the trade parent range; `putBagContents`/`takeBagContents` for
   trade slots; on trade completion, contents follow the bag to the
   partner's general slot as the server's post-trade ItemPackets dictate
   (verify what eqemu sends: likely full ItemPackets for received items —
   if so, the client only needs to not lose the OWN side's contents when
   they leave).
2. Tests: bag with contents into trade slot → contents in 3031+; cancel →
   back to general with contents; complete → own side cleared cleanly.
   Red/green.
3. Live: Testchar trades a bag with two items to Oraclerogue; DB rows
   land under Oraclerogue's general range; both clients' `/equip list`
   agree with the DB.

## Verification
Full ctest; Review; evidence review/T259/. Commit:
`T259: trade-window bags keep their contents`.

## Review (executor, 2026-08-26, slot3, task/T259-trade-bags on fable d1cbb0f6)

Evidence: /mnt/projects/fable/review/T259/ (README.md indexes it).

### Trade numbering, verified both ways

| where | trade slot | bag contents | source |
|---|---|---|---|
| Titanium wire / client model | 3000-3007 | 3031 + (trade-3000)*10 + idx | `common/patches/titanium_limits.h:229-231` (`TRADE_BAGS_BEGIN = 3031`, `SLOT_COUNT = 10`); `titanium.cpp:3630-3631` (server->client) / `:3717-3718` (client->server); client `calcContainerSlotId()` (`inventory_constants.h:648-650`, already knew the range) |
| eqemu internal / DB | 3000-3007 | 11410 + (trade-3000)*200 + idx (computed: `emu_constants.h:247` `TRADE_BAGS_BEGIN = SHARED_BANK_BAGS_END + 1`, bank 6210 + 24*200, shared 11010 + 2*200) | not observed in the DB: trade rows are transient and every DB snapshot was taken outside a trade |
| partner's TradeView packets | relative 0-7 | relative 31-110 (`bagslot_id - TRADE_BEGIN`) | `zone/trading.cpp:176-184` `Trade::SendItemData`; the per-content packets are dead in practice, see 4 |

Live: Testchar's bag left general 22 with its two swords at client 331/332
(cursor), then 3031/3032 (trade slot 3000) -- `live_run3_complete_testchar_debug.log:32178-32182, 32919-32923`;
exactly two MoveItems (22->30, 30->3000), none for the contents.

### What eqemu sends on completion / cancel (the plan's open question)

Read from `/mnt/projects/claude/EQEmu/zone/trading.cpp` + `zone/client_packet.cpp`,
then confirmed in the logs:

- **Into the trade**: `Client::SwapItem` "Step 4" (`zone/inventory.cpp:1992-2010`)
  accepts a MoveItem into 3000-3007 ONLY from the cursor -- anything else is
  `Kick("Trade with non-cursor item")`. `Trade::AddEntity` (`trading.cpp:86-155`)
  `PutItemInInventory(trade_slot, *inst)` -- the whole ItemInstance, contents
  inside -- with NO client update for the giver. The partner gets one
  `ItemPacketTradeView` (101) at relative slot 0-7, serialised WITH sub-items.
- **Completion** (`Handle_OP_TradeAcceptClick`, `client_packet.cpp:15305`; both
  sides accepted -> `FinishTrade(other)` each way): the RECEIVER gets one
  `ItemPacketTrade` (103) per item at a free general slot, bag serialised WITH
  contents (`PutItemInInventory(free_slot, *inst, true)`, `trading.cpp:349`,
  `inventory.cpp:1055-1068`); the GIVER gets NOTHING for its trade slots
  (`DeleteItemInInventory(trade_slot)` with `client_update=false`,
  `trading.cpp:368`); then `OP_FinishTrade` (0 bytes) to both. Live:
  `live_run3_complete_oraclerogue_debug.log:37834-37846` (bag at client 27 with
  swords at 301/302, "You receive Large Bag from Testchar."), Testchar's side
  `:38306-38308` ("Trade completed", "You give Large Bag to Oraclerogue.",
  "Cleared trade slots 3000-3007 and 3031-3110 (3 items)").
- **Cancel** (`Handle_OP_CancelTrade`, `client_packet.cpp:4316`): the packet is
  forwarded to the partner and `FinishTrade(this)` returns ONLY THE SENDER's
  items (as `ItemPacketTrade` at a free general slot). The other client's
  items stay in the server's trade slots until THAT client sends
  `OP_CancelTrade` itself -- nothing else on the server calls `ResetTrade()`
  for a live client (`client_process.cpp:623` has it commented out; the only
  callers are bot.cpp). willeq never echoed it. Live run 1:
  `live_run1_cancel_testchar_debug.log:35400-35402` echo sent + range cleared,
  then `:37204-37230` the bag comes back as ItemPacket type 103 at slot 22 with
  swords parsed at 251/252; DB after run 1 == staged (`db_after_run1.txt`).

### 1. Client changes (Scope 1)

- `InventoryManager::isBagParentSlot()` admits trade slots. Every T252/T255
  range-move site (`pickupItem`, both `placeItem` branches, both `moveItem`
  direct branches, `ingestParsedItem`) now treats 3000-3007 as a parent;
  `takeBagContents`/`putBagContents` needed nothing (`calcContainerSlotId`
  already knew the range).
- `InventoryManager::moveItem()` pre-flights a trade-slot destination: the
  source must be the cursor (mirrors the server's Kick), message tells the
  player the two-step form. Before this, `/equip 22 3000` disconnected the
  player.
- `InventoryManager::clearTradeSlots()` now also clears 3031-3110 and bumps
  the change counter. It had NO callers; `TradeManager::resetTradeState()`
  calls it now (finish, partner cancel, local cancel, decline), so the giver's
  own side is dropped exactly when the server drops it.
- `TradeManager::handleCancelTrade()` echoes `OP_CancelTrade` when the trade
  was Active (the server rule above); the forwarded echo comes back in state
  None and is ignored -- no loop (pinned).
- `EverQuest` constructor: `m_trade_manager->setInventoryManager(...)` --
  **this call had no caller anywhere**, so `TradeManager::getOwnItem()` (the
  "You give X to Y" summary) always saw a null manager and the new clearing
  could not reach the model. Found by the first green run (3 tests still red).
- `TradeManager::hasOwnItem()` reads the inventory trade slot when
  `m_own_item_slots` is empty -- `addItemToTrade()` has no callers (T103 noted
  it), so the summary never printed a given item. Live run 3 prints it.
- `EqActionHandler::acceptTrade()`: during an ACTIVE trade `/accept` is the
  window's Accept button (`clickAccept()` -> `OP_TradeAcceptClick`).
  `TradeAcceptIntent` -- the only path to `clickAccept()` -- has NO producer
  in the tree (grep: `eq_bridge.cpp` consumes it, nothing pushes it), so no
  trade could be completed from any willeq client, UI or scripted. Needed for
  the live leg; reachability-pattern instance, flagged as a deviation below.
- `ZoneProcessTradePartnerItem()`: parses the partner's item WITHOUT a
  sub-item map (the contents belong to the partner; parsing them against our
  relative slot 0-7 only produced one T255 "no client slot -- dropped" WARN
  per content item, `live_run1_cancel_oraclerogue_debug.log:33104-33112`) and
  consumes relative slots 31-110 if they ever arrive (they do not: eqemu's
  per-content loop looks the bag up in the trade slot BEFORE it is put there,
  `trading.cpp:146-150`, so it finds nothing -- zero such packets in three
  runs).

### 2. Red/green, measured

`tests/test_eq_commands_equip.cpp` 52 -> 57, `tests/test_eq_action_handler_trade.cpp` 10 -> 11:

| test | red (`red_before_fix_*.txt`, unmodified code) | green |
|---|---|---|
| CursorMove_BagIntoTradeSlot_ContentsLandInTradeRange | FAIL | pass |
| DirectMove_GeneralBagStraightIntoTradeSlot_RefusedBeforeSend | FAIL | pass |
| TradeFinish_OwnTradeSlotsAndContentsClearedCleanly (+ "You give Large Bag to Buddy.") | FAIL | pass |
| TradeCancelledByPartner_EchoesCancel_ClearsRange_ReturnedBagKeepsContents | FAIL | pass |
| LocalCancel_ClearsOwnTradeRange | FAIL | pass |
| AcceptTrade_ActiveTrade_ClicksAcceptInsteadOfRefusing | FAIL | pass |

Red: 5 FAILED / 52 passed and 1 FAILED / 10 passed. Green: 57/57, 11/11
(`green_after_fix_*.txt`).

### 3. Live leg (Scope 3) -- two clients, three runs

Lock `T259 2026-08-26` (noclobber, first try, no contention), `up.sh befallen`.
Testchar (willeq_test, GM) = giver, **Oraclerogue** (willeq_rogue, char 81) =
partner, both `build/bin/willeq --log-level=DEBUG --script --eqe1-path`, Testchar
logging in first. Staging (`db_stage_writes.sql`): Testchar 23 -> Large Bag with
4010/4011 = Rusty Short/Long Sword; Testchar's cursor Tackle (33) and the stray
6011 Meal parked in bank 2000/2001 so the cursor is free (trade needs the cursor);
Oraclerogue's `character_data` set to Testchar's zone/position. Exit 0 on all
six client processes; `selected slot` / `desync` / `invalid slot` /
`putBagContents` WARN = 0 in every Testchar log.

| run | gesture | Testchar client | Oraclerogue client | DB after |
|---|---|---|---|---|
| 1 | `/trade Oraclerogue`, `/accept`, `/equip 22 30`, `/equip 30 3000`, partner `/canceltrade` | swords 331/332 -> 3031/3032; echo cancel; range cleared (3 items); bag back at 22 via ItemPacket 103 with swords at 251/252; `/equip list` = swords 251/252 (`:39976-39978`) | "Testchar has offered you a Large Bag." (`:33113-33114`, pre-fix binary: two parser WARNs for the contents) | == `db_staged.txt` (`db_after_run1.txt`) |
| 2 | same offer, both `/accept` | range cleared (3 items), `/equip list` = nothing carried (`:41662`) | ItemPacket 103: Large Bag at 27, swords 301/302; "You receive Large Bag from Testchar."; `/equip list` = swords 301/302 (`:37107-37109`) | Testchar: no 23, no 4010/4011; Oraclerogue: 28 = Large Bag, **5010/5011** = swords (`db_after_run2.txt`) |
| 3 | run 2 again on the final binary (hasOwnItem + partner-view fixes), after `db_restage_run3.sql` | as run 2 plus **"You give Large Bag to Oraclerogue."** (`:38307`) | as run 2, **zero** parser WARNs (`grep -a dropped` empty) | as run 2 (`db_after_run3.txt`) |

Client/server agreement: Oraclerogue's client 27 == server 28; client 301/302 ==
server 5010/5011 (4010 + (28-23)*200 + idx); Testchar's 251/252 == 4010/4011.

Teardown (`db_restore_writes.sql`): Oraclerogue's 28/5010/5011 deleted; Testchar
23 re-inserted as Scaled Curskin (the server DELETED the original row when the
bag was traded; `color`/`custom_data` set to match its siblings, `db_row23_24_
after_restore.txt` -- the original `guid` is unrecoverable, see deviations);
Tackle/Meal back to 33/6011; both characters re-pinned. The zone saved Testchar's
nudged x/y AFTER the first pin (visible only after `down.sh`), so mariadb was
brought up alone under a fresh noclobber lock and both rows re-pinned
(`scripts/repin.sh`, `eqemu_down2.log`). `diff db_before.txt db_restored.txt` is
EMPTY. Lock deleted.

### Verification bar

| step | result |
|---|---|
| `cmake --build slot3/build -j24` | PASS, 0 errors (`build_full.txt`, `build_test_green2.txt`) |
| ctest full | **2960/2961** -- only `AudioManagerTest.PreloadCommonSounds` (ALSA quirk). +6 from this unit (`ctest_full.txt`). |
| build-gles2-x86 / visual gate / ARM | NOT RUN -- plan narrows to "Full ctest; Review; evidence"; non-graphics, non-platform logic (T250/T252/T255 precedent). |
| check_file_sizes.sh | `--files` on the 8 changed files: 4 ok, 3 pre-existing WARN, 1 pre-existing FAIL -- `eq.cpp` 3923 (fable) -> 3930 (+7: the constructor wiring line and its comment; the fix belongs where the two managers are created). The `fable` form lists 18 files: fable has advanced past this branch's base. |
| eqemu | lock taken/released twice, DB restored (diff empty). |

### Deviations / observations

- **Territory**: `eq_action_handler.cpp` (`/accept` during an active trade),
  `eq_merchant.cpp` (partner-item parse) and `eq.cpp` (one wiring line) are
  outside the plan's named territory but not inside T258's (db builder) or
  T254's (combat/eq_movement). Each was required to reach or prove the plan's
  own scope: no trade could be COMPLETED by any willeq client without the
  first; the third is why the trade-end clearing could work at all.
- Reachability pattern, two more instances: `TradeAcceptIntent` (no producer)
  and `TradeManager::setInventoryManager()` (no caller). `addItemToTrade()` /
  `removeItemFromTrade()` still have no callers -- `hasOwnItem()` now works
  without them; they could be deleted in a later sweep.
- Partner bag CONTENTS are not surfaced in the trade window (TradeState has
  eight partner slots only). The packet carries them; a UI unit could show
  them. Not done here.
- Picking an item back OUT of a trade slot (3000 -> cursor) is not
  pre-flighted; the official client locks offered items and eqemu's SwapItem
  has no source-side trade rule. Untested, unchanged.
- Testchar's inventory row 23 has a new `guid` (server-assigned on delete/
  re-insert); every visible column matches `db_before.txt`.
- `refactor/deferred.md` not edited (parallel units live).
