# BOARD — the open-work queue of record

- **Tree confirmed at:** `3808ebe9` (branch `task/K00-legA-board`, base `fable`).
- **Date:** 2026-09-03. Built by K00 Leg A (`refactor/K00_truth_reset.md`).
- **Suite:** 3497 of 3498 passing, per `refactor/J16_eq_header_type_extraction.md`'s
  Review (the last merged code unit). The sole expected failure is the ALSA host
  quirk `AudioManagerTest.PreloadCommonSounds`. The count grows; run it.
- **Items are confirmed against the tree at the tip above; nothing here is quoted
  from an older board.**

**Classes.** **A** live defect — a reachable path gives the wrong answer, silently
does nothing, or looks wrong. **B** implemented-but-unreached, or hygiene that
needs a code change. **C** needs Will's design decision. **P** process/doc.

**Rules.** One entry per open item. Anchors are file + function/class name, never
a line number. `Prov:` is the item number in the archived board
(`refactor/completed/deferred_archive_20260903.md`) and appears nowhere else. An
item marked *unconfirmed* names what would confirm it; nobody may scope a fix off
it without taking that measurement first.

---

## A — live defects

- **B001** (A) `src/client/movement/movement_state.cpp`
  `eqt::movement::MovementComponent::SetPositionState` (K07 moved it) —
  every arm ends in `SendSpawnAppearance`, so a server-forced pose cannot be
  applied without echoing it back, and its `AT_DIE` arm encodes a pose self never
  receives. Closes when: a non-sending internal setter exists and the death path
  (`OP_Death`/`OP_BecomeCorpse` for self), not the `AT_DIE` arm, drives self's
  `positionState`. Prov: deferred item 15.

