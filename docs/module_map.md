# Module Map

Functional map of the oversized translation units, produced 2026-07-22 from
commit `a0c02cd`. **Line ranges refer to that commit** and drift as Batch F
splits land; cluster names and the Batch F unit column are the stable keys.
As each Batch F unit completes, its rows gain the new file path.

Sizes at time of analysis:

| File | Lines | Methods | Class members |
|---|---|---|---|
| src/client/eq.cpp | 19,676 | ~470 | 264 (eq.h) |
| src/client/graphics/irrlicht_renderer.cpp | 16,394 | ~250 | ~380 (header) |
| src/client/graphics/simulation_worker.cpp | 4,356 | 57 | — |
| src/client/graphics/entity_renderer.cpp | 4,329 | ~95 | — |

## Manager extraction patterns (for reference)

- **Callback injection (GOOD — the template):** `TradeManager` holds no
  `EverQuest*`; EverQuest wires ~12 `std::function` setters in
  `SetupTradeManagerCallbacks()`.
- **Friend + back-pointer (DO NOT replicate):** `CombatManager` (58 `m_eq->`
  back-calls), `SpellManager` (31). Declared `friend class` of EverQuest.

## eq.cpp clusters (line ranges @ a0c02cd)

Entanglement: CLEAN = little/no shared member state, safe mechanical lift.
HIGH = tied to core entity/movement/zone state; keep in core or split last.

| Cluster | Lines | ~LOC | Entangle | Batch F unit |
|---|---|---|---|---|
| Static lookup/name/string tables | 204–938 | ~730 | CLEAN | F02 |
| Ctor / lifecycle / snapshot publish | 939–1478 | ~540 | HIGH | core |
| Login connection (LoginOn/Send/Process) | 1479–1709 | ~230 | CLEAN-ish | F03 |
| World connection (WorldOn/Send/Process) | 1710–2155 | ~445 | Medium | F03 |
| Zone connection + ZoneOnPacketRecv dispatcher (~845-line switch @2215–3060) | 2156–3274 | ~1,120 | HIGH | core |
| NewZone + PlayerProfile (~409-line method) | 3275–3820 | ~545 | HIGH | core |
| Inventory/items | 3821–4026 | ~205 | Medium | F05 |
| Trade (callback-bridged already) | 4027–4457 | ~430 | CLEAN | F05 |
| Loot / Vendor / Bank / Trainer | 4458–5244 | ~790 | Medium | F05 |
| Zone spawns / world entities | 5245–6609 | ~1,365 | HIGH | F11 |
| Tribute / AA stubs | 6610–6679 | ~70 | CLEAN | F07 |
| Exp / Raid / ClientUpdate (~275-line method) | 6680–7154 | ~475 | HIGH | F11 |
| DeleteSpawn / MobHealth / HPUpdate | 7057–7287 | ~230 | HIGH | F11 |
| Chat (channel msgs, chat input) | 7288–7593 | ~305 | Medium | F06 |
| **RegisterCommands (single ~2,366-line method @7709–10075)** | 7594–10074 | ~2,480 | Isolated | **F01** |
| UCS chat-server connection | 10075–10313 | ~240 | DEAD CODE — entire cluster is inside a `/* */` block, superseded by the live `UCSConnection` in src/client/ucs.cpp (F04 finding) | none |
| High-level movement (Move/Follow/CombatMovement) | 10314–10627 | ~315 | HIGH | F08 |
| Player stat getters (~40 one-liners) | 10628–10828 | ~200 | CLEAN | F07 |
| GameState sync + group accessors | 10829–10977 | ~150 | Medium | F07 |
| Face/MoveTo/StopMovement/UpdateMovement (~447-line method) | 10978–11498 | ~520 | HIGH | F08 |
| SendPositionUpdate + geometry math helpers | 11499–11839 | ~340 | Mixed | F08 |
| Appearance (WearChange/Illusion) | 11840–12085 | ~245 | HIGH | F11 |
| MoveDoor | 12086–12149 | ~64 | Low | F11 |
| Tasks/DZ/Cast/Mana/Buffs | 12150–12431 | ~280 | Medium | F07 |
| Text messages (Colored/Formatted/Simple) | 12432–12666 | ~235 | Medium | F06 |
| PlayerState/Death/Stamina/Bind | 12667–12928 | ~260 | HIGH | core |
| Zone change / teardown / disconnect | 12929–13333 | ~405 | HIGH | F09 |
| Pathfinding (LoadPathfinder/FindPath/FollowPath) | 13334–13524 | ~190 | Medium | F08 |
| Zone geometry (LoadZoneMap/Lines, GetBestZ, FixZ) | 13525–13912 | ~390 | Medium | F09 |
| MovementMode / PositionState | 13913–14003 | ~90 | Medium | F08 |
| SpawnAppearance/Animation/Click*/WorldObject | 14004–14251 | ~250 | Medium | F11 |
| ApplyPoison / Track | 14252–14338 | ~90 | Low | F07 |
| Group / Raid / Pet | 14339–14937 | ~600 | HIGH | F12 |
| Jump / MovementHistory / UpdateLoop | 14938–15238 | ~300 | HIGH | F08 |
| Water state | 15239–15394 | ~155 | Medium | F08 |
| Emote/AFK/Anon/Camp/Logout | 15395–15553 | ~160 | Low | F07 |
| Rezz / WhoAll / Inspect | 15554–15800 | ~250 | Medium | F07 |
| Guild (self-contained domain) | 15801–16026 | ~225 | CLEAN-ish | F04 |
| Corpse / Consent | 16027–16165 | ~140 | Low | F07 |
| Assist/Boat/Split/LFG/EnvDamage/Discipline | 16166–16449 | ~285 | Low | F07 |
| Save/Popup/Duel/BindWound | 16450–16633 | ~185 | Low | F07 |
| Recipes / tradeskill search | 16634–16739 | ~105 | CLEAN | F05 |
| Surname/Random/FindPerson/social misc | 16740–16899 | ~155 | Low | F07 |
| CameraEffect / PlayMP3 / Sound | 16900–16976 | ~75 | Low | F07 |
| GM commands | 16977–17169 | ~190 | CLEAN | F04 |
| Combat resolution (Consider/Action/Damage/Loot) | 17217–17710 | ~495 | HIGH | F13 |
| QueuePacket + keyboard movement | 17711–17923 | ~215 | HIGH | F08 |
| Graphics bridge + init (InitGraphics, GameTick, **ProcessBridgeIntents ~668 lines @18530–19198**, ProcessBridgeEvents) | 17924–19676 | ~1,750 | HIGH, seam-able | F10 |

