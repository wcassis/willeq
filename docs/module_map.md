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

`src/client/eq.cpp` + 36 `eq_*.cpp` domain TUs + the extracted components under
`src/client/<family>/` (Batch K).

| TU | Lines | Contents |
|---|---|---|
| src/client/eq.cpp | 1,073 | Core after J02: ctor/lifecycle, session recording, DB/spell-system init, ConnectToLogin/World/Zone, TickNetwork, loading-phase state, PublishFullStateSnapshot, zone connection callbacks, ZoneOnPacketRecv, ReplayZonePacket, SafeQueueZonePacket, QueuePacket, Start/StopUpdateLoop, SetEQClientPath/SetConstrainedConfig |
| eq_action_handler.cpp | 783 | `action::EqActionHandler` — the `IActionHandler` implementation wrapping `EverQuest` for `ActionDispatcher`/`CommandProcessor` (action-layer migration T99–T143; raid guards T179–T184; skill gate T159/T162) |
| src/client/movement/movement_component.cpp | 492 | K07: eqt::movement::MovementComponent -- Move/MoveTo/MoveToEntity, Follow/StopFollow, StartCombatMovement, Face/FaceEntity, StopMovement |
| eq_bridge.cpp | 890 | InitGraphics/ShutdownGraphics, GameTick, Pre/PostRenderTick, ProcessBridgeIntents/Events, zone-load snapshot, loot-window flow, hotbar config, audio init |
| src/client/merchant/merchant.cpp | 689 | K05: `eqt::merchant::MerchantComponent` — inventory item movement, ScribeSpellFromScroll, the trade request/coin/accept/cancel senders, ReadBook and ZoneProcessTradePartnerItem, moved out of `class EverQuest` (was `eq_merchant.cpp`) |
| eq_commands.cpp | 1,110 | RegisterCommands driver + the game-registry remainder after T139 deleted 34 redundant commands (registry 1 of `docs/decisions.md` 2026-08-03) |
| eq_zone.cpp | 1,094 | Zone change/teardown, CleanupZone, DisconnectFromZone, LoadZoneMap/Lines/Sources, CheckZoneLine, GetBestZ/FixZ, in-zone teleport (T231) |
| eq_zone_entry.cpp | 1,057 | J02: ZoneProcessNewZone, ZoneProcessPlayerProfile (the LOAD-BEARING player heading flip), ZoneProcessAnimation, ZoneProcessGuildMOTD, CheckZoneRequestPhaseComplete |
| eq_zone_dispatch.cpp | 958 | J02: `DispatchZonePacket` — the zone-stream opcode switch, nothing else |
| eq_login_world.cpp | 945 | Login-server + world-server connection handling, eqcrypt_block |
| eq_strings.cpp | 767 | Name/string lookup tables, opcode names, dump helpers |
| src/client/player/player_misc.cpp | 747 | K08: `eqt::player::PlayerComponent` — assist/split/LFG/shielding/discipline/banker, save, duel, bindwound, track, cosmetic misc social, camera/mp3/sound handlers |
| src/client/player/player_component.cpp | 701 | K08: `eqt::player::PlayerComponent` — emote/AFK/anon/roleplay/camp/logout, rezz/who/inspect (+ OP_InspectAnswer reply, T170), corpse/consent; the four T145 double-prefix senders (SendCamp/SendLogout/SendWhoAllRequest/SendInspectRequest) |
| eq_chat.cpp | 67 | K03: only the three AddChat* output primitives (system/combat/miss) are left on `class EverQuest`; everything else moved to `src/client/chat/chat_component.cpp` |
| src/client/player/player_buffs.cpp | 613 | K08: `eqt::player::PlayerComponent` — task/DZ, spell-cast, mana, buff and pet-buff handlers, poison, bind wound, track |
| src/client/player/player_state.cpp | 507 | K08: `eqt::player::PlayerComponent` — SetMoveSpeed/SetCombatStopDistance, PlayerStateAdd/Remove, Death, Stamina, ZonePlayerToBind, LevelUpdate, tradeskill combine. The position/heading/moving and AFK/anon/roleplay/camping accessors stayed on `class EverQuest` in `src/client/eq_player_info.cpp` |
| eq_combat_actions.cpp | 526 | Consider/Action/Damage/MoneyOnCorpse handlers, Bind_Wound reply (T177). T268 deleted the headless ZoneProcessLootItem parser |
| eq_commands_spells.cpp | 494 | RegisterSpellCommands (spell/skill/hotkey/detail/audio commands) |
| src/client/eq_player_info.cpp | 285 | K08: the PLAYER-STATE FACADE that stayed on `class EverQuest` — stat/currency/weight getters, position/heading/moving accessors, AFK/anon/roleplay/camping flags, group accessors |
| src/client/player/player_info.cpp | 186 | K08: `eqt::player::PlayerComponent` — tribute/AA handlers (SendAATable/RespondAA/TributeInfo/SendGuildTributes), exp/zonein, world-objects-sent, SyncEntityToGameState/RemoveEntityFromGameState |
| eq_zone_handshake.cpp | 349 | J02: the ZoneSend* handshake senders (SessionReady, ZoneEntry, ReqClientSpawn, ClientReady, SetServerFilter, StreamIdentify, Ack, ReqNewZone, AA/tribute requests, SpawnAppearance, SendExpZonein) |
| eq_commands_movement.cpp | 320 | RegisterMovementCommands (movement/combat/utility-debug/sky commands) |
| chat_component.cpp (in `src/client/chat/`) | 607 | K03: `eqt::chat::ChatComponent` — channel message send/process, `SendChatMessage`, `ProcessChatInput`, `DispatchSlashCommand` (registry 1 entry point), emote send/process, colored/formatted/simple text, and `ChatChannelName()` (T320), moved out of `class EverQuest` (was `eq_chat.cpp`) |
| group_component.cpp (in `src/client/group/`) | 960 | K04: `eqt::group::GroupComponent` — group invite/update/disband, raid (T169–T184: OP_RaidUpdate decode, raid senders), pet accessors/commands, moved out of `class EverQuest` (was `eq_group.cpp`) |
| guild_component.cpp (in `src/client/guild/`) | 276 | K01: `eqt::guild::GuildComponent` — the guild senders + processors, moved out of `class EverQuest` (was `eq_guild.cpp`; m_guild_id populated T133) |
| gm_component.cpp (in `src/client/gm/`) | 317 | K02: `eqt::gm::GmComponent` — the GM command senders + processors, moved out of `class EverQuest` (was `eq_gm.cpp`; wired T148) |
| eq_commands_train.cpp | 264 | T280: `/train` and `/trainclose` game-registry side |
| src/client/player/profile_appearance.cpp | 20 | PlayerProfile appearance fields stored so `/appearance` merges faithfully (T167); header `include/client/player/profile_appearance.h` |
| eq_action_handler_extended.cpp | 957 | J12: the `EqActionHandler` remainder — GM/debug, camp/logout, report, world-object and tradeskill actions |
| eq_action_handler_spells_trade.cpp | 570 | J12: `EqActionHandler`'s spell, pet, trade and `/appearance` actions (SendFaceChange's producer, T153) |
| eq_bridge_intents.cpp | 703 | J13: `ProcessBridgeIntents` — the `RendererIntent` variant switch |
| eq_bridge_ui_audio.cpp | 744 | J13: bridge-side UI and audio wiring, hotbar slot activation |
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
| chat | `chat/chat_component`, `eq_chat` (AddChat* only), `eq_strings` | 13 | 1 | **K03 DONE** (10 of 13 defs moved; the three AddChat* output primitives stay on `EverQuest`; `eq_strings` untouched, still a free-function candidate) |
| group | `group/group_component` | 23 | 5 | **K04 DONE** (group, raid and pet together; `DismissPet`/`UpdateGroupMemberFromEntity` deleted, zero callers) |
| merchant | `merchant/merchant`, `_trade`, `_vendor_bank` | 54 | 1 | **K05 DONE** (54 defs, not the 56 the batch table claimed; all 54 moved. `m_trainer_skill_caps` moved; the loot/vendor/scribe members stay on `EverQuest` pending K09/K11/K12) |
| entities | `entities/entities_component` (+ `_appearance`/`_updates`/`_world`) | 28 | 1 | **K06 DONE** (`m_entities`, the spawn-id -> Entity map, stays a core member — the component owns the handlers) |
| movement | `src/client/movement/movement_component`, `movement_state`, `movement_update` | 42 | 8 | **K07 DONE** (`eqt::movement::MovementComponent`; `HasReachedDestination` deleted, zero callers; `m_last_movement_update` boarded dead) |
| player | `src/client/player/player_component`, `player_misc`, `player_state`, `player_buffs`, `player_info`, `profile_appearance`; facade in `src/client/eq_player_info.cpp` | 139 | 11 | **K08 DONE** (139 defs, not the 142 the batch table claimed; 93 moved, the 46-body player-state facade stayed on `EverQuest`) |
| zone | `eq_zone`, `_dispatch`, `_entry`, `_handshake` | 41 | 17 | K09 |
| login_world | `eq_login_world` | 42 | 9 | K10 |
| bridge | `eq_bridge`, `_intents`, `_ui_audio` | 46 | 8 | K11 (becomes bridge/intent handlers, not a component) |
| commands | `eq_commands`, `_movement`, `_spells`, `_train` | 32 | 0 | K12 (becomes action handlers) |
| combat | `eq_combat_actions` | 6 | 1 | folds into whichever component owns attack |
| core | `eq.cpp` | 38 | 3 | stays — ctor/dtor, wiring, `Test_*` seams |
| action_handler | `eq_action_handler*` | 19 | 0 | K13 (retargeted last) |

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

