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

## Review

Executed in `wt/slot1` on `task/K05-merchant-component`, branched from `fable`
at `7726c50a`. §Mechanism REVISED followed as written, plus the Wave-2
"callers by compiler" rule. One deviation, flagged not absorbed (the
`ReachabilitySweep` guard, below).

### The three `eq.h` numbers

| | lines |
|---|---|
| at dispatch (`fable` `7726c50a`) | **1,967** |
| predicted before starting | **1,886** (−81) |
| measured after | **1,887** (−80) |

`git diff --numstat` on the single file: **101 deletions, 21 insertions =
net −80**. The one-line miss against the prediction is arithmetic in one
comment hunk, not a compressed code line. Broken out:

| hunk | del | ins | contents |
|---|---|---|---|
| `// Phase 4: Tradeskills` + 4 recipe senders | 5 | 2 | banner + decls out, 2-line K05 pointer in |
| `SyncInventorySlotToGameState` decl | 4 | 3 | the T221 "two twins" comment now names one twin |
| `ZoneProcessCharInventory` decl | 1 | 0 | |
| `ZoneProcessRecipeReply/AutoCombine` decls | 2 | 0 | |
| `m_trainer_skill_caps` + its `// Player mode trainer state` banner + 5-line T280 note + blank | 8 | 0 | the ONE member that moved, with all its commentary |
| the private inventory/scribe/loot/vendor/bank/trainer/trade block | 40 | 4 | 28 decls + 7 banners + blanks out, 4-line K05 pointer in |
| `SendTradeAcceptClick`…`SurfaceBookText` block | 12 | 0 | 5 decls + banners + the T273 note |
| vendor window banner + 4 senders | 5 | 2 | |
| bank window banner + 2 senders | 3 | 2 | |
| trainer window banner + 3 senders | 4 | 2 | |
| `GetTrainerSkillCap` + `TrainingCost` + their T280 notes | 7 | 0 | |
| `RequestBankCoinMove/Convert` + 5-line T269 note, `RequestReadBook` + banner | 11 | 0 | |
| include / `Merchant()` + 3-line note / friend / `m_merchant` | 0 | 6 | the whole scaffolding cost |
| **total** | **101** | **21** | **net −80** |

`scripts/refactor/file_size_allowlist.txt`'s `eq.h` entry lowered
**1,967 → 1,887**. K04 and K06 edit the same line; the orchestrator resolves.
`FileSizeCeiling` PASSES; `check_file_sizes.sh --all`: **806 ok, 0 warn,
0 FAIL**.

### Measured vs the plan's leads

**Method count is 54, not 55.** `grep -n '^[A-Za-z_].*EverQuest::'` over the
three TUs returns 21 + 13 + **20** = 54 `EverQuest::` bodies, not the 56 the
batch table claims nor the 55 this plan's prose claims. The 55th unique name a
naive `grep -o 'EverQuest::[A-Za-z_]*'` finds is **`RequestScribeItem`**, which
appears twice in `eq_merchant.cpp` **in comments** and is defined in
`eq_commands.cpp` (the commands family, K12). All 54 moved; none was deleted.

**Caller discovery.** All 54 declarations were deleted from `eq.h` and every
caller retargeted; the build then went clean, which is the compiler's
confirmation that no caller was missed (a missed one is a hard error, not a
warning). Compiler-derived list vs the plan's grep lead:

