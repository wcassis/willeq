# BOARD — the open-work queue of record

- **Tree confirmed at:** `3808ebe9` (branch `task/K00-legA-board`, base `fable`).
- **Date:** 2026-09-03. Built by K00 Leg A (`refactor/K00_truth_reset.md`).
- **Suite:** DO NOT QUOTE A NUMBER FROM THIS LINE. The suite grows with every
  unit -- it went 3,760 -> 3,949 across T352-T370 alone -- so a count written
  here is stale within a day. Get the current one with
  `ctest --test-dir <slot>/build -N | tail -1`, and the current pass/fail with
  `ctest --test-dir <slot>/build -j6`. The only invariant is that the ALSA host
  quirk `AudioManagerTest.PreloadCommonSounds` is the SOLE expected failure
  (ctest still exits 8 with only it failing -- read the summary line, not the
  exit status). Last measured: **3,948 of 3,949** at `a7e760dc` + T371's doc
  sweep, 2026-09-05, slot2.
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

- ~~**B003d** (B) `src/client/entities/entities_component_appearance.cpp:326`
  `EntitiesComponent::FindEntityByName` — the unranked case-insensitive PREFIX
  resolver T356 took out of moveTo/follow still backs `MoveToEntityWithinRange`,
  `StartCombatMovement(name)` and `UpdateCombatMovement`, so combat movement
  keeps the lowest-spawn-id pick; and `MovementComponent::Follow` stores its
  target as a NAME and re-matches it by exact string every tick
  (`src/client/movement/movement_update.cpp:183`), so following one of two
  identically-named mobs is ambiguous per tick.~~ **CLOSED by T359 Leg A** (`310725a4`,
  `refactor/T359_combat_movement_spawn_id.md`). `CombatState` and `PlayerState`
  now store a SPAWN ID as the target identity (the name rides along for the log
  lines only): `UpdateCombatMovement` and the per-tick follow branch read the
  entity map by id, `MoveToEntityWithinRange`/`FaceEntity` take a `uint16_t`,
  and the name-taking `StartCombatMovement(const std::string&, float)` — which
  had ZERO production callers — was deleted. Pinned by
  `MovementComponentTest.{FollowTracksTheSpawnIdAcrossTicksNotTheNameMatch,
  CombatMovementTracksTheSpawnIdAcrossTicksNotTheNameMatch}`, both verified to
  fail on the name re-match (they latched the far beetle at x=100 instead of the
  near one at x=30). **The resolver was NOT deleted by T359**: `RequestWarp`
  (`/warp <entity_name>`) still called it with a genuinely user-typed name — see
  the new **B070**, which **T362 Leg A (`4a06d2e2`) closed by deleting it**
  along with that last caller. Prov: T356 Review.

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

- ~~**B009** (B) `tests/test_daybreak_connection_pcap.cpp` — reads `connect_code`,
  `encode_key` and `max_packet_size` raw off the wire, which is exactly the bug
  T21 fixed in production; it cannot fail if that fix regresses, and passes today
  only because eqemu's key is a byte-swap palindrome. Closes when: the test reads
  through the production path. Prov: deferred item 40.~~ **CLOSED by T364 Leg A
  `f734ac3a`**: `processSessionResponse()` now feeds the recorded bytes through
  `DaybreakConnection::ProcessPacket()`; the only value the test supplies is our
  own connect code. Two new tests give it teeth.
  `RecordedHandshakeYieldsNetworkOrderMaxPacketSize` asserts `max_packet_size
  == 512` — the one field the capture can discriminate (network-order 512 reads
  raw as 131072). The other two cannot be caught by any capture on disk:
  **measured, all eleven `OP_SessionResponse` datagrams in
  `data/captures/session.pcap` carry `connect_code` ∈ {0, 0xFFFFFFFF} and
  `encode_key` ∈ {0, 0xFFFFFFFF}** — byte-swap palindromes, T21's own RNG-collapse
  finding — so `SynthesisedHandshakeByteSwapsAllThreeFields` writes a 17-byte
  handshake's wire bytes big-endian by hand (connect_code 0x11223344, encode_key
  0xA1B2C3D4, max_packet_size 512). With T21's byte-swap reverted in production
  both new tests fail and the pre-T364 version of the file passes 4 of 4, which is
  this entry restated as a measurement.

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

- ~~**B011** (B) `src/client/graphics/entity_size.cpp`
  `EntityRenderer::setEntitySize` — updates neither `collisionHeight` nor `deckZ`,
  so a resized boat's deck surface goes stale. Nothing in the game resizes a
  boat, so this is a documented limit, not a live defect.~~ **CLOSED by T359
  Leg C (`23863718`) — the limit is ASSERTED, not removed.** The recompute was rejected on
  reading it: it would be a THIRD copy of a formula whose two existing copies
  (`entity_mesh_build.cpp` reads `mesh->getBoundingBox()` and a local `z`;
  `entity_build_step.cpp` reads `vis.meshNode->getMesh()->getBoundingBox()` and
  `vis.lastZ`) already disagree on their inputs, and no reachable path could
  exercise it — eqemu's size changes are `Mob::ChangeSize()` (SPA 89 / SPA 58) on
  players and NPCs, and boats are zone-owned static spawns nothing casts on.
  Pinned by `EntityRescaleNodeTest.ResizingABoatLeavesItsDeckSurfaceWhereItWas`,
  which builds a REAL race-72 Ship mesh (collisionHeight 338.78, deckZ 199.39),
  doubles the boat, checks the node really rescaled, and asserts both collision
  values held. Verified load-bearing: with the recompute planted it fails
  (collisionHeight 677.56, deckZ 368.78). The limit is also now stated in the
  source next to the code that has it. Prov: deferred item 38.

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

- ~~**B015** (B) `src/client/audio/midi_player.cpp` `MidiPlayer::loadSoundFont` —
  swaps a raw `tsf*` with no atomic or mutex; safe today only because its single
  call site runs before the audio thread exists, which the file's own comment
  records.~~ **CLOSED by T359 Leg B** (`c73f8745`) — the precondition is ASSERTED, not
  synchronised (a mutex in the realtime audio callback was explicitly rejected).
  `MidiPlayer::setAudioThreadRunning(bool)` is called by `AudioManager` at the
  one point every backend branch converges on (`initialize`, right after the
  started backend) and again in `shutdown` after `backend_->stop()`;
  `loadSoundFont` refuses with a `LOG_ERROR` naming the hazard and returns false
  while that flag is set, before loading anything. Pinned by
  `MidiPlayerPreallocTest.{LoadSoundFontRefusedWhileTheAudioThreadIsRunning,
  LoadSoundFontRefusalDoesNotDisturbTheAudioThreadFlag}`; the first was verified
  to fail with the check disabled (the swap was accepted). Prov: archived board,
  T131.

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

- ~~**B064** (B) `src/client/graphics/eq/mesh_building.cpp`
  `RaceModelLoader::getMergedTextures` — T352's lock covers `otherChrCaches_`
  and `mergedTexturesCacheValid_`, but the same function also reads
  `globalTextures_`, `numberedGlobalTextures_`, `armorTextureCache_` and
  `zoneTextures_`, which other loader paths mutate without
  `otherChrCacheMutex_`. **Unconfirmed as a live race** — TSAN reported none of
  them in any T352 run; the driver never mutated them concurrently. Confirming
  needs a run that loads a zone (a `#zone` tour under TSAN) while the prep
  worker is live. Closing it is a restructure, not a lock. Prov: T352 Review.~~
  **CLOSED by T370 `373cea89` as NOT a live race — the measurement T352 could not take is
  now taken.** The T120/T121 amplified zone tour (login, six `#zone` crossings
  Befallen<->Qeynos2, `/camp`) was run three times against a live eqemu under
  ThreadSanitizer with the EntityPrepWorker doing real background prep
  (112 / 79 / 107 completed prep jobs; all three runs reached script step 42
  of 43). **ThreadSanitizer reported ZERO races on any of the four containers
  in any run** — no `globalTextures_`, no `numberedGlobalTextures_`, no
  `armorTextureCache_`, no `zoneTextures_`, and none anywhere in
  `RaceModelLoader`. The clean result is a finding, not a silent harness: the
  same three runs each reported a DIFFERENT in-process race (B125 below) with
  byte-identical stacks, and the post-fix binary still reports the Mesa
  libgallium races the harness suppresses by library when the suppression file
  is emptied (`review/T370/poscontrol_postfix/`). Reports, logs, the tour
  script and the runner are in `review/T370/`. No restructure was performed and
  none is warranted on this evidence; the code-level observation stands
  (`loadGlobalModels`/`loadZoneModels`/`getArmorTexture` do mutate three of the
  four outside `otherChrCacheMutex_`), but every mutation the tour reached ran
  on the same thread as the reader. Anyone reviving it must bring a report.

- ~~**B065** (B) `src/client/graphics/eq/model_loading_chr.cpp`
  `RaceModelLoader::loadModelFromCachedChr` — writes
  `otherChrCaches_[lowerFilename]` directly instead of through
  `storeOtherChrCacheLocked()`, the LRU-accounting bypass T91 was written to
  eliminate, surviving at this one site. Under `otherChrCacheMutex_`, so not a
  race: the entry never enters `chrCacheLruOrder_` and can never be evicted,
  while still counting against `maxChrCacheEntries_`. Prov: T352 Review.~~
  **CLOSED by T364 Leg B `ca96cb56`**: it was **four** sites in that function,
  not one — the two negative-cache inserts, the positive insert, and the
  animation-source archive insert; all four now go through
  `storeOtherChrCacheLocked()`. Pinned by `tests/test_chr_cache_lru.cpp`
  (`ChrCacheLruTest`), which drives the user-reachable `ensureModelDataLoaded()`
  for five races whose rows name five different plain zone archives, cap 2,
  offline against the real client tree under `EDT_NULL`. Measured pre-fix:
  **6 of 6 cached entries untracked, `chrCacheLruOrder_` empty, 6 entries against
  a cap of 2** — i.e. the cap was not merely exceeded, on this path it did
  nothing at all. Post-fix `untracked=0`, `entries==lruOrder`, `entries<=cap`.
  The accounting had no observable surface, so `getChrCacheAccountingForTest()`
  was added next to `setMaxChrCacheEntries()`.

