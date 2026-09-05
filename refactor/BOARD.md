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
  verbatim. ~~Still owed, and NOT this unit: the official client's full death
  sequence — C→S Lying appearance, no further `OP_ClientUpdate` while dead,
  then an `OP_ZoneChange` re-entry (shape in the T344 Review).~~ **REMAINDER
  CLOSED by T346** (`refactor/T346_self_death_sequence.md`): the Lying
  appearance is a dedicated `SendSpawnAppearance(AT_ANIMATION, ANIM_LYING)` in
  `ZoneProcessDeath`'s self branch (`SetPositionState`'s `POS_DEAD` arm stays
  empty — it is also the debug action's path, where we are not dead); the
  silence is a `POS_DEAD` gate at the top of
  `MovementComponent::SendPositionUpdate()`, the one function every C→S
  `OP_ClientUpdate` funnels through, released in `ZoneProcessPlayerProfile`
  so a respawned character talks again; the bind re-entry needed NO change —
  `ZoneProcessZonePlayerToBind` already called `RequestZoneChange`
  unconditionally, same zone included, which the replayed capture confirmed
  before any edit. Pinned by `tests/test_self_death_sequence.cpp` (9 tests)
  against `tests/goldens/self_death_outbound_g07.txt`, extracted from the
  T311 capture by `scripts/refactor/t311_death_golden.py`. Live proof still
  owed. Prov: deferred item 15.

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
  the fix) and golden statements 27-28. ~~**⚠ LIVE PROOF OWED** — T342 held the
  eqemu lock.~~ **LIVE PROOF DONE (T345, 2026-09-04, `review/T345/`):** one Num
  Lock tap, nothing held — `action=ToggleAutorun -> actionQ=1, bridgeQ=0` then
  `Autorun ON` in the client's own log, and the renderer's per-frame position
  line (emitted only on a real change) went 0 -> 164 -> 347 lines while the
  player walked `(306.1, 223.0)` -> `(171.9, 403.7)`. A single W tap stopped the
  stream dead at 17:56:05.006 (351 lines) and it stayed dead for 16 s. Five PNGs.
  Prov: deferred item 10, T343 Review, T345 Review.

- ~~**B003** (A) auto-attack — 12 live runs with `/target` and `/attack` both
  confirmed in the log produced no swing.~~ **CLOSED by T350, NOT FABLE.**
  Measured live at `920280dd` (qeynos2, Testchar, `a_fire_beetle` 2024, three
  phases, `review/T350/`). The client DOES send `OP_AutoAttack`(on); eqemu
  refuses the swing because the player is not FACING the target, in the one
  branch of `Client::Process` that sends nothing at all — no packet, no
  message, no log (`/mnt/projects/akk-stack/code/zone/client_process.cpp:430`, `else if (!los_status ||
  !los_status_facing) { }`; the gate is `Mob::IsFacingMob`, `/mnt/projects/akk-stack/code/zone/mob.cpp:7599`,
  ±80 of 511.5 ≈ ±56°). Nothing in fable's `/target`/`/attack` path faces the
  target, and the official client does not face for you either, so there is no
  divergence. **The decisive measurement**: auto-attack held ON, target held,
  heading swept through 8 points with `/face <x> <y> <z>` — 17 `OP_Damage`
  packets and 17 queued melee animations arrived in the two windows pointing at
  the mob (server heading 135.02° and 180.04°; the mob was at 180.04°), zero in
  the other six. The archived runs failed because `#npctypespawn` drops the mob
  ON the invoker: at 0.04 units the bearing is 255.75 against a stored heading
  of 160. **The heading pipeline is CORRECT and is refuted as a suspect**:
  `character_data.heading=160` → client `337.50deg` → `heading_scaled=640` →
  eqemu `EQ12toFloat(640)=160`, exact, every sample; the transform is
  `server_deg = (450 − client_deg) mod 360`.

