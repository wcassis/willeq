# Module Map

Post-Batch-F layout (2026-07-23), refreshed by T215 (2026-08-25), by J21
(2026-09-02) after Batch J's splits, and by K00 (2026-09-03, `3808ebe9`). The former
mega-files are split into domain translation units; classes are unchanged —
only method definitions moved. Line counts are `wc -l` at `3808ebe9`; they
drift with normal work. Ceilings: warn >1,500, fail >3,000
(`scripts/refactor/check_file_sizes.sh`). **As of Batch J no file in the tree
is over the 3,000-line fail line** — the grandfathered pair this map used to
name (`irrlicht_renderer.cpp`, `entity_renderer.cpp`) were split by J11 and
J03. The pre-split cluster analysis this map replaced is preserved in git
history (docs/module_map.md @ commit a0c02cd..93a3c2a era) and in each unit's
review section under `refactor/F*.md` and `refactor/J*.md`.

## Game state — EverQuest class (`include/client/eq.h`)

`src/client/core/everquest.cpp` + `src/client/core/` (K14: the last flat
`eq_*.cpp` TUs) + `src/client/action/` + the extracted components under
`src/client/<family>/` (Batch K).

| TU | Lines | Contents |
|---|---|---|
| src/client/core/everquest.cpp | 1,152 | K14: moved from the old flat `eq.cpp` (git mv, no line change) plus `DumpEntityAppearance` (moved in from `eq_strings.cpp` -- it reads `m_entities`, not a pure lookup). Core after J02: ctor/lifecycle, session recording, DB/spell-system init, ConnectToLogin/World/Zone, TickNetwork, loading-phase state, PublishFullStateSnapshot, zone connection callbacks, ZoneOnPacketRecv, ReplayZonePacket, SafeQueueZonePacket, QueuePacket, Start/StopUpdateLoop, SetEQClientPath/SetConstrainedConfig |
| src/client/action/eq_action_handler.cpp | 783 | K14: moved from the old flat `eq_action_handler.cpp` (git mv, no content change). `action::EqActionHandler` — the `IActionHandler` implementation wrapping `EverQuest` for `ActionDispatcher`/`CommandProcessor` (action-layer migration T99–T143; raid guards T179–T184; skill gate T159/T162) |
| src/client/movement/movement_component.cpp | 492 | K07: eqt::movement::MovementComponent -- Move/MoveTo/MoveToEntity, Follow/StopFollow, StartCombatMovement, Face/FaceEntity, StopMovement |
| src/client/bridge/bridge_component.cpp | 846 | K11: `eqt::bridge::BridgeComponent` — graphics lifecycle, the game-thread ticks, zone-load snapshot, BSP-tree install, the spawn/pet/movement bridge marshalling and the T269 bank-coin projection helpers |
| src/client/merchant/merchant.cpp | 689 | K05: `eqt::merchant::MerchantComponent` — inventory item movement, ScribeSpellFromScroll, the trade request/coin/accept/cancel senders, ReadBook and ZoneProcessTradePartnerItem, moved out of `class EverQuest` (was `eq_merchant.cpp`) |
| src/client/commands/commands_component.cpp | 1,123 | K12: `eqt::commands::CommandsComponent` — RegisterCommands driver + the game-registry remainder after T139 deleted 34 redundant commands (registry 1 of `docs/decisions.md` 2026-08-03), moved out of `class EverQuest` (was `eq_commands.cpp`) |
| src/client/zone/zone_component.cpp | 1,092 | K09: `eqt::zone::ZoneComponent` — zone change/teardown, CleanupZone, DisconnectFromZone, ProcessDeferredZoneChange, BeginGracefulZoneOut (T322), LoadZoneMap/Lines/Sources, CheckZoneLine, GetBestZ/FixZ, in-zone teleport (T231) |
| src/client/zone/zone_entry.cpp | 1,057 | K09: `eqt::zone::ZoneComponent` — ZoneProcessNewZone, ZoneProcessPlayerProfile (the LOAD-BEARING player heading flip), ZoneProcessAnimation, ZoneProcessGuildMOTD, CheckZoneRequestPhaseComplete |
| src/client/zone/zone_dispatch.cpp | 894 | G01: the zone-stream opcode TABLE. `DispatchZonePacket` is a 15-line lookup (`ZoneOpcodeTable()` → call, else `IgnoredZoneOpcodes()` → nothing, else the unhandled-opcode WARN). The 145 former `case` arms are 142 table entries — a captureless lambda each, calling either a sibling's `ctx_.<Family>().ZoneProcess*(p)` or one of the 38 named private `On*` handlers this file also holds — plus 3 deliberate no-ops in `kIgnoredOpcodes` with their reason. `tests/goldens/zone_opcodes_g01.txt` + `tests/test_zone_dispatch_table.cpp` pin the arm set both ways |
| src/client/login_world/login_world_component.cpp | 996 | K10: eqt::login_world::LoginWorldComponent -- login-server + world-server connection handling (ConnectToLogin/ConnectToWorld, the Login*/World* handshakes, character select, chat-server info, zone handoff), eqcrypt_block |
| src/client/core/eq_strings.cpp | 699 | K14: split. Ten name/price lookups (class/race/deity/opcode/body-type/chat-type/equip-slot/npc-type, `FormatPrice`) and `DumpPacket` are free functions in namespace `eqt::strings` (`include/client/eq_strings.h`); `GetStringMessage`/`GetFormattedStringMessage`/`LoadStringFiles` became `eqt::StringDb`, the fourth context service, reached as `ctx_.strings` / `EverQuest::Strings()`. `DumpEntityAppearance` moved out to `src/client/core/everquest.cpp` (it reads `m_entities`, not a pure lookup) |
| src/client/player/player_misc.cpp | 747 | K08: `eqt::player::PlayerComponent` — assist/split/LFG/shielding/discipline/banker, save, duel, bindwound, track, cosmetic misc social, camera/mp3/sound handlers |
| src/client/player/player_component.cpp | 701 | K08: `eqt::player::PlayerComponent` — emote/AFK/anon/roleplay/camp/logout, rezz/who/inspect (+ OP_InspectAnswer reply, T170), corpse/consent; the four T145 double-prefix senders (SendCamp/SendLogout/SendWhoAllRequest/SendInspectRequest) |
| src/client/core/eq_chat.cpp | 35 | K13: three forwarders (K14 moved the file, no content change). `EverQuest::AddChatSystemMessage`/`AddChatCombatMessage`/`AddChatMissMessage` call `src/client/chat_output.cpp`; the receiver is kept for the callers outside the EverQuest family (action layer, spell/skill managers, `src/client/application_commands.cpp`) |
| src/client/chat_output.cpp | 67 | K13: `eqt::ChatOutput` (`include/client/chat_output.h`) — the three chat output primitives (system / combat / combat-miss), the context service every component reaches as `ctx_.out`. Publishes `SystemMessage`/`ChatMessage` through the bridge and logs to console |
| src/client/player/player_buffs.cpp | 613 | K08: `eqt::player::PlayerComponent` — task/DZ, spell-cast, mana, buff and pet-buff handlers, poison, bind wound, track |
| src/client/player/player_state.cpp | 507 | K08: `eqt::player::PlayerComponent` — SetMoveSpeed/SetCombatStopDistance, PlayerStateAdd/Remove, Death, Stamina, ZonePlayerToBind, LevelUpdate, tradeskill combine. The position/heading/moving and AFK/anon/roleplay/camping accessors stayed on `class EverQuest` in `src/client/core/eq_player_info.cpp` |
| src/client/commands/combat_actions.cpp | 534 | K12: `eqt::commands::CommandsComponent` — Consider/Action/Damage/MoneyOnCorpse/Stun handlers, Bind_Wound reply (T177). T268 deleted the headless ZoneProcessLootItem parser; moved out of `class EverQuest` (was `eq_combat_actions.cpp`) |
| src/client/commands/commands_spells.cpp | 495 | K12: `eqt::commands::CommandsComponent` — RegisterSpellCommands (spell/skill/hotkey/detail/audio commands), moved out of `class EverQuest` (was `eq_commands_spells.cpp`) |
| src/client/core/eq_player_info.cpp | 253 | K13: 46 forwarders (K14 moved the file, no content change). Every player-state accessor on `class EverQuest` now calls `src/client/player_facade.cpp`; the receiver is kept for the callers outside the EverQuest family (combat, the spell/skill/inventory managers, the action layer, the UI) |
| src/client/player_facade.cpp | 279 | K13: `eqt::PlayerFacade` (`include/client/player_facade.h`) — the player-state read/write surface over `eqt::state::GameState`, the context service every component reaches as `ctx_.self`. Stat/currency/weight getters, position/heading/moving, AFK/anon/roleplay/camping, group accessors; the header also carries the T288 position/spawn-id/zone-identity accessor block (`PosX`/`PosY`/`PosZ`/`HeadingDeg`/`SetPosLocal`/`SetHeadingLocal`/`MySpawnId`/`SetMySpawnId`/`CurrentZoneName`/`CurrentZoneId`), `IsFullyZonedIn` and the vendor/bank/trainer window predicates |
| src/client/packet_sender.cpp | 133 | K13: `eqt::PacketSender` (`include/client/packet_sender.h`) — the zone-stream outbound path, the context service every component reaches as `ctx_.net`. `Queue` (opcode + body framing, session recording, the main-thread/async split) and `SafeQueueZone` (the guarded `EQ::Net::Packet` path); owns the Batch R suppressed-send counter and the T268 queued-packet capture seam |
| src/client/player/player_info.cpp | 186 | K08: `eqt::player::PlayerComponent` — tribute/AA handlers (SendAATable/RespondAA/TributeInfo/SendGuildTributes), exp/zonein, world-objects-sent, SyncEntityToGameState/RemoveEntityFromGameState |
| src/client/zone/zone_handshake.cpp | 320 | K09: `eqt::zone::ZoneComponent` — the ZoneSend* handshake senders (ZoneEntry, ReqClientSpawn, ClientReady, SetServerFilter, StreamIdentify, Ack, ReqNewZone, AA/tribute requests, SendExpZonein). ZoneSendSessionReady and ZoneSendSpawnAppearance were deleted, zero callers |
| src/client/commands/commands_movement.cpp | 321 | K12: `eqt::commands::CommandsComponent` — RegisterMovementCommands (movement/combat/utility-debug/sky commands), moved out of `class EverQuest` (was `eq_commands_movement.cpp`) |
| chat_component.cpp (in `src/client/chat/`) | 607 | K03: `eqt::chat::ChatComponent` — channel message send/process, `SendChatMessage`, `ProcessChatInput`, `DispatchSlashCommand` (registry 1 entry point), emote send/process, colored/formatted/simple text, and `ChatChannelName()` (T320), moved out of `class EverQuest` (was `src/client/core/eq_chat.cpp`) |
| group_component.cpp (in `src/client/group/`) | 960 | K04: `eqt::group::GroupComponent` — group invite/update/disband, raid (T169–T184: OP_RaidUpdate decode, raid senders), pet accessors/commands, moved out of `class EverQuest` (was `eq_group.cpp`) |
| guild_component.cpp (in `src/client/guild/`) | 276 | K01: `eqt::guild::GuildComponent` — the guild senders + processors, moved out of `class EverQuest` (was `eq_guild.cpp`; m_guild_id populated T133) |
| gm_component.cpp (in `src/client/gm/`) | 317 | K02: `eqt::gm::GmComponent` — the GM command senders + processors, moved out of `class EverQuest` (was `eq_gm.cpp`; wired T148) |
| src/client/commands/commands_train.cpp | 265 | K12: `eqt::commands::CommandsComponent` — T280: `/train` and `/trainclose` game-registry side, moved out of `class EverQuest` (was `eq_commands_train.cpp`) |
| src/client/player/profile_appearance.cpp | 20 | PlayerProfile appearance fields stored so `/appearance` merges faithfully (T167); header `include/client/player/profile_appearance.h` |
| src/client/action/eq_action_handler_extended.cpp | 957 | J12: the `EqActionHandler` remainder — GM/debug, camp/logout, report, world-object and tradeskill actions. K14 moved the file (one call-site retarget: `FormatVendorPrice` inlined to `eqt::strings::FormatPrice`, and `GetClassName` re-qualified to `eqt::strings::GetClassName`) |
| src/client/action/eq_action_handler_spells_trade.cpp | 570 | J12: `EqActionHandler`'s spell, pet, trade and `/appearance` actions (SendFaceChange's producer, T153). K14 moved the file, no content change |
| src/client/bridge/bridge_component_intents.cpp | 702 | K11: `eqt::bridge::BridgeComponent::ProcessBridgeIntents` — the `RendererIntent` variant switch — and `ProcessBridgeEvents` |
| src/client/bridge/bridge_component_ui_audio.cpp | 710 | K11: `eqt::bridge::BridgeComponent` — inventory stat sync, the loot window, per-client config / hotbar / inspect-message persistence, the audio lifecycle and hotbar slot activation |
| src/client/merchant/merchant_trade.cpp | 714 | K05: `eqt::merchant::MerchantComponent` — the trade/loot/vendor/bank/tradeskill Setup*Callbacks, the loot transfer handlers and the shop request/buy/sell/money/end handlers (was `eq_merchant_trade.cpp`, J13) |
| src/client/merchant/merchant_vendor_bank.cpp | 633 | K05: `eqt::merchant::MerchantComponent` — the vendor window packet + open/buy/sell/close senders, the bank window, the GM trainer (owner of the moved `m_trainer_skill_caps`) and the Phase 4 tradeskill recipe senders/handlers (was `eq_merchant_vendor_bank.cpp`, J13) |
| entities_component.cpp (in `src/client/entities/`) | 847 | K06: `eqt::entities::EntitiesComponent` — zone spawns, time of day, spawn doors, zonepoints, spawn appearance, moved out of `class EverQuest` (was `eq_entities.cpp`) |
| entities_component_appearance.cpp (in `src/client/entities/`) | 668 | K06: `eqt::entities::EntitiesComponent` — teardown/health/appearance cluster (DeleteSpawn, MobHealth, HPUpdate, FindEntityByName, ListEntities, WearChange, ApplyAppearanceToGameState, UpdatePlayerAppearanceFromInventory, Illusion), moved out of `class EverQuest` (was `eq_entities_appearance.cpp`; entity size T318) |
| entities_component_updates.cpp (in `src/client/entities/`) | 784 | K06: `eqt::entities::EntitiesComponent` — ground spawns, weather, new spawns, exp updates, ClientUpdate interpolation, moved out of `class EverQuest` (was `eq_entities_updates.cpp`) |
| entities_component_world.cpp (in `src/client/entities/`) | 406 | K06: `eqt::entities::EntitiesComponent` — doors, spawn-appearance/animation/click-door senders, world-object (tradeskill container) senders and handlers, moved out of `class EverQuest` (was `eq_entities_world.cpp`) |
| src/client/movement/movement_state.cpp | 896 | K07: MovementComponent -- pathfinding (LoadPathfinder/FindPath/FollowPath), SetMovementMode / SetPositionState, jump, movement history, water and region state, keyboard control |
| src/client/movement/movement_update.cpp | 850 | K07: MovementComponent -- UpdateMovement (the per-tick pump), SendPositionUpdate and the wire packing (wire z = groundZ + 0.625*size, T311) |