- ~~**B105** (B)~~ **CLOSED by T369 (`bae6befc`)**:
  `S3DLoader::deriveChrArchivePath()` derives the character archive from the
  FILENAME's stem (`std::filesystem::path`: `parent_path` / `stem`, then a
  trailing `_chr` with an optional numeric variant stripped), so a directory
  component containing `_chr` no longer truncates the path. Pinned three ways in
  `tests/test_chr_path_derivation.cpp`: the derivation directly, end-to-end (an
  archive loaded out of `/tmp/willeq_chrscan_<pid>/` returns characters where it
  returned zero before), and an IDENTITY pin over the real client tree — all
  **1,006 `.s3d` archives** under `/mnt/projects/EverQuestP1999` derive
  byte-for-byte what the old code derived, so the fix changed only the broken
  case. The `_chr2` variants (`acrylia_chr2.s3d` and 30-odd siblings) are why the
  strip allows trailing digits; a bare `_chr`-suffix match would have moved them.
  The silence is closed too: **both** negative-cache branches in
  `searchZoneChrFilesForModel` now log WARN naming the path ("is a `_chr.s3d`
  archive that yielded NO characters"), caching behaviour unchanged.

- ~~**B095** (B)~~ **CLOSED by T366 Leg A (`a2b83e28`)**: all four sites now call
  `storeOtherChrCacheLocked()`, the inline push/evict copy is deleted, and there is
  exactly ONE `otherChrCaches_[key]` left in the tree — the helper's own line.
  **Measured, per path, with a cap of 2** (`tests/test_chr_cache_lru.cpp`,
  `ChrCacheScanTest`, a private client tree of symlinked archives so the directory
  scan is bounded): `loadModelFromZoneChr`'s animation-source branch left
  **1 of 1** cached entries untracked (`lruOrder=0`); `searchZoneChrFilesForModel`
  left **1 of 3** untracked and held **3 entries against a cap of 2**. Both are 0
  and 2 post-fix. So it was NOT the total bypass B065's path turned out to be —
  the search path's inline accounting really did track its positive inserts, and
  only the negative-cache entries escaped, which is what pinned the map over its
  cap forever. Original text: `src/client/graphics/eq/model_loading_chr.cpp`
  `RaceModelLoader::loadModelFromZoneChr` (the animation-source insert) and
  `src/client/graphics/eq/model_loading_search.cpp`
  `RaceModelLoader::searchZoneChrFilesForModel` (its two negative-cache inserts,
  and a third that open-codes the store helper's LRU push/evict inline) — the
  same `otherChrCaches_[key] = ...` bypass T364 Leg B closed in
  `loadModelFromCachedChr`, at four more sites. **Confirmed by reading at
  `ca96cb56`, not measured**: no test drives these two functions'
  LRU accounting, so the size of the effect is unknown — B065's own path turned
  out to be total (6 of 6 entries untracked), and this may be the same or may be
  partly masked by the inline accounting in `searchZoneChrFilesForModel`. Closes
  when: all four go through `storeOtherChrCacheLocked()` and
  `tests/test_chr_cache_lru.cpp` is extended to reach them. Prov: T364 Leg B
  Review.

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

- ~~**B020** (C) `include/client/graphics/constrained_renderer_config.h` — eight
  measured memory categories (zoneGeometry, s3dZoneSource, gpuTexture, vboEbo,
  eqe1Mapped, characterModels, placeableObjects, untracked) carry no configured
  budget, only baseline guards. The per-category ruling is owed. Prov: archived
  board, T69.~~ **STALE — CLOSED by T293 (2026-08-26): every one of the eight categories has a configured budget in the orangepi preset (`config/constrained_presets.json`), derived per Will's ruling from the assets on disk, 1280x720 and the Orange Pi One envelope (`include/client/graphics/constrained_renderer_config.h` §T293). The only remaining unbudgeted figure is untracked RSS, which is B021. Re-confirmed to Will 2026-09-05.**

- **B021** (C) `scripts/ci/visual_gate.py` `untrackedBytes` — emitted and
  baselined but deliberately not asserted; gating it needs repeat-run data from a
  second machine first. Prov: archived board, T69.

- **B022** (C) `src/client/graphics/spell_visual_fx.cpp` — the data-driven spell
  visual layer is still entirely ahead: nothing consumes the `spells.eff` reader
  in place of the hardcoded resist/school vocabulary, and the spells whose
  `spell_affect_index` hits an empty padding record need a rendering ruling.
  Prov: archived board, T70.
  **T380 (2026-09-05) surveyed this and put numbers on it — the ruling is still
  open, but it is now correctly scoped as LOW priority, and a bigger defect was
  found underneath it (B160).** Census script:
  `scripts/refactor/t380_spell_visual_census.py`; full tables in
  `refactor/T380_spell_visuals_survey.md` Review.
  - The reader DOES have a production caller — `SpellVisualFX::loadSpellsEff()`
    in the constructor built by
    `src/client/graphics/renderer/renderer_entities.cpp` — but its result is
    consumed only by `resolveVisual()`, which per **B160** can never see a
    non-`-1` index. Loaded, correct, and inert.
  - 8,066 spells: **7,105 Resolved / 163 EmptyRecord / 798 NoAffectIndex / 0
    OutOfRange**. `data/willeq.db` matches `spells_us.txt` on
    `spell_affect_index` for all 8,066 rows.
  - `spells.eff`: 256 records, **90 carry sprite names**, 166 do not; of the 90,
    53 have a Source stage, **9** a Middle stage, 89 a Target stage.
  - **T70's "5 empty-padding indices (70, 71, 80, 84, 88), 148 spells" is
    WRONG — it is 6 indices and 163 spells; index 60 (15 spells) was missed.**
    Counts: 60→15, 70→109, 71→1, 80→6, 84→5, 88→27. The stale figure is also
    written into `tests/test_spell_visual_selection.cpp`'s comment (its
    `EXPECT_GE(emptyRecord, 148u)` still passes).
  - **"Padding" is the wrong word.** No record is zero-filled; every one has
    non-zero bytes in the 800-byte-per-emitter region `SpellsEffReader` reads
    into `unknown_bytes` and never interprets. 164 of the 166 sprite-less
    records are BYTE-IDENTICAL (a default template at indices 37..255); the two
    exceptions are index 36 (unreferenced) and index 60. Also: `header_a` is 1
    for only 32 of 256 records and 58 of the 90 sprite-bearing records have
    `header_a == 0` — it is NOT the enabled flag, do not rule on it.
  - **The ruling is NPC-only.** All 163 padding-hit spells have every one of
    the 16 class-level fields at 255 — NPC breath/rot/mist attacks, the
    Heated/Burning/Blazing/Flaming Blade proc line (index 84) and the
    `Xalirilan's`/`Iony's` augury family (index 88). Of the **2,584**
    player-castable spells: 2,548 Resolved, **0 EmptyRecord**, 36
    NoAffectIndex. A player never casts a padding-hit spell.
  - Oracle spells that settle it (capture recipe in the T380 Review; needs the
    eqemu lock): **Frost Bolt 54** (index 23, Wizard 1, `travel_type == 3`),
    **Flame Jet 1494** (index 71, its sole referent), **GM Cleansing 6594**
    (index −1, all classes, no reagent).

- ~~**B023** (B)~~ **CLOSED 2026-09-05 by T375 (`src/client/player/player_misc.cpp`).**
  `SendReport` now builds the body eqemu's `Client::Handle_OP_Report` actually
  parses: `"<reported>|<reporter>|<text>\0"`. The handler casts to NO struct and
  performs NO size check — it walks the payload with a three-state parser (bytes
  before the first `|` are `reported`, before the second `|` are `reporter`, the
  rest is the text with `0x0a` -> `\n` and `0x00` terminating) and then calls
  `database.AddReport(reporter, reported, text)`. The old body wrote a 4-byte
  zero "category_id" (no such field exists in the handler) and an unterminated
  string, so no `|` was ever found, `mode` never left 0, and the row eqemu filed
  had an empty reporter and an EMPTY `reported_text`. `/bug` reports a defect and
  not a player, so `reported` is sent empty (a leading `|`). The sender also
  moved from `zone_connection->QueuePacket()` to `ctx_.net.SafeQueueZone()` so the
  T268 seam and the session recorder both see it (the T360/B068 shape). Pinned by
  `tests/test_op_report_shape.cpp` (6 tests; 5 red pre-fix), which transcribes
  eqemu's parse loop as its oracle. ~~**LIVE PROOF IS OWED**~~ **LIVE-PROVEN
  T377, 2026-09-05** (`review/T377/` §4): one `/bug test T377` from fable
  (Testchar, qeynos2), the client's own `Chat submit: '/bug test T377'` at
  10:35:25.212 followed by `Report submitted`, and the row eqemu filed reads
  **`name` = `Testchar`, `reported` = '' (empty), `reported_text` = `test T377`**.
  The two pre-T375 rows still in the table are the control: both have an EMPTY
  `name` and an EMPTY `reported_text` with the whole message stuffed into
  `reported` behind four zero bytes. B023 is closed on the wire, not just on the
  unit test. Note `Handle_OP_Report`'s first statement is `if (!CanUseReport)`,
  so a client gets exactly ONE report per zone session — T377 sent exactly one. Original text: the wire
  shape does not match eqemu's `OP_Report` (pipe-delimited reported/reporter/
  message versus a 4-byte category plus raw text), so `AddReport()` is never
  reached. Fixing it is a command-shape decision, not a mechanical patch. Prov:
  archived board, T146.

- **B160** (A) `include/client/graphics/spell_visual_fx.h` —
  `SpellVisualFX::setSpellDatabase()` has **ZERO callers tree-wide**
  (`src/`, `include/`, `tests/`), so `m_spell_db` is always `nullptr` in
  production and every consumer takes its null branch: `getSpellColor()` and
  `getSpellColor(const SpellData*)` in
  `src/client/graphics/spell_visual_fx_nodes.cpp` always return the default
  `SColor(255,150,180,255)` (the six resist colours and five school colours are
  dead code); `isSprayCastStyle()` always returns `false` (the spray cast style
  never plays); `getTextureForSpell()` always returns `m_particle_texture`; and
  `resolveVisual()` in `src/client/graphics/spell_visual_fx.cpp` always leaves
  `affect_index` at `kSpellsEffNoEffectIndex`, so the T70/T72 `spells.eff`
  layer can never reach `Resolved` and its three suppression gates can never
  fire. **Net user-visible effect: every spell in the game renders one
  identical lavender-blue orbital glow.** The renderer has no route to a
  `SpellDatabase` today — the only instance is `SpellManager::m_spell_db`
  (`include/client/spell/spell_manager.h`), game-thread-owned, and
  `SpellDatabase`'s LRU (`include/client/spell/spell_database.h`) has no mutex,
  so that pointer must NOT simply be handed across the bridge. Fix shape
  (T380 Review, Leg B1): enrich `SpellCastVisualStartedData` /
  `SpellCastVisualCompleteData` with `resistType`, `school`,
  `spellAffectIndex`, `travelType` on the game thread — the
  `InventorySlotChanged` precedent — or give the renderer its own
  `SpellDatabase` instance. Pre-fix-failing test: two spells of different
  resist types must produce different particle colours. Found by T380.

- **B161** (B) `include/client/graphics/spell_visual_fx.h` — seven public
  methods with **zero callers tree-wide**: `createProjectile`,
  `createImpactAtPosition`, `createBuffAura`, `createRainEffect`,
  `createGroundCircle`, `removeBuffAura`, `removeAllForEntity`. Their bodies
  live in `src/client/graphics/spell_visual_fx.cpp`; with them the node
  builders `createProjectileNode`, `createAuraNode`, `createRainNode`,
  `createGroundCircleNode` in
  `src/client/graphics/spell_visual_fx_nodes.cpp`, the `ProjectileTravel` and
  `AuraPersistent` branches of `SpellVisualFX::update()`, and the
  `pendingImpacts` deferred-creation dance are unreachable.
  `createProjectileNode`/`createAuraNode` are also called from
  `applyWorkerResults()`, but only for worker `createEvents` produced from
  commands that `createProjectile`/`createBuffAura` would have pushed — so that
  route is dead too. No buff aura and no spell projectile is ever created in
  production; `src/client/bridge/irrlicht_bridge.cpp` calls only
  `createCastGlow`, `removeCastGlow`, `createSpellComplete` and `createImpact`.
  Decide per T380 Leg B4: wire projectiles (85 spells carry
  `spells_us.txt` `TravelType == 3`, and 9 `spells.eff` records carry a Middle
  stage) or delete the seven. Not both in one unit. Found by T380.

