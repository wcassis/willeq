# Module Map

Post-Batch-F layout (2026-07-23). The four former mega-files are split into
domain translation units; classes are unchanged — only method definitions
moved. Line counts as of Batch F completion; they drift with normal work.
The pre-split cluster analysis this map replaced is preserved in git history
(docs/module_map.md @ commit a0c02cd..93a3c2a era) and in each unit's review
section under `refactor/F*.md`.

## Game state — EverQuest class (`include/client/eq.h`)

| TU | Lines | Contents |
|---|---|---|
| src/client/eq.cpp | 3,629 | Core: ctor/lifecycle, PublishFullStateSnapshot, ZoneOnPacketRecv dispatcher (~845-line switch), NewZone/PlayerProfile, zone connection phases, PlayerState/Death/Stamina/Bind, ZoneProcessEmote/GuildMOTD/LevelUpdate, SafeQueueZonePacket, QueuePacket, Start/StopUpdateLoop, SetEQClientPath/SetConstrainedConfig |
| eq_commands.cpp | 1,110 | RegisterCommands driver + RegisterSocialCommands (chat/group/trade/guild/social), runPmemDiagnostics, runLoadDiagnostics |
| eq_commands_movement.cpp | 900 | RegisterMovementCommands (movement/combat/utility-debug/sky commands) |
| eq_commands_spells.cpp | 783 | RegisterSpellCommands (spell/skill/hotkey/detail/audio commands) |
| eq_entities.cpp | 2,438 | Zone spawns, NewSpawn/DeleteSpawn, ClientUpdate interp, HP/MobHealth, appearance/WearChange/Illusion, doors, world objects, FindEntityByName |
| eq_movement.cpp | 1,943 | Move/Follow/UpdateMovement, SendPositionUpdate, pathfinding, water state, jump, keyboard movement |
| eq_bridge.cpp | 1,837 | InitGraphics/ShutdownGraphics, GameTick, Pre/PostRenderTick, ProcessBridgeIntents/Events, zone-load snapshot, loot-window flow, hotbar config, audio init |
| eq_merchant.cpp | 1,558 | Inventory/items, trade, loot/vendor/bank/trainer callbacks, recipes |
| eq_player_info.cpp | 2,006 | Stat getters, group accessors, tribute/AA, buffs/casts, emote/AFK/camp, rezz/who/inspect, corpse/consent, duels, social misc, camera/sound handlers |
| eq_zone.cpp | 801 | Zone change/teardown, CleanupZone, DisconnectFromZone, LoadZoneMap/Lines/Sources, CheckZoneLine, GetBestZ/FixZ |
| eq_strings.cpp | 746 | Name/string lookup tables, opcode names, dump helpers |
| eq_login_world.cpp | 696 | Login-server + world-server connection handling, eqcrypt_block |
| eq_group.cpp | 623 | Group invite/update/disband, pet accessors/commands |
| eq_chat.cpp | 538 | Channel messages, chat input, AddChat* family, colored/formatted/simple text |
| eq_combat_actions.cpp | 529 | Consider/Action/Damage/MoneyOnCorpse/LootItem handlers |
| eq_guild.cpp | 232 | Guild senders + processors |
| eq_gm.cpp | 200 | GM command senders + processors |

## Renderer — IrrlichtRenderer class (`include/client/graphics/irrlicht_renderer.h`)

| TU | Lines | Contents |
|---|---|---|
| src/client/graphics/irrlicht_renderer.cpp | 2,736 | Core: init/shutdown, loading screen, slash-command processing, camera/lighting/time-of-day, run(), saveScreenshot, sky façade, door/world-object façades, player/vision state, createEntityRenderer, diagnostics setters |
| renderer_zone_load.cpp | 2,795 | loadZoneSequential driver + 20 per-step methods (loadZoneStep0..13, 8a–8f) with ZoneLoadContext (Batch G01) |
| renderer_frame.cpp | 1,583 | processFrame + input/visibility/simulation/render sub-passes |
| renderer_player_physics.cpp | 1,570 | updatePlayerMovement, collision setup, BSP-filtered raycasting, collision debug lines |
| renderer_zone_stream.cpp | 1,363 | Lazy/background mesh load pipeline, progressive load, PVS neighborhood |
| renderer_overlays.cpp | 1,064 | Map/navmesh overlays, target outline/selection, targeting/LOS, zone-line overlay, FPS |
| renderer_sim_bridge.cpp | 1,063 | SimulationWorker marshaling: buildSimulationZoneData, postSimulationInput, applySimulationOutput |
| renderer_diagnostics.cpp | 972 | dumpScene, profiling, frame timings, getMemoryReport |
| renderer_zone_draw.cpp | 835 | createZoneMesh, sorted/portal zone drawing, region meshes, VBO helpers |
| renderer_zone_lifecycle.cpp | 705 | unloadZone, setZoneEnvironment, placeholder build/destroy, instant scene, zone-ready state |
| renderer_debug.cpp | 531 | Debug toggles/cycles, debug shader swaps |
| renderer_effects.cpp | 500 | Fire-glow icospheres, EQE1 archive loading |
| renderer_entities.cpp | 464 | Entity/inventory/combat-anim façades (thin forwards) |
| renderer_gpu_upload.cpp | 455 | GPU upload thread init/drain/apply |
| renderer_rdp.cpp | 214 | RDP server integration (WITH_RDP guard) |

## Simulation worker — SimulationWorker class

| TU | Lines | Contents |
|---|---|---|
| simulation_worker.cpp | 474 | Lifecycle core: ctor, start/stop, data intake, workerLoop, computeAll dispatch |
| sim_particles.cpp | 1,168 | Particle system + spell VFX |
| sim_boids.cpp | 743 | Boids + tumbleweeds |
| sim_visibility.cpp | 714 | Regions, PVS, portals, occlusion, light selection, frustum helper |
| sim_animation.cpp | 538 | Fire flicker, tree/vertex/detail animation, light animation |
| sim_entity_sync.cpp | 510 | Entity sync/interp/visibility, name tags |
| sim_weather.cpp | 305 | Weather system, sky state |

## Entity renderer — EntityRenderer class

| TU | Lines | Contents |
|---|---|---|
| entity_renderer.cpp | 3,201 | Core: registration/mesh building, update/interp, remove/decay, first-person/player, animation setters |
| entity_combat_anim.cpp | 437 | Combat animation queues + animation queries |
| entity_equipment.cpp | 332 | Equipment models, attachment, bone transforms |
| entity_castbars.cpp | 264 | Casting bar state + rendering |
| entity_spatial.cpp | 218 | Spatial grid, BSP occlusion cache, boats |

## Conventions

- New extractions follow the patterns established in Batch F unit reviews
  (`refactor/F14`–`F24`): TUs re-open `namespace EQT { namespace Graphics }`,
  add full includes for types the class header only forward-declares, and
  file-local static helpers live with their sole callers.
- File-size ceilings: warn >1,500, fail >3,000
  (`scripts/refactor/check_file_sizes.sh`; policy in docs/decisions.md).
- Behavior-preserving moves are verified with
  `scripts/refactor/verify_move_only.py BASE..HEAD`.
