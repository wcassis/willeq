# J01 — Batch J pilot: split inventory_manager.cpp (2,502 → 4 TUs, largest 795)

**Slot: assigned at dispatch. Branch `task/J01-inventory-split` cut from
`fable`. x86 only. No Pi. No eqemu.**

**This is the PILOT for Batch J** (`refactor/J_file_size_reclamation.md`) — it
proves the template on this tree before 17 more units fan out across three
slots. Chosen for that role deliberately: zero file-local statics, two already
independent classes, and a subsystem with real unit-test coverage. **If the
template is wrong, it is cheaper to find out here.** Read
`refactor/F_file_split.md` (process), `refactor/T166_split_renderer_zone_load.md`
(the most recent precedent, and the closest template) and `docs/module_map.md`
before editing anything.

## MOVE-ONLY. No behavior changes, no interface changes.

Method definitions move **verbatim**. No header changes beyond what the split
mechanically requires (it should require none — `InventoryManager`'s and
`TitaniumItemParser`'s declarations stay exactly where they are). **No method
decomposition** — that is Batch G's shape and is explicitly out of scope for
every Batch J unit, including this one.

## The split

Preamble is 12 lines (`src/client/inventory/inventory_manager.cpp:1-20`);
duplicate the includes each new TU actually needs, as T166 did — do not
create a shared "everything" header.

| Range | Cluster | Target TU | ≈ lines |
|---|---|---|---|
| 21-500 | **`TitaniumItemParser::`** — `toInt`, `toFloat`, `splitByPipe`, both `parseItem` overloads, `parseInstanceData`, `parseStaticData` (224), `parseSubItems` | `inventory_item_parser.cpp` | 490 |
| 509-793 | slot storage: get/set/remove/clear, `cacheItemById`, `findItemSlotByItemId`, trade/bank/world-container clears | **stays** `inventory_manager.cpp` | — |
| 795-1575 | cursor: `pickupItem`, `placeItem` (124), partial stacks, cursor queue/money, scribe slot, `swapItems`, `canPlaceItemInSlot`, `canEquipItem`, equip destination, `moveItem` (184) | `inventory_cursor.cpp` | 795 |
| 1577-1971 | `canPlaceInBag`, `canPlaceInWorldContainer`, restrictions, `hasLoreConflict`, the bag family, the bank-bag family | `inventory_bags.cpp` | 410 |
| 1973-2241 | bank / shared-bank / equipment / general / sellable queries, `calculateEquipmentStats`, `calculateTotalWeight`, `isValidSlot`, `destroyItem`, `destroyCursorItem` | **stays** | — |
| 2243-2499 | `processCharInventory`, `processItemPacket`, `ingestParsedItem`, `processMoveItemResponse`, `processDeleteItemResponse`, `dumpInventory` | `inventory_packets.cpp` | 270 |

**Result: 4 TUs** — `inventory_manager.cpp` ≈ 550, `inventory_cursor.cpp` ≈ 795,
`inventory_item_parser.cpp` ≈ 490, `inventory_bags.cpp` ≈ 410,
`inventory_packets.cpp` ≈ 270. Every one under 1,500 with room to grow.

**Verify the line ranges yourself against the tree at your branch tip before
you cut** — they were measured at `9ae1460c` and this file must not be trusted
over the code. Report any drift in your Review rather than silently adjusting.

## Hazards

**None found by the survey** — no file-local statics, no anonymous namespaces,
no order-dependent code. That is why it is the pilot. **If you find one, that
is the single most valuable thing this unit can report**: it means the survey's
hazard analysis is unreliable and the other 17 units need re-checking before
dispatch. Say so loudly rather than working around it quietly.

`CMakeLists.txt` has explicit source lists (no globs) at `:266` and `:456` —
add all four new TUs.

## Acceptance (the Batch J bar — every item, reported by exit code)

- build clean; `cmake --build` classified by **EXIT CODE**, never by grepping
  the log (`grep ' error '` never matches GCC, which prints `error:`)
- full **`ctest -j6`** (NOT -j24 — the visual gate's CPU ceiling flakes above
  that), zero new failures vs `refactor/test_baseline.txt`
- `python3 scripts/refactor/verify_move_only.py fable..HEAD` **clean** — this is
  the unit's central proof; paste its output in the Review
- visual gate PASS with **goldens untouched**. A re-bless would mean the split
  changed behaviour — **STOP and report** rather than re-blessing
- `scripts/build-gles2-x86.sh` and `scripts/build-arm-noble.sh` both clean
- `bash scripts/refactor/check_file_sizes.sh --files <every changed and created
  file>` → **0 warn, 0 fail**

## Review must answer, for the batch's sake

1. Did `verify_move_only.py` come back clean on the first try? If not, what did
   it flag and was it a real behaviour change?
2. How long did the unit take, wall-clock, and what was the slowest step? Waves
   1-3 are three units in parallel; the schedule depends on this number.
3. Anything about the template that should change before J02-J04 are written.

## Territory

`src/client/inventory/`, `CMakeLists.txt`, this file. Nothing else.
Concurrent units at dispatch time: T311 (no `src/` changes), T312
(`irrlicht_renderer.*`, `renderer_frame.cpp`, `graphics_input_handler.cpp`,
`renderer_player_physics.cpp`), T313 (`graphics/ui/`). No overlap with any.
