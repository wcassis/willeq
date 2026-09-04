# BOARD — the open-work queue of record

- **Tree confirmed at:** `3808ebe9` (branch `task/K00-legA-board`, base `fable`).
- **Date:** 2026-09-03. Built by K00 Leg A (`refactor/K00_truth_reset.md`).
- **Suite:** 3,759 of 3,760 passing at `661cb078` (2026-09-04, main verification run). The sole expected failure is the ALSA host
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

- ~~**B001** (A) `src/client/movement/movement_state.cpp`
  `eqt::movement::MovementComponent::SetPositionState` (K07 moved it) —
  every arm ends in `SendSpawnAppearance`, so a server-forced pose cannot be
  applied without echoing it back, and its `AT_DIE` arm encodes a pose self never
  receives.~~ **CLOSED by T344** (`refactor/T344_forced_pose_setter.md`):
  `ApplyPositionState()` is the non-sending setter (the pre-split body minus
  the five sends); `SetPositionState()` is it plus four of them, for the five
  user-initiated call sites (`/sit`, `/stand`, `/camp`, the
  `setPositionState(int)` action, auto-hunting's sit-to-med). The two
  server-forced paths now use `ApplyPositionState` — `ZoneProcessSpawnAppearance`'s
  `AT_ANIMATION` arm when `spawn_id == MySpawnId()`, and `ZoneProcessDeath`
  for self, which is what drives `positionState` to Dead. Both were holes,
  not echoes: nothing wrote `player().positionState()` from the zone stream
  at all. The `AT_DIE` send is DELETED (T311 Leg A: type 0 appears zero times
  in either direction of a real official-client session); the arm is kept
  because `POS_DEAD` is reachable from both new paths. Pinned by
  `tests/test_forced_pose_no_echo.cpp` (8 tests; 4 failed pre-fix, including
  the `AT_DIE` echo), which replays the T311 self-death `OP_Death` bytes
  verbatim. Still owed, and NOT this unit: the official client's full death
  sequence — C→S Lying appearance, no further `OP_ClientUpdate` while dead,
  then an `OP_ZoneChange` re-entry (shape in the T344 Review). Prov: deferred
  item 15.

- ~~**B002** (A) `src/client/action/eq_action_handler.cpp` `EqActionHandler::toggleAutorun`
  — a `LOG_DEBUG` stub, and the only live consumer
  (`renderer_player_physics.cpp`'s `playerMovement_.autorun`) has zero writers, so
  three bound keys report success and do nothing.~~ **CLOSED by T343**: the route
  is RENDERER-side, not game-side — phase 1 (`readMovementInput`) rewrites the
  movement flags every frame, so a game-thread writer loses the race and a
  key-DOWN action has no key-up to clear it (T334's swim finding, B046). The
  receiver now queues `RA::ToggleAutorun` on `actionQueue_` instead of the bridge
  queue, `processCommonInput()` has an arm that flips
  `PlayerCameraComponent::toggleAutorun()`, and phase 1 cancels it on a
  translational movement key (turning deliberately does not cancel — steering is
  the point). The game-side `toggleAutorun()` survives for the console handler
  alone with an honest comment; `GraphicsInputHandler`'s now-unreachable arm was
  deleted. Pinned by `tests/test_autorun_arm.cpp` (3 of its 4 cases failed before
  the fix) and golden statements 27-28. **⚠ LIVE PROOF OWED** — T342 held the
  eqemu lock. Prov: deferred item 10, T343 Review.

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

- ~~**B024** (P)~~ **CLOSED 2026-09-04 (M00 merge): the structural programme is complete.** Batch K (K00–K16: `eq.h` 2,025 → 899, twelve components + four context services, the transition handle measured down to 83 uses = B038), Batch L (L01–L13: `irrlicht_renderer.h` 1,914 → 1,343, eleven components, residue 151 = B050 closed), Batch G (six of eight oversize methods decomposed and golden-pinned; `createDefaultConfig` ruled out; the variant loader parked as B052), M00 (no allowlist; the 1,500 ceiling enforced tree-wide by ctest). Original text: Component extraction of `class EverQuest` (2,025-line header, 685
  member functions, 178 data members, 37 flat `src/client/eq_*.cpp` TUs) and of
  `class IrrlichtRenderer` is the open structural programme —
  `refactor/K_component_extraction.md`, K01..K13, then Batch L. Both headers hold
  a no-grow allowlist entry that each K unit ratchets down. Prov: the J tracker's
  "WILL'S RULING", 2026-09-02."

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

- ~~**B032** (A) `src/client/movement/movement_component.cpp` `MovementComponent::StartMoveForward`
  / `StartMoveBackward` / `StartTurnLeft` / `StartTurnRight` — the keyboard
  movement START half has zero callers while the STOP half is called (T316
  recorded the keep; the switch has no on position).~~ **CLOSED by T343 —
  REFUTED and deleted.** The poll table measured at `849db7a2`: G06 phase 1
  (`readMovementInput`, `renderer_player_physics.cpp`) polls nine
  `HotkeyAction`s — MoveForward/MoveBackward/TurnLeft/TurnRight/StrafeLeft/
  StrafeRight/Jump/SwimUp/SwimDown — every frame and writes the whole of
  `PlayerMovementState`. Forward/back/turn are the renderer's per-frame poll,
  exactly like swim, so this was a SECOND, parallel route, not a switch missing
  its on position; and T319's "only game-side mover" premise fails too, because
  the route needs key-down/key-up pairs and the console handler is line-based.
  Deleted as one closed cluster: the eight Start*/Stop* methods,
  `UpdateKeyboardMovement()`, the four `PlayerState` flags they were the only
  writers of, and the `UpdateMovement()` branch that was their only reader.
  Its one reachable call site, `EqActionHandler::stopAllMovement()`, was BROKEN
  and is now fixed: it called four Stop* that never cleared the movement target,
  so `/stop` during a MoveTo/Follow resumed on the next tick; it now calls
  `StopMovement()` (public all along, despite the comment that claimed
  otherwise). Pinned by `tests/test_stop_all_movement.cpp` (failed before the
  fix on all three target assertions). Prov: K07 Review, T316, T319, T343 Review.

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
  (1) ~~the FIVE UNMEASURED rows — coin pickup by denomination from a corpse,
  buy, sell, combine success and combine failure.~~ **T342 MEASURED AND WIRED
  FOUR OF THE FIVE, 2026-09-04** (`refactor/T342_unmeasured_sound_rows.md`
  § Review, `review/T342/`), on the same tone probe — its `tonemap.json` came
  out byte-identical to T328's, so the two units' rows are one measurement.
  ~~coin pickup by denomination~~ — **all four denominations are the SAME
  sound**, `BuyItem.WAV` 138, measured pp/gp/sp/cp from the inventory money
  slots; `pickupMoney()` was already denomination-agnostic, so the measurement
  closes it with no code change and a test pins it. ~~buy~~ — **TWO sounds
  together**, `BuyItem.WAV` 138 + `wearclth.WAV` 146 (the coin, and the item
  landing in a slot), wired at `MerchantComponent::ZoneProcessShopPlayerBuy`.
  ~~sell~~ — **one sound**, `BuyItem.WAV` 138 only (the item *leaves* a slot,
  and T328 row 7 measured that gesture silent), wired at
  `ZoneProcessShopPlayerSell`. ~~combine success~~ and ~~combine failure~~ —
  **both measured SILENT** (2 successes, 3 failures) and now PINNED silent;
  `LevDn.WAV` sits in the bank and would have been a plausible guess for the
  failure, and the oracle plays nothing. 7 new tests. **What REMAINS on this
  clause is one row: coin out of a CORPSE'S LOOT WINDOW.** T342 spawned an NPC
  with a cash loot table and killed it, but `#npctypespawn` drops it among North
  Qeynos' training dummies and eight verified right-clicks reached a dummy's
  Consider window instead of the corpse. It needs a zone with no NPC clutter at
  the spawn point, not a new technique — T342 also recorded the three
  interaction rules that unblocked everything else (a 3D-view click must carry
  **no** pointer motion while a UI click needs 1 px; `#goto` an **offset** from
  the NPC, never its own coordinates; every click needs a verified retry loop —
  the merchant window took three identical right-clicks). Two server rules found
  on the way, both of which invalidate the obvious recipe for a combine failure:
  **a GM always succeeds** (`/mnt/projects/akk-stack/code/zone/tradeskills.cpp:1124`), and **Baking 300 can
  never fail** — eqemu's `chance = skill − 0.75 × trivial + 51.5` (floor 5, cap
  95) gives 95% success at skill 300 even against the highest-trivial Baking
  recipe that exists, so a failure needs a low-skill character, not a
  high-trivial recipe.
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

- **B038** (B) **the `EverQuest&` transition-handle residue — 83 -> 5.**
  T341 retired clusters 1-4 and censused both friend axes for all twelve
  components. What is LEFT, measured at T341's tip:
  (1) **`EverQuest::m_test_bypass_send_guard`, 5 `owner_.` uses** — movement 3
  (`movement_update.cpp` 573/596, `movement_state.cpp` 191), group 2
  (`group_component.cpp` 84/112). The T88 test seam gating the movement and
  group send guards, set by `EverQuest::Test_SetBypassSendGuardForSeam`. It is
  a TEST seam on a PRODUCTION guard, so re-homing it is a design question
  (where does a send-guard bypass live once the families own their guards?),
  not a mechanical move — which is why T341 left it;
  (2) **22 `&owner_` identity keys in `eqt::movement::MovementComponent`** —
  the function-static `std::map<EverQuest*, …>` throttle tables (K06/K07
  correction (n)). Not private access; they need the handle, not the
  friendship;
  (3) **2 `std::bind(&EverQuest::ZoneOnStatusChangeReconnectDisabled, &owner_, …)`
  in `eqt::zone::ZoneComponent`** (`zone_component.cpp` 534/585) — a PRIVATE
  member function taken by address. This is the only remaining axis-2 use
  tree-wide and the only reason zone is still a friend;
  (4) **2 `EQ::castRefusalMessage(owner_, result)` in
  `eqt::bridge::BridgeComponent`** (`bridge_component_intents.cpp` 423/644) —
  `owner_` passed BY REFERENCE to a free function. Needs the handle, needs no
  friendship: bridge is the first component to lose its `friend` line while
  KEEPING the handle.
  Three `friend` lines survive (group, movement, zone) and four handles
  (group, movement, zone, bridge); eight of each went at T341, gm's at K13.
  Closes when: `m_test_bypass_send_guard` is re-homed or ruled permanent, and
  (2)-(4) are ruled the permanent shape or removed. Prov: K13 Review, T341
  Review.

- ~~**B039** (P)~~ **CLOSED 2026-09-04: the two-axis rule is stated at `refactor/K_component_extraction.md` §K13 corrections (ii) and applied by every L unit since.** (P) **the `friend class` test is two-axis, not one.** K13 removed the
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
  B048 (T328 Leg B's own text said B045 here; B045 was already the audio
  listener entry below, and the draw half was filed as B048 — corrected T334);
  the wire half — this entry — is closed.**
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

- ~~**B045**~~ **CLOSED 2026-09-04 by T334 — but the premise was WRONG and the
  real defect was worse.** "3D audio positioning is wired at one end only" is
  refuted: `AudioManager::setListenerPosition()` has had a producer since D20c2
  — `BridgeComponent::PostRenderTick()`, every frame — and it deliberately uses
  the PLAYER position rather than the camera's, because in third person the
  camera can sit far behind and would make a nearby NPC sound distant. What was
  broken is the ORIENTATION it passed: `HeadingDeg()` returns DEGREES, and the
  conversion divided by 512 as though those were the 0-512 wire units AND
  negated cos, so the listener faced a mirrored direction turning 1.42x too
  fast. Every positioned sound panned to the wrong side at every heading but
  due north (measured: heading 90 gave forward (0.893, -0.450) instead of
  (1, 0); heading 360 gave (-0.957, 0.290) instead of (0, 1)). T334 moved the
  block to `BridgeComponent::UpdateAudioListener()` in
  `bridge_component_ui_audio.cpp` and routed the conversion through
  `EQT::Audio::eqHeadingToListenerForward()`, which agrees with
  `computeMovementVector()` and `HandlePlayerPositionUpdate()`: forward =
  (sin h, cos h, 0), h in degrees. Pinned by
  `tests/test_spatial_audio.cpp` (`ListenerForwardTest`, 2 tests, both failed
  before). `PlayerCameraComponent::getCameraTransform()` — the zero-caller
  accessor this entry was actually about — is DELETED (§5): the listener it was
  written for exists and is placed by design. A camera-based listener would be
  a listening measurement against the official client, not an argument from
  this accessor's existence. ~~(A) `src/client/graphics/player/renderer_camera.cpp` —
  **the audio listener is never given the camera transform.**~~ Prov: L05
  Review; T334 Review.

- ~~**B046**~~ **CLOSED 2026-09-04 by T334. One half was real and worse than
  boarded; the other half was refuted.**
  * **Vision — REAL, and the diagnosis was off by one hop.** The buff-fade hook
    was not missing: `EverQuest::InitializeSpellSystem()` installs a
    `BuffManager` fade callback (`src/client/core/everquest.cpp`) that, when an
    UltraVision/InfraVision buff expires, RECOMPUTES the vision from every buff
    still up and publishes the absolute result as a `VisionChanged` event. The
    gap was at the CONSUMER: `irrlicht_bridge.cpp`'s arm called
    `setVisionType()`, which only ever upgraded, so the recomputed value could
    raise vision and never lower it — the fade publish was a no-op and an
    Ultravision buff lasted the session. `setVisionType()` now applies the
    payload absolutely (`resetVisionToBase()` first, then the buff value on
    top), so the racial floor still holds and `resetVisionToBase()` has a
    production caller. `renderer_types.h`'s "never downgraded" comment is
    corrected. Pinned by `tests/test_vision_buff_fade_wire.cpp` (3 tests, all
    failed before) and by the rewritten
    `PlayerCameraComponentTest.VisionAppliesTheBuffValueOverTheRacialBase`.
    ~~**NOTE: the grant side is still missing — see B055**~~ (corrected by T335b: this read "B054", which is the debug-overlay item, now closed; the vision-grant gap is B055). **Closed by T339 (2026-09-04): the grant side now publishes, sharing one recompute with the fade side.**
  * **Swim — REFUTED, no arms added.** "`setSwimUp()`/`setSwimDown()` are the
    sole writers … vertical swim control has no producer" (L03 correction 17)
    is false. `readInputState()`
    (`src/client/graphics/player/renderer_player_physics.cpp`, G06 phase 1)
    polls `HotkeyAction::SwimUp`/`SwimDown` — bound to `[` and `]` in
    `hotkey_manager.cpp` AND in `config/hotkeys.json` — and assigns
    `playerMovement_.swimUp/.swimDown` every frame, exactly as it does for
    WASD. It is pinned by the G06 golden
    (`tests/goldens/movement_order_g06.txt` lines 23-24). Vertical swim control
    has worked all along. Adding the boarded `RendererAction` arms would have
    lost the race with the poll (overwritten the next frame, and nothing to
    clear the flag on key-up), so T334 DELETED `setSwimUp()`, `setSwimDown()`
    and `setLevitating()` under §5 instead; the third duplicated
    `setSwimmingState()`'s live levitating parameter. None had a `pushEvent`.
  ~~(A) `src/client/graphics/player/renderer_camera.cpp` — **vision never
  returns to the racial base, and vertical swim control has no producer.**~~
  Prov: L05 Review; T334 Review.

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

- ~~**B048** (A) `src/client/graphics/overlays/overlays_component.cpp`
  `OverlaysComponent::drawNavmeshOverlay()` — **the navmesh overlay draws
  nothing, and turning it on wipes the entire 2D UI.**~~ **CLOSED 2026-09-04: code fixed by
  T333, and the owed live ON/OFF pair captured by T336** (T332 had held the
  eqemu lock for T333's whole run). Cause, measured headlessly on a real GLES2 device and
  not guessed: by the debug-overlay pass the driver is already latched in
  `Is2DMode` (2D drawing happens during and just after `smgr->drawAll()`), and
  `COpenGLES2Driver::setOrthoProjection()` early-returns while that latch is
  set. So (1) the world-space line batch was multiplied by the leftover ORTHO
  matrix in `Matrices[ETS_VIEW]/[ETS_PROJECTION]` and landed off screen —
  "nothing draws"; and (2) `setMaterial()` re-enabled back-face culling
  (`SMaterial`'s default) and the depth test, and the static-UI pass could not
  undo it because `setOrthoProjection()` is the only caller of
  `setCull(false)`/`setDepthTest(false)` — so every UI quad was culled away.
  Flipping `BackfaceCulling` alone was measured to bring the UI back, which
  names the wipe exactly. Fixed by borrowing the captured 3D transforms for the
  batch and handing the driver back the state it was found in — the shape
  `drawTargetOutline()` and the portal wireframe batch already use. No pass
  reordering (the G05 golden is untouched). Pinned by
  `tests/test_navmesh_overlay_draw.cpp` (real headless GLES2 + framebuffer
  readback): all three edge probes and the UI probe read `0,0,0` before the fix.
  **The live pair is in `review/T336/`**: same character, same qeynos2 spot, one
  Ctrl+N apart. The ON frame draws green navmesh lines across the ground AND
  keeps the health/mana/stamina bars, FPS counter, chat panel, hotbar and A1-A8
  bar - both halves of the defect gone on a real device, with the client's own
  log showing `Navmesh overlay: ON` and the same 112 triangles T329 measured.
  Compare `review/T329/` for the pre-fix pair. Nothing is owed on this entry.
  Prov: T328 Leg B Review, T333 Review, T336 Review.

## Added 2026-09-04 by the T328 Leg B live session

- ~~**B049** (A) **RE-MEASURED AND CONFIRMED LIVE by T331 (2026-09-04) — the
  writer is NAMED and it is fable, not the server. Ready for a fix unit.**
  `Testchar` was found at `z=11.8806` after being left at `z=3.751` — exactly
  **13 × 0.625**. T311 Leg B (2026-09-02) matched the official client's wire z
  (`groundZ + 0.625*size`) and reported the compounding closed. **It is not
  closed on the graphical path.** T331 measured, on `Testchar` (Human male,
  size 6) standing still at the canonical qeynos2 spot `(-74, 428)`:

  | cycle | fable `--no-graphics` | fable **with graphics** | official Titanium client |
  |---|---|---|---|
  | 1 | 3.75 | **4.38063** | **3.751** |
  | 2 | 3.75 | **5.00563** | **3.751** |
  | 3 | 3.75 | **5.63063** | **3.751** |
  | step | 0 | **+0.625/login** | **0** |

  The oracle is steady to the last digit across three login/camp cycles, so the
  **server applies no transform and is not the writer**.

  **Named writer: `BridgeComponent::OnGraphicsMovement`,
  `src/client/bridge/bridge_component.cpp:717`** — it clears
  `zAwaitingFrameConversion` **without** the paired conversion, on the premise
  (stated in its own comment) that "a z the RENDERER produced is definitively in
  the render frame". False at zone-in: the renderer was *seeded* with the raw
  wire z and echoes it straight back. The `[ZONE-IN] setPlayerPosition`
  zone-load snapshot re-seeds it a second time **after** the model has loaded,
  which is the half T311 did not cover. Discriminator: the correct deferred
  conversion would have produced a render z of `3.12`; **no `3.12` appears
  anywhere in the run log**, and both `setPlayerPosition` and `client_pos` hold
  exactly one distinct value all session.

  ⚠ **The `--no-graphics` path CANNOT exercise this defect** — no renderer means
  no `PlayerPositionChanged`, so the flag is never cleared and the send passes
  through verbatim, accidentally correct. That is why T311's three-login
  regression test is green today. **Any fix must be pinned with a renderer
  present, or by replaying the zone-load snapshot.**

  Closes when: a fix lands and a **graphical** login/camp cycle is measured
  twice in a row with the persisted z unchanged at 3.751. **Needs eqemu.**
  Prov: **T331 Review + `review/T331/README.md`**; T328 Leg B Review; T311.~~

  **CLOSED by T332 (2026-09-04), proven live.** Three graphical login/camp
  cycles of `Testchar`, same spot, same method as T331:

  | cycle | z before | z after | delta | T331's same cycle |
  |---|---|---|---|---|
  | 1 | 3.751 | **3.75** | −0.001 (1/8 spawn quantisation) | 3.75 -> **4.38063** |
  | 2 | 3.75 | **3.75** | **0** | 4.38063 -> **5.00563** |
  | 3 | 3.75 | **3.75** | **0** | 5.00563 -> **5.63063** |

  ⚠ **The writer T331 named was a real hole but NOT the one that fired.** With
  `OnGraphicsMovement` fixed alone the live run still persisted 4.38063. T332
  instrumented the run: `awaiting=false` on the FIRST self position update — the
  pending-conversion flag was never set at all, because on a real graphical
  zone-in NEITHER self-SPAWN edge (the two T311 marked) is ever taken for our own
  character; the spawn id arrives with the PlayerProfile. **The writer is
  `ZoneProcessPlayerProfile` (`src/client/zone/zone_entry.cpp`)**, which stored
  the server's WIRE z raw and never marked the conversion owed. Fixed at all
  inbound self-position edges, with the conversion factored into one function
  (`BridgeComponent::ConvertSelfZToRenderFrame`) that also publishes the
  corrected z onto the bridge — `SetPosition`'s `PlayerMoved` goes to the state
  layer's own EventBus and never reaches the renderer, so the renderer had been
  keeping the seeded wire z. Pinned by `tests/test_zone_in_z_conversion.cpp`
  (7 tests, renderer present — the headless arm structurally cannot fail on
  this). Prov: **T332 Review + `review/T332/README.md`**.

## Added 2026-09-04 by the L12 Review (Batch L close-out)

- **B050** (B) **CLOSED by L13 (2026-09-04): the `owner_.` residue in the
  eleven renderer components is 1,065 -> 151 (-85.8%), and every one of the 151
  is accounted for.** L12 measured 1,065 against its own 300-use STOP and
  boarded the distribution rather than widening the rule on an executor's
  authority. The orchestrator's ruling was NOT to lower the promotion threshold
  to `>= 2` (that renames the god class as a 160-member `RendererContext`) but
  to apply Batch K's Mechanism 3 pairwise: **a member used by two components
  moves to the component that WRITES it, or on a tie to the one with more uses,
  and the other side reaches it through `ctx_.<Owner>().<accessor>()`.**
  Measured with the same comment- and string-filtered census over
  `src/client/graphics/<family>/` + `include/client/graphics/<family>/` for the
  eleven components; L13 reproduced L12's 1,065 and all ten per-family counts
  exactly before editing anything.

  | # | cluster | before | after | outcome |
  |---|---|---|---|---|
  | 1 | the frame-timing blob (`frameTimings_` 200, `frameTimingsAccum_` 135, `frameTimingsSampleCount_` 11, `lastPolygonCount_` 6, `currentFps_` 4, the two EMA cost members, the scene-profile pair, `frameCount_`, `lastFpsTime_`, `frameTimingEnabled_`) | **374** | **0** | ten members to `FrameComponent` (it accumulates), the scene-profile pair to `DiagnosticsComponent` (it writes both, three lines apart). `DiagnosticsComponent` reads the accumulator through `ctx_.Frame().timingsAccum()` |
  | 2 | two-family state, 55 members | **480** | **45** | 50 moved to their owner. The five that stay are `simulationWorker_` 26 (the worker handle -- L07's STOP), `currentFrameSimResults_` 7 and `portalVisibleRegions_` 5 (two of the four SimulationWorker-output pointers `test_simulation_output_lifetime.cpp` guards as a group, T115/T116), `pendingVBOUploads_` 5 (the GPU upload thread's in-flight set) and `forcePvsUpdate_` 2 (a genuine three-way 1-1-1 write tie that the rule cannot break) |
  | 3 | members typed by an `IrrlichtRenderer`-NESTED type | **69** | **0** | the blocker was the TYPE, not the threshold: all four already cleared L12's `>= 3` bar. L13 hoisted `FireGlowLight`, `DoorCollisionData` and `FrameTimings` to `renderer_types.h`, `RenderPassTimer` (a class with a virtual base) to a new `include/client/graphics/render_pass_timer.h`, and `ZoneLightData` to `renderer_types.h` **RENAMED `RendererZoneLightData`** -- measured: a namespace-scope `EQT::Graphics::ZoneLightData` would not be ambiguous, it would silently SHADOW `EQT::ZoneLightData` for unqualified uses inside `EQT::Graphics`, which is worse. Each keeps a `using` alias in the class, so every existing spelling still compiles; the four members then promoted into `RendererContext` (now 54 core + **48** promoted + 11 pointers) |
  | 4 | the two EQE1 mutexes | **12** | **12** | unchanged and deliberate. Relocating the access path of a synchronisation primitive is what Batch L's second STOP forbids in a structural unit; L12 and L13 both declined |
  | 5 | single-component members still on the renderer | **49** | **13** | ten moved to their one component. The six that stay: `pendingGPUResults_` 6 (drained from the GPU upload thread), `softwareCursorTexture_` 2 (a renderer-owned eviction guard binds a REFERENCE to it -- moving the referent makes its validity depend on component teardown order), `backgroundThreadPool_` 2 (a thread-pool handle), `initialized_` 1, `cachedDisplaySettings_` 1 (init/core terminal owners, correction 10), `loading_` 1 (the `std::atomic<bool>` the loading thread sets) |
  | 6 | `IrrlichtRenderer`'s PUBLIC methods, 36 names | **81** | **81** | untouched by design -- the renderer's external API, called by `Application`, `IrrlichtBridge` and `graphical_mode` as well as by components (the K11 rule). Not a defect, and not a target |
  | | **total** | **1,065** | **151** | |

  Per component after L13 -- `rdp` **0**, `zone_stream` 4, `overlays` 7,
  `player` 7, `eqe1` 7, `frame` **9** (was 346), `diagnostics` **11** (was 190),
  `zone_load` 14, `sim` 23, `zone_lifecycle` 32, `slash` 37 (all 37 are public
  methods -- L12 already proved this component private-clean on both census
  axes). `irrlicht_renderer.h` **1,556 -> 1,343**; 72 member declarations moved
  into nine component headers and 4 into the context.

  **NOTHING crossing a thread boundary moved and no synchronisation changed.**
  The remaining 151 are 81 public-method uses plus 70 in the thirteen reasoned
  stays above; there is no unclassified residue, so this entry closes.
  Prov: L13 Review.

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
  **T340 Leg A done, Leg B open** (`refactor/T340_variant_loader_behaviour_test.md`).
  (1) is CLOSED: `tests/test_variant_loader_candidates.cpp` drives the method
  through six of its eleven semantic exits using real shipped S3D archives --
  primary hit, **primary-miss/fallback-hit through the outer `continue`**
  (race 139 ICM -> IKM, Will's all-or-nothing case), an unusable matched
  character skipped, all candidates exhausted + the caller's negative cache
  proved not to re-scan, and an empty candidate list. Seam: the loader's own
  public `setClientPath()`/`loadZoneModels()` control the candidate source
  (a fixture client directory symlinking only the archives a case may see),
  plus one read-only `variantLoadTrace()` getter -- the whole production edit,
  additive, written under the mutex the success blocks already take. Both
  planted `continue` -> `return false` faults were caught, each by exactly one
  case. Measured while there: G07's exits 155/525 are **unreachable with
  shipped data** (no character model in any of the 378 `*_chr.s3d` archives has
  zero parts), and exit 640 -- the global-path twin of the tested 280 -- should
  be added in Leg B before the zone and global blocks are merged. (2) remains
  open.

## Added 2026-09-04 by M00 (`refactor/M00_ceiling_closed.md`)

- ~~**B053** (P) The 1,500-line ceiling was a *soft* limit: `--all` printed WARN
  above it and only the 3,000-line hard cap could fail a run, because two
  single-class headers could not meet 1,500 and were carried on a no-grow
  allowlist (K00 Leg B).~~ **CLOSED IN PLACE by M00 — the milestone, with the
  numbers.** `include/client/eq.h` 2,025 -> **899** (Batch K,
  `refactor/K_component_extraction.md`);
  `include/client/graphics/irrlicht_renderer.h` 1,914 -> **1,343** (Batch L,
  `refactor/L_renderer_component_extraction.md`); Batch J
  (`refactor/J_file_size_reclamation.md`) and Batch G
  (`refactor/G_oversize_methods.md`) took the `.cpp` side. Measured at
  `668801ab`: **866 files, 0 fail, 0 warn — nothing in the tree is over 1,500.**
  `scripts/refactor/file_size_allowlist.txt` is now EMPTY (kept, comment-only,
  so `--all` still parses it) and an unlisted file over 1,500 is a **FAIL** in
  the `FileSizeCeiling` ctest test. Largest survivors:
  `tests/test_eq_commands_equip.cpp` 1,492 (8 lines of headroom — the next file
  to trip the gate), `src/client/graphics/door_manager.cpp` 1,394,
  `tests/test_zone_connection_flow.cpp` 1,367,
  `src/client/spell/spell_manager.cpp` 1,360. See `docs/decisions.md`,
  2026-09-04. **B024's closing sentence ("Both headers hold a no-grow allowlist
  entry that each K unit ratchets down") is superseded by this entry** — M00
  struck nothing per its contract; whoever closes B024 should drop it.

## Added 2026-09-04 by T333

- ~~**B054**~~ (B) **CLOSED — all three struck (T335 + T335b).** `src/client/graphics/overlays/overlays_component.cpp`
  `OverlaysComponent::drawMapOverlay()`, `renderer_frame_render.cpp`'s
  `frustumDebugDraw_` block, and
  `src/client/graphics/player/renderer_player_collision.cpp`
  `PlayerCameraComponent::drawCollisionDebugLines()` — **all three carry
  B048's defect verbatim** and were deliberately left alone by T333 (the
  contract said no behaviour change to any other overlay, and ~~none of the
  three has live evidence~~ — **two of the three now do, see T338 below**). Each runs in the same post-`drawAll()`
  debug-overlay pass, each calls `setMaterial()` with a 3D material and then
  `draw3DLine()` in world space, and none re-sets `ETS_VIEW`/`ETS_PROJECTION`
  from `ctx_.captured3DView`/`captured3DProj` or restores 2D-safe render state
  afterwards. So Ctrl+M (map overlay), `collisionDebug`, and the frustum debug
  draw should each draw nothing AND wipe the 2D UI for the rest of the frame,
  exactly as Ctrl+N did. All three are off by default, which is why nobody has
  reported it. Closes when: each is proven — a test in the shape of
  `tests/test_navmesh_overlay_draw.cpp` — to draw and to leave the following 2D
  pass intact, or is deleted. Prov: T333 Review.

  **T335 (2026-09-04):** the fix is now ONE shared RAII guard,
  `EQT::Graphics::WorldLineBatch` in
  `include/client/graphics/world_line_batch.h`, used by all three fixed sites
  including the navmesh one, which was refactored onto it.
  ~~`drawMapOverlay()` (Ctrl+M)~~ and
  ~~`PlayerCameraComponent::drawCollisionDebugLines()`~~ are **STRUCK** — each
  proven by a new pre-fix failing pixel test in
  `test_navmesh_overlay_draw.cpp`'s shape (`tests/test_map_overlay_draw.cpp`,
  `tests/test_collision_debug_draw.cpp`); both symptoms reproduced pre-fix,
  4/4 after. Note the file for the collision lines is
  `renderer_player_collision.cpp`, not `renderer_camera.cpp` (J33 moved it).
  ~~The `frustumDebugDraw_` block in `renderer_frame_render.cpp`~~ is **STRUCK
  by T335b**, which resolved the golden question T335 STOPped on: an order
  golden pins a SEQUENCE and LEARNS a reviewed edit in place at the positions
  it touches (batch-G correction 32) — it is not a freeze on the TU. The block
  now uses the same guard; the edit is count-neutral (the guard replaces the
  `setMaterial(debugMat)` it supersedes and its restore is a destructor), so
  `tests/goldens/frame_render_order_g05.txt` changed by exactly one line at
  entry 127 and is still 305 entries. `tests/test_frame_render_order.cpp` gained
  a documented `kGoldenSize` naming that edit; both re-plants (drop the guard,
  swap two pass calls) still fail the guard. Proven by
  `tests/test_frustum_debug_draw.cpp` — pre-fix 3 of 4 cases fail, 4/4 after.
  It also turned up a symptom nobody had predicted: the material setup sits
  BEFORE the region loop, so Ctrl+V with an EMPTY box list drew no line and
  still wiped the UI. Prov: T335 Review "Site 4", T335b Review.

## Added 2026-09-04 by the T334 Review (B045/B046)

- ~~**B055** (A) `src/client/core/everquest.cpp` `InitializeSpellSystem` —
  **a vision buff LANDING still does nothing; only its fade is published.**~~
  **STRUCK by T339 (2026-09-04).**
  T334 fixed the consumer, so a `VisionChanged` event now applies correctly in
  both directions — but the ONLY publisher of that event tree-wide is the
  `BuffManager` FADE callback. `BuffManager::applyBuff()` has no apply-side
  callback at all, so casting Ultravision or Infravision on yourself never
  reaches the renderer: `currentVision_` stays at the racial base, and the
  fade callback then publishes the same base again. The feature is therefore
  still end-to-end dead, one hop earlier than B046 said. Not fixed in T334:
  the fix needs an apply/refresh callback on `BuffManager` (or the same
  recompute driven from wherever `applyBuff` is called), which is a
  state-module change beyond a publish and T334's contract STOPped on exactly
  that. Closes when: applying a vision buff raises vision, proven by a test on
  the publisher (and ideally a live capture of the zone-light change).
  Prov: T334 Review.

  **T339 (2026-09-04):** half-refuted and fixed. `BuffManager` already HAD the
  apply-side callback — a `BuffApplyCallback` typedef, `setBuffApplyCallback()`,
  and four live invocations including one at the end of `applyBuff()`. What was
  missing was the REGISTRATION: `InitializeSpellSystem()` called only
  `setBuffFadeCallback()`, so the apply hook fired into a permanently-null
  `std::function` and its only callers tree-wide were in
  `tests/test_buff_manager.cpp`. No state-module change was needed after all.
  Both hooks now share ONE body over ONE recompute,
  `BuffManager::computeVisionType(exclude_spell_id)` (the old inline fade loop,
  moved onto the class it already read). Precedence is BEST VISION WINS
  (Ultravision > Infravision > none) and is OURS, not the oracle's — `docs/`
  records nothing about it; stated in the header comment. A second defect fell
  out: three of the four fade paths notify BEFORE erasing, so the old fade loop
  counted the departing buff as still up — hence the exclusion parameter.
  `tests/test_vision_buff_fade_wire.cpp` 3 -> 11 tests, driving a real
  `BuffManager` over the real spell DB into the renderer, plus a source-reader
  pin on the registration (pre-fix failing run in the T339 Review; `EverQuest`
  is not constructible in a unit test, so that half has no runtime seam).
  A live capture of the zone-light change is still owed if Will wants it.
  Prov: T339 Review.

  **T338 (2026-09-04): the live pair is captured, and B054 needs nothing
  further.** `review/T338/` holds a baseline plus both keyed toggles at tip
  `03239def` (T335 + T335b on it), same character, same qeynos2 spot, same
  forced 11:00 daylight, one keypress apart. **Ctrl+M**: `Map overlay: ON` then
  `Map overlay: Updated with 151 triangles` in the client's own log, magenta
  map-overlay wireframe drawn, and the whole 2D UI still present (bars, FPS,
  chat, hotbar, A1-A8). **Ctrl+V**: `Frustum culling: ON + DEBUG DRAW`, green /
  blue / yellow region boxes drawn, same UI intact. `collisionDebug` has no key
  binding and stays pinned by `tests/test_collision_debug_draw.cpp` only.
  Visual judgement is Will's; the claim made is only that the lines and the
  listed UI elements are present. Prov: T338 Review, `review/T338/README.md`.
