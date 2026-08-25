# T227 — Option A stage 3: `EverQuest::m_game_state` becomes the ONLY `state::GameState`

Slot: slot1. x86 only. OWNS eqemu (two-client probe, step 6). Decision:
docs/decisions.md 2026-08-25 (Will's A1: stage 1 = T220 f906f697, stage 2 =
T221 eae2df68, stage 3 = THIS). Background refactor/T78_two_gamestate_analysis.md;
authoritative deletion list: refactor/T221_fold_projections_to_source.md
"Stage-3 deletion list". All line numbers at fable 292fd24b.

## Goal
Delete `Application::m_gameState` (application.h:465, made at application.cpp:198).
Every holder takes `m_eqClient->GetGameState()` (eq.h:1266, public, non-const
ref to the value member eq.h:1410). `GameStateSyncBridge::projectEvent()` and
its 40-case switch are deleted; what remains is the T220 two-stage forwarding
hop. The action layer then reads the object the packet handlers write, on the
same thread, so the READ/WRITE-model split -- and the whole "every action-layer
read needs a projection case" contract -- ends. decisions.md gets a closing
addendum (orchestrator, at merge).

## Scope
1. **Reference swap (application.cpp).** Construction order already permits it:
   EverQuest is built at :204, every holder after. Replace `*m_gameState` with
   `m_eqClient->GetGameState()` at :290 (ActionDispatcher), :296
   (CommandProcessor), :332 (`m_gameMode->initialize`, game_mode.h:133 takes
   `state::GameState&`), :342 (InputActionBridge); drop the second ctor arg at
   :560-561 and :637-638 (see step 3). Delete :198 alloc, :737 `m_gameState.reset()`
   (m_eqClient.reset() at :736 now owns the lifetime -- and it is already the
   LAST reset after m_gameMode->shutdown() :727 (ConsoleRenderer/AutomatedMode
   unsubscribe, headless_mode.cpp:45, automated_mode.cpp:40), m_inputBridge/
   m_commandProcessor/m_dispatcher :732-734 -- no dangling ref). Delete
   application.h:465 and the `getGameState()` pair :268-269 (zero callers:
   `grep -rn "getGameState()" src tests` hits only the mode classes' own).
   `setZoneConnected` at :765/:790: DELETE both. They run on the GAME thread
   (gameThreadLoop :745) and are now redundant: the source writes
   `world().setZoneConnected(true)` at eq_entities.cpp:1694 (first ClientUpdate,
   immediately before `StartUpdateLoop()` :1705 -- i.e. the same instant
   `IsFullyZonedIn()` eq.h:926 becomes true) and `false` at eq.cpp:740
   (StatusDisconnected) and eq_zone.cpp:337 (CleanupZone). Keep the
   `m_fullyConnected` bookkeeping around them (hotbar/inspect load, T30 flags).
   Other `m_gameState` text in application.cpp is comments only (:1418, :1430,
   :1476, :1922) and application.h (:359-377, :506, :551) -- rewrite to name
   `EverQuest::m_game_state`. Only reader of isZoneConnected is
   action_dispatcher.cpp:31 -- unchanged.
2. **"Not on the list" re-verification (source writer for each case).** Verdict
   per case; DELETE = source complete, delete with the switch; FOLD = write first.
   | case | source write | verdict |
   |---|---|---|
   | PlayerSpawnIdSet | `player().setSpawnId` at eq_entities.cpp:374, :807, :1181, :1595 -- each publish site (:405, :1231, :1670) is preceded by a setSpawnId in the same handler; :807 (SpawnAppearance new id) writes with NO publish, so the source is a superset | DELETE |
   | VendorWindowOpened/Closed | NONE: `grep -rn "setVendor\|clearVendor" src/client` hits only the bridge and player_state.cpp. Publishes at eq_merchant.cpp:976-984 (ShopRequest), :1160-1166 (ShopEndConfirm), :1326-1332 (CloseVendorWindow) | FOLD |
   | BankWindowOpened/Closed | NONE: publishes at :1361-1370 (OpenBankWindow), :1390-1396 (CloseBankWindow) | FOLD |
   | TrainerWindowOpened/Closed | NONE: publishes at :1500-1504 (trainer open, data built :1448-1451), :1563-1569 (close) | FOLD |
   | EntityStateSnapshot | `SyncEntityToGameState` eq_player_info.cpp:287-295 add-or-replace on the source, same function that publishes :328-331 | DELETE |
   | EntityDespawned | `RemoveEntityFromGameState` eq_entities.cpp:1861 (:337 removeEntity), same handler as the only publisher `OnSpawnRemovedGraphics` :1856 -> eq_bridge.cpp:665-672 | DELETE |
   | ZoneUnloading | CleanupZone eq_zone.cpp:270 `entities().clear()`, :273 `doors().clear()` -- unconditional, vs the publish :247-251 which is `#ifdef EQT_HAS_GRAPHICS` | DELETE (see step 7) |
   | GroupChanged | join/update eq_group.cpp:402-404 + :409 clearPendingInvite; leave :486-520 keeps `m_in_group` (no change needed); disband/`ClearGroup` :293-305 -> `group().clearGroup()`; make-leader :547-548; cancel-invite :601 clearPendingInvite (the exact case the bridge comment :289-306 worried about) | DELETE |
   | GroupInviteReceived | eq_group.cpp:362 `setPendingInvite` | DELETE |
   | TradeRequestReceived/Started/AcceptStateChanged/Cancelled/Completed | eq_merchant.cpp:557 setPendingRequest, :579 startTrade, :593 reset (None), :645 setAcceptState, :710 reset (completed), :728 reset (cancelled) -- T103 wrote every lambda both ways (:545-555 comment) | DELETE |
   | PlayerFlagsChanged | eq_player_info.cpp:849/:865/:881 setAFK/setAnonymous/setRoleplay, before each T218 publish | DELETE |
   | DoorSpawned/DoorStateChanged (T219, also not on T221's list) | eq_entities.cpp:576 `doors().addDoor`, :2427 `setDoorState` | DELETE |
   FOLD detail (eq_merchant.cpp, each placed ABOVE its `if (m_bridge)`, mirroring
   T221's rule): ShopRequest -> `m_game_state.player().setVendor(m_vendor_npc_id,
   m_vendor_sell_rate, m_vendor_name)` (after m_vendor_name is set :968-974);
   ShopEndConfirm and CloseVendorWindow -> `clearVendor()` (before `m_vendor_npc_id
   = 0`); OpenBankWindow -> `setBanker(m_banker_npc_id)`; CloseBankWindow ->
   `clearBanker()`; trainer open -> `setTrainer(m_trainer_npc_id, m_trainer_name)`;
   trainer close -> `clearTrainer()`. Signatures player_state.h:278-296 match the
   bridge's calls 1:1; data is in hand at every site. Readers: CommandProcessor's
   vendor/bank/trainer commands (T49) -- they now see the source.
3. **GameStateSyncBridge -> pure forwarding hop.** Smallest diff: keep the class
   and file names (28 references across src/include/tests/docs; a rename is a
   separate cosmetic unit), drop `state::GameState&` from the ctor (only two
   production ctor sites + the two test fixtures), delete `projectEvent()` and
   `m_state`, `applyEvent()` becomes `m_wrapped.applyEvent(event)`;
   `projectPendingEvents()` becomes "drain base queue -> append to
   m_forwardQueue" (rename to `stagePendingEvents()`; keep
   `forwardProjectedEvents()`'s body, rename `forwardStagedEvents()`). Includes
   of game_state.h / inventory_constants.h / logging.h go. Folding the queue into
   GameStateBridge was rejected by T220 for the same reasons (non-virtual
   drainEvents, ~100 test call sites). Header comment: replace the T40-T104
   history block (:14-132) with a 15-line "why two hops" note; the history lives
   in refactor/. include/client/bridge/CLAUDE.md "Two-stage drain" and root
   CLAUDE.md threading table: one-line edits.
4. **Thread audit after the swap** (writers = EverQuest packet handlers, game
   thread; readers = action layer, game thread):
   - `CleanupZone` -- reached ONLY via `DisconnectFromZone()` eq_zone.cpp:377 from
     the game thread's `UpdateMovement()` (P36, eq.cpp:3775-3795). `~EverQuest`
     eq.cpp:290-326 does NOT call it (StopUpdateLoop + connection resets only),
     and shutdown joins m_gameThread (application.cpp:898) before
     `m_eqClient.reset()` :736. Verdict: single-threaded.
   - `m_update_thread` (eq.cpp 500 ms loop) calls `CombatManager::Update()` only
     :3798; combat.cpp's setTarget/clearTarget writes (T221) fire from
     SetTarget/ClearTarget, which Update() can reach (auto-hunt/despawn paths)
     -> POSSIBLE off-thread write of `combat()`. STOP-check item: executor greps
     `CombatManager::Update` call graph; if it reaches SetTarget/ClearTarget it is
     a pre-existing race the TSAN tour must show or clear, listed not fixed.
   - ConsoleRenderer (console_renderer.cpp:79-112) and AutomatedMode
     (automated_mode.cpp:25) subscribe to `state.events()`; callbacks run
     synchronously inside the writer, i.e. game thread. Change of behaviour: they
     now hear EverQuest's bus -- `PlayerMoved` from `player().setPosition` (T58,
     eq.cpp:3190, 60 Hz) reaches AutomatedMode's catch-all subscriber (cost: one
     optional callback), and the T81 duplicate console spawn/despawn line
     disappears (single addEntity per spawn). CleanupZone's `entities().clear()`
     fires N despawn lines on the console at zone-out (was the same via the
     ZoneUnloading projection). Verdict: game thread, cosmetic deltas only.
   - graphics/: `grep -rn "GetGameState\|GameState&\|state::GameState" src/client/graphics include/client/graphics` = 0 hits. IrrlichtBridge/ConsoleBridge hold no GameState. Verdict: no render-thread reader.
   - Loading thread: reads `m_zoneLoadSnapshot` only (application.cpp:1615). None.
   - `EqActionHandler` already reads `m_eq.GetGameState()` (eq_action_handler.cpp:239-1021), game thread. Unchanged.
5. **Tests.**
   - tests/test_game_state_sync_bridge.cpp: the 68 `GameStateSyncBridgeTest`
     projection cases (:48-1348) DIE -- their assertions are on `state->` after
     applyEvent. Keep the 7 `*StillDelegateToWrappedBridge` cases (:115, :213,
     :407, :527, :736, :844, :986, :1150, :1228, :1348 -- collapse to one
     "every event type forwards once") and the 6 `GameStateSyncBridgeSplitDrainTest`
     cases (:1427-1536) with `state` removed from the fixture (:1379-1381) and
     the stage names updated; :1427 "MutatesStateButForwardsNothing" becomes
     "StagesButForwardsNothing". CMake :483-489 drops `${ACTION_LAYER_STATE_SOURCES}`
     if nothing else in the file needs it.
   - tests/test_game_state_source_writes.cpp (48 cases) is the state net; ADD
     Vendor/Bank/Trainer cases (`OpenBankWindow(id)`/`CloseBankWindow()` are
     public; ShopRequest via a serialized packet or the T17 `#define private public`
     the file already uses) x {WithBridge, NoBridge}; RED table per T221.
   - The 13 files constructing a `state::GameState` and passing it to
     ActionDispatcher/CommandProcessor/InputActionBridge (test_action_dispatcher,
     test_appearance_command, test_command_processor{,_combat_guild,_migration,
     _world,_senders}, test_eq_action_handler_trade, test_input_action_bridge,
     test_merged_help, test_setheading_conversion, test_skill_reachability,
     test_game_state_sync_bridge): ctor signatures are untouched (`GameState&`),
     they compile and stay green.
   - tests/test_headless_command_refusal.cpp: bare `Application app;` never
     `initialize()`d; `DispatchSlashCommand` touches neither m_gameState nor
     m_eqClient (grep) -- unaffected. test_beneficial_spell_casting (T220's
     `ProcessBridgeEvents()` caller) unaffected.
6. **Live acceptance (eqemu, two clients A/B; observable = the READ path now
   reading the source):** pet: `/pet attack` on a summoned pet -> no "You have no
   pet" refusal; spells: cast from gem 1 -> `/cast` refusal while casting, gem
   Refresh reported after; inventory: `/equip` or a MoveItem -> occupancy-gated
   command accepts; combat: `/target Guard` then `/hail` -> hails the target
   (T220's dense pair), `/attack on` then `/attack off` reports the flag; entity
   move: B walks, A `/moveto <B>` -> path ends at B's NEW position; appearance:
   `/wearchange` (or B swaps a weapon) -> A's `/inspect`-style read shows the new
   slot; group: A `/invite B`, B `/accept` -> B's pending-invite gate opens then
   clears, A `/disband`; trade: A trades B, both accept -> `trade().active()`
   gate flips (`/trade` refusal before, accept path after); vendor/bank/trainer:
   `/bank` then `/bank close` (the FOLDed domain) -> "Bank window opened"/closed
   with the read-side command gate agreeing. TSAN: T220's `review/T220/
   run_tsan_x86.sh` dense tour (`/target Guard`+`/hail` x40) -> 0 reports naming
   `state::`; T225 is already at 0 elsewhere, so ANY report is new.
7. **Zone-load/unload handoff.** Source side: CleanupZone (game thread) clears
   entities :270 / doors :273 synchronously, before `ZoneUnloading` is even
   drained; the renderer teardown is `unloadZone()` on the loading thread
   (application.cpp:1626-1650, P17), independent of GameState. The ZoneUnloading
   event keeps flowing to IrrlichtBridge (irrlicht_bridge.cpp:812) through the
   forwarding hop -- unchanged. T220 already established that the READ model
   clears "immediately"; stage 3 keeps exactly that timing (same function, same
   thread). Also cleared at CleanupZone that the projection never had: combat
   movement :263-264, world flags :337/:367, pet :276-284. Nothing the bridge
   case cleared is missed; nothing extra is needed.

## Territory
src/client/application.cpp, include/client/application.h,
src/client/bridge/game_state_sync_bridge.cpp, include/client/bridge/
game_state_sync_bridge.h (+ CLAUDE.md there and the root CLAUDE.md table),
src/client/eq_merchant.cpp (the 7 vendor/bank/trainer sites ONLY), tests/
test_game_state_sync_bridge.cpp, tests/test_game_state_source_writes.cpp,
tests/CMakeLists.txt. Comment-only touches allowed: command_processor.h:259-265,
event_bus.h:30/39/458, application.h:359-377. EXCLUDED: action_dispatcher.cpp,
command_processor.cpp, state/*, any other eq_*.cpp, game_state_bridge.h.

## STOP
- Any "Not on the list" case whose source write is NOT found at the cited
  line (the table above is the claim; verify before deleting) -> fold, don't
  delete; if the data is not in hand -> STOP.
- `CombatManager::Update()` (m_update_thread) reaches a `combat()` write ->
  report with the call chain; do not add a lock.
- Any test in step 5's 13-file list goes red -> STOP (ctor contract changed).
- TSAN shows a `state::` report -> STOP, archive, report.

## Verification
1. `cmake --build build -j24`; ctest: expect 2830 - 68 + new ≈ 2775, sole
   failure the ALSA case.
2. Source-writes RED table for the FOLD hunk (revert eq_merchant.cpp only).
3. build-gles2-x86.sh, build-arm-noble.sh, check_file_sizes.sh (application.cpp
   and eq_merchant.cpp shrink/grow within existing WARN).
4. Visual gate PASS (renderer stream unchanged by construction: no pushEvent
   added or removed).
5. Step 6 live tour, both clients exit 0; TSAN dense tour 0 `state::`.
6. Review lists, per deleted case, the source line it relies on (the table
   above, re-cited at the executed commit), and the final `grep -rn
   "state::GameState" src include` census: exactly ONE instance.

## Size
Production ≈ -480 / +60: game_state_sync_bridge.cpp -440 (switch + includes),
.h -120 (history comment) +15, application.cpp/.h -12 +8, eq_merchant.cpp +7.
Tests: sync_bridge -1300 (+~40 forwarding/staging), source_writes +120,
CMake -1. Net ≈ -1650 lines. One executor, one slot, eqemu for step 6 only.
