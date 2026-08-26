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