- **B002** (A) `src/client/action/eq_action_handler.cpp` `EqActionHandler::toggleAutorun`
  — a `LOG_DEBUG` stub, and the only live consumer
  (`renderer_player_physics.cpp`'s `playerMovement_.autorun`) has zero writers, so
  three bound keys report success and do nothing. Closes when: a renderer-side
  setter is driven from the game side and a live keypress moves the player.
  Prov: deferred item 10.

- **B003** (A) auto-attack — 12 live runs with `/target` and `/attack` both
  confirmed in the log produced no swing. **Unconfirmed at this tip, needs a live
  eqemu session**; it may be the draw predicate, the animation, the attack timer
  or the test setup. Closes when: a live run shows a swing, or the defect is
  named. Prov: deferred item 27.

- **B004** (A) `include/client/graphics/ui/ui_layout.h` `centerPopup` — `popW`/
  `popH` are hardcoded 400/350 at every resolution; the archive records general
  and equipment slots drawn outside that frame and unreachable by mouse.
  **Unconfirmed at this tip, needs a rendered inventory popup or a static-panel
  hit-test.** Closes when: every drawn slot is inside the frame and clickable.
  Prov: archived board, "static-panel UI surface still untested after T54".

## B — implemented-but-unreached, and hygiene needing a code change

- ~~**B005** (B) the `UCSConnection` TU and header (under `src/client/` and
  `include/client/`, basename `ucs`) — 413 lines of live-looking code with zero
  callers.~~ **CLOSED by K15**: re-measured at `7c125b28` (comment- and
  string-filtered grep + the compiler) — zero references outside its own two
  files. Both files, the `CMakeLists.txt` entry and the commented
  `ConnectToUCS` call are deleted, with tombstones in `CMakeLists.txt` and at
  the call site in
  `LoginWorldComponent::WorldProcessSetChatServer`. The parsed `m_ucs_host` /
  `m_ucs_port` / `m_ucs_connection_type` were **kept** — the plan asked for them
  but they are NOT zero-reference: they carry the T13 wire contract and 13
  assertions in `test_login_world_flow.cpp` / `test_login_world_component.cpp`
  read them through public accessors. Prov: deferred item 4.

- ~~**B006** (B) the S2 leftovers~~ **CLOSED by K16** (the K15 halves below stand as the record).
  ~~`application_config.cpp`'s `irrlicht_renderer.h` include~~ **deleted** —
  re-measured at `7c125b28` and confirmed by the compiler. **The survey's claim
  was FALSE for `application_frame.cpp`**: it names no `IrrlichtRenderer` token,
  but it drives the renderer through `m_renderer->getDriver()`,
  `->processFrame()` and `->saveScreenshot()`, so removing the include gave
  three "invalid use of incomplete type" errors. The include is kept and this is
  the record — a name-token grep does not see a use through a typed member
  pointer; only the compiler does.
  ~~`door_manager.cpp`'s static `EverQuest::GetDebugLevel()`~~
  **replaced**: the gate is now the logging module's own level query
  (`ShouldLog` behind `LOG_DEBUG`/`LOG_TRACE` — user-initiated at DEBUG, every
  other door at TRACE) and the `include/client/eq.h` include is gone from that
  renderer TU. ~~(1) `EverQuest::InitGraphics` still takes an
  `IrrlichtRenderer*` — K15 read it and hit the plan's STOP condition: inside
  `BridgeComponent::InitGraphics` the pointer reaches exactly two calls,
  `initLoadingScreen(const RendererConfig&)` and `initDeferredGraphics()`, and
  narrowing needs an interface that DOES NOT EXIST.~~ **DONE — K16**: Will
  approved the two-method interface on 2026-09-03 and K16 built it.
  `include/client/graphics/zone_load_surface.h` declares
  `EQT::Graphics::IZoneLoadSurface` (virtual dtor,
  `initLoadingScreen(const RendererConfig&)`, `initDeferredGraphics()`);
  `IrrlichtRenderer` implements it (its only base class); both `InitGraphics`
  signatures take it, and the `IrrlichtRenderer` forward declaration LEFT
  `include/client/eq.h` and `include/client/bridge/bridge_component.h` — the
  compiler confirms nothing else in either header needed the name. Pinned by
  `tests/test_zone_load_surface.cpp`, which drives the real InitGraphics body
  with a stub implementing only those two methods.
  ~~(2) the asset loaders
  (`include/client/graphics/eq/pfs.h`, `include/client/graphics/eq/wld_loader.h`)
  still live in `EQT::Graphics`~~ — **NAMING ONLY, LEFT DELIBERATELY.** They are
  graphics-side loaders reached from graphics-side code; the namespace is a
  label, not a dependency, and moving it renames every use site for no
  behavioural or structural gain. Not reopened without a reason beyond the name.
  Prov: the S2 residue.

- **B007** (B) `include/client/skill/skill_data.h` `SkillData::endurance_cost` /
  `mana_cost` — declared, defaulted 0, and assigned nowhere in the tree, so both
  of `SkillManager::getSkillUseError()`'s resource refusals are unreachable and a
  skill that should fail for endurance proceeds. Closes when: the source data is
  found and loaded, or the fields and their branches are deleted. Do not invent
  values. Prov: deferred item 35.

- **B008** (B) `src/common/net/daybreak_connection_send.cpp`
  `DaybreakConnection::ValidateCRC` — its `default:` arm returns false with no log
  line, silently rejecting every packet for any `crc_bytes` other than 2 or 4.
  Closes when: the arm logs, or the case is proven unreachable and asserted.
  Prov: deferred item 41.

- **B009** (B) `tests/test_daybreak_connection_pcap.cpp` — reads `connect_code`,
  `encode_key` and `max_packet_size` raw off the wire, which is exactly the bug
  T21 fixed in production; it cannot fail if that fix regresses, and passes today
  only because eqemu's key is a byte-swap palindrome. Closes when: the test reads
  through the production path. Prov: deferred item 40.

- **B010** (B) `tests/test_irrlicht_camera_command.cpp`
  `CameraCommandRoundTripTest` — no `PrintTo` for the parameter type, so gtest
  names each case by hex-dumping an ASLR-randomised pointer and the eight ctest
  names are not stable identifiers. Closes when: a `PrintTo` or name generator
  gives stable names. Prov: deferred item 42.

- **B011** (B) `src/client/graphics/entity_size.cpp`
  `EntityRenderer::setEntitySize` — updates neither `collisionHeight` nor `deckZ`,
  so a resized boat's deck surface goes stale. Nothing in the game resizes a
  boat, so this is a documented limit, not a live defect. Closes when: both are
  recomputed, or the limit is asserted in a test. Prov: deferred item 38.

- **B012** (B) `src/client/action/action_dispatcher.cpp`
  `ActionDispatcher::clearTarget` — reachable only from a keybinding; no command
  registration exists in `command_processor_registration.cpp`, so a `--script`
  run cannot clear a target. Closes when: a command reaches it or it is proven
  keybinding-only by design. Prov: archived board, T39 dead-state follow-ups.

- **B013** (B) `src/client/graphics/slash/renderer_slash_commands.cpp` `/newskills` —
  the only working skills-window toggle, registered in neither command registry,
  so it is invisible to `/help` and to tab completion and reachable only by a user
  who already knows the word. Closes when: it is registered, or renamed and
  merged with `/skills`. Prov: archived board, T143.

- **B014** (B) `src/client/graphics/eq/race_model_loader.cpp`
  `RaceModelLoader::preloadModelData` — calls
  `ConstrainedTextureCache::getOrLoad()` with an empty buffer whenever a JSON
  manifest exists but no EQE1 archive is configured: one guaranteed-to-fail call
  per composite name. Closes when: the call is gated on EQE1 being configured.
  Prov: archived board, T92.

- **B015** (B) `src/client/audio/midi_player.cpp` `MidiPlayer::loadSoundFont` —
  swaps a raw `tsf*` with no atomic or mutex; safe today only because its single
  call site runs before the audio thread exists, which the file's own comment
  records. Closes when: the swap is synchronised or the single-call-site
  precondition is asserted. Prov: archived board, T131.

- **B016** (B) `src/client/graphics/eq/` `RaceModelLoader::ensureModelDataLoaded`
  — ~20 unlocked `loadedModels_` reads across four TUs; a blanket lock
  self-deadlocks, so it needs a recursive mutex or a restructure.
  **Unconfirmed at this tip, needs a TSAN run** to show the warnings are still
  raised. Prov: archived board, T114.

- ~~**B019** (B) `include/client/eq.h` `EverQuest::m_guild_name` — declared and
  default-initialised, **zero references anywhere**.~~ **CLOSED by K15**:
  re-measured at `7c125b28`, still zero references; deleted. Prov: K01 Review.

## C — needs Will's decision

- **B017** (C) `src/client/guild/guild_component.cpp`
  `eqt::guild::GuildComponent::SendGuildInviteAccept` —
  prints "You have declined the guild invite." on a path the server processes as
  an accept. T318 confirmed the decline is **unfixable client-side** (the server
  tests an already-remapped value). The open half is user-facing wording, and no
  copy should be invented. Prov: deferred item 37.

- **B018** (C) first-person eye placement — `refactor/T301_first_person_eye_placement.md`
  is written and not dispatched; T309 landed the measured draw rule
  (`src/client/graphics/entity_first_person_draw.cpp`) but the eye sits at the
  static bbox crown, and Will judges the look on the live gate. Prov: deferred
  item 16.

- **B040** (C) server-sent placeables and doors are measured by no gate — the
  benchmark's `placeableObjectsBytes` (`tools/hwcaps/zone_bench_memory.h`) is
  summed over the zone's own S3D archive and never connects to a server, so
  eqemu's `object` and `doors` tables are counted by nothing. Closing it needs a
  live-client memory harness that does not exist. **WILL RULED 2026-09-03:
  leave open, LOW PRIORITY** — performance measurement is secondary to the
  structural work. **(K15: renumbered from a duplicate `B019` — the ID collided with
  the `m_guild_name` item above. The one cross-reference, in
  `refactor/K00_truth_reset.md`, was fixed with it.)** Prov: the archived board's
  "SERVER-SENT PLACEABLES AND DOORS ARE UNMEASURED BY ANY GATE" section.

- **B020** (C) `include/client/graphics/constrained_renderer_config.h` — eight
  measured memory categories (zoneGeometry, s3dZoneSource, gpuTexture, vboEbo,
  eqe1Mapped, characterModels, placeableObjects, untracked) carry no configured
  budget, only baseline guards. The per-category ruling is owed. Prov: archived
  board, T69.

- **B021** (C) `scripts/ci/visual_gate.py` `untrackedBytes` — emitted and
  baselined but deliberately not asserted; gating it needs repeat-run data from a
  second machine first. Prov: archived board, T69.

- **B022** (C) `src/client/graphics/spell_visual_fx.cpp` — the data-driven spell
  visual layer is still entirely ahead: nothing consumes the `spells.eff` reader
  in place of the hardcoded resist/school vocabulary, and the spells whose
  `spell_affect_index` hits an empty padding record need a rendering ruling.
  Prov: archived board, T70.

- **B023** (C) `src/client/player/player_misc.cpp` `eqt::player::PlayerComponent::SendReport` — the wire
  shape does not match eqemu's `OP_Report` (pipe-delimited reported/reporter/
  message versus a 4-byte category plus raw text), so `AddReport()` is never
  reached. Fixing it is a command-shape decision, not a mechanical patch. Prov:
  archived board, T146.

