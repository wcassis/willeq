# K05 — `MerchantComponent` (family: merchant — vendor, bank, trainer, trade, item ops)

Batch: `refactor/K_component_extraction.md` §Mechanism (REVISED). Wave 2;
runs alongside K06/K07. Fold the Wave 1 Reviews (K02–K04) before dispatch.

## Measured at `feb6b10b` (2026-09-03)

Three TUs, 2,000 lines, 55 `EverQuest::` bodies: `eq_merchant.cpp` (677),
`eq_merchant_trade.cpp` (702), `eq_merchant_vendor_bank.cpp` (621). Three
root `CMakeLists.txt` sites. All three move into `src/client/merchant/` under
their existing names minus the `eq_` prefix; one header
`include/client/merchant/merchant_component.h`.

**Caller discovery is by COMPILER, not grep.** Method names here (`SendMoveItem`,
`TrainSkill`, `OpenBankWindow`…) are distinctive, but the batch rule from this
wave on is: delete the declarations from `eq.h`, build, and the error list IS
the caller list. Retarget each, rebuild until clean. Then `grep -rw` in
`tests/` for the same names to catch test callers the build did not compile
(it compiles them all under `EQT_BUILD_TESTS=ON`, so expect none).

Lead caller clusters (grep at `feb6b10b`; verify by build):
- `eq_bridge_intents.cpp` — ~15 sites (`BuyFromVendor`, `SellToVendor`, `Open/CloseBankWindow`, `RequestOpenVendor`, `Request/CloseTrainerWindow`, `TrainSkill`, `SendMoveItem`, `SendDeleteItem`, `SendMoveCoin`, `ScribeSpellFromScroll`, `RequestReadBook`…).
- `eq_commands.cpp` — ~8 (`BuyFromVendor`, `SellToVendor`, `SendMoveItem`, `RequestReadBook`, `RequestScribeItem`, `ScribeRefusalMessage`, `SendReadBookRequest`, `SendRecipeAutoCombine`, `ZoneProcessMoveItem`).
- `eq_action_handler*.cpp`, `action/command_processor_bank.cpp`, `action/command_processor_world.cpp`, `action/action_dispatcher_guild_misc.cpp`, `eq_commands_train.cpp`, `eq_commands_spells.cpp`, `eq_movement_update.cpp` (`CloseBankWindow`), `eq_zone.cpp`/`eq_player_state.cpp` (`CloseVendorWindow`), `eq_combat_actions.cpp` + `include/client/combat.h` (`ZoneProcessLootItemToUI`, `ZoneProcessLootedItemToInventory` — `combat.h` may DECLARE a friend or a callback type; read it).
- `eq_zone_dispatch.cpp` — 16 `ZoneProcess*` handlers.
- `eq.cpp` / `eq_bridge.cpp` — the nine `Setup*Callbacks` wiring methods. These are called from the ctor/bridge wiring; retarget to `m_merchant->Setup…()`.
- `SendCancelTrade`, `SendTradeAcceptClick`, `SendTradeRequest`, `SendTradeRequestAck`, `SurfaceBookText`: no external callers in the grep — internal or dead; decide by §5.

Members:

| member | refs | elsewhere | decision |
|---|---|---|---|
| `m_vendor_items` | 5 | `eq_commands.cpp` (reader) | shared non-core: `owner_.`; note for K12 |
| `m_trainer_skill_caps` | 4 | `action/action_dispatcher_guild_misc.cpp` | shared non-core: `owner_.`; note for K12 |
| `m_pending_scribe_book_slot`, `_source_slot`, `_spell_id` | 1 each | `eq_zone_dispatch.cpp` | read the dispatch site: if it is inside the `DispatchZonePacket` switch, that read moves into the handler you retarget and the members **move** |
| `m_loot_items`, `m_pending_loot_slots` | 2 / 6 | entities, bridge_ui_audio, zone_dispatch, combat | shared: `owner_.`; **the loot cluster's owner is decided in K11 (bridge)** |
| `m_on_completed` (1, trade_manager), `m_inv` (1), `m_loot_all_` (1), `m_gameState` (1), `m_eq` (1) | | these are OTHER classes' members appearing in lambdas/callbacks — not `EverQuest` members; leave |
| core: `m_bridge` 52, `m_game_state` 82, `m_trade_manager` 32, `m_inventory_manager` 29, `m_zone_connection` 26, `m_audio_manager` 8, `m_entities` 6, `m_combat_manager` 5, `m_spell_manager` 5, `m_skill_manager` 2 | | | `ctx_.` |

Predicted `eq.h` delta: −55 declarations −3 members (if the scribe trio
moves) −banners +4 ≈ **−55 or better**.

## Territory

`include/client/merchant/` + `src/client/merchant/` (new), the three
`eq_merchant*.cpp` (move), `eq.h`, `eq.cpp` (ctor + Setup call sites), root
`CMakeLists.txt` (three lines), `tests/test_merchant_component.cpp` + cmake,
allowlist, `docs/module_map.md` (merchant rows), and CALL-SITE-ONLY edits in
the caller files the compiler names. K06/K07 edit the same shared files at
other lines.

## Verification / Review

As K02, plus: the compiler-derived caller list pasted in the Review with the
grep lead beside it, differences named.
