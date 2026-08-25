# Module Map

Post-Batch-F layout (2026-07-23), refreshed by T215 (2026-08-25, fable
`e70e3347`). The former mega-files are split into domain translation
units; classes are unchanged — only method definitions moved. Line counts
are `wc -l` at `e70e3347`; they drift with normal work. Ceilings: warn
>1,500, fail >3,000 (`scripts/refactor/check_file_sizes.sh`); the checker
gates only files a unit changes, so `irrlicht_renderer.cpp` (3,054) and
`entity_renderer.cpp` (3,341) sit above the fail line grandfathered — the
next unit that grows either must split it (T166 did this for
`renderer_zone_load.cpp`; T115 trimmed a comment to hold a file at 3,000).
The pre-split cluster analysis this map replaced is
preserved in git history (docs/module_map.md @ commit a0c02cd..93a3c2a era)
and in each unit's review section under `refactor/F*.md`.

## Game state — EverQuest class (`include/client/eq.h`)

`src/client/eq.cpp` + 18 `eq_*.cpp` domain TUs.

| TU | Lines | Contents |
|---|---|---|
| src/client/eq.cpp | 3,934 | Core: ctor/lifecycle, PublishFullStateSnapshot, ZoneOnPacketRecv dispatcher, NewZone/PlayerProfile, zone connection phases, PlayerState/Death/Stamina/Bind, ZoneProcessEmote/GuildMOTD/LevelUpdate, SafeQueueZonePacket, QueuePacket, Start/StopUpdateLoop, SetEQClientPath/SetConstrainedConfig |
| eq_entities.cpp | 2,641 | Zone spawns, NewSpawn/DeleteSpawn, ClientUpdate interp, HP/MobHealth, appearance/WearChange/Illusion, doors, world objects, FindEntityByName, corpse naming (T160/T185) |
| eq_action_handler.cpp | 2,205 | `action::EqActionHandler` — the `IActionHandler` implementation wrapping `EverQuest` for `ActionDispatcher`/`CommandProcessor` (action-layer migration T99–T143; raid guards T179–T184; skill gate T159/T162) |
| eq_player_info.cpp | 2,104 | Stat getters, group accessors, tribute/AA, buffs/casts, emote/AFK/camp, rezz/who/inspect (+ OP_InspectAnswer reply, T170), corpse/consent, duels, social misc, camera/sound handlers |
| eq_movement.cpp | 2,031 | Move/Follow/UpdateMovement, SendPositionUpdate, pathfinding, water state, jump, keyboard movement |
| eq_bridge.cpp | 2,001 | InitGraphics/ShutdownGraphics, GameTick, Pre/PostRenderTick, ProcessBridgeIntents/Events, zone-load snapshot, loot-window flow, hotbar config, audio init |
| eq_merchant.cpp | 1,667 | Inventory/items, trade, loot/vendor/bank/trainer callbacks, recipes (tradeskill senders T149) |
| eq_login_world.cpp | 923 | Login-server + world-server connection handling, eqcrypt_block |
| eq_zone.cpp | 884 | Zone change/teardown, CleanupZone, DisconnectFromZone, LoadZoneMap/Lines/Sources, CheckZoneLine, GetBestZ/FixZ |
| eq_commands.cpp | 793 | RegisterCommands driver + the game-registry remainder after T139 deleted 34 redundant commands (registry 1 of `docs/decisions.md` 2026-08-03) |
| eq_strings.cpp | 745 | Name/string lookup tables, opcode names, dump helpers |
| eq_group.cpp | 595 | Group invite/update/disband, raid (T169–T184: OP_RaidUpdate decode, raid senders), pet accessors/commands |
| eq_chat.cpp | 533 | Channel messages, chat input, AddChat* family, colored/formatted/simple text |
| eq_combat_actions.cpp | 530 | Consider/Action/Damage/MoneyOnCorpse/LootItem handlers, Bind_Wound reply (T177) |
| eq_commands_spells.cpp | 496 | RegisterSpellCommands (spell/skill/hotkey/detail/audio commands) |
| eq_commands_movement.cpp | 320 | RegisterMovementCommands (movement/combat/utility-debug/sky commands) |
| eq_guild.cpp | 257 | Guild senders + processors (m_guild_id populated T133) |
| eq_gm.cpp | 231 | GM command senders + processors (wired T148; three open defects, see `refactor/deferred.md`) |
| eq_profile_appearance.cpp | 20 | PlayerProfile appearance fields stored so `/appearance` merges faithfully (T167) |

## Renderer — IrrlichtRenderer class (`include/client/graphics/irrlicht_renderer.h`)