- **B162** (C) `src/client/spell/spells_eff_reader.cpp`
  `SpellsEffReader::effectNameToTexture()` maps only **18 of the 40** distinct
  sprite names in `spells.eff` — 16 of those exist in
  `/mnt/projects/EverQuestP1999/SpellEffects/` — which is **157 of 301 sprite
  slots (52%)**. The 22 it rejects are the commonest names (`GENI00_SPRITE`
  ×32, `GENN00_SPRITE` ×15, `GENG00_SPRITE` ×12) plus `GEN_Z_SPB`,
  `I_SNOWFLAKESPRITE`, `I_TFIRE_SPB`, `I_TSMOKE_SPB`; the function excludes
  digit `'0'` deliberately ("never resolves"). One alternative reading — the
  digit is **0-based**, so `GENx0 -> spelx1` — maps 36 of 40 names, 34 on disk,
  **272 of 301 slots (90%)**. That is a HYPOTHESIS, not a finding: settle it by
  capturing which texture the official client draws for **Frost Bolt (54)** or
  **Burst of Flame (93)**, not by argument (`match-the-oracle-dont-infer-it`).
  Separately, no mapping helps until there is a path to draw the file at all:
  the GLES2 spell path draws point sprites from a single 4x4, 16-tile
  PROCEDURAL atlas (`data/textures/particle_atlas.bmp`, `ParticleAtlas` in
  `include/client/graphics/environment/particle_types.h`) with exactly one
  spell tile (`SpellSparkle = 8`). The seam is
  `EmitterConfig::textureRegions[]` filled by
  `SpellEffectsConfig::getTextureIndex()`
  (`src/client/graphics/environment/spell_effects_config.cpp`), which maps a
  NAME to a tile index and falls back to `SoftCircle`. Found by T380.

- **B163** (B) `tools/willeq_db_builder.cpp` `loadSpells()` binds
  `SpellField::CastAnim` (120) but not `TargetAnim` (121) or `TravelType`
  (122), both of which `include/client/spell/spell_constants.h` defines; the
  `spells` table has `cast_anim` and no `target_anim` / `travel_type` column
  (`PRAGMA table_info`, verified by
  `scripts/refactor/t380_spell_visual_census.py --db data/willeq.db`). Both
  carry signal a data-driven visual layer needs: `target_anim` is non-zero for
  **3,608 of 8,066** spells (27 distinct values) and `travel_type == 3` for
  exactly **85** spells — that 85 IS the projectile population, and it is the
  only field that says a spell should have a travel stage. Adding them bumps
  `kWilleqDbExpectedVersion`, which invalidates EVERY build tree (T304/T355),
  so it must be its own leg. Found by T380.

- **B142** (C) `/bug <description>` (registered in
  `src/client/action/command_processor_registration.cpp` with the alias
  `report`) sends `HC_OP_Report`, which is eqemu's PLAYER-report channel, not
  its bug channel. Titanium has a separate `OP_Bug` (0x7ac2, `patch_Titanium.conf`)
  whose `Handle_OP_Bug` (`/mnt/projects/akk-stack/code/zone/client_packet.cpp:4253`) requires
  `app->size == sizeof(BugReport_Struct)` — an **8,736-byte** Titanium struct
  (`/mnt/projects/akk-stack/code/common/patches/titanium_structs.h:2604`) carrying `category_name[64]`,
  `character_name[64]`, `ui_path[128]`, position, heading, `time_played`,
  `target_id`, `target_name[64]`, `optional_info_mask`, `bug_report[2050]` and
  `system_info[4098]`. fable has no `HC_OP_Bug` at all. T375 made `/bug` correct
  ON THE CHANNEL IT USES (`AddReport` now files a real row), so nothing is
  broken; what needs Will's call is the product shape: (a) should `/bug` move to
  `OP_Bug` and gain a category table, and (b) should `/report` split off as
  `/report <player>` — the accused-player field the report channel actually has,
  which fable currently sends empty because its only caller reports a defect.
  Discovered by T375 while reading the two handlers side by side.

## P — process and documentation

- ~~**B024** (P)~~ **CLOSED 2026-09-04 (M00 merge): the structural programme is complete.** Batch K (K00–K16: `eq.h` 2,025 → 899, twelve components + four context services, the transition handle measured down to 83 uses = B038), Batch L (L01–L13: `irrlicht_renderer.h` 1,914 → 1,343, eleven components, residue 151 = B050 closed), Batch G (six of eight oversize methods decomposed and golden-pinned; `createDefaultConfig` ruled out; the variant loader parked as B052), M00 (no allowlist; the 1,500 ceiling enforced tree-wide by ctest). Original text: Component extraction of `class EverQuest` (2,025-line header, 685
  member functions, 178 data members, 37 flat `src/client/eq_*.cpp` TUs) and of
  `class IrrlichtRenderer` is the open structural programme —
  `refactor/K_component_extraction.md`, K01..K13, then Batch L. Both headers hold
  a no-grow allowlist entry that each K unit ratchets down. Prov: the J tracker's
  "WILL'S RULING", 2026-09-02."

- ~~**B025** (P) Eight methods dominate their TUs and were left out of Batch J
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
  `refactor/J_file_size_reclamation.md`, "Optional, NOT part of this batch".~~ **STALE — CLOSED by Batch G (2026-09-04):** G01 `DispatchZonePacket` (opcode table), G02 `ProcessBridgeIntents`, G03 `registerBuiltinCommands`, G04 `Application::initialize`, G05 `processFrameRender`, G06 `updatePlayerMovement`, T340 the variant loader; `createDefaultConfig` explicitly KEPT as a data table (`refactor/G_oversize_methods.md` §CLOSE-OUT). Re-measured 2026-09-05: `zone_dispatch.cpp` is 894 lines of table + handlers under the 1,500 ceiling.

- ~~**B026** (P) Two unmerged branches have never been audited —
  `task/eqcpp-zoneprocess-track-b-remaining` and
  `task/eqemu-zone-line-transition-scenario`, both measured 300 commits ahead of
  `fable` at this tip, both pre-dating the lettered programme.~~ **CLOSED
  2026-09-05 by T372** (`refactor/T372_unmerged_branch_survey.md` §Review),
  completing the survey `refactor/T305_branch_audit.md` began. The two branches
  are ONE branch with two three-commit tips: `merge-base(A,B)` is `4b5f2c8d`,
  **297 of each branch's 300 commits are shared**, and branch B's own three
  commits are two markdown files with ZERO lines of code. Every commit is
  classified: **(a) 8** already on `fable` by another route (T306's Daybreak +
  RDP ports, H03/T322's logout deferral, K09's `kZoneOpcodeTable`), **(b) 265**
  superseded (76 decomposition → Batches J/K/L/G, 180 Codex process
  scaffolding → `EXECUTOR_PROTOCOL.md` + this board, 9 a second x86 GLES2
  code path `fable` deliberately does not have), **(c) 24** still relevant —
  now B136, B137 and B138. Both branches are recommended for DELETION (B's
  immediately; A's once B136/B137 are worked and B138 is ruled on); a merge was
  measured non-viable at ~25,000 lines inside conflict regions. Prov: deferred
  item 22.

- ~~**B027** (P) Something on the eqemu server sets `Oraclerogue`'s persisted `gm`
  flag to 1 — four consecutive live units (T297, T320, T322, T323) saw it, none
  issued `#gm`, and T323 captured it before starting any client.~~ **STRUCK by
  T377, 2026-09-05 — THE PREMISE IS REFUTED: nothing sets it, the value is simply
  PERSISTED** (`review/T377/` §5). Source: `character_data.gm` has exactly ONE
  writer tree-wide, `Client::SetGM(bool)` (`/mnt/projects/akk-stack/code/zone/client.cpp:2659`), whose callers
  are the `#set gm [on|off]` handler, `Lua_Client::SetGM`, `Perl_Client_SetGM`
  and the `OP_GMBecomeNPC` arm that only ever sets it FALSE; load and save are a
  pure round trip (`pp->gm = e.gm` / `e.gm = pp->gm`, `/mnt/projects/akk-stack/code/zone/zonedb.cpp:501`
  and `:1096`), nothing derives it from account status, `world/` has no writer,
  and the only quest script that touches it (`quests/lua_modules/commands/don.lua`)
  *restores* an existing GM state around `#don`. Live: the flag was set to **0**
  by hand and sampled through a whole fable session as Oraclerogue — PRE-CLIENT 0,
  after login 0, after an explicit `#save` 0, after `#zone qeynos2` 0, after
  camp/quit 0. T297/T320/T322/T323 each observed a value set once by a
  `#set gm on` (the "kit out a character" recipe; `SetGM()` calls `Save()`) and
  persisted ever since. The flag was restored to 1. Prov: deferred item 43;
  T377 Review.

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

- ~~**B034** (B)~~ `include/client/graphics/irrlicht_renderer.h` — **SIX of the
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
  failure, and the oracle plays nothing. 7 new tests. ~~**What REMAINS on this
  clause is one row: coin out of a CORPSE'S LOOT WINDOW.**~~ **MEASURED T377,
  2026-09-05, on the same tone probe** (`review/T377/` §3; its `tonemap.json` is
  again byte-identical, `md5 0dc0708b…`, so all three units' rows are ONE
  measurement). **`BuyItem.WAV` 138 — the same generic coin sound as every other
  coin row — and the loot window's OPENING is SILENT** (`itemclth.wav`'s chord is
  absent from the window; both controls either side resolve zero non-bed tones,
  as do two right-clicks that missed the corpse). **Single trial**: nine further
  right-clicks on two later corpses never reopened the loot window, and a second
  asset (`stepcrch.wav`, a footstep) also resolves in the one good window, so the
  attribution rests on `buyitem` being the sound T328/T342 already measured for
  every other coin movement. **The GESTURE is not what T342 assumed** — there is
  no "click the coin": the client's own Looting Tip says *"Coins on the corpse
  are taken automatically by the first person to loot the corpse"* and
  `You receive 1 platinum, 8 gold, 3 silver and 4 copper from the corpse.`
  appears the instant the loot window opens. **fable plays NOTHING here and does
  not bank the coin either** — `CommandsComponent::ZoneProcessMoneyOnCorpse`
  (`src/client/commands/combat_actions.cpp:487`) logs and returns, with the
  comment `// For now, just log it`. That gap is **B172**. The recipe that worked,
  for the next unit: zone **`arena`** (no clutter), `#npctypespawn 113423`
  (`Gargalek_Daemado`, loottable 13487, 1144–7572 copper, a GIANT so the corpse is
  big), `/target Gargalek`, **`#damage 100000` not `#kill`** (`#kill` calls
  `Mob::Kill()` with no killer; `#damage` credits you), then step 20 units away
  and press **F9 for third person** so the corpse is on screen at all. Original
  text: T342 spawned an NPC
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