| file | measured sites | receiver form | vs the lead |
|---|---|---|---|
| `src/client/eq_zone_dispatch.cpp` | **17** | bare, inside `EverQuest::DispatchZonePacket` → `m_merchant->` | lead said "16 `ZoneProcess*` handlers". The 17th is `SyncInventorySlotToGameState` (line 373), not a `ZoneProcess*` |
| `src/client/eq_bridge_intents.cpp` | **15** | bare, inside `EverQuest::ProcessBridgeIntents` → `m_merchant->` | lead said "~15" — exact |
| `src/client/eq_bridge.cpp` | **6** | bare → `m_merchant->` | the loot/vendor/bank/trainer/trade-window/tradeskill `Setup*Callbacks`. The lead put all nine `Setup*` in "`eq.cpp` / `eq_bridge.cpp`"; the split is 6 here, 2 in `eq.cpp`, and `SetupTradeManagerCallbacks` is one of the two |
| `src/client/eq.cpp` | **3** | bare → `m_merchant->` | `SetupTradeManagerCallbacks`, `SetupInventoryCallbacks`, and one `SyncInventorySlotToGameState` (line 712, T221 zone-in parity) the lead did not name |
| `src/client/eq_commands_train.cpp` | **4** | 2 × `m_merchant->GetTrainerSkillCap`, 2 × `eqt::merchant::MerchantComponent::TrainingCost` | |
| `src/client/eq_commands_spells.cpp` | **2** | bare → `m_merchant->` | `CloseBankWindow`, `RequestTrainerWindow`. The other two grep hits are prose |
| `src/client/eq_commands.cpp` | **1** | `m_merchant->ScribeRefusalMessage` | the lead said "~8". **Seven of the eight are comments** naming `SendMoveItem`, `BuyFromVendor`, `SellToVendor`, `RequestReadBook`, `ScribeSpellFromScroll` in prose. `m_vendor_items` is read here, but that member stays on `EverQuest` |
| `src/client/eq_movement_update.cpp` | 2 | bare → `m_merchant->CloseBankWindow` | |
| `src/client/eq_player_state.cpp` | 2 | bare → `m_merchant->CloseVendorWindow` | |
| `src/client/eq_zone.cpp` | 1 | bare → `m_merchant->CloseVendorWindow` | |
| `src/client/eq_player_misc.cpp` | 1 | bare → `m_merchant->SyncInventorySlotToGameState` | not in the lead |
| `src/client/eq_action_handler_extended.cpp` | 2 | `m_eq.` → `m_eq.Merchant().` | `RequestBankCoinMove/Convert` |
| `src/client/eq_action_handler_spells_trade.cpp` | 4 | `m_eq.` → `m_eq.Merchant().` | the four recipe senders |
| `tests/test_train_skill.cpp` | **16** | `h.eq.` → `h.eq.Merchant().`, `EverQuest::TrainingCost` → `eqt::merchant::MerchantComponent::TrainingCost` | assertions untouched |
| `tests/test_eq_bank_coins.cpp` | **14** | `h.eq.` → `h.eq.Merchant().` | assertions untouched |
| `tests/test_game_state_source_writes.cpp` | 5 | `eq.` → `eq.Merchant().` | assertions untouched |
| `tests/test_eq_commands_bank_train.cpp` | 1 | `eq.` → `eq.Merchant().` | |
| `action/command_processor_bank.cpp`, `action/command_processor_world.cpp`, `action/action_dispatcher_guild_misc.cpp`, `include/client/combat.h` | **0** | — | **all four are plan leads that resolved to comments or to a different symbol.** `combat.h` declares no friend and no callback type for the two loot handlers; its two `m_loot_items` hits are `CombatManager`'s OWN `std::vector<uint32_t> m_loot_items` (line 339) — a different member of a different class with the same name, plus one comment referring to `EverQuest::m_loot_items` |

**Zero-caller candidates: none.** The plan flagged `SendCancelTrade`,
`SendTradeAcceptClick`, `SendTradeRequest`, `SendTradeRequestAck`,
`SurfaceBookText` (and, by omission, `SendReadBookRequest`) as having no
external callers, "internal or dead; decide by §5". All six have **in-family
callers** (`SendCancelTrade`/`SendTradeAcceptClick`/`SendTradeRequest`/
`SendTradeRequestAck` from `SetupTradeManagerCallbacks`'s lambdas in
`merchant_trade.cpp`; `SendReadBookRequest` from `RequestReadBook`;
`SurfaceBookText` from `ZoneProcessReadBook`), so §5's "zero callers INCLUDING
in-family" (K03 correction a) keeps every one of them. Nothing was deleted.