## P — process and documentation

- **B024** (P) Component extraction of `class EverQuest` (2,025-line header, 685
  member functions, 178 data members, 37 flat `src/client/eq_*.cpp` TUs) and of
  `class IrrlichtRenderer` is the open structural programme —
  `refactor/K_component_extraction.md`, K01..K13, then Batch L. Both headers hold
  a no-grow allowlist entry that each K unit ratchets down. Prov: the J tracker's
  "WILL'S RULING", 2026-09-02.

- **B025** (P) Eight methods dominate their TUs and were left out of Batch J
  deliberately: `eqt::zone::ZoneComponent::DispatchZonePacket` 880
  (`src/client/zone/zone_dispatch.cpp`, K09),
  `RaceModelLoader::loadModelFromGlobalChrWithVariantsForAnimation` 815
  (`variant_loading.cpp`), `IrrlichtRenderer::updatePlayerMovement` 806
  (`renderer_player_physics.cpp`), `CommandProcessor::registerBuiltinCommands` 768
  (`command_processor_registration.cpp`), `eqt::bridge::BridgeComponent::ProcessBridgeIntents` 580
  (`src/client/bridge/bridge_component_intents.cpp`), `Application::initialize` 569 (`application.cpp`),
  `DetailManager::createDefaultConfig` 548 (`detail_manager.cpp`, a data table —
  leave it), `IrrlichtRenderer::processFrameRender` 546 (`renderer_frame.cpp`).
  Closes when: each is split or explicitly kept. Prov:
  `refactor/J_file_size_reclamation.md`, "Optional, NOT part of this batch".

- **B026** (P) Two unmerged branches have never been audited —
  `task/eqcpp-zoneprocess-track-b-remaining` and
  `task/eqemu-zone-line-transition-scenario`, both measured 300 commits ahead of
  `fable` at this tip, both pre-dating the lettered programme. Closes when: a
  survey unit reports what is in them. Prov: deferred item 22.

- **B027** (P) Something on the eqemu server sets `Oraclerogue`'s persisted `gm`
  flag to 1 — four consecutive live units (T297, T320, T322, T323) saw it, none
  issued `#gm`, and T323 captured it before starting any client. It silently
  changes what a test character can do. **Needs eqemu.** Prov: deferred item 43.

- **B028** (P) Batch Z's Z5.3 empty-draw measurement (one-triangle buffer
  isolation) was never taken. Prov: archived board, Batch Z.

## Added 2026-09-03 by the K07 Review (movement extraction)

- **B029** (B) `src/client/movement/movement_state.cpp` `MovementComponent::m_water_surface_z`
  — written on every water-state change and read by nothing; swim height has
  no consumer. Closes when: a reader exists or the member is deleted. Prov: K07 Review.

- ~~**B030** (B) `include/client/eq.h` `EverQuest::m_swim_up` / `m_swim_down` with
  `SetSwimUp` / `SetSwimDown` — zero readers, zero setter callers.~~ **CLOSED by
  K13**: all four, plus `GetWaterState()` and `IsSwimming()`, had exactly one
  occurrence tree-wide (their own definition) and were deleted; `m_water_state`
  itself moved into `eqt::movement::MovementComponent`, its sole writer. Prov: K07 Review.

- ~~**B031** (B) `include/client/eq.h` `EverQuest::m_last_movement_update` — zero
  references tree-wide.~~ **CLOSED by K15**: re-measured at `7c125b28`, still
  zero; deleted. Prov: K07 Review.

- **B032** (A) `src/client/movement/movement_component.cpp` `MovementComponent::StartMoveForward`
  / `StartMoveBackward` / `StartTurnLeft` / `StartTurnRight` — the keyboard
  movement START half has zero callers while the STOP half is called (T316
  recorded the keep; the switch has no on position). Closes when: the input
  path calls the Start* methods and a live keypress moves the player, or the
  cluster is deleted together with its Stop* half. Prov: K07 Review, T316.

## Added 2026-09-03 by the Batch L survey (`refactor/L_renderer_component_extraction.md`)