- ~~**B038** (B)~~ **CLOSED by T366 Leg B (`7ac4efe6`): the handle is at ZERO.**
  All four clusters retired, on the orchestrator's 2026-09-05 ruling that the
  transition handle goes entirely. (1) `m_test_bypass_send_guard` moved to
  `eqt::PacketSender` — the bypass belongs to the thing that sends; guards read
  `ctx_.net.TestBypassSendGuard()` and the 70 test sites arm it as
  `eq.Net().Test_SetBypassSendGuardForSeam(true)`. (2) movement's 22 `&owner_`
  identity keys are back on `this` (`const MovementComponent*`); one component per
  `EverQuest`, stated as an invariant comment. (3)
  `ZoneOnStatusChangeReconnectDisabled` moved into `eqt::zone::ZoneComponent` —
  its only two callers were the binds in that file. (4) `EQ::castRefusalMessage`
  takes `eqt::ClientContext&`; everything it reads is a context member. The
  `Owner&` ctor parameter and the `EverQuest& owner_` member are gone from group,
  movement, zone and bridge, and the last three `friend` lines with them —
  `CombatManager` and `TradeManager` are the only friends `eq.h` has left, and
  neither is a component. Final census: comment-filtered `owner_` under
  `src/client`/`include/client` outside `graphics/` = **0**; the 341 inside
  `graphics/` are Batch L's `IrrlichtRenderer& owner_` (163, eleven renderer
  components, the closed B050) and a `ZoneShaderManager*` back-pointer in
  `zone_shader_callbacks.h` (178). Proof: `TestBypassSendGuard()` forced to
  false fails 23 cases in `test_position_frame_roundtrip` and 2 in
  `test_group_accept_gm_hideme`; restored, both green. See
  `refactor/K_component_extraction.md` §CLOSE-OUT and `CLAUDE.md`. Original text:
  **the `EverQuest&` transition-handle residue — 83 -> 5.**
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

- ~~**B050** (B)~~ **CLOSED by L13 (2026-09-04): the `owner_.` residue in the
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
  applies Infravision's 0.75 + red shift, measured as R +5.5% / B +0.4% --
  **CLOSED by T360 Leg A**, `3bca791c`) and
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

- ~~**B058** (B) `/skills` vs `/newskills` — a NAMING choice, not a defect.
  `/newskills` (`renderer_slash_commands.cpp:422`) and the bare `skills` keyword
  piped stdin recognises (`console_input_handler.cpp:432` →
  `InputAction::ToggleSkills` → `renderer_frame_input.cpp:222`) toggle the same
  `skillsPopupState_.isOpen`, but `/newskills` additionally closes the spellbook
  and the inventory popup and echoes to chat, and `/skills` typed WITH a slash is
  unknown to all three registries — so the real client's name for the window does
  nothing. Closes when: Will rules on the name (rename `/newskills` to `/skills`
  and keep one body, or keep both and say why). Do not merge them silently: the
  two bodies are not equivalent. Prov: T347 (B013 measurement).
  **WILL RULED 2026-09-05: rename to `/skills`, REMOVE `/newskills` (no alias);
  keep the fuller body (closes spellbook + inventory, echoes). → T363. Now class B.**~~
  **CLOSED by T363** (`refactor/T363_skills_rename.md`, branch
  `task/T363-skills-rename` off `e69410f4`). The command is `/skills`, no alias;
  the one body is `FrameComponent::toggleSkillsWindow()`, which the slash arm and
  the K keybind (`RendererAction::ToggleSkills`) both call, so the popup mutual
  exclusion is now reached from BOTH. **The entry's middle arrow was WRONG and the
  measurement is worse than it recorded:** `InputAction::ToggleSkills` does not
  reach `renderer_frame_input.cpp` at all — it is a different enum, and NOTHING
  consumes it (`InputActionBridge::processDiscreteActions` has no arm), so the
  bare `skills` word on piped stdin did **nothing whatsoever**, it did not toggle
  the popup. The interception is deleted; the word now falls through to
  `m_rawCommands` → `Application::DispatchSlashCommand("skills")` → registry 3's
  `/skills`. Pinned by 11 tests (3 in `test_slash_commands_component`, 4 in
  `test_headless_command_refusal`, 4 in `test_merged_help`), all failing before
  the change. Two new items fell out: **B090** and **B091**.

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
  **WILL 2026-09-05: expects Troll = Ultravision, Ogre = Infravision, but the
  official client decides — T361 captures the per-race night floor.**

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