### Family grouping (source: `refactor/K_component_extraction.md` §Families)

The flat `eq_*.cpp` split names families that Batch K extracts into components.
This table and that plan must agree; change both together.

| family | TUs | `EverQuest::` defs | single-family members | K unit |
|---|---|---|---|---|
| guild | `guild/guild_component` | 11 | 3 | **K01 DONE** (pilot; `ClientContext` in `include/client/client_context.h`) |
| gm | `gm/gm_component` | 12 | 2 | **K02 DONE** |
| chat | `chat/chat_component`, `core/eq_chat` (AddChat* only), `core/eq_strings` | 13 | 1 | **K03 DONE**, **eq_strings split by K14** (10 of 13 defs moved; the three AddChat* output primitives stay on `EverQuest`; `eq_strings` split into `eqt::strings` free functions + `eqt::StringDb`, K14) |
| group | `group/group_component` | 23 | 5 | **K04 DONE** (group, raid and pet together; `DismissPet`/`UpdateGroupMemberFromEntity` deleted, zero callers) |
| merchant | `merchant/merchant`, `_trade`, `_vendor_bank` | 54 | 1 | **K05 DONE** (54 defs, not the 56 the batch table claimed; all 54 moved. `m_trainer_skill_caps` moved; the loot/scribe members stay on `EverQuest` pending K09/K11; `m_vendor_items`/`GameVendorItem` moved to `CommandsComponent` by K12, the last family reading it -- reached from Merchant's writers via `owner_.Commands().VendorItems()`) |
| entities | `entities/entities_component` (+ `_appearance`/`_updates`/`_world`) | 28 | 1 | **K06 DONE** (`m_entities`, the spawn-id -> Entity map, stays a core member — the component owns the handlers) |
| movement | `src/client/movement/movement_component`, `movement_state`, `movement_update` | 42 | 8 | **K07 DONE** (`eqt::movement::MovementComponent`; `HasReachedDestination` deleted, zero callers; `m_last_movement_update` boarded dead) |
| player | `src/client/player/player_component`, `player_misc`, `player_state`, `player_buffs`, `player_info`, `profile_appearance`; facade in `src/client/core/eq_player_info.cpp` | 139 | 11 | **K08 DONE** (139 defs, not the 142 the batch table claimed; 93 moved, the 46-body player-state facade stayed on `EverQuest`) |
| zone | `src/client/zone/zone_component`, `zone_dispatch`, `zone_entry`, `zone_handshake` | 34 | 39 | **K09 DONE** (`eqt::zone::ZoneComponent`; `DispatchZonePacket` moved whole; `ZoneSendSessionReady`/`ZoneSendSpawnAppearance` deleted and `m_zone_sequence` boarded dead; the zone connection itself — `ConnectToZone`, the `ZoneOn*` callbacks, `ReplayZonePacket`, `SafeQueueZonePacket` — stays in `src/client/core/everquest.cpp`) |
| login_world | `src/client/login_world/login_world_component` | 37 | 22 | **K10 DONE** (`eqt::login_world::LoginWorldComponent`; both connection managers moved, `ConnectToLogin`/`ConnectToWorld` moved in from `src/client/core/everquest.cpp`; `WorldSendSessionReady` and the two `World*ChannelMessage` declarations deleted, zero callers) |
| bridge | `src/client/bridge/bridge_component`, `bridge_component_intents`, `bridge_component_ui_audio` | 45 | 14 | **K11 DONE** (`eqt::bridge::BridgeComponent`; the loot cluster and the T171 inspect message moved in; `SaveEntityDataToFile`, `PlaySoundAt`, `PlayCombatSound`, `PlaySpellSound`, `PlayUISound` and seven dead inline accessors deleted, zero callers; FIVE sanctioned lifecycle forwarders — `setBridge`, `InitGraphics`, `ShutdownGraphics`, `GameTick`, `PostRenderTick` — stay on `EverQuest`, defined in `src/client/core/everquest.cpp`) |
| commands + combat | `src/client/commands/commands_component`, `_movement`, `_spells`, `_train`, `combat_actions` | 28 | 2 | **K12 DONE** (`eqt::commands::CommandsComponent`; combat folded in, per the batch's revised note; `m_stunned_until` moved single-family, `m_vendor_items`/`GameVendorItem` moved in from `EverQuest` -- K05 had left it there naming K12 as the last family reading it; `runLoadDiagnostics` has zero callers but is kept, private -- it is the sole publisher of `DiagnosticsSceneDump`, still consumed by `irrlicht_bridge.cpp`) |
| core | `core/everquest` | 38 | 3 | stays — ctor/dtor, wiring, `Test_*` seams. K14 moved `eq.cpp` to `src/client/core/everquest.cpp` |
| action_handler | `action/eq_action_handler*` | 19 | 0 | K14 moved into `src/client/action/`; retarget onto components is a later measured unit |

## Application (`include/client/application.h`) — split by J06

| TU | Lines | Contents |
|---|---|---|
| src/client/application.cpp | 948 | Core: initialize, run, shutdown, requestQuit, gameThreadLoop, mainLoop |
| application_commands.cpp | 552 | processNetworkEvents/processInput, processPreConnectionLifecycleCommands, loadScriptFile/updateScript, dispatchToActionOrRenderer (the renderer-catalog gate), printCommandResult, wireUnifiedCommandDispatch, `Application::DispatchSlashCommand` |
| application_zone_loading.cpp | 458 | start/joinLoadingThread, checkLoadingComplete, loadZoneGraphicsOnThread (registers snapshot entities into the EntityRenderer), loadZoneGraphics, setupRDPAudio, updateLoadingProgress |
| application_config.cpp | 434 | parseArguments, loadConfigFile |
| application_frame.cpp | 224 | updateGameState, render |

## Slash commands — action::CommandProcessor — split by J04 (+ T268/T269/T280)

| TU | Lines | Contents |
|---|---|---|
| command_processor_social.cpp | 798 | Chat, target/attack, group/raid, duel, trade, who/inspect, guild |
| command_processor_registration.cpp | 791 | `registerBuiltinCommands()` — 142 `registerCommand({…})` calls, the whole registry-2 vocabulary |
| command_processor_spells_gm.cpp | 713 | Cast/mem/gems/buffs/spellbook, pet, stance and movement modes, logging controls, track/find, the GM family, /appearance |
| command_processor_world.cpp | 490 | Corpse/consent, world objects, tradeskill recipes, vendor/buy/sell, equip/read/scribe, loc/sit/stand/move/warp/follow, quit/camp, debug/perf/time |
| src/client/action/command_processor.cpp | 284 | Core: processCommand/processInput, registerCommand, lookup/completion/category queries, parsing helpers, message sinks |
| command_processor_bank.cpp | 158 | T269: `/bank` and its coin sub-commands |
| command_processor_loot.cpp | 131 | T268: the loot-window commands |
| command_processor_train.cpp | 43 | T280: `/train`, `/trainclose` |
| command_processor_internal.h | 60 | J04 private header: the shared statics/helpers the four split TUs need |

## Inventory — InventoryManager — split by J01

| TU | Lines | Contents |
|---|---|---|
| inventory_cursor.cpp | 800 | Cursor pickup/place, partial stacks, cursor money, scribe scroll, swap/move, equip validation |
| inventory_manager.cpp | 581 | Core: slot get/set/remove, item cache, per-container accessors, stat/weight calculation, destroy |
| inventory_item_parser.cpp | 503 | The wire item-blob parser |
| inventory_bags.cpp | 414 | Bag/world-container/bank-bag placement rules, lore conflicts, bag contents |
| inventory_packets.cpp | 276 | processCharInventory, processItemPacket, move/delete responses, dumpInventory |
| inventory_constants.cpp | 125 | Slot constants and name tables |

## Renderer — IrrlichtRenderer class (`include/client/graphics/irrlicht_renderer.h`)

K16 (board B006): `IrrlichtRenderer` now implements
`EQT::Graphics::IZoneLoadSurface` (`include/client/graphics/zone_load_surface.h`,
37 lines) — the two-method zone-load surface, `initLoadingScreen(const
RendererConfig&)` and `initDeferredGraphics()`. That is the whole of what
`BridgeComponent::InitGraphics` ever did with the renderer pointer, so
`EverQuest::InitGraphics` / `BridgeComponent::InitGraphics` take the interface
and the game side names no renderer class. It is the class's only base.

`src/client/graphics/renderer/` (the class's own four TUs, moved there by L12)
plus the eleven components Batch L extracted, each in its own directory.

Batch L (`refactor/L_renderer_component_extraction.md`) is doing to
`IrrlichtRenderer` what Batch K did to `EverQuest`. L01 created
`include/client/graphics/renderer_context.h` (261 lines) —
`EQT::Graphics::RendererContext`, the renderer's counterpart to
`include/client/client_context.h`. L12 (the close-out) widened it to **54 core
members + 44 promoted shared members + 11 sibling-component pointers**: the
promoted set is every `IrrlichtRenderer` member the census measured at three or
more components, and the pointers are what `owner_.<Family>()` became
(`ctx_.<Family>()`, set by `SetComponents()` after the last `make_unique`).
L12 also removed `RdpComponent`'s `IrrlichtRenderer&` (zero reaches on both
census axes) and `SlashCommandsComponent`'s `friend` line. The residue is
boarded as **B050**.

| Component | Header | TU |
|---|---|---|
| `EQT::Graphics::RdpComponent` (no `friend` line, and no `IrrlichtRenderer&` since L12) | `include/client/graphics/rdp/rdp_component.h` | `src/client/graphics/rdp/rdp_component.cpp` |
| `EQT::Graphics::Eqe1ArchiveComponent` | `include/client/graphics/eqe1/eqe1_archive_component.h` | `src/client/graphics/eqe1/eqe1_archive_component.cpp` |
| `EQT::Graphics::OverlaysComponent` | `include/client/graphics/overlays/overlays_component.h` | `src/client/graphics/overlays/overlays_component.cpp`, `src/client/graphics/overlays/overlays_hud_env.cpp` |
| `EQT::Graphics::DiagnosticsComponent` | `include/client/graphics/diagnostics/diagnostics_component.h` | `src/client/graphics/diagnostics/diagnostics_component.cpp`, `src/client/graphics/diagnostics/diagnostics_debug.cpp` |
| `EQT::Graphics::PlayerCameraComponent` | `include/client/graphics/player/player_camera_component.h` | `src/client/graphics/player/renderer_player_physics.cpp`, `src/client/graphics/player/renderer_player_collision.cpp`, `src/client/graphics/player/renderer_camera.cpp` |
| `EQT::Graphics::SimBridgeComponent` | `include/client/graphics/sim/sim_bridge_component.h` | `src/client/graphics/sim/renderer_sim_bridge.cpp` |
| `EQT::Graphics::ZoneStreamComponent` | `include/client/graphics/zone_stream/zone_stream_component.h` | `src/client/graphics/zone_stream/renderer_zone_stream.cpp`, `src/client/graphics/zone_stream/renderer_zone_merge.cpp` |
| `EQT::Graphics::ZoneLifecycleComponent` | `include/client/graphics/zone_lifecycle/zone_lifecycle_component.h` | `src/client/graphics/zone_lifecycle/renderer_zone_lifecycle.cpp`, `src/client/graphics/zone_lifecycle/renderer_zone_draw.cpp`, `src/client/graphics/zone_lifecycle/renderer_gpu_upload.cpp` |
| `EQT::Graphics::ZoneLoadComponent` | `include/client/graphics/zone_load/zone_load_component.h` (with the private `include/client/graphics/zone_load/zone_load_context.h`) | `src/client/graphics/zone_load/renderer_zone_load.cpp`, `src/client/graphics/zone_load/renderer_entity_load_gate.cpp`, `src/client/graphics/zone_load/renderer_zone_load_objects.cpp`, `src/client/graphics/zone_load/renderer_zone_load_entities.cpp`, `src/client/graphics/zone_load/renderer_zone_load_env.cpp` |
| `EQT::Graphics::FrameComponent` | `include/client/graphics/frame/frame_component.h` | `src/client/graphics/frame/renderer_frame.cpp`, `src/client/graphics/frame/renderer_frame_input.cpp`, `src/client/graphics/frame/renderer_frame_render.cpp` |

| TU | Lines | Contents |
|---|---|---|
| src/client/graphics/player/renderer_player_physics.cpp | 840 | `EQT::Graphics::PlayerCameraComponent` — `updatePlayerMovement()`, the 806-line per-frame local movement path (a Batch G decomposition candidate; it moved whole). Extracted from IrrlichtRenderer by L05 (Batch L). Header `include/client/graphics/player/player_camera_component.h`. The basename is a string literal in `tests/test_first_person_camera_single_formula.cpp` — do not rename |
| src/client/graphics/diagnostics/diagnostics_component.cpp | 1,160 | EQT::Graphics::DiagnosticsComponent -- dumpScene, scene-breakdown and frame-timing profiling, getMemoryReport/getMemoryReportBreakdown, EQE1 usage report (T207/T214). Extracted from IrrlichtRenderer by L04 (Batch L). Header include/client/graphics/diagnostics/diagnostics_component.h |
| src/client/graphics/zone_lifecycle/renderer_zone_draw.cpp | 1,156 | `EQT::Graphics::ZoneLifecycleComponent` — createZoneMesh, sorted/portal zone drawing, region meshes, VBO helpers. Also hosts `IrrlichtRenderer::RenderPassTimer::OnRenderPassPreRender`, which did NOT move. Extracted from IrrlichtRenderer by L09 (Batch L). Header `include/client/graphics/zone_lifecycle/zone_lifecycle_component.h` |
| src/client/graphics/zone_load/renderer_zone_load_env.cpp | 1,140 | `EQT::Graphics::ZoneLoadComponent` — loadZoneStep10_Sky, 11_Env, 12_Lights, 12b_Simulation, 13_Cleanup. Extracted from IrrlichtRenderer by L10 (Batch L). Header `include/client/graphics/zone_load/zone_load_component.h` |
| src/client/graphics/sim/renderer_sim_bridge.cpp | 1,114 | `EQT::Graphics::SimBridgeComponent` — SimulationWorker marshaling: buildSimulationZoneData, worker start/stop, postSimulationInput, applySimulationResults, and applySimulationOutput/bindFrameSimulationBuffers (split by lifetime contract, T116). Extracted from IrrlichtRenderer by L07 (Batch L). Header `include/client/graphics/sim/sim_bridge_component.h`. SimulationWorker itself stays owned by IrrlichtRenderer — L07 changed no thread ownership |
| src/client/graphics/overlays/overlays_component.cpp | 1,057 | `EQT::Graphics::OverlaysComponent` — map/navmesh overlays, target outline/selection, mouse targeting/LOS, zone-line overlay and bounding boxes, FPS counter. Extracted from IrrlichtRenderer by L03 (Batch L). Header `include/client/graphics/overlays/overlays_component.h` |
| src/client/graphics/zone_load/renderer_zone_load.cpp | 1,009 | `EQT::Graphics::ZoneLoadComponent` — the loadZoneSequential() driver (the loading thread's entry) + steps 0_EQE1, 1_S3D, 2_BSP, 4_Regions, 5_Assets, with ZoneLoadContext (Batch G01; split T166). Extracted from IrrlichtRenderer by L10 (Batch L). Header `include/client/graphics/zone_load/zone_load_component.h`; the private ZoneLoadContext definition is `include/client/graphics/zone_load/zone_load_context.h` |
| src/client/graphics/renderer/irrlicht_renderer.cpp | 955 | Core after J11: ctor/dtor, run(), saveScreenshot, sky/door/world-object façades, player/vision state, createEntityRenderer, diagnostics setters |
| src/client/graphics/zone_stream/renderer_zone_stream.cpp | 874 | `EQT::Graphics::ZoneStreamComponent` — lazy/background mesh load pipeline, progressive load, PVS depth and neighbourhood bookkeeping, entity-prep request queue, deferred-entity pickup (T111/T210). Extracted from IrrlichtRenderer by L08 (Batch L). Header `include/client/graphics/zone_stream/zone_stream_component.h` |
| src/client/graphics/renderer/renderer_init.cpp | 831 | J11: init/shutdown, loading screen (initLoadingScreen, loadGlobalAssets, show/hideLoadingScreen), plus the `RendererContext` aggregate initialiser, the eleven component constructions and `ctx_.SetComponents()`. Moved into `renderer/` by L12 |
| src/client/graphics/zone_lifecycle/renderer_zone_lifecycle.cpp | 732 | `EQT::Graphics::ZoneLifecycleComponent` — unloadZone (retains persistent EQE1 archives, T214), setZoneEnvironment, placeholder build/destroy, instant scene, zone-ready state. Extracted from IrrlichtRenderer by L09 (Batch L) |
| src/client/graphics/overlays/overlays_hud_env.cpp | 653 | `EQT::Graphics::OverlaysComponent` (second TU) — fog setup, loading-screen draw, environmental display settings, HUD setup/update, inventory paperdoll |
| src/client/graphics/slash/renderer_slash_commands.cpp | 594 | `EQT::Graphics::SlashCommandsComponent` — `processSlashCommand()`, the registry-3 renderer slash-command if-chain (593 lines, moved whole; a Batch G decomposition candidate). Extracted from IrrlichtRenderer by L06 (Batch L). Header `include/client/graphics/slash/slash_commands_component.h`. The basename is compiled into `tests/test_merged_help.cpp` as `RENDERER_COMMAND_SOURCE` and parsed there — do not rename |
| src/client/graphics/zone_load/renderer_zone_load_objects.cpp | 579 | `EQT::Graphics::ZoneLoadComponent` — loadZoneStep6_Objects, 7_Doors, 9_Collision. Extracted from IrrlichtRenderer by L10 (Batch L) |
| src/client/graphics/diagnostics/diagnostics_debug.cpp | 577 | EQT::Graphics::DiagnosticsComponent -- runtime debug toggles/cycles, debug shader swaps, indoor region map load. Same header |
| src/client/graphics/player/renderer_camera.cpp | 534 | `EQT::Graphics::PlayerCameraComponent` (third TU) — camera/projection per mode, `repositionFirstPersonCamera()` (THE one first-person formula), first-person pitch clamp and vFOV, `/fpcam` dump, camera mode cycling, player position/vision/swimming state. The basename is a string literal in `tests/test_first_person_camera_single_formula.cpp` — do not rename |
| src/client/graphics/player/renderer_player_collision.cpp | 743 | `EQT::Graphics::PlayerCameraComponent` (second TU) — movement collision, world-triangle raycasts, ground-height queries, the incremental collision-registration API and the collision debug lines. Same header |
| src/client/graphics/zone_stream/renderer_zone_merge.cpp | 576 | ZoneMergeBuffers (build/destroy/renderMerged) + the texture-eviction guards: RegionMeshTextureGuard, EntityMeshTextureGuard, ObjectNodeTextureGuard (T123), DoorTextureGuard/boids (T124), plus `EQT::Graphics::ZoneStreamComponent`'s three guard-registration bodies. Extracted from IrrlichtRenderer by L08 (Batch L). Its header `include/client/graphics/renderer_zone_merge.h` did NOT move — it declares namespace-scope classes and free functions, and twelve TUs plus three guard tests include it by that path |
| src/client/graphics/zone_load/renderer_zone_load_entities.cpp | 552 | `EQT::Graphics::ZoneLoadComponent` — loadZoneStep8_Entities + 8a_BaseModelPrep, 8b_VariantModelPrep, 8c_VariantTextures, 8d_EquipmentPrep, 8e_DistributePrepResults, 8f_BuildEntityMesh. Extracted from IrrlichtRenderer by L10 (Batch L). The T109 load gate (ensureEntityPrepWorkerStarted, deferEntityForLoadGate) is ZoneLoadComponent's own since L12, in src/client/graphics/zone_load/renderer_entity_load_gate.cpp |
| src/client/graphics/frame/renderer_frame_input.cpp | 756 | `EQT::Graphics::FrameComponent` — frame phases 1-3: processFrameInput, processCommonInput, processPlayerInput, processInputDeltas, processChatInput, processFrameVisibility, processFrameSimulation. Extracted from IrrlichtRenderer by L11 (Batch L). Header `include/client/graphics/frame/frame_component.h`. Read AS TEXT by `tests/test_static_ui_scroll.cpp` and `tests/test_debug_hotkey_arms.cpp` — do not rename |
| src/client/graphics/frame/renderer_frame_render.cpp | 598 | `EQT::Graphics::FrameComponent` — frame phase 4: processFrameRender, the 546-line draw pass (a recorded Batch-G decomposition candidate; it moved whole). Extracted from IrrlichtRenderer by L11 (Batch L). Same header. Read AS TEXT by `tests/test_ui_hide_command.cpp` — do not rename |
| src/client/graphics/frame/renderer_frame.cpp | 537 | `EQT::Graphics::FrameComponent` — processFrame, the four-phase orchestrator, plus the frame-timing accumulation and the spike/RED/budget diagnostics. Extracted from IrrlichtRenderer by L11 (Batch L). Same header, plus the component's out-of-line ctor/dtor. Also hosts `FrameComponent::measureSection()` — L12 finished the move L04 predicted and L11 was told to leave, so the declaration and `sectionStart_` are the component's now; the write-only `frameStart_` was deleted in the same unit |
| src/client/graphics/renderer/renderer_entities.cpp | 502 | Entity/inventory/combat-anim façades (thin forwards). Moved into `renderer/` by L12 |
| src/client/graphics/zone_lifecycle/renderer_gpu_upload.cpp | 447 | `EQT::Graphics::ZoneLifecycleComponent` — GPU upload thread init/drain/apply. GPUUploadThread still owns its own thread; the handle stays on RendererContext. Extracted from IrrlichtRenderer by L09 (Batch L) |
| src/client/graphics/renderer/renderer_event_receiver.cpp | 385 | J11: `RendererEventReceiver::OnEvent` and the key/mouse query surface. Moved into `renderer/` by L12 — **read AS TEXT by `tests/test_ui_hide_command.cpp`, `tests/test_static_ui_scroll.cpp` and `tests/test_debug_hotkey_arms.cpp`, all three repointed in the same unit** |
| src/client/graphics/eqe1/eqe1_archive_component.cpp | 726 | EQT::Graphics::Eqe1ArchiveComponent -- EQE1 flat-pack archive open/resolve/index/report plus the fire-glow icosphere renderer (EQT_HAS_DRM), extracted from IrrlichtRenderer by L02 (Batch L). Header include/client/graphics/eqe1/eqe1_archive_component.h |
| src/client/graphics/rdp/rdp_component.cpp | 233 | EQT::Graphics::RdpComponent -- RDP server integration (WITH_RDP guard), extracted from IrrlichtRenderer by L01 (Batch L). Header include/client/graphics/rdp/rdp_component.h |
| src/client/graphics/zone_load/renderer_entity_load_gate.cpp | 78 | `EQT::Graphics::ZoneLoadComponent` — ensureEntityPrepWorkerStarted, deferEntityForLoadGate, the entity load distance gate (T109/T111). Folded into the component and moved into `zone_load/` by L12 (L10 deviation 1, closed) |

## Zone shader — split by J09

| TU | Lines | Contents |
|---|---|---|
| zone_shader_sources_gles2.h | 1,017 | GLSL ES 1.0 shader sources (private header, `zone_shader.cpp` only) |
| zone_shader_callbacks.h | 702 | `ShaderCallback` and friends (private header, `zone_shader.cpp` only) |
| src/client/graphics/zone_shader.cpp | 525 | Material registration and the `getActive*()` accessors |

The two headers are `#include`d by `zone_shader.cpp` and by nothing else;
they hold file-local `static` definitions and are not a public interface.
T326 deleted a third, desktop-GL sources header — its build configuration was
retired by P30/P32 and no supported build parsed it.

## Simulation worker — SimulationWorker class

| TU | Lines | Contents |
|---|---|---|
| sim_particles.cpp | 1,168 | Particle system + spell VFX |
| sim_boids.cpp | 743 | Boids + tumbleweeds |
| sim_visibility.cpp | 710 | Regions, PVS, portals, occlusion, light selection, frustum helper |
| simulation_worker.cpp | 561 | Lifecycle core: ctor, start/stop, data intake, workerLoop, computeAll dispatch |
| sim_animation.cpp | 538 | Fire flicker, tree/vertex/detail animation, light animation |
| sim_entity_sync.cpp | 510 | Entity sync/interp/visibility, name tags |
| sim_weather.cpp | 305 | Weather system, sky state |

## Entity renderer — EntityRenderer class (split by J03)

| TU | Lines | Contents |
|---|---|---|
| entity_updates.cpp | 645 | J03: createEntity, updateEntity, pending-update queues, main-thread entity state |
| entity_mesh_build.cpp | 612 | J03: registerEntity, buildEntityMesh, prep-result distribution, composite path selection |
| entity_renderer.cpp | 540 | Core after J03: ctor/dtor, con colors, global character loading, per-race scale/color, placeholder mesh, animation/pose/weapon/light setters |
| entity_build_step.cpp | 495 | J03: processOneEntityBuildStep — the frame-budgeted build step |
| entity_prep_worker.cpp | 476 | EntityPrepWorker: background entity prep (start/stop, requestPrep, priority sort, prepVariantTextures, prepEquipmentModels) |
| entity_lifecycle.cpp | 451 | J03: removeEntity, corpse decay, appearance update, clearEntities, visibility/name-tag/material toggles |
| entity_combat_anim.cpp | 437 | Combat animation queues + animation queries |
| entity_first_person_draw.cpp | 401 | T309: the attack-animation draw rule, first-person mode, player entity position/animation |
| entity_interpolation.cpp | 372 | J03: updateInterpolation |
| entity_equipment.cpp | 320 | Equipment models, attachment, bone transforms |
| entity_castbars.cpp | 264 | Casting bar state + rendering |
| entity_spatial.cpp | 218 | Spatial grid, BSP occlusion cache, boats |
| entity_size.cpp | 58 | setEntitySize |

## WLD loader — WLDLoader class (split by J05)

| TU | Lines | Contents |
|---|---|---|
| wld_loader.cpp | 475 | Core: parseFromArchive/parseWldBuffer, the fragment dispatch loop, memory accounting, region bounds/lookup, checkZoneLine |
| wld_fragments_actor.cpp | 504 | 0x14/0x15 actor def+ref, 0x2C/0x2D mesh reference |
| wld_fragments_bsp.cpp | 502 | 0x37, 0x21 BSP tree, 0x22 region, 0x29 region flags, zone-line string decode |
| wld_fragments_mesh.cpp | 484 | 0x36 mesh, combined/per-region geometry accessors, PVS presence |
| wld_fragments_skeleton.cpp | 344 | 0x10–0x13 skeleton track/def/ref |
| wld_fragments_light.cpp | 261 | 0x1B/0x1C light source, 0x28 point light, 0x2A/0x35 ambient, 0x2F |
| wld_fragments_material.cpp | 159 | 0x03/0x04/0x05 texture, 0x30/0x31 material |
| wld_internal.h | 18 | J05 private header shared by the seven TUs |

## Test build — tests/CMakeLists.txt (split by J20)

`tests/CMakeLists.txt` (135 lines) is now a driver that `include()`s 18
per-domain fragments from `tests/cmake/`: `action_handlers`, `archives`,
`audio`, `commands`, `core_utils`, `data_formats`, `database`, `eqe1`,
`graphics`, `midi`, `network`, `packets_skills`, `renderer_guards`,
`spells`, `state`, `ui_rendering`, `world_geometry`, `zone_integration`.
Add a new test to the fragment for its domain, not to the driver.

## Conventions

- New extractions follow the patterns established in Batch F unit reviews
  (`refactor/F14`–`F24`), T166 (`refactor/T166_split_renderer_zone_load.md`)
  and Batch J (`refactor/J01_*` … `refactor/J20_*`):
  TUs re-open `namespace EQT { namespace Graphics }`, add full includes for
  types the class header only forward-declares, and file-local static
  helpers live with their sole callers.
- File-size ceilings: warn >1,500, fail >3,000
  (`scripts/refactor/check_file_sizes.sh`; policy in docs/decisions.md).
- Behavior-preserving moves are verified with
  `scripts/refactor/verify_move_only.py BASE..HEAD`.
- **Cite a function or symbol, not a line number.** Every Batch J split
  invalidated hundreds of `file.cpp:NNNN` citations at once; J21 had to
  repair them by hand. A stale line number is worse than no line number.
