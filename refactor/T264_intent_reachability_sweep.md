# T264 — intent/handler reachability sweep (patterns #12, #13, #15 were found one at a time)

Slot: slot3. x86. NO eqemu. Class A. Territory: read the whole tree; EDIT
only (a) clearly dead producer-less intents / caller-less handlers
(delete, with the grep proof), (b) `InventoryManager::getItemById()`
returning `false` instead of `nullptr` on miss (T251 note, tiny), tests.
Do NOT touch willeq_database / builder / EQE1 resolver (T262 slot1). If a
dead intent looks like it SHOULD have a producer (a user-facing feature
that cannot be reached), do NOT wire it here — list it as a class-A
finding with the feature it blocks.

## Method (mechanical, exhaustive — this is the point)
1. `RendererIntent` variant (include/client/events/renderer_intents.h,
   ~51 structs): for each, grep producers (`pushIntent(`, `emplace`, the
   struct name constructed) outside tests, and consumers (the `case` /
   `std::visit` arm in eq_bridge.cpp ProcessBridgeIntents). Table:
   intent | producers (file:line) | consumer | verdict.
2. `GameEventType` (102 enumerators): publishers (`pushEvent(` with the
   type) and subscribers/cases (IrrlichtBridge switch, ConsoleRenderer,
   AutomatedMode, GameStateSyncBridge residue). Same table.
3. `IActionHandler` pure virtuals: which `ActionDispatcher` method calls
   each, and which command/hotkey/intent reaches that dispatcher method
   (CommandProcessor registration, InputActionBridge, hotkey table).
   Same table — this is where #12/#15 lived.
4. `EverQuest` public `Send*` methods: callers outside tests (T155/T168
   did this once; re-run it — T168 deleted the last five, more may have
   appeared).
Deliverables: the four tables in the Review; deletions for the dead ones
that block nothing; a class-A list for the dead ones that block a
feature; a test that pins reachability for anything you wire or keep
(e.g. a compile-time or gtest table check — see T223's drift-guard test
for the pattern) so the sweep never has to be repeated by hand.

## Verification
Full ctest; Review; evidence review/T264/ (the raw greps). Commit:
`T264: intent/handler reachability sweep -- <n> dead deleted, <m> blocked features listed`.

## Review (2026-08-26, slot3, task/T264-reachability-sweep from fable 331bc64c)

Evidence: `/mnt/projects/fable/review/T264/` (README.md indexes it). The raw
greps are `raw/table{1..4}_*.txt` (before) and `raw/table{1..4}_*_after.txt`
(after); the scripts that produced them are `sweep_*.sh` in the same
directory and are what the pinning test re-implements.

### Headline counts

| axis | total | dead, deleted | dead, kept (blocks a feature) | live |
|---|---|---|---|---|
| RendererIntent variant members | 54 | 19 | 15 | 20 |
| GameEventType enumerators | 102 | 6 | 0 | 96 |
| IActionHandler pure virtuals | 165 | 18 | 2 | 145 |
| EverQuest::Send* (eq.h) | 77 | 0 | 0 | 77 (2 reached only through a kept blocked intent arm) |

"Dead" for an intent = no `pushIntent(` producer anywhere outside
eq_bridge.cpp; for an event = no publisher OR no case/subscription in
irrlicht_bridge.cpp / console_bridge.cpp / console_renderer.cpp /
automated_mode.cpp (GameStateSyncBridge is a pure pass-through since T227,
so those four TUs are the only consumers); for a handler method = no
ActionDispatcher method that calls it is invoked from any production TU,
directly or through another dispatcher method (checkGMMode, the pet
wrappers); for a sender = no caller outside its own definition.

Deletion rule (from the plan): delete only what blocks nothing -- i.e. the
feature behind the route is reachable another way (command / hotkey /
another intent) or the route was a no-op stub. Anything whose target has NO
other route is kept and listed under "Blocked features" below, not wired.

One correction to the dispatch's framing of the T251 note:
`InventoryManager::getItemById()` already returns `nullptr` on every miss
path (`inventory_manager.cpp:541-563`); there is no `false` to fix. The
note was stale. No change made.

### Deleted (34 items + 6 event data structs, 1,125 lines removed)

- 19 intents (struct + variant entry + eq_bridge.cpp arm): CloseVendorIntent,
  CloseBankIntent, TradeRequestIntent, TradeAcceptIntent, TradeCancelIntent,
  ForgetSpellIntent, InterruptSpellIntent, PetCommandIntent,
  GroupInviteIntent, DisbandIntent, DeclineInviteIntent, GroupAcceptIntent,
  RequestCampIntent, RequestQuitIntent, SlashCommandIntent,
  RequestMemoryReport, RequestSceneDump, HotbarChangedIntent,
  ChatLinkClickIntent. Every one has a live command route naming the same
  last hop (table 1, verdict column), or was a LOG_TRACE no-op. Grep proof:
  `raw/table1_intents.txt` shows `(none)` under producers for each;
  `raw/table1_intents_after.txt` no longer lists them.