- ~~**B066** (A) `src/client/graphics/sim_animation.cpp`
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
  Prov: T358 Review, `review/T358/logs/frame_rgb_visionvid.txt`.~~ **CLOSED by T360 Leg A**
  (`3bca791c`, `refactor/T360_vision_swap_packet_sender.md`): the mapping now
  lives once, in `visionLightModifiers()`
  (`include/client/graphics/vision_type.h`), and switches on `VisionType`
  rather than on an int -- neither call site in `sim_animation.cpp` writes a
  magic number, and the uint8_t the worker boundary carries is cast to the
  enum in exactly one place. `VisionType` moved out of `renderer_types.h` into
  that new leaf header because `renderer_types.h` INCLUDES
  `simulation_worker.h`, so the dependency could not run the other way; every
  existing spelling resolves unchanged. The comment on
  `SimulationInput::visionType` is corrected. The intensity NUMBERS are
  untouched -- that is B067. Pinned by `tests/test_vision_zone_lights.cpp`,
  which drives both real functions through `computeOnce()` (light 0 through
  `output.selectedLights`, the animated light 1 through
  `output.zoneLightAnimColors`): 2 of 4 failed pre-fix, 4 of 4 pass after.

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
  x1.554. ~~see **B080** for why the residual is not smaller.~~ **T367 (2026-09-05)
  re-checked that attribution and it is WRONG: B080's 35x night gap does not
  reproduce — at T361's own spot fable's night is 1.30x BRIGHTER than the
  oracle's, not 35x darker (T367 Leg A table). T361's residual therefore has
  some other cause and is still unexplained. T367 did not re-measure the vision
  ratios themselves (its change moves the night baseline at that spot by +7.2%,
  one distant light now surviving), so a re-measurement against the oracle table
  is OWED.** **RE-MEASURED T377 (`review/T377/` §2), 2026-09-05, with T367's fix
  in the binary — the row is now taken and the OWE is CLOSED.** BOTH clients were
  re-measured in the same session, same spot, same 700x420+40+40 crop, linear
  light, fable stepped over 1167 dumped frames and the oracle over a 2164-frame
  video: **the oracle reproduces to 0.4%** (Ultravision 1.554 -> **1.560**,
  Infravision 1.253 -> **1.252**), which is the control that makes fable's row
  readable. **fable's Infravision now matches the oracle: 1.237 vs 1.252 (99%).
  fable's Ultravision is 1.449 against 1.560 (93%) — and T367's light-culling fix
  moved it slightly DOWN, not up (T361's 1.473 -> 1.449).** So the ~7% residual
  on the Ultravision lift is real and is still unexplained; the absolute night
  levels are NOT compared (T367's composition rule). What the re-measurement DID
  find is that the scalar ratio hides a per-channel error: the oracle's
  Ultravision is near-neutral with a blue lift (R x1.458 / G x1.463 / B x1.851)
  while fable's green lags (R x1.461 / **G x1.281** / B x1.663), and the oracle's
  Infravision raises all three channels (R x1.328 / G x1.211 / B x1.208) while
  fable's **drives blue DOWN to x0.821** at R x1.600. That is **B171** — **STRUCK PROVISIONALLY by T383 (`776357f1`), live re-measure OWED; T383 also established that NO code path in the tree can lower a channel under a vision buff, so the x0.821 is unexplained by the source and the re-measure has to account for it.** See B110 for the term that does separate the two clients.** Pinned by
  `tests/test_vision_ambient.cpp` (3 of 6 cases fail against the pre-T361
  behaviour). Prov: T361 Review, `review/T361/README.md`. **CORRECTION (T383): `visionType` now has THREE
  consumers — T361 added `IrrlichtRenderer::updateTimeOfDay` ->
  `applyVisionAmbientFloor`, the global-ambient floor, and it is the dominant
  one. The two below are the zone-light arms.** `visionType` had exactly two consumers tree-wide
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

- ~~**B080** (A) `src/client/graphics/renderer/irrlicht_renderer.cpp`
  `IrrlichtRenderer::updateTimeOfDay` / the zone lighting as a whole —
  **fable's night is ~35x darker than the official client's on near geometry.**~~
  **STRUCK by T367 (`b572846f`, branch `task/T367-night-brightness-oracle`): the
  35x DOES NOT REPRODUCE, and the lead underneath it DOES.** T367 ran the same
  four-state sequence on both clients — two qeynos2 coordinates x forced 11:00 /
  22:00, camera untouched inside each client — and read every state off a flat
  window of a video or a frame dump, never a single still.
  **The 35x was two different views, not two brightnesses.** `#goto` leaves the
  official client at heading 0.00 (its own `#loc` says so) while fable keeps its
  camera yaw, so the two frames at one coordinate show different geometry;
  T361's cross-client crop was measuring composition. Measured properly at a
  **lamp-free** spot at 22:00, fable reads **0.005502** linear grey and the
  oracle **0.004245** — fable is **1.30x BRIGHTER**, not 35x darker. **fable's
  night ambient floor is not the defect**, and neither is gamma (no sRGB
  framebuffer in either client, and the two agree to 1.34x at noon, where a 2.2
  exponent would also have shown).
  **The lead was right:** fable's own census read `ZoneLightVis: 0/218 visible`
  on EVERY frame of a whole session — including 6 units from a radius-40 lamp,
  where 54 lights were inside the render distance and all 54 were thrown away by
  the PVS or frustum test. The lights were never "data-only"; they were 100%
  CULLED before selection, so `uLightPos[1..7]` were never fed. **Fixed** in
  `SimulationWorker::computeLightVisibility` / `computeObjectLightVisibility`
  (`src/client/graphics/sim_visibility.cpp`): a light whose own influence sphere
  contains the camera survives PVS and frustum culling; a light outside its own
  radius is culled exactly as before. Census after: `1/218` and `5/218` zone,
  `3/107` and `14/107` object. Worth **+7.2%** on the night frame and −0.2% by
  day. Pinned by `tests/test_zone_light_culling.cpp` (3 of 4 fail pre-fix).
  **The residual is NOT this and is boarded separately as B110**: the official
  client lights zone geometry from the WLD's baked per-vertex colours and fable
  discards them. Prov: T367 Review, `review/T367/README.md`.

- ~~**B068** (B) `src/client/entities/entities_component_world.cpp`
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
  `review/T358/logs/death_burst_server_cs.txt`.~~ **CLOSED by T360 Leg B**
  (`140acbd7`, `refactor/T360_vision_swap_packet_sender.md`): both senders now
  call `ctx_.net.SafeQueueZone` -- the same call `SendSave` uses -- which feeds
  the T268 ordered-capture seam BEFORE its gates and the session recorder after
  them, so the hand-written `Test_RecordQueuedPacket` line T346 added is
  deleted and one call site feeds both observers. `SendAnimation` keeps its
  unreliable stream-0 send. Pinned by two new tests in
  `tests/test_self_death_sequence.cpp`: one opens a real `SessionRecorder` onto
  a temp `.sleq` and reads the appearance and the animation back out of the
  file; the other replays the whole T311 death burst and projects the RECORDED
  C->S zone stream through the same projection as the T268 seam, against the
  same `self_death_outbound_g07.txt`. Pre-fix the recorder saw zero
  appearances and the recorded burst was two lines where the oracle has three.
  The golden's three compared lines are UNCHANGED; its header learned the
  second observer in place. Fixture consequence, not a defect:
  `test_forced_pose_no_echo`'s `PoseHarness` reads the DaybreakConnection
  resend map and had to set `conn->m_stats.sent_packets = 1`, because
  `SafeQueueZone` refuses a connection whose stats are all zero -- the same
  line `DeathHarness` has carried since T346.

## Added 2026-09-04 by T357 Leg A (B057)

- ~~**B069** (B) `src/client/action/command_processor.cpp`
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
  A (step 1 measurement).~~ **CLOSED by T362 Leg B (`465429b2`)**: both call
  `eqt::completion::PrefixCompletions` (`include/client/command_completion.h`),
  which sorts AND de-duplicates, and both `toLower` bodies call
  `eqt::completion::ToLower`. The header sits at the client include root, not
  under `action/` or `graphics/ui/`, so neither registry gains an include edge
  on the other's layer; it is header-only and templated on the two maps'
  different mapped types, so it adds no link dependency and no CMake change.
  Pinned by the new `tests/test_command_completion.cpp` —
  `CommandRegistryCompletionTest.ACommandRegisteredWithItsOwnNameAsAnAliasCompletesOnce`
  offered `consider` twice pre-fix, once post-fix.
## Added 2026-09-04 by T359 (B003d close-out)

- ~~**B070** (B) `src/client/commands/commands_movement.cpp`
  `CommandsComponent::RequestWarp` — the LAST caller of the unranked
  case-insensitive prefix resolver `EntitiesComponent::FindEntityByName`, and the
  reason T359 could not delete it. `/warp <entity_name>` takes a genuinely
  user-typed name, so it needs a resolution, but it uses the unranked one
  (lowest spawn id wins) rather than `EntityManager::findEntityByName(name, x, y,
  z)`, the T244 nearest-first ranked overload that `/target`, `/face`, `/moveto`
  and `/follow` all use since T350/T356. Same defect shape as B003b/B003c, one
  command later: with two same-named mobs `/warp` teleports to a different one
  than `/target` picked. `/warp` is a GM/debug command, which is why this is B and
  not A. Closes when: `RequestWarp` resolves through the ranked lookup (ideally at
  the dispatcher, as T356 did) and `FindEntityByName` is deleted with its last
  caller. Prov: T359 Leg A (`310725a4`).~~ **CLOSED by T362 Leg A
  (`4a06d2e2`)**: `/warp` does pass through `ActionDispatcher`, so the resolution
  moved there. `ActionDispatcher::warp()` parses its own arg string — empty and
  three-float forms still go down as the raw string to `IActionHandler::warp()`;
  a name resolves ONCE through `EntityManager::findEntityByName(name, px, py,
  pz)` and goes down as a spawn id via the new `IActionHandler::warpToEntity()`
  (game side: `CommandsComponent::RequestWarpToEntity`). An unresolvable name
  fails at the dispatcher and never reaches the handler.
  `EntitiesComponent::FindEntityByName` is **DELETED**, header and body, leaving
  one name resolver in the client; the deleted body carried no `pushEvent`
  (K12). Flagged narrowing: the deleted resolver substituted underscores for
  spaces in the search term and the ranked one does not — the same narrowing
  T350/T356 accepted for `/face`, `/moveto` and `/follow`. Pinned by
  `ActionDispatcherTest.Warp_ByName_PicksTheNearestMatch_NotTheLowestSpawnId`
  plus two siblings.

## Added 2026-09-05 by T363 (B058 close-out)

- ~~**B090** (A) `src/client/input/console_input_handler.cpp`
  `ConsoleInputHandler::processCommand` — the `inventory`/`inv` and `group`
  keywords on piped stdin set `InputAction::ToggleInventory` /
  `InputAction::ToggleGroup`, and **nothing consumes either**:
  `InputActionBridge::processDiscreteActions`
  (`src/client/action/input_action_bridge.cpp`) has no arm for any of the three
  UI toggles. T363 measured the set mechanically (the InputAction literals
  console_input_handler produces minus the ones input_action_bridge consumes =
  ToggleInventory, ToggleSkills, ToggleGroup) and deleted only the `skills` one,
  which was its territory. So a `--script`/piped `inventory` line silently does
  nothing today — the same defect B058 recorded for `skills`, one keyword over.
  Closes when: `inventory`/`inv` falls through to `m_rawCommands` the way
  `skills` now does (registry 3 knows `/newinv`, so the fall-through needs the
  word to BE a catalog name, or an explicit rewrite to `newinv`), and `group`
  either gets a consumer or is deleted with its dead action. Note the asymmetry:
  `skills` worked as a fall-through only because T363 gave registry 3 a command
  of exactly that name. Prov: T363 (B058 close-out measurement).~~
  **CLOSED by T365 Leg A** (`4cb806f5`, branch
  `task/T365-stdin-keywords-help-completion` off `8fd539ed`). `/newinv` is
  renamed **`/inventory`** in registry 3 (catalog row + if/else chain, no
  alias, Will's B058 shape), so the bare `inventory` keyword falls through to
  `m_rawCommands` and reaches the one body. **`inv` could NOT fall through**
  and the entry did not anticipate why: registry 2 registers `inv` as an
  ALIAS OF `/invite` and is tried before registry 3, so the word would have
  invited a group member. It is rewritten to `inventory` in
  `ConsoleInputHandler` — the only place that knows which window was meant —
  and `/inventory` deliberately has no `inv` alias. **`group` had no consumer
  to route to:** `RendererAction::ToggleGroup` (the G keybind's enum, not the
  `InputAction` one) is an EMPTY arm in `renderer_frame_input.cpp` —
  "Group panel visibility is driven by inGroup state" — so there is no
  group-window toggle anywhere in the tree; the keyword is deleted and the
  word is now honestly unrecognized. `InputAction::ToggleInventory`,
  `ToggleSkills` and `ToggleGroup` are all deleted. Pinned by 6 new tests
  (4 `HeadlessCommandRefusalTest`, 2 `MergedHelpTest`), all failing before.
  Residue: `InputAction::ToggleVendor`, `TogglePetWindow`, `ToggleTrainer`
  and `ToggleSpellbook` are consumer-less in the same way but have no
  producer either — declared and unreachable → **B100**.

- ~~**B092** (B) `src/client/help/merged_help.cpp` `BuildHelpResponse` —
  a bare `/help <arg>` resolves `<arg>` as a CATEGORY before it tries a command
  name, so a command whose name equals a category name has no reachable help
  entry. T363 hit this immediately: registry 2 has a `"Skills"` category
  (bandage/train/trainclose/track), so `/help skills` prints that category and
  the renamed command's own detail block (`=== skills ===`,
  `Registry: renderer (graphics)`) can no longer be reached by name —
  `/help newskills` used to reach it. `/help graphics` still lists it, and the
  command itself is unaffected. Closes when: the resolution tries both and says
  so (e.g. prints the command detail followed by "…is also a category"), or the
  order is inverted with the reverse collision handled. Pinned as it stands by
  `MergedHelpTest.T363_HelpSkillsResolvesToTheSkillsCategoryNotTheCommand`.
  Prov: T363.~~
  **CLOSED by T365 Leg B** (`d7ea0ef4`). `BuildHelpResponse` now resolves the
  argument as a COMMAND NAME first and, when the same word is also a category,
  appends the category after a one-line `"X" is also a category:` note — both
  reachable in one response, nothing that used to print is lost. Command-first
  was chosen because the official Titanium client's `/help <x>` takes a command
  name and has no merged categories, so there is no oracle for the collision
  itself but there is one for the un-collided case.
  `T363_HelpSkillsResolvesToTheSkillsCategoryNotTheCommand` is renamed
  `T365_HelpSkillsPrintsTheCommandFirstThenTheCategory` and rewritten to the
  new truth; `/inventory` vs the "Inventory" category and a category that is
  NOT also a command name are pinned alongside it.

- ~~**B091** (P) `refactor/T357_tab_completion_spell_land_eqe1_gate.md` §Review and
  the two `CLAUDE.md` sentences it added
  (`include/client/graphics/ui/CLAUDE.md`, `src/client/input/CLAUDE.md`) claim
  that because Tab and `/help` share the same merged pair, "Tab and `/help` can
  never list different commands." **They already do.** `/help` lists THREE
  registries — `BuildCategoryOverview` appends registry 3's whole Graphics
  category from `AllRendererCommands()` — while `Application::commandCompletions`
  unions only registries 1 and 2, because registry 3 has no `getCompletions()`.
  So none of the ~70 renderer commands is tab-completable: `/newskills` was not
  before T363 and `/skills` is not after it (T363 verified this by reading
  `CommandProcessor::getCompletions` and `commandCompletions`; the rename changed
  nothing here). Closes when: either the completion provider also offers the
  static catalog's names, or all three claims are corrected to say Tab covers
  registries 1+2 only. Prov: T363 (plan step 3 check).~~
  **CLOSED by T365 Leg C** (`c70501c8`) — BOTH halves, not just the doc one.
  Registry 3 got a completion source: `eqt::help::RendererCommandCompletions`
  (`src/client/help/renderer_command_catalog.cpp`), built on T362's shared
  helper `eqt::completion::PrefixCompletions`, so all three registries run the
  one prefix idiom. `Application::commandCompletions()` unions it last and is
  now **public** so the union is asserted end-to-end rather than pinned as
  source text. `/ski<Tab>` completes to `/skills`. The three claims are
  corrected in place: the T357 Review paragraph plus its deviation note,
  `include/client/graphics/ui/CLAUDE.md` and `src/client/input/CLAUDE.md`.
  Pinned by 2 `HeadlessCommandRefusalTest` cases (failing before) and 6
  `RendererCommandCompletionTest` cases.

## Added 2026-09-05 by T365

- ~~**B100** (B) `include/client/input/input_handler.h` `eqt::input::InputAction`
  — `ToggleVendor`, `TogglePetWindow`, `ToggleTrainer` and `ToggleSpellbook`
  are declared, have **no producer** (`ConsoleInputHandler` never sets them and
  no other InputHandler does) and **no consumer**
  (`InputActionBridge::processDiscreteActions` has no arm for any of them).
  T365 deleted the three that at least had a producer
  (`ToggleInventory`/`ToggleSkills`/`ToggleGroup`); these four are the residue
  and were out of that unit's territory. Note `test_input_action_bridge.cpp`'s
  `UIToggleActions_AreNotBridgeTerritory` now uses `ToggleVendor` as its
  stand-in, so deleting them means restating that case over a surviving
  enumerator (or deleting it — with all seven gone, "not bridge territory" has
  no subject left). Closes when: each is either produced-and-consumed or
  deleted, and the compiler confirms no reader remains. Prov: T365 Leg A.~~ **STRUCK T368 Leg B (`8afdc6d7`)**: all four deleted; the enum now has NO UI-toggle section, with a tombstone naming the two enums a UI toggle actually belongs in (`eqt::input::HotkeyAction` and `EQT::Graphics::RendererAction`, both untouched and both live). The plan's instruction to DELETE `UIToggleActions_AreNotBridgeTerritory` was wrong -- its set also held `ToggleWireframe`/`ToggleHUD`, which `GraphicalMode` really does consume with `consumeAction()`, so the case has a subject and was RESTATED over four surviving graphics toggles as `GraphicsToggleActions_AreNotBridgeTerritory`. Pinned by `InputActionEnumTest.T368_NoConsumerlessUITogglesRemain` (a whole-line source scan of the header, failing pre-fix with all four found).

- ~~**B101** (B) `src/client/graphics/frame/renderer_frame_input.cpp`
  `case RA::ToggleGroup:` — the G keybind (and the Alt+P alternate binding,
  `hotkey_manager.cpp`) reaches an EMPTY arm: "Group panel visibility is driven
  by inGroup state". So **G does nothing** — pressing it in a live session
  neither opens nor closes anything, which is not what a key bound to
  "ToggleGroup" promises. `RA::TogglePet` and `RA::ToggleBuffWindow` are empty
  for the same stated reason. T365 measured this while looking for a consumer
  for the `group` stdin keyword and found none, which is why that keyword was
  deleted rather than routed. Closes when: either the three keys get real
  toggles (a `groupPanelState_.isOpen` the panel honours, the shape
  `FrameComponent::toggleSkillsWindow()` has) or the bindings are removed so no
  key advertises a behaviour the tree does not have. Class C if Will wants the
  panels to stay auto-driven. Prov: T365 Leg A.~~ **STRUCK T368 Leg A (`466bcadc`)**: all three keys got real toggles -- `FrameComponent::toggleGroupWindow()`, `togglePetWindow()`, `toggleBuffWindow()`, the `toggleSkillsWindow()` shape. Each panel's render early-returns on a free predicate (`groupPanelIsVisible()` / `petPanelIsVisible()` / `buffBarIsVisible()`, `static_panels.h`) that composes the new `isOpen` with the auto-driven state: group = `isOpen` alone (an open window with no group draws its empty titled frame; joining a group never reopens a closed one), pet = `isOpen && hasPet`, buffs = `isOpen && at least one buff` -- and `hitTestBuffBar()` reads the same predicate, so a hidden icon is unclickable. `isOpen` defaults TRUE because the official client's `defaults.ini` writes `Show=0` only under a section whose window ships closed and writes none under `[GroupWindow]`, `[PetInfoWindow]` or `[BuffWindow]`. Bindings unchanged -- see B115 for why. Fixed in passing: `IrrlichtBridge`'s `PetRemoved` arm reset `petPanelState_` wholesale, which put `isOpen` back to true and silently reopened a window the player had closed. Pinned by 10 cases, 9 failing pre-fix.



## Added 2026-09-05 by T367 (the night oracle)

- **B110** (C) `src/client/graphics/eq/wld_fragments_mesh.cpp`
  `WldLoader::parseFragment36` / `src/client/graphics/eq/zone_geometry.cpp` /
  `src/client/graphics/zone_shader_sources_gles2.h` — **fable throws away the
  zone's baked per-vertex lighting, which is what the official client uses to
  light geometry at night.** `parseFragment36` reads `header.colorCount` and
  then `ptr += sizeof(uint32_t) * header.colorCount;` under the comment
  `// Skip colors`; the mesh builders then write `SColor(255,255,255,255)` into
  every textured zone vertex, so `aColor` in the zone shader is a constant.
  **MEASURED, T367** (temporary instrumentation on `parseFragment36`, one
  qeynos2 load, reverted before commit — `review/T367/logs/qeynos2_baked_vertex_colours.txt`):
  all **3,830** `R<n>_DMSPRITEDEF` region meshes carry `colorCount ==
  vertexCount`, **72,436** zone vertices carry a colour, the vertex-weighted
  mean byte is **44.2/255 (0.173)** with per-mesh minima at 0 and maxima at 255
  — a real baked light distribution, not a constant. The consequence, measured
  live at one coordinate 6 units from a lamp: the official client's night sits
  only **17.1x** below its own day there while fable's sits **47.5x** below, so
  a lamp is worth **2.8x** of night light that fable does not have. T367's
  culling fix (see B080) restores the point-light path and is worth 1.07x of
  that 2.8x; the rest is the baked colours plus the fact that fable evaluates
  zone point lights PER VERTEX on large zone triangles (only the player light is
  per pixel), which smears a lamp instead of making a pool.
  **This is class C, not A, because it cannot be done without Will**: the
  colours have no field on `ZoneGeometry::vertices` (loader + both mesh builders
  + the atlased builder change), and the shader currently computes
  `vColor = baseLighting * uTintColor * aColor`, under which multiplying EQ's
  baked light by a 0.08 night ambient makes the world DARKER, not brighter —
  EQ's model is that the baked colour IS the light. Changing the zone lighting
  equation moves every pixel golden, so a deliberate re-bless with the gate's
  contact sheet is unavoidable and may not be self-blessed. Closes when the
  baked colours are loaded, the lighting equation matches the oracle, and the
  lamp-spot day/night ratio lands near the oracle's 17.1x.
  Prov: T367 Review, `review/T367/README.md`.
## Added 2026-09-05 by T368

- **B115** (B) The official client's DEFAULT KEYS for the group, pet and buff
  windows are not recorded in any file on disk, so fable's `G` / `Alt+P` (group),
  `P` (pet) and `Alt+B` (buff) in `src/client/input/hotkey_manager.cpp` are
  UNVERIFIED against the oracle. Measured by T368, without the server:
  `eqgame.exe` holds a 247-entry table of `KEYMAPPING_` action names (file
  offset `0x2945a8`, VA `0x6945a8`) which includes `TOGGLE_PARTYWIN` (index
  152), `TOGGLE_BUFFWIN` (155) and `TOGGLE_PETINFOWIN` (168) — so the ACTION
  names are settled — but the table is a bare `char*` array with no parallel
  default-scancode array anywhere in `.data`/`.rdata` (scanned at strides 1, 2,
  4, 8, 12 and 16 for a 247-long window of plausible scancodes; nothing). An
  `eqclient.ini` records a `KEYMAPPING_<NAME>_<n>` line only for a binding the
  user CHANGED, and the only such lines in ANY of the seven `eqclient*.ini` /
  `p1999-*.ini` files under `/mnt/projects/EverQuestP1999` are five movement
  overrides (`FORWARD_2=17`, `BACK_2=31`, `RIGHT_2=32`, `LEFT_2=30`,
  `DUCK_1=208`/`45`). Closes when: someone screenshots the official client's
  Options > Keys tab (needs the eqemu lock, held by T367 at the time) and either
  confirms fable's three or names the official ones. Related, and ALSO
  unverified: what the official group window PAINTS with no group —
  `defaults.ini` proves it is SHOWN by default (no `Show=0` under
  `[GroupWindow]`) but not what is inside it; fable draws the titled frame with
  no member rows. **MEASURED T377 (`review/T377/`), 2026-09-05 — BOTH HALVES
  ANSWERED, and fable is WRONG on two of three keys.** The official client's
  Options > Keys > category **UI** page reads, screenshotted at
  `review/T377/oracle/T377_oracle_options_keys_UI_page1..4.png`:
  **Toggle party window = `Alt P`** (the group window; Alternate `-------`),
  **Toggle buff window = `Alt B`**, **Toggle pet info window = `Alt F1`**, and
  — the sting — **Toggle Customer Service Win = `P`**. Against
  `hotkey_manager.cpp:486-492`: buff `Alt+B` **MATCHES**; group's `Alt+P`
  alternate matches but its PRIMARY `G` is an invention (`G` is bound to nothing
  in the UI or Commands category; `Alt G` is Guild Management); pet's `P` is
  **wrong AND collides with the official Customer Service key**, which should be
  `Alt+F1`. `Alt+P` was also verified live (it hides and re-shows the Group
  window). **The empty group window paints ONLY ITS TITLE BAR** — a single strip
  reading `Group`, no member rows, no empty body, no reserved height — visible in
  every `review/T377/oracle/*.png`; fable draws a titled *frame*. The rebinding
  and the empty-window shape are **B170**. Prov: T368 Leg A; T377 Review.

---

## Added 2026-09-05 by T371 (documentation truth sweep, T352-T370)

- ~~**B130** (P) The orientation docs carry hand-maintained COUNTS with no
  regression net, and T371 found every one of them wrong at `a7e760dc`:
  `src/client/input/CLAUDE.md`'s `InputAction` enum (said 40, measured 33 —
  T365 and T368 deleted seven consumer-less enumerators) and `HotkeyAction`
  enum (said 80, measured 94); the `registerCommand({...})` count in
  `src/client/input/CLAUDE.md` and `include/client/graphics/ui/CLAUDE.md`
  (said 142, measured 143 — T347's `/cleartarget`);
  `include/client/bridge/CLAUDE.md`'s intent-variant count (said 35, measured
  36 — T328 Leg B's `PlaySoundIntent`, which the list also omitted);
  the `owner_` handle count in `CLAUDE.md` and
  `src/client/graphics/CLAUDE.md` (said 151, measured 165); `README.md`'s
  registered-test/executable counts (said 2746/123, measured 3949/232). Each
  was written with a "counted <date>" or a commit hash, so the staleness is
  invisible without re-counting. `DocClaims` (T371) is a NAME table and
  catches none of this. Closes when either the counts are generated from the
  tree at build time, or a test asserts the doc numbers against the tree the
  way `command_registration_order_g03.txt` asserts the registration count.
  T371 corrected all of them in place and, where the number is inherently
  drifty (the suite total, the `owner_` census), replaced it with the command
  that measures it.~~ **CLOSED by T373 Leg A `4bd4709a`**
  (`refactor/T373_doc_counts_net.md`): `scripts/refactor/check_doc_counts.py`,
  registered as the `DocCounts` ctest test under label `hygiene` beside
  `DocClaims`. Seven counts are netted over fourteen doc sites against
  measuring functions over the tree -- the `InputAction` (33) and
  `HotkeyAction` (94) enum bodies, `registerCommand({` call sites in
  `command_processor_registration.cpp` (143), the `RendererIntent`
  `std::variant` (36), the `loadZoneStep*` functions (20), and the
  `*_component.h` headers per family (graphics 11, client 12) -- and a doc
  site that LOSES its sentence fails as MISSING rather than passing silently.
  The two counts that must NOT be pinned (the registered-test total, the
  `owner_` census) are asserted the other way round: the doc must still name
  its measuring command. Planted fault (`HotkeyAction` 94 -> 80) failed
  naming file, line, stated and measured number; reverted byte-for-byte and
  green. One further drift found in passing and fixed: `README.md`'s
  directory tree still said "123 test executables" (T371 corrected only the
  prose paragraph). Prov: T371.

- ~~**B131** (P) `refactor/BOARD.md` and the batch close-outs (`refactor/K_*.md`)
  are inside `DocCitations`' live-doc set but deliberately OUTSIDE `DocClaims`'
  scan (T371 measured 39 retired-name hits in them, every one legitimate
  struck-item ledger text — `/newskills`, `setOnSpellLand`,
  `EntitiesComponent::FindEntityByName`, the five T354 collision functions, and
  so on). The consequence: if a FUTURE board entry describes LIVE code using a
  name that has since been retired, nothing catches it, and the board is the
  first thing a new agent reads. Closes when either the ledger text is
  distinguishable from live description (a per-entry struck marker the scanner
  can honour) or someone rules that board drift is acceptable.~~ **CLOSED by
  T373 Leg B** (`refactor/T373_doc_counts_net.md`, the tip of
  `task/T373-doc-counts-net`; Leg A is `4bd4709a`): the
  exemption became a RULE. `check_doc_claims.py` now scans
  `refactor/BOARD.md`, one top-level bullet at a time, and only LIVE bullets
  -- a bullet whose text opens with the strike marker is a closed ledger
  entry and is skipped whole, together with every continuation line under it,
  up to the next top-level bullet or heading. Prose belonging to no bullet
  (the class/rules preamble, the section headings) is not an entry and is not
  scanned. The pass line reports how many live entries were scanned -- 19 at this
  tip: the 15 live items plus the 4 header bullets, out of 88 top-level
  bullets in the file. The batch
  close-outs and the unit plans stay OUT entirely: they are historical end to
  end and have no live half to separate, which is the one part of T371's
  exemption that was right. Planted fault: `setOnSpellLand` written into a
  live entry -> FAIL naming the entry's line; reverted byte-for-byte and
  green. Prov: T371.

## Added 2026-09-05 by T372 (the two never-audited branches, B026 closed)

Every item below is the (c) residue of `task/eqcpp-zoneprocess-track-b-remaining`
— the only content of either branch that `fable` does not already have by
another route. Full classification and evidence:
`refactor/T372_unmerged_branch_survey.md` §Review. Nothing was ported; each
entry is described here in enough detail to work WITHOUT the branch, so the
branches can be deleted independently of these items.

- ~~**B136** (A) `eqt::entities::EntitiesComponent::m_pending_door_clicks`
  (`include/client/entities/entities_component.h`) is never cleared at zone
  change. Door IDs are `uint8_t` and are reissued by each zone's `SpawnDoor`,
  so a `ClickDoor` sent just before zoning leaves a stale entry that flags the
  NEXT zone's first matching `OP_MoveDoor` with the same numeric id as
  user-initiated — which changes the `userInitiated` field of the
  `DoorStateChanged` event the renderer consumes, plus the debug log line. The
  member has exactly four occurrences tree-wide (declaration; `insert` in
  `SendClickDoor`, `src/client/entities/entities_component_world.cpp`; `erase`
  in the `MoveDoor` handler, same file; one comment) and none of them is a
  clear-on-zone-change. Closes when: the set is cleared on the same zone-cleanup
  path that clears the door state manager, with a test. Prov: T372, branch
  commit `ec7f14da`.~~ **CLOSED by T374 Leg A** (`3ef7826c`):
  `EntitiesComponent::ClearPendingDoorClicks()` is called from
  `ZoneComponent::CleanupZone()` one line after `game_state.doors().clear()` --
  the one cleanup site, no second one added. Pinned by
  `tests/test_door_click_zone_reset.cpp`
  (`DoorClickZoneReset.PendingClickDoesNotSurviveAZoneChange`, RED pre-fix:
  `userInitiated` was `true`; plus the preservation case
  `ClickThenMoveDoorInTheSameZoneIsUserInitiated`, green both sides).

- ~~**B137** (B) `HC_OP_TargetCommandReply` is registered in `kZoneOpcodeTable`
  (`src/client/zone/zone_dispatch.cpp`, the `OnTargetCommandResponse` entry) as
  the bare literal `0x61f9` with only a trailing comment, while every sibling
  entry uses an `HC_OP_*` enumerator and `eq_strings.cpp` can name it. Closes
  when: the enumerator exists in `include/client/eq_opcodes.h` beside
  `HC_OP_TargetCommand`, the table and `src/client/core/eq_strings.cpp` use it,
  and (optionally, as the branch did) a self-check asserts every registered
  opcode resolves to a name. Prov: T372, branch commit `6c082a4f`.~~
  **CLOSED by T374 Leg B** (`7d5e1bed`). `HC_OP_TargetCommandReply = 0x61f9`
  is in `include/client/eq_opcodes.h` beside `HC_OP_TargetCommand`; the table
  and `GetOpcodeName` use it. A SECOND bare literal was found in the same
  table and fixed with it: `0x65ca`, whose `HC_OP_Consider` enumerator already
  existed and simply was not used. The optional self-check was taken and
  measured 23 dispatched opcodes with NO `GetOpcodeName` entry (every
  unhandled-opcode WARN and DumpPacket header for them read
  `OP_Unknown_0x....`); all 23 already had enumerators and all 23 cases were
  added. Pinned by `tests/test_zone_opcode_literals.cpp` (source reader over
  `zone_dispatch.cpp` + the two runtime checks); RED pre-fix on two keys, green
  after. The G01 golden `tests/goldens/zone_opcodes_g01.txt` is byte-identical
  and `test_zone_dispatch_table` passes unedited (6/6).

- **B138** (C) `fable` has no live-server scenario lane and no exit-code
  contract at all: `src/client/main.cpp` ends `return 0;` unconditionally, so
  every live verification on this board finishes with a human reading a
  transcript. The audited branch built one — a `--scenario <name> --timeout <n>`
  client mode over a `ScenarioRunner` + `IScenario` registry; a taxonomy shared
  verbatim between the client and the scripts (0 pass / 2 operator error /
  3 stale session refused / 4 kick failed / 10 assertion failed / 11 timeout /
  12 stack failure) with a guard so an external quit mid-run cannot masquerade
  as success; a `MOD_SCENARIO` log module pinned above `--log-level` so pass/fail
  survives quiet logging; one real scenario (`arena-idle`, a four-stage C→S→C
  position-acceptance oracle asserting zone-in, a nudge, the throttled send, and
  the server echo within 1.5 units); and `test/harness/`
  (`precheck`→`reset`→`seed`→`normalize`→`run-scenario` over SQL fixtures, with
  a world-telnet stale-session kick that `fable`'s `.eqemu-lock` convention
  cannot see). `fable` has the half the branch lacks — `scripts/eqemu/up.sh` /
  `down.sh` / `status.sh`. Both T305 and T372 recommend REBUILDING this against
  `fable`'s architecture (~500 lines of C++ plus bash), not porting it; T305
  sized it at 1–2 units. **Needs Will: this is new capability, not a defect
  fix.** Closes when: Will rules build or drop. Prov: T372, the 22-commit
  scenario cluster on `task/eqcpp-zoneprocess-track-b-remaining`.

- **B139** (B) `AutomatedMode::setAutomationCallback`
  (`include/client/mode/automated_mode.h`) has ZERO callers in `src/`,
  `include/`, `tests/` or `benchmarks/` — the callback is invoked in
  `src/client/mode/automated_mode.cpp` but nothing ever registers one, so
  automated mode's per-tick hook is an unreached seam (the implemented-but-
  unreached pattern again). It is also exactly the seam B138's scenario lane
  would plug into, so the two should be ruled on together: wire it, or delete it
  with the mode's command queue. Closes when: it has a caller or it is gone.
  Prov: T372 (flagged incidentally by T305 at `9bb156ea`, still true at
  `3964d100`).

- ~~**B140** (B) `EntitiesComponent::SendClickDoor` and `SendClickObject`
  (`src/client/entities/entities_component_world.cpp`) end in
  `ctx_.zone_connection->QueuePacket(p, 0, true)` — a raw dereference of a
  `std::shared_ptr` with no null check, and the last two senders in the family
  that bypass `eqt::PacketSender::SafeQueueZone`. Two consequences, the same
  pair T360 fixed for `SendSpawnAppearance`/`SendAnimation` (B068): the R01
  session recorder and the T268 ordered-capture seam are both BLIND to every
  door and world-object click the client sends, so a `--record-session .sleq`
  is a false negative for them and the Batch R replay oracle can never assert
  on one; and a null `m_zone_connection` is a segfault rather than a logged
  refusal. MEASURED in T374: a harness that set all three `IsFullyZonedIn()`
  terms but installed no connection crashed here (exit 139) — which is why
  `tests/test_door_click_zone_reset.cpp` carries a real
  DaybreakConnection-over-FakeUdpTransport it otherwise would not need.
  `IsFullyZonedIn()` makes the null case unlikely in production, so this is B,
  not A. Closes when: both senders go through `ctx_.net.SafeQueueZone`, with
  the click visible in `Test_GetQueuedPackets()`. Prov: T374.~~ **CLOSED by
  T376 Leg A** (`89a908c9`): all THREE direct senders in
  `src/client/entities/entities_component_world.cpp` now call
  `ctx_.net.SafeQueueZone(p, 0, true)` -- `SendClickDoor`, `SendClickObject`
  and `SendCloseContainer`, the third one the board text did not name but
  which is the same shape in the same TU.
  `tests/test_click_senders_packet_sender.cpp` pins both halves: three no-connection cases (pre-fix the binary
  died with exit 139 on the first one, taking every other case with it) and
  two observer cases, the T268 ordered-capture seam and a real `.sleq` read
  back off disk, both of which read ZERO pre-fix.

- ~~**B141** (B) `EQT::DoorStateManager` (`include/client/door_state_manager.h`)
  carries a COMPLETE, correct, and entirely unreached duplicate of the pending-
  door-click set that T374 just fixed: `pendingClicks_` plus
  `addPendingClick`/`removePendingClick`/`isClickPending`/`clearPendingClicks`,
  with `setDoorState()` erasing the id and `clear()` clearing the set — i.e. it
  already had both halves of the B136 defect handled. All four accessors have
  ZERO callers tree-wide (measured at `672e3c3a`), because the live set is
  `EntitiesComponent::m_pending_door_clicks`. The duplicated-then-drifted
  pattern, and the reason B136 existed at all: the copy that was reached lacked
  the zone clear the copy that was not reached had. Closes when: one of the two
  is gone — either `m_pending_door_clicks` moves onto the DoorStateManager
  (which needs the `isClickPending` read to happen BEFORE `setDoorState`, since
  that method erases it) or the manager's four methods and `pendingClicks_` are
  deleted. Prov: T374.~~ **CLOSED by T376 Leg B** (`8962be28`): decided by
  ownership -- the manager owns door state, so the manager's set is the one
  copy. `SendClickDoor` calls `addPendingClick`, `ZoneProcessMoveDoor` reads
  `isClickPending` BEFORE `setDoorState` (which consumes the entry), and
  `ZoneComponent::CleanupZone`'s second call is gone because
  `DoorStateManager::clear()` already clears `doors_` and `pendingClicks_`
  together. `EntitiesComponent::m_pending_door_clicks`,
  `ClearPendingDoorClicks()` and the now-unused `<set>` include are deleted.
  T374's two `DoorClickZoneReset` cases are unedited and green; three new
  `DoorPendingClickOwnership` cases pin WHICH set through the manager's public
  accessors and were all red pre-fix.

- ~~**B145** (B) The T376 census of direct zone-stream senders: 70 sites in 13
  files still call the zone connection directly instead of
  `eqt::PacketSender::SafeQueueZone`, so the R01 session recorder and the T268
  ordered-capture seam do not see any of them. Ten are the B140 crash
  shape.~~ **CLOSED (T378, 2026-09-05.)** 69 sites at T378's tip -- T375 had
  already routed one of player_misc.cpp's on its way past, which is the whole
  difference from T376's 70 -- routed in eight per-family commits: chat
  13e60ab8 (2), group 835ca086 (6), movement f8205cba (1), player 4774190e
  (25), zone handshake 8fc1005b (12), merchant 2794291a (13), gm ee2ac431 (9),
  core f2e234c8 (1). The census is now ZERO outside PacketSender and the
  login/world TU (B146), and 6e4f9d9e pins it: tests/test_direct_sender_census.cpp
  re-runs the sweep on every ctest and names the file and line of any new
  direct sender (proven by re-planting one). tests/test_direct_senders_packet_sender.cpp
  carries a case pair per family, 17 cases; every no-connection case for a
  crash-shape family died with exit 139 pre-fix, and every recorded-session
  case counted 0. Only the bare `if (ctx_.zone_connection) { send; }` wrappers
  were deleted (two, in group); the eleven zone-handshake if/else wrappers
  STAY, because their else arm is the only writer of the
  replay-suppressed-send counter, which SafeQueueZone's own null guard does not
  bump. 9bad7fe4 armed the four harnesses that observe below SafeQueueZone
  (17 cases across BankCoinIntent, ReadBook, ScribeScroll and
  EqActionHandlerTransformsReal) with the `conn->m_stats.sent_packets = 1` line
  T346/T360/T376 already carry -- exactly the trap the entry predicted. Two
  corrections to this entry as written: the file count was 13, not 12 (it
  counted merchant_vendor_bank.cpp inside merchant.cpp), and everquest.cpp's
  one site is SendPetition, not SendZoneChange -- there is no SendZoneChange in
  that TU. Prov: T376, T378.

- ~~**B146** (B) The login and world streams have no `PacketSender` equivalent:
  `src/client/login_world/login_world_component.cpp` holds **12 direct sends**
  (4 on `m_login_connection`, 8 on `m_world_connection`), none of which the R01
  session recorder sees on the ClientToServer side of those streams, even
  though `EQT::Net::SessionStream` has `Login` and `World` members.~~
  **CLOSED (T379, `e9ee0220` + `f08369db`).** `eqt::PacketSender` gained the
  per-stream entry points the board's second option named — `QueueLogin()` and
  `QueueWorld()`, `SafeQueueZone`'s three-step body (T268 seam, R01 recorder
  with this stream's `SessionStream`, then the queue) with the zone-only
  machinery dropped: no stream/reliable arguments, no async hop, no zero-stats
  guard (it reads `isZoneConnected()`, false for the whole handshake by
  definition) and no catch-and-reset. Both are null-safe on two axes, unbound
  and bound-but-down, each logging and bumping the replay-suppression counter
  exactly as the twelve call sites' own else branch did. The connections are
  `LoginWorldComponent`'s and it is constructed in `EverQuest`'s BODY, so they
  are bound as pointers by `BindLoginWorldStreams` from the constructor rather
  than as references in the initialiser list. All 12 sends routed; zero
  `->QueuePacket(` left in the TU, pinned by a census case.
  `tests/test_login_world_senders_packet_sender.cpp`, 7 cases (3 red -> green
  on the two observers, plus the census pin). Prov: T376, T379.

- ~~**B125** (A) `include/client/audio/audio_mixer.h` `MixChannel::volume` —
  a plain `float` written by the GAME thread and read by the miniaudio AUDIO
  thread, with no synchronisation.~~ **FOUND AND CLOSED IN THE SAME UNIT
  (T370).** The B064 zone tour under ThreadSanitizer reported it **3 of 3 runs
  with byte-identical stacks** — the only race the tour found anywhere:
  write at `MusicPlayer::setVolume` (`music_player.cpp:213`) <-
  `AudioManager::playMusic` <- `ZoneAudioManager::update` <-
  `BridgeComponent::UpdateAudioListener` on the game thread, previous read at
  `AudioMixer::render` (`audio_mixer.cpp:50`) <- `miniaudioDataCallback` on the
  miniaudio worker thread. It fires on every zone change, because that is when
  the zone's music is (re)started. Fixed by making `MixChannel::volume` and its
  identical sibling `MixChannel::pan` `std::atomic<float>` — no lock, so
  `render()` stays lock-free and no lock-ordering edge is added; the struct was
  already non-copyable (`std::atomic<bool> active`) and every touch site is a
  plain assignment or an implicit load. **3 of 3 post-fix tours clean, exit 0.**
  `review/T370/run{1,2,3}/tsan_report.*` (pre-fix, verbatim; fixed in `373cea89`),
  `review/T370/runpostfix{1,2,3}/` (post-fix). Prov: T370 Review.

---

## Added 2026-09-05 by T377 (owed live proofs 5)

- **B170** (A) `src/client/input/hotkey_manager.cpp` `HotkeyManager::registerDefaultBindings`
  — **two of fable's three window keys do not match the official client, and one
  of them collides with an official binding.** MEASURED T377 against the oracle's
  own Options > Keys > **UI** page (`review/T377/oracle/T377_oracle_options_keys_UI_page1..4.png`),
  which B115 could not reach because `eqgame.exe` holds no default-scancode table
  on disk: the official defaults are **party (group) window `Alt P`**, **buff
  window `Alt B`**, **pet info window `Alt F1`**, every one with an EMPTY
  Alternate column. fable binds `ToggleGroup` to **`G`** with `Alt+P` only as the
  alternate, `TogglePetWindow` to **`P`**, and `ToggleBuffWindow` to `Alt+B`.
  So: buff is correct; group's primary `G` is an invention (`G` is bound to
  nothing in the UI or Commands category — `Alt G` is Guild Management); and
  pet's `P` is both wrong and **the official key for the Customer Service
  window**, so a player who learned the real client reaches for `Alt+F1` and gets
  nothing, then presses `P` expecting the petition window and gets a pet window.
  Closes when the three primaries are `Alt+P` / `Alt+F1` / `Alt+B`, `G` and `P`
  are dropped or re-justified, and a test pins the table against the measured
  row. **A second, cheaper half:** the oracle's group window with NO group paints
  **only its title bar** — a single strip reading `Group`, no member rows, no
  body, no reserved height — while fable draws the titled *frame* T368 gave it
  (`FrameComponent::groupPanelIsVisible`, `static_panels.h`). Prov: B115; T377 Review.