- ~~**B033** (B) `include/client/graphics/irrlicht_renderer.h` — 14 methods
  declared and defined nowhere.~~ **CLOSED by K15**: all 14 re-measured at
  `7c125b28` (each had exactly one occurrence tree-wide, its own declaration;
  the offset/rotation pair really does live on `EntityRenderer`) and deleted.
  Prov: Batch L survey.

- **B034** (B) `include/client/graphics/irrlicht_renderer.h` — **SIX of the
  eight CLOSED by K15, and the survey's claim was FALSE for the other two.**
  Deleted: ~~`lastOccCamX_`~~ (with its `lastOccCamY_/Z_` and the whole
  `lastOccFwd{X,Y,Z}` line — the occlusion camera-movement gate was never
  implemented), ~~`hudCallback_`~~ (its only reference was `setHUDCallback`,
  which had zero callers; the `HUDCallback` typedef and the setter went with
  it), ~~`cachedHudText_`~~, ~~`cachedHotkeysText_`~~,
  ~~`cachedHeadingDebugText_`~~. **KEPT: `chatPrefillMutex_` and
  `pendingChatPrefill_` are LIVE** — T319 wired them after the Batch L survey
  was taken. `requestChatPrefill()` writes on the game thread from
  `IrrlichtRenderer::handleActionResult` (`irrlicht_renderer.cpp`) and
  `takeChatPrefill()` reads on the render thread in
  `renderer_frame_input.cpp`; `test_reply_to_tell_key.cpp` covers the trip.
  This entry stays open only as the record of that correction. Prov: Batch L
  survey, corrected by K15.

## Added 2026-09-03 by the K09 Review (zone extraction)

- ~~**B035** (B) `include/client/zone/zone_component.h` `ZoneComponent::m_spawn_appearance_sent`
  / `m_client_spawned` — write-only.~~ **CLOSED by K15**: re-measured at
  `7c125b28` — the only occurrences were the two declarations, the two
  `CleanupZone` resets and one already-commented-out line in
  `src/client/core/everquest.cpp`. Members and resets deleted; the live spawn-ready state
  is `game_state.world().clientReady()`, reset on the adjacent line. The dead
  comment in `eq.cpp` was left in place — that file is K14's territory this
  wave. Prov: K09 Review.

## Added 2026-09-03 by the K11 Review (bridge extraction)

