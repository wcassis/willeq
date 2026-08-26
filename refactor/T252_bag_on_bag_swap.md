# T252 — swapping two bags that both have contents collides client-side (T250 finding)

Slot: slot3. x86. OWNS eqemu briefly (noclobber) for one live bag swap.
Class A/B. Territory: src/client/inventory/inventory_manager.* bag-content
storage + `relocateBagContents`, tests/test_eq_commands_equip.cpp +
inventory tests. Do NOT touch spell/* / eq_strings (T251 slot2), combat /
eq.cpp / eq_entities (T247 slot1).

## Defect (T250 Review)
Bag contents are stored FLAT by general-slot range (bag in general slot N
owns content slots computed from N). Swapping bag A (with contents) onto
bag B (with contents): `relocateBagContents` moves A's contents into B's
range while B's are still there → A's overwrite B's at matching indices;
B's contents are then relocated from a range that no longer holds them.
eqemu carries contents INSIDE the bag ItemInstance, so the server is
correct and the client desyncs until resync.

## Scope
1. First establish what the official client does for this gesture (the
   Titanium client refuses to drop a non-empty bag onto another non-empty
   bag? or swaps both?). Check eqemu `SwapItem` for the bag-on-bag branch
   (/mnt/projects/akk-stack/code/common/inventory_profile.cpp — look for
   `IsClassBag` / `_swapItemInBag` / "cannot put a container in a
   container" style rules) and mirror the SERVER's rule exactly: if the
   server refuses, pre-flight refuse locally (T77 style, official string);
   if the server swaps both with contents, relocate in two phases via a
   temporary holding vector so neither overwrites the other.
2. Tests: bag A{x,y} ↔ bag B{p,q} → both bags' contents follow their bag
   (or local refusal with nothing sent, per the rule). Red/green measured.
3. Live: stage two bags with contents (documented DB writes, restored),
   `/equip` one onto the other, compare DB vs `/equip list`.

## Verification
Full ctest; Review; evidence review/T252/. Commit:
`T252: bag-on-bag swaps follow the server rule`.

## Review (executor, 2026-08-26, slot3, task/T252-bag-swap on fable a7f51112)

Evidence: /mnt/projects/fable/review/T252/ (README.md indexes it).

### 1. The server rule (Scope 1)

There is NO bag-on-bag refusal in eqemu. `EQ::InventoryProfile::SwapItem`
(`common/inventory_profile.cpp:260`) validates only `IsSlotAllowed` /
race-class-deity-level in both directions (`:325-:389`, the T236 rule) and
then does

    _PutItem(source_slot, destination_item_instance);   // :392
    _PutItem(destination_slot, source_item_instance);   // :393

An `ItemInstance` CARRIES its bag contents (`m_contents`; `GetItem(idx)`,
`_PutItem(idx)` at `inventory_profile.cpp:179-200, :1449-1451`), so both
bags move with their contents in one atomic swap. `Client::SwapItem`
(`zone/inventory.cpp`) adds no container rule for possessions either -- the
only container-related code there is the shared-bank `VerifyInventory`
sweep (`:1833-1840`, `:1856-1863`) and the world-container branch. The
"cannot put a bag with contents into a bag" rule (`IsNoneEmptyContainer`,
`:2039`) is about nesting, a different gesture the client already refuses
(`canPlaceInBag`). Branch taken: **swap both, contents follow** -- no
local refusal, no new string.

The wire/DB numbering that the client mirrors: a bag on the cursor keeps
its contents at `CalcSlotId(slotCursor, idx)` = Titanium 331-340
(`common/patches/titanium.cpp:3599-3600, :3688-3689`); general-bag contents
are Titanium 251 + (general-22)*10 + idx (`:3596-3597`). Server-internal /
DB numbering is 4010 + (general-23)*200 + idx (RoF2 `invbag::SLOT_COUNT =
200`, `rof2_limits.h:225`) -- run 1 of the live leg found that the hard way
(see 4).

### 2. The client fix (Scope 1, second branch)

`src/client/inventory/inventory_manager.{h,cpp}`:
- `relocateBagContents(from, to)` now accepts a general slot OR
  `CURSOR_SLOT` on either side and is take-then-put: every item in the
  from-range is lifted out (`takeBagContents`, full `BAG_SLOT_COUNT`
  range, index preserved) before anything is written (`putBagContents`,
  via the existing `calcContainerSlotId`). A collision on put -- impossible
  under the new invariants -- logs a WARN instead of silently destroying
  an item the way the old loop did.
- NEW `swapBagContents(a, b)`: both ranges lifted, then each written under
  the other parent. Either side may be empty or not a bag.
- `pickupItem()`: a bag picked up from a general slot takes its contents to
  the cursor range (the server's own model). Before, they stayed under the
  vacated slot and `placeItem()` fetched them from `cursorSourceSlot_`,
  which cannot represent "B on the cursor with contents while A sits in
  B's old slot with contents" -- the root of the T250 collision.
- `placeItem()`: swap branch -> `swapBagContents(CURSOR_SLOT, target)`;
  empty branch -> `relocateBagContents(CURSOR_SLOT, target)`. Neither
  consults `cursorSourceSlot_` any more (its T250 meaning is unchanged;
  it is still used by the lore check and logging).
- `moveItem()` direct slot<->slot branch (`/equip 22 23`): previously moved
  the two bag items and left BOTH contents ranges where they were -- a
  desync even for one bag onto an empty slot. Now `swapBagContents` /
  `relocateBagContents` after the `setItem` pair.
- `++changeCounter_` on every range move so the UI refreshes.
No packet change anywhere: the server needs exactly the one MoveItem it
already got (asserted in the tests, counted in the live logs).

### 3. Red/green (Scope 2), measured

`tests/test_eq_commands_equip.cpp`, three new tests (40 -> 43):
`DirectSwap_BagOnBag_BothContentsFollowTheirBags`,
`DirectMove_BagToEmptyGeneral_ContentsFollow`,
`CursorSwap_BagOnBag_BothContentsFollowTheirBags` (A{x,y} <-> B{p,q} via
pickup/place/place; asserts the cursor range mid-gesture and the exact
three MoveItem intents).
- `review/T252/red_before_fix.txt`: 3 FAILED / 40 passed against the old code.
- `review/T252/green_after_fix.txt`: 43/43. T250's
  `CursorSwap_DisplacedBag_ContentsFollowTheBagOnNextPlace` and
  `CursorSwap_SourceSlotTracksDisplacedItem` unchanged and green.

### 4. Live leg (Scope 3)

Lock `T252 2026-08-26` (noclobber, exit 0 first try -- no contention),
`up.sh befallen`, Testchar (GM warrior L2), `build/bin/willeq
--log-level=DEBUG --script --eqe1-path`, two scripted runs, exit 0 each.

| run | staged | `/equip 22 23` client | wire | server / DB after |
|---|---|---|---|---|
| 1 | 23 = Large Bag{Rusty Short Sword, Rusty Long Sword @4010/4011}, 24 = Backpack (its two axes were staged at 4020/4021 -- WRONG slots, server dropped them, bag loaded empty) | swords 251/252 -> 261/262 (`live_run1_*:21103-21107`) | 1 packet | 23 = Backpack, 24 = Large Bag, swords at **4210/4211** (`db_after_run1.txt`) |
| 2 | 23 = Backpack{Rusty Axe, Rusty Battle Axe @4010/4011}, 24 = Large Bag{swords @4210/4211} (`db_staged2.txt`) | `Swapping bag contents: parent 22 (2 items) <-> parent 23 (2 items)`; `/equip list` after: swords 251/252, axes 261/262 (`live_run2_*:20914-20941`) | 1 packet | 23 = Large Bag, 4010/4011 = swords; 24 = Backpack, 4210/4211 = axes (`db_after_run2.txt`) -- identical to the client's list (client 22 == server 23) |

grep counts in both logs: `SendMoveItem called` = 1, `selected slot` = 0,
`desync` = 0, `invalid slot` = 0, `putBagContents` WARN = 0.

Teardown: 4 content rows deleted (the 2 mis-staged ones the server had
already discarded), slots 23/24 back to 12654, Testchar re-pinned to
36 / -77.9232 / 21.9086 / 3 / 190 (the run had nudged x/y to -77.875 /
21.875); `diff db_before.txt db_restored.txt` empty; `down.sh` clean;
lock deleted.

### Verification bar

| step | result |
|---|---|
| `cmake --build slot3/build -j24` | PASS, 0 errors (`build_full.txt`) |
| ctest full | **2928/2929** -- only `AudioManagerTest.PreloadCommonSounds` (ALSA quirk). Suite is 2929 here (fable has grown past the 2746 baseline; +3 from this unit). |
| build-gles2-x86 / visual gate / ARM | NOT RUN -- plan narrows verification to "Full ctest; Review; evidence"; the change is non-graphics, non-platform logic (same narrowing T250 applied). |
| check_file_sizes.sh | 0 FAIL on the three changed files (`check_file_sizes_mine.txt`); `inventory_manager.cpp` pre-existing WARN (+60 lines). The `fable` form lists 16 files incl. an `eq.cpp` FAIL -- that is fable having advanced (T247/T251 merges) after this branch was cut; `git status` shows only the three files on this branch. |
| eqemu | lock taken/released, DB restored byte-for-byte (`db_restored.txt`). |

### Deviations / observations (out of scope, not changed)

- Run 1 staged the second bag's contents at the wrong server slots (assumed
  `SLOT_COUNT` 10, it is 200 server-side). The server silently discarded
  those rows; run 2 re-staged correctly and is the plan's scenario. Run 1
  is kept as a one-bag-onto-empty-bag data point.
- Only the FRONT cursor item's contents can live in the cursor range; a
  second bag queued behind it (server keeps each bag's contents inside its
  own ItemInstance) has nowhere to put its contents client-side. The
  CharInventory parser already had this limitation (it writes every cursor
  bag's contents to 331-340). Not touched.
- Moving a bag between general and BANK slots still does not relocate
  contents on the client (bank ranges are separate helpers; pre-existing,
  outside this unit's territory).
- `refactor/deferred.md` not edited (parallel units live); the T250 Review's
  "candidate for a follow-up unit" observation is closed by this unit.