- 6 events (enumerator + Data struct + variant entry + every publish/case):
  PlayerFlagsChanged, EntityStateSnapshot, AutoAttackChanged (published,
  never consumed -- all three fed the GameStateSyncBridge projection that
  T227 removed; event_bus.h's own comments already said "no projection
  consumer remains"), CombatEvent, TargetHPUpdated, ToggleSkillsWindow
  (consumed, never published). `CombatManager::PublishAutoAttackChanged`
  is now `WriteAutoAttackState` (the T221 GameState write it also did).
  `IrrlichtRenderer::updateCurrentTargetHP()` went with TargetHPUpdated
  (its only caller). The target HP bar is NOT affected: OP_HPUpdate
  re-publishes TargetChanged with the new hpPercent (eq_entities.cpp:1911)
  and the TargetChanged arm writes `cachedTargetInfo_.hpPercent`.
- 18 IActionHandler pure virtuals (+ their ActionDispatcher wrapper,
  EqActionHandler override, RecordingActionHandler fake, and the
  `Direction` enum that only startMoving/stopMoving used): startMoving,
  stopMoving, moveToEntityWithinRange, targetNearest, castSpellOnTarget,
  useAbility, clickDoor, toggleSneak, startCamp, cancelCamp, moveItem,
  deleteItem, memorizeSpell (by id), openSpellbook, closeSpellbook,
  requestZone, tradeskillCombine, sendPositionUpdate. Nine of them were
  "not yet implemented" LOG stubs; the rest duplicate a live route (table
  3). Grep proof: `raw/table3_handlers.txt` (no external caller line under
  each), `raw/table3_handlers_after.txt` (147 entries).
- 0 senders. T168 deleted the last caller-less five; none has re-appeared
  (`raw/table4_senders.txt`, every entry has a caller).

### Blocked features (class A list -- NOT wired here, per the plan)

Each is a user-facing feature with a consumer arm / last hop in place and
NO producer anywhere: no slash command, no hotkey, no UI gesture. The kept
intents and handler methods are the natural target for whichever unit
wires them.

| # | feature | kept hop(s) | what is missing |
|---|---|---|---|
| F1 (CLOSED by T268 as COMMANDS: `/loot <slot|item name>`, `/lootall`, `/lootclose`; the three intents stay producer-less as the UI-gesture leg -- no loot popup exists -- and DestroyAllLootIntent was deleted, Titanium has no destroy-loot packet) | Loot window: take ONE item, loot all, destroy all, close the window | LootItemIntent/LootAllIntent/DestroyAllLootIntent/CloseLootIntent arms -> EverQuest::LootItemFromCorpse / LootAllFromCorpse / DestroyAllCorpseLoot / CloseLootWindow; IActionHandler::lootItem/lootAll (dispatcher methods exist, no command) | `/loot <id>` only OPENS the corpse; with `/autoloot on` CombatManager's own LootSubState machine takes everything, with it off nothing can take an item. No loot panel exists in the static UI. Note: EverQuest::LootAllFromCorpse (m_loot_all_in_progress loop) and CombatManager::LootAll (LootSubState) are two drifted loot-all implementations. |
| F2 | Read a book / note item | ReadItemIntent arm -> RequestReadBook -> SendReadBookRequest | CLOSED by T273: `/read` (slot or item name) is the producer |
| F3 | Bank coins: deposit / withdraw / convert | BankCurrencyMoveIntent / BankCurrencyConvertIntent arms (build MoveCoin_Struct, SendMoveCoin) | `/bank` opens the window; nothing moves coins. TradeManager's SendMoveCoin callback is the trade-window path, not the bank. |
| F4 | Spellbook popup: click a spell to memorize; open/close pose | MemorizeSpellIntent arm -> SpellManager::memorizeSpell; SpellbookStateIntent arm -> SendSpawnAppearance(AT_ANIMATION 110/100) | the popup EXISTS (RA::ToggleSpellbook, SpellbookPopupState) and its click handler is a LOG stub that says "would need MemorizeSpellIntent" (static_ui_input.cpp:111). `/mem` covers memorize by command, so this is the UI leg only. `cmdSpellbook` still prints "Spellbook window not yet implemented in UI" -- stale, same class as T223's /newskills. |
| F5 | Scribe a spell scroll | ScribeSpellIntent arm -> EverQuest::ScribeSpellFromScroll (level/class/already-scribed checks, sends the packets) | **CLOSED by T276**: `/scribe <slot\|name>` is the producer. The arm also assumed the scroll was already on the cursor (nothing put it there) and the intent's `bookSlot` was `uint8_t` against a 400-slot book. Still no MOUSE gesture -- the inventory popup's `SlotDisplayInfo` carries no item id or scroll effect. |
| F6 | Click off a buff | BuffCancelIntent arm -> BuffManager::removeBuffBySlot | no command, the buff window has no click handler |
| F7 | Manual tradeskill combine + closing the container | TradeskillCombineIntent arm -> SendTradeSkillCombine; TradeskillCloseIntent arm -> SendCloseContainer (these two senders have NO other caller) | T149's recipe auto-combine is a different server mechanism; the classic combine-what-I-put-in-the-container has no route, and an opened world container is never closed |
| F8 | Train a skill at a trainer; close the trainer window | TrainSkillIntent arm -> EverQuest::TrainSkill (sends GMTrainSkill); CloseTrainerIntent arm -> CloseTrainerWindow (sends GMEndTraining) | `/train` opens the window (RequestTraining); nothing trains or closes |

Not in the four tables but found on the way, for the board:
- `IOutputRenderer::updateCurrentTargetHP` (output_renderer.h:197) and its
  Null/Console implementations have no caller -- the same dead route on
  the headless-renderer interface. Left alone (not one of the four axes).
- The InputAction axis (hotkey -> `InputAction` -> InputActionBridge) was
  NOT swept; only its dispatcher calls were (they are all live).
- Root `CLAUDE.md` ("51 intent structs", "102 event types", "99-case
  switch over the 101 enumerators"), `include/client/state/CLAUDE.md` and
  `include/client/bridge/CLAUDE.md` now carry stale counts (35 / 96 / 96).
  Docs are T263's territory this cycle; not touched.

### The pinning test

`tests/test_reachability_sweep.cpp` (new target, links only gtest) parses
renderer_intents.h, eq_bridge.cpp, event_bus.h, action_dispatcher.{h,cpp},
eq.h and every `.cpp`/`.h` under `src/` and `include/` (`WILLEQ_REPO_ROOT`,
the T223 idiom) and asserts, on every ctest:
1. every variant member has an eq_bridge.cpp arm;
2. the producer-less intent set EQUALS `kBlockedIntents` (the F1-F8 list),
   in both directions;
3. every GameEventType enumerator has a publisher and a consumer;
4. the unreachable IActionHandler set EQUALS `kBlockedHandlerMethods`
   ({lootItem, lootAll}), computed through the dispatcher call graph with
   the internal-call closure (so isGMMode via checkGMMode and
   sendPetCommand via the pet wrappers count as reached);
5. every eq.h `Send*` has a caller.
Wiring a listed intent fails with "no longer producer-less ... remove it
from the list"; a new orphan fails with its name. Seven red checks
(`red_checks.sh`, logs `raw/red_*.log`), each made by editing SOURCE TEXT
and re-running the already-built binary: deleting the TargetIntent
producer, renaming the ZoningEnabledIntent arm, renaming the DoorSpawned
case, renaming the HotbarCooldownStarted publish, renaming the
`m_dispatcher.bankToggle()` call, renaming the `SendSave()` call, and
adding a fake ReadItemIntent producer -- 7/7 detected with the expected
name in the message. Runtime ~12 s.

### The four tables (before-state; `raw/*_after.txt` has the after-state)

### Table 1 -- RendererIntent variant (54 before, 35 after)

| intent | producers (before) | consumer arm (before) | verdict |
|---|---|---|---|
| `PlayerPositionChanged` | src/client/graphics/renderer_player_physics.cpp:777 | eq_bridge.cpp:812 | LIVE |
| `TargetIntent` | src/client/graphics/renderer_overlays.cpp:598 | eq_bridge.cpp:819 | LIVE |
| `ChatSubmitIntent` | src/client/graphics/renderer_frame.cpp:884 | eq_bridge.cpp:1025 | LIVE |
| `DoorInteractIntent` | src/client/graphics/renderer_frame.cpp:605, src/client/graphics/renderer_overlays.cpp:611 | eq_bridge.cpp:895 | LIVE |
| `LootCorpseIntent` | src/client/graphics/renderer_overlays.cpp:590 | eq_bridge.cpp:892 | LIVE |
| `LootItemIntent` | (none) | eq_bridge.cpp:1085 | KEPT, BLOCKED: loot one item (F1) |
| `LootAllIntent` | (none) | eq_bridge.cpp:1088 | KEPT, BLOCKED: loot all (F1) |
| `DestroyAllLootIntent` | (none) | eq_bridge.cpp:1091 | KEPT, BLOCKED: destroy all loot (F1) |
| `CloseLootIntent` | (none) | eq_bridge.cpp:1094 | KEPT, BLOCKED: close loot window (F1) |
| `WorldObjectInteractIntent` | src/client/graphics/renderer_frame.cpp:613, src/client/graphics/renderer_overlays.cpp:621 | eq_bridge.cpp:1010 | LIVE |
| `ZoningEnabledIntent` | src/client/graphics/renderer_overlays.cpp:825 | eq_bridge.cpp:1021 | LIVE |
| `ReadItemIntent` | eq_commands.cpp (`RequestReadItem`, T273) | eq_bridge.cpp:1028 | WIRED by T273: `/read` -- off `kBlockedIntents` |
| `VendorToggleIntent` | src/client/graphics/renderer_frame.cpp:595, src/client/eq_commands.cpp:502 | eq_bridge.cpp:898 | LIVE |
| `VendorBuyIntent` | src/client/eq_commands.cpp:533 | eq_bridge.cpp:1098 | LIVE |
| `VendorSellIntent` | src/client/eq_commands.cpp:554 | eq_bridge.cpp:1101 | LIVE |
| `CloseVendorIntent` | (none) | eq_bridge.cpp:1104 | DELETED -- /vendor (VendorToggleIntent arm) closes an open window |
| `BankerInteractIntent` | src/client/graphics/renderer_overlays.cpp:594, src/client/eq_commands_spells.cpp:400 | eq_bridge.cpp:934 | LIVE |
| `BankCurrencyMoveIntent` | (none) | eq_bridge.cpp:1111 | KEPT, BLOCKED: bank coin deposit/withdraw (F3) |
| `BankCurrencyConvertIntent` | (none) | eq_bridge.cpp:1189 | KEPT, BLOCKED: bank coin conversion (F3) |
| `CloseBankIntent` | (none) | eq_bridge.cpp:1108 | DELETED -- /bank (RequestBankToggle) closes an open window |
| `TradeRequestIntent` | (none) | eq_bridge.cpp:1256 | DELETED -- /trade -> EqActionHandler::requestTrade |
| `TradeAcceptIntent` | (none) | eq_bridge.cpp:1265 | DELETED -- /accept -> EqActionHandler::acceptTrade (T259) |
| `TradeCancelIntent` | (none) | eq_bridge.cpp:1268 | DELETED -- /tradecancel -> EqActionHandler::cancelTrade |
| `TrainerToggleIntent` | src/client/graphics/renderer_frame.cpp:598 | eq_bridge.cpp:973 | LIVE |
| `CastSpellIntent` | src/client/graphics/ui/static_ui_input.cpp:67 | eq_bridge.cpp:1035 | LIVE |
| `MemorizeSpellIntent` | (none) | eq_bridge.cpp:1431 | KEPT, BLOCKED (UI leg only): spellbook-popup click -> memorize (F4); /mem exists |
| `ForgetSpellIntent` | (none) | eq_bridge.cpp:1054 | DELETED -- /forget -> EqActionHandler::forgetSpell |
| `ScribeSpellIntent` | src/client/eq_commands.cpp (`RequestScribeItem`, T276) | eq_bridge.cpp:1059 | LIVE -- `/scribe` (T276) |
| `SpellbookStateIntent` | (none) | eq_bridge.cpp:1062 | KEPT, BLOCKED: spellbook open/close pose (F4) |
| `InterruptSpellIntent` | (none) | eq_bridge.cpp:1065 | DELETED -- /stopcast -> EqActionHandler::interruptCast |
| `BuffCancelIntent` | (none) | eq_bridge.cpp:1069 | KEPT, BLOCKED: click off a buff (F6) |
| `SkillActivateIntent` | src/client/graphics/ui/static_ui_input.cpp:157 | eq_bridge.cpp:1075 | LIVE |
| `PetCommandIntent` | (none) | eq_bridge.cpp:1272 | DELETED -- /pet -> EqActionHandler::sendPetCommand |
| `GroupInviteIntent` | (none) | eq_bridge.cpp:1275 | DELETED -- /invite -> EqActionHandler::inviteToGroup |
| `DisbandIntent` | (none) | eq_bridge.cpp:1279 | DELETED -- /disband -> EqActionHandler::leaveGroup |
| `DeclineInviteIntent` | (none) | eq_bridge.cpp:1285 | DELETED -- /decline -> EqActionHandler::declineGroupInvite |
| `RequestCampIntent` | (none) | eq_bridge.cpp:1289 | DELETED -- arm was ProcessChatInput("/camp"); /camp exists |
| `RequestQuitIntent` | (none) | eq_bridge.cpp:1292 | DELETED -- arm was ProcessChatInput("/q"); /q exists |
| `SlashCommandIntent` | (none) | eq_bridge.cpp:1422 | DELETED -- arm was ProcessChatInput(text); ChatSubmitIntent carries that |
| `RequestMemoryReport` | (none) | eq_bridge.cpp:1425 | DELETED -- arm was a LOG_TRACE no-op |
| `RequestSceneDump` | (none) | eq_bridge.cpp:1428 | DELETED -- arm was a LOG_TRACE no-op |
| `HotbarChangedIntent` | (none) | eq_bridge.cpp:1295 | DELETED -- arm was a LOG_TRACE no-op |
| `MusicVolumeChangeIntent` | src/client/graphics/renderer_frame.cpp:815 | eq_bridge.cpp:1299 | LIVE |
| `EffectsVolumeChangeIntent` | src/client/graphics/renderer_frame.cpp:819 | eq_bridge.cpp:1308 | LIVE |
| `GroupAcceptIntent` | (none) | eq_bridge.cpp:1318 | DELETED -- /accept -> EqActionHandler::acceptGroupInvite |
| `HotbarActivateIntent` | src/client/graphics/ui/static_ui_input.cpp:49 | eq_bridge.cpp:1322 | LIVE |
| `ChatLinkClickIntent` | (none) | eq_bridge.cpp:1388 | DELETED -- arm was ZoneSendChannelMessage(say); no chat-link UI exists, /say exists |
| `TradeskillCombineIntent` | (none) | eq_bridge.cpp:1395 | KEPT, BLOCKED: manual combine (F7) |
| `TradeskillCloseIntent` | (none) | eq_bridge.cpp:1398 | KEPT, BLOCKED: close tradeskill container (F7) |
| `TrainSkillIntent` | (none) | eq_bridge.cpp:1405 | KEPT, BLOCKED: train a skill (F8) |
| `CloseTrainerIntent` | (none) | eq_bridge.cpp:1408 | KEPT, BLOCKED: close trainer window (F8) |
| `MoveItemIntent` | src/client/inventory/inventory_manager.cpp:805, src/client/inventory/inventory_manager.cpp:887 (+9) | eq_bridge.cpp:1412 | LIVE |
| `DeleteItemIntent` | src/client/inventory/inventory_manager.cpp:2099, src/client/inventory/inventory_manager.cpp:2137 | eq_bridge.cpp:1415 | LIVE |
| `EquipmentChangedIntent` | src/client/inventory/inventory_manager.cpp:810, src/client/inventory/inventory_manager.cpp:892 (+3) | eq_bridge.cpp:1418 | LIVE |

### Table 2 -- GameEventType (102 before, 96 after)

| event | publishers (before) | consumers (before) | verdict |
|---|---|---|---|
| `PlayerMoved` | player_state.cpp:323, eq_commands_movement.cpp:278 (+6) | irrlicht_bridge.cpp:21 | LIVE |
| `PlayerStatsChanged` | player_state.cpp:337, eq_entities.cpp:2028 (+3) | console_bridge.cpp:47, irrlicht_bridge.cpp:27 | LIVE |
| `PlayerPositionStateChanged` | eq_movement.cpp:1516 | irrlicht_bridge.cpp:38 | LIVE |
| `PlayerMovementModeChanged` | eq_movement.cpp:1478 | irrlicht_bridge.cpp:41 | LIVE |
| `PlayerFlagsChanged` | eq_player_info.cpp:841 | (none) | DELETED (published by SetAFK/SetAnonymous/SetRoleplay, consumed by nothing; T218 fed a projection T227 removed; the setters write GameState directly) |
| `EntitySpawned` | entity_manager.cpp:358, eq_entities.cpp:1666 (+1) | console_bridge.cpp:16, irrlicht_bridge.cpp:48, console_renderer.cpp:89 | LIVE |
| `EntityDespawned` | entity_manager.cpp:367, eq_bridge.cpp:696 | console_bridge.cpp:23, irrlicht_bridge.cpp:74, console_renderer.cpp:92 | LIVE |
| `EntityMoved` | entity_manager.cpp:383, eq_bridge.cpp:731 | irrlicht_bridge.cpp:80, game_state_bridge.h:110, game_state_bridge.h:122 | LIVE |
| `EntityStateSnapshot` | eq_player_info.cpp:333 | (none) | DELETED (published per spawn with a whole Entity copy, consumed by nothing; T81 fed a projection T227 removed) |
| `EntityStatsChanged` | entity_manager.cpp:394, eq_entities.cpp:1911 | irrlicht_bridge.cpp:87 | LIVE |
| `EntityAppearanceChanged` | eq_entities.cpp:2177, eq_entities.cpp:2287 (+1) | irrlicht_bridge.cpp:91 | LIVE |
| `EntityLightChanged` | eq_entities.cpp:945, eq_entities.cpp:1679 (+1) | irrlicht_bridge.cpp:112 | LIVE |
| `EntityAnimationEvent` | skill_manager.cpp:721, eq_entities.cpp:772 (+3) | irrlicht_bridge.cpp:118 | LIVE |
| `EntityWeaponSkillsChanged` | eq_bridge.cpp:1494 | irrlicht_bridge.cpp:461 | LIVE |
| `EntityPoseStateChanged` | eq_entities.cpp:763, eq_bridge.cpp:546 | irrlicht_bridge.cpp:126 | LIVE |
| `EntityDeathAnimation` | eq_entities.cpp:786, eq.cpp:3387 | irrlicht_bridge.cpp:133 | LIVE |
| `CorpseDecayStarted` | eq_bridge.cpp:686 | irrlicht_bridge.cpp:139 | LIVE |
| `CombatAnimation` | eq_combat_actions.cpp:412 | irrlicht_bridge.cpp:145 | LIVE |
| `CombatSkillAnimation` | eq.cpp:2640, eq.cpp:2650 (+4) | irrlicht_bridge.cpp:152 | LIVE |
| `ReceivedDamageAnimation` | eq.cpp:2709 | irrlicht_bridge.cpp:158 | LIVE |
| `PlayerSpawnIdSet` | eq_entities.cpp:405, eq_entities.cpp:1231 (+1) | irrlicht_bridge.cpp:164 | LIVE |
| `NetworkReady` | eq_entities.cpp:1699, eq_bridge.cpp:185 | irrlicht_bridge.cpp:170 | LIVE |
| `DoorSpawned` | game_state.cpp:31, eq_entities.cpp:602 | irrlicht_bridge.cpp:183 | LIVE |
| `DoorStateChanged` | game_state.cpp:37, eq_entities.cpp:2463 | irrlicht_bridge.cpp:190 | LIVE |
| `ZoneChanged` | world_state.cpp:121, eq.cpp:2022 | console_bridge.cpp:87, irrlicht_bridge.cpp:198, console_renderer.cpp:102 | LIVE |
| `ZoneLoading` | world_state.cpp:144, eq.cpp:369 | irrlicht_bridge.cpp:202 | LIVE |
| `ZoneLoaded` | eq.cpp:445, eq.cpp:466 (+1) | console_bridge.cpp:93, irrlicht_bridge.cpp:209 | LIVE |
| `ChatMessage` | eq_chat.cpp:151, eq_chat.cpp:246 (+5) | console_bridge.cpp:30, irrlicht_bridge.cpp:217, console_renderer.cpp:84 | LIVE |
| `SystemMessage` | irrlicht_renderer.cpp:1057, eq_chat.cpp:291 (+2) | console_bridge.cpp:39, irrlicht_bridge.cpp:232 | LIVE |
| `CombatEvent` | (none) | console_bridge.cpp:55, irrlicht_bridge.cpp:243, console_renderer.cpp:97 | DELETED (3 consumer cases, no publisher anywhere; combat text arrives as ChatMessage/DamageEvent) |
| `TargetChanged` | combat_state.cpp:88, eq_entities.cpp:1932 (+4) | console_bridge.cpp:75, irrlicht_bridge.cpp:248, console_renderer.cpp:107 | LIVE |
| `TargetHPUpdated` | (none) | irrlicht_bridge.cpp:281 | DELETED (1 consumer case, no publisher; OP_HPUpdate re-publishes TargetChanged with the new hpPercent -- eq_entities.cpp:1911 -- so the target HP bar was never fed by this) |
| `AutoAttackChanged` | combat.cpp:338 | (none) | DELETED (published by Enable/DisableAutoAttack, consumed by nothing; the choke point writes GameState::combat() directly -- now WriteAutoAttackState) |
| `DamageEvent` | eq_combat_actions.cpp:383 | irrlicht_bridge.cpp:289 | LIVE |
| `SpellCastStarted` | spell_manager.cpp:1258, eq_player_info.cpp:494 | irrlicht_bridge.cpp:295 | LIVE |
| `SpellCastComplete` | spell_manager.cpp:1277 | irrlicht_bridge.cpp:300 | LIVE |
| `GroupChanged` | group_state.cpp:156, eq_group.cpp:466 (+6) | console_bridge.cpp:100, irrlicht_bridge.cpp:307 | LIVE |
| `GroupMemberUpdated` | group_state.cpp:176, eq_group.cpp:480 (+1) | irrlicht_bridge.cpp:321 | LIVE |
| `GroupInviteReceived` | group_state.cpp:111, eq_group.cpp:371 | irrlicht_bridge.cpp:334 | LIVE |
| `TimeOfDayChanged` | world_state.cpp:133, eq_commands_movement.cpp:312 (+1) | irrlicht_bridge.cpp:339 | LIVE |
| `PetCreated` | pet_state.cpp:112, eq_bridge.cpp:576 (+1) | irrlicht_bridge.cpp:347, console_renderer.cpp:112, console_renderer.cpp:223 | LIVE |
| `PetRemoved` | pet_state.cpp:121, eq_bridge.cpp:592 | irrlicht_bridge.cpp:359, console_renderer.cpp:115, console_renderer.cpp:230 | LIVE |
| `PetStatsChanged` | pet_state.cpp:131, eq_entities.cpp:1948 | irrlicht_bridge.cpp:365, console_renderer.cpp:118, console_renderer.cpp:238 | LIVE |
| `PetButtonStateChanged` | pet_state.cpp:140, eq_bridge.cpp:609 | irrlicht_bridge.cpp:372 | LIVE |
| `VendorWindowOpened` | player_state.cpp:182, eq_merchant.cpp:1005 | irrlicht_bridge.cpp:383 | LIVE |
| `VendorWindowClosed` | player_state.cpp:195, eq_merchant.cpp:1190 (+1) | irrlicht_bridge.cpp:386 | LIVE |
| `VendorItemAdded` | eq_merchant.cpp:1249 | irrlicht_bridge.cpp:389 | LIVE |
| `BankWindowOpened` | player_state.cpp:209, eq_merchant.cpp:1400 | irrlicht_bridge.cpp:392 | LIVE |
| `BankWindowClosed` | player_state.cpp:220, eq_merchant.cpp:1429 | irrlicht_bridge.cpp:395 | LIVE |
| `TrainerWindowOpened` | eq_merchant.cpp:1540 | irrlicht_bridge.cpp:398 | LIVE |
| `TrainerWindowClosed` | player_state.cpp:242, eq_merchant.cpp:1608 | irrlicht_bridge.cpp:401 | LIVE |
| `TradeskillContainerOpened` | eq_entities.cpp:2668 | irrlicht_bridge.cpp:404 | LIVE |
| `TradeskillContainerClosed` | eq_entities.cpp:2682 | irrlicht_bridge.cpp:407 | LIVE |
| `InventorySlotChanged` | inventory_state.cpp:219, eq.cpp:641 (+5) | irrlicht_bridge.cpp:412 | LIVE |
| `CursorItemChanged` | inventory_state.cpp:230 | irrlicht_bridge.cpp:440 | LIVE |
| `EquipmentStatsChanged` | inventory_state.cpp:244, eq_bridge.cpp:1536 | irrlicht_bridge.cpp:444 | LIVE |
| `CurrencyChanged` | eq_bridge.cpp:1178, eq_bridge.cpp:1545 (+2) | irrlicht_bridge.cpp:452 | LIVE |
| `BankCurrencyChanged` | eq_bridge.cpp:1186, eq_bridge.cpp:1252 | irrlicht_bridge.cpp:458 | LIVE |
| `LootWindowOpened` | eq_bridge.cpp:1581 | irrlicht_bridge.cpp:469 | LIVE |
| `LootWindowClosed` | eq_entities.cpp:1799, eq_bridge.cpp:1705 | irrlicht_bridge.cpp:472 | LIVE |
| `LootItemAdded` | eq_merchant.cpp:839 | irrlicht_bridge.cpp:475 | LIVE |
| `LootItemRemoved` | eq_merchant.cpp:913 | irrlicht_bridge.cpp:478 | LIVE |
| `TradeRequestReceived` | trade_state.cpp:41, eq_merchant.cpp:580 | irrlicht_bridge.cpp:483 | LIVE |
| `TradeStarted` | trade_state.cpp:28, eq_merchant.cpp:606 | irrlicht_bridge.cpp:486 | LIVE |
| `TradeItemUpdated` | trade_state.cpp:112, eq_merchant.cpp:636 | irrlicht_bridge.cpp:489 | LIVE |
| `TradeMoneyUpdated` | trade_state.cpp:71, eq_merchant.cpp:657 | irrlicht_bridge.cpp:492 | LIVE |
| `TradeAcceptStateChanged` | trade_state.cpp:53, eq_merchant.cpp:668 | irrlicht_bridge.cpp:495 | LIVE |
| `TradeCancelled` | eq_merchant.cpp:613, eq_merchant.cpp:748 | irrlicht_bridge.cpp:498 | LIVE |
| `TradeCompleted` | eq_merchant.cpp:730 | irrlicht_bridge.cpp:501 | LIVE |
| `SpellGemChanged` | spell_manager.cpp:1310, spell_state.cpp:284 (+1) | irrlicht_bridge.cpp:506 | LIVE |
| `CastingStateChanged` | spell_state.cpp:298 | irrlicht_bridge.cpp:522, console_renderer.cpp:123, console_renderer.cpp:244 | LIVE |
| `SpellMemorizing` | spell_state.cpp:312 | irrlicht_bridge.cpp:528 | LIVE |
| `BuffUpdated` | eq_player_info.cpp:649, eq.cpp:617 | irrlicht_bridge.cpp:534 | LIVE |
| `BuffRemoved` | eq_player_info.cpp:623 | irrlicht_bridge.cpp:547 | LIVE |
| `VisionChanged` | eq.cpp:255 | irrlicht_bridge.cpp:555 | LIVE |
| `SkillValueChanged` | eq.cpp:1223 | irrlicht_bridge.cpp:564 | LIVE |
| `SkillsRefreshed` | eq.cpp:2360 | irrlicht_bridge.cpp:576 | LIVE |
| `SkillsSnapshot` | eq.cpp:597 | irrlicht_bridge.cpp:579 | LIVE |
| `WeatherChanged` | eq_entities.cpp:1035 | irrlicht_bridge.cpp:596 | LIVE |
| `SwimmingStateChanged` | eq_movement.cpp:1832 | irrlicht_bridge.cpp:602 | LIVE |
| `CollisionMapChanged` | application.cpp:1675, application.cpp:1789 (+4) | irrlicht_bridge.cpp:610 | LIVE |
| `ZoneLineBoundingBoxes` | eq_entities.cpp:657 | irrlicht_bridge.cpp:616 | LIVE |
| `ExpProgressChanged` | eq_entities.cpp:1287, eq.cpp:529 | irrlicht_bridge.cpp:623 | LIVE |
| `CharacterInfoChanged` | eq.cpp:519, eq.cpp:2567 | irrlicht_bridge.cpp:631 | LIVE |
| `WorldObjectSpawned` | eq_entities.cpp:1000 | irrlicht_bridge.cpp:644 | LIVE |
| `NoteWindowOpened` | eq_merchant.cpp:427 | irrlicht_bridge.cpp:653 | LIVE |
| `SpellScribeCompleted` | eq.cpp:1539 | irrlicht_bridge.cpp:659 | LIVE |
| `SpellbookSnapshot` | eq.cpp:580 | irrlicht_bridge.cpp:671 | LIVE |
| `ToggleSkillsWindow` | (none) | irrlicht_bridge.cpp:722 | DELETED (LOG_TRACE-only case, publisher deleted by T223) |
| `HotbarCooldownStarted` | eq_bridge.cpp:2056 | irrlicht_bridge.cpp:696 | LIVE |
| `HotbarSlotAssigned` | hotbar_model.cpp:129 | irrlicht_bridge.cpp:687 | LIVE |
| `SkillActivationFeedback` | eq.cpp:130 | irrlicht_bridge.cpp:708 | LIVE |
| `RendererCommand` | application.cpp:1249 | irrlicht_bridge.cpp:711 | LIVE |
| `SpellCastVisualStarted` | spell_manager.cpp:945, spell_manager.cpp:982 | irrlicht_bridge.cpp:729 | LIVE |
| `SpellCastVisualComplete` | spell_manager.cpp:118, spell_manager.cpp:1043 | irrlicht_bridge.cpp:754 | LIVE |
| `SpellCastVisualInterrupted` | spell_manager.cpp:1084 | irrlicht_bridge.cpp:782 | LIVE |
| `SpellMemorizeVisualStarted` | spell_manager.cpp:677 | irrlicht_bridge.cpp:801 | LIVE |
| `SpellMemorizeVisualComplete` | spell_manager.cpp:167 | irrlicht_bridge.cpp:805 | LIVE |
| `ZoneUnloading` | eq_zone.cpp:348 | irrlicht_bridge.cpp:812 | LIVE |
| `NavmeshChanged` | eq_movement.cpp:1282, eq_movement.cpp:1311 (+1) | irrlicht_bridge.cpp:819 | LIVE |
| `DiagnosticsMemoryReport` | eq_commands.cpp:240 | irrlicht_bridge.cpp:827 | LIVE |
| `DiagnosticsSceneDump` | eq_commands.cpp:252 | irrlicht_bridge.cpp:883 | LIVE |

### Table 3 -- IActionHandler pure virtuals (165 before, 147 after)

| IActionHandler pure virtual | ActionDispatcher method(s) | reached from (before) | verdict |
|---|---|---|---|
| `acceptGroupInvite` | acceptGroupInvite | command_processor.cpp:1327 -> acceptGroupInvite | LIVE |
| `acceptTrade` | acceptTrade | command_processor.cpp:1587 -> acceptTrade | LIVE |
| `activateHotbarCommandSlot` | activateHotbarCommandSlot | input_action_bridge.cpp:343 -> activateHotbarCommandSlot | LIVE |
| `assist` | assist | command_processor.cpp:1529 -> assist | LIVE |
| `assistGroup` | assistGroup | command_processor.cpp:2698 -> assistGroup | LIVE |
| `bandage` | bandage | command_processor.cpp:1974 -> bandage | LIVE |
| `bankToggle` | bankToggle | command_processor.cpp:2206 -> bankToggle | LIVE |
| `beginCastFromGem` | beginCastFromGem | command_processor.cpp:2242 -> beginCastFromGem | LIVE |
| `bug` | bug | command_processor.cpp:1985 -> bug | LIVE |
| `buyFromVendor` | buyFromVendor | command_processor.cpp:2062 -> buyFromVendor | LIVE |
| `campNow` | camp | command_processor.cpp:2190 -> camp | LIVE |
| `cancelCamp` | cancelCamp | (none) | DELETED (no caller; EverQuest::CancelCamp fires on movement, eq_movement.cpp:1527) |
| `cancelTrade` | cancelTrade | command_processor.cpp:1595 -> cancelTrade | LIVE |
| `castSpell` | castSpell | input_action_bridge.cpp:262 -> castSpell, input_action_bridge.cpp:300 -> castSpell (+1) | LIVE |
| `castSpellOnTarget` | castSpellOnTarget | (none) | DELETED (no caller; /cast <gem> <target> is beginCastFromGem(gem, target)) |
| `clearTarget` | clearTarget | input_action_bridge.cpp:90 -> clearTarget | LIVE |
| `clickDoor` | clickDoor | (none) | DELETED (no caller; DoorInteractIntent + clickNearestDoor are the door routes) |
| `clickNearestDoor` | clickNearestDoor, interactNearest | input_action_bridge.cpp:147 -> clickNearestDoor, command_processor.cpp:1128 -> clickNearestDoor (+1) | LIVE |
| `clickWorldObject` | clickWorldObject | command_processor.cpp:1793 -> clickWorldObject | LIVE |
| `closeSpellbook` | closeSpellbook | (none) | DELETED (LOG stub) |
| `consent` | consent | command_processor.cpp:1771 -> consent | LIVE |
| `consentDeny` | consentDeny | command_processor.cpp:1937 -> consentDeny | LIVE |
| `consider` | consider | input_action_bridge.cpp:95 -> consider, command_processor.cpp:1073 -> consider | LIVE |
| `corpseDrag` | corpseDrag | command_processor.cpp:1760 -> corpseDrag | LIVE |
| `corpseDrop` | corpseDrop | command_processor.cpp:1764 -> corpseDrop | LIVE |
| `cycleTargets` | cycleTargets | input_action_bridge.cpp:132 -> cycleTargets | LIVE |
| `cycleTargetsReverse` | cycleTargetsReverse | input_action_bridge.cpp:136 -> cycleTargetsReverse | LIVE |
| `debugLevel` | debugLevel | command_processor.cpp:2194 -> debugLevel | LIVE |
| `declineGroupInvite` | declineGroupInvite | command_processor.cpp:1305 -> declineGroupInvite | LIVE |
| `declineTrade` | declineTrade | command_processor.cpp:1591 -> declineTrade | LIVE |
| `deleteItem` | deleteItem | (none) | DELETED (LOG stub; InventoryManager pushes DeleteItemIntent) |
| `disableAutoAttack` | disableAutoAttack | command_processor.cpp:1400 -> disableAutoAttack | LIVE |
| `disableCombatManager` | disableCombatManager | command_processor.cpp:1426 -> disableCombatManager | LIVE |
| `dismissPet` | dismissPet | command_processor.cpp:2394 -> dismissPet | LIVE |
| `duelAccept` | duelAccept | command_processor.cpp:1554 -> duelAccept | LIVE |
| `duelDecline` | duelDecline | command_processor.cpp:1558 -> duelDecline | LIVE |
| `duelRequest` | duelRequest | command_processor.cpp:1545 -> duelRequest | LIVE |
| `dumpEntityAppearance` | dumpEntityAppearance | command_processor.cpp:2497 -> dumpEntityAppearance | LIVE |
| `enableAutoAttack` | enableAutoAttack | input_action_bridge.cpp:86 -> enableAutoAttack, command_processor.cpp:1396 -> enableAutoAttack | LIVE |
| `enableCombatManager` | enableCombatManager | command_processor.cpp:1424 -> enableCombatManager | LIVE |
| `equip` | equip | command_processor.cpp:2068 -> equip, command_processor.cpp:2070 -> equip | LIVE |
| `findItemSlotByItemId` | useItemById | input_action_bridge.cpp:331 -> useItemById | LIVE |
| `findPerson` | findPerson | command_processor.cpp:2716 -> findPerson | LIVE |
| `followEntity` | followEntity | command_processor.cpp:2173 -> followEntity | LIVE |
| `forgetSpell` | forgetSpell | command_processor.cpp:2275 -> forgetSpell | LIVE |
| `friendsWho` | friendsWho | command_processor.cpp:2726 -> friendsWho | LIVE |
| `gmEmoteZone` | gmEmoteZone | command_processor.cpp:2826 -> gmEmoteZone | LIVE |
| `gmFind` | gmFind | command_processor.cpp:2785 -> gmFind | LIVE |
| `gmGoto` | gmGoto | command_processor.cpp:2778 -> gmGoto | LIVE |
| `gmHideMe` | gmHideMe | command_processor.cpp:2814 -> gmHideMe, command_processor.cpp:2817 -> gmHideMe | LIVE |
| `gmKick` | gmKick | command_processor.cpp:2792 -> gmKick | LIVE |
| `gmKill` | gmKill | command_processor.cpp:2799 -> gmKill | LIVE |
| `gmLastName` | gmLastName | command_processor.cpp:2836 -> gmLastName | LIVE |
| `gmSummon` | gmSummon | command_processor.cpp:2771 -> gmSummon | LIVE |
| `gmZoneRequest` | gmZoneRequest | command_processor.cpp:2764 -> gmZoneRequest | LIVE |
| `guildAcceptInvite` | guildAcceptInvite | command_processor.cpp:1717 -> guildAcceptInvite | LIVE |
| `guildDeclineInvite` | guildDeclineInvite | command_processor.cpp:1721 -> guildDeclineInvite | LIVE |
| `guildDemote` | guildDemote | command_processor.cpp:1735 -> guildDemote | LIVE |
| `guildInvite` | guildInvite | command_processor.cpp:1713 -> guildInvite | LIVE |
| `guildMakeLeader` | guildMakeLeader | command_processor.cpp:1742 -> guildMakeLeader | LIVE |
| `guildRemove` | guildRemove | command_processor.cpp:1728 -> guildRemove | LIVE |
| `hail` | hail | command_processor.cpp:1089 -> hail | LIVE |
| `hailTarget` | hailTarget, interactNearest | input_action_bridge.cpp:143 -> hailTarget, command_processor.cpp:1087 -> hailTarget (+1) | LIVE |
| `hasTellToReplyTo` | replyToLastTell | command_processor.cpp:1049 -> replyToLastTell | LIVE |
| `inspectTarget` | inspectTarget | command_processor.cpp:1674 -> inspectTarget | LIVE |
| `interruptCast` | interruptCast | command_processor.cpp:2283 -> interruptCast | LIVE |
| `inviteTarget` | inviteTarget | command_processor.cpp:1167 -> inviteTarget | LIVE |
| `inviteToGroup` | inviteToGroup | command_processor.cpp:1156 -> inviteToGroup | LIVE |
| `isGMMode` | checkGMMode | command_processor.cpp -> gmKick/gmKill/... -> checkGMMode | LIVE (via checkGMMode() inside every gm* wrapper) |
| `jump` | jump | input_action_bridge.cpp:76 -> jump, command_processor.cpp:2462 -> jump | LIVE |
| `leaveGroup` | leaveGroup | command_processor.cpp:1186 -> leaveGroup | LIVE |
| `listEntities` | listEntities | command_processor.cpp:2490 -> listEntities | LIVE |
| `listHuntTargets` | listHuntTargets | command_processor.cpp:2532 -> listHuntTargets | LIVE |
| `listMemorizedSpells` | listMemorizedSpells | command_processor.cpp:2279 -> listMemorizedSpells | LIVE |
| `lootAll` | lootAll | (none) | KEPT, BLOCKED (F1): real body (EverQuest::LootAllFromCorpse), no dispatcher caller |
| `lootCorpse` | lootCorpse | input_action_bridge.cpp:282 -> lootCorpse, command_processor.cpp:2536 -> lootCorpse (+1) | LIVE |
| `lootItem` | lootItem | (none) | KEPT, BLOCKED (F1): real body (EverQuest::LootItemFromCorpse), no dispatcher caller |
| `memorizeSpell` | memorizeSpell | (none) | DELETED (by-id; no caller -- /mem is memorizeSpellByName, same SpellManager call) |
| `memorizeSpellByName` | memorizeSpellByName | command_processor.cpp:2259 -> memorizeSpellByName | LIVE |
| `moveItem` | moveItem | (none) | DELETED (LOG stub; InventoryManager pushes MoveItemIntent, /equip is equip()) |
| `moveToEntity` | moveToEntity | input_action_bridge.cpp:235 -> moveToEntity, command_processor.cpp:2148 -> moveToEntity | LIVE |
| `moveToEntityWithinRange` | moveToEntityWithinRange | (none) | DELETED (no caller; EverQuest::MoveToEntityWithinRange keeps its follow/hunt callers) |
| `moveToLocation` | moveToLocation | input_action_bridge.cpp:231 -> moveToLocation, command_processor.cpp:2136 -> moveToLocation | LIVE |
| `openSpellbook` | openSpellbook | (none) | DELETED (LOG stub; spellbook is a renderer popup) |
| `perfReport` | perfReport | command_processor.cpp:2198 -> perfReport | LIVE |
| `petition` | petition | command_processor.cpp:1992 -> petition | LIVE |
| `raidAccept` | raidAccept | command_processor.cpp:1217 -> raidAccept | LIVE |
| `raidGroup` | raidGroup | command_processor.cpp:1261 -> raidGroup | LIVE |
| `raidInvite` | raidInvite | command_processor.cpp:1288 -> raidInvite | LIVE |
| `raidLeave` | raidLeave | command_processor.cpp:1220 -> raidLeave | LIVE |
| `raidList` | raidList | command_processor.cpp:1223 -> raidList | LIVE |
| `raidLock` | raidLock | command_processor.cpp:1264 -> raidLock, command_processor.cpp:1267 -> raidLock | LIVE |
| `raidLooter` | raidLooter | command_processor.cpp:1282 -> raidLooter | LIVE |
| `raidLootType` | raidLootType | command_processor.cpp:1246 -> raidLootType | LIVE |
| `raidMakeLeader` | raidMakeLeader | command_processor.cpp:1239 -> raidMakeLeader | LIVE |
| `raidRemove` | raidRemove | command_processor.cpp:1233 -> raidRemove | LIVE |
| `raidStatus` | raidStatus | command_processor.cpp:1214 -> raidStatus | LIVE |
| `recipeAutoCombine` | recipeAutoCombine | command_processor.cpp:1913 -> recipeAutoCombine | LIVE |
| `recipeDetails` | recipeDetails | command_processor.cpp:1904 -> recipeDetails | LIVE |
| `recipesFavorite` | recipesFavorite | command_processor.cpp:1930 -> recipesFavorite | LIVE |
| `replyToLastTell` | replyToLastTell | command_processor.cpp:1049 -> replyToLastTell | LIVE |
| `requestGuildMOTD` | requestGuildMOTD | command_processor.cpp:1749 -> requestGuildMOTD | LIVE |
| `requestQuit` | quit | command_processor.cpp:2186 -> quit | LIVE |
| `requestTrade` | requestTrade | command_processor.cpp:1583 -> requestTrade | LIVE |
| `requestWho` | requestWho | command_processor.cpp:1661 -> requestWho | LIVE |
| `requestZone` | requestZone | (none) | DELETED (LOG stub) |
| `rewind` | rewind | command_processor.cpp:1978 -> rewind | LIVE |
| `rezAccept` | rezAccept | command_processor.cpp:1941 -> rezAccept | LIVE |
| `rezDecline` | rezDecline | command_processor.cpp:1945 -> rezDecline | LIVE |
| `rollRandom` | rollRandom | command_processor.cpp:1692 -> rollRandom | LIVE |
| `save` | save | command_processor.cpp:1967 -> save | LIVE |
| `sellToVendor` | sellToVendor | command_processor.cpp:2090 -> sellToVendor | LIVE |
| `sendAnimation` | sendAnimation | command_processor.cpp:2594 -> sendAnimation, command_processor.cpp:2602 -> sendAnimation (+2) | LIVE |
| `sendChatMessage` | sendChatMessage | input_action_bridge.cpp:220 -> sendChatMessage, command_processor.cpp:91 -> sendChatMessage (+7) | LIVE |
| `sendPetCommand` | petAttack, petBackOff, petFocus, petFollow, petGuard, petHealth, petHold, petHoldOff, petLeader, petSit, petSpellHold, petStand, petTaunt, petTauntOff, sendPetCommand | command_processor.cpp:2328 -> petAttack, command_processor.cpp:2333 -> petBackOff (+14) | LIVE |
| `sendPositionUpdate` | sendPositionUpdate | (none) | DELETED (no caller; movement sends its own) |
| `sendTell` | sendTell | input_action_bridge.cpp:213 -> sendTell, command_processor.cpp:1026 -> sendTell | LIVE |
| `setAFK` | setAFK, toggleAFK | command_processor.cpp:1132 -> toggleAFK | LIVE |
| `setAnonymous` | setAnonymous | command_processor.cpp:1137 -> setAnonymous | LIVE |
| `setAppearance` | setAppearance | command_processor.cpp:2917 -> setAppearance | LIVE |
| `setAutoHunting` | setAutoHunting | command_processor.cpp:2504 -> setAutoHunting, command_processor.cpp:2509 -> setAutoHunting | LIVE |
| `setAutoLoot` | setAutoLoot | command_processor.cpp:2524 -> setAutoLoot | LIVE |
| `setGuildMOTD` | setGuildMOTD | command_processor.cpp:1751 -> setGuildMOTD | LIVE |
| `setHeading` | faceEntity, faceLocation, setHeading | input_action_bridge.cpp:240 -> faceEntity, command_processor.cpp:1069 -> faceEntity (+3) | LIVE |
| `setInspectMessage` | setInspectMessage | command_processor.cpp:2861 -> setInspectMessage, command_processor.cpp:2866 -> setInspectMessage (+1) | LIVE |
| `setMovementMode` | setMovementMode | command_processor.cpp:2422 -> setMovementMode, command_processor.cpp:2430 -> setMovementMode (+1) | LIVE |
| `setPathfinding` | setPathfinding | command_processor.cpp:2579 -> setPathfinding | LIVE |
| `setPositionState` | setPositionState | command_processor.cpp:2446 -> setPositionState, command_processor.cpp:2454 -> setPositionState | LIVE |
| `setRoleplay` | setRoleplay | command_processor.cpp:2585 -> setRoleplay, command_processor.cpp:2590 -> setRoleplay | LIVE |
| `setSurname` | setSurname | command_processor.cpp:1685 -> setSurname | LIVE |
| `shieldTarget` | shieldTarget | command_processor.cpp:1537 -> shieldTarget | LIVE |
| `showLocation` | showLocation | command_processor.cpp:2100 -> showLocation | LIVE |
| `showOrSetZoning` | showOrSetZoning | command_processor.cpp:2162 -> showOrSetZoning | LIVE |
| `sit` | sit | command_processor.cpp:2107 -> sit | LIVE |
| `split` | split | command_processor.cpp:1963 -> split | LIVE |
| `stand` | stand | command_processor.cpp:2113 -> stand | LIVE |
| `startCamp` | startCamp | (none) | DELETED (no caller; /camp is camp() -> campNow() -> StartCampTimer) |
| `startMoving` | startMoving | (none) | DELETED (no caller; key movement is renderer physics, scripted movement is /moveto) |
| `stopAllMovement` | stopAllMovement | input_action_bridge.cpp:247 -> stopAllMovement | LIVE |
| `stopFollow` | stopFollow | command_processor.cpp:2182 -> stopFollow | LIVE |
| `stopMoving` | stopMoving | (none) | DELETED (no caller) |
| `targetEntity` | interactNearest, targetEntity | input_action_bridge.cpp:157 -> interactNearest, input_action_bridge.cpp:272 -> targetEntity (+1) | LIVE |
| `targetGroupMember` | targetGroupMember | input_action_bridge.cpp:104 -> targetGroupMember, input_action_bridge.cpp:108 -> targetGroupMember (+3) | LIVE |
| `targetNearest` | targetNearest | (none) | DELETED ("not yet implemented" stub; targetNearestPC/NPC are live) |
| `targetNearestNPC` | targetNearestNPC | input_action_bridge.cpp:128 -> targetNearestNPC | LIVE |
| `targetNearestPC` | targetNearestPC | input_action_bridge.cpp:124 -> targetNearestPC | LIVE |
| `targetSelf` | targetSelf | input_action_bridge.cpp:100 -> targetSelf | LIVE |
| `taunt` | taunt | command_processor.cpp:1435 -> taunt | LIVE |
| `timeOfDay` | timeOfDay | command_processor.cpp:2202 -> timeOfDay | LIVE |
| `toggleAutoAttack` | toggleAutoAttack | input_action_bridge.cpp:82 -> toggleAutoAttack, command_processor.cpp:1408 -> toggleAutoAttack | LIVE |
| `toggleAutoFire` | toggleAutoFire | command_processor.cpp:1517 -> toggleAutoFire | LIVE |
| `toggleAutorun` | toggleAutorun | input_action_bridge.cpp:72 -> toggleAutorun | LIVE |
| `toggleCombatManager` | toggleCombatManager | command_processor.cpp:1422 -> toggleCombatManager | LIVE |
| `toggleLFG` | toggleLFG | command_processor.cpp:1678 -> toggleLFG | LIVE |
| `toggleSneak` | toggleSneak | (none) | DELETED (LOG stub; /sneak is setMovementMode(2)) |
| `trackTarget` | trackTarget | command_processor.cpp:2707 -> trackTarget | LIVE |
| `tradeskillCombine` | tradeskillCombine | (none) | DELETED (LOG stub; manual combine is blocked feature F7) |
| `trainWithTarget` | trainWithTarget | command_processor.cpp:2210 -> trainWithTarget | LIVE |
| `useAbility` | useAbility | (none) | DELETED ("not yet implemented" stub) |
| `useItem` | useItem, useItemById | input_action_bridge.cpp:331 -> useItemById | LIVE |
| `useSkill` | useSkill | input_action_bridge.cpp:322 -> useSkill, command_processor.cpp:1511 -> useSkill | LIVE |
| `vendorList` | vendorList | command_processor.cpp:2007 -> vendorList | LIVE |
| `vendorToggle` | vendorToggle | command_processor.cpp:2003 -> vendorToggle | LIVE |
| `warp` | warp | command_processor.cpp:2155 -> warp, command_processor.cpp:2158 -> warp | LIVE |
| `yellForHelp` | yellForHelp | command_processor.cpp:1696 -> yellForHelp | LIVE |

### Table 4 -- EverQuest::Send* (77, unchanged)

| EverQuest::Send* | callers outside own definition (before) | verdict |
|---|---|---|
| `SendAnimation` | skill_manager.cpp:707, eq_player_info.cpp:812 (+1) | LIVE |
| `SendApplyPoison` | skill_manager.cpp:544 | LIVE |
| `SendAssist` | eq_action_handler.cpp:369 | LIVE |
| `SendAssistGroup` | eq_action_handler.cpp:2163 | LIVE |
| `SendBindWound` | skill_manager.cpp:562 | LIVE |
| `SendCamp` | eq_player_info.cpp:903 | LIVE |
| `SendCancelTrade` | eq_merchant.cpp:557 | LIVE |
| `SendChatMessage` | ucs.cpp:307, eq_chat.cpp:379 (+6) | LIVE |
| `SendClickDoor` | eq_bridge.cpp:896, eq_action_handler.cpp:647 (+1) | LIVE |
| `SendClickObject` | eq_bridge.cpp:1015, eq_action_handler.cpp:1129 | LIVE |
| `SendCloseContainer` | eq_bridge.cpp:1400 | LIVE-BY-ARM ONLY: sole caller is the kept TradeskillCloseIntent arm (blocked feature F7) |
| `SendConsent` | eq_action_handler.cpp:1516 | LIVE |
| `SendConsentDeny` | eq_action_handler.cpp:1520 | LIVE |
| `SendConsiderCorpse` | combat.cpp:368 | LIVE |
| `SendCorpseDrag` | eq_action_handler.cpp:1508 | LIVE |
| `SendCorpseDrop` | eq_action_handler.cpp:1512 | LIVE |
| `SendDeleteItem` | eq_bridge.cpp:1416 | LIVE |
| `SendDuelAccept` | eq_action_handler.cpp:488 | LIVE |
| `SendDuelDecline` | eq_action_handler.cpp:496 | LIVE |
| `SendDuelRequest` | eq_action_handler.cpp:475 | LIVE |
| `SendFaceChange` | eq_action_handler.cpp:1282 | LIVE |
| `SendFindPerson` | eq_action_handler.cpp:2188 | LIVE |
| `SendFriendsWho` | eq_action_handler.cpp:2192 | LIVE |
| `SendGetGuildMOTD` | eq_action_handler.cpp:1500 | LIVE |
| `SendGMEmoteZone` | eq_action_handler.cpp:2238 | LIVE |
| `SendGMFind` | eq_action_handler.cpp:2222 | LIVE |
| `SendGMGoto` | eq_action_handler.cpp:2218 | LIVE |
| `SendGMHideMe` | eq_action_handler.cpp:2234 | LIVE |
| `SendGMKick` | eq_action_handler.cpp:2226 | LIVE |
| `SendGMKill` | eq_action_handler.cpp:2230 | LIVE |
| `SendGMLastName` | eq_action_handler.cpp:2242 | LIVE |
| `SendGMSummon` | eq_action_handler.cpp:2214 | LIVE |
| `SendGMZoneRequest` | eq_action_handler.cpp:2210 | LIVE |
| `SendGroupDecline` | eq_group.cpp:175, eq_action_handler.cpp:735 | LIVE |
| `SendGroupDisband` | eq_group.cpp:143, eq_bridge.cpp:1281 | LIVE |
| `SendGroupFollow` | eq_group.cpp:167 | LIVE |
| `SendGroupInvite` | eq_bridge.cpp:1276, eq_action_handler.cpp:712 (+1) | LIVE |
| `SendGuildDemote` | eq_action_handler.cpp:1492 | LIVE |
| `SendGuildInvite` | eq_action_handler.cpp:1476 | LIVE |
| `SendGuildInviteAccept` | eq_action_handler.cpp:1480, eq_action_handler.cpp:1484 | LIVE |
| `SendGuildLeader` | eq_action_handler.cpp:1496 | LIVE |
| `SendGuildRemove` | eq_action_handler.cpp:1488 | LIVE |
| `SendInspectAnswer` | eq_player_info.cpp:1286 | LIVE |
| `SendInspectRequest` | eq_action_handler.cpp:1441 | LIVE |
| `SendJump` | eq_movement.cpp:1684 | LIVE |
| `SendLeaveGroup` | eq_bridge.cpp:1282, eq_action_handler.cpp:740 | LIVE |
| `SendLFGCommand` | eq_action_handler.cpp:1446 | LIVE |
| `SendLogout` | eq_player_info.cpp:941 | LIVE |
| `SendMoveCoin` | eq_bridge.cpp:1125, eq_bridge.cpp:1209 (+1) | LIVE |
| `SendMoveItem` | eq_bridge.cpp:1413 | LIVE |
| `SendMovementHistory` | eq_movement.cpp:1194 | LIVE |
| `SendPetCommand` | eq_group.cpp:290, eq_bridge.cpp:1273 (+2) | LIVE |
| `SendPetition` | eq_action_handler.cpp:1586 | LIVE |
| `SendPositionUpdate` | eq_commands_movement.cpp:269, eq_movement.cpp:457 (+16) | LIVE |
| `SendRaidCommand` | eq_action_handler.cpp:1599, eq_action_handler.cpp:1610 (+7) | LIVE |
| `SendRandom` | eq_action_handler.cpp:1455 | LIVE |
| `SendReadBookRequest` | eq_merchant.cpp:434 | LIVE |
| `SendRecipeAutoCombine` | eq_action_handler.cpp:1205 | LIVE |
| `SendRecipeDetails` | eq_action_handler.cpp:1197 | LIVE |
| `SendRecipesFavorite` | eq_action_handler.cpp:1213 | LIVE |
| `SendRecipesSearch` | eq_action_handler.cpp:1185 | LIVE |
| `SendReport` | eq_action_handler.cpp:1582 | LIVE |
| `SendRewind` | eq_action_handler.cpp:1578 | LIVE |
| `SendRezzAnswer` | eq_action_handler.cpp:1524, eq_action_handler.cpp:1528 | LIVE |
| `SendSave` | eq_action_handler.cpp:1538 | LIVE |
| `SendSetGuildMOTD` | eq_action_handler.cpp:1504 | LIVE |
| `SendShielding` | eq_action_handler.cpp:384 | LIVE |
| `SendSpawnAppearance` | eq_movement.cpp:1521, eq_movement.cpp:1531 (+11) | LIVE |
| `SendSplit` | eq_action_handler.cpp:1534 | LIVE |
| `SendSurname` | eq_action_handler.cpp:1450 | LIVE |
| `SendTrackTarget` | eq_action_handler.cpp:2182 | LIVE |
| `SendTradeAcceptClick` | eq_merchant.cpp:553 | LIVE |
| `SendTradeRequest` | eq_merchant.cpp:541 | LIVE |
| `SendTradeRequestAck` | eq_merchant.cpp:545 | LIVE |
| `SendTradeSkillCombine` | eq_bridge.cpp:1396 | LIVE-BY-ARM ONLY: sole caller is the kept TradeskillCombineIntent arm (blocked feature F7) |
| `SendWhoAllRequest` | eq_action_handler.cpp:1463 | LIVE |
| `SendYellForHelp` | eq_action_handler.cpp:1459 | LIVE |

### Verification bar

1. `cmake --build build -j24`: OK, zero errors (`raw/build1.log`,
   `raw/build_sweep.log`).
2. `ctest`: **2971/2972**, sole failure `AudioManagerTest.PreloadCommonSounds`
   (documented ALSA quirk). +5 new tests (ReachabilitySweep.*). Six
   pre-existing tests were rewritten, not dropped: the two
   CombatManagerAutoAttack publish tests now assert the GameState write
   and that NOTHING is published; the hotbar Combat test asserts the same
   state; the sync-bridge pass-through tests use EntitySpawned /
   PlayerStatsChanged / PlayerMovementModeChanged as payloads. Two T240
   handler tests (castSpellOnTarget, memorizeSpell-by-id) were deleted
   with their methods; the T240 fix itself lives on in
   memorizeSpellByName -> SpellManager::memorizeSpell, still tested.
   `raw/ctest_full.log`.
3-5. build-gles2-x86.sh / visual gate / build-arm-noble.sh: NOT run -- the
   plan's verification is "Full ctest; Review; evidence". The only
   graphics-side edits are deletions of code with no caller
   (irrlicht_bridge.cpp three cases, renderer_overlays.cpp one method,
   irrlicht_renderer.h one declaration); the GLES2-config tree that IS the
   test tree compiled them. Flagged as a deviation, not absorbed.
6. `check_file_sizes.sh fable`: 27 files, 19 ok, 8 WARN (all pre-existing
   over-1500 files, every one of them SHRANK here), 0 FAIL
   (`raw/check_file_sizes.log`).

### Deviations

- Steps 3-5 of the bar not run (above).
- Docs with stale counts left to T263 (above).
- `getItemById()` change from the dispatch not made: already correct.
- The dispatch said "delete only what blocks nothing"; MemorizeSpellIntent
  is producer-less AND its feature is reachable by `/mem`, so the rule
  would have deleted it. Kept instead, because the spellbook popup's click
  handler names it as the missing piece (F4) -- deleting it would have
  removed the consumer the two-line wiring needs.
