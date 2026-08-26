# T250 — cursorSourceSlot_ is stale after a cursor swap; returnCursorItem() is dead (T249 findings)

Slot: slot3. x86. NO eqemu unless a bag-swap live check is cheap (noclobber).
Class A. Territory: src/client/inventory/inventory_manager.* (cursor
bookkeeping, `relocateBagContents`, `returnCursorItem`), tests/
test_eq_commands_equip.cpp + inventory tests. Do NOT touch combat/eq.cpp
(T247 slot1), audio (T248 slot2).

## Defects (T249 Review, review/T249/)
1. After `placeItem()` swaps the cursor item into an occupied slot, the
   DISPLACED item becomes the cursor front but `cursorSourceSlot_` still
   names the ORIGINAL pickup slot. `relocateBagContents(cursorSourceSlot_,
   …)` would then move the wrong bag's contents if the displaced item is a
   bag. Fix: on swap, set `cursorSourceSlot_` to the slot the displaced item
   came from (the destination), and make sure every consumer of
   `cursorSourceSlot_` agrees on the meaning ("where the current cursor
   front came from"). Test: pick up bag A from general 22, place on general
   23 holding bag B → cursor = B, source = 23; then placing B into 24 moves
   B's contents, not A's. Red/green.
2. `returnCursorItem()` has zero production callers. Either wire it where
   the official client returns the cursor (zone-out / auto-return on
   timeout — check eqemu's cursor handling) or delete it. Prefer delete
   unless a caller is obvious.

## Verification
Full ctest; Review; evidence review/T250/. Commit:
`T250: cursor source tracks the displaced item; dead returnCursorItem removed`.

## Review (executor, 2026-08-26, slot3, task/T250-cursor-source on fable 55ba5eee)

Evidence: /mnt/projects/fable/review/T250/ (README.md indexes it).

### Item 1 — cursorSourceSlot_ after a swap

Meaning settled as "the slot the CURRENT cursor front came from". Audit of
every consumer of `cursorSourceSlot_` in inventory_manager.cpp against that
meaning:

| consumer | before | after |
|---|---|---|
| `placeItem()` swap branch: `relocateBagContents(cursorSourceSlot_, target)` | stale pickup slot after any prior swap | correct: swap branch now sets `cursorSourceSlot_ = targetSlot` (the displaced item came from there) |
| `placeItem()` empty branch: `relocateBagContents(sourceSlot, target)` | same staleness | correct for the same reason |
| `canPlaceItemInSlot()` -> `hasLoreConflict(item, cursorSourceSlot_)` (exclude own slot) | after a swap excluded the wrong slot | excludes targetSlot, which now holds the placed (different) item; harmless either way, now consistent |
| `destroyCursorItem()` / `dumpInventory` debug logging | cosmetic | cosmetic |
| `setItem`/`removeItem`/`popCursorItem`/`placeOnMatchingStack` resets to CURSOR_SLOT / SLOT_INVALID | unaffected | unaffected |

Red/green (test_eq_commands_equip, two new tests):
- `CursorSwap_SourceSlotTracksDisplacedItem`: pick up sword from 22, place on
  23 holding Milk -> cursor = Milk, source must be 23. RED: was 22.
- `CursorSwap_DisplacedBag_ContentsFollowTheBagOnNextPlace`: the plan's
  scenario. Bag A (empty) from 22 onto 23 holding bag B (Bread in B's slot 0);
  cursor = B, source = 23; place B into 24 -> Bread must be at
  calcBagSlotId(24,0) and gone from 23's range. RED: source stayed 22,
  relocateBagContents(22,24) moved nothing, Bread stayed under bag A.
- After fix: 40/40 in the binary (review/T250/green_after_fix.txt).

### Item 2 — returnCursorItem()

DELETED (declaration in inventory_manager.h, definition in
inventory_manager.cpp; 23 lines). Grounds:
- Zero callers anywhere in src/, include/, tests/ (grep over the tree).
- No server-side counterpart to wire it to. eqemu never "returns" a cursor
  item to its source: the pickup MoveItem already moved the item to slot 30
  server-side, the server has no notion of a source slot, and across zoning
  it persists the cursor queue to the DB (`database.SaveCursor`, zone/
  inventory.cpp:815/1047/1074/1094/1298/1979) and re-sends it as a Limbo
  item packet on zone-in (zone/client_packet.cpp:1797). A client-local
  "return" would silently desync the client from a server that still has the
  item on the cursor. There is no timeout auto-return in eqemu either.
- The method's own guard (`cursorSourceSlot_ != CURSOR_SLOT`) shows it was
  designed as a local-only undo that pushes no intent -- exactly the
  client/server drift class T236/T249 have been closing.

### Verification

| step | result |
|---|---|
| cmake --build slot3/build -j24 | PASS, 0 errors (build_full.log) |
| ctest full | 2918/2919 -- only AudioManagerTest.PreloadCommonSounds (documented ALSA quirk). Note the suite is 2919 here, not the 2746 baseline: fable has grown since e70e3347. |
| build-gles2-x86 / visual gate / ARM | NOT RUN -- plan narrows verification to "Full ctest; Review; evidence"; the change is non-graphics, non-platform logic. |
| check_file_sizes.sh | 0 FAIL on the three changed files; inventory_manager.cpp is a pre-existing WARN (>1500 lines) and shrank by 27 lines. Running it as `check_file_sizes.sh fable` from the branch also listed audio files -- that is because `fable` advanced to 8182ee94 (T248 merged) after this branch was cut from 55ba5eee as instructed; those files are untouched on this branch (`git status`: only the three files). |
| eqemu | not used; no lock taken. Plan allowed a live bag-swap check "if cheap"; I judged it not needed -- the defect is pure client-local bookkeeping with no packet change (the MoveItem intents are identical before/after, asserted in the second test). |

### Observations (out of scope, not changed)

- The client keeps bag contents FLAT in `items_` keyed by the bag's general
  slot range, whereas eqemu's `_PutItem` carries contents inside the bag's
  ItemInstance. So a swap where BOTH bags have contents collides on the
  client: placing bag A (with contents) onto general 23 holding bag B (with
  contents) runs `relocateBagContents(22, 23)` while B's contents are still
  in 23's range -- A's items overwrite B's at matching indices. The
  official-client behaviour for that gesture (allowed? refused?) is not
  established here. Candidate for a follow-up unit; the plan's own scenario
  (A empty) is unaffected.
- `refactor/deferred.md` line 31 still carries the "NEW (T249)" sentence
  this unit closes; not edited here to avoid a merge conflict with the
  parallel units -- orchestrator to strike on merge.