## irrlicht_renderer.cpp clusters (line ranges @ a0c02cd)

All clusters share scene handles (`device_`, `driver_`, `smgr_`, `camera_`);
existing sub-objects (entityRenderer_, simulationWorker_, doorManager_,
skyRenderer_, detailManager_, cameraController_, gpuUploadThread_) are the
proven seams.

| Cluster | Lines | ~LOC | Entangle | Batch F unit |
|---|---|---|---|---|
| Init / loading screen / slash-cmd / shutdown | 142–1336 | ~1,200 | Medium | core |
| GPU upload thread | 1337–1756 | ~420 | CLEAN | F21 |
| Camera / lighting / time-of-day / fog / HUD | 1782–2490 | ~700 | Medium | core |
| Zone load/unload/env (**loadZoneSequential ~2,613 lines @3208–5821**) | 2491–5821 | ~3,330 | VERY HIGH | **F17** |
| Zone mesh / region / portal rendering | 5822–6624 | ~800 | HIGH | F18 |
| Lazy/background mesh load pipeline | 6625–7329 | ~700 | HIGH | F18 |
| Entity façade (thin forwards to entityRenderer_) | 7330–7813 | ~480 | CLEAN | F21 |
| Player/vision/camera state | 7752–8011 | ~260 | Medium | core |
| Simulation bridge (postSimulationInput / applySimulationOutput) | 8012–9054 | ~1,040 | HIGH | F20 |
| dumpScene / profiling / measureSection | 9055–9341 | ~285 | CLEAN | F14 |
| processFrame + sub-passes | 9342–10436 | ~1,100 | HIGH | F22 |
| run / screenshot / ~30 debug toggles | 10901–11533 | ~630 | CLEAN | F15 |
| Frame-timing profiling | 11533–11834 | ~300 | CLEAN | F14 |
| Player movement/collision (**updatePlayerMovement ~828 lines @11866–12694**) | 11835–12991 | ~1,160 | HIGH | F19 |
| Progressive load / PVS | 12992–13446 | ~455 | HIGH | F18 |
| Collision raycasting subsystem | 13447–14053 | ~600 | Medium | F19 |
| Overlays (map, navmesh, target outline, FPS) | 14054–15295 | ~1,240 | CLEAN-ish | F15 |
| Targeting (mouse picking, LOS) | 14557–14780 | ~225 | Medium | F15 |
| Inventory/character-info façade | 14781–14965 | ~185 | CLEAN | F21 |
| Combat-anim façade | 15296–15364 | ~70 | CLEAN | F21 |
| getMemoryReport | 15365–15725 | ~360 | CLEAN | F14 |
| RDP remote desktop | 15726–15936 | ~210 | CLEAN | F14 |
| Fire-glow icosphere effects | 15937–16265 | ~330 | CLEAN | F16 |
| EQE1 archive loading | 16266–16394 | ~130 | CLEAN | F16 |

## simulation_worker.cpp (cleanest split — already struct-isolated)

Communicates only via SimulationInput/SimulationOutput. Each compute pass is
independently extractable: regions/visibility/portals (~700), lighting/fire
(~250), tree/vertex/detail animation (~340), **particles (computeParticles
~680 lines @1699–2378)**, entity sync/interp (~495), boids (~455),
tumbleweeds (~275), weather (~220), spell VFX (~290). → F23.

## entity_renderer.cpp

Entangled core: mesh building (buildEntityMesh ~400 lines,
processOneEntityBuildStep ~470) and update/interp (~925). Clean lifts:
casting bars (~230), combat-anim controller (~395), spatial grid + BSP
occlusion (~130), equipment (~310), boats (~60), con-color/lookup setup
(~270). → F24.