| TU | Lines | Contents |
|---|---|---|
| src/client/graphics/irrlicht_renderer.cpp | 3,054 | Core: init/shutdown, loading screen, slash-command processing (registry 3 if-chain), camera/lighting/time-of-day, run(), saveScreenshot, sky façade, door/world-object façades, player/vision state, createEntityRenderer, diagnostics setters |
| renderer_frame.cpp | 1,649 | processFrame + input/visibility/simulation/render sub-passes |
| renderer_player_physics.cpp | 1,558 | updatePlayerMovement, collision setup, BSP-filtered raycasting, collision debug lines |
| renderer_diagnostics.cpp | 1,162 | dumpScene, profiling, frame timings, getMemoryReport, EQE1 usage report (T207/T214) |
| renderer_zone_draw.cpp | 1,155 | createZoneMesh, sorted/portal zone drawing, region meshes, VBO helpers |
| renderer_zone_load_env.cpp | 1,139 | loadZoneStep10_Sky, 11_Env, 12_Lights, 12b_Simulation, 13_Cleanup (split from renderer_zone_load.cpp, T166) |
| renderer_sim_bridge.cpp | 1,110 | SimulationWorker marshaling: buildSimulationZoneData, postSimulationInput, applySimulationOutput (split by lifetime contract, T116) |
| renderer_overlays.cpp | 1,064 | Map/navmesh overlays, target outline/selection, targeting/LOS, zone-line overlay, FPS |
| renderer_zone_load.cpp | 1,001 | loadZoneSequential driver + the remaining per-step methods with ZoneLoadContext (Batch G01; split T166) |
| renderer_zone_stream.cpp | 968 | Lazy/background mesh load pipeline, progressive load, PVS neighborhood, deferred-entity pickup (T111/T210) |
| renderer_zone_lifecycle.cpp | 734 | unloadZone (retains persistent EQE1 archives, T214), setZoneEnvironment, placeholder build/destroy, instant scene, zone-ready state |
| renderer_effects.cpp | 604 | Fire-glow icospheres, EQE1 archive loading |
| renderer_zone_load_objects.cpp | 578 | loadZoneStep6_Objects, 7_Doors, 9_Collision (split from renderer_zone_load.cpp, T166) |
| renderer_debug.cpp | 573 | Debug toggles/cycles, debug shader swaps |
| renderer_zone_merge.cpp | 557 | ZoneMergeBuffers (build/destroy/renderMerged) + the texture-eviction guards: RegionMeshTextureGuard, EntityMeshTextureGuard, ObjectNodeTextureGuard (T123), DoorTextureGuard/boids (T124) |
| renderer_zone_load_entities.cpp | 551 | loadZoneStep8_Entities + 8a_BaseModelPrep, 8b_VariantModelPrep, 8c_VariantTextures, 8d_EquipmentPrep, 8e_DistributePrepResults, 8f_BuildEntityMesh (split from renderer_zone_load.cpp, T166) |
| renderer_entities.cpp | 478 | Entity/inventory/combat-anim façades (thin forwards) |
| renderer_gpu_upload.cpp | 446 | GPU upload thread init/drain/apply |
| renderer_rdp.cpp | 214 | RDP server integration (WITH_RDP guard) |
| renderer_entity_load_gate.cpp | 68 | ensureEntityPrepWorkerStarted, deferEntityForLoadGate — the entity load distance gate (T109/T111) |

## Simulation worker — SimulationWorker class

| TU | Lines | Contents |
|---|---|---|
| sim_particles.cpp | 1,168 | Particle system + spell VFX |
| sim_boids.cpp | 743 | Boids + tumbleweeds |
| sim_visibility.cpp | 714 | Regions, PVS, portals, occlusion, light selection, frustum helper |
| simulation_worker.cpp | 561 | Lifecycle core: ctor, start/stop, data intake, workerLoop, computeAll dispatch |
| sim_animation.cpp | 538 | Fire flicker, tree/vertex/detail animation, light animation |
| sim_entity_sync.cpp | 510 | Entity sync/interp/visibility, name tags |
| sim_weather.cpp | 305 | Weather system, sky state |

## Entity renderer — EntityRenderer class

| TU | Lines | Contents |
|---|---|---|
| entity_renderer.cpp | 3,341 | Core: registration/mesh building, update/interp, remove/decay, first-person/player, animation setters |
| entity_prep_worker.cpp | 476 | EntityPrepWorker: background entity prep (start/stop, requestPrep, priority sort, prepVariantTextures, prepEquipmentModels) |
| entity_combat_anim.cpp | 437 | Combat animation queues + animation queries |
| entity_equipment.cpp | 269 | Equipment models, attachment, bone transforms |
| entity_castbars.cpp | 264 | Casting bar state + rendering |
| entity_spatial.cpp | 218 | Spatial grid, BSP occlusion cache, boats |

## Conventions

- New extractions follow the patterns established in Batch F unit reviews
  (`refactor/F14`–`F24`) and T166 (`refactor/T166_split_renderer_zone_load.md`):
  TUs re-open `namespace EQT { namespace Graphics }`, add full includes for
  types the class header only forward-declares, and file-local static
  helpers live with their sole callers.
- File-size ceilings: warn >1,500, fail >3,000
  (`scripts/refactor/check_file_sizes.sh`; policy in docs/decisions.md).
- Behavior-preserving moves are verified with
  `scripts/refactor/verify_move_only.py BASE..HEAD`.
