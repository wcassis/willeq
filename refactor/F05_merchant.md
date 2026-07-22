# F05 — Split inventory/trade/loot/vendor/bank/trainer/recipes into eq_merchant.cpp

Move the item-economy clusters out of `src/client/eq.cpp` (15,373 lines).
Pure move — no behavior change, no interface change, no reformatting.

## Scope (original refs @ a0c02cd; ~3,900 lines have been removed above/around
these by F01–F04, positions shifted. Enumerate by NAME and contiguity.)

Move to new `src/client/eq_merchant.cpp` four clusters:
1. **Inventory/items** (orig 3821–4026, ~7 methods): CharInventory processing,
   MoveItem/DeleteItem senders, `SetupInventoryCallbacks`,
   `ScribeSpellFromScroll`.
2. **Trade** (orig 4027–4457, ~12 methods): `SendTrade*`, `ReadBook`,
   `TradePartnerItem`, `SetupTradeManagerCallbacks`.
3. **Loot/Vendor/Bank/Trainer** (orig 4458–5244, ~30 methods):
   `Setup*Callbacks` (loot, vendor, bank, trade-window, tradeskill, trainer),
   `Shop*`, money handlers, vendor/bank/GM-training/Train handlers.
4. **Recipes** (orig 16634–16739, ~6 methods): tradeskill recipe search.

These are ~55 methods, ~1,530 lines total. The three big clusters are
contiguous with each other (3821–5244); recipes is separate. Enumerate every
`EverQuest::` definition in those ranges by grep + reading boundaries; list
all in the review. In-range methods that are clearly not item-economy
(judgment call) stay put, documented.

## Steps

1. Locate boundaries; confirm the 3821–5244 super-range's start (after
   PlayerProfile cluster) and end (before zone spawns cluster).
2. Create `src/client/eq_merchant.cpp`: header comment + pruned includes
   (state/network code; inventory_manager and trade_manager headers likely
   needed; no graphics/audio expected).
3. Move verbatim. File-local static helpers: usual rule (move if exclusive,
   else leave + document).
4. Add to `CLIENT_CORE_SOURCES` in CMakeLists.txt.

## Verification

- `cmake --build build -j24` clean.
- `ctest --test-dir build` — green modulo the documented exceptions in
  refactor/test_baseline.txt (ALSA expected-fail; RDP flake → re-run isolated).
- `python3 scripts/refactor/verify_move_only.py BASE..HEAD` — PASS or
  documented de-static-ing (BASE = merge-base with fable if it advanced).
- `bash scripts/refactor/check_file_sizes.sh --files <abs>/eq_merchant.cpp`
  — ~1,530 lines: WARN accepted, must not FAIL.
- eq.cpp shrinks by ~1,450+ lines.

## Review

(executor fills in)
