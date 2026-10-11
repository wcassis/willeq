# B603 (#187): gameplay-manager control API with no production caller

Card: `refactor/BOARD.md` B603 (struck 2026-10-11); issue #187.
Branch: `task/kb-b603-gameplay-manager-zero-callers`, cut at
`06852f3130783c808acc3472b0204a95a8e66b79` (origin/fable at cut time).

The card listed 24 functions. The T458 ratchet (`EXEMPT_TESTS_GROUPS`,
`scripts/refactor/check_zero_callers.py`) carried **20** of them under the
`"B603"` boarded reason; the other 4 (`InventoryManager::pickupMoney /
pickupPartialStack / placeOnMatchingStack / returnCursorMoney`) were **never
ratchet rows**, and they are **LIVE in production since B442's coin gesture** —
the card text predates B442. See the table below.

## Decision table

B468's rule: "nothing reads it" and "nothing CAN read it" look identical from
a grep. A production caller = not dead → keep. A function whose only caller
is a test is deleted WITH that test, unless the test is a seam netting LIVE
behaviour, in which case the function is kept and re-classified `seam`.
Every row was re-grepped tree-wide (src, include, tests, tools, benchmarks,
docker, scripts) before touching it.

| owner::name | verdict | where |
|---|---|---|
| `CombatManager::EnableAutoLoot` | **deleted** | inline setter, duplicate of `SetAutoLootEnabled()` (the `/loot` route); no caller |
| `CombatManager::GetHighestPriorityTarget` | **deleted** | its only callers were 5 test blocks; the observable scan output is `m_potential_targets`, now read directly through the T17 preamble |
| `CombatManager::SetAggroRadius` | **deleted** | setter only; `m_aggro_radius` still read by `GetNearbyHostiles()` + the hunt scan (both stay) |
| `CombatManager::SetFleeThreshold` | **deleted** | setter only; `m_flee_hp_threshold` keeps its 20.0f constructor value (the official client's flee point) |
| `CombatManager::ToggleAutoFire` | **deleted** | `/autofire` inlines the same toggle through `EnableAutoFire()`/`DisableAutoFire()` (`EqActionHandler::toggleAutoFire`); nothing called the method |
| `SpellManager::beginCastById` | **deleted** | nothing called it; the hotbar spell search calls `beginCastFromGem` directly (T483) |
| `SpellManager::setOnCastComplete` | **deleted** (+ member `m_on_cast_complete`, 4 invocation sites, typedef `SpellCastCallback`) | invocations in `spell_manager.cpp` (interrupt/complete) and `spell_packet_handlers.cpp` (`endOwnCastByServer`, `resolvePendingOutcome`); nothing ever registered the std::function. The live outcome path is the bridge publish `publishCastComplete()` (`spell_publish.cpp`), which stays |
| `SpellManager::setOnSpellFade` | **deleted** (+ member `m_on_spell_fade`, typedef `SpellFadeCallback`) | never invoked even in its own tests (its test only assigned and called the slot directly); deleted with that test |
| `SpellManager::swapGems` | **deleted** | no caller; its 4 tests (invalid slot, same slot, swap, cast/cooldown/memorize guards) went with it; the `EXPECT_FALSE(mgr->swapGems(0, 8))` line in `test_ninth_spell_gem.cpp` removed |
| `BuffManager::clearEntityBuffs` | **deleted** | no caller; `m_entity_buffs` is still read by `getEntityBuffs`/`hasEntityBuff`/`checkBuffExpiration`/`setEntityBuff`, so it is NOT made write-only. The `ExpirationAppliesToPetAndEntityBuffs` comment that named it is updated |
| `BuffManager::removeBuff` | **deleted** | no caller; the fade wire path is `removeBuffBySlot()` (stays). Test call sites (`test_levitation.cpp`, `test_vision_buff_fade_wire.cpp`) switched to `removeBuffBySlot` with the slot looked up from the player list |
| `ActiveBuff::getTimeString` | **deleted** | no caller; its test (`GetTimeStringFormats`) went with it. Cascade: `getRemainingSeconds()`'s only production caller was `getTimeString`, so deleting it made the accessor tests-only → **`getRemainingSeconds()` deleted too** (the field `remaining_seconds` is public and still read directly by production, `buff_manager.cpp`/`buff_cancel_request.cpp`); the debug prints in `test_beneficial_spell_casting.cpp` read the field |
| `ActiveBuff::isAboutToExpire` | **deleted** | no caller; its test (`IsAboutToExpireBoundary`) went with it |
| `GroupComponent::DeclineGroupInvite` | **deleted** | nothing called it. **Declining an invite IS reachable**: `/decline` (registered `command_processor_registration.cpp:179`, handler `command_processor_social.cpp:338` `cmdDecline`) → `ActionDispatcher::declineGroupInvite()` (`action_dispatcher_social.cpp:105`) → `EqActionHandler::declineGroupInvite()` (`eq_action_handler.cpp:743`), which sends the packet directly (`SendGroupDecline` is the wire half and stays). There is no UI decline button (T264 deleted `DeclineInviteIntent`). The component method was a dead duplicate. Its test (`DeclineStillConsumesThePendingInvite`) went with it; the live route is pinned by `test_command_processor_migration.cpp`'s `Decline_PendingInvite_*` tests |
| `PlayerComponent::SetMoveSpeed` | **deleted** | no caller; the live writer is the state's own `setMoveSpeed()` (from `movement_state.cpp`); the one-store test line went with it |
| `InventoryManager::pickupMoney` | **kept — LIVE** | `bridge_component_cursor.cpp:129` (`RunCursorCoinGesture`, the B442 coin gesture, reached via `RunCursorGesture` from the trade window B896 and the bank coin rows B996) |
| `InventoryManager::returnCursorMoney` | **kept — LIVE** | `bridge_component_cursor.cpp:132` (same gesture, the to-store half) |
| `InventoryManager::placeOnMatchingStack` | **kept — LIVE** | `cursor_gesture.cpp:199` (`ApplyCursorGesture`) |
| `InventoryManager::pickupPartialStack` | **kept — LIVE** | `cursor_gesture.cpp:225` (`ApplyCursorGesture`) |
| `HotbarModel::assignSlot` | **kept — seam** | no production caller; the only slot writer is `loadFromJson()` (startup config load). Their tests (T156 `test_skill_reachability.cpp` — drives `assignSlot` as the REAL producer of the `HotbarSlotAssigned` bridge event, the anti-fake-substitution net — plus T174's hotbar routing in `test_input_action_bridge.cpp` and `test_eq_action_handler_hotbar_item.cpp`) need the function. **Missing feature**: no command or UI path assigns a slot (card's second flagged read) — board separately, do not wire here |
| `HotbarModel::clearSlot` | **kept — seam** | same row; `test_skill_reachability.cpp:325` is the only caller |
| `ZoneLines::checkMovement` | **deleted** | never wired; the live poll is the 3-arg `checkPosition()` in `movement_state.cpp`; its 2 test lines went with it |
| `WhoAllEntry::isLinkdead` | **deleted** | the only caller was a test; the `unknown80[1] == kWhoStrLinkdead` compare is inlined into the test's assertions (the wire byte stays pinned directly) |
| `(free)` `calcBankBagSlotId` | **kept — seam** | test-only, but it is the wire-constant arithmetic of the bank-bag sub-slots (2031+ = `BANK_BAGS_BEGIN + (bankSlot - BANK_BEGIN) * BAG_SLOT_COUNT + bagIndex`), asserted in `test_eq_commands_equip_bags.cpp` against the same constants the server computes (B796's rules). Deleting it would unpin the wire layout. **No `inventory_constants.h` change was needed** — the function's rule is independent of B796's Titanium/server user-boundary conversions (those convert at the user boundary; this one is the raw slot layout) |

## The two flagged reads

- **`GroupComponent::DeclineGroupInvite`** — declining an invite IS reachable
  today through `/decline` (route above). The component method was a dead
  duplicate, not a missing feature. No gap to board.
- **`HotbarModel::assignSlot/clearSlot`** — the hotbar UI does NOT assign
  through another path: slots are filled only by `loadFromJson()` at startup
  and published by `publishAllSlots()` at zone-in. No command, no intent, no
  UI control calls `assignSlot`/`clearSlot`. Hotbar slot assignment is a
  **MISSING feature** — board it as its own line (see FOLLOW-UP), do not wire
  it here.

## A merge artifact caught by ctest (2026-10-11)

While this branch's first commit was being composed, a mid-lean run left the
working tree's `beginCastFromGem()` tail with `sendCastSpellPacket(spell_id,
0xFF, target_id)` — `beginCastById`'s no-gem marker (its tail is nearly
identical) — instead of `gem_slot`. Two `test_eq_action_handler_cast` tests
(`CastSpell_ProfileLoadedGem_CastsThroughSpellManager`,
`CastSpell_GemIndexIsOneBased`) went red on the wire slot (255 vs 0). The fix
(commit 3) restores `gem_slot`. After the fix, `spell_manager.cpp` differs
from origin/fable only by B603's deletions — verified by diff.

## Red / green

Ratchet: `scripts/refactor/check_zero_callers.py --repo . --check` (ctest
`ZeroCallers`, label hygiene). HEAD carried all 20 `"B603"` rows; the working
tree deletes 17 functions (18 if you count the `getRemainingSeconds` cascade,
which had no row of its own — it was a live pass-through in HEAD).

- **RED** — HEAD's ratchet (all 20 B603 rows) run against the working tree:
  exit 1, `tests-only: 1 unlisted, 17 stale` — the 17 deleted rows report
  STALE, the 3 seam rows do not (their functions still exist). Evidence:
  `kb-evidence/b603-gameplay-manager-zero-callers/RED_zero_callers.txt`.
- **GREEN** — working ratchet (17 deleted rows removed; `calcBankBagSlotId`,
  `assignSlot`, `clearSlot` re-classified `seam`) run against the working
  tree: exit 0. `zero-caller census clean: 374 functions` (unchanged — none of
  the deleted functions was in the zero bucket); `tests-only census clean:
  303 functions, all classified (boarded 82, observer 169, seam 52)`.
  Arithmetic: 320 (baseline) − 20 B603 rows + 3 seam rows = 303; boarded
  102 − 20 = 82, seam 49 + 3 = 52, observer 169 unchanged. Evidence:
  `kb-evidence/b603-gameplay-manager-zero-callers/GREEN_zero_callers.txt`.
- **WriteOnlyFields** — `check_write_only_fields.py --repo . --check`: exit 0,
  `write-only census clean: 48 fields`, `unused census clean: 86 fields` —
  unchanged from baseline (the cascade check: `m_aggro_radius`,
  `m_flee_hp_threshold`, `m_auto_loot_enabled`, `m_auto_fire_enabled`,
  `m_potential_targets`, `m_entity_buffs`, `PlayerState::m_moveSpeed` all keep
  surviving readers/writers; the two std::function members were deleted with
  their invocations, so no new write-only member). Evidence:
  `kb-evidence/b603-gameplay-manager-zero-callers/GREEN_write_only.txt`.
- `ZeroCallersSelfTest` untouched.

## Not verified / left open

- **Build + full ctest + visual gate are CI's job** (this card is offline; the
  worker box is 4 GiB-capped and only targeted local builds are run there).
  CI pipeline on the pushed head is the build/census/gate proof; the gate
  frames must be identical — no rendering-reachable path is touched by this
  diff (the bridge publishes stay; only the dead std::function slots go).
- **Hotbar slot assignment** is a missing feature (see above): board it
  separately (FOLLOW-UP on #187), do not wire it in this unit.
- The `inventory_cursor.cpp` REACHABILITY comment (`pickupMoney`) still names
  B036 as the open item — B036 is the money-slot **UI**, which is genuinely
  still absent (the coin gesture is reached via the trade window and bank
  rows, not a money-slot UI). Left as-is; it is a live B036 reference, not a
  B603 mention.
- `docs/module_map.md` line counts for the touched TUs were already stale at
  HEAD (player_state 507 vs 547, spell_manager 460 vs 389, spell_gems 378 vs
  350); they are not ratcheted by `check_doc_counts` (which checks only the
  rows it was taught) — refreshed to the measured counts as part of touching
  those rows.
- Historical unit reports under `refactor/T*.md` / `F*.md` / `K*.md` /
  `J*.md` name deleted functions (`beginCastById`, `swapGems`,
  `GetHighestPriorityTarget`, `SetAggroRadius`, `SetFleeThreshold`,
  `EnableAutoLoot`, `DeclineGroupInvite`, `SetMoveSpeed`, the callback
  setters, ...). Those are bannered/completed unit plans — historical
  snapshots per the docs layout, **not** live descriptions of current
  behaviour. `BOARD.md` B603 is struck; the one live mention of `setOnSpellFade`
  in B760's entry (a live entry) is a statement of the then-current state
  (T357's "deliberately LEFT"), which B603 now supersedes — it reads as
  history ("its own test documents that it too has no invoker"), so it is
  left rather than rewritten into B760's narrative.