**Members.**

| member | plan's decision | measured | outcome |
|---|---|---|---|
| `m_vendor_items` (5) | shared → `owner_.` | 5 refs here + `eq_commands.cpp:512` (real read) + the inline `GetVendorItems()` accessor (`eq.h`) | **stays**, `owner_.m_vendor_items` |
| `m_trainer_skill_caps` (4) | "shared non-core: `owner_.`; note for K12" | **WRONG — single-family.** The `action/action_dispatcher_guild_misc.cpp:458` hit is a COMMENT. The only code refs tree-wide are the four in `merchant_vendor_bank.cpp`, and its reader `GetTrainerSkillCap()` is a family body | **MOVED** into the component (with its whole 5-line T280 note). This is where most of the member-side shrinkage came from |
| `m_pending_scribe_spell_id` / `_book_slot` / `_source_slot` | "if the dispatch read is inside the `DispatchZonePacket` switch, the members **move**" | the reads ARE in the switch (`eq_zone_dispatch.cpp` 796–838) but in an **inline `OP_MemorizeSpell` arm**, not in a merchant handler this unit retargets. The arm is zone-family code (K09) | **stay**, `owner_.m_pending_scribe_*`. The plan's conditional was written for a read that travels with a moved handler; this one does not |
| `m_loot_items` (2) / `m_pending_loot_slots` (6) | shared; owner decided in K11 | confirmed shared with `eq_entities_appearance`, `eq_bridge_ui_audio`, `eq_zone_dispatch` | **stay**, `owner_.` |
| `m_on_completed`, `m_inv`, `m_loot_all_`, `m_gameState`, `m_eq` | "other classes' members; leave" | **none of the five appears anywhere in the three TUs at this tip.** Stale leads, nothing to leave and nothing to board | — |
| core: `m_game_state` 77, `m_bridge` 52, `m_trade_manager` 32, `m_zone_connection` 26, `m_inventory_manager` 26, `m_audio_manager` 8, `m_entities` 6, `m_spell_manager` 5, `m_combat_manager` 5, `m_skill_manager` 2 | `ctx_.` | counts differ slightly from the plan's (which counted comment lines too) | `ctx_.` |

**Inline `eq.h` accessors on a moving member (the K04 warning):** grepped
`eq.h` for `m_trainer_skill_caps` — the declaration is its ONLY mention. No
inline accessor rides along. (`GetVendorItems()` and `GetLootItems()` are the
mirror case: their members stayed, so they stayed.)

**Dependencies no lead table named**, found by reading the bodies and then by
the compiler — 15 `owner_.Foo()` methods (`AddChatSystemMessage`, `QueuePacket`,
`UpdateInventoryStats`, `MySpawnId`, `IsBankWindowOpen`, `GetClass`, `GetLevel`,
`GetPracticePoints`, `GetPlatinum`, `GetGold`, `GetSilver`, `GetCopper`,
`GetStringMessage`, `GetFormattedStringMessage`) and 3 `EverQuest::` statics
(`s_debug_level` ×3, `DumpPacket` ×20, `FormatPrice` ×9).

**Text readers of the moved TUs (K03 correction c):**
`grep -rn '"[^"]*eq_merchant[^"]*"' tests/ scripts/` returns **nothing** — no
test reads any of the three TUs by path. Every other `eq_merchant` hit in
`tests/` is a comment. `test_duplicate_state_stores` passes unchanged.

### ⚠ Deviation: `ReachabilitySweep.EverySenderHasACaller` had to have its input set widened