- ~~**B003b** (A) `ActionDispatcher::faceEntity` — `/face <name>` resolved a
  DIFFERENT entity than `/target <name>`.~~ **CLOSED by T350** (found while
  measuring B003). `action_dispatcher.cpp:99` used the unranked
  `EntityManager::findEntityByName(name)`, which ties on the LOWEST SPAWN ID,
  while `/target` uses T244's ranked overload, which ties on DISTANCE. Measured:
  with two `a_fire_beetle`s in qeynos2, `/target` took spawn 277 at 3.5 units
  and `/face` took spawn 68 at 550 units, turning the player 131° away from the
  target (client heading 138.92°, matching spawn 68's bearing to two decimals) —
  after which the server silently refuses every swing. Fixed with the ranked
  overload + `ActionDispatcherTest.FaceEntity_PicksTheNearestMatch_NotTheLowestSpawnId`,
  verified to fail on the unpatched source.

- ~~**B003c** (B) `ActionDispatcher::moveToEntity` / `followEntity`
  (`src/client/action/action_dispatcher.cpp:154,167`) call the same unranked
  `findEntityByName(name)` T350 fixed in `faceEntity`, but only as an existence
  check before passing the NAME to the handler — so the tie-break is not
  observable *there*, and the question is what the handler does with the name.
  Closes when: the handler's own resolution is read and either shown to rank by
  distance too, or made to. Prov: T350.~~ **CLOSED by T356 `287ecbb7`.** The
  handler does NOT rank: `EqActionHandler::moveToEntity/followEntity` forward
  the name to `MovementComponent::MoveToEntity/Follow`, which resolved it with
  a THIRD resolver — `EntitiesComponent::FindEntityByName`
  (`src/client/entities/entities_component_appearance.cpp:326`), an unranked
  case-insensitive PREFIX match over the spawn-id-keyed map, so the lowest
  spawn id among matches wins. So the defect was live and worse than /face's:
  three resolvers, two of them unranked. Fixed by resolving ONCE in the
  dispatcher with the ranked overload and passing the winner's SPAWN ID down
  (`IActionHandler::moveToEntity/followEntity` now take `uint16_t`, the T230
  rule). Pinned by
  `ActionDispatcherTest.MoveToAndFollow_PickTheNearestMatch_NotTheLowestSpawnId`,
  verified to fail on the unranked lookup (picked spawn 68, expected 277).

- **B003d** (B) `src/client/entities/entities_component_appearance.cpp:326`
  `EntitiesComponent::FindEntityByName` — the unranked case-insensitive PREFIX
  resolver T356 took out of moveTo/follow still backs `MoveToEntityWithinRange`,
  `StartCombatMovement(name)` and `UpdateCombatMovement`, so combat movement
  keeps the lowest-spawn-id pick; and `MovementComponent::Follow` stores its
  target as a NAME and re-matches it by exact string every tick
  (`src/client/movement/movement_update.cpp:183`), so following one of two
  identically-named mobs is ambiguous per tick. Closes when: combat movement
  resolves once through the ranked lookup and passes a spawn id (the T230/T356
  rule), Follow tracks the spawn id, and the resolver has no callers left (then
  delete it). Prov: T356 Review.

- ~~**B004** (A) `include/client/graphics/ui/ui_layout.h` `centerPopup` — `popW`/
  `popH` are hardcoded 400/350 at every resolution; the archive records general
  and equipment slots drawn outside that frame and unreachable by mouse.~~
  **REFUTED by T351**: measured at `5601a3ed` across 640x480, 800x600 (the
  default and the orangepi preset's display), 1024x768, 1280x1024 and
  1920x1080 — all 30 slot rects, plus the T60 paperdoll rect, lie wholly
  inside the frame at every one, and the centre of every slot rect
  hit-tests back to its own index through `StaticUIInput::processInput()`.
  The frame itself is wholly on screen at all five. The archive's finding
  was real but is the defect **T57 fixed**: both the renderer and the
  hit-test now read `InventoryLayout::slotRect()`, whose geometry is
  relative to the popup's own origin and width, so containment cannot vary
  with resolution — the hardcoded 400x350 is not itself a defect. The
  per-resolution table is now a test
  (`InventoryPopupFrameT351.EverySlotIsInsideTheFrameAndHitTestableAtEveryResolution`
  in `tests/test_static_ui_panels.cpp`), because every pre-existing
  containment test ran only at the fixture's single 800x600 layout and so
  could not tell "inside at 800x600" from "inside everywhere". No code
  changed. Prov: archived board, "static-panel UI surface still untested
  after T54"; `refactor/T351_inventory_popup_frame.md` Review.

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

- ~~**B007** (B) `include/client/skill/skill_data.h` `SkillData::endurance_cost` /
  `mana_cost` — declared, defaulted 0, and assigned nowhere in the tree, so both
  of `SkillManager::getSkillUseError()`'s resource refusals are unreachable and a
  skill that should fail for endurance proceeds.~~ **CLOSED 2026-09-04 by T353,
  by DELETION.** The premise "a skill that should fail for endurance" is false:
  **no skill has a resource cost in this era, on either side of the wire.** Every
  eqemu handler for every opcode fable sends for a skill was read
  (`/mnt/projects/akk-stack/code/zone/client_packet.cpp`, plus
  `special_attacks.cpp:325` `Client::OPCombatAbility` for the `OP_CombatAbility`
  family) — not one reads or spends endurance or mana; their only refusals are
  timers, range and zone rules. Server-wide, endurance is spent at exactly one
  opcode handler (`client_packet.cpp:9765`, `Handle_OP_Jump`), by **spell**
  `endurance_cost` (`effects.cpp:900`) and by a damage-absorb bonus
  (`attack.cpp:3829`). No client-side skill-cost table exists to load from, so
  the official client cannot refuse locally either and no class C is owed.
  Deleted: both fields, both refusal branches, `canUseSkill()`'s resource gate,
  the `requires_behind` conjunct that ranked endurance above facing, and
  `SkillManager::checkResourceRequirement()` with its declaration — plus
  `SkillGateTest.T159_ABackstabShortOfEnduranceBlamesEnduranceNotFacing`, which
  reached the branch only by writing 1,000,000 through the mutable accessor.
  Facing is now the sole producer of "You must be behind your target", pinned by
  `SkillGateTest.NEGATIVE_BackstabIsRefusedFromTheFrontOfTheTarget`
  (`tests/test_skill_gates.cpp:405`). Full enforcement table with absolute-path
  citations: `refactor/T353_skill_costs.md` § Review. Prov: deferred item 35.

- ~~**B008** (B) `src/common/net/daybreak_connection_send.cpp`
  `DaybreakConnection::ValidateCRC` — its `default:` arm returns false with no log
  line, silently rejecting every packet for any `crc_bytes` other than 2 or 4.~~
  **CLOSED 2026-09-04 by T347.** The arm is REACHABLE, not dead:
  `daybreak_connection_decode.cpp`'s `OP_SessionResponse` handler assigns
  `m_crc_bytes = reply.crc_bytes;` straight off the wire, so the server picks the
  width. The arm now logs a `LOG_WARN` naming the value. Pinned by
  `DaybreakCrcCaptureTest.UnsupportedCrcBytesIsLoggedNotSilent`
  (`tests/test_daybreak_crc_capture.cpp`), which drives the real recorded
  handshake, forces `crc_bytes = 3`, and asserts both that a supported width
  stays quiet and that the rejection names `crc_bytes=3`. Prov: deferred item 41.

- **B009** (B) `tests/test_daybreak_connection_pcap.cpp` — reads `connect_code`,
  `encode_key` and `max_packet_size` raw off the wire, which is exactly the bug
  T21 fixed in production; it cannot fail if that fix regresses, and passes today
  only because eqemu's key is a byte-swap palindrome. Closes when: the test reads
  through the production path. Prov: deferred item 40.

- ~~**B010** (B) `tests/test_irrlicht_camera_command.cpp`
  `CameraCommandRoundTripTest` — no `PrintTo` for the parameter type, so gtest
  names each case by hex-dumping an ASLR-randomised pointer and the eight ctest
  names are not stable identifiers. Closes when: a `PrintTo` or name generator
  gives stable names. Prov: deferred item 42.~~ **CLOSED by T356 `fe812e8d`**:
  a `CameraModeCaseName` generator (4th `INSTANTIATE_TEST_SUITE_P` argument,
  derived from the case's own `setArg`) plus a `PrintTo`. The eight names are
  now `.../SetThenReadRoundTrips/{Free,Follow,First_Person}` and
  `.../{free,FOLLOW,firstperson,fp,first_person}`, and two
  `--gtest_list_tests` runs of the built binary are byte-identical (they
  differed on all eight lines before).

- **B011** (B) `src/client/graphics/entity_size.cpp`
  `EntityRenderer::setEntitySize` — updates neither `collisionHeight` nor `deckZ`,
  so a resized boat's deck surface goes stale. Nothing in the game resizes a
  boat, so this is a documented limit, not a live defect. Closes when: both are
  recomputed, or the limit is asserted in a test. Prov: deferred item 38.

- ~~**B012** (B) `src/client/action/action_dispatcher.cpp`
  `ActionDispatcher::clearTarget` — reachable only from a keybinding; no command
  registration exists in `command_processor_registration.cpp`, so a `--script`
  run cannot clear a target.~~ **CLOSED 2026-09-04 by T347.** `/cleartarget`
  (no alias, category Combat) registered in `registerCombatCommands()`
  immediately after `/target`, routing through `cmdClearTarget` to the same
  `ActionDispatcher::clearTarget()` the `ClearTarget` keybinding uses. G03's
  order golden `tests/goldens/command_registration_order_g03.txt` learned the
  insertion IN PLACE at entry 022 (count 142 → 143, recorded in the golden
  header and in the test's size guard); both faults re-planted (drop the
  registration → count clause fails 143→142; swap `registerCombatCommands()`
  with `registerSocialCommands()` → position clause fails at 21). Tests:
  `CommandProcessorTest.T347_ClearTargetIsRegisteredInTheCombatCategory` and
  `.T347_ClearTargetDispatchesToTheHandler`. Prov: archived board, T39
  dead-state follow-ups.

- ~~**B013** (B) `src/client/graphics/slash/renderer_slash_commands.cpp`
  `/newskills` — the only working skills-window toggle, registered in neither
  command registry, so it is invisible to `/help` and to tab completion and
  reachable only by a user who already knows the word.~~ **CLOSED 2026-09-04 by
  T347 as STALE — measured, no code change.** Three claims, checked:
  (1) *invisible to `/help`* — FALSE since T223, which added registry 3's static
  catalog: `src/client/help/renderer_command_catalog.cpp:62` carries
  `{"newskills", {}, "Toggle the skills window"}`, and
  `MergedHelpTest.T223_NewSkillsIsFoundByHelp` /
  `.T223_OverviewListsGraphicsCategoryWithTag` pin that `/help newskills` and the
  `/help` overview both find it.
  (2) *invisible to tab completion* — a PHANTOM: `getCompletions()` has ZERO
  production callers in either registry (`CommandProcessor::getCompletions`,
  `CommandRegistry::getCompletions` — definitions and tests only), so nothing is
  tab-completable today. Boarded separately below as B053.
  (3) *unreachable except by a user who knows the word* — FALSE: `--script` and
  piped stdin both run `Application::DispatchSlashCommand`, whose registry-3
  stage is the renderer bridge (T29), so `/newskills` is scriptable.
  What is LEFT is a naming/design choice, not hygiene, and is boarded as B058:
  `/newskills` and stdin's bare `skills` keyword
  (`console_input_handler.cpp:432` → `InputAction::ToggleSkills` →
  `renderer_frame_input.cpp:222`) toggle the SAME `skillsPopupState_.isOpen` but
  differ — `/newskills` also closes the spellbook and the inventory popup and
  echoes to chat; the keyword does neither — and `/skills` WITH a slash is
  unknown to every registry. Prov: archived board, T143; measured T347.

- ~~**B014** (B) `src/client/graphics/eq/race_model_loader.cpp`
  `RaceModelLoader::preloadModelData` — calls
  `ConstrainedTextureCache::getOrLoad()` with an empty buffer whenever a JSON
  manifest exists but no EQE1 archive is configured: one guaranteed-to-fail call
  per composite name.~~ **CLOSED by T357 Leg C**
  (`refactor/T357_tab_completion_spell_land_eqe1_gate.md`, commit `200180ba`):
  the call is gated on `constrainedTextureCache_->getConfig().eqe1Path.empty()`
  — the SAME predicate every other EQE1 site in the tree reads
  (`renderer_zone_load.cpp`'s "Step 0: EQE1 … (if configured)",
  `renderer_init.cpp`, `eqe1_archive_component.cpp`), reached through the
  cache's own config copy, so no new plumbing and no second predicate. It is
  deliberately NOT gated on the loader set being non-empty: T284/T207 open packs
  lazily and a composite sheet's pack is meant to still be closed here — the
  miss is what drives `eqe1ArchiveResolver_` → `openArchiveContaining()`.
  Pinned by `Eqe1PreloadGate.CompositeSheetLookupIsGatedOnEqe1BeingConfigured`
  (`tests/test_constrained_texture_cache.cpp`), a source-reader pin next to
  T92's diagnostic test — reaching the composite branch needs a real S3D model,
  a real manifest and the shipped EQ tree, so a counted assertion would
  `GTEST_SKIP` on this host. Prov: archived board, T92.

- **B015** (B) `src/client/audio/midi_player.cpp` `MidiPlayer::loadSoundFont` —
  swaps a raw `tsf*` with no atomic or mutex; safe today only because its single
  call site runs before the audio thread exists, which the file's own comment
  records. Closes when: the swap is synchronised or the single-call-site
  precondition is asserted. Prov: archived board, T131.

- ~~**B016** (B) `src/client/graphics/eq/` `RaceModelLoader::ensureModelDataLoaded`
  — ~20 unlocked `loadedModels_` reads across four TUs.~~ **CLOSED by T352**,
  and the premise was half wrong. TSAN at `210a9128` reported **zero** races on
  `loadedModels_`: the unlocked reads are 25, all in `ensureModelDataLoaded()`
  alone (T113 had already locked every access in the three sibling TUs), and
  every writer is main/loading-thread, so they never collide — a documentation
  defect, not a threading one. The self-deadlock hazard is confirmed, which is
  why they stay as they are. What TSAN *did* report, **3 of 4 runs with both
  stacks**, is the neighbouring `RaceModelLoader::getMergedTextures()`
  (`mesh_building.cpp`, no lock at all) racing `storeOtherChrCacheLocked()`
  (`race_model_loader.cpp`) on the EntityPrepWorker thread — reached from
  `ensureModelDataLoaded()`. Fixed with one `lock_guard` on the
  `otherChrCacheMutex_` the writer already holds; 6 of 6 post-fix runs clean.
  `review/T352/`. The residuals are B062 and B063. Prov: archived board, T114.

- **B064** (B) `src/client/graphics/eq/mesh_building.cpp`
  `RaceModelLoader::getMergedTextures` — T352's lock covers `otherChrCaches_`
  and `mergedTexturesCacheValid_`, but the same function also reads
  `globalTextures_`, `numberedGlobalTextures_`, `armorTextureCache_` and
  `zoneTextures_`, which other loader paths mutate without
  `otherChrCacheMutex_`. **Unconfirmed as a live race** — TSAN reported none of
  them in any T352 run; the driver never mutated them concurrently. Confirming
  needs a run that loads a zone (a `#zone` tour under TSAN) while the prep
  worker is live. Closing it is a restructure, not a lock. Prov: T352 Review.

- **B065** (B) `src/client/graphics/eq/model_loading_chr.cpp`
  `RaceModelLoader::loadModelFromCachedChr` — writes
  `otherChrCaches_[lowerFilename]` directly instead of through
  `storeOtherChrCacheLocked()`, the LRU-accounting bypass T91 was written to
  eliminate, surviving at this one site. Under `otherChrCacheMutex_`, so not a
  race: the entry never enters `chrCacheLruOrder_` and can never be evicted,
  while still counting against `maxChrCacheEntries_`. Prov: T352 Review.

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
  ~~`RaceModelLoader::loadModelFromGlobalChrWithVariantsForAnimation` 815
  (`variant_loading_animation.cpp`)~~ **done, T340 Leg B: 132 lines of
  candidate loop plus four functions, see the closed B052**, `IrrlichtRenderer::updatePlayerMovement` 806
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

- ~~**B029** (B) `src/client/movement/movement_state.cpp` `MovementComponent::m_water_surface_z`
  — written on every water-state change and read by nothing; swim height has
  no consumer.~~ **CLOSED 2026-09-04 by T347: deleted.** The member had four
  occurrences tree-wide (its declaration, the local `waterSurfaceZ` that fed it,
  that local's one assignment in `UpdateWaterState()`'s OnSurface arm, and the
  store) and zero readers. `pushEvent` checked first, as the entry asked:
  `SwimmingStateChangedData` (`include/client/state/event_bus.h:702`) carries
  `{isSwimming, swimSpeed, isLevitating}` and no height, so the value reached no
  consumer by that route either. Member, local and store deleted; the OnSurface
  arm keeps a comment saying add a reader before adding the estimate back.
  Pinned by `MovementWaterStateGuard.NoUnreadWaterSurfaceHeightIsStored` and
  `.SwimmingEventCarriesNoSurfaceHeight` (`tests/test_movement_component.cpp`) --
  source guards, since a deleted member has no runtime handle; planted fault
  (re-declare the member) fails the first. Prov: K07 Review.

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
  fix on all three target assertions).
  **T345 (2026-09-04) — live proof done, and one T343 wording correction.**
  `review/T345/`: `/move 728 226 3.12`, then `/stop` typed 12 ms after the last
  `[MOVEMENT]` line, at waypoint 32 of 43 — and no `[MOVEMENT]` line for the
  next 58 s. Three `/loc`s spread over 44 s all read `642.4, 141.8, 5.1` against
  a destination of `728, 226`; they are legible in the chat panel of
  `T345_stop_stop_settled_1024x768.png`. **Correction:** T343's Review calls
  `stopAllMovement()` "the `/stop` command". It is not — `/stop` is an ALIAS of
  `stopfollow` (`command_processor_registration.cpp:556`) and runs
  `cmdStopFollow` -> `stopFollow()` -> `StopFollow()` -> `StopMovement()`;
  `stopAllMovement()` is reached only from `console_input_handler.cpp:412`, and
  `--script`/piped stdin hit the alias too. Both routes end at `StopMovement()`,
  so the captured behaviour is the behaviour T343 describes.
  Prov: K07 Review, T316, T319, T343 Review, T345 Review.

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

- ~~**B042**~~ **CLOSED 2026-09-04 by T354 — DELETED.** Re-measured zero callers
  two ways (comment-filtered grep; declarations and definitions removed, full
  build exit 0) and no `pushEvent` in the body (K12 ff). The draw body, its
  declaration, the three members `inZoneLine_`/`zoneLineTargetZoneId_`/
  `zoneLineDebugText_` and the per-frame call all went; the G05 order golden
  learned the deletion IN PLACE at entry 279 with `kGoldenSize` 305 -> 304 and
  both faults re-planted. `renderZoneLineOverlays()` survives for the live
  bounding-box labels. The closure condition below was "the draw body and its
  three members go (it is not wanted)" — that arm is the one taken: a developer
  aid with no official-client oracle. Prov: `refactor/T354_dead_renderer_paths.md` Review.
  ~~(B) `EQT::Graphics::OverlaysComponent::drawZoneLineOverlay` — **the zone-line debug
  border can never turn on.**~~ It is called every frame from
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

- ~~**B047**~~ **CLOSED 2026-09-04 by T354 — DELETED, and the comment corrected.**
  The measurement L05 asked for, taken twice: a comment-filtered grep put every
  one of the five at exactly two non-comment hits (its own declaration and its own
  definition), and deleting both, then building, gave exit 0 with zero errors —
  no compile error on a declaration, no link error on a definition. No `pushEvent`
  in any body (K12 ff). So all five are gone, 97 lines. The T335 pixel-test seam
  is untouched (`test_collision_debug_draw.cpp` uses
  `seedCollisionDebugLineForTest()`/`collisionDebugLineCount()`, never
  `clearCollisionDebugLines()`), and `extractWorldTriangles()` stays — it still has
  two live callers. **The gap is NOT erased with the API:** the false comment now
  states that collision is built ONCE, by `setupMinimalZoneCollision()` at load
  step 9 walking `ctx_.regionMeshNodes` in a single pass with objects added inline
  in the same step, and that anything streamed in after step 9 still gets no
  collision. A second stale comment in the same file (why `updateAbsolutePosition()`
  is needed) now names `extractWorldTriangles()` instead. `getFirstPersonVerticalFov()`
  was NOT in scope and is untouched. Prov: `refactor/T354_dead_renderer_paths.md` Review.
  ~~(B) **the incremental collision-registration API has never run, and a comment
  claims it does.**~~ `addRegionToCollision()`, `addDoorToCollision()`,
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

- ~~**B051** (B) `src/client/application.cpp` `Application::initConstrainedRendererConfig`
  — `customMemorySpec` is write-only (set in one preset arm, never read).~~
  **CLOSED 2026-09-04 by T347: deleted.** Two occurrences tree-wide (the
  declaration and the one `= true`), zero reads. Deleted rather than read,
  because there is no downstream decision for it to make: BOTH arms of the
  enclosing if/else call `builtConfig.loadJsonOverrides(config.constrainedPreset,
  "config/constrained_presets.json")` identically afterwards, and each arm
  already logs which path it took. G04's order golden
  `tests/goldens/application_init_order_g04.txt` learned the deletion IN PLACE
  at entries 163 and 166 (count 316 → 314, recorded in the golden header and in
  the test's size guard); both faults re-planted (drop
  `m_graphicsInitialized = false;` → count clause fails 314→313; swap
  `initConstrainedRendererConfig` with `initRendererAndGraphics` → position
  clause fails at statement 162). Prov: G04 Review.

## Added 2026-09-04 by the G07 STOP

- ~~**B052** (B) `src/client/graphics/eq/variant_loading_animation.cpp`
  `loadModelFromGlobalChrWithVariantsForAnimation` — 815 lines that were NOT
  phases but DUPLICATION.~~ **CLOSED 2026-09-04 by T340 (Legs A and B,
  `refactor/T340_variant_loader_behaviour_test.md`).** (1) The behaviour test:
  `tests/test_variant_loader_candidates.cpp`, six cases on real shipped S3D
  archives, driving seven of the eleven semantic exits -- primary hit,
  **primary-miss/fallback-hit through the outer `continue`** (race 139
  ICM -> IKM, Will's all-or-nothing case), an unusable matched character
  skipped on the zone path AND on the global path (exit 640, Leg B), all
  candidates exhausted + the caller's negative cache proved not to re-scan,
  and an empty candidate list. Seam: the loader's own public
  `setClientPath()`/`loadZoneModels()` control the candidate source (a fixture
  client directory symlinking only the archives a case may see), plus the
  read-only `variantLoadTrace()` getter. (2) The de-duplication:
  **815 -> 132 lines of candidate loop plus four functions** --
  `buildCodesToTry` 26, `selectVariantParts` 68, `mergeAnimationsFrom` 36
  (the four copies, which were byte-identical once normalised, so nothing
  needed parameterising), `tryBuildVariantFrom` 226 (the character body,
  shared by both paths, `fromGlobal` selecting only log wording and the
  trace's source label); TU 882 -> 576. The exit census is identical
  before and after -- same eleven semantic exits, same exits per test case --
  and a planted `continue` -> `return false` at the candidate loop still
  fails the fallback case alone. Measured while there, and recorded in the
  Leg B Review: G07's exits 155/525 are unreachable with shipped data, and
  so is "exit 640 then a success" (no archive holds an unusable-then-usable
  pair for any race code, and no `fallback_code` race's primary code is ever
  unusable), which is why exit 640 is pinned by a skip COUNT rather than by
  a return value. Original text: "one `for (codeToTry …)` candidate loop
  whose six `continue`s mean 'next candidate' and whose successes `return`
  from three loops deep; the animation-merge body appears FOUR times
  (0.92–0.96 similar) and the zone/global character blocks are 0.91 similar.
  No test drives this method. Prov: G07 Review."

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
  ~~**STRUCK by T339 (2026-09-04) — then PARTLY REOPENED by T345's live run: the
  hook is registered and correct, but nothing in a live session can reach it.**~~
  **CLOSED by T348 (2026-09-04): the producer exists. See the T348 note at the
  end of this item.**
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
  ~~A live capture of the zone-light change is still owed if Will wants it.~~
  ~~**⚠ T345 (2026-09-04): THE LIVE CAPTURE WAS ATTEMPTED AND FAILED — THE HOOK
  IS UNREACHABLE LIVE. This half of B055 REOPENS.**~~ **Fixed by T348 below.** `review/T345/`: at forced
  22:00 night, `#castspell 46` lands (both `SpellAction` packets, `effect_flag=0`
  then `4`), and across six frames — before, twice after, after a re-cast, after
  a `#zone`, after `#nukebuffs` — the zone light never changes and neither
  `Vision buff changed` nor `Vision upgraded to:` is ever logged. Measured cause,
  one hop upstream of T339's fix: `PlayerComponent::ZoneProcessBuff`
  (`player_buffs.cpp:257`) calls `BuffManager::setPlayerBuff()` — the only
  production caller that fires the apply callback — ONLY on an `OP_Buff` with
  `effect_type == 2 && fade != 1`, and **eqemu never sends one**: every
  `[SPELL] Buff:` line in the session carries `fade=1`, and server-side the only
  sender of `bufffade = 0` (`Client::SendBuffDurationPacket`) is gated on
  `zone->BuffTimersSuspended()` (`spell_effects.cpp:3866`). The one route that
  DOES deliver a landed buff — the PlayerProfile at zone-in, `zone_entry.cpp:684`
  -> `BuffManager::setPlayerBuffs()` (plural) — fills `m_player_buffs` directly
  (`buff_manager.cpp:93`) and does NOT fire the callback, so a re-log with the
  buff up publishes nothing either. T339's registration and tests are correct;
  what is missing is a PRODUCER. Closes when a vision buff landing in a LIVE
  session changes the zone light. Rig notes for whoever takes it: `#castspell 46`
  needs no class/level/gem (`AccountStatus::Guide`, self-target), but Testchar's
  account is flagged `invulnerable=1` and an invulnerable client REFUSES its own
  `#castspell` — `/say #set invulnerable off` first, and restore it.
  Prov: T339 Review, T345 Review, `review/T345/README.md`.

  **T348 (2026-09-04): CLOSED in code; the live pixel proof is owed and BLOCKED.**
  The landing sequence was MEASURED from `review/T345/logs/live_session_vision.log`
  before a line was written. A self-cast that lands produces `ManaChange`, then
  `SpellAction effect_flag=0`, then `SpellAction effect_flag=4` — and on a re-cast
  the server slips two `fade=1` `OP_Buff` packets (the old copy, then the 0xFFFFFFFF
  slot clear) BETWEEN the two. **There is no `OP_Buff` with `fade=0` anywhere in
  any of the five sessions**, confirming T345: `MakeBuffFadePacket` is the only
  `OP_Buff` eqemu sends a Titanium client. So the buff list was never updated on
  landing at all — `setPlayerBuffs()` (plural, the PlayerProfile) is a later,
  separate delivery, not the landing. The producer therefore went on the
  **`OP_Action` arm**: `CommandsComponent::ZoneProcessAction` calls the new
  `PlayerComponent::NoteBuffLandedOnPlayer()` (`player_buffs.cpp`) when
  `type == 231 && effect_flag == 4 && target == MySpawnId()`. It uses
  `BuffManager::applyBuff()` — existing public API, **no state-module change** —
  because the Action packet carries no slot; the duration is computed with
  `SpellData::calculateDuration(caster_level)` for the same reason. Gated on
  `isBuffSpell()` (beneficial AND with a duration): `effect_flag == 4` is sent for
  a RESISTED detrimental spell too, so admitting those would post debuffs that
  never landed. `tests/test_buff_landing_producer.cpp` (7 tests) replays the
  measured packets through a real `EverQuest` and asserts on the `VisionChanged`
  event T339's REAL registered hook publishes into a recording bridge — the whole
  chain, wire to renderer. Pre-fix: 3 of 7 failed, pasted in the T348 Review.
  ~~**Still owed: the live capture.** The eqemu lock was held by T350
  (`/mnt/projects/fable/.eqemu-lock` = `T350 2026-09-04`) for the whole unit, so
  no session was run and `review/T348/` does not exist.~~ **LIVE-PROVEN by T358
  (2026-09-05), `review/T358/`.** At forced 22:00 in qeynos2 a `#castspell 46`
  produces the whole chain in one millisecond -- `SpellAction effect_flag=4` ->
  `Buff landed (OP_Action): spell=46 caster=282 level=20 ticks=360 slot=0`
  (T348's new producer line) -> `Vision buff changed (spell 46), recomputed
  vision = 2` -> `Vision upgraded to: Ultravision` -- and `#nukebuffs` produces
  the mirror (`recomputed vision = 0`, `Vision reset to base: Normal`). The buff
  reaches the buff bar too (`35m` box on screen, matching the 360 ticks).
  **The zone light changes, and only the VIDEO proves it**: four stills spread
  2-3% against a 1.3% still-to-still noise floor and said "no change", while the
  260-frame capture stepped frame by frame gives 0.017618 (59 frames before) ->
  0.018485 (84 frames buffed, +4.9%) -> 0.017392 (77 frames after the fade), both
  edges landing on the log lines to within one frame. Two defects fell out of the
  same measurement and are B066 (the vision->light switch is SWAPPED: the landing
  applies Infravision's 0.75 + red shift, measured as R +5.5% / B +0.4%) and
  B067 (vision reaches only 218 data-only point lights, never the global
  ambient -- which is why full Ultravision is worth 5% and the day/night cycle
  is worth 4x). Neither blocks B055: the producer works and is reached.
  Prov: T348 Review, T358 Review, `review/T358/README.md`.

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

## Added 2026-09-04 by T345 (owed live proofs)

- ~~**B056** (A)~~ **CLOSED by T348 (merged 8bad5632): `createEntity()` had zero production callers; `setPlayerRace` is now called from `IrrlichtRenderer::registerEntity()` on the live path and pinned by test; the race table itself is B060.** (A) `src/client/graphics/player/renderer_camera.cpp`
  `PlayerCameraComponent::setPlayerRace` — **lead, not yet a proven zero-caller.**
  The method logs `Player race {} -> base vision: {}` at INFO on every call, and
  that line appears **nowhere** in any of T345's five live session logs, although
  the player entity is created in each of them
  (`T105 ENTITY spawn=287 race=1 ... isNPC=false isPlayer=true`). Its only call
  site tree-wide is `IrrlichtRenderer::createEntity()`
  (`src/client/graphics/renderer/renderer_entities.cpp:29`), while the live zone-load path
  builds entities through `src/client/graphics/zone_load/renderer_zone_load_entities.cpp`.
  If that holds, `baseVision_` is never set from the player's race in a real
  session, so the racial vision floor (Dark Elf Ultravision, Dwarf/Troll/Ogre
  Infravision) is dead too — the same implemented-but-unreached shape as B002 and
  B055, and a second reason the vision feature shows nothing live. Closes when
  either a live log carries the `Player race N -> base vision` line, or the call
  is moved onto the path that actually creates the player entity and a live
  capture shows a Dark Elf seeing in the dark. Prov: T345 Review,
  `review/T345/README.md`.

## Added 2026-09-04 by the T347 measurement (B013)

- ~~**B057** (B) `include/client/action/command_processor.h`
  `CommandProcessor::getCompletions` and `include/client/graphics/ui/command_registry.h`
  `CommandRegistry::getCompletions` — both implemented, both tested, and both
  with ZERO production callers: no input path (chat box, console, script) ever
  asks either registry to complete a partial command, so the "tab completion"
  both `CLAUDE.md` files advertise does not exist.~~ **CLOSED by T357 Leg A**
  (`refactor/T357_tab_completion_spell_land_eqe1_gate.md`, commit `d7f8de70`):
  BOTH are now called. The chat box is a hand-rolled string in
  `ChatPanelState`, not an Irrlicht `IGUIEditBox` (there is none in the tree),
  drained by `FrameComponent::processChatInput()` on the RENDER thread; its new
  `KEY_TAB` arm calls `FrameComponent::applyCommandCompletion()`
  (`src/client/graphics/frame/renderer_frame_input.cpp`), which asks the
  provider `Application::initRendererAndGraphics()` installs. That provider is
  `Application::commandCompletions()`, which MERGES registry 1
  (`eqt::ui::CommandRegistry`, the registry the submit path's `ChatSubmitIntent`
  reaches via `ChatComponent::DispatchSlashCommand`) with registry 2
  (`action::CommandProcessor`, reached by the unrecognized-command fallback) —
  the same pair `eqt::help::BuildHelpResponse()` merges, so Tab and `/help` can
  never list different commands. One match replaces the partial and appends a
  space; several print on one System-channel chat line; none does nothing. Both
  `CLAUDE.md` sentences now name the caller. Pinned by
  `ChatTabCompletionTest.*` (7 cases, `tests/test_frame_component.cpp`) for the
  behaviour and by `tests/goldens/application_init_order_g04.txt` entry 256 for
  the reachability. See the new **B066** for the duplication the plan asked to
  record rather than merge. Prov: T347 (B013 measurement).

- **B058** (C) `/skills` vs `/newskills` — a NAMING choice, not a defect.
  `/newskills` (`renderer_slash_commands.cpp:422`) and the bare `skills` keyword
  piped stdin recognises (`console_input_handler.cpp:432` →
  `InputAction::ToggleSkills` → `renderer_frame_input.cpp:222`) toggle the same
  `skillsPopupState_.isOpen`, but `/newskills` additionally closes the spellbook
  and the inventory popup and echoes to chat, and `/skills` typed WITH a slash is
  unknown to all three registries — so the real client's name for the window does
  nothing. Closes when: Will rules on the name (rename `/newskills` to `/skills`
  and keep one body, or keep both and say why). Do not merge them silently: the
  two bodies are not equivalent. Prov: T347 (B013 measurement).

## Added 2026-09-04 by the T346 Review (self-death sequence)

- ~~**B059** (A) `src/client/player/player_misc.cpp` `PlayerComponent::SendSave`
  — queues a bare 2-byte `OP_Save`; the official client's `OP_Save` is 192
  bytes and it sends one on EVERY self death as the first packet of the
  burst (T311 capture, all four deaths), fable sends none from the death
  path. Closes when: the 192-byte body is decoded from the capture
  (`review/T311/legA/decode/`), fable's `SendSave` fills it, and
  `ZoneProcessDeath` sends it before the Lying appearance — the
  self-death golden (`tests/goldens/self_death_outbound_g07.txt`) learns
  it in place. Prov: T346 Review.

  **T348 (2026-09-04): PROVEN and FIXED in code; the live capture is owed and
  BLOCKED.** The lead held, and harder than stated: `IrrlichtRenderer::createEntity()`
  has **ZERO production callers tree-wide** — every `createEntity` reference in
  `src/`/`include/`/`tests/` is to the unrelated `EntityRenderer::createEntity`.
  The live zone load registers every entity through `IrrlichtRenderer::registerEntity()`
  (`application_zone_loading.cpp:242` threaded, `:352` inline) and post-load spawns
  arrive on the `EntitySpawned` bridge arm (`irrlicht_bridge.cpp:64`), which calls
  the same. That is why `[ZONE-IN] setPlayerSpawnId` — set from the same `isPlayer`
  arm at both sites — is in all five T345 logs and `Player race N -> base vision`
  is in none. Fix: one call, `playerCamera_->setPlayerRace(raceId)` under
  `if (result && isPlayer && playerCamera_)` in `registerEntity()`
  (`renderer_entities.cpp`), the shape `createEntity()` already had. **Territory
  deviation, stated:** the plan named `src/client/graphics/zone_load/renderer_zone_load_entities.cpp`;
  that file preps and builds meshes for entities already registered, never creates
  one and never sees `isPlayer` — it was not modified. Three tests appended to
  `tests/test_vision_buff_fade_wire.cpp`: the floor at runtime (Dark Elf ->
  Ultravision, Human -> Normal, an untold renderer -> Normal, which is why the
  miss was invisible) plus a source-reader pin on the call being inside
  `registerEntity()` and on the two live sites still using it. Pre-fix failure in
  the T348 Review. ~~**Still owed: a live Dark Elf zone-in capture** — the eqemu
  lock was held by T350 all unit.~~ **LIVE-PROVEN by T358 (2026-09-05),
  `review/T358/`.** No `#race` write was needed: `Testcaster` (character 67) is a
  Dark Elf, race 6, and `Testchar` (65) a Human, race 1, both on account
  `willeq_test`, so the proof is two logins. At every zone-in, with no buff up,
  the client logs `Player race 6 -> base vision: Ultravision` (twice, two
  sessions) and `Player race 1 -> base vision: Normal` (four times, four
  sessions) — the line T345 recorded as appearing in NONE of its five logs. A
  like-for-like pixel A/B was attempted (both characters `#zone befallen` to the
  same safe point) and is CONFOUNDED — the camera settles facing differently in
  each frame, so the pair is reported as a failed measurement, not as evidence;
  B067 also explains why a racial floor moves so little on screen.
  Prov: T348 Review, T358 Review.

## Added 2026-09-04 by T348 (B055/B056)

- **B060** (C) `src/client/graphics/player/renderer_camera.cpp` →
  `EQT::Graphics::racialBaseVision()` — **MEASURED AGAINST THE ORACLE BY T361
  (`c531006f`); every row but ONE is now settled and fixed, and only Froglok
  still needs Will.** Method: two logins as Oraclealpha in qeynos2 at forced
  22:00, camera never touched, race changed in-game with `#race N`, read from
  the session VIDEO, each session carrying its own `#castspell 46` positive
  control that race 6 lands on to four decimals. Three levels and only three,
  window spreads ≤0.14 luma against a 4.8-luma separation:
  **Normal** = Human (1), Barbarian (2), Erudite (3); **Infravision** = Wood Elf
  (4), High Elf (5), Half Elf (7), Dwarf (8), **Troll (9)**, **Ogre (10)**,
  Halfling (11), Gnome (12), Iksar (128), Vah Shir (130); **Ultravision** = the
  **Dark Elf (6) ALONE**. So the old table was wrong for 4, 5, 9 and 128 (all
  Ultravision, all actually Infravision) and omitted 130; all fixed and pinned
  by `tests/test_vision_ambient.cpp`. Will's expectation was Troll =
  Ultravision, Ogre = Infravision; **the oracle puts both on Infravision.**
  **THE ONE OPEN ROW: Froglok (330) measures 63.49** — a distinct FOURTH level,
  1.51 luma below Ultravision and 3.31 above Infravision, both far outside the
  noise, which the three-valued `VisionType` cannot express. It is left on the
  `Normal` default rather than rounded to a neighbour by argument. Closes when
  Will rules on Froglok (round it to Ultravision, round it to Infravision, or
  give `VisionType` a fourth value). Prov: T361 Review, `review/T361/README.md`.
  Original text follows.

  ~~**the racial vision table has no oracle, and it disagrees with B056's own
  board text.**~~ The table puts Wood Elf
  (4), High Elf (5), Dark Elf (6), Troll (9) and Iksar (128) on Ultravision, and
  Half Elf (7), Dwarf (8), Ogre (10), Halfling (11), Gnome (12) on Infravision.
  B056's entry above describes the intent as "Dark Elf Ultravision, Dwarf/Troll/
  Ogre Infravision" — Troll and Ogre are on the other side in both directions.
  `grep -rni 'ultravision|infravision' docs/` returns NOTHING, so nothing under
  `docs/` records the official client's table either way. T348 wired the floor
  onto the live path (B056) but did NOT touch the table: picking values by
  argument is exactly the smell `match-the-oracle-dont-infer-it` warns about, and
  the contract said "the race table only if wrong" without saying what right is.
  Closes when the table is settled against the official client (a night capture
  per race is cheap once the eqemu lock is free — one zone-in each, the floor is
  visible with no buff up) or Will rules on it. Prov: T348 Review.

- ~~**B061** (B) `include/client/spell/spell_manager.h:193`
  `SpellManager::setOnSpellLand()` — **zero production callers.** The
  `SpellLandCallback` typedef, the setter and the member all exist;
  `m_on_spell_land` is invoked at `spell_manager.cpp:1027` and nothing tree-wide
  ever sets it, so the invocation fires into a permanently-null `std::function`
  — the exact shape T339 found on `setBuffApplyCallback` (B055). Noticed while
  siting T348's producer; NOT used for it, because the invocation sits OUTSIDE
  the `is_success_packet` gate and so fires on the animation packet
  (`effect_flag == 0`) as well as the landing, which is wrong for any consumer
  that means "landed".~~ **CLOSED by T357 Leg B (DELETED)**
  (`refactor/T357_tab_completion_spell_land_eqe1_gate.md`, commit `16529e54`):
  typedef, setter, member and invocation are gone, and so is
  `SpellManagerCallbacks.OnSpellLandReceivesSpellCasterTarget`, the test that
  only exercised the setter (the mixed
  `HandleActionSuccessPacketCompletesOwnCastAndFiresCallback` kept its
  `setOnCastComplete` half). Not re-wired: landing is OP_Action `effect_flag`
  4 and T348 already built that producer, so a second, wrong-shaped hook has no
  consumer to serve. K12 `pushEvent` check: none of the removed lines carried
  one — the nearest (`spell_manager.cpp:1012`, the
  `SpellCastVisualComplete` push) is twelve lines above and untouched. The
  sibling `setOnSpellFade` is deliberately LEFT: its own test documents that it
  too has no invoker, and that is a separate question. Absence pinned by
  `SpellLandHookIsGone.NoTypedefSetterMemberOrInvocationRemains`. Prov: T348
  Review.

- ~~**B062** (B) `include/client/spell/spell_data.h:74` `SpellData::endurance_cost`
  — the sibling shape T353 found one struct over and left alone as out of B007's
  territory. Declared `int32_t`, defaulted 0, and assigned at **zero** sites:
  `src/client/spell/spell_database.cpp` loads `mana_cost` (line 73) and nothing
  else resource-shaped, and no `endurance` string exists anywhere else under
  `src/client/spell/` or `tools/willeq_db_builder.cpp`. Unlike the skill case
  this one IS enforced server-side — `/mnt/projects/akk-stack/code/zone/effects.cpp:900`
  refuses a spell for insufficient endurance — so the question is real: does the
  Titanium `SPDat` export fable reads carry the column? Closes when: the column
  is found and loaded, or the field is deleted. Do not invent values. Prov: T353.~~
  **CLOSED by T355** (`refactor/T355_spell_endurance_cost.md`): the export DOES
  carry it — **spells_us.txt field 166**, established from eqemu's own positional
  importer (`/mnt/projects/akk-stack/code/client_files/import/main.cpp`
  `ImportSpells()` writes `split[i]` into `spells_new` column `i`, and `EndurCost`
  is column 166 of that table per
  `/mnt/projects/akk-stack/code/common/repositories/base/base_spells_new_repository.h`, which reads it back as
  `row[166]`; the control is `mana` at 19, the index fable's builder already used).
  Cross-checked against a live peq `spells_new` on 4520/4608/4614 (900/85/125) —
  exact match to field 166 of the text file. Now loaded through the whole existing
  pipeline (`willeq_db_builder` -> `spells.endurance_cost` -> `SpellInfo` ->
  `SpellData`; `kWilleqDbExpectedVersion` 5 -> 6, `data/willeq.db` rebuilt,
  263 of 8066 spells non-zero), and refused LOCALLY beside mana:
  `SpellManager::checkEndurance()` + `CastResult::NotEnoughEndurance` + eqstr
  **5805** "Insufficient Endurance to activate this ability!" in the shared
  `castRefusalStringId()` table (so all three refusal sites emit it). Server stays
  the backstop. 7 new tests, both RED shapes pasted in the Review.

  it in place. Prov: T346 Review.~~ **CLOSED by T349**
  (`refactor/T349_op_save_body.md`): `EQT::Save_Struct`
  (`include/common/packets/character.h`, `static_assert`ed to 192) and a
  `SendSave` that fills it; `ZoneProcessDeath`'s self branch sends it
  first, before `ApplyPositionState(POS_DEAD)` and the Lying appearance.
  The decode came out of the SERVER-SIDE packetlogs, not the timeline TSV
  (which keeps only 32 of the 192 bytes): the floats at offsets 5/9/13 are
  byte-for-byte the last `OP_ClientUpdate`'s y/x/z in all four deaths, z
  bit pattern included, and offsets 110..174 are uninitialized client
  memory. eqemu's `Handle_OP_Save`
  (`/mnt/projects/akk-stack/code/zone/client_packet.cpp:13661`) reads NONE
  of the body. `self_death_outbound_g07.txt` learned `C->S OP_Save` as its
  new first line in place; the body is pinned by a second golden,
  `tests/goldens/op_save_body_g08.txt`.

## Added 2026-09-05 by T358 (the owed live proofs 3)

- **B066** (A) `src/client/graphics/sim_animation.cpp`
  `SimulationWorker::applyVisionWeatherToZoneLights` and
  `SimulationWorker::computeLightAnimations` — **the vision-type switch is
  SWAPPED against the enum it switches on.** Both read
  `case 1: // Ultravision -> intensity 1.0` and
  `case 2: // Infravision -> intensity 0.75, redShift 0.3`, while
  `EQT::Graphics::VisionType` (`include/client/graphics/renderer_types.h`) and
  `EQ::BuffManager::computeVisionType` (`src/client/spell/buff_manager.cpp`) are
  both **Normal = 0, Infravision = 1, Ultravision = 2** — and `computeVisionType`
  literally `return 2;` for an Ultravision effect. So an Ultravision landing gets
  Infravision's treatment and vice versa. The wrong mapping is also written into
  the comment on `SimulationInput::visionType`
  (`include/client/graphics/simulation_worker.h`), which is presumably where it
  came from. **MEASURED LIVE, T358:** across a `#castspell 46` landing, the
  400x300 world crop's R rises 5.5% and G 5.2% while **B rises 0.4%** (R/B
  1.3618 -> 1.4303) — a red shift, which only the Infravision arm produces.
  Closes when the two switches use `VisionType` (or its underlying values)
  instead of hand-written cases, the header comment is corrected, and a test pins
  Ultravision -> intensity 1.0 / redShift 0 and Infravision -> 0.75 / 0.3.
  Prov: T358 Review, `review/T358/logs/frame_rgb_visionvid.txt`.

- ~~**B067** (A) `src/client/graphics/sim_animation.cpp` /
  `src/client/graphics/zone_shader.cpp` — **vision type never reaches the global
  ambient; it only scales the zone's point lights, so Ultravision is nearly
  invisible.**~~ **CLOSED by T361 (`c531006f`, branch
  `task/T361-ultravision-oracle`).** Vision now raises a per-channel FLOOR under
  the global ambient in `IrrlichtRenderer::updateTimeOfDay()` (new TU
  `src/client/graphics/vision_ambient.cpp`), which is the one place fable
  computes the day/night ambient and hands it to both `smgr_->setAmbientLight()`
  and `ZoneShader::setAmbientColor()`. **The mechanism is the oracle's, measured
  not assumed:** the official Titanium client at the same spot lifts a 700x420
  world crop **x1.554** (linear) for Ultravision and **x1.253** for Infravision;
  its SKY does not change (x1.17, blue x1.02) so it is not a post-process, and
  its NEAR geometry lifts x1.32 while distant lifts x3.25 — fog explains the
  second and cannot explain the first. The two gains were SOLVED by A/B (the
  frame's response to mean ambient is a power law whose exponent two independent
  probe points give as 1.1402 and 1.1409), not chosen. fable now measures
  **x1.473** on the search crop and **x1.780** combined against the oracle's
  x1.554 — see **B080** for why the residual is not smaller. Pinned by
  `tests/test_vision_ambient.cpp` (3 of 6 cases fail against the pre-T361
  behaviour). Prov: T361 Review, `review/T361/README.md`. `visionType` has exactly two consumers tree-wide
  (`applyVisionWeatherToZoneLights` and `computeLightAnimations`), both of which
  multiply `zoneData_.zoneLightNodes[i].diffuseColor`. In qeynos2 that is 218
  lights the client's own log calls `ZONE LIGHTS: 218 total (no scene nodes,
  data-only)`. `ZoneShader::setAmbientColor` is never called from anything vision
  related. **MEASURED LIVE, T358:** at forced 22:00, the day/night cycle is worth
  **4x** on the same crop and camera (0.0457 midday -> 0.0176 at 22:00) while a
  full Ultravision buff is worth **+4.9%** (0.017618 -> 0.018485, 143 frames).
  A player cannot see in the dark with Ultravision up. Note this is measured
  THROUGH B066's swap, so the true figure at 1.0 intensity would be somewhat
  larger — but the ceiling is still "218 lamps brighter", not "the world is
  lit". Closes when vision raises the zone shader's ambient (or whatever the
  official client's Ultravision actually does — that is B060's oracle question)
  and a night frame with the buff up is measurably, obviously brighter.
  Prov: T358 Review, `review/T358/logs/frame_brightness_visionvid.txt`.

## Added 2026-09-05 by T361 (the Ultravision oracle)

- **B080** (A) `src/client/graphics/renderer/irrlicht_renderer.cpp`
  `IrrlichtRenderer::updateTimeOfDay` / the zone lighting as a whole —
  **fable's night is ~35x darker than the official client's on near geometry,
  and that gap is what stops T361's vision match from closing.** Measured at
  the same spot, same zone, same forced 22:00, same 300x300 near-wall crop:
  the oracle reads **0.0643** linear grey and fable **0.0018**. On a whole
  700x420 world crop the oracle reads 0.0332 and fable 0.0019 — a factor of
  17. The consequence is not cosmetic: because fable's night baseline is so
  much lower, the SAME ambient raise is a much larger *fraction* of it, so no
  single ambient parameter can reproduce both the oracle's near-wall ratio
  (x1.325) and its whole-frame ratio (x1.554). T361 solved for the whole-frame
  ratio and lands at x1.473 / x1.780 (crop-dependent) with the near wall
  over-lifting at x2.07. The suspected cause is that fable's zone point lights
  do not reach the near wall at all at this spot while the oracle's lamps
  plainly do (fable's own log: `ZONE LIGHTS: 218 total (no scene nodes,
  data-only)` — they scale colours but light nothing), but that is a LEAD, not
  a finding. Closes when fable's unbuffed night frame is within a small factor
  of the oracle's at the same spot, measured the same way, and T361's residual
  is re-checked against the oracle table. Prov: T361 Review,
  `review/T361/README.md`.

- **B068** (B) `src/client/entities/entities_component_world.cpp`
  `EntitiesComponent::SendSpawnAppearance` / `SendAnimation` — **these two
  senders bypass `PacketSender`, so the session recorder (`--record-session`,
  R01) cannot see them, and a `.sleq` is a FALSE NEGATIVE for every appearance
  and animation the client sends.** The recorder is hooked into
  `PacketSender::SafeQueueZone`/`Queue` (`src/client/packet_sender.cpp`); both
  senders call `ctx_.zone_connection->QueuePacket(p)` directly. T348/T346 already
  knew about the bypass — the code carries an explicit
  `ctx_.net.Test_RecordQueuedPacket(...)` line to keep the T268 ordered-capture
  seam working — but the SESSION RECORDER was not given the same treatment.
  **MEASURED, T358:** a `--record-session` capture of a self-death that
  definitely ran T346 step 1 contains **zero C->S `OP_SpawnAppearance`**; the
  proof had to be taken from the server's own zone packet log instead. This also
  degrades the Batch R replay oracle, which replays what the recorder saw.
  **Second half, same site:** the bypass reorders the death burst. T349 asserts
  `OP_Save` is the first C->S packet of the burst and the client's log emits them
  in that order in one millisecond, but the server RECEIVES the Lying appearance
  first, because `SendSave` goes through `SafeQueueZone` -> `QueuePacketAsync`
  when called off the main thread while `SendSpawnAppearance` queues
  synchronously. Closes when both senders go through `PacketSender` (which also
  makes the ordering match the oracle) and a test asserts a recorded session
  contains the appearance. Prov: T358 Review,
  `review/T358/logs/death_burst_server_cs.txt`.
## Added 2026-09-04 by T357 Leg A (B057)

- **B069** (B) `src/client/action/command_processor.cpp`
  `CommandProcessor::getCompletions` and
  `src/client/graphics/ui/command_registry.cpp` `CommandRegistry::getCompletions`
  — duplicated-then-drifted. Two independent implementations over two different
  command maps; neither delegates. Both share the same
  `name.find(lowerPartial) == 0` prefix idiom over primary names then aliases,
  both use their class's own private `toLower`, and both sort. The drift is ONE
  line: `CommandProcessor` de-duplicates (`std::unique` after the sort),
  `CommandRegistry` does not — so a `CommandRegistry` command whose name is also
  registered as its own alias is returned twice. T357 Leg A did not merge them
  (the plan said to record, not merge); it hid the drift instead, because
  `Application::commandCompletions()` sorts and de-duplicates the union.
  A `CommandRegistry` caller that is not that one still sees the duplicate.
  Closes when: the prefix-match is one shared helper both call, or
  `CommandRegistry::getCompletions` de-duplicates like its twin. Prov: T357 Leg
  A (step 1 measurement).
