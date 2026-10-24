# B1140 (#460) REAGENT-COUNT: fable refused 1899 Imbue Fire Opal that the official client casts

Branch `task/B1140-reagent-count` (wt/b1140, cut from fable dacd9534). Evidence: `/mnt/projects/fable/review/B1140/`
(README, pcaps, logs, plans, `progress.log`).

## Prior work

- T246 (`9eaa67fc`, `refactor/T246_cast_check_stubs.md`) made `SpellManager::checkReagents()` real: count every
  consumed component over worn 0-21 + general 22-29 + bag contents 251-330 (eqemu's invWhereWorn|invWherePersonal
  set) and refuse locally with 272, sending nothing. Its live leg was NOT RUN and it never asked the oracle whether
  the official client refuses at all. T251 (`361e2102`) named the item (433) from the ItemPacket cache; T253
  (`9d0a8038`) routed the hotbar through the same refusal.
- No earlier card implemented `/autoinventory`: B1137's fable log shows `CMD outcome=unrecognized registry=none
  cmd="/autoinventory"`. The server-sent OP_DeleteItem handler (`processDeleteItemResponse`) was a stub ("we trust
  our client-side deletion").

## Cause (code + pcap, lane 5)

1. **Where the item lands.** `#summonitem 10031` arrives as one `OP_ItemPacket` type 106, serialized slot **30**
   (Titanium cursor), stack 1, id 10031, lore "Fire Opal". Testcaster logs in with Crimson Robe of Alendine (11641)
   ALREADY on the cursor (CharInventory slot 30; DB slot 33), so the opal is queued BEHIND the robe, on the server
   (eqemu `PushCursor`, `m_cursor`) and in fable (`cursorQueue_`).
2. **`/autoinventory`.** The official client sends `OP_MoveItem 30->311` + `30->30` (the robe into the Backpack in
   general 28), a second press `30->312`... (O1). Fable did not know the command: the opal never left the cursor
   queue.
3. **What checkReagents enumerated.** Worn + general + bag ranges by item id (stack `quantity`); never the cursor.
   With the opal in the queue it counted 0 and refused: "You are missing Fire Opal.", no OP_CastSpell.
4. **What the oracle does.** With ZERO Fire Opals carried anywhere (O1 cast A) the official client SENDS
   `OP_CastSpell`. The server checks components when the cast finishes (eqemu `spells.cpp` CastedSpellFinished:
   `HasItem(component, count, invWhereWorn|invWherePersonal)`, cursor excluded, pet spells exempt by rule) and sends
   272 + 433; Testcaster's GM flag lets it finish ("Your GM flag allows you to finish casting even though you're
   missing required components."). B1127's "the oracle casts it" was that GM override: in B1127 legC the opal was
   still on the cursor and the server called it missing too. There is no client-side component check to match.
5. **Consumption.** Last unit of a stack: `S->C OP_MoveItem from=<slot> to=-1` (O1, 313). One unit off a surviving
   stack: `S->C OP_DeleteItem from=<slot>` (O2, 311: 2 -> 1). Fable ignored OP_DeleteItem in its model and published
   the slot as EMPTY.

## Fix

- `SpellManager::validateCast()`: no reagent check. `checkReagents()`, `countCarriedItem()`,
  `getLastMissingReagentId()` and T251's 433 branch in `castRefusalMessage()` are deleted (no producer is left),
  with what only they read: `SpellData::requiresReagents()` and InventoryManager's by-id item cache
  (`getItemById`/`cacheItemById`, ZeroCallers hygiene).
  The server's own 272/433 reach chat through the existing SimpleMessage/FormattedMessage handlers.
- `/autoinventory` (registered in the B442 cursor family, golden entry 082):
  `InventoryManager::autoInventoryCursor()` -> `findAutoInventorySlot()` -> `placeOnMatchingStack()`. Order as
  measured: a same-id stack with room (O2 `30->311 qty=1` with 312 empty), else the first empty general slot (O2
  `30->25`), else the first empty bag slot bag by bag (O1 `311, 312, 313`); `30->30` when the queue advances.
  Not measured (plain ascending order taken): several stacks, several bags, empty worn slots.
- `processDeleteItemResponse()`: one unit off a stackable with quantity > 1, else remove; the merchant publish
  reports the slot's real state.

## Tests (red on dacd9534, green here)

- `test_eq_action_handler_cast`: `CastSpell_NoReagentCarried_SendsCastLikeOfficialClient`,
  `CastSpell_ImbueFireOpal_OpalQueuedOnCursor_SendsCast` (the defect's own state), and the four T246/T251/T253
  reagent refusals inverted (`..._StillSendsCast`, `..._SendsCast`). 6 FAIL on dacd9534 (`review/B1140/red_cast.txt`).
- `test_game_state_source_writes`: `Inventory_DeleteItem_OnStackTakesOneUnit` FAILS on dacd9534 (quantity stays 2).
- `test_autoinventory` (new): O1 and O2 replayed move by move; does not compile on dacd9534.
- `test_command_registration_order` golden learns `autoinventory` at 082 (count 157 -> 158).

## Live (lane 5, review/B1140/fable/)

F1 (WIZ): A no opal -> fable sends OP_CastSpell, server 272 + 433 + GM finish (= O1); B summon + 3 x /autoinventory
-> `30->311,30->30; 30->312,30->30; 30->313` (= O1), cast -> `S->C OP_MoveItem 313->-1`, fable removes it; C two
opals -> `30->313`, `30->313 qty=1` (= O2), cast -> `S->C OP_DeleteItem 313`, fable stack 2 -> 1, server DB at leg
end `5212 10031 charges 1` (= fable's model).

## Other reagent spells (<= 60)

265 level-1..60 spells carry components (spells_us.txt fields 58-61 ids, 62-65 counts). Every one went through the
same deleted local check, so all of them were refused whenever the component was short in fable's view (cursor,
bank, count short, pet spells the server exempts). Imbue line is 1891-1899 (1900-1920 are the "Acting ..." spells,
no components). Enchant metals 667-670/1359/1797/1889-1893, Summon Corpse 3 (17355 x1), Bone Walk 351 (13073 x2) and
the Pearl/Peridot users all reach the server unchanged now.

**The client's component data is not the server's.** `review/B1140/db_probe.txt` (lane 5 `spells_new`): 1889
Enchant Mithril needs 10476 x1 only (spells_us.txt: 10476 + 22503 Blue Diamond), 1890/1892/1893 likewise one
component, 351 Bone Walk 13073 x1 (spells_us.txt: x2). A client-side check on spells_us.txt refuses casts this
server accepts, which is a second reason, beside the oracle, that the check cannot stand.

Second live spell, 1889 Enchant Mithril as ENC (O3 oracle, F2 fable, same packets): Large Brick of Mithril only
-> cast, `S->C OP_MoveItem 312->-1`, Enchanted Lrg. Brick of Mithril summoned (dacd9534 would have refused it
locally for the Blue Diamond); brick gone + Blue Diamond carried -> cast, server 272 + 433 "Large Brick of
Mithril", GM finish, the diamond not consumed.

## Not changed (noted)

- The official client sends `OP_MoveItem 30->30` when an item arrives on an EMPTY cursor (O1 after cast B, O2
  after summon 2); fable does not. eqemu's handler for 30->30 is only a lore-duplicate check
  (`inventory.cpp:1623`), so nothing desyncs; not in this card's scope.
- A server `OP_DeleteItem` for a slot the server already holds empty (DeleteItemInInventory's first branch) is
  indistinguishable on the wire from a one-unit delete; fable takes one unit.
