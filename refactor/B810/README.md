# B810 (#199): the loot window's corpse slots convert at the user boundary

Branch `task/kb-b810-corpse-slot-split`, commit 4946c6b1. Class B, the sibling of
T494/B796 (`docs/decisions.md` 2026-09-17). The model was already server-
converted; the user boundary was not: `/loot <n>` (window open) dispatched the
TYPED number straight to the wire, and every loot printout showed the model
number.

## What the fix is

ONE pair of conversion functions plus its parser, in
`include/client/inventory/inventory_constants.h`, beside T494's possessions
pair:

- `serverCorpseSlotToModel()` — port of the server's `ServerToTitaniumCorpseSlot()`
  (eqemu `titanium.cpp:3641`);
- `corpseSlotToServer()` — port of `TitaniumToServerCorpseSlot()` (`titanium.cpp:3727`);
- `parseCorpseSlot()` — the `/loot`-by-number IN hop (the same three-outcome
  shape as `parseUserSlot()`); `describeUnaddressableCorpseSlot()` — the
  refusal text.

## The table

The corpse pair and the possessions pair share the SAME three arms over the
corpse domain; the corpse pair is a distinct function only because its DOMAIN
is smaller.

| meaning                                  | server (RoF2, `inventory.slot_id`) | Titanium (model, wire) |
|------------------------------------------|------------------------------------|------------------------|
| general 1-8                              | 23-30                              | 22-29 (−1)             |
| corpse cursor + equipment range          | 33-54                              | 30-51 (−3)             |
| `POSSESSIONS_COUNT + slotAmmo`           | 56                                 | 52 (−4)                |

Server slots with no model slot: 0-22 (the looter's own gear, which a corpse
never holds), 31/32 (general 9/10 — no Titanium slot exists) and 55 (one below
ammo). `/loot 22` is therefore refused; `/loot 23` loots model 22. [measured
against the server functions' constants `rof2_limits.h`/`titanium_limits.h`,
transcribed via the issue + fable's possessions port; the eqemu tree is not in
this checkout — see "Not verified".]

## Numbering per hop

| hop                                                        | numbering |
|------------------------------------------------------------|-----------|
| wire: `OP_LootItem` in both directions, `LootItemIntent`, `m_loot_items`, window `slot` | model (Titanium) — UNCHANGED |
| `/loot <n>` (window open), typed number                     | server IN → `parseCorpseSlot()` (NEW) |
| `EqActionHandler::lootItem` "Looting … (slot N)"           | model OUT → `corpseSlotToServer()` (NEW) |
| `ActionDispatcher::lootItem` refusal "(is a loot window open?)" | model OUT → `corpseSlotToServer()` (NEW) |
| loot panel rows                                            | names only — no slots printed |

Every corpse-slot site in `src/` `include/` and the numbering each holds:

- `src/client/graphics/ui/static_ui_input.cpp:362,369` — pushes `LootItemIntent`
  with the window's model slot. Model. (Wire side; untouched.)
- `src/client/bridge/bridge_component_intents.cpp:534` — `onLootItemIntent` →
  `LootItemFromCorpse(corpseId, modelSlot, autoLoot)`. Model. (Untouched.)
- `src/client/bridge/bridge_component_ui_audio.cpp:231-232` — the ONE
  `OP_LootItem` sender; logs the model slot. Model. (Untouched.)
- `src/client/bridge/bridge_component_ui_audio.cpp:290` —
  `FindLootSlotByName` returns a model slot. Model. (Untouched.)
- `src/client/graphics/ui/loot_panel.cpp:223-249` — window list from
  `LootItemAddedData::slot` (model, server-converted before the wire). Model.
  (Untouched.)
- `src/client/graphics/ui/static_panels.h:632` — `LootItemDisplay::slot`
  comment "CORPSE slot id". Model. (Untouched.)
- `src/client/combat_hunting.cpp:584-604` — auto-loot queue add/remove over
  model slots. Model. (Untouched.)
- `src/client/combat_rounds.cpp:115` — `LootItem(slot)` → the wire. Model.
  (Untouched.)
- `src/client/merchant/merchant_trade.cpp:230-343` — the loot-item packet
  parse; publishes the model slot. Model. (Untouched.)