- ~~**B171** (A) `src/client/graphics/vision_ambient.cpp` /
  `IrrlichtRenderer::updateTimeOfDay` — **the vision floor's per-channel gains
  are wrong against the oracle, even where the scalar ratio is right.**~~
  **STRUCK PROVISIONALLY by T383 (branch `task/T383-vision-channels`,
  commit `776357f1`) — THE LIVE RE-MEASURE IS OWED to the next lock
  unit.** All six floor channels were re-solved against T377's oracle table by
  inverting T361's measured power law per channel
  (`floor_c = night_c * oracle_ratio_c ^ (1/1.1405)`): Ultravision gain
  **0.1601** x tint **(0.695, 0.698, 1.607)**, Infravision gain **0.1248** x
  tint **(0.823, 0.759, 1.419)**. Forward-checked, the predicted per-channel
  ratios land within **0.11%** of all six oracle numbers and the implied
  whole-frame greys within **0.33%** — channels and scalar together, so the
  plan's STOP did not fire. The zone-light arm's Infravision `redShift` was
  the second parameter and was solved from the same oracle row by least
  squares on the log of its three channel ratios: **0.3 -> 0.0508** (a lamp
  red-to-blue spread of 1.857 against the oracle's 1.0998; residuals
  -0.35% / +1.38% / -0.99%). Pinned by `tests/test_vision_ambient.cpp`
  (**6 of 18 fail pre-fix**) and `tests/test_vision_zone_lights.cpp`.
  **The point-light tint was NOT the blue-down cause and nothing in the tree
  is**: `applyVisionAmbientFloor` is a per-channel `std::max`, both zone-light
  arms raise every channel against Normal's flat 0.25 (Infravision blue is
  `0.75*(1-s)`, x2.85 UP), and the object lights never see `visionType` at all
  (vision-invariant — possibly its own defect). fable's measured **B x0.8209**
  is therefore not reproducible from the source, and no per-channel model fits
  the pre-fix fable row (a three-parameter additive fit puts blue's
  point-light coefficient at -0.00177, non-physical). **That is what the owed
  re-measure must settle.** Prov: T383 Review, `review/T383/README.md`.

  ORIGINAL TEXT: **the vision floor's per-channel gains
  are wrong against the oracle, even where the scalar ratio is right.** MEASURED
  T377 (`review/T377/` §2), both clients in one session, same spot, same crop,
  video-stepped on both sides. Ultravision: the oracle lifts **R x1.458 / G x1.463
  / B x1.851** — near-neutral with a blue lift, matching its own "eyes glow
  violet" flavour — while fable lifts **R x1.461 / G x1.281 / B x1.663**, so
  fable's green lags its red by 12.5 points and the buff reads as a magenta cast.
  Infravision: the oracle raises **all three** (R x1.328 / G x1.211 / B x1.208)
  while fable **drives blue DOWN to x0.821** at R x1.600 — the only channel in
  either table that moves the wrong way. T361 solved the two SCALAR gains by A/B
  against the oracle and they are close (fable 1.449 / 1.237 vs the oracle's
  1.560 / 1.252); the per-channel shape was never fitted. Closes when the three
  channel gains are solved against the oracle table the same way the scalar was,
  and `tests/test_vision_ambient.cpp` pins them. Prov: T377 Review; B067.