`irrlicht_renderer.cpp` + 24 `renderer_*.cpp` TUs (J11 split the core).

| TU | Lines | Contents |
|---|---|---|
| renderer_frame.cpp | 1,752 | processFrame + input/visibility/simulation/render sub-passes |
| renderer_player_physics.cpp | 1,546 | updatePlayerMovement, collision setup, BSP-filtered raycasting, collision debug lines |
| renderer_diagnostics.cpp | 1,162 | dumpScene, profiling, frame timings, getMemoryReport, EQE1 usage report (T207/T214) |
| renderer_zone_draw.cpp | 1,155 | createZoneMesh, sorted/portal zone drawing, region meshes, VBO helpers |
| renderer_zone_load_env.cpp | 1,139 | loadZoneStep10_Sky, 11_Env, 12_Lights, 12b_Simulation, 13_Cleanup (split from renderer_zone_load.cpp, T166) |
| renderer_sim_bridge.cpp | 1,113 | SimulationWorker marshaling: buildSimulationZoneData, postSimulationInput, applySimulationOutput (split by lifetime contract, T116) |
| renderer_overlays.cpp | 1,042 | Map/navmesh overlays, target outline/selection, targeting/LOS, zone-line overlay, FPS |
| renderer_zone_load.cpp | 993 | loadZoneSequential driver + the remaining per-step methods with ZoneLoadContext (Batch G01; split T166) |
| src/client/graphics/irrlicht_renderer.cpp | 908 | Core after J11: ctor/dtor, run(), saveScreenshot, sky/door/world-object façades, player/vision state, createEntityRenderer, diagnostics setters |
| renderer_zone_stream.cpp | 873 | Lazy/background mesh load pipeline, progressive load, PVS neighborhood, deferred-entity pickup (T111/T210) |
| renderer_init.cpp | 757 | J11: init/shutdown, loading screen (initLoadingScreen, loadGlobalAssets, show/hideLoadingScreen) |
| renderer_zone_lifecycle.cpp | 739 | unloadZone (retains persistent EQE1 archives, T214), setZoneEnvironment, placeholder build/destroy, instant scene, zone-ready state |
| renderer_effects.cpp | 697 | Fire-glow icospheres, EQE1 archive loading |
| renderer_hud_env.cpp | 643 | J11: fog setup, loading-screen draw, environmental display settings, HUD setup/update, paperdoll |
| renderer_slash_commands.cpp | 592 | J11: `IrrlichtRenderer::processSlashCommand()` — the registry-3 if-chain |
| renderer_zone_load_objects.cpp | 578 | loadZoneStep6_Objects, 7_Doors, 9_Collision (split from renderer_zone_load.cpp, T166) |
| renderer_debug.cpp | 573 | Debug toggles/cycles, debug shader swaps |
| renderer_camera.cpp | 571 | J11: camera/projection per mode, first-person pitch clamp and vFOV, /fpcam dump, camera mode cycling, player position/vision/swimming state |
| renderer_zone_merge.cpp | 566 | ZoneMergeBuffers (build/destroy/renderMerged) + the texture-eviction guards: RegionMeshTextureGuard, EntityMeshTextureGuard, ObjectNodeTextureGuard (T123), DoorTextureGuard/boids (T124) |
| renderer_zone_load_entities.cpp | 551 | loadZoneStep8_Entities + 8a_BaseModelPrep, 8b_VariantModelPrep, 8c_VariantTextures, 8d_EquipmentPrep, 8e_DistributePrepResults, 8f_BuildEntityMesh (split from renderer_zone_load.cpp, T166) |
| renderer_entities.cpp | 502 | Entity/inventory/combat-anim façades (thin forwards) |
| renderer_gpu_upload.cpp | 446 | GPU upload thread init/drain/apply |
| renderer_event_receiver.cpp | 385 | J11: `RendererEventReceiver::OnEvent` and the key/mouse query surface |
| renderer_rdp.cpp | 230 | RDP server integration (WITH_RDP guard) |
| renderer_entity_load_gate.cpp | 68 | ensureEntityPrepWorkerStarted, deferEntityForLoadGate — the entity load distance gate (T109/T111) |

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