- `src/client/action/command_processor_loot.cpp:62-82` — THE IN hop
  (window-open, by number). Server number in, model slot out. CHANGED.
- `src/client/action/command_processor_loot.cpp:91,123` — `/loot <corpseId>`
  (window closed): the number is a corpse id, not a slot. Untouched.
- `src/client/action/eq_action_handler.cpp:641-658` — "Looting … (slot N)"
  printout. Model slot in, server number printed. CHANGED.
- `src/client/action/action_dispatcher.cpp:667-677` — loot refusal printout.
  Model slot in, server number printed. CHANGED.

No numeric-literal corpse slot was added anywhere else; the only new numeric
literals are the `static_assert`s inside the header (the table itself, pinned)
and the tests.

## RED / GREEN

The IN-hop test `CommandProcessorTest.Loot_WindowOpen_NumberIsAServerSlotConvertedToTheModelSlot`
(`tests/test_command_processor.cpp:713`) fails on the pre-fix raw-number
dispatch — the recording fake receives `lootItem(176, 23)` (the raw typed
number), not the expected `lootItem(176, 22)`:

    [ RUN      ] CommandProcessorTest.Loot_WindowOpen_NumberIsAServerSlotConvertedToTheModelSlot
    .../tests/test_command_processor.cpp:726: Failure
    Value of: handler.calledOnceWith("lootItem(176, 22)")
      Actual: false
    Expected: true
    [  FAILED  ] CommandProcessorTest.Loot_WindowOpen_NumberIsAServerSlotConvertedToTheModelSlot (0 ms)

    1 FAILED TEST

Green after: the full suites, all rebuilt after the last edit,

- `test_command_processor` 432/432 [measured]
- `test_loot_window_actions` 19/19 [measured]
- `test_action_dispatcher` 63/63 [measured]
- `test_loot_panel` 29/29 [measured]
- `test_reachability_sweep` 5/5 [measured]
- `test_input_action_bridge` 41/41 [measured]
- hygiene `ctest -L hygiene`: all doc/size tests green (FileSizeCeiling,
  DocCitations, DocClaims, DocCounts, CommentClaims). [measured]

The new `tests/test_command_processor_corpse_slots.cpp` (32 tests) pins: both
functions against the table for every slot 0-60 (dead slots included), the
IN-then-OUT round-trip over the whole domain, the dead slot 22/21/31/32/55 in
BOTH directions, the parser's three outcomes, and the command processor's IN
hop (dispatch + both refusal messages).

## Existing tests

`tests/test_command_processor.cpp` had one loot-by-number case that encoded the
old (wrong) behaviour: it asserted the raw number was dispatched. It now
asserts the converted model slot (23 → 22) — the wrong-numbering case the
card anticipated. `tests/test_loot_window_actions.cpp` used synthetic slots 0/1
in its wire assertions; those are not corpse slots (dead model slots), so they
were replaced with realistic corpse model slots (22, 23). All other
LootClick/window cases stay green untouched.

## Not verified / left open

1. **The eqemu tree is not in this checkout.** The table was transcribed from
   the issue text + fable's possessions port + the constants the server's two
   functions read (`rof2_limits.h`, `titanium_limits.h` values cited in the
   issue); every value is `static_assert`-pinned in the header. The QA card
   re-derives against the server source.
2. **CLAUDE.md (line ~916) still says "exactly two functions".** The card asks
   it to become "two pairs", but CLAUDE.md is a protected agent-instruction
   file and this worker's headless approval for it timed out — it is owed at
   review/merge time. `docs/decisions.md` carries the naming now.
3. **The three data-asset repro tests** (`TexturesReproducible`,
   `PackReproducible`, `DbReproducible`) fail in this worktree because the
   asset-rebuilder tools (`generate_textures`, `eqe1_builder`,
   `willeq_db_builder`) are not built there — an environment gap, unrelated to
   this change (which touches no data bytes). CI builds them; the full pipeline
   at the MR head is the authoritative green.
4. The RED above was demonstrated by reverting the IN-hop hunk in-tree
   (rebuild, run, restore) rather than a detached `origin/fable` worktree build
   (time). The QA card reproduces the RED independently on `origin/fable`.