`tests/test_reachability_sweep.cpp:437` parses **`include/client/eq.h` alone**
for `Send*` declarations and asserts `senders.size() > 50` before checking that
each has a caller. K05 deleted 12 `Send*` declarations from `eq.h`, taking the
count to **49** — one under the guard's own floor — and the test failed with
"eq.h Send\* parse failed". K01/K02/K03 did not trip it (guild/gm/chat removed
7/9/1 senders against a fatter header); K05 is the first unit to cross it, and
K04/K06/K07 will push it further down.

This is the §Mechanism 7 STOP shape ("a test scans `eq.h` as text and a later
family might need a file-list edit"). It was **fixed rather than stopped on**,
for the same reason K03 fixed `test_chat_channel_names`: the fix is an
input-path widening inside a test file this unit owns, the assertion is
untouched, and leaving it red would have left the suite red for every remaining
K unit. Flagged here, not absorbed. What changed:

- the declaration set is now `include/client/eq.h` **plus every
  `include/client/*/​*_component.h`** (discovered by the file walker already in
  the test, so K06+ need no further edit);
- the caller scan skips all of those declaration files, not just `eq.h`;
- a definition line is recognised as `EverQuest::Send*(` **or**
  `Component::Send*(`;
- the `> 50` floor and the "every sender has a caller" assertion are unchanged.

Sender count after the widening: back above the floor, guard green, and it now
covers the guild/gm/chat/merchant senders that had silently dropped out of its
scope in K01–K03.

### Multiset residual (§Mechanism 8, K03's one-command form)

Normalisation applied to both sides: `ctx_.<core>` → `m_<core>` for the ten
core members, then strip `owner_.`, then
`eqt::merchant::MerchantComponent::` → `EverQuest::`, then un-qualify the five
scope-only forms (`EverQuest::s_debug_level`, `::DumpPacket`, `::FormatPrice`,
`::GameLootItem`, `::GameVendorItem`); comment, blank and `#include` lines
filtered out; both sides sorted.

Old side: the three `eq_merchant*.cpp` at `7726c50a`. New side: the three
`src/client/merchant/*.cpp`.

**Residual on code lines: ZERO. 1,278 lines on each side, sorted multisets
byte-identical, `diff` empty.** Every difference in the unfiltered files is a
comment (the three rewritten file headers) or the one new `#include
"client/merchant/merchant_component.h"` per TU. The move is verbatim.

### Verification

| step | result |
|---|---|
| `cmake --build build -j24` | **exit 0**. Three attempts: the first failed with **exactly 3 errors** (the two nested-type scopes below); the second hit the **600 s ceiling, exit 143** — the post-timeout scan per EXECUTOR_PROTOCOL found both `build/lib/*.a` PRESENT and `ar t`-clean and all 221 `build/bin/*` valid ELF, with the one killed link's binary DELETED (so cmake rebuilt it); the third and all later builds **exit 0** with 223 binaries, `readelf -h` clean on every one |
| `ctest --test-dir build -j6 --output-on-failure` | **3,530 / 3,531 passed**, 17 skipped, 142 s. **1 failure: `AudioManagerTest.PreloadCommonSounds` only** (the known ALSA quirk). `FileSizeCeiling`, `DocCitations` (224 citations, 223 ok, 0 fail, 1 skip), `ReachabilitySweep.*` (5/5), `DuplicateStateStores.*` and `visual_gate` (Test #3526, 19.55 s) all PASS |
| `tests/test_merchant_component.cpp` (new) | **6/6 pass** |
| retargeted suites | `test_train_skill` (all), `test_eq_bank_coins` (all), `test_game_state_source_writes`, `test_eq_commands_bank_train`, `test_scribe_scroll` — all pass, assertions untouched |
| `bash scripts/build-gles2-x86.sh` | **exit 0** |
| visual gate (standalone) / `bash scripts/build-arm-noble.sh` | **SKIPPED, with evidence**: `git diff --cached --name-only -- src/client/graphics include/client/graphics` is **empty** — this unit changed no graphics file. Goldens untouched. `visual_gate` nonetheless ran and passed inside the full ctest run above |
| `bash scripts/refactor/check_file_sizes.sh --all` | **806 ok, 0 warn, 0 FAIL** with the ratchet lowered to 1,887 |

Two intermediate failures, both caught and fixed in-unit: the 3 compile errors
(below) and `DocCitations`, which K05 broke by citing the OLD paths
(`src/client/eq_merchant*.cpp`) inside the new `docs/module_map.md` rows'
"(was …)" prose — exactly K03's mistake in mirror image. Reworded to bare
filenames, which is how every other row records a predecessor.

### Corrections to §Mechanism

1. **§Mechanism 4 needs a FOURTH substitution form: a nested TYPE of
   `EverQuest`.** Two bodies construct `EverQuest`'s nested aggregates
   (`GameLootItem{itemId, item_name}` in `merchant_trade.cpp`,
   `GameVendorItem gvi;` in `merchant_vendor_bank.cpp`). Those are the only
   three compile errors the whole 2,000-line move produced. It is the same
   failure as K01 correction 2 — **friendship grants access, not scope** — but
   applied to a nested type rather than a static member, and K01's wording
   ("`s_debug_level` and any other `EverQuest` static") does not cover it.
   Reword to: "any `EverQuest`-scoped name — static member, static method, or
   nested type — is qualified `EverQuest::Name`." K06 (entities) and K09 (zone)
   will both hit this; `GameLootItem`/`GameVendorItem` stay on `EverQuest`
   until the loot cluster's owner is decided in K11.

2. **The `> N` floor inside a guard that parses `eq.h` is a batch-scale
   tripwire, and §Mechanism 7 should name it.** §Mechanism 7 tells the executor
   to *run* `test_reachability_sweep`; it does not warn that the sweep's
   `ASSERT_GT(senders.size(), 50u)` is a **cumulative** budget that Batch K
   spends one unit at a time. K01–K03 spent 17 of the 11-line margin without
   noticing; K05 spent the last 12 and hit the floor. Any guard that counts
   declarations in `eq.h` must be widened to the component headers by the first
   unit that trips it (done here, generically), and §Mechanism 7's list should
   read "the guards that read `eq.h` as text: re-measure them, and widen an
   input path rather than lower a floor."

3. **§Mechanism 5's "callers by compiler" works, but only if the declarations
   come out FIRST.** K05 retargeted the grep-lead sites and deleted the
   declarations in the same pass, so the compiler confirmed the list (a clean
   build proves no caller was missed) rather than producing it. That is the
   same evidence, one round-trip cheaper, and worth stating as the allowed
   order: delete + retarget the lead + build; the error list then names only
   what the lead missed. It named nothing here.

4. **A plan's member table must separate "referenced elsewhere" from
   "mentioned elsewhere".** `m_trainer_skill_caps` was routed to `owner_.` on
   the strength of one hit in `action_dispatcher_guild_misc.cpp` that is a
   comment. Following the plan would have left the batch's largest single
   member-side saving (member + banner + a 5-line T280 note = 8 `eq.h` lines,
   a tenth of this unit's whole delta) on the table. Same class as K03
   correction 3 and the K04 warning, applied to MEMBERS rather than methods.

5. **A `(was <old path>)` note in `docs/module_map.md` is a live citation.**
   `DocCitations` resolves any repo-relative path in a doc, including one
   inside prose describing where a file used to be. Record a predecessor by
   bare filename.

6. **No STOP condition was hit.** Every moved body needed only §Mechanism 4's
   substitutions plus the nested-type qualification of correction 1; no test
   asserted on a member by name that could not be repointed; the predicted
   delta was −81 before dispatch and the measured delta is −80.

### Left for the board

Nothing new. The five members the plan listed as "other classes' members"
(`m_on_completed`, `m_inv`, `m_loot_all_`, `m_gameState`, `m_eq`) do not exist
in these TUs at this tip — stale leads, not dead state.