- **B036** (A, feature — **WILL RULED 2026-09-03**) UI sounds are wanted:
  opening and closing bags, picking up coins (pp/gp/sp/cp), and possibly
  tradeskill (combine) sounds — "there may be other UI sounds". Today no UI
  event plays a sound (combat, cast, death, door and water sounds are wired
  through `ctx_.audio_manager` and were never affected). Closes when: the
  official client's UI sound set for those events is MEASURED (which sound
  file the Titanium client plays on bag open/close, coin pickup by
  denomination, combine — from its sound archives or a captured session,
  per `match-the-oracle`), and fable plays the same sounds at the same
  events (`InventoryManager` bag open/close, `ZoneProcessMoneyUpdate`
  deltas, `ZoneProcessRecipeAutoCombine`/combine reply). **Plan:
  `refactor/T328_ui_sounds.md`. Leg A MEASURED 2026-09-04 (`review/T328/`):
  bag open/close and tradeskill container = `itemclth.WAV` (145); item
  dropped into a slot = `wearclth.WAV` (146); coin to cursor = `BuyItem.WAV`
  (138, the generic coin-move sound); inventory open/close, item pickup,
  coin return and EVERY button click are SILENT in the official client.
  Unmeasured (blocked in-world): coin pickup by denomination from a corpse,
  buy, sell, combine success/fail. Leg B wires the measured rows.** Found on the way: `include/client/audio/ui_sounds.h`
  already catalogues UI sound IDs (`BUY_ITEM`, `BUTTON_CLICK`, `CHEST_OPEN`…)
  and NO production code plays any of them — the reachability pattern again.
  **LEG B DONE 2026-09-04 — every MEASURED row is wired and tested**
  (`refactor/T328_ui_sounds.md` § Review — Leg B). Through a new
  `PlaySoundIntent` -> `BridgeComponent::PlaySound()`, the path movement
  already used for `WaterIn`: container open AND close -> `ITEM_CLOTH` 145
  (`EntitiesComponent::ZoneProcessClickObjectAction`, both arms — fable has NO
  bag window, so the server's `OP_ClickObjectAction` is the only place it
  learns a container opened); item dropped into a slot and stack-merge ->
  `WEAR_CLOTH` 146 (`InventoryManager::placeItem` / `placeOnMatchingStack`,
  on the LOCAL drop, matching the oracle's timing); coin onto the cursor ->
  `BUY_ITEM` 138 (`InventoryManager::pickupMoney`). The four measured-SILENT
  gestures are now PINNED silent by tests, so nothing can quietly add a sound
  the oracle does not make. Id tables reconciled into ONE measured table,
  `EQT::Audio::SoundBank` (98..149), checked in-test against the shipped
  `SoundAssets.txt`; `SoundId::BUY_ITEM` 145->138 and `SoundId::TELEPORT`
  141->137 corrected, and the invented `OPEN_WINDOW` (143 = thunder1),
  `CLOSE_WINDOW` (144 = thunder2) and `SELL_ITEM` (146 = wearclth) deleted.
  13 new tests in `tests/test_ui_sound_events.cpp`.
  **THIS ENTRY STAYS OPEN on three things Leg B deliberately did not guess:**
  (1) the FIVE UNMEASURED rows — coin pickup by denomination from a corpse,
  buy, sell, combine success and combine failure. Leg A was blocked in-world,
  not on method: the Titanium client **will not drop coin on the ground**, a
  corpse could not be right-clicked because `#goto` lands the player on top of
  it, `#goto` also puts the player *inside* a merchant so her model is occluded
  by his own, and a tradeskill component would not stay in a container slot
  (EQ moves items click-pickup/click-drop, and the drop into the grid did not
  take). Closing them needs a camera/interaction helper for the oracle client;
  the expensive parts (the real-time ALSA-loopback capture rig and the 2604-file
  tone probe) are built and proven under `review/T328/scripts/`.
  (2) `InventoryManager::pickupMoney()` has **NO production caller** — fable has
  no money-slot UI, so the coin sound is wired at the right publisher but is
  unreachable until that UI exists. (3) SIX `SoundId` names that do NOT describe
  the file their id names, left at their old values because T328 measured UI
  events only: `MELEE_HIT` (119 = SwingBig), `MELEE_MISS` (120 = SwordBlk),
  `KICK` (121 = Impale), `PUNCH` (122 = Stab), `SPELL_FIZZLE`
  (109 = SpelGdHt, not SpelFail) and — with a LIVE production caller at
  `src/client/player/player_state.cpp:184` — **`DEATH` (140 = LevDn.WAV, a
  level-DOWN sound, not a death cry)**, where `PlayerSounds::getSoundId(Death,
  race, gender)` is the race-specific sound it probably wants. Each is
  annotated in `sound_assets.h`. Closes when those three are settled.
  Prov: K11 Review; Will's ruling; T328 Leg A + Leg B Reviews.

- ~~**B037** (B) `include/client/bridge/bridge_component.h` `m_audio_config_use_3d_audio`
  — settable only by editing the header.~~ **CLOSED by K15 — WIRED, both ends.**
  K11's note claiming the flag was "still read by `InitializeAudio()`" was
  **wrong**: re-measured at `7c125b28`, it had zero writers AND zero readers, so
  a config source alone would have produced a knob that does nothing. It now has
  both: `audio.use_3d` (JSON) / `--no-3d-audio` (CLI) ->
  `AppConfig::audioUse3D` -> `Bridge().SetUse3DAudio()` ->
  `InitializeAudio()` -> `AudioManager::setUse3DAudio()`, which gates the
  spatial branch of `AudioManager::playSoundInternal()` (off = every sound
  plays centred at the effects volume, no attenuation, no panning). Prov:
  K11 Review.

## Added 2026-09-03 by the K13 Review (context services)

- **B038** (B) `include/client/eq.h` — **the `EverQuest&` transition-handle
  residue, measured.** K13 took the twelve components' `owner_.` count from
  **1,425 to 83** (94.2% removed) by turning the four target groups into the
  three context services and the twelve component pointers. What is left is
  eleven distinct members and nine methods, in five clusters, and each cluster
  is a separate small decision rather than one sweep:
  (1) **renderer-lifetime state** — `m_constrained_config` (8),
  `m_graphics_initialized` (5), `m_constrained_preset` (2),
  `m_loading_status_ptr` (1), all `bridge`-only, read by four `src/client/core/everquest.cpp`
  bodies `Application` drives around the K11 forwarders;
  (2) **the loading-phase state machine** — `EverQuest::SetLoadingPhase` (12) +
  `m_loading_phase` (5) + `IsGraphicsReady` (1); not player state, so K13 did
  not fold it into `eqt::PlayerFacade`;
  (3) **lifecycle entry points** — `RequestQuit` (7), `TickNetwork` (3),
  `StartUpdateLoop`/`StopUpdateLoop`/`OnGameStateComplete`/`ConnectToZone`/
  `SetSneak` (1 each) + `m_update_running` (1), `m_pending_logout_disconnect` (3),
  `m_zone_session_established` (1);
  (4) **members with a public `EverQuest` accessor carrying external callers** —
  `m_guild_id` (8) + `m_guild_rank` (2) (`GetGuildID`/`GetGuildRank`, ~20 test
  sites), `m_last_tell_sender` (2) (`GetLastTellSender`, 3 action-handler + 6
  test sites), `m_string_db` (2) via `GetStringMessage` (4) /
  `GetFormattedStringMessage` (3), `m_profile_appearance` (1),
  `m_zone_server_host`/`m_zone_server_port` (1 each);
  (5) **the send-guard test seam** — `m_test_bypass_send_guard` (5).
  Ten of the twelve components still take the handle; only `eqt::gm::GmComponent`
  reached zero and dropped both it and its `friend` line. Closes when: each
  cluster is re-homed or the handle is declared permanent. Prov: K13 Review.

- **B039** (P) **the `friend class` test is two-axis, not one.** K13 removed the
  `friend` lines for merchant and commands on a zero-`owner_.m_` measurement and
  the build failed: a component also names `EverQuest`'s PRIVATE STATICS by
  qualification (`EverQuest::FormatPrice`, `EverQuest::DumpPacket`,
  `EverQuest::s_debug_level`), which friendship grants and no `owner_.` census
  sees. Both lines were restored in-unit. Closes when: `refactor/K_component_extraction.md`
  §Mechanism 7 states the two-axis rule. Prov: K13 Review.

## Added 2026-09-03 by the L03 Review (overlays extraction)

- ~~**B041** (A) `src/client/bridge/irrlicht_bridge.cpp` `IrrlichtBridge::applyEvent`,
  `state::GameEventType::NavmeshChanged` arm — **the navmesh overlay (Ctrl+N) can
  never draw anything.** The arm's whole body is a `LOG_TRACE` under the comment
  "Handled by renderer if navmesh visualization is active", which is false: the
  only writer of the renderer's navmesh pointer is
  `EQT::Graphics::OverlaysComponent::setNavmesh` (L03 moved it off
  `IrrlichtRenderer`) and **nothing calls it**, so the pointer is permanently
  null and `updateNavmeshOverlay()` returns immediately. Three publishers push
  the event (`src/client/movement/movement_state.cpp` x2,
  `src/client/zone/zone_component.cpp`) carrying the navmesh in
  `NavmeshChangedData`. L03 kept `setNavmesh` rather than applying Mechanism 5 to
  it precisely so this stays a one-line fix. Closes when: the arm calls
  `renderer_->setNavmesh(...)` with the event's payload and Ctrl+N draws in a
  navmesh-enabled zone. Prov: L03 Review.~~ **STRUCK T329**: the arm now
  forwards `d.navmesh` to `renderer_->setNavmesh(...)` (both non-null on zone
  load and `nullptr` on zone leave), pinned by
  `tests/test_navmesh_overlay_wire.cpp`. ~~**The live screenshot proof (Ctrl+N
  drawing in a navmesh-enabled zone) is OWED** — the eqemu lock was held by
  T328 for this unit's entire run.~~ **LIVE LEG DONE 2026-09-04 by T328 Leg B
  (`review/T329/`, two captures + both client logs): the WIRE is proven — Ctrl+N
  reaches `ToggleNavmeshOverlay`, the navmesh pointer is non-null (B041's actual
  bug) and the client builds 112 overlay triangles, all in its own log. But
  NOTHING DRAWS, and switching the overlay on makes the entire 2D UI vanish.
  B041's closure condition said "and Ctrl+N draws", so the DRAW half is now
  B045; the wire half — this entry — is closed.**
  Prov: `refactor/T329_navmesh_overlay_wire.md` Review; T328 Leg B Review.

- **B042** (B) `src/client/graphics/overlays/overlays_component.cpp`
  `EQT::Graphics::OverlaysComponent::drawZoneLineOverlay` — **the zone-line debug
  border can never turn on.** It is called every frame from
  `renderer_frame_render.cpp` and gates on `inZoneLine_`, whose only writer was
  `IrrlichtRenderer::setZoneLineDebug()`. That method had zero callers tree-wide
  and no `pushEvent`, so L03 deleted it under Mechanism 5; the three members it
  wrote (`inZoneLine_`, `zoneLineTargetZoneId_`, `zoneLineDebugText_`) were kept
  because the live draw body reads them. This is a disclosure, not a regression —
  the overlay was equally unreachable before L03. Closes when: either the zone
  entry path sets the state (the feature is wanted) or the draw body and its
  three members go (it is not). Prov: L03 Review.

## Added 2026-09-03 by the L04 Review (diagnostics extraction)

- ~~**B043**~~ **CLOSED 2026-09-04 by T330** — the four arms (`ToggleWireframe`/`ToggleHUD`/`ToggleNameTags`/`ToggleOldModels`) are in `processCommonInput`'s switch and pinned by `tests/test_debug_hotkey_arms.cpp`.
  **STILL OPEN (B, split out): the `GraphicalMode` route.** `GraphicalMode::toggleWireframe/toggleHUD/toggleNameTags` still dispatch through an `output::IOutputRenderer*` that `IrrlichtRenderer` does not implement, so they reach `output_renderer.h`'s empty virtual defaults — wire or delete that interface route.
  ~~(A) **Four bound debug hotkeys have never done anything.**~~
  `RendererEventReceiver::OnEvent` queues `RendererAction::ToggleWireframe`,
  `ToggleHUD` and `ToggleNameTags`
  (`src/client/graphics/renderer/renderer_event_receiver.cpp:181-183`), and
  `ToggleOldModels` sits in the same enum
  (`include/client/graphics/renderer_types.h:111-112`), but
  `IrrlichtRenderer::processCommonInput()`'s switch
  (`src/client/graphics/frame/renderer_frame_input.cpp`) has **no arm for any of the
  four** — they fall through its `default: break;`. The hotkeys are bound and
  parsed (`src/client/input/hotkey_manager.cpp:111-113`,
  `include/client/input/input_handler.h:63-65`). The second route is dead too:
  `GraphicalMode::toggleWireframe/toggleHUD/toggleNameTags`
  (`src/client/mode/graphical_mode.cpp:184-199`) call through an
  `output::IOutputRenderer*`, and `class IrrlichtRenderer` **does not derive
  from `IOutputRenderer`** — so those reach `output_renderer.h`'s empty
  `virtual ... {}` defaults, not the renderer. The implementations are live and
  correct; they are now `DiagnosticsComponent::toggleWireframe/toggleHUD/`
  `toggleNameTags/toggleOldModels`, reachable as
  `renderer.Diagnostics().toggleX()`. **Fix is four switch arms** in
  `processCommonInput`, mirroring the `ToggleZoneLights` / `CycleObjectLights`
  arms next to them. Not done in L04, which is structural. This is the
  T313/T315 unwired-handler pattern for the fourth time. Prov: L04 Review.

- ~~**B044** (P)~~ **CLOSED 2026-09-03 at the L04 merge — correction 5 struck in `refactor/L_renderer_component_extraction.md`.** (P) **`refactor/L01_rdp_component.md`'s correction 5 is factually
  wrong and is being cited by every L plan.** It says `setCurrentTarget`,
  `clearCurrentTarget`, `toggleHUD`, `toggleNameTags`, `toggleWireframe`,
  `dumpScene` and `getMemoryReport` are "`IOutputRenderer` virtual overrides"
  that must stay on `IrrlichtRenderer` as forwarders. `IrrlichtRenderer`
  implements no interface (`include/client/graphics/irrlicht_renderer.h`,
  `class IrrlichtRenderer {`). A forwarder is justified by its ACTUAL callers
  — the bridge and `Application` (the K11 rule) — never by a name collision
  with an interface the class does not implement. Closes when: correction 5 is
  struck from L01's Review and `refactor/L_renderer_component_extraction.md`'s
  "L01 corrections" list. Prov: L04 Review.

- **B045** (A) `src/client/graphics/player/renderer_camera.cpp` —
  **the audio listener is never given the camera transform.**
  `getCameraTransform(posX..upZ)` carried the comment "Get camera position and
  orientation for audio listener" on `IrrlichtRenderer` and has **zero callers
  tree-wide**, in-family included (K03 correction a; comment- and
  string-filtered, receiver-checked). Nothing in `src/client/audio/` ever asks
  for it, so 3D audio positioning is wired at one end only — K15 correction
  (qq)'s shape. The implementation is live and correct; L05 kept it and it is
  now `PlayerCameraComponent::getCameraTransform`, reachable as
  `renderer.PlayerCamera().getCameraTransform(...)`. **Fix is one call site**
  in the audio update path. Not done in L05, which is structural. Prov: L05
  Review.

- **B046** (A) `src/client/graphics/player/renderer_camera.cpp` —
  **vision never returns to the racial base, and vertical swim control has no
  producer.** Two zero-caller knobs on the same family, both the sole enable
  path of live state (L03 correction 17):
  * `resetVisionToBase()` is the ONLY downgrade path for `currentVision_`.
    `setVisionType()` is live (`src/client/bridge/irrlicht_bridge.cpp:582`) and
    only ever UPGRADES, by design. Nothing calls the reset, so an Ultravision
    or Infravision buff is **permanent for the session** — the comment
    "call when vision buffs fade" describes a call that does not exist.
  * `setSwimUp()` / `setSwimDown()` are the **sole writers** of
    `playerMovement_.swimUp` / `.swimDown`, which `updatePlayerMovement()`
    reads live every frame. No caller exists on either route, so a swimming
    player cannot ascend or descend deliberately. `setLevitating()` is a third
    zero-caller writer, harmlessly duplicated by `setSwimmingState()`'s
    levitating parameter.
  All four kept by L05 and reachable through `renderer.PlayerCamera()`.
  Fixes are a buff-fade hook and two input-action arms. Prov: L05 Review.

- **B047** (B) `src/client/graphics/player/renderer_player_collision.cpp` —
  **the incremental collision-registration API has never run, and a comment
  claims it does.** `addRegionToCollision()`, `addDoorToCollision()`,
  `addObjectToCollision()`, `findBspRegionForPoint()` and
  `clearCollisionDebugLines()` have **zero callers tree-wide**.
  `src/client/graphics/zone_lifecycle/renderer_zone_draw.cpp:198-199` asserts in a comment
  that "Collision detection is set up progressively via
  setupMinimalZoneCollision() and addRegionToCollision() during the background
  zone loading phases" — only `setupMinimalZoneCollision()` is actually called
  (`src/client/graphics/zone_load/renderer_zone_load_objects.cpp:545`), once, so
  collision for regions/doors/objects streamed in AFTER that call is never
  registered. Whether that is a live gap or dead ambition needs a measurement,
  not a deletion: L05 kept all five on `PlayerCameraComponent` rather than
  take them under §5, because a silent delete converts a findable defect into
  an invisible one (L03 correction 5). `getFirstPersonVerticalFov()` is a
  sixth zero-caller body, kept for the same reason and harmless. Prov: L05
  Review.

## Added 2026-09-04 by the T328 Leg B Review (UI sounds + T329's owed live leg)

- **B048** (A) `src/client/graphics/overlays/overlays_component.cpp`
  `OverlaysComponent::drawNavmeshOverlay()` — **the navmesh overlay draws
  nothing, and turning it on wipes the entire 2D UI.** Measured live in
  `qeynos2` (`review/T329/`, two screenshots plus both client logs): after
  Ctrl+N the client logs `Navmesh overlay: ON` and
  `Navmesh overlay: Updated with 112 triangles`, so the triangle list is
  non-empty and `drawNavmeshOverlay()`'s early return is not taken — yet no
  navmesh lines appear, and the health/mana/stamina bars, FPS counter, chat
  panel and hotbar present in an identical overlay-OFF capture are all GONE.
  Two grabs from the same session, so not one bad frame. Both symptoms are
  consistent with a single cause in that function — it issues
  `driver->draw3DLine()` three times per triangle after `setMaterial()`
  (EMT_SOLID, `EAAM_LINE_SMOOTH`, thickness 2) and
  `setTransform(ETS_WORLD, ...)`, and leaves both set — but that was NOT
  isolated, and the file is L05's territory this wave. Invisible until T329
  wired `setNavmesh`: the pointer was permanently null before, so the function
  always returned early. Closes when: Ctrl+N draws navmesh triangles in a
  navmesh-enabled zone AND the 2D UI is still there, proven by a live capture
  pair. Prov: T328 Leg B Review.

## Added 2026-09-04 by the T328 Leg B live session

- **B049** (A) `Testchar` was found at `z=11.8806` after being left at
  `z=3.751` — a drift of exactly **13 × 0.625**, one T311 login z-compounding
  step per session. T311 Leg B (2026-09-02) matched the official client's
  wire z (`groundZ + 0.625*size`) and reported the compounding closed; this
  measurement says it is not, or that a second writer re-adds the offset on
  login. Closes when: a login/logout cycle is measured twice in a row with
  the persisted z unchanged, and the writer is named. **Needs eqemu.** Prov:
  T328 Leg B Review; T311.

## Added 2026-09-04 by the L12 Review (Batch L close-out)

- **B050** (B) **The `owner_.` residue in the eleven renderer components:
  1,065 uses, six ranked clusters — and it is over L12's own 300-use STOP
  threshold, because the plan's promotion rule (`>= 3 components`) does not
  reach the coupling the tree actually has. The STOP FIRED and is reported
  here rather than worked around.** Measured with a comment- and
  string-filtered census over `src/client/graphics/<family>/` +
  `include/client/graphics/<family>/` for the eleven extracted components:
  **1,796 at `1c9afba7`, 1,065 after L12 (-731, 40.7%).** The clusters,
  ranked, with the one reason each is still there:

  | # | cluster | uses | why it stayed |
  |---|---|---|---|
  | 1 | the frame-timing blob — `frameTimings_` 200, `frameTimingsAccum_` 135, `frameTimingsSampleCount_` 11, `lastPolygonCount_` 6, `currentFps_` 4, the two EMA cost members, the scene-profile pair, `frameCount_`, `lastFpsTime_`, `frameTimingEnabled_` | **374** | jointly owned by exactly TWO components — `frame` writes it, `diagnostics` reads and reports it — so it is below the `>= 3` promotion threshold L12 was given. It is the single largest coupling left in the renderer and one unit's whole work: either a `FrameTimings` service both components hold, or `frame` owns it and `diagnostics` reads it through `ctx_.Frame()` |
  | 2 | two-family state, 55 members — `playerConfig_` 56 (player 46 / frame 10), `storedZoneEnvironment_` 37, `simulationWorker_` 26, `cameraMode_` 21, `playerLightNode_` 19, `guienv_` 14, `textureRebuildQueue_` 13, `zoneTriangleSelector_`/`terrainOnlySelector_` 12 each, `zoneMergeBuffers_` 12, `currentTargetId_` 12, `sortedDrawEntries_` 11, `chatInputFocused_` 11, … | **480** | same reason: exactly two components each. Sec.Mechanism 3's "the unit extracting the LAST family moves it" has no answer once every family is extracted and two of them still share the member — that is a per-member ownership call (dominant owner vs. context member), 55 of them, and L12's rule did not cover it. **Clusters 1+2 are 854 of the 1,065 and are entirely a `>= 3` vs `>= 2` threshold question** |
  | 3 | members typed by an `IrrlichtRenderer`-NESTED type — `fireGlowLights_` 26, `zoneLightData_` 22, `renderPassTimer_` 14, `doorCollisionData_` 7 | **69** | all four clear the `>= 3` bar and were deliberately NOT promoted: `renderer_context.h` would have to name `IrrlichtRenderer::ZoneLightData` etc., which re-acquires the ~60-transitive include the context exists to avoid (batch correction 10). The fix is to hoist each struct to `renderer_types.h` with a `using` alias left in the class (batch correction 22's shape) — and `ZoneLightData` needs care: two OTHER structs of that name exist (`SimulationZoneData::ZoneLightData`, and one at namespace scope in `include/client/zone_object_data.h`) |
  | 4 | the two EQE1 mutexes — `eqe1Mutex_` 6, `eqe1CandidatesMutex_` 6 | **12** | both clear the `>= 3` bar; both were left alone on purpose. Relocating the access path of a synchronisation primitive is what Batch L's second STOP condition forbids in a structural unit, so the four EQE1 members beside them were promoted and these two were not |
  | 5 | single-component members still on the renderer, 16 names — `sunLight_` 16, `pendingGPUResults_` 6, `zoneLightsEnabled_` 5, `debugDirectionalLightEnabled_` 4, … | **49** | each is ALSO reached by `src/client/graphics/renderer/renderer_init.cpp` or its `irrlicht_renderer.cpp`, which are terminal owners: batch correction 10's "the last family moves it" never fires for a member shared only with init/core |
  | 6 | `IrrlichtRenderer` methods that stayed — `isLoading` 25, `getDetailManager` 7, `setRenderDistance` 4, `isRegionPvsVisible` 4, `getCameraModeString` 4, and 31 more at 1-2 each | **81** | the renderer's PUBLIC API, called by `Application`, `IrrlichtBridge` and `graphical_mode` as well as by components (the K11 rule); 30 of the 36 are the one-use `/toggle`-and-query surface `SlashCommandsComponent` drives. Not a defect |
  | | **total** | **1,065** | |

  Per component after L12 — `rdp` **0** (`friend` line and `IrrlichtRenderer&`
  both gone), `zone_stream` 19, `slash` 37 (`friend` line gone; all 37 are
  public methods), `eqe1` 49, `sim` 59, `zone_load` 72, `overlays` 73,
  `player` 84, `zone_lifecycle` 136, `diagnostics` 190, `frame` 346.

  Closes when: a ruling on the promotion threshold (clusters 1+2), the three
  type hoists of cluster 3, and a re-measure. Clusters 4-6 are reasoned keeps
  and may simply stay. Prov: L12 Review.

## Added 2026-09-04 by the G04 Review

- **B051** (B) `src/client/application.cpp` `Application::initConstrainedRendererConfig`
  — `customMemorySpec` is write-only (set in one preset arm, never read).
  Closes when: read where the preset is applied, or deleted. Prov: G04 Review.

## Added 2026-09-04 by the G07 STOP

- **B052** (B) `src/client/graphics/eq/variant_loading_animation.cpp`
  `loadModelFromGlobalChrWithVariantsForAnimation` — 815 lines that are NOT
  phases: one `for (codeToTry …)` candidate loop whose six `continue`s mean
  "next candidate" and whose successes `return` from three loops deep. The
  length is duplication — the animation-merge body appears FOUR times
  (0.92–0.96 similar) and the zone/global character blocks are 0.91 similar.
  **No test drives this method** (zero hits in `tests/`); the EQE1 crash
  matrix does not reach it. A verbatim-slice decomposition (Batch G's proof)
  cannot apply. Closes when: (1) a behaviour test drives the loader through
  the primary-miss/fallback-hit and all-candidates-exhausted paths (a race
  whose fallback code hits must LOAD, not cache null — Will's all-or-nothing
  rule), then (2) `mergeAnimationsFrom()` replaces the four copies and the
  candidate loop keeps its shape (`buildCodesToTry` / `selectVariantParts` /
  `tryBuildVariantFrom` as the G07 Review lays out, 815 → ~250). Prov: G07
  Review (`refactor/G07_variant_loader.md`).