- **B172** (A) `src/client/commands/combat_actions.cpp`
  `eqt::commands::CommandsComponent::ZoneProcessMoneyOnCorpse` — **corpse coin is
  parsed, logged and DROPPED: no sound, and no credit to the player's purse.**
  The body ends `// For now, just log it - money is auto-looted when we loot any
  item`, and the log line itself is behind `GetDebugLevel() >= 1`. MEASURED T377
  (`review/T377/` §3): the official client plays **`BuyItem.WAV` (138)** and
  prints `You receive 1 platinum, 8 gold, 3 silver and 4 copper from the corpse.`
  the instant the loot window opens — corpse coin is granted on OPENING the
  window, not by a separate click, so `ZoneProcessMoneyOnCorpse` is exactly the
  right publisher and it is the one T328 Leg B did not wire. (T328 wired
  `BUY_ITEM` at `InventoryManager::pickupMoney()`, which is a different gesture
  and still has NO production caller — see B036 clause 2.) Closes when the
  handler pushes `PlaySoundIntent{BUY_ITEM}`, banks the four denominations, and a
  test pins both. Prov: T377 Review; B036.

## Added 2026-09-05 by T383 (vision colour channels)

- **B180** (B) `tests/test_direct_sender_census.cpp`
  `DirectSenderCensus.LoginAndWorldStreamsAreTheOnlyExemptionAndStillTwelve` —
  **a RED test on the `fable` tip, and it is the census that is stale, not the
  code.** T379 (`f08369db`) closed B146 by routing all 12 login/world direct
  sends through `PacketSender`; T378's census (`6e4f9d9e`) still asserts
  `kLoginWorldExpected == 12` and now measures **0**. The test's own failure
  message says exactly what to do: *"If B146 was closed, this count goes to 0
  and kLoginWorldExpected comes with it"*. It fails standalone (22 s), not
  only under `-j6`, so it is not a load artefact. Closes when the exemption
  and its constant are deleted and the case asserts that login/world have no
  direct senders left. Found by T383, whose diff touches no network file.
  Prov: T383 Review, `review/T383/README.md`.
