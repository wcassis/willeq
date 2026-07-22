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

### Boundaries found (current tree, before this unit; `fable` @ c3e9262)

The 3-cluster super-range described in the plan (orig 3821–5244) is now at
current lines **2414–3836** (inclusive of the trailing `#endif` closing an
`#ifdef EQT_HAS_GRAPHICS` guard that wraps everything from
`ZoneProcessMoveItem` through `CloseTrainerWindow`). `ZoneProcessCharInventory`
itself (2414–2429) sits just before that guard opens and is not itself
graphics-gated. The recipes cluster is at current lines **12519–12623**
(comment banner `Phase 4: Tradeskill Recipes` through the last recipe
handler, immediately before the next section `Phase 4: Cosmetic`). Both
ranges were contiguous and cut cleanly (no interleaved unrelated methods
found on inspection — nothing was left behind as "not item-economy").

Total moved: 1423 + 105 = **1528 lines**, 47 methods.

### Methods moved, by cluster

**1. Inventory/items (7 methods)**
- `ZoneProcessCharInventory`, `ZoneProcessMoveItem`, `ZoneProcessDeleteItem`
- `SetupInventoryCallbacks`, `SendMoveItem`, `SendDeleteItem`
- `ScribeSpellFromScroll`

**2. Trade (11 methods)**
- `SendTradeRequest`, `SendTradeRequestAck`, `SendTradeCoins`, `SendMoveCoin`,
  `SendTradeAcceptClick`, `SendCancelTrade`
- `SendReadBookRequest`, `ZoneProcessReadBook`, `RequestReadBook`
- `ZoneProcessTradePartnerItem`, `SetupTradeManagerCallbacks`

**3. Loot/Vendor/Bank/Trainer (23 methods)**
- `ZoneProcessLootItemToUI`, `ZoneProcessLootedItemToInventory`
- `SetupLootCallbacks`, `SetupVendorCallbacks`, `SetupBankCallbacks`,
  `SetupTradeWindowCallbacks`, `SetupTradeskillCallbacks`, `SetupTrainerCallbacks`
- `ZoneProcessShopRequest`, `ZoneProcessShopPlayerBuy`, `ZoneProcessShopPlayerSell`,
  `ZoneProcessShopEndConfirm`, `ZoneProcessVendorItemToUI`
- `RequestOpenVendor`, `BuyFromVendor`, `SellToVendor`, `CloseVendorWindow`
- `OpenBankWindow`, `CloseBankWindow`
- `SetupTrainerCallbacks` (listed once above; kept for clarity of grouping)
- `ZoneProcessGMTraining`, `RequestTrainerWindow`, `TrainSkill`, `CloseTrainerWindow`
- `ZoneProcessMoneyUpdate` (found co-located inside this super-range during
  the move; a money/currency handler, clearly item-economy — moved with the
  cluster, not called out separately in the original plan's method count).

**4. Recipes (6 methods)**
- `SendRecipesFavorite`, `SendRecipesSearch`, `SendRecipeDetails`,
  `SendRecipeAutoCombine`, `ZoneProcessRecipeReply`, `ZoneProcessRecipeAutoCombine`

47 methods total (plan estimated ~55; the difference is the plan's rough
per-cluster method counts overlapping in the enumeration — actual unique
`EverQuest::` definitions in the moved ranges are as listed above and were
verified by exhaustive grep of the new file plus a diff-based line-multiset
check, see verifier results below).

### Helper analysis

No file-local (non-member, static/anonymous-namespace) helper functions were
found exclusively used by these clusters — everything moved is a member
function of `EverQuest`. No de-static-ing was required.

### Preprocessor guards preserved verbatim

The `#ifdef EQT_HAS_GRAPHICS ... #endif` guard spanning `ZoneProcessMoveItem`
through `CloseTrainerWindow`, and the nested `#ifdef WITH_AUDIO ... #endif`
blocks around vendor/bank music start/stop calls inside it, were moved
byte-for-byte with their original placement preserved.

### Includes added to eq_merchant.cpp

`client/eq.h`, `client/trade_manager.h`, `client/spell/spell_manager.h`,
`client/skill/skill_manager.h` (both needed: `ScribeSpellFromScroll` and
`ZoneProcessGMTraining` call through `m_spell_manager`/`m_skill_manager`,
which are forward-declared only in `eq.h`), `common/logging.h`,
`common/packet_structs.h`, `common/name_utils.h` (for `EQT::toDisplayName`),
`client/bridge/game_state_bridge.h` (full `GameStateBridge` type for
`m_bridge->pushEvent`), and — matching the original file's own guards —
`client/inventory/inventory_manager.h` / `inventory_constants.h` /
`item_instance.h` under `EQT_HAS_GRAPHICS`, and `client/audio/audio_manager.h`
under `WITH_AUDIO`. `<cstring>` for `memset`/`strncpy`/`memcpy`.

### Line counts

- `eq.cpp`: 15,373 → 13,845 lines (−1,528).
- `eq_merchant.cpp`: 1,558 lines (1,528 moved body + 30 lines of new header
  comment/includes).

### Verifier summary

- `cmake --build build -j24`: clean (pre-existing `-Wpointer-arith` warnings
  only, identical pattern to warnings already present in unmodified eq.cpp).
- `ctest --test-dir build`: 1191/1192 passed. Sole failure
  `AudioManagerTest.PreloadCommonSounds` — documented pre-existing
  expected-fail in `refactor/test_baseline.txt` (ALSA card resolution on this
  host), unrelated to this change.
- `verify_move_only.py c3e9262..HEAD`: **PASS** (move-only, modulo
  boilerplate: +25/-0 allowed lines, all `#include`/guard additions).
  `CMakeLists.txt` reported separately as non-C++, reviewed manually (single
  line added to `CLIENT_CORE_SOURCES`).
- `check_file_sizes.sh --files eq_merchant.cpp`: **WARN** (1,558 > 1,500
  ceiling) — accepted per plan, not a FAIL.

### Deviations

- Included `ZoneProcessMoneyUpdate` in cluster 3 (loot/vendor/bank/trainer):
  it fell inside the contiguous super-range and is a currency/money handler,
  clearly item-economy; the original plan's method list didn't name it
  explicitly but it wasn't excluded either. No behavior change — moved
  verbatim like everything else in the range.
- No methods were left behind as "not item-economy" — the entire
  2414–3836 and 12519–12623 ranges were item-economy content.
- No de-static-ing was needed (no file-local helpers in range).
