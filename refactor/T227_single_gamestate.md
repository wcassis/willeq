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

## Review

Executed in `/mnt/projects/fable/wt/slot1`, branch `task/T227-single-gamestate`,
base fable tip `a191426f` (verified at checkout; `fable` advanced to `cd0e1075`
(T229/T231) during the unit -- none of those commits touch this unit's files).
eqemu owned for steps 6-7 (lock taken with the noclobber form AFTER every
offline step was green, deleted after `down.sh`; Testchar and Oraclerogue rows
restored, `db_restored.txt` == `db_before.txt`). Evidence:
`/mnt/projects/fable/review/T227/` (README indexes every file).

### Headline

There is ONE `state::GameState`: `grep -rn "state::GameState" src include`
finds exactly one instance, `include/client/eq.h:1410`
`eqt::state::GameState m_game_state;` (every other hit is a reference type,
a forward declaration or a comment). `Application::m_gameState`, its
`getGameState()` pair and the two `setZoneConnected` mirrors are gone;
ActionDispatcher / CommandProcessor / the mode / InputActionBridge take
`m_eqClient->GetGameState()`. `GameStateSyncBridge::projectEvent()` and its
40-case switch are deleted (476 lines); what remains is the T220 forwarding
hop (`applyEvent` = forward; `stagePendingEvents` / `forwardStagedEvents`),
46 lines. The three domains T221's census found with NO source write --
vendor / bank / trainer windows -- were folded first (7 sites in
`eq_merchant.cpp`, each ABOVE its `if (m_bridge)` guard) with 8 new
source-write cases x {WithBridge, NoBridge} = 16, RED 14/16 before the fold
(the two negative-path cases pass by construction), 64/64 after.

### Step 1 -- FOLD, red/green measured

| measurement | file | result |
|---|---|---|
| tests added, eq_merchant.cpp unfolded | `red_run_npc_before_fold.log` | 14 FAILED / 2 passed of the 16 `Npc_*` cases |
| after the fold | `green_source_writes_after_fold.log` | 64/64 |
| RED table: eq_merchant.cpp reverted to `a191426f`, full binary rebuilt | `red_run_revert_eq_merchant.log` | **14 RED** of 64 (all 7 positive `Npc_*` cases x 2 legs); 50 pass |
| restored, full rebuild | `green_source_writes_final.log` | 64/64 |

Sites (eq_merchant.cpp at this commit): `ZoneProcessShopRequest` :976
`setVendor(m_vendor_npc_id, m_vendor_sell_rate, m_vendor_name)`;
`ZoneProcessShopEndConfirm` :1165 `clearVendor()`; `CloseVendorWindow` :1334
`clearVendor()`; `OpenBankWindow` :1372 `setBanker(m_banker_npc_id)`;
`CloseBankWindow` :1404 `clearBanker()`; `ZoneProcessGMTraining` :1464
`setTrainer(m_trainer_npc_id, m_trainer_name)`; `CloseTrainerWindow` :1583
`clearTrainer()`. Data was in hand at every site (no STOP).

### Step 2 -- "Not on the list" re-verification (grep at `a191426f`, all found)

| case | source write found at | verdict applied |
|---|---|---|
| PlayerSpawnIdSet | eq_entities.cpp:374, :807, :1181, :1595 `player().setSpawnId` | DELETED |
| Vendor/Bank/Trainer* | none before this unit -> the 7 folds above | FOLDED then deleted |
| EntityStateSnapshot | eq_player_info.cpp:314 `addEntity` / :317 `getEntityMutable` (add-or-replace) | DELETED |
| EntityDespawned | eq_player_info.cpp:339 `removeEntity` (RemoveEntityFromGameState) | DELETED |
| ZoneUnloading | eq_zone.cpp:270 `entities().clear()`, :273 `doors().clear()` | DELETED |
| GroupChanged | eq_group.cpp:304 clearGroup, :402-404 setInGroup/LeaderName/IsLeader, :409 clearPendingInvite, :547-548 make-leader, :601 cancel-invite clearPendingInvite | DELETED |
| GroupInviteReceived | eq_group.cpp:362 `setPendingInvite` | DELETED |
| Trade* | eq_merchant.cpp:557 setPendingRequest, :579 startTrade, :593/:710/:728 reset, :611 setPartnerItem, :631 setMoney, :645 setAcceptState | DELETED |
| PlayerFlagsChanged | eq_player_info.cpp:851/:867/:883 setAFK/setAnonymous/setRoleplay | DELETED |
| DoorSpawned/DoorStateChanged | eq_entities.cpp:576 `doors().addDoor`, :2427 `setDoorState` | DELETED |
| (T221 list) Pet/Spells/Entity/Combat/Inventory/GroupMemberUpdated/TradeItem/Money | as T221's stage-3 table; eq_entities.cpp:2191 appearance merge re-confirmed | DELETED |

Plus `world().setZoneConnected`: true at eq_entities.cpp:1694 (first
ClientUpdate), false at eq.cpp:740, :3741, :3749 and eq_zone.cpp:337 -- the
Application mirrors were redundant and are deleted; `m_fullyConnected`
bookkeeping kept.

### Step 2b -- `m_update_thread` STOP-check: REACHED, reported, not fixed

`CombatManager::Update()` (called only from `m_update_thread`, eq.cpp:3798,
500 ms loop) DOES reach `combat()` writes: `Update()` -> `CheckForAggro()`
(combat.cpp:1360-1366) -> `SetTarget(uint16_t)` -> `PublishTargetChanged`
-> `m_game_state.combat().setTarget` (T221); `Update()` ->
`ProcessCombatRound()` :1345 -> `ClearTarget()` -> `clearTarget`;
`Update()` -> `UpdateAutoHunting()` -> `FindNextHuntTarget()` :1560/:1571/:1585
-> `SetTarget`/`ClearTarget`. All are gated on `m_enabled` (combat.cpp:139),
set ONLY by `/combat on|toggle` (eq_action_handler.cpp:445/:465
`enableCombatManager`/`toggleCombatManager`) -- off by default, off in every
tour here. This is a PRE-EXISTING race (present since T221 on
`combat()`, and since forever on `m_current_target_id` itself, which the
same thread pair already shared): an off-thread write of
`CombatState::m_targetId` against the game thread's `hasTarget()` reads
whenever the combat manager is enabled. Not introduced by this unit (the
projection never covered `/combat on` either), no lock added per the STOP
text. The TSAN dense tour cannot show it (combat manager disabled) -- it is
listed for the board, not cleared.

### Step 3/4 -- what `GameStateSyncBridge` is now, thread audit

Header 63 lines (15-line "why two hops" note replaces the T40-T104 history),
.cpp 46 lines: `explicit GameStateSyncBridge(GameStateBridge& wrapped)`,
`applyEvent` forwards, `stagePendingEvents()` drains the base queue into the
staging FIFO (game thread), `forwardStagedEvents()` re-coalesces and forwards
(render thread). No `game_state.h` / `inventory_constants.h` / `logging.h`
include. Readers and writers of the one GameState are all game-thread
(packet handlers, EqActionHandler, the action layer, ConsoleRenderer /
AutomatedMode subscribers on the source's bus -- cosmetic deltas only, e.g.
the T81 duplicate console spawn line is gone); no render-thread reader
(`grep GetGameState|state::GameState src/client/graphics include/client/graphics`
= 0); loading thread reads `m_zoneLoadSnapshot` only. Shutdown order:
`m_eqClient.reset()` is the LAST reset, after mode shutdown and every
holder of the reference (application.cpp `shutdown()`).

### Step 5 -- tests

- `tests/test_game_state_sync_bridge.cpp` 1546 -> 244 lines: the 68
  projection cases are gone; 7 cases remain (one "every event type forwards
  once, unchanged" over all 33 formerly-intercepted types + PlayerMoved,
  plus the 6 T220 split-drain cases with `state` removed and the stage names
  updated, incl. "StageStepStagesButForwardsNothing"). CMake target no
  longer links `${ACTION_LAYER_STATE_SOURCES}`.
- `tests/test_game_state_source_writes.cpp`: +8 `Npc_*` cases (x2 legs).
  ShopRequest / GMTraining are driven through real packets via
  `ReplayZonePacket`; bank via the public `OpenBankWindow/CloseBankWindow`;
  vendor/trainer close via the public `CloseVendorWindow/CloseTrainerWindow`.
- The 13-file ctor-contract list: all green (`step5_thirteen_files.txt`:
  test_action_dispatcher 48, test_appearance_command 7, test_command_processor
  367 [the `_combat_guild/_migration/_world/_senders` sources are compiled
  into that binary], test_eq_action_handler_trade 10, test_input_action_bridge
  37, test_merged_help 24, test_setheading_conversion 8, test_skill_reachability
  27, test_headless_command_refusal 3). No STOP.

### Verification bar

| Step | Result |
|---|---|
| 1. `cmake --build slot1/build -j24` | exit 0 (`build_full.log`, `build_full_restored.log`) |
| 2. ctest | **2807/2808**; sole failure `AudioManagerTest.PreloadCommonSounds` (ALSA, documented) (`ctest_full.txt`). (Plan's "≈2775" estimate predates T222-T226's additions.) |
| RED table | 14/64 with eq_merchant.cpp reverted (`red_run_revert_eq_merchant.log`) |
| 3. `build-gles2-x86.sh` | exit 0 (`build_gles2_x86.log`) |
| 4. Visual gate | **PASS**, goldens untouched (`visual_gate.txt`; `git status` shows only this unit's 12 files) |
| 5. `build-arm-noble.sh` | exit 0 (`build_arm_noble.log`) |
| 6. `check_file_sizes.sh` | `--files` on this unit's files: 8 ok, 3 WARN (pre-existing oversize application.cpp / eq_merchant.cpp / tests CMakeLists), 0 FAIL (`check_file_sizes_files.txt`). The `fable`-relative run (`check_file_sizes.txt`) also lists T229/T231's files because `fable` moved; its one FAIL (eq.cpp 3940 > 3000) is pre-existing and untouched here. |
| Census | `state::GameState` instances in src+include: **1** (eq.h:1410) |

### Step 6 -- live acceptance (two clients, qeynos2, headless x86; `run2_*`)

A = Testchar (GM, actor), B = Oraclerogue (observer/counterpart), staged
8 units apart at T220's door-20 spot. Both exit 0. `run1_*` is the first
attempt: identical outcomes for every domain except three sequencing
misses of the script (A's `/disband` fired 100 ms before B's accept
landed -> correctly refused "You are not in a group"; A's own `/accept`
before B had accepted; `/target Guard` picked a guard 351 units away so
`/bank` was refused on distance; and B's `/moveto Testchar` was a no-op at
8 units) -- fixed in run2's script (longer waits, banker spawned with
`#npctypespawn 1107` next to A, B walks 35 units to Rendallen first).

| domain | READ-path gate exercised (source-fed since T227) | A (actor) | B (observer) |
|---|---|---|---|
| zone-connected | `checkZoneConnection()` -> `world().isZoneConnected()` on EVERY command (mirror deleted) | all 63 script steps `outcome=applied`/`failed` past the gate, none held | same, 17 steps |
| pet | `pet().hasPet()` | `/pet guard` "You do not have a pet." -> `#makepet` -> "Pet guarding"; `/pet attack` passes hasPet, fails on the NEXT check ("specify a target") | sees `Testchar`s_familiar000` spawn |
| combat | `combat().hasTarget()` + `entities()` in hailTarget; auto-attack flag | `/target Guard` -> `/hail` says "Hail, Guard Hewet"; `/attack` "Auto-attack enabled", `/stopattack` "disabled" | sees `[CHAT] Testchar: Hail, Guard Hewet` |
| entities / appearance | `entities().findEntityByName` (/inspect, /target, /moveto) | `/inspect Oraclerogue` -> "Received inspect data for Oraclerogue" | "Testchar is inspecting you" |
| entity move | `entities()` position (source `updateEntityPosition`) | B at (14.5, 92.5) walked to (45.1, 86.9); A's `/moveto Oraclerogue` targets **(45.12, 86.88, 3.25)** = B's NEW position; A's `/loc` after: (40.7, 86.5) | B's own `/loc` = (45.1, 86.9, 3.4) |
| group | `group().hasPendingInvite()` (B), in-group (A `/disband`) | `/invite` applied; `/disband` -> "Left group" | `/groupaccept` before invite: "No pending group invite"; after: "Accepted group invite"; "Your group has been disbanded" |
| trade | `trade().hasPendingRequest()` (B), `trade().active()` (A) | `/trade` x2: second says "You are already trading"; `/canceltrade` "Trade cancelled" | `/accept` "Accepting trade with Testchar"; "Trade cancelled" |
| vendor/bank/trainer (FOLD) | `/npcstate` reads `player().isAtBank()/bankerNpcId()` | `/target Sorn` -> `/bank` "Bank window opened" -> `/npcstate` **"Banker: id=276"** -> `/bank` "closed" -> `/npcstate` "Banker: none" | (banker spawned by A via `#npctypespawn 1107`, non-persistent) |
| doors (T219) | `doors()` | `/door` -> "Clicking door 20 ('DOOR1')" | -- |
| flags (T218) | `player().isAFK()` | `/afk` "AFK enabled" -> `/afk` "AFK disabled" | -- |
| spells | NO action-layer GameState read exists (`/cast` refusal is `SpellManager::isCasting()`, eq_action_handler.cpp:536); ran `/gems` + `/cast 1` for the record ("No spell in gem 1", Testchar is a level-2 warrior) | applied | -- |
| inventory | NO action-layer GameState read exists (`/equip` -> `RequestEquip`, InventoryManager); ran `/equip list` for the record | applied | -- |

Deviation from the plan's step-6 wording, flagged: the plan's spells
("`/cast` refusal while casting, gem Refresh") and inventory
("occupancy-gated command accepts") legs presuppose an action-layer read of
`spells()` / `inventory()`; a grep of src/client/action, src/client/input
and eq_action_handler.cpp finds none (the only `spells()`/`inventory()`
writers are the T221 source writes, covered by test_game_state_source_writes).
Those two domains are therefore covered by the unit tests, not by a live
gate. The "`/wearchange`" appearance leg was replaced by `/inspect` (the
entities()-by-name read) -- Oraclerogue has no weapon to swap.

### TSAN (`tsan_after_dense.log`, T220's dense tour, 111 commands)

`slot1/build-tsan` rebuilt from this branch (`build_tsan.log`, instrumented:
35 `__tsan_*` symbols), T220's driver shape (`run_tsan_x86.sh`, ASLR off,
Mesa-only suppressions), graphics (surfaceless EGL) mode, all 111 commands
`outcome=applied`, clean `/camp` + "Application shutdown complete", exit 0.
**Zero ThreadSanitizer reports** -- no `tsan_after_dense_report.*` file was
produced at all (TSAN creates the `log_path` file on the first report), so
0 `state::` reports and 0 of anything else; consistent with T225 having
already retired T220's 17 RendererEventReceiver reports. No STOP.

### Deviations (flagged)

- Two comments outside the territory still name the old stage functions:
  `src/client/eq_bridge.cpp:296-297` (`projectPendingEvents` /
  `forwardProjectedEvents`) and
  `include/client/bridge/game_state_bridge.h:73-75` (same) -- both files
  are EXCLUDED by the plan, left untouched; one-line touch at merge.
- `include/client/eq.h` untouched (the plan's `GetGameState()` is already
  public and non-const); no `eq.h` change was needed.
- The full-suite count is 2807/2808, not the plan's ≈2775: the estimate was
  made before T222-T226 added cases; the delta from this unit is
  -68 projection cases, -6 renamed split-drain cases, +7 sync-bridge cases,
  +16 source-write cases.
- `run1_*` archived alongside `run2_*` (see above); no domain differs in
  outcome between them except the four scripting fixes listed.
