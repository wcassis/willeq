# BOARD — the open-work queue of record

- **Tree confirmed at:** `3808ebe9` (branch `task/K00-legA-board`, base `fable`).
- **Date:** 2026-09-03. Built by K00 Leg A (`refactor/K00_truth_reset.md`).
- **Suite:** DO NOT QUOTE A NUMBER FROM THIS LINE. The suite grows with every
  unit -- it went 3,760 -> 3,949 across T352-T370 alone -- so a count written
  here is stale within a day. Get the current one with
  `ctest --test-dir <slot>/build -N | tail -1`, and the current pass/fail with
  `ctest --test-dir <slot>/build -j6`. **T392 (2026-09-05, B215) RETIRED the
  last expected failure: the suite is now ALL GREEN and any red is a real red.**
  The `AudioManagerTest.PreloadCommonSounds` "ALSA quirk" waiver was a
  misdiagnosis -- the binary never opened `willeq.db`. Last measured:
  **4,056 of 4,056** at `1311b50a` (T392, slot2 -- 4,055 under `ctest -j6` plus
  `visual_gate`, which misses its gfaydark CPU-p95 ceiling under that load and
  passes standalone). `visual_gate` also needs `build-gles2` rebuilt from the
  same tip, or it fails with an identical `characterModelsBytes` deficit in all
  three zones -- that shape is a stale tree, never a defect.
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

- ~~**B022** (C) `src/client/graphics/spell_visual_fx.cpp` --
  **THE PADDING RULING IS ANSWERED BY THE ORACLE (T389, 2026-09-05):
  `EmptyRecord` and `NoAffectIndex` DO NOT DIFFER -- both draw the same
  generic bright-blue dome of point sprites, for the same 2.8 s.** Measured
  back to back in one session, 20 s apart, same spot and camera:
  **4243 Intoxication I** (affect index 70, EmptyRecord, self-target,
  effect 10 = null so it lands harmlessly) peaked at RGB .378/.383/.523 and
  **6594 GM Cleansing** (affect index -1, NoAffectIndex) at .381/.386/.624;
  **1494 Flame Jet** (index 71, the other byte-identical template) drew the
  same generic blue in a third run. So the three
  `!visual.isFallback() && !visual.hasXStage()` gates in
  `spell_visual_fx.cpp` are CORRECT AS WRITTEN for both fallback classes --
  `isFallback()` is true for each, so neither is ever suppressed -- and
  T380 Leg B3's STOP ("stop if the oracle's answer differs between 1494 and
  6594") did **not** fire. Evidence:
  `review/T389/oracle/T389_oracle_6594_full_1024x768.png`,
  `review/T389/oracle/T389_oracle_1494_full_1024x768.png`;
  `refactor/T389_spell_fx_oracle.md` Review S4. What is left of B022 is only
  the sprite-name half, which is **B162**. Original text: the data-driven spell
  visual layer is still entirely ahead: nothing consumes the `spells.eff` reader
  in place of the hardcoded resist/school vocabulary, and the spells whose
  `spell_affect_index` hits an empty padding record need a rendering ruling.
  Prov: archived board, T70.
  **T380 (2026-09-05) surveyed this and put numbers on it — the ruling is still
  open, but it is now correctly scoped as LOW priority, and a bigger defect was
  found underneath it (B160, since CLOSED by T381 — the resist/school
  vocabulary and `SpellVisualSource::Resolved` are reachable now, so B022 is
  once again only about the sprite-name/padding ruling).** Census script:
  `scripts/refactor/t380_spell_visual_census.py`; full tables in
  `refactor/T380_spell_visuals_survey.md` Review.
  - The reader DOES have a production caller — `SpellVisualFX::loadSpellsEff()`
    in the constructor built by
    `src/client/graphics/renderer/renderer_entities.cpp` — but its result is
    consumed only by `resolveVisual()`, which per **B160** could never see a
    non-`-1` index. Loaded, correct, and inert. **T381 fixed that**: the index
    arrives on the `SpellCastVisual*` payload and the reader is live. What is
    still inert is what the resolution is USED for — see B165.
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
~~ **CLOSED 2026-09-06: T389 measured EmptyRecord == NoAffectIndex (the generic dome) on the official client; the gates are correct as written; all 163 hits are NPC-only.**
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

- ~~**B160** (A)~~ **CLOSED 2026-09-05 by T381 (`fb98dc7c`).** The three facts
  the renderer needs — resist type, casting school, `spell_affect_index` — now
  cross the bridge BY VALUE on `SpellCastVisualStartedData` /
  `SpellCastVisualCompleteData` (`EQ::SpellVisualVocabulary`,
  `include/client/spell/spell_visual_vocabulary.h`, +8 bytes per payload,
  `EventData` unchanged at 280 bytes because a larger alternative dominates
  the variant). `SpellManager::handleBeginCast` / `handleAction` / the
  cast-timeout path in `update()` fill it from the game thread's own
  `SpellDatabase`; `src/client/bridge/irrlicht_bridge.cpp` calls the new
  `SpellVisualFX::noteSpellVocabulary()` before `createCastGlow()` /
  `removeCastGlow()`. `setSpellDatabase()`, `m_spell_db`, the
  `getSpellColor(const SpellData*)` overload and the four `if (m_spell_db)`
  null branches are DELETED, and with them `spell_database.h` disappears from
  `src/client/graphics/`. The six resist colours, the five school colours,
  `isSprayCastStyle()`'s spray path and `SpellVisualSource::Resolved` are all
  reachable. Pinned by `tests/test_spell_visual_vocabulary.cpp` (6 tests, 4 red
  pre-fix). **The pointer had ONE caller after all** — `tools/model_viewer.cpp`
  (T380's census grepped `src/`, `include/`, `tests/` only); it now plays the
  game thread's part with `noteSpellVocabulary()`. See B165, B166. ~~**A live
  two-client pair showing a fire cast and a frost cast side by side is OWED**
  — the eqemu lock was held by T377 during T381.~~ **TAKEN T401
  (`4ff78bb4`, `review/T401/` §3): the two colours are DIFFERENT on the
  observing client's screen and they are the right two.** Testcaster gem-cast
  (not `#castspell`) Burst of Flame 93 and Frost Bolt 54 on itself with
  Testbuddy watching 15 units away; in a 180x160 crop differenced against a
  frame 2.5 s earlier the changed pixels read **(255, 155, 63) — orange —
  for 93** and **(64, 255, 241) — cyan — for 54**, 442 and 438 pixels.
  Frames, 4x crops and the 920-frame video are in `review/T401/fable/`. Original text:
  `include/client/graphics/spell_visual_fx.h` —
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
  methods with **zero callers in `src/`/`include/`** (T381: `tools/` was never
  grepped for these either — re-run the census including it before deleting
  anything, see B166; `model_viewer.cpp` calls `createProjectile`,
  `createImpact`, `createImpactAtPosition`, `createRainEffect`,
  `createGroundCircle` and `createBuffAura` from its spell-test mode, so this
  is a "no CLIENT caller" item, not a no-caller item): `createProjectile`,
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
  **T388 re-took the census under the B166 rule, naming the directories:
  `src/`, `include/`, `tests/`, `tools/`, `benchmarks/` and `docker/`** (the
  last two hold no C++ caller of any of these). It splits the seven in two,
  and it does NOT match the shape T388's own plan assumed:
  - **FIVE have exactly one caller, `tools/model_viewer/model_viewer_spell_test.cpp`**
    (T385 split `model_viewer.cpp`; the spell-test wiring moved there):
    `createProjectile` (two call sites, plus
    `tests/test_spell_visual_vocabulary.cpp` drives it for T381's travel-gate
    case), `createImpactAtPosition`, `createBuffAura`, `createRainEffect`,
    `createGroundCircle`. **Kept.** The tool is the manual FX harness -- the
    one place a developer exercises this layer by hand, and the reason B160
    survived T70/T72 -- so deleting the method deletes the harness's feature.
    The class header now says so at the Effect Creation block.
    **`createProjectile` is NOT callerless**, which is what T388's plan
    asserted; B161's own text was right and the plan's paraphrase was not.
  - **TWO have ZERO callers in any of the six directories**: `removeBuffAura`
    and `removeAllForEntity`. Not even `model_viewer` calls them -- it creates
    auras and never removes them, and nothing calls `removeAllForEntity` on
    despawn. Boarded separately as **B195**; nothing was deleted here, per the
    plan's "strike nothing".
  So the decision B161 is waiting on is narrower than "wire or delete the
  seven": it is the PROJECTILE question, and that is **B165**, whose answer
  waits on **B162**'s oracle capture. T384 has already made the data
  available (`SpellVisualVocabulary::travelType`, 85 spells with
  `travel_type == 3`); nothing consumes it. Census by T388.

- ~~**B162** (B) `src/client/spell/spells_eff_reader.cpp` --
  **THE HYPOTHESIS IS SETTLED BY THE ORACLE (T389, 2026-09-05): the digit is
  0-BASED. `GEN<L><d>0_SPRITE` -> `spel<l><d+1>.tga`, and the `'0'` exclusion
  is wrong too.** Two spells.eff records whose every non-empty sprite slot in
  every stage holds ONE name, for a letter with at least two files on disk,
  settle it -- the drawn particle can only have come from that name:
  record **20** (every slot `GENN00_SPRITE`, cast as spell **41 Weaken**) drew
  `speln1.tga`'s green/orange mottled spheres, where the shipped rule says a
  `'0'` digit resolves to nothing; record **35** (every slot `GENA10_SPRITE`,
  cast as spell **253 Whirling Wind**) drew `spela2.tga`'s bright blue
  EIGHT-pointed stars, **not** `spela1.tga`'s blue blob cloud, which is what
  the verbatim reading gives. Corroborated by `spelo1.tga` (285 Pendril's
  Animation, record 11) and `spelu1.tga` (54 Frost Bolt's `GENU00` Source
  stage). Evidence: `review/T389/oracle/T389_oracle_41_weaken_full_1024x768.png`,
  `review/T389/oracle/T389_oracle_253_GENA10_crop3.5x.png`,
  `review/T389/sprites/montage_discriminators.png`;
  `refactor/T389_spell_fx_oracle.md` Review S3. **A trap the measurement had to
  clear: the purple ribbon swirl at a caster's hands is the client's own
  casting animation, not a record sprite** -- it appears on any caster,
  including an NPC casting in frame, and it briefly looked like evidence for
  the verbatim reading. **What is still open, and why this stays (C):**
  `effectNameToTexture()` was deliberately NOT changed by T389, because
  correcting it moves 157 -> 272 of 301 sprite slots across 8,066 spells at
  once and nothing downstream can draw a `spel*.tga` anyway (see **B225**).
  That correction plus its regression net is the remaining work here.
  T389 also did the smallest end-to-end slice on top of the measurement
  (T380 Leg B2): `GEND10_SPRITE` -> `ParticleAtlas::SpellSparkle` and
  `GENU10_SPRITE` -> `ParticleAtlas::IceCrystal` in
  `SpellEffectsConfig::buildTextureNameMap()`, routed into
  `EmitterConfig::textureRegions[]` by the new
  `SpellPresets::applyStageSpriteNames()` from
  `SpellVisualFX::createImpact()` / `createImpactAtPosition()`, pinned by
  `tests/test_spell_target_stage_textures.cpp` (8 tests, 6 red pre-fix).
  `ParticleAtlas` was NOT extended and no `spel*.tga` is loaded. Original text:
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
  NAME to a tile index and falls back to `SoftCircle`. Found by T380.~~ **STRUCK T409 Leg B (this commit)**: `effectNameToTexture()` now
  implements the measured rule -- `GEN<L><d>0_SPRITE` -> `spel<l><d+1>.tga`,
  digit range `'0'..'8'` ('9' would construct a two-character index no file
  uses), and the `'0'` exclusion is gone. **Census taken over the real
  `spells.eff` and the real `SpellEffects/`, not quoted** (new
  `SpellsEffReaderFileTest.TheResolvedSlotCensusIsTheZeroBasedOne`, which
  prints the table): 40 distinct names / 301 non-empty slots either way;
  **18 names -> 36, 166 slots -> 286 resolved, and 157 -> 272 landing on a
  file that exists**. The 4 that never resolve are the non-GEN names
  (`GEN_Z_SPB`, `I_SNOWFLAKESPRITE`, `I_TFIRE_SPB`, `I_TSMOKE_SPB`). The
  counterexamples MOVED: T70's pair was `GENA30`/`GENY10`, and under the
  correct rule `GENY10` resolves to the real `spely2.tga` while `GENA20` and
  `GENA30` construct the absent `spela3`/`spela4`. Second half done too:
  the name -> tile map gained **ten rows** by T389's tile-vs-crop comparison
  (`GENA00`/`GENA10` -> SpellSparkle, `GENB00` -> StarShape, `GENC00` ->
  Ember, `GENE00`/`GENE01` -> LeafShape, `GENJ00`/`GENU00` -> SmokeWisp,
  `GENN00` -> SnowPatch, `GENZ00` -> WispyCloud), two of them oracle-measured
  (`spela2` record 35, `speln1` record 20). Crops and the tile sheet:
  `review/T409/sprites/`. **A trap the crops exposed: every `spel*.tga` is a
  4x4 SHEET of one identical frame, so any descriptor taken over the whole
  file is meaningless** -- T389's own crops are whole sheets. 14 tests red
  pre-fix (6 in `test_spells_eff_reader`, 8 in
  `test_spell_target_stage_textures`). What is NOT mapped, and why, is
  **B300**. Prov: T409 Review Leg B; T389 Review S3.

- ~~**B163** (B)~~ **CLOSED 2026-09-05 by T384 (`57258eea`).**
  `tools/willeq_db_builder.cpp` `loadSpells()` now binds `TargetAnim` (121)
  and `TravelType` (122) beside the `CastAnim` (120) it already bound, and the
  `spells` table carries `target_anim` and `travel_type` as its last two
  columns (99 -> 101). The indices were verified against eqemu's own positional
  import: `Columns()` in
  `/mnt/projects/akk-stack/code/common/repositories/base/base_spells_new_repository.h`
  has `CastingAnim` at 120, `TargetAnim` at 121 and `TravelType` at 122, so the
  STOP did not fire. `kWilleqDbExpectedVersion` 6 -> 7; all three build trees
  rebuilt. The rebuilt db reproduces T380's census exactly -- `target_anim`
  non-zero for **3,608 of 8,066** rows, `travel_type == 3` for exactly **85**,
  8,066 rows and 13 tables both sides, every shared column byte-identical
  except `metadata` (`version`, `build_date`). `SpellData` gains `target_anim`
  (declared since forever, never assigned) and `travel_type`, and
  `EQ::SpellVisualVocabulary` gains `travelType` -- the slot T381 deliberately
  left empty -- packed into the padding byte after `school`, so the struct is
  still **8 bytes** and neither bridge payload grew. Pinned by
  `SpellDatabaseTest.SpellVisualColumnsRoundTripForMeasuredSpells` /
  `...AreZeroForANonProjectileSpell` and
  `SpellVisualVocabularyTest.VocabularyCarriesTravelType`. **This unit only
  makes the data AVAILABLE -- nothing consumes `travel_type` yet; the
  projectile question is still B165, whose decision waits on B162's oracle.**

- **B165** (B) **ANNOTATED by T389 (2026-09-05): the oracle says both impact
  gates and the cast-glow gate are correct as written, so only the travel gate
  is still a question.** T389 measured that `EmptyRecord` and `NoAffectIndex`
  draw the SAME generic effect on the official client (see B022), and both are
  `isFallback()`, so the gates never suppress either -- which is the behaviour
  the shipped code already has. The travel gate's 6,250-spell population is
  still unexercised because `createProjectile()` still has no client caller,
  and **T389 could not measure whether the official client draws a travelling
  bolt at all** -- four attempts failed for reasons recorded in **B227**. The
  projectile decision therefore still waits, but it now waits on B227's
  capture, not on B162, which is settled. Original text: The three T72 stage-suppression gates
  (`!visual.isFallback() && !visual.hasXStage()` in
  `SpellVisualFX::createCastGlow` / `createProjectile` / `createImpact` /
  `createImpactAtPosition`, `src/client/graphics/spell_visual_fx.cpp`) became
  *reachable* at T381, but measured against the shipped data only ONE of them
  has a population. Of the 7,105 spells whose `spell_affect_index` resolves to
  a populated `spells.eff` record, **every** referenced record carries a Source
  stage and a Target stage — so the cast-glow gate and both impact gates can
  still never fire — while **6,250** reference a record with no Middle stage,
  which is the travel gate's population. Two consequences: (a) the only gate
  with anything to suppress guards `createProjectile()`, which is one of B161's
  seven zero-caller methods, so it fires only in a test today; (b) all four
  gates sit INSIDE the `if (m_particle_manager)` branch, and T124 recorded that
  `m_particle_manager` is null in production because `setParticleManager()`
  runs before `particleManager_` is constructed — re-measure that before
  scoping anything on the gates. Counts derived in the T381 Review from
  `spells.eff` + `data/willeq.db`; reproduce with
  `scripts/refactor/t380_spell_visual_census.py`. Found by T381.

- **B195** (B) `include/client/graphics/spell_visual_fx.h`
  `SpellVisualFX::removeBuffAura(entity_id, spell_id)` and
  `SpellVisualFX::removeAllForEntity(entity_id)` have **ZERO callers** in
  `src/`, `include/`, `tests/`, `tools/`, `benchmarks/` or `docker/` (T388
  census, the B166 rule). They are the removal half of B161's creation half,
  and unlike the other five they are not even reached by the
  `tools/model_viewer` FX harness: the harness calls `createBuffAura` and
  never removes the aura it made. Their bodies are in
  `src/client/graphics/spell_visual_fx_nodes.cpp`. **Do not delete them
  independently of B161/B165** -- if the projectile/aura path is wired, an
  aura that is created and never removed is a leak and these are the fix; if
  it is deleted, these go with it. Sequenced behind B165. Found by T388.

- **B166** (P) A reachability census that greps only `src/`, `include/` and
  `tests/` is not "tree-wide". T380 boarded B160 as "`setSpellDatabase()` has <!-- RETIRED-OK: B166 quotes the claim that was wrong; T381 deleted the method -->
  ZERO callers tree-wide"; T381 found the call in `tools/model_viewer.cpp`
  (line 2750 of the 3,246-line TU as it then stood; T385 decomposed that file,
  and the spell-test wiring now lives in
  `tools/model_viewer/model_viewer_spell_test.cpp`), i.e. the ONE place a
  developer actually exercises the spell FX layer by hand, which is exactly why
  the defect survived — the tool looked right and the game did not. `tools/` holds
  `model_viewer`, `willeq_db_builder`, the zone/atlas builders and the audit
  binaries, all of which call production APIs. Every future zero-caller claim
  must state the directories it grepped and include `tools/` (and `docker/`,
  `benchmarks/` where relevant) or say why not. Found by T381.

- ~~**B142** (C) `/bug <description>` (registered in
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

  **WILL RULED 2026-09-06:** leave /bug on the report channel. CLOSED as ruled.
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
  immediately; A's once B136/B137 are worked -- **B138 was ruled on and CLOSED
  by T410**, which rebuilt the scenario lane against `fable` rather than
  porting it, so nothing on either branch is needed for it); a merge was
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
  **T392 NOTE (2026-09-05, `1311b50a`):** every sound this entry wired --
  145, 146, 138 -- was INAUDIBLE on fable until T392, and for a reason that
  had nothing to do with the wiring: `willeq.db` returned each filename with
  a trailing carriage return, so the PFS lookup missed silently (B200,
  struck). The tests were green throughout because they assert on the
  `PlaySoundIntent`, which is the correct seam and did fire. ~~**A live ear on
  id 138 is OWED before this entry can rest on "fable plays the same
  sounds".**~~ **TAKEN T401 (`4ff78bb4`, `review/T401/` §1): id 138 is HEARD.**
  The session logs **21** `Loaded sound from PFS` and **zero** `not found`,
  and the recording off an ALSA loopback matches the real `buyitem.wav` bytes
  (extracted from `snd2.pfs`) at **ncc 0.83** at 232.065 s -- the instant of
  the `Sound play: id=138` line. `LevDn.WAV` 140 (ncc 0.996) and `SpelCast.WAV`
  108 on a gem cast (0.72-0.77, three trials) are heard too. **But two new
  defects came with it: every one of them plays at exactly DOUBLE rate
  (B250), and `SwordBlk.WAV` 120 is played 30 times and is INAUDIBLE because
  it is emitted at the player's stale spawn position (B251).** ~~both~~ **BOTH
  FIXED by T404** (`4452b378` Leg A, `853bdf2b` Leg B) -- the mixer never read
  `MixChannel::sampleRate` and played every asset at the device's 22,050, and
  the self entity row was a projection nothing refreshed. **Neither is
  live-re-checked yet; T404 took no server.** This clause's
  live proof is discharged for the coin/container/cast rows; clause 3's
  `DEATH` id (140 = LevDn) is now also proven audible and still wrong-named.

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
  comment `// For now, just log it`. That gap is **B172**, **CLOSED T382 Leg A (`0d7751fe`)**: the handler now
banks the four denominations through `PlayerState::addCurrency`, publishes
`CurrencyChanged`, pushes `PlaySoundIntent{BUY_ITEM}` (138) and prints eqstr
12072 `You receive %1 from the corpse.` with the counts -- so **THIS CLAUSE'S
LAST ROW IS WIRED AND TESTED, and clause 1 is CLOSED**: every measured coin,
container, buy, sell and combine row is now either wired or pinned silent. What
is still UNMEASURED is narrower than a row: what the official client prints
when ONE denomination is zero (T377's single capture had all four non-zero).
fable prints all four; a corpse with, say, no silver would settle it in one
line of a future live session. The LIVE proof of the corpse row itself (hear
138, watch the purse move on fable) is OWED. The recipe that worked,
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
  unreachable until that UI exists. **RE-CONFIRMED T382 Leg A (`0d7751fe`)**:
  still zero production callers, and deliberately left alone — the corpse grant
  is a different gesture with a different publisher
  (`CommandsComponent::ZoneProcessMoneyOnCorpse`), so wiring one did not reach
  the other. (3) SIX `SoundId` names that do NOT describe
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

  **WILL RULED 2026-09-06:** add a FOURTH vision level for Froglok; floor solved by live A/B. → the vision follow-up after T405.
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
  fable's **drives blue DOWN to x0.821** at R x1.600. That is **B171** — **STRUCK PROVISIONALLY by T383 (`776357f1`), live re-measure OWED; T383 also established that NO code path in the tree can lower a channel under a vision buff, so the x0.821 is unexplained by the source and the re-measure has to account for it.** ~~See B110 for the term that does separate the two clients.~~ **T408 (2026-09-06) MEASURED B110 and it does NOT separate them: loading the baked colours moves fable away from the oracle on the lamp invariant, and the term that separates the two clients is the UNIFORM x2.2 night deficit, i.e. B280. So B067's ~7% Ultravision residual has no explanation in B110 either and stays unexplained.** Pinned by
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
  **WILL RULED 2026-09-06:** APPROVED as its own unit (T408); the re-bless of the moved pixel goldens is approved for that unit, contact sheet to Will.
  **ANSWERED by T408 (`32cf1fb2`, branch `task/T408-baked-vertex-lighting`), and the
  answer is NO on the second half.** The colours ARE loaded now -- `parseFragment36`
  reads the array into `ZoneGeometry::vertexColors`, `buildTexturedMesh` writes it into
  the vertex and flags the material, and all six zone vertex programs carry the
  composition -- and `tests/test_baked_vertex_lighting.cpp` re-takes the census through
  the PRODUCTION loader on every ctest run. **Two corrections to the entry above.**
  (1) **The census numbers here were 2x.** Through the production loader qeynos2 has
  **1,915** region meshes and **36,218** coloured vertices, not 3,830 and 72,436;
  T367's temporary instrumentation double-counted every mesh (1,915 is the number
  `wld_loader.h`'s own `ResolvedMaterialList` comment has carried since batch J). The
  distribution is otherwise as described: min 0, max 255, mean RGB byte 44.44/255.
  (2) **The premise is REFUTED: the baked colours are not what separates the two
  clients at these spots.** Measured live at two spots x two hours x two clients with
  weather and heading pinned (`review/T408/`), on a GEOMETRY-ONLY crop -- T361/T405's
  700x420+40+40 world crop is about half SKY at both spots and no zone-lighting term
  touches the sky. On the invariant `L = (day/night)_lampfree / (day/night)_lamp` (how
  much more a lamp is worth at night than by day, each spot's composition divided out,
  immune to a uniform night-ambient error): **oracle 2.281** (two sessions agreeing to
  0.5%), **fable with the baked light OFF 2.476 (+8.5%)**, and EVERY baked composition
  moves fable AWAY -- additive gain 0.25/0.50/1.00 give 2.826/3.096/3.622, max
  0.50/1.00 give 3.461/4.683, and multiplicative (the tree's own equation with the
  colours loaded, the one this entry predicted would darken the world) gives 0.655.
  **L is monotone in gain above the oracle's value, so no gain improves the fit and
  the plan's STOP fired.** The reason is the residual's SHAPE: to reproduce the
  oracle's own day/night ratio fable's night needs **x2.27** at the lamp-free spot and
  **x2.09** at the lamp spot -- the SAME factor, i.e. a UNIFORM deficit, which is
  **B280**, not this. A baked term is lamp-shaped by construction (x1.03 in the open,
  x2.48 next to a lamp at gain 1.0) and cannot supply it. The baked-alpha sky mask was
  measured too and is null (+0.1% / -0.2% by day).
  **So the committed composition is `kBakedLightOff` and NO PIXEL GOLDEN MOVED** --
  mode 0 is byte-for-byte the pre-T408 equation, pinned by
  `BakedLightCompositionTest.EveryZoneVertexShaderCarriesTheBakedUniformsAndIsNeutralWhenOff`
  -- so Will's pre-approved pixel re-bless was NOT used. Three memory baselines did
  move and were blessed under the same approval (`characterModelsBytes` +6,144/+6,288/
  +6,216 = 24 bytes per character geometry, the new vector member; character WLD meshes
  carry no colour array at all). **What is left open is `B325`: re-run T408's
  `/bakedlight` probe after B280 is solved, because moving the night ambient moves
  every row of that table.** This entry is now class B, not C: the design decision Will
  was needed for has been taken and measured.
## Added 2026-09-05 by T368

- ~~**B115** (B) The official client's DEFAULT KEYS for the group, pet and buff
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
  and the empty-window shape are **B170**. Prov: T368 Leg A; T377 Review.~~ **STRUCK T382 (`0d7751fe`, `7f6128f8`)**: the screenshot B115 asked for exists -- `review/T377/oracle/T377_oracle_options_keys_UI_page1..4.png` -- and both halves of the entry were acted on by T382 Leg B. The three primaries are now `Alt+P` / `Alt+F1` / `Alt+B` with no alternates, `G` and `P` are bound to nothing, and the empty group window paints only its title bar. See B170.

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

- ~~**B138** (C) `fable` has no live-server scenario lane and no exit-code
  contract at all~~ **CLOSED T410** (`task/T410-scenario-lane`). Built NEW per
  Will's 2026-09-06 ruling, not ported. `src/client/scenario/` holds an
  `IScenario` state machine over a narrow `IScenarioWorld`, a registry, and a
  `ScenarioRunner` driven from `AutomatedMode`'s per-tick automation callback
  (B139's seam); `--scenario <name>`, `--timeout <s>`, `--scenario-hold <s>`
  and `--scenario-spell <id>` on `Application`; `main.cpp` returns the
  taxonomy **0 pass / 1 fail / 2 timeout / 3 could-not-start**, with the
  reason on stderr (`SCENARIO <name> <verdict> exit=N reason=...`) and in the
  log under the new `MOD_SCENARIO` module, which the lane pins to INFO so no
  `--log-level` can silence a verdict. The external-quit guard is that an
  UNFINISHED run reports 2, never 0. Three scenarios (`zone_in`,
  `two_client_survive`, `cast_lands`) with 20 headless state-machine cases in
  `tests/test_scenario_state_machines.cpp`, and `scripts/scenario.sh` doing
  lock -> `up.sh` -> snapshot -> run -> restore-with-the-world-down ->
  `down.sh` -> release. **The three live PASS runs are OWED (B305)**: the
  eqemu lock was held by T405 for the whole unit. Prov: T372, T305.

- ~~**B139** (B) `AutomatedMode::setAutomationCallback` has ZERO callers~~
  **CLOSED T410** (`task/T410-scenario-lane`). It has one now:
  `Application::initScenario()`
  (`src/client/scenario/scenario_application_wiring.cpp`) registers the
  `ScenarioRunner` on it, so the hook fires once per ~60 Hz game tick from
  `Application::processInput()`. The callback's existing "return false to
  stop" contract is what ends a scenario run -- it sets `m_quitRequested`, the
  game thread's `isQuitRequested()` check unwinds normally, and nothing calls
  `exit()` from inside a tick. Nothing about the mode had to be restructured
  (the plan's STOP condition did not fire). Prov: T372, T305.

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

- ~~**B170** (A) `src/client/input/hotkey_manager.cpp` `HotkeyManager::registerDefaultBindings`
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
  (`FrameComponent::groupPanelIsVisible`, `static_panels.h`). Prov: B115; T377 Review.~~ **STRUCK T382 Leg B (`7f6128f8`)**: `registerDefaultBindings` binds group to `Alt+P` alone (the `G` primary deleted), pet to `Alt+F1` (the `P` binding deleted), buff unchanged -- one binding each, matching the oracle's empty Alternate column. `config/hotkeys.json`, `README.md` and the two CLAUDE.md key tables follow, as do the stale `G`/`P` mentions in `frame_component.h`, `static_panels.{h,cpp}`, `irrlicht_bridge.cpp` and `test_frame_component.cpp`. The `HotkeyAction` enum is untouched, so `DocCounts` nets the same count. The second half too: `renderGroupPanel` paints the new free function `groupPanelFrameRect()`, which collapses the frame to the title band the layout already reserves (`BAR_HEIGHT + MARGIN`) when there is no group, sharing the full panel's origin and width. 13 tests (7 in `test_hotkey_manager.cpp`, 6 in `test_static_ui_panels.cpp`); 6 of them fail pre-fix.

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
  re-measure must settle.** **RE-MEASURED T386 (`review/T386/` §4): the re-solve
  is REFUTED and BOTH scalars REGRESSED (Ultravision 1.4483 -> 1.1686 against the
  oracle's 1.5612, Infravision 1.2450 -> 1.0757 against 1.2555), and the
  B x0.8209 anomaly did NOT reproduce (blue is now x1.1274, moving up). This
  strike is REVERSED -- see B188.** Prov: T383 Review, `review/T383/README.md`.

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

- ~~**B172** (A) `src/client/commands/combat_actions.cpp`
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
  test pins both. Prov: T377 Review; B036.~~ **STRUCK T382 Leg A (`0d7751fe`)**: `ZoneProcessMoneyOnCorpse` banks the four denominations through `PlayerState::addCurrency` (a thin delta wrapper over `setCurrency`, still the tree's ONE purse writer and the function `OP_MoneyUpdate` credits through), publishes the same `CurrencyChanged` projection `OP_MoneyUpdate` publishes, pushes `PlaySoundIntent{BUY_ITEM}` and prints eqstr 12072 with the counts. Two judgement calls, both recorded in the code: the list is spelled WITHOUT the Oxford comma eqstr 13108 carries, because that is what the oracle's pixels show (13108 is deliberately not used); and "all four denominations zero" is the guard that keeps the sound and the line off every non-grant shape -- eqemu's six `SendLootReqErrorPacket` responses, a corpse with no money, and the autosplit path all carry four zeros. A zero in ONE denomination is still printed; the official client's zero form is UNMEASURED (T377's single capture had all four non-zero) and the test says so. 11 tests (`tests/test_corpse_coin.cpp` + 3 in `tests/test_ui_sound_events.cpp`); 7 of them fail pre-fix. **The LIVE proof -- loot a corpse on fable, hear 138, watch the purse move -- was TAKEN by T386 and SPLIT: the banking and the chat line are PROVEN (two trials, `review/T386/` §1a); the SOUND is NOT (the asset will not load -- B200); and the purse cannot be watched at all because nothing draws it (B201). **BOTH HALVES CLOSED T401 (`4ff78bb4`, `review/T401/` §1 and §2): the sound is HEARD (`buyitem.wav` at ncc 0.83, on the `Sound play: id=138` line) and the purse IS watched -- `character_currency` 4/6/1/8 -> 9/13/2/16 with the four counts on screen in the inventory popup, beside the official client's own window at the same purse.** The official client's ZERO-denomination form is still UNMEASURED -- `review/T386/` §1d has the method and the trap.**

## Added 2026-09-05 by the orchestrator (T381 merge)

- ~~**B167** (P) `scripts/refactor/check_file_sizes.sh --all` scans `src/`,
  `include/` and `tests/` only, so M00's "1,500 ceiling enforced tree-wide" is
  FALSE for `tools/`.~~ **MOSTLY CLOSED by T385** (Leg A `4c28993c`, Leg B and
  the three follow-on splits). `--all` now scans `src/`, `include/`, `tests/`,
  `tools/` and `benchmarks/` (971 files, up from 866), and the census it finds
  is **six** files, not the five this entry counted by hand — the sixth is
  `tools/hwcaps/gles2_fog_volume_benchmark.cpp` at 1,711. T385 decomposed four:
  `tools/model_viewer.cpp` 3,246 -> 155 (eight TUs under `tools/model_viewer/`,
  behind `model_viewer_state.h`), `tools/model_viewer_spell_bar.h` 1,965 -> 14
  (three headers), `tools/entity_composite_builder.cpp` 1,861 -> 484 (three TUs
  under `tools/entity_composite/`, `namespace ecb`), and
  `tools/hwcaps/gles2_fog_volume_benchmark.cpp` 1,711 -> 1,220 (ten GLSL
  sources to their own TU). Each was proved by byte-comparing the tool's own
  output before and after — for `entity_composite_builder` that is 16 produced
  files identical by md5. `docs/decisions.md` 2026-09-05, `CLAUDE.md`
  §"Process requirement" and `refactor/M00_ceiling_closed.md` all say
  "tree-wide" truthfully now. **Remains open only for the two `hwcaps` zone
  files, which are T387's** (see B185). Prov: T381 Review (file-size table),
  T385 Review.

## Added 2026-09-05 by T385 (tools/ under the ceiling)

- ~~**B185** (P) The two `tools/hwcaps` zone files are the last two files in the
  tree over the size ceiling: `tools/hwcaps/zone_bench_loader.cpp` **3,280**
  and `tools/hwcaps/gles2_zone_benchmark.cpp` **3,060**, both over the 3,000
  hard cap, not just the 1,500 ceiling. They were invisible until T385 pointed
  `check_file_sizes.sh --all` at `tools/` (B167), so `FileSizeCeiling` is RED
  on the tip until they are split. Dispatched to **T387** (slot3) in the same
  wave as T385; this entry exists so the RED test has an owner on the board and
  is not mistaken for a T385 regression. Note for whoever takes it:
  `gles2_zone_benchmark` is what `scripts/ci/visual_gate.sh` targets, so its
  split needs a gate run, not just a byte-compared log. Closes when both are
  under 1,500 and `check_file_sizes.sh --all` is 0 FAIL. Prov: T385 Review. **CLOSED by T387 (`4b5a697c`…`39131bf6`): `zone_bench_loader.cpp` 3,280 → 977 (+4 TUs), `gles2_zone_benchmark.cpp` 3,060 → 992 (+3 TU pairs); gate before/after: 9 golden PNGs, every asserted memory category, all deterministic CSV columns and `--help` byte-identical. `--all` is 0 FAIL on the merged tip.**~~ **CLOSED by T387 (see B167).**
## Added 2026-09-05 by T383 (vision colour channels)

- ~~**B180** (B) `tests/test_direct_sender_census.cpp`
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
  Prov: T383 Review, `review/T383/README.md`.~~ **CLOSED at the T378 merge (`b48996f7`, before T383 reported):** the orchestrator learned the pin in place — `kLoginWorldExpected` 12 → 0, case renamed `LoginAndWorldStreamsHaveNoDirectSendersEither`; the merge-step golden rule applies to census pins too.

## Added 2026-09-05 by T386 (owed live proofs 6)

Full evidence for every entry below: **`review/T386/README.md`** (measurement
only; no source file changed; tip `c0a76fea`, both slot2 trees rebuilt from it).

- ~~**B200** (A) `src/client/audio/audio_manager.cpp` `AudioManager::loadSoundDataFromPfs`
  / `loadSound` — **every sound that lives in a `snd*.pfs` archive fails to load
  at runtime, so fable is silent for the whole UI/combat sound set.** MEASURED
  T386 (`review/T386/` §1b) in one live `arena` session: **31** `Failed to load
  sound: <name> (not found on disk or in PFS)` lines and **zero** `Loaded sound
  from PFS` lines. Among them is **`BuyItem.WAV`, sound id 138** —
  `Sound failed: id=138 file=BuyItem.WAV (not found)`, 11 ms after the corpse
  loot response — so T382's B172 wiring is proven live end to end
  (`PlaySoundIntent{BUY_ITEM}` -> `BridgeComponent::PlaySound` ->
  `AudioManager::playSound(138)` -> the db resolves the filename) and the player
  still hears nothing. Also `SwordBlk.WAV` (120) once a combat round,
  `Swing.WAV`, `SwingBig.WAV`, `Kick1.WAV`, `KickHit.WAV`, `RndKick.WAV`,
  `Punch1.WAV`, `PunchHit.WAV`, `Spell_1..5.wav`, `SpelCast.WAV`,
  `LevelUp.WAV`, `WaterIn.WAV`, `WatTrd_1/2.WAV`. **Narrowed, not root-caused:**
  the names ARE in the index the client loaded (`config/snd_index_cache.json`
  maps `buyitem.wav` and `swordblk.wav` to `snd2.pfs`; the client logs
  `Loaded PFS index cache: 2088 sound files`), and `loadSoundDataFromPfs`
  lowercases before the lookup, so case is not the obvious cause; every OTHER
  failure exit in that function logs (`Failed to open archive (lazy)`,
  `Failed to extract … (lazy)`, `Failed to decode WAV from PFS`) and **none of
  those lines appears**, which leaves the silent `pfsFileIndex_.find() == end()`
  return as the only reachable exit. Closes when a live session plays a PFS
  sound and a test pins the id-138 lookup against the shipped index.
  Prov: T386 Review §1b; B172; B036.~~ **STRUCK T392 (`1311b50a`)**: it is NOT an audio-path defect --
  `AudioManager`, the PFS reader and the shipped index were all correct.
  **`data/willeq.db` held `BuyItem.WAV` followed by a CARRIAGE RETURN; all
  2,165 `sound_assets.filename` rows carried one**, because
  `tools/willeq_db_builder.cpp` read Titanium's CRLF `*.txt` files with a bare
  `std::getline` on newline, leaving the CR inside the LAST field of every
  record. `playSound(138)` therefore looked up `buyitem.wav<CR>`,
  `pfsFileIndex_.find()` missed, and the one exit that does not log returned --
  exactly the exit T386 narrowed to. **The CR is also why the cause stayed
  invisible: the failure log line ENDS in it, so a terminal renders
  `Failed to load sound: BuyItem.WAV` with the name looking perfect.** Four
  more tables carried the same taint -- `eqstrings` (5,901), `dbstrings`
  (8,305), `race_data` (475), `login_strings` (470) -- and are fixed by the
  same change. Fix: a `readLine()` helper used at all eight file loops in the
  builder, plus a `split()` that keeps a trailing empty token (the old
  istringstream form dropped `dbstr_us.txt`'s 19 deliberately-blank strings the
  moment the CR stopped padding them), and `data/willeq.db` rebuilt --
  **schema and version unchanged (still 7), the other seven tables
  bit-identical, every difference in the five CR-only**. Pinned by
  `tests/test_pfs_sound_load.cpp` (15 tests, 7 red pre-fix) against the real
  archives: `BuyItem.WAV`/138 plus SwordBlk/120, Swing/118, Spell_1/103,
  LevelUp/139, WaterIn/100 by name AND by id, an all-2,165-row
  control-character sweep, and two negatives. ~~**The LIVE ear is still OWED**
  (no server this unit; the archives on disk were the oracle) -- one T328
  tone-probe row for id 138 on the next lock unit, see B036/B172.~~
  **TAKEN T401** (`review/T401/` §1): 21 `Loaded sound from PFS`, zero
  `not found`, and id 138 identified in the recording by matched filter
  against the archive's own bytes (ncc 0.83). Not by a tone probe -- a
  tone-probe client reads LOOSE files and would have bypassed the PFS path
  this entry is about; the method and that deviation are in §1b. New: B250,
  B251.

- ~~**B201** (A) `src/client/bridge/irrlicht_bridge.cpp`
  `GameEventType::CurrencyChanged` arm / `renderer_entities.cpp`
  `updateCharacterStats` — **the player's purse is never drawn anywhere, so
  "watch the purse move" is not a thing a fable player can do.** The bridge arm
  for `CurrencyChanged` is a comment plus a `LOG_TRACE` ("Currency display is
  context-dependent … Deferred"), and the renderer entry point that is supposed
  to carry the four counts discards them outright
  (`(void)platinum; (void)gold; (void)silver; (void)copper;`). CONFIRMED live
  T386 (`review/T386/` §1c): the inventory window opened immediately after a
  corpse loot shows paperdoll, equipment and general slots and **no coin
  counters** (`review/T386/fable/T386_fable_arena2_purse_01_after_1024x768.png`),
  while `character_currency` moved by exactly the amount the chat line named.
  So B172's own close-out wording is unmeasurable as written, and every future
  coin row has no on-screen consumer. Closes when the coin counts reach a panel
  and a test pins the projection. Prov: T386 Review §1c; B172.~~ **STRUCK T393 (`f13b9979`)**: the purse is drawn. `InventoryLayout::coinRect()`/`coinDecalRect()` put four coin rows in the inventory popup at the OFFICIAL client's own placement -- `/mnt/projects/EverQuestP1999/uifiles/default/EQUI_Inventory.xml`, `IW_Money0..3` at Location X=303, Y=121/147/171/197, Size 70x24, decals A_PlatinumCoin/A_GoldCoin/A_SilverCoin/A_CopperCoin at DecalOffset 1,3 DecalSize 18x18, inside a 428x460 `InventoryWindow` -- mapped proportionally onto the fixed 400x350 `centerPopup` (283,92 / 283,111 / 283,130 / 283,149, 65x18, identical at all five resolutions T351 measured). The `CurrencyChanged` bridge arm is no longer a payload-discarding `LOG_TRACE`: it calls the free function `applyCurrencyChanged()` (the `applyLootWindowOpened()` idiom) onto `InventoryPanelState::coins`, so ALL FIVE publishers -- `OP_MoneyUpdate` (`src/client/core/everquest.cpp`), the corpse-coin grant (`src/client/commands/combat_actions.cpp`), the merchant (`src/client/merchant/merchant_trade.cpp`) and the two stat pushes in `bridge/` -- reach the panel. `updateCharacterStats`'s four `(void)` discards are gone too, though that function still has ZERO callers tree-wide (grepped `src/ include/ tests/ tools/ benchmarks/ docker/`) -- the reaching path is the bridge arm. 12 tests in `tests/test_static_ui_panels.cpp` (`PurseT393*`), **7 of them fail pre-fix**, including a device-backed render pin (`TheInventoryPopupEmitsTwoTextEntriesPerCoinRow`: 24 text entries before, 32 after). ~~**The live screenshot pair (fable vs oracle inventory with coins) is still OWED** -- T393 held no eqemu lock.~~ **TAKEN T401 (`4ff78bb4`, `review/T401/` §2), and the two windows agree**: four rows, platinum->gold->silver->copper, a round decal left and the count right, **9 / 13 / 2 / 16 in both** (the oracle character's purse was written to fable's post-loot purse before login). `review/T401/fable/T401_fable_purse_coinrows_crop4x.png` vs `review/T401/oracle/T401_oracle_purse_coinrows_crop4x.png`. B218's letter is gone. **B219 is now VISIBLE rather than predicted** and is what remains. [B219 CLOSED by T409 Leg A.] Two consequences boarded: **B218** (fable's UI atlas has no coin sprite, so the decal box gets a letter) and **B219** (fable's paperdoll rect spans the whole inter-column gap, so the oracle-scaled coin column overlays its right margin). Prov: T393 Review; B172.

- ~~**B202** (A) `src/client/graphics/sim/` `SimBridgeComponent::applySimulationOutput`
  — **SIGSEGV on the render thread whenever a second player is in the zone.**
  MEASURED T386 (`review/T386/` §5): **five segfaults across seven
  client-instances in six two-client runs**, in `qeynos2` AND in `arena`, in
  both the observing and the casting role, 20-60 s after zone-in. Single-client
  sessions on the same binary in the same zones ran 5+ minutes clean.~~
  **STRUCK by T391 (`e332162f`, 2026-09-05) — and it was never a race.** The
  worker parked in its condition variable was the tell: this is a plain
  use-after-free of `EntityVisual::lightNode`.
  `EntityRenderer::setEntityLight()` builds it with
  `addLightSceneNode(nullptr, ...)`, so Irrlicht parents it to the ROOT node
  and drops its construction reference — the node comes back at a reference
  count of **one, held by its parent alone**, while `EntityVisual` keeps a raw
  pointer. `applySimulationOutput()`'s cull arm calls
  `visual.lightNode->remove()`, which is `Parent->removeChild(this)`, which
  drops: the node is **deleted** and the pointer dangles, so the next frame
  that re-admits the entity runs `addChild()` on freed memory. **The "second
  player" shape is the other player's lantern**: T386's crashing session built
  exactly ONE entity light node out of 206 entities, for Testbuddy
  (`items.light = 11`); no NPC in `qeynos2` carries a light, so a single-client
  session has nothing to free. Reproduced under ASAN
  (`review/T391/logs/t391_prefix3_asan_asan.2431866`, heap-buffer-overflow at
  `entity_lifecycle.cpp:210`), with a befallen run whose second player carried
  no light surviving 407 s as the negative control. Fixed by ownership at the
  apply side (`grab()` at creation, `drop()` on all three clear paths), pinned
  by `tests/test_entity_light_node_lifetime.cpp` (5 tests through the real
  apply seam; 2 failed pre-fix, all pass post-fix), live-proven by two-client
  ASAN sessions surviving 331 s in `qeynos2` and 330 s in `arena` with no
  report and a clean shutdown. **B160's owed two-client proof is unblocked but
  still owed** — T391 did not have the budget to take it. Prov: T391 Review;
  T386 Review §5.

- ~~**B203** (A) `include/client/graphics/vision_ambient.h` `kUltravisionAmbientGain`
  / `kUltravisionTint` / `kInfravisionAmbientGain` / `kInfravisionTint` —
  **B171 REOPENED: T383's re-solve is refuted by the live re-measure it was
  waiting for, and it made both scalar ratios WORSE.** MEASURED T386
  (`review/T386/` §4), T377 §2 repeated exactly (same character, spot, forced
  22:00, 700x420+40+40 crop, linear light, video-stepped over 882 frames;
  composition control: the two sessions' night stills differ by RMSE 2.7%).

  | | R | G | B | grey |
  |---|---|---|---|---|
  | oracle Ultravision | x1.4581 | x1.4633 | x1.8507 | **x1.5612** |
  | T383 predicted | x1.4569 | x1.4640 | x1.8503 | x1.5561 |
  | fable pre-T383 | x1.4609 | x1.2814 | x1.6629 | x1.4483 |
  | **fable post-T383 (measured)** | **x0.9674** | **x0.9723** | **x1.6377** | **x1.1686** |
  | oracle Infravision | x1.3284 | x1.2111 | x1.2079 | **x1.2555** |
  | T383 predicted | x1.3298 | x1.2125 | x1.2085 | x1.2582 |
  | fable pre-T383 | x1.6002 | x1.1264 | **x0.8209** | x1.2450 |
  | **fable post-T383 (measured)** | **x1.0688** | **x1.0345** | **x1.1274** | **x1.0757** |

  Three facts. (1) **The `B x0.8209` anomaly did NOT reproduce** — Infravision
  blue is now x1.1274, moving UP and closest of the three to the oracle; the
  question T383 left open is answered no. (2) **The forward model is wrong**:
  raising the floor's R and G from 0.08 to ~0.111 predicted x1.457/x1.464 and
  delivered **x0.967/x0.972** — the frame got very slightly DARKER in those
  channels, so T361's `ambient^1.1405` power law does not hold per channel at
  this operating point. (3) **Both scalars regressed**: Ultravision 1.4483 ->
  1.1686 (93% of the oracle -> 75%), Infravision 1.2450 -> 1.0757 (99% -> 86%),
  and the pre-T383 Infravision scalar was very nearly right. A fourth
  observation for whoever takes this: fable's night-no-buff BASE also moved
  between the two tips on the same crop — R 0.004256 -> 0.005295, G 0.003610 ->
  0.004362, **B 0.002649 -> 0.004095 (+55%)** — which nothing in T383's stated
  changes touches. Closes when a live re-measure lands all six channel ratios
  and both greys within ~2% of the oracle table, `tests/test_vision_ambient.cpp`
  pins the constants that did it, and the fit is closed against a measurement
  rather than extrapolated. Prov: T386 Review §4; B171; B067.

  **T394 (`7cd6c124`) REVERTED T383's six constants and its redShift** —
  `include/client/graphics/vision_ambient.h`, `include/client/graphics/vision_type.h`
  and `src/client/graphics/vision_ambient.cpp` are byte-identical to the
  pre-T383 tree again (Ultravision gain **0.1521** x tint **(1.009, 0.848,
  1.143)**, Infravision gain **0.0956** x tint **(1.539, 0.843, 0.617)**,
  Infravision lamp redShift **0.3**; `visionFrameRatios()` and
  `kVisionFrameExponent`, which existed only to express the refuted model, are
  gone with it). A refuted model is reverted, not patched: **no constant here
  may be moved by argument again.** `tests/test_vision_ambient.cpp` gained
  `VisionGainsRevertedT394Test` (5 cases naming the refutation) and
  `tests/test_vision_zone_lights.cpp` is back to T360's literal; **6 cases fail
  against T383's constants and all pass against the restored ones**.

  **B203 STAYS OPEN.** What is OWED is a **LIVE A/B search** in the next lock
  unit — one parameter at a time, oracle frame beside fable frame, T361's
  method — ~~starting from the live-measured pre-T383 baseline (Ultravision
  R 1.4609 / G 1.2814 / B 1.6629, grey 1.4483; Infravision R 1.6002 / G 1.1264
  / B 0.8209, grey 1.2450)~~ **— and NOT from those numbers, which T401
  REFUTED as weather-inflated (B252): re-measured on the same T394 constants
  under a modifier PROVEN 1.000 for the whole window, fable's Ultravision is
  grey x1.1606 (R 1.2078 / G 1.1658 / B 1.0649) and its Infravision grey
  x1.1693 (R 1.2754 / G 1.1076 / B 1.0596). Nor from the oracle table above,
  which did not reproduce per-channel — see B253.** Not a desk re-fit: the
  desk re-fit is what T383 did.

  **The +55% blue night-base shift: BISECTED TO NO COMMIT, and the plan's range
  was wrong.** `c0a76fea..65cbed39` is not the interval between the two
  measurements — `c0a76fea` IS T386's own base/tip and `65cbed39` is later.
  The real interval is **`032de79a` (T377's tip) .. `c0a76fea` (T386's tip)**,
  48 commits. Diffed in full: the only changes under `src/client/graphics/` or
  `include/client/graphics/` are T383's vision floors, T381's spell FX and
  T382's UI panels/hotkeys. **Nothing in the interval touches
  `updateTimeOfDay`, the day/night ambient curve, the zone lights,
  `sim_animation.cpp`, any shader, fog or culling** — and T383's floors cannot
  reach an unbuffed frame here, because Testchar is **race 1**, which the
  session's own log resolves as `Player race 1 -> base vision: Normal`
  (`review/T377/logs/fable_session_vis.log:808`). No commit changed the night
  base. What the bisect found instead is **B211**. Prov: T386 Review §4; T394
  Review; B171; B067.

  **T396 CLOSED THE WEATHER CONFOUND B211 NAMED, AND THE ANSWER IS WORSE THAN
  "UNCONTROLLED".** T377's whole vision-measurement window ran under SERVER
  RAIN at intensity 9 (`weatherMod = 0.460`) — see the struck B211 for the
  packet and the arithmetic. Weather multiplies the base ambient and leaves
  the vision floor untouched, so **every ratio measured in that session is
  inflated**, and `VisionAmbientCompositionT396Test.UncontrolledWeatherInflatesAMeasuredVisionRatio`
  shows the apparent Ultravision ratio more than DOUBLING at 0.460. The owed
  LIVE A/B search must therefore (a) verify the modifier is 1.000 for the
  whole window — the log now prints every change in both directions plus one
  line per zone-in, so this is now checkable from the session log alone — and
  (b) treat the pre-T383 baseline numbers above as SUSPECT until re-measured
  under a verified-clear sky, because nobody knows what weather T358/T361/T377
  were under. Nothing about the constants themselves changed in T396.~~ **STRUCK T405 (`2edf3b3b`) -- THE LIVE RE-SOLVE LANDED.** The owed A/B search was
  run with BOTH confounds pinned for the first time: weather proven 1.000 for
  every window from the client's own log (and `#set weather 0 0` is NOT enough
  on its own -- `Zone::ChangeWeather()` re-rolls on a 60 s timer from
  `zone.rain_chance1..4`, which is 10/10/10/10 for qeynos2 and dropped rain
  INSIDE this unit's first Ultravision window; the rig zeroes those columns for
  the session and restores them), and the camera AIMED with
  `#goto -74 428 3.751 160` (see the struck B253). On that rig the composition
  control passes at RMSE 0.389% (oracle) and 1.540% (fable).

  Six probe builds, each re-measured in its own live session (the full search
  log is `review/T405/README.md` §3). Committed:
  Ultravision gain **0.24607** x tint **(1.07877, 0.79866, 1.12256)**,
  Infravision gain **0.16257** x tint **(1.15398, 0.70739, 1.13864)** --
  i.e. floors (0.26545, 0.19653, 0.27623) and (0.18760, 0.11500, 0.18511).
  Measured on exactly those, mean of two confirmation sessions:

  | | R | G | B |
  |---|---|---|---|
  | oracle Ultravision (heading-pinned) | x2.2531 | x1.4399 | x1.7687 |
  | **fable Ultravision** | **x2.2662** (+0.58%) | **x1.4730** (+2.30%) | **x1.8020** (+1.88%) |
  | oracle Infravision (heading-pinned) | x1.8744 | x1.1956 | x1.1936 |
  | **fable Infravision** | **x1.8679** (-0.35%) | **x1.1576** (-3.18%) | **x1.1631** (-2.56%) |

  From 75% / 88% of the oracle's grey (T401's clear-sky measurement of the
  T394 constants) to every channel inside 3.2%. **The greys are NOT within 2%
  and CANNOT be**: "grey" is the unweighted mean of the three channel means, so
  a grey ratio is a base-weighted average of the channel ratios weighted by each
  client's OWN night base -- fable's is red-heavy, the oracle's green-heavy, and
  two clients matching exactly per channel still report different greys. Against
  the grey fable would report at exact per-channel agreement (computed from its
  own measured base) the residuals are **+1.39% and -1.67%**, and
  `VisionSolvedT405Test.GreyIsBaseWeightedSoTheOracleGreyIsNotATarget` pins that
  arithmetic. The two facts that made the solve possible, both new: the three
  channels are **coupled** (raising the R and B floors lowers the frame's G
  ratio at a fixed G floor, proven by an isolated three-point G sweep), so a
  per-channel fit could never converge; and the old Infravision floor
  (0.147, 0.081, 0.059) sat **below the night base on B and 1% above it on G**,
  so two of its three channels could not move at all. Eight
  `VisionSolvedT405Test` cases pin the constants, the floors, the base-clearance
  rule and the grey arithmetic; **5 fail against T394's constants and all pass
  against T405's**. `vision_type.h` is untouched (a probe-only lamp change was
  reverted and is pinned). Residues: the two chromaticity questions are now
  **B280**, the world-clock drift **B281**. Prov: T405 Review; `review/T405/`.

- ~~**B211** (A) `src/client/graphics/renderer/irrlicht_renderer.cpp`
  `IrrlichtRenderer::updateTimeOfDay` — **the weather ambient modifier
  multiplies the night ambient and is uncontrolled by, and invisible to, every
  vision measurement taken so far.** FOUND T394 while bisecting B203's +55%
  blue night-base shift, which no commit between the two measurement tips
  explains (see B203). `updateTimeOfDay` scales `r`, `g`, `b` and
  `sunIntensity` by `weatherEffects_->getAmbientLightModifier()`, and T377's
  own vision session logged **`weatherMod=0.460`** 26 s before its first
  measurement window
  (`review/T377/logs/fable_session_vis.log:9193`) — the night ambient more
  than halved, with no `#set weather` in any measurement recipe. Two
  consequences. (1) The log line fires only when the modifier **changes by
  >0.01 AND is below 0.99**, so a return to 1.0 is never logged: a session
  cannot be shown to have been weather-free from its log. (2) The vision floor
  is applied **AFTER** the weather multiply (the comment on the floor block
  says so deliberately), so weather darkens the base a vision ratio is measured
  against while leaving the floor fixed — which **inflates a measured vision
  ratio in proportion to the weather at the time**. Every cross-session night
  or vision-ratio comparison in T358/T361/T377/T383/T386 is therefore
  unpinned, T383's fit included, and B203's owed live re-solve cannot be
  trusted until this is nailed down. Closes when the measurement recipe forces
  and verifies a known weather state (or the client logs the modifier every
  time it is applied), the ordering against the floor is a deliberate,
  tested decision, and one session demonstrates a repeatable night base. **Not
  fixed in T394** — T394 was a revert and holds no server lock. B210 is left
  free for T391. Prov: T394 Review; B203.~~ **STRUCK T396 (`0c0d484d` +
  `824de12e`).** The 0.460 is REPRODUCED headlessly with its source, and it is
  not a client defect: **the SERVER sent rain.** The same session log carries
  `Weather packet: type=0, intensity=9` (line 1074) and the decode
  `newType=1 newIntensity=9` (7443) 24 s before the 0.460 at 9193, so
  `targetDarkening = storm.maxDarkening(0.6) * 9/10 = 0.54` and
  `modifier = 1 - 0.54 = 0.460` exactly. The two overlay multipliers
  contributed nothing (unified GLES2 weather particles were active, so
  `getRainDaylightMultiplier()` returns false) and lightning was off under the
  session's own `low` preset. `tests/test_weather_ambient_modifier.cpp` (9
  cases, no device: the controller takes four nulls) pins the arithmetic, the
  0.5/s ramp, and the oracle's RULE that clear weather leaves the modifier at
  exactly 1.0. **The weather-ACTIVE value is still unmeasured against the
  oracle and was deliberately not touched — see B230.**
  Three fixes. (1) **`onZoneEnter()`/`onZoneLeave()` had ZERO callers**
  (grepped `src/ include/ tests/ tools/ benchmarks/ docker/`) and
  `onZoneEnter()` reset nothing anyway, so a zone-in inherited the previous
  zone's darkening — the oracle rule broken in the one place it matters. Both
  now share `resetWeatherState()` and both are wired (SEQ Step11 in
  `renderer_zone_load_env.cpp`, `unloadZone()` in
  `renderer_zone_lifecycle.cpp`). (2) The log fires on **every** change in
  **both** directions with the old value, the new value and the type/intensity
  behind it, plus once per zone-in; the last-logged value moved from a
  function-static to the member `weatherModLogged_`. A session can now be
  shown weather-free from its own log. (3) **The ordering is a tested
  decision, not a comment**: the four-step composition moved into
  `composeSceneAmbient()` (`vision_ambient.h/.cpp`), which `updateTimeOfDay()`
  calls, and the vision floor is LAST. Six `VisionAmbientCompositionT396Test`
  cases pin it, including B211's own consequence — under rain at 0.460 the
  apparent Ultravision ratio more than DOUBLES. Proven a real net by
  inverting the two steps (2 of 6 red, green on restore). Visual gate PASS
  with **no golden moved**: `gles2_zone_benchmark` receives no `OP_Weather`,
  so the goldens do not carry weather and the STOP condition did not fire.

- ~~**B204** (B) `src/client/graphics/spell_visual_fx_nodes.cpp` /
  `SpellVisualFX` — no spell impact visual is drawn at all, on the observing
  client or anywhere.~~ **STRUCK T395 (`4537d40f`)**: the chain is NOT intact,
  and the two broken links found are both upstream of the atlas, so B162/B165
  are untouched.
  1. **THE GATE, and it made the visual unreachable by construction.**
     `SpellManager::handleSpellAction` set `isSuccess = (effect_flag == 4)` and
     `irrlicht_bridge.cpp` gated `createSpellComplete()` + `createImpact()` on
     it. eqemu sends TWO OP_Action packets per cast (`Mob::SpellOnTarget`,
     `/mnt/projects/akk-stack/code/zone/spells.cpp`): `effect_flag = 0` --
     whose own comment reads *"Actual cast action - this causes the caster
     animation and the particles around the target"* -- goes to the target,
     the caster AND `entity_list.QueueCloseClients(...)`, i.e. everyone in
     range; the `effect_flag = 0x04` repeat, *"what causes the buff icon to
     appear"*, is queued **only to the target client and the caster client**.
     So the impact was gated on the one packet a bystander never receives.
     T386's own logs say so and nobody read them that way: the observer
     (`review/T386/logs/fable_session_spells13_observer.log:952,1161`) records
     `effect_flag=0` twice and `effect_flag=4` **never**, while the caster's
     log records the 4. Fixed by carrying `isCastAnimation = (effect_flag == 0)`
     on `SpellCastVisualCompleteData` and gating the FX on that; `isSuccess`
     keeps meaning "landed" for the chat lines, the buff icon and
     `completeCast()`. The cast-timeout path arms it too (no OP_Action ever
     arrives there).
  2. **THE ORIGIN BURST.** `SimulationWorker::computeParticles` runs its SPAWN
     phase **before** its spell-effect LIFECYCLE phase, and only the lifecycle
     phase refreshed an entity-attached emitter's position from
     `pi.entityPositions` -- which is always one frame behind, because the
     renderer resolves only what the PREVIOUS frame's `positionRequestEntities`
     named. So an impact emitter was created at `(0,0,0)` and was still there
     on its first spawn frame: with culling off it fired its whole one-shot
     burst at the world origin and latched `isBurstSpawned` (**measured: every
     particle 1,020.76 units from the target**), and with culling on -- the
     shipped configuration -- it was culled there instead, spending a frame out
     of the impact preset's 0.1 s `emitterLifetime` (below ~20 fps it is dead
     before its position ever arrives). Fixed by refreshing an attached
     emitter's position in the spawn phase, before the lifetime check and the
     frustum test.
  Pinned by `tests/test_spell_impact_visual.cpp` (5 tests, **4 red pre-fix**).
  ~~**The live pixel proof is OWED to T389** — this unit held no server lock, so
  "a landed spell paints something an observing client can see, and the two
  vocabulary colours differ on screen" is still unmeasured.~~ **TAKEN T401
  (`4ff78bb4`, `review/T401/` §3): BOTH halves are proven.** The observing
  client logs `SpellAction: … effect_flag=0` and then `Enqueued
  CreateSpellEffect 'Impact'` + `SimWorker: Created spell effect 'Impact'`
  for each of the two casts — T395's `effect_flag == 0` gate is right on a
  bystander, which sees the 0 and never the 4 — and the pixels move, in two
  different colours (see B160). The plan's note about `#castspell` was
  followed: the casts were `/cast` from a gem. Note for whoever
  takes it: **`#castspell` is the wrong driver for the cast-glow half.** eqemu
  broadcasts `OP_BeginCast` to close clients (`Mob::SendBeginCast`,
  `QueueCloseClients`), but T386's observer log carries no BeginCast at all
  because a GM `#castspell` never sends one — use a real gem cast.
  Prov: T386 Review §3; B160; B161; B165.

- ~~**B205** (B) the eqstr for "your spell failed to land … invulnerable" —
  fable prints a 45-character hex blob in front of the spell name in a line the
  player sees.~~ **STRUCK T395 (`54643545`)**. It is **not an eqstr and not a
  colour code**: it is an eqemu **SAY-LINK**, and `0FFFFF` is not `0F` + `FFFF`
  but `action_id = 0` followed by `item_id = 0xFFFFF` (`SAYLINK_ITEM_ID`).
  `Mob::SpellOnTarget` wraps the spell name in
  `Saylink::Silent("#castspell <id>", <name>)`
  (`/mnt/projects/akk-stack/code/common/say_link.cpp`,
  `EQ::SayLinkEngine::GenerateLink()`), which emits
  `0x12 <fixed-width hex body> <display text> 0x12`. The body width is
  client-version specific and the Titanium/"6.2" layout is exactly **45**
  characters: `%1X` action_id, `%05X` item_id, `%05X`×5 augments, `%1X`
  is_evolving, `%04X` evolve_group, `%1X` evolve_level, `%08X` hash. The two
  measured blobs therefore decode as `item_id = 0xFFFFF` with
  **`augment_2 = 0x4F` (79)** and **`0x50` (80)** -- consecutive `saylink`
  table row ids for `#castspell 93` and `#castspell 54` (a SILENT saylink puts
  its id in augment_2, a spoken one in augment_1), which is why the board saw
  them as "sequential and not the spell ids". Two corrections to this entry's
  own text: the delimiters ARE in the line and were invisible in the
  transcription (`cat -A` on
  `review/T386/logs/fable_session_selfcast.log:1041,1879` shows `^R` = 0x12 on
  both sides), and the second blob quoted here is one character short -- both
  are 45 on the wire. Fixed by `eqt::StripSayLinks()`
  (`include/client/say_link_text.h`, `src/client/core/say_link_text.cpp`),
  applied at the two consumers that print server-authored text:
  `ZoneComponent::OnSpecialMesg` and `ChatComponent::ZoneProcessChannelMessage`
  (a player's item link is the same wire shape). Pinned by
  `tests/test_say_link_text.cpp` (7 tests, **2 red pre-fix**).
  Prov: T386 Review §3.

### Added / struck by T392 (2026-09-05)

- ~~**B215** (A) `tests/test_sound_assets.cpp` `AudioManagerTest.PreloadCommonSounds`
  -- the ONE standing suite waiver ("ALSA host quirk, expected-fail") was a
  MISDIAGNOSIS and had been quoted as fact in `CLAUDE.md`, `README.md`,
  `refactor/EXECUTOR_PROTOCOL.md`, `refactor/TESTING_CHARTER.md`,
  `refactor/test_baseline.txt` and the BOARD header since 2026-07-22.~~
  **STRUCK T392 (`1311b50a`)**: the host's missing ALSA card is real and noisy,
  but `AudioManager::initialize()` falls back to the RDP loopback backend and
  SUCCEEDS -- which the other twenty `AudioManagerTest` cases in the same binary
  had been proving all along. The failure was `0 vs 0`:
  `preloadCommonSounds()` resolves every sound id through
  `WilleqDatabase::globalInstance()`, and `test_sound_assets` never opened the
  database, so no id resolved to a filename. Fixed by adding the
  `WilleqDbEnvironment` the audio tests already use elsewhere plus the
  `WILLEQ_DB_PATH` define; it needed B200's `willeq.db` fix as well -- with the
  CR-tainted db and the environment in place it still returns `0 vs 0`, MEASURED
  both ways. **The suite now has NO expected failures**; every doc above was
  struck in the same commit. Prov: T392 Review.

- ~~**B216** (B) `tools/willeq_db_builder.cpp` `loadRaceData` / the `race_data`
  table -- 475 rows built into every `willeq.db` and read by nothing.~~
  **STRUCK T395 (`fd8f2779`)**: the census was re-run over `src/ include/
  tests/ tools/ benchmarks/ docker/ scripts/` and still returned only the
  builder, so `loadRaceData()`, its `CREATE TABLE`, its call site and the
  `race_data:` line of the builder's report are gone (a tombstone replaces the
  loader). **The schema version did NOT move**: `WilleqDatabase::open()`
  validates `metadata.version` and nothing else -- there is no table list to
  satisfy -- so this is content-only and `kWilleqDbExpectedVersion` stays at 7.
  `data/willeq.db` rebuilt (mode 664) with T392's identity proof: 14 tables ->
  13, and all **eleven** content tables byte-identical either side (row count
  plus a sha256 of the full `SELECT *` -- `spells` 8,066, `dbstrings` 8,305,
  `eqstrings` 5,901, `item_models` 3,661, `equipment_textures` 2,623,
  `sound_assets` 2,165, `equipment_models` 1,000, `login_strings` 470,
  `race_models` 190, `music_files` 24, `animation_sounds` 9). `metadata`
  differs only in `build_date` and `total_entries` (32,889 -> 32,414 = the 475
  deleted rows exactly). Pinned by
  `WilleqDatabaseShippedContentTest.RaceDataTableIsGone`.
  Prov: T392 Review.

### Added by T395 (2026-09-05)

- ~~**B220** (B) `src/client/graphics/environment/particle_manager.cpp`
  `ParticleManager::removeSpellEffect(effectID)` **cannot remove anything**:
  it writes the id into `cmd.preAssignedEffectID`, while the worker's
  `ParticleCommand::RemoveSpellEffect` arm (`src/client/graphics/sim_particles.cpp`)
  matches on `cmd.effectID`, which the setter never touches and which defaults
  to 0 -- and effect ids start at 1, so the loop can never hit. It has **ZERO
  callers** in `src/ include/ tests/ tools/ benchmarks/ docker/` (the B166
  rule), so nothing is broken today; it is a wired-wrong method waiting for its
  first caller. Either fix the field and give it a caller, or delete it with
  the rest of B161's unreached creation half. Same family as B195. Found by
  T395 while tracing B204.~~ **CLOSED by T397 Leg B**
  (`refactor/T397_spell_effects_config_path.md`). **DELETED**, with its
  command enum member, its worker arm and the `ParticleCommandData::effectID`
  field: re-confirmed zero callers across `src/ include/ tests/ tools/
  benchmarks/ docker/`, and no caller anywhere keeps the id
  `createSpellEffect()` returns, so by-id removal has no consumer to wire.
  Removal is by entity and always was. Recover with
  `git show 8382268d:src/client/graphics/environment/particle_manager.cpp`.

  **AND TRACING IT FOUND A LIVE CLASS-A DEFECT, FIXED IN THE SAME COMMIT.**
  `ParticleManager::removeSpellEffectsForEntity()` -- the removal every real
  caller uses (`SpellVisualFX::removeCastGlow` / `removeBuffAura` /
  `removeAllForEntity`) -- had the identical wrong-field bug, and there it was
  not harmless. It wrote `cmd.targetID`, leaving `cmd.entityID` at 0, while
  the worker matches `casterEntityID == cmd.entityID || targetEntityID ==
  cmd.entityID`. Every cast glow is `createSpellEffect(def, caster, 0)` and
  every impact is `createSpellEffect(def, 0, target)`, so one of those two
  fields is ALWAYS 0 and an `entityID` of 0 matched **every spell effect alive
  in the zone**. The call behaved as `clearAllSpellEffects()` while removing
  nothing it was asked to -- and `createCastGlow()` issues three of them
  before every single cast, so any player starting a cast wiped every other
  player's impacts and glows. This is a plausible contributor to B160's owed
  observer pair (T389) and to B204's "nothing was drawn": a second client
  casting anywhere nearby deleted the impact under test. Pinned by
  `tests/test_spell_effect_removal.cpp`, 3 tests, 2 red pre-fix, driving a
  real `ParticleManager::drainCommands()` into a real
  `SimulationWorker::computeOnce()`.

- ~~**B221** (B) `src/client/graphics/environment/particle_manager.cpp`
  `ParticleManager::ParticleManager()` loads `config/spell_effects.json` by a
  **CWD-relative path**, and every spell preset reads it through
  `SpellEffectsConfig::instance()`. Started from anywhere but the repo root the
  load fails silently and each preset falls back to `EmitterConfig`'s own
  defaults -- which are a different effect, not a degraded one: the shipped
  `impact` style is a 36-particle one-shot burst with a 0.1 s emitter lifetime,
  and the fallback is a **permanent** emitter at 10 particles/second. MEASURED
  by T395: `tests/test_spell_impact_visual.cpp` produced zero particles in two
  frames until it loaded the config the way the renderer does. No other data
  file is reached this way (`willeq.db` and the EQ tree are both configured
  paths). Closes when the path is resolved against the config directory the
  client was given. Found by T395.~~ **CLOSED by T397 Leg A**
  (`refactor/T397_spell_effects_config_path.md`). `SpellEffectsConfig::load()`
  resolves its argument through the new `eqt::ResolveDataPath()`
  (`include/client/data_paths.h`, `src/client/core/data_paths.cpp`), which
  tries the CWD-relative path FIRST -- so a run from the repo root, the visual
  gate included, reads exactly the bytes it read before -- then
  `$WILLEQ_DATA_ROOT`, then the running executable's directory and its
  parents. Pinned by `tests/test_data_paths.cpp`, 7 tests, 2 red pre-fix.
  **THREE CORRECTIONS to the text above, all measured.** (a) The load is in
  `ParticleManager::init()`, not the constructor. (b) The fallback on a
  MISSING FILE is `SpellEffectsConfig::setDefaults()`'s built-in style table,
  whose `impact` is a 12-particle burst -- still wrong, but not a permanent
  emitter. `EmitterConfig`'s struct defaults (the permanent 10/s emitter) are
  what you get when `load()` is **never called at all**, which is the harness
  state T395 measured, not the production miss. (c) "No other data file is
  reached this way" is FALSE and was the unit's biggest surprise: `willeq.db`
  is `"data/willeq.db"` in `application.cpp`, `willeq.json` itself defaults to
  a bare relative name, and the census found ~40 such literals under
  `src/client`. That census is **B235**, **STRUCK by T399 (2026-09-05)** --
  all 41 literal sites now route through `ResolveDataPath()`, which was the
  first such resolution in the tree.

- ~~**B218** (B) `src/client/graphics/ui/static_panels.cpp` `renderInventoryPopup`
  / `assets/ui_atlas` — **there is no coin sprite in fable's UI atlas.** The
  oracle's four `IW_Money` buttons each blit a decal
  (`A_PlatinumCoin` / `A_GoldCoin` / `A_SilverCoin` / `A_CopperCoin`) into an
  18x18 box at DecalOffset 1,3, and T393 reproduced that BOX
  (`InventoryLayout::coinDecalRect()`) but had nothing to put in it, so it draws
  the denomination letter `p`/`g`/`s`/`c` in the coin's colour instead. The
  `UISprite` enum has no coin entry and the atlas generator emits none. Closes
  when the four coin sprites are in the atlas, `coinDecalRect()` gets a
  `drawSprite`, and `tests/test_ui_atlas_pixels.cpp` pins them. Prov: T393 Review.

  **T396 Leg B STOPPED on the plan's own condition and did not change a line.**
  The pixels EXIST and are located exactly: `EQUI_Animations.xml` puts all four
  animations in ONE texture, `window_pieces01.tga`, as a contiguous 72x18 strip
  at (90,140) -- `A_PlatinumCoin` (90,140), `A_GoldCoin` (108,140),
  `A_SilverCoin` (126,140), `A_CopperCoin` (144,140), each 18x18. The file is a
  256x256 uncompressed 32-bit TGA (type 2, bottom-left origin, 262,162 B) at
  `/mnt/projects/EverQuestP1999/uifiles/default/window_pieces01.tga`, and the
  four alpha masks read as real ~16px coin discs (verified by decoding the
  header and dumping the masks). **Two independent blockers, neither of which
  the plan anticipated.** (1) They are **not in any archive fable reads**:
  `uifiles/` is named nowhere in `src/ include/ tools/ scripts/` except one
  comment in `inventory_layout.h`; `tools/ui_atlas_builder.cpp` is *fully
  procedural* and opens **no file at all** (a 256x256 RGBA buffer,
  `setPixel`/`fillRect`/`fillGradientV`, one hand-written `reg()` per sprite);
  and the only TGA decoder in the tree is a standalone `loadTGA()` in
  `tools/dump_icon_sheet.cpp`, not linked by anything under `src/`.
  `eqe1_builder` accepts `.bmp`/`.dds`/`.json`/`.wld` and never `.tga`. (2)
  Getting an atlas to the CLIENT means overwriting
  **`/mnt/projects/EverQuest_ETC1/willeq.eqe1`** -- a 2.8 MB SHARED asset
  outside every worktree, read live by every slot's ctest
  (`tests/test_ui_atlas_pixels.cpp` hard-codes that path) and by the visual
  gate -- and the worktree **cannot reproduce its inputs**: the canonical
  recipe is `eqe1_builder --bmp-dir data/atlas --bmp-dir data/textures`, and
  `data/atlas/` does not exist in `main` or any slot, while the shipped pack
  holds far more than `data/textures`'s 64 BMPs (`creature_atlas`,
  `detail_atlas`, `particle_atlas`, the rain/snow/storm sets, `software_cursor`,
  ...). A repack from a guessed input set would silently drop entries for every
  slot at once. Doing the code half alone is a REGRESSION, not a partial win:
  with no atlas entry `UIAtlas::mapSprite` drops the name with a `LOG_WARN`,
  `getSprite()` returns the empty rect, and `drawSprite` would replace a
  readable letter with a degenerate quad. **B218 stays open; the blocker is
  boarded as B230.** Prov: T396 Review Leg B.~~ **STRUCK T398 Leg B (this commit)**:
  the four coin decals are in the atlas and the popup draws them. T396 located
  the pixels correctly and both of its blockers were real; T398 Leg A removed
  the second one, and Leg B did the rest. `tools/tga_reader.h` (the decoder
  lifted out of `tools/dump_icon_sheet.cpp`, which was never a CMake target --
  the tree's only TGA decoder compiled NOWHERE, and both tools build it now)
  gives `ui_atlas_builder` a `--uifiles-path`; it crops the 72x18 strip at
  (90,140) of `window_pieces01.tga` into four 18x18 tiles at atlas row 4
  (y=224, x=0/20/40/60 -- a 20 px pitch so no 18 px tile shares a 4x4 ETC1
  block with its neighbour). Four new `UISprite` enumerators in denomination
  order, four `mapSprite` names, and `renderInventoryPopup` now calls
  `drawSprite(coinDecalRect(...), CoinPlatinum + denom)` where T393 wrote a
  letter. **Missing art is a HARD FAILURE at both levels** (Will's
  all-or-nothing rule): `ui_atlas_builder` exits 1 if `--uifiles-path` is given
  and the TGA is unreadable, and `build_willeq_pack.sh` refuses to pack at all
  -- a coinless atlas would be the exact regression this entry warned about.
  Five new tests in `tests/test_ui_atlas_pixels.cpp` (`UIAtlasCoinDecalTest`,
  device-backed against the live pack), **all five fail pre-fix** -- including
  the end-to-end one that renders the whole popup and samples the centre of
  the gold row's decal box: (69,69,71) before, gold after. The static-panel
  seam moved the other way and is pinned:
  `PurseT393Render.TheInventoryPopupEmitsOneTextEntryPerCoinRow` went 32 -> 28
  text quads, because the letter is gone. Pack rebuilt and reinstalled by Leg
  A's script; only 3 of 67 entries changed (`ui_atlas`, `ui_atlas_alpha`,
  `ui_atlas.json`), the other 64 byte-identical, +239 B total, all of it the
  four new sprite records in the JSON. Before/after captures in
  `review/T398/`. **B219 (the paperdoll overlaps the purse) and T393's owed
  live oracle pair are untouched and still open.** [B219 CLOSED by T409 Leg A;
  the live pair is still owed.] Gate consequence boarded as
  B240. Prov: T398 Review Leg B; B230; B218.

- ~~**B219** (C) `include/client/graphics/ui/inventory_layout.h` `paperdollRect`
  — **fable's paperdoll rect is proportionally far wider than the oracle's
  character view, so the oracle-placed purse sits ON it.** The oracle's
  `IW_CharacterView` is 85x168 at (166,57) inside a 428x460 window — 20% of the
  width — and its coin column at X=303 clears it entirely. fable's
  `paperdollRect()` fills the whole gap between the two equipment columns (x
  48..352 of a 400-wide popup, 76%), so the oracle-scaled coin rows (x 283..348)
  land over its right-hand margin. T393 drew them AFTER the paperdoll blit so
  they are on top (UIRenderer submission order is draw order, T294) and the
  character model is centred in the rect, but nobody has looked at the two
  together on screen. ~~Needs Will's eye on the owed live pair before anything
  moves~~ **THE PAIR IS TAKEN (T401, `review/T401/` §2) and it confirms the
  prediction exactly**: the oracle's coin column clears its character view
  entirely, fable's four rows land ON the paperdoll panel's right-hand margin,
  over the dark doll background. They are legible (drawn after the doll blit,
  as T393 intended). **This entry now waits on nothing but Will's ruling**:
  either the doll narrows to the oracle's proportion or the purse does
  not follow the oracle here. Prov: T393 Review; T401 Review §2.

  **WILL RULED 2026-09-06:** shrink the paperdoll to the oracle proportion; the coin column stays. → T409.~~
  **STRUCK T409 Leg A (this commit)**: `paperdollRect()` now maps the
  oracle's own `IW_CharacterView` -- Location 166,57 Size 85x168 inside the
  428x460 `InventoryWindow`, quoted from
  `/mnt/projects/EverQuestP1999/uifiles/default/EQUI_Inventory.xml` -- onto
  `centerPopup` by `coinRect()`'s rule (proportional, integer division, onto
  the fixed 400x350). Offset **(155,43) size 79x127** inside the frame at
  every resolution, i.e. {355,168,434,295} at 800x600, against the old
  {248,145,552,401} (offset (48,20) size 304x256). The doll now ends at
  popup-x 234 and the coin column starts at popup-x 283, so the two are
  **disjoint by 49 px at every one of the five resolutions T351 measured**,
  and disjoint from the four decal boxes too. The T60 invariants are
  unaffected -- the rect is still inside the frame and clear of all 30 slots.
  Second, smaller finding the narrowing forced: the paperdoll's offscreen
  render target was a **hardcoded 304x256 in two places**
  (`overlays_hud_env.cpp` `ensurePaperdollInitialized()` and
  `tools/hwcaps/ui_inventory_capture.cpp`), sized to the OLD rect, so a
  narrowed rect would have squashed the model 1.9x horizontally. Both now
  call the new `InventoryLayout::paperdollRenderTargetSize()` (the rect
  supersampled 2x = 158x254), so the target can never drift from the rect
  again. 4 tests in `tests/test_static_ui_panels.cpp` (`PaperdollT409.*`),
  **3 red pre-fix**; the fourth (`TheRenderTargetCarriesTheRectsAspect`) is a
  guard that was green pre-fix by construction and would have gone red on the
  rect change alone. ~~The live screenshot pair is OWED~~ -- still OWED, to
  the next unit that holds the eqemu lock (T405 holds it this wave).
- ~~**B230** (B) `tools/ui_atlas_builder.cpp` / `/mnt/projects/EverQuest_ETC1/willeq.eqe1`
  -- **the UI atlas cannot take a pixel from the official client, and the
  shipped pack cannot be rebuilt from the tree.** Split out of B218 by T396
  Leg B, which stopped on it (full evidence under B218). Two pieces, and the
  second blocks every future atlas change, not just the coins. (a)
  `ui_atlas_builder` opens no file: adding an oracle sprite needs a TGA reader
  (lift `loadTGA()` out of `tools/dump_icon_sheet.cpp`) plus a
  `--uifiles-path`, or a deliberate ruling that fable's atlas stays
  hand-drawn. (b) **`willeq.eqe1` has no reproducible build recipe in the
  repo**: `data/atlas/` is absent everywhere, so the documented
  `eqe1_builder --bmp-dir data/atlas --bmp-dir data/textures` cannot be run,
  and the pack holds entries `data/textures`'s 64 BMPs do not explain. Until
  (b) is fixed nobody can change one atlas pixel without hand-assembling a
  2.8 MB shared asset that three worktrees read concurrently. Closes when the
  pack has a checked-in deterministic build (a script or CMake target that
  reproduces the current file byte-for-byte -- T279 recorded the builder as
  deterministic) and a repack is one command with a temp-file-then-`mv`, so a
  concurrent slot never reads it half-written. Then B218 is a small unit.
  Prov: T396 Review Leg B; B218.~~ **STRUCK T398 (Leg A `457fa812`, Leg B this
  commit)**: both halves are closed, and **(b)'s premise was wrong**. The pack
  partitions with ZERO residue. Its 67 entries are the 3 the procedural
  `ui_atlas_builder` produces (`ui_atlas`, its ETC1 alpha companion
  `ui_atlas_alpha`, and the raw `ui_atlas.json`) plus the **64 BMPs in
  `data/textures`** -- and every name this entry cited as unexplained
  (`creature_atlas`, `detail_atlas`, `particle_atlas`, the rain/snow/storm
  sets, `software_cursor`) IS one of those 64. `data/atlas/` never needed to
  exist and nothing had to be extracted from the pack for provenance; the
  recipe is generate-then-pack, not a missing checked-in directory.
  `scripts/build_willeq_pack.sh` stages the two input sets into a temp
  directory whose subdirectory names ("atlas" before "textures") reproduce the
  shipped entry order under `eqe1_builder`'s global path sort, packs with
  `--name willeq`, and installs by writing a temp file **in the destination
  directory** and `mv`-ing it over the pack -- atomic, so a concurrent slot's
  ctest or gate never reads it half-written. **The very first rebuild was
  byte-identical to the shipped pack** (sha256 `a59af18d...`, 2,810,863 B),
  confirming T279's determinism finding for the ETC1 encoder end-to-end.
  `PackReproducible` (ctest, labels `hygiene;slow`, RUN_SERIAL, ~41 s) rebuilds
  into a temp path and compares against BOTH the pinned
  `data/willeq.eqe1.sha256` and the installed pack, so an input drift and a
  hand-edit of the shared file are each a red; planted fault (one byte of
  `data/textures/tumbleweed.bmp` XORed) FAILS, restored PASSES. (a) closed in
  Leg B -- see the struck B218. One caveat, boarded as B241: the coin lift
  makes the pack depend on an external path, so "reproducible from the tree"
  is now "reproducible from the tree PLUS the official client tree".
  Prov: T398 Review.

- ~~**B240** (C) `tests/goldens/manifest.json` (the `memory-coverage` block,
  the three `eqe1MappedBytes` values) -- **the visual gate's EQE1 memory
  baseline is stale by exactly +239 bytes in all three zones and needs Will's
  bless.** T398 Leg B deliberately grew `/mnt/projects/EverQuest_ETC1/willeq.eqe1`
  from 2,810,863 to 2,811,102 bytes (the four coin sprite records added to the
  raw `ui_atlas.json` entry; only 3 of 67 entries changed, the other 64
  byte-identical). `eqe1MappedBytes` is the mapped size of the pack set, so
  every zone moved by that same 239:

  | zone     | manifest    | measured    | delta |
  |----------|------------:|------------:|------:|
  | qeynos2  | 119,944,431 | 119,944,670 | +239  |
  | gfaydark | 118,610,523 | 118,610,762 | +239  |
  | befallen | 113,095,019 | 113,095,258 | +239  |

  **No image golden moved and no CPU p95 ceiling was missed** -- these three
  metrics are the gate's ONLY failures, and two consecutive gate runs produced
  bit-identical numbers. T389 (slot1) independently measured the same +239 on
  every branch. The block was blessed by Will in chat for T303, whose own note
  records the SAME cause (a rebuilt `willeq.eqe1`) and says the values were
  applied BY HAND, never by `--bless` -- which would also rewrite the T69
  per-zone memory block. **T398 did not touch it.** Closes when Will blesses
  the three numbers above and someone edits them by hand (lines 171/180/189).
  Until then `visual_gate` is the one red in a 4,105-test suite. Prov: T398
  Review Leg B; B230; B218. **T406 ADDS A THIRD AND MUCH LARGER DELTA TO
  THE SAME BLESS (2026-09-06)**: converting the 59 B266 textures to in-band
  RGBA8888 moved `willeq.eqe1` from 2,830,206 to **19,994,910** bytes
  (+17,164,704), so `eqe1MappedBytes` is now stale by **+17,184,047** per zone,
  not +19,343 -- measured qeynos2 137,128,478 / gfaydark 135,794,570 /
  befallen 130,279,066 against manifest 119,944,431 / 118,610,523 /
  113,095,019 (three standalone gate runs, one after each family commit). No
  image golden moved in any of them and all three CPU p95 numbers stayed
  inside their ceilings. T406 did not bless either, and whether the 17.2 MB is
  the right price at all is boarded separately as **B287**. **T400 ADDS A SECOND, LARGER DELTA TO THE SAME
  BLESS (this commit)**: rebuilding the pack for B225 moved `willeq.eqe1` again,
  2,811,102 -> 2,830,206 bytes (**+19,104**), so `eqe1MappedBytes` is now stale
  by **+19,343** per zone, not +239. T400 did NOT bless it either -- same rule,
  Will blesses, someone edits lines 171/180/189 by hand:

  | zone     | manifest    | measured after T400 | delta from manifest |
  |----------|------------:|--------------------:|--------------------:|
  | qeynos2  | 119,944,431 |         119,963,774 |            +19,343  |
  | gfaydark | 118,610,523 |         118,629,866 |            +19,343  |
  | befallen | 113,095,019 |         113,114,362 |            +19,343  |

  No image golden moved and no CPU p95 ceiling was missed in T400's gate run
  either. The cause is the same one this entry already records (a rebuilt
  `willeq.eqe1`) and the +239 and +19,104 are the same bless, not two.

  **WILL RULED 2026-09-06:** BLESSED — applied at the T406 merge with the final measured numbers.~~ **APPLIED at the T406 merge: manifest eqe1MappedBytes = 137,128,478 / 135,794,570 / 130,279,066 (Will's bless 2026-09-06); the standalone gate PASSES on the tip.**
- **B241** (B) `scripts/build_willeq_pack.sh` / `tools/ui_atlas_builder.cpp`
  -- **`willeq.eqe1` is reproducible from the tree PLUS the official client
  tree, not from the tree alone.** B230 closed on a byte-identical rebuild
  whose inputs were entirely checked in; T398 Leg B then made the four coin
  decals come out of
  `/mnt/projects/EverQuestP1999/uifiles/default/window_pieces01.tga`, which
  lives outside the repo. That is a deliberate trade -- copying the oracle's
  pixels into the repo would duplicate licensed art, and every other EQE1 pack
  already reads that tree -- but it means the `PackReproducible` ctest test
  fails on a host without the client tree (it aborts with a named error rather
  than packing a coinless atlas, which is the correct all-or-nothing
  behaviour, but it is still a red rather than a skip). Two ways out, neither
  taken here: make `PackReproducible` SKIP when the uifiles directory is
  absent, the way `test_ui_atlas_pixels` skips without the pack; or pin the
  sha256 of `window_pieces01.tga` alongside the pack's so a silently updated
  client tree is caught as an input drift rather than as a mysterious pack
  mismatch. The second is the stronger fix and neither is urgent -- every
  host that runs this suite today has the tree. Found by T398. Prov: T398
  Review Leg A; B230.

- **B231** (B) `src/client/graphics/weather_effects_controller.cpp`
  `getAmbientLightModifier` -- **how much the OFFICIAL client darkens for
  ACTIVE weather is unmeasured, and fable's number is invented.** Raised by
  T396, which fixed the clear-weather rule (modifier exactly 1.0) and
  deliberately left the active-weather curve alone, per its plan. fable uses
  `1 - storm.maxDarkening * intensity/10` with `maxDarkening = 0.6`, so server
  rain at intensity 9 more than halves the world's ambient (0.460) -- a number
  nobody has compared against the oracle. It is also large enough to dominate
  any live light measurement taken in a rainy zone, which is how it corrupted
  T377 (see the struck B211). Needs an oracle A/B in a lock unit: same zone,
  same spot, forced time, weather on and off, oracle frame beside fable frame,
  T361's method. Until then no vision or ambient measurement may be taken
  without asserting the modifier is 1.000 in the session log. Prov: T396
  Review Leg A; B203; B211.
### Added by T397 (2026-09-05)

  **WILL RULED 2026-09-06:** capture the official client under forced rain and mimic the curve. → the next lock unit (T418). Now class B.
- ~~**B235**~~ **STRUCK (T399, 2026-09-05)** — commits `3128a1fa` (core/app, 12
  sites), `239b51c9` (config/*.json loaders, 14), `2061b4a0` (data/textures,
  8), `76c1371c` (audio + index caches, 5), `35d26d9a` (the rest, 2), tests
  `99a61191`. **41 literal sites routed through `eqt::ResolveDataPath()`,
  including `data/willeq.db` (version check unchanged), both files T397 left
  to T396, and `willeq.json`'s own default.** Eleven silent-or-DEBUG defaults
  became ERROR logs, each for a file that IS shipped and whose absence
  substitutes a DIFFERENT set of values: `constrained_presets.json` (was NO
  log at all), `debug.json` (no log), `hotkeys.json`, `display_settings.json`,
  `eqe1_archives.json`, `detail_objects.json` x2, `tree_wind.json`,
  `weather_effects.json`, `environment_effects.json`, `music_events.json`.
  Left quiet on purpose: `config/zones/*` + `data/config/weather.json`
  (neither directory exists; genuinely optional per-zone overrides) and the
  two index CACHES, whose miss means "rescan". The census is now a TEST --
  `tests/test_data_path_routing.cpp` re-takes it on every ctest run over all
  of `src/client` + `include/client` and fails on a bare literal. The client
  no longer has to be started from the repo root: a headless run from `/tmp`
  produces a log identical to the repo-root run modulo timings, loading the
  same 94 hotkey bindings and the same `willeq.db`. Original entry:
- ~~B235 (B)~~ `src/client/**` — ~40 shipped data files are named by
  CWD-relative literals; the client only works because everything starts it
  from the repo root.** B221 was filed as a one-off; the T397 census shows it
  is the tree-wide convention. Measured with
  `grep -rnE '"(config|data|assets)/[^"]*"' src/client`: `data/willeq.db`
  (`application.cpp`), `config/constrained_presets.json`,
  `config/race_models.json`, `config/debug.json`, `config/hotkeys.json`,
  `config/display_settings.json`, `config/eqe1_archives.json`,
  `config/environment_effects.json`, `config/weather_effects.json`,
  `config/detail_objects.json`, `config/tree_wind.json`,
  `config/music_events.json`, `config/snd_index_cache.json`,
  `config/gfx_index_cache.json`, `config/zones/<zone>/{weather,tree_wind,tree_overrides}.json`,
  `data/item_models.json`, `data/eqe1_texture_index.json`, `data/zone_lines*.json`,
  `data/region_maps`, `data/detail/zones`, and eight `data/textures/*.bmp`
  loads (particle/detail/boids/tumbleweed/storm/rain/snow overlays, the
  software cursor, the spell-particle fallback). `willeq.json` itself defaults
  to the bare name `"willeq.json"` (`include/client/application.h`), so there
  is no configured root to resolve against either. T397 added the one
  resolution -- `eqt::ResolveDataPath()`, `include/client/data_paths.h` -- and
  routed **only** `config/spell_effects.json` through it (B221's closing
  condition). Closes when every literal above goes through it, or a deliberate
  decision records that the client is repo-root-only and the doc says so.
  **Two of these are another unit's territory today**:
  `config/environment_effects.json` and `config/weather_effects.json` sit in
  the weather/ambient files T396 owns, which is why T397 left them. Found by
  T397 Leg A.

- ~~**B245** (B) `config/chat_settings.json`, `config/ui_settings.json` — **two
  shipped config files that NOTHING reads.** Both are git-tracked in `config/`,
  and `CLAUDE.md` documented `chat_settings.json` as live "user settings", but
  the strings `chat_settings` and `ui_settings` appear in **no `.cpp` or `.h`
  under `src/`, `include/` or `tools/`** — only in `README.md`, `CLAUDE.md` and
  `refactor/completed/U_ui_overhaul.md`. Either the chat/UI window state was
  never wired to persistence, or its loader was deleted and the files outlived
  it. Closes when the persistence is wired with a test, or both files and their
  doc lines are deleted. `CLAUDE.md` now says they are dead. Found by T399's
  census (they are the two shipped files the B235 grep could not see, because
  no literal names them).~~ **CLOSED by T402 Leg B** (`refactor/T402_caches_dead_configs.md`): both files DELETED. Provenance measured, not guessed — `git log -S'ui_settings.json' -- src include` names `0f25cb57` "U07c2: Delete WindowManager + all old UI window files (~35K lines)": they are the deleted WindowManager's saved state. What they describe (draggable per-window x/y/w/h + `visible` per resolution, a `globalTheme` colour/layout block, 19 chat `channelFilters`) does not exist in the client that replaced it: `UILayout` computes every panel rect from the screen size, there are no movable windows, and `channelFilters`/`globalTheme`/`blinkIntervalMs`/`maxHistory` appear in NO source at all. The one live concept, `showTimestamps`, is a `ChatPanelState`/`GameModeConfig` field no file feeds. The genuinely missing feature is boarded as **B275**; the deleted bytes stay recoverable at `0f25cb57^`. Pinned by `DataPathRouting.EveryShippedConfigFileIsNamedByCode`, which starts from the config DIRECTORY (the only way to see a file nothing opens) and fails on any shipped `config/*.json` no source names.

- **B275** (B) `include/client/graphics/ui/ui_layout.h` `UILayout`, `include/client/graphics/ui/static_panels.h` `ChatPanelState` — **the client persists NO user settings.** Chat timestamps, panel visibility, chat scrollback size, UI colours and every panel position are computed or defaulted at startup and nothing writes them back: `saveLayout`, `loadLayout` and any preferences writer return zero hits under `src/`, `include/` and `tools/`, and the only two files that ever held such state (`config/chat_settings.json`, `config/ui_settings.json`) were the deleted WindowManager's and are gone (B245). So a player who moves nothing notices nothing, and a player who changes anything loses it at exit. Closes when a preferences file is written and re-read with a test, or when Will rules that a fixed static UI needs no persistence. Found by T402.

- **B280** (A) `src/client/graphics/renderer/irrlicht_renderer.cpp`
  `IrrlichtRenderer::updateTimeOfDay` night ambient (0.08, 0.08, 0.15) --
  **fable's night is 5.6x darker than the official client's AND a different
  colour, and every vision number is a ratio precisely because of it.**
  MEASURED T405 (`review/T405/`) on the first rig with both weather and heading
  pinned -- same server, zone, spot and heading, 700x420+40+40 crop, linear
  light: the oracle's unbuffed night base is (R .01900, G .03333, B .02797),
  grey .02675; fable's is (R .00540, G .00453, B .00445), grey .00478. The
  oracle's night is **5.6x brighter** and **green-dominant**; fable's is nearly
  flat and faintly red-dominant. Two consequences, and the second is the one
  that bites. (1) Every vision comparison in the project since T358 has been a
  buff/no-buff RATIO, so an absolute difference this large has never been
  visible. (2) A grey ratio is a base-weighted average of the three channel
  ratios, weighted by each client's OWN base -- so **fable and the oracle cannot
  report the same grey ratio even when their per-channel ratios agree exactly**,
  which is why B203 closed on per-channel agreement with the grey held against a
  base-corrected target (+1.39% / -1.67%) instead of the oracle's own grey
  (+7.18% / +4.57%).
  `VisionSolvedT405Test.GreyIsBaseWeightedSoTheOracleGreyIsNotATarget` pins that
  arithmetic so it is not re-discovered as a defect. Closes when the night
  ambient itself is solved against the oracle the way the vision floors now
  are. **T408 (2026-09-06) MEASURED THE SHAPE OF THIS DEFICIT AND IT IS
  UNIFORM**: at two spots 10x apart in night brightness, fable's night needs the
  SAME factor (x2.27 lamp-free, x2.09 next to a lamp) to reproduce the oracle's own
  day/night ratio at that spot. That rules out every lamp-shaped explanation --
  B110's baked vertex colours included, measured and refuted -- and says this is a
  single scalar (or a per-channel triple) on the night ambient. It also gives the
  target: `review/T408/logs/final_table.txt`. **It is one job with a re-solve of those floors**, because moving the
  base moves every ratio measured against it -- which is exactly why T405 did
  not attempt it mid-search. Prov: T405 Review §2, §4; B203; B067.

## Added 2026-09-06 by T408 (the baked vertex lighting)

- **B325** (B) `include/client/graphics/zone_shader.h` `kBakedLightMode` /
  `src/client/graphics/slash/renderer_slash_commands.cpp` `/bakedlight` --
  **the zone's baked per-vertex lighting is LOADED and shipped in every zone
  vertex, and the committed composition ignores it.** That is deliberate and
  measured, not an oversight: T408's live A/B (B110) found that every member of
  the composition family moves fable AWAY from the official client on the lamp
  invariant, because the deficit that actually separates them is UNIFORM
  (B280), not lamp-shaped. `kBakedLightOff` is therefore the best fit in the
  family and mode 0 is byte-for-byte the pre-T408 equation. The colours reach
  the GPU, the six shader programs carry the composition, and `/bakedlight
  <mode> [gain] [skyWeight]` is the reachable consumer -- the same shape
  `/shaderdebug` has. Cost: 24 bytes per loaded geometry (the vector member,
  blessed into the three `characterModelsBytes` baselines) plus 145 KB of
  qeynos2 vertex colours. **Closes when B280 is solved and T408's probe is
  re-run** -- moving the night ambient moves every row of T408's table, so the
  composition cannot be re-solved before it -- **or when Will rules that the
  colours should not ship until there is a consumer**, in which case the loader
  and the six shader edits revert together and only the census test stays.
  Prov: T408 Review; `review/T408/README.md` §4.

- **B326** (B) `src/client/graphics/eq/zone_geometry.cpp`
  `ZoneMeshBuilder::buildTexturedMeshFromUploaded` / `buildCompositeMesh` /
  `zone_geometry_atlased.cpp` `buildAtlasedMesh` -- **three of the four zone
  mesh builders have ZERO callers, and T408 wired the baked colours into only
  the one that is reached.** Census over `src/ include/ tests/ tools/
  benchmarks/ docker/`: `buildTexturedMesh` is called from
  `renderer_zone_draw.cpp` (the zone), `renderer_zone_stream.cpp` and
  `door_manager.cpp` (objects and doors) and `variant_loading.cpp` /
  `race_model_loader.cpp` (characters, `flipV=true`); the other three are
  called by nothing outside their own declarations. The atlased builder cannot
  take the baked flag as things stand anyway -- `AtlasShaderCallback` already
  uses `SMaterial::MaterialTypeParam` for the atlas page index and
  `MaterialTypeParam2` for the alpha page, which is the field T408 uses as the
  flag on the main callback. Closes when the three are deleted, or wired and
  reached with a test. Found by T408 while looking for "the three mesh
  builders" the plan named -- the plan named the ENTITY builders
  (`entity_mesh_build.cpp`, `entity_build_step.cpp`), which must NOT get the
  zone's baked light: they draw character models, and character WLD meshes
  carry no colour array at all (T408 measured `colorCount == 0` for every one,
  which is why the memory baselines moved by exactly one empty vector header
  per geometry). Prov: T408 Review §5.

- **B281** (A) `src/client/graphics/renderer/irrlicht_renderer.cpp` /
  `SkyRenderer::updateTimeOfDay` -- **fable's night frame gets steadily
  brighter as the world clock runs, with no buff, no weather and no camera
  movement; the official client's does not.** MEASURED T405. A probe session
  whose clock was set once with `#set time 22 0` and then left alone drifted its
  unbuffed base **x1.68** in about five minutes (grey .00432 -> .00952, a smooth
  continuous ramp), and its own two night stills differ by RMSE **7.46%** -- the
  session was discarded and re-run, costing the unit a probe. Even the
  well-behaved sessions drift 3-5% across a measurement window. **The oracle,
  measured in the same zone at the same spot on the same server in the same
  hours, does not drift**: its control reads 0.996-1.003 over seven minutes.
  Ruled out: weather (proven 1.000 throughout, zero modifier lines in the log)
  and the vision floors (the drifting frames are unbuffed and Testchar is race 1
  -> Normal, so no floor applies). The suspect is a celestial/moon term feeding
  light into the scene while the day/night ambient curve is FLAT across the
  20:00-05:00 arm -- but that is a suspect, not a finding; it was not bisected.
  T405 worked around it by re-pinning the clock with `#set time 22 0`
  immediately before each of its five measurement windows
  (`review/T405/scripts/t405_fable_vision.sh`), which is a rig workaround and
  not a fix -- **any future night measurement that does not do this is
  contaminated.** Closes when the drift is root-caused and either removed or
  shown to match the oracle. Prov: T405 Review §1d.


- ~~**B246** (B) `src/client/graphics/eq/race_codes.cpp` `loadRaceMappings` —
  **the function ignores its argument; `config/race_models.json` is validated
  at startup and read by nothing.** The body is
  `(void)jsonPath; s_mappingsLoaded = (WilleqDatabase::globalInstance() != nullptr);`
  — race data has come from SQLite since the db landed. Yet
  `Application::initRequiredFileValidation` (S03) still `stat`s
  `config/race_models.json` and refuses to start without it, and
  `renderer_init.cpp` still passes the literal and LOG_FATALs on a false
  return that only ever means "no database". So a shipped JSON file is a hard
  startup requirement for a loader that never opens it. Closes when the
  parameter and the S03 check go (or the JSON is genuinely read again).
  T399 routed the literal through `ResolveDataPath()` rather than change
  behaviour mid-unit. Found by T399.~~ **CLOSED by T402 Leg C** (`refactor/T402_caches_dead_configs.md`): the parameter is gone (`bool loadRaceMappings()`), the S03 `checkFile` line for `config/race_models.json` is gone, and `renderer_init`'s fatal now says `Race model mappings unavailable: no willeq.db is open` — the only condition a false return has ever meant. **`config/race_models.json` is NOT deleted and that is not a STOP:** the census (`src/ include/ tests/ tools/ benchmarks/ scripts/ docker/`) shows it is the DB BUILDER's input — `tools/willeq_db_builder.cpp:1353` `loadRaceModels(db, configPath + "/race_models.json")` builds the `race_models` table from it, and `scripts/generate_race_colors.py`, `scripts/update_race_models_from_reference.py` and `scripts/generate_s3d_reference_json.py` read it too. It is build-time source, not a runtime file; only the RUNTIME requirement was wrong. Proven live: with `config/race_models.json` moved away the client starts, initialises graphics and logs `Race mappings using on-demand SQLite lookup`; with `data/willeq.db` moved away it fails naming the db (`failed to open data/willeq.db ... rc=14`, and in fact dies at the spell database before the renderer's check is reached). Pinned by `RaceNameNoDatabaseTest.RaceMappingsFailOnTheDatabaseAndNothingElse`, `RaceNameFromDbTest.RaceMappingsBindToTheDatabase` and the G04 init-order golden, which learned the deletion of entry 46 in place (correction 32).

- ~~**B260** (P) `tests/goldens/manifest.json` / `scripts/ci/visual_gate.sh` —
  **the visual gate is RED on the `fable` tip itself: T398 Leg B grew the
  shared `willeq.eqe1` by 239 bytes and the Will-blessed `eqe1MappedBytes`
  baselines were not re-blessed.** Measured by T399 (2026-09-05) standalone on
  `build-gles2` built from its own tip: `memory-coverage qeynos2` expected
  119,944,431 got 119,944,670; `gfaydark` 118,610,523 -> 118,610,762;
  `befallen` 113,095,019 -> 113,095,258 — **+239 in all three, the signature of
  one shared file every zone maps, not of anything zone-dependent.** Every
  other manifest field matches and all three CPU p95 numbers are inside their
  ceilings. The pack is `/mnt/projects/EverQuest_ETC1/willeq.eqe1`, the only
  file in that tree modified on 2026-09-05 (21:20), and T398's Leg B
  (`23378528`, merged `3f328966`) is what put the four coin decals in it;
  `tests/goldens/manifest.json` has not changed since T303 (`a32600a4`, "bless
  the T80 memory-coverage baselines (Will)"). **Only Will re-blesses**, so
  T399 did not. Until then every unit that touches graphics will report this
  same gate FAIL and have to re-derive the attribution. Found by T399.~~ **DUPLICATE of B240 (T398 boarded the same 239-byte delta first) — struck at the T399 merge; the bless request stands under B240.**
- ~~**B248** (B) `config/snd_index_cache.json`, `config/gfx_index_cache.json` —
  **two generated caches are git-tracked, and every client run rewrites them,
  so a plain `willeq` run dirties the working tree.** T399 measured it: a 40 s
  headless run (`--no-graphics`) left `config/snd_index_cache.json` modified
  (same 17 archives, same 2,088 index entries, same `eqPath` — the content is
  equivalent, only the serialisation differs), and it had to be restored by
  hand before committing. That is a trap for every executor: an unrelated
  modified file appears in `git status` after any run, and a careless
  `git add -A` commits it. Either both files leave the index and join
  `.gitignore` (they are caches; a miss just triggers a rescan, which is why
  T399 deliberately left their miss silent), or the writer becomes
  content-stable and only rewrites on a real change. Found by T399.~~ **CLOSED by T402 Leg A** (`refactor/T402_caches_dead_configs.md`): both files left the index (`git rm --cached` + `.gitignore`) and the client now resolves them through `eqt::ResolveCachePathForRead()` / `ResolveCachePathForWrite()` (`include/client/data_paths.h`), which pick the first WRITABLE of `$WILLEQ_CACHE_DIR`, `$WILLEQ_DATA_ROOT/cache`, `<exe dir>/cache` and `cache/` — none of them inside the checkout. Keeping them as shipped seeds was rejected: each is keyed on ONE absolute `eqPath` (`/mnt/projects/EverQuestP1999`), so the seed is worthless to any other tree, and a stale seed would have to be re-read AND re-written on every run. Cold-rescan cost measured instead: **~0.4 s** for the 17 sound archives (613 ms vs 221 ms for `test_pfs_sound_load` end to end) and **~30 ms** for the 378 `*_chr.s3d` (cold 31-57 ms vs 12 ms warm). Pinned by `PfsIndexCacheLocation.RescanWritesOutsideTheRepoConfigDir` (an alias EQ tree of symlinks forces the rescan; nothing under `config/` may appear, vanish or change), `DataPathRouting.IndexCachesAreNotWrittenUnderConfig` and three `CachePaths.*` tests.

### Struck / amended by T386

- **B172** — its owed live proof is **TAKEN and SPLIT**. The banking and the
  chat line are **PROVEN live, two independent trials** (`review/T386/` §1a):
  `You receive 1 platinum, 9 gold, 2 silver and 3 copper from the corpse.` with
  `character_currency` 4/6/1/8 -> 5/15/3/11, and
  `You receive 3 platinum, 3 gold, 5 silver and 5 copper from the corpse.` with
  5/15/3/11 -> 8/18/8/16 — both deltas exactly the four printed counts. The
  SOUND half is **NOT** proven and is now **B200** (**root-caused and fixed
  T392 `1311b50a` -- a trailing CR on every `sound_assets` filename in
  `willeq.db`; the LIVE EAR for id 138 is still OWED**); "watch the purse move" turns
  out to be impossible on fable's screen and is now **B201**. The official
  client's **zero-denomination** form is **still UNMEASURED**: T386's oracle
  attempt is recorded in `review/T386/` §1d together with the method that will
  work (eqemu splits the loot table's copper by digit division, `/mnt/projects/akk-stack/code/zone/loot.cpp`,
  so ~1 kill in 4 of `Gargalek_Daemado` has a zero) and the trap that killed it
  (a level-1 character takes falling damage from `#goto`'s Z in `arena` and gets
  stuck at HP 0 answering `You may not use that command while dead.`; use a
  levelled character, the zone's own safe Z 51, and `#set invulnerable on`
  BEFORE the first `#goto`).

- **B170** — **PROVEN live** (`review/T386/` §2). `Alt+P`, `Alt+F1` and `Alt+B`
  each toggle their window (six `Group window opened/closed`, `Pet window …`,
  `Buff window …` lines against six `OnEvent: action Toggle…` lines); bare `G`
  and bare `P` produce no input event and no window line. The empty group window
  is a bare title strip on both clients — crops
  `review/T386/oracle/T386_oracle_group_window_nogroup_CROP.png` and
  `review/T386/fable/T386_fable_group_window_nogroup_CROP.png`. **A trap worth
  keeping:** the first run pressed all eight keys with the chat field focused
  (`INPUT-TRACE … chatFocused=true`) and every renderer-local action was
  silently dropped — `Alt+F1` still registered because `F1` is not a text
  character, while `Alt+P`, `G` and `P` were typed into the chat box. Send
  `Escape` before each key.

- **B171** — its provisional strike **is reversed by T394 (`7cd6c124`)**:
  T383's constants are gone from the tree and the defect B171 names (the
  per-channel gains are wrong against the oracle) is live again, tracked as
  **B203**. Do not re-strike it from a desk fit.

- **B160** — its owed live pair is **NOT PROVEN** and the reason is bigger than
  the colours; see **B204** (nothing was drawn -- **the two causes were found
  and fixed by T395**, so the live pair is now worth re-attempting). **B202
  (the two-client session segfaults) is STRUCK by T391 (`e332162f`), so that
  half of the block is gone** -- a two-client session now survives 5 minutes --
  but neither T391 nor T395 had the budget to take the fire-vs-frost observer
  pair, so B160's proof is still owed. **T389 (2026-09-05) did not take it
  either** -- seven official-client sessions took that session's budget and
  both fable runs were single-client -- and it found the reason the pair would
  not have shown anything yet: see **B226** (fable's impact burst is 36
  particles of 0.05-0.15 world units for 0.4-0.8 s and is not resolvable in a
  3.22 fps capture, against the oracle's hundreds of sprites over a third of
  the screen for 2-3 s). T389 did measure the ORACLE half of the same
  question: Burst of Flame is R-dominant (.360/.379/.199) and Frost Bolt is
  B-dominant (.213/.284/.315) on the official client, so the two colours are
  real there. Take the observer pair after B226.

### Added by T400 (2026-09-05)

- ~~**B260** (B) `src/client/graphics/texture_generators.cpp` /
  `data/textures/` -- **the 926-line TU that DEFINES every procedural texture
  the client ships was in no CMake target at all, and 63 of its 64 outputs
  still have no build recipe.** Found by T400 while fixing B225: a
  tree-wide grep for `texture_generators` in every `CMakeLists.txt`/`*.cmake`
  returned NOTHING. P26 retired the `generate_textures` tool (its only
  consumer) on the recorded rationale "no found consumer of its output
  anywhere in the tree or docs" -- and `tools/retired/README.md` still says
  that. **That rationale is now known false**: T398 established that the 64
  BMPs in `data/textures/` are exactly two thirds of `willeq.eqe1`'s 67
  entries, so the generator's output is a shipped asset read by every zone
  load. The consequence was B225 -- nobody could regenerate the particle
  atlas, so the alpha-less file kept shipping for a year with the code that
  proves it wrong compiling nowhere. T400 gave the TU ONE consumer
  (`tools/particle_atlas_builder`) and one pinned output (the particle
  atlas, `test_particle_atlas_alpha` asserts the shipped file matches
  `generateParticleAtlas()` texel for texel). The other 63 BMPs
  (`creature_atlas`, `detail_atlas`, the rain/snow/storm sets,
  `software_cursor`, `tumbleweed`, ...) are still checked-in binaries with
  no proof they match their generator and no way to rebuild them. Closes
  when either (a) the retired tool is restored as a target and its full
  output is shown to reproduce `data/textures/` byte-for-byte (then the
  directory is derived, not hand-carried), or (b) each remaining generator
  is deleted as dead code on a deliberate ruling that the BMPs are the
  source of truth. Do NOT half-do it: a generator that compiles but whose
  output nobody compares is exactly the state that produced B225. The
  `tools/retired/README.md` paragraph needs correcting whichever way it
  goes. Prov: T400 Review.~~ **CLOSED by T403 (2026-09-06, `9122297d`) via route (a).**
  `tools/generate_textures.cpp` is un-retired as a live CMake target and
  `scripts/build_textures.sh --check` (ctest `TexturesReproducible`)
  regenerates all 64 into a temp dir on every run: **63 of 64 come out
  byte-for-byte identical to the shipped file**, so `data/textures/` is now
  DERIVED. The 64th, `detail_atlas.bmp`, differs in PIXELS and was left
  alone per the plan -> **B265**. The 24-bit alpha drop turned out to affect
  all 63, not just the particle atlas -> **B266**.
  `tools/retired/README.md` corrected (and the stale `B247` citation in it
  fixed to B260); CLAUDE.md gained a shipped-data recipe section.

- ~~**B261** (B) `tools/eqe1_builder.cpp` / `docker/irrlicht-drm/COGLES2Shaders.cpp`
  -- **the ETC1 `_alpha` companion is INVISIBLE to any shader with a single
  sampler, and nothing warns you.** The default ETC1 encode stores a
  texture's alpha in a separate `<name>_alpha` pack entry, uploaded to
  texture unit 1 and sampled only by the alpha-TEST variants
  (`ATLAS_ALPHA3D_FS` / `ALPHA3D_FS`, which `discard` below a threshold).
  Every other built-in shader -- `SOLID3D_FS`, and every hand-rolled GLES2
  program such as `UnifiedParticleRenderer`'s point sprite -- reads
  `texture2D(uTexture, uv).a`, which for an ETC1 (RGB-only) texture is
  **1.0 everywhere**. So a texture can be `hasAlpha=1` in the pack, have a
  perfectly good companion, and still draw fully opaque, with no log line
  and no failing test. That is half of B225 and it cost T400 the larger
  half of its investigation. T400 fixed the one texture it owned
  (`--rgba8888-texture particle_atlas`) but added no general guard. Closes
  when either the builder refuses/warns for a texture whose consumers are
  single-sampler, or -- better and cheap -- a ctest asserts the invariant
  the other way round: for each pack entry with `hasAlpha=1` and an ETC1
  payload, SOME material in the tree binds its companion to unit 1. Related
  and NOT the same thing: an alpha-blend material with a companion has no
  shader at all (there is no blended dual-sampler variant), so "bind the
  companion" is not a fix for a blended draw -- in-band alpha is. Prov:
  T400 Review.~~  **CLOSED by T411 (2026-09-06)**, the second way round: the invariant is now enforced by CONSTRUCTION rather than by a warning.
  `COGLES2Texture` carries its own companion (`setAlphaCompanion()`, recorded
  once by `ConstrainedTextureCache::uploadCompressedETC1`), the driver binds
  texture unit 1 from the unit-0 texture in ONE place
  (`COpenGLES2Driver::bindAlphaCompanionUnit()`, used by the 3D material path
  AND both `draw2DImage` overloads, which bound NOTHING on unit 1 before), and
  the three alpha-BLEND programs -- `SOLID3D_FS`, `UI2D_FS` and
  `UnifiedParticleRenderer`'s `POINT_SPRITE_FS` -- do
  `texColor.a *= texture2D(uAlphaTexture, uv).r`. The multiply needs no
  `hasAlpha` uniform: with no companion the driver binds a 1x1 opaque-white
  texture, so it is an exact no-op for in-band alpha, and for an ETC1 payload
  `texColor.a` is 1.0, so it is exactly the companion. Pinned in PIXELS by
  `tests/test_alpha_blend_companion.cpp` (6 cases through a real headless
  GLES2 device; the 3 companion cases read (255,255,255) and FAIL with the
  three multiply lines removed, the 3 no-companion controls stay green either
  way). The note "an alpha-blend material with a companion has no shader at
  all, so in-band alpha is the only fix" was the wrong conclusion -- giving
  the blend shaders the sampler was ~10 lines of GLSL and returned 14.7 MB of
  pack (see the struck B287).

### Added by T403 (2026-09-06)

- **B265** (B) `data/textures/detail_atlas.bmp` /
  `src/client/graphics/detail/detail_texture_atlas.cpp` -- **the shipped
  detail atlas is the ONLY one of the 64 BMPs that its generator does not
  reproduce, and it is wrong in two independent ways.** T403 regenerated all
  64 through the un-retired `tools/generate_textures`; 63 matched byte for
  byte, this one did not (same 786,486 bytes, identical 54-byte header,
  **29,509 of 262,144 pixels differ**). Two causes, measured separately:
  (a) **5-bit quantisation.** Every one of the shipped file's 786,432 pixel
  bytes is a multiple of 8 -- it was round-tripped through an `A1R5G5B5`
  (16-bit) texture. The retired tool asked for an `EDT_SOFTWARE` device and
  wrote `createImage(createAtlas(driver))`, i.e. image -> software texture ->
  image, and Irrlicht's software driver stores textures at 16 bpp. 18,913
  pixels differ by exactly that truncation and nothing else.
  (b) **Code drift in the twelve footprint tiles** (rows 5-6, `AtlasTile::
  FootprintLeft/Right{Grass,Dirt,Sand,Snow,Swamp,Jungle}`): 883 pixels in
  each of the twelve, identical counts, and the shipped pixels are near-black
  (BGR values 0..24) where `drawFootprintLeft/Right` now draws the per-biome
  colour it is passed (e.g. `(25,35,40)..(54,..)` for grass). The BMP predates
  the biome-coloured footprints. 10,596 pixels total.
  T403 did NOT regenerate it: `detail_atlas` is drawn by every outdoor zone
  the visual gate covers, so moving its pixels is a golden move and the plan's
  STOP condition. It is hash-pinned in `data/textures.sha256` and named in
  `scripts/build_textures.sh`'s `NOT_REPRODUCIBLE` list, so the file cannot
  drift further unnoticed, but the tree is not its source of truth. Closes when
  someone regenerates it deliberately (dropping the software-driver round trip
  -- `DetailTextureAtlas::createAtlasImage()` exists now and needs no driver
  textures), reblesses whatever gate images move, and rebuilds the pack.
  Prov: T403 Review §2.

- ~~**B266** (A) `data/textures/*.bmp` (63 files) / `tools/generate_textures.cpp`
  -- **B225 was not one file. Every generator except the particle atlas draws
  a real alpha channel, and every one of those 63 BMPs ships 24-bit with that
  alpha thrown away.** Measured by `generate_textures --report`, which prints
  the alpha histogram of each image BEFORE it is written: `software_cursor`
  (an 8x10 arrow on a transparent ground) has 2 alpha levels min 0 max 255;
  `tumbleweed` 2 levels min 0 max 255; `creature_atlas` 23 levels; every rain
  texture 64+ levels min 0 max ~219; every snow texture 64+ levels min 0;
  `spell_particle_fallback` (a cosine-falloff soft circle) 64+ levels min 0
  max 254; the 32 `storm_cloud_blend_*` frames 56-64+ levels with max ~90-107,
  i.e. the clouds are meant to be **mostly transparent** and ship fully
  opaque. Irrlicht's `CImageWriterBMP` emits 24 bpp and drops alpha with no
  diagnostic; `ConstrainedTextureCache::decodeBMP()` then substitutes
  `A=255`, and in the pack ETC1 is RGB-only anyway (B261). Live consumers
  confirmed: `SpellVisualFx` (`spell_particle_fallback`), `DetailManager`
  (`detail_atlas`), `BoidsManager` (`creature_atlas`), plus the weather and
  cursor paths. This is the same defect as B225 with 63 siblings, and it is
  now measurable in one command because the generator finally compiles.
  **Not fixed in T403**, deliberately: the fix rewrites 63 shipped BMPs (they
  grow ~33%), rebuilds `willeq.eqe1`, and needs a per-texture decision about
  in-band RGBA8888 vs an ETC1 `_alpha` companion the consuming shader
  actually samples (B261 again) -- and the plan's rule was to change no
  shipped BMP. Closes when the 63 are converted through
  `tools/bmp32_writer.h`, their consumers are checked against B261, the pack
  is rebuilt, and the gate is re-blessed. Prov: T403 Review §3.~~  **CLOSED by T406 (2026-09-06)** in three commits, one per consumer
  family: `75de2288` weather (the 32 `storm_cloud_blend_*`, the ten rain
  intensities + `rain_layer_mid/far`, the ten snow intensities +
  `snow_layer_mid/far` -- 56 files), `d5dd61fc` `software_cursor.bmp`, and
  `93429d28` `creature_atlas.bmp` + `tumbleweed.bmp`. **59 files converted,
  not 63.** The other four are recorded with their reason and pinned as
  24-bit: `spell_particle_fallback.bmp` (its only consumer is an
  `EMT_TRANSPARENT_ADD_COLOR` particle system, i.e. `glBlendFunc(GL_ONE,
  GL_ONE)`, and `SOLID3D_FS` does not multiply RGB by alpha, so the alpha is
  provably never sampled -> new **B286**), `detail_atlas.bmp` (B265: no build
  reproduces it, so a 32-bit rewrite is a pixel move in every outdoor gate
  zone), and `rain_streaks.bmp` + `snow_flakes.bmp` (**no consumer at all** --
  `RainOverlay`/`SnowOverlay` name neither). Every converted consumer was
  checked against B261 and not one is alpha-TEST: the storm dome and the two
  billboard sets are `EMT_TRANSPARENT_ALPHA_CHANNEL` -> `SOLID3D_FS`, and the
  rain/snow/cursor draws are `draw2DImage(..., useAlphaChannelOfTexture=true)`
  -> the single-sampler `UI2D` program. All 59 therefore take the in-band
  `--rgba8888-texture` override, which `scripts/build_willeq_pack.sh` now
  builds as a list. Pinned by `tests/test_texture_alpha_census.cpp` (14 cases,
  3 failing per family before its fix), whose
  `EveryShippedBmpIsAccountedFor` forces every future generator into one list
  or the other. **The gate was NOT re-blessed** (that is B240's, and the cost
  is now large enough to be its own question -> new **B287**).

- **B267** (B) `tools/particle_atlas_builder.cpp` -- **a 118-line target that
  is now a strict subset of another one.** T400 created it to regenerate the
  single file `data/textures/particle_atlas.bmp`; T403's `generate_textures`
  writes all 64 including that one, through the same `tools/bmp32_writer.h`,
  and `generate_textures --only particle_atlas.bmp` is the exact equivalent
  (verified: `TexturesReproducible` regenerates the shipped 32-bit atlas
  byte-identical). Two producers for one asset is the duplicated-then-drifted
  pattern; the shipped file would follow whichever one someone happened to
  run. Kept in T403 only because `tests/test_particle_atlas_alpha.cpp`'s
  failure message and T400's Review name it by hand. Closes when the target
  and source are deleted and that test's message points at
  `generate_textures` instead -- or on a ruling that the narrow tool earns its
  keep. Prov: T403 Review §5.

- ~~**B268** (A) `src/client/graphics/texture_generators.cpp`
  `generateParticleAtlas()`'s `pseudoNoise` and `noise2D()` -- **the bytes of
  a SHIPPED asset depended on `CMAKE_BUILD_TYPE`.** Both hashes ran
  `n * (n * n * 15731 + 789221) + 1376312589` in signed `int`, which
  overflows -- undefined behaviour -- and GCC folded it differently at -O0
  and at -O2. Measured: `tools/generate_textures` built in slot1's default
  tree (no `CMAKE_BUILD_TYPE`, -O0) and in a `RelWithDebInfo` tree (-O2 -g
  -DNDEBUG) from the SAME source produced particle atlases differing in
  **157 of 4,096 texels, all in the ALPHA channel, all inside tile 11 (the
  snow patch, the only `pseudoNoise` consumer)**, alpha deltas up to 80. The
  -O0 build matched the shipped file; the -O2 build did not, which is why
  T400's `ParticleAtlasAlpha.ShippedFileMatchesTheGeneratorPixelForPixel`
  passed in a slot and failed on the merged tip against the identical BMP.
  `noise2D()` (the storm-cloud Perlin hash, 32 shipped frames) carried the
  same UB and was only accidentally stable.~~ **FIXED and CLOSED by T403
  (2026-09-06, `9122297d`):** both hashes moved to `uint32_t`, whose wrap is
  defined and reproduces the shipped values at every optimisation level.
  Proof: the tool built at -O0 and at -O2 (`build-rwdi`,
  `-DCMAKE_BUILD_TYPE=RelWithDebInfo`) now writes all 64 BMPs **bit-identical
  to each other**, and 63 of 64 bit-identical to `data/textures/` (the 64th
  is B265's `detail_atlas.bmp`, which no build reproduces);
  `test_particle_atlas_alpha` is 5/5 green built RelWithDebInfo. No shipped
  byte changed. Prov: T403 Review §4.

### Added by T389 (2026-09-05)

- ~~**B225** (A) `data/textures/particle_atlas.bmp` -- **the shipped particle
  atlas has NO alpha channel, so every particle in the game is drawn as a
  flat, fully opaque coloured square.** The file is a 24-bit `BMP3`, 64x64,
  and every one of the 256 pixels in tile 0 is `255,255,255` (checked with
  PIL: `len(np.unique(...)) == 1` over the tile). `generateParticleAtlas()`
  in `src/client/graphics/texture_generators.cpp` builds the sixteen shapes
  entirely in ALPHA -- `drawCircle`/`drawStar`/`drawSpellSparkle` write a
  constant RGB and vary only `pixelColor.setAlpha(...)` -- and
  the retired generator's `writeImage(..., "particle_atlas.bmp")`
  drops it (that tool is no longer under `tools/retired/` -- T403 un-retired
  it as `tools/generate_textures.cpp`). `ParticleManager::loadParticleAtlas()` loads that file and
  `particleMaterial_.MaterialType = EMT_TRANSPARENT_ALPHA_CHANNEL`, so the
  blend reads alpha 255 everywhere: SoftCircle, Snowflake, SpellSparkle and
  IceCrystal are indistinguishable opaque squares that differ only in the
  flat RGB the generator happened to pick. **Consequence for the spell work:
  a sprite-name -> tile mapping (B162, T389 Leg B) cannot change the shape of
  anything on screen today, only its tint** -- and it bounds what any spell
  particle can ever look like. Closes when the atlas is stored in a format
  that carries alpha (PNG, or a BMP32/`ECF_A8R8G8B8` writer) and a test
  asserts a non-trivial alpha histogram for at least two tiles. Not fixed by
  T389: it is a texture-pipeline change with a golden-image blast radius, not
  part of a measurement unit. Prov: T389 Review S6.~~ **STRUCK T400 (this commit)**: closed on both halves, and
  the second half was NOT in this entry. (a) The file. `data/textures/particle_atlas.bmp`
  is now a 32-bit BMP (BITMAPV4HEADER, BI_BITFIELDS, top-down; 12,342 -> 16,506 bytes)
  written by the new `tools/particle_atlas_builder`, which calls
  `generateParticleAtlas()` on an `EDT_NULL` device and encodes through the new
  shared `tools/bmp32_writer.h` -- the same layout `ui_atlas_builder` has always
  used, and the reason `ui_atlas` had alpha in the pack while `particle_atlas` did
  not. The retired tool's `writeImageToFile(img, "*.bmp")` was the whole defect:
  Irrlicht's `CImageWriterBMP` emits 24 bpp and drops alpha silently. RGB is
  UNCHANGED and stays straight (non-premultiplied) -- the material alpha-BLENDS
  (`EMT_TRANSPARENT_ALPHA_CHANNEL` -> `glBlendFunc(GL_SRC_ALPHA,
  GL_ONE_MINUS_SRC_ALPHA)`), not adds, and a constant per-tile RGB is what
  survives bilinear/mip interpolation without darkening its own edges.
  (b) **The pack, which this entry did not mention and which alone would have
  left every particle opaque anyway.** In `willeq.eqe1` the entry was ETC1, and
  ETC1 is RGB-ONLY: `eqe1_builder` puts the alpha in a separate
  `<name>_alpha` companion that ONLY the alpha-TEST shader variants sample
  (`ATLAS_ALPHA3D_FS`, texture unit 1). Both particle draw paths sample ONE
  texture and read `texColor.a` from it -- `ParticleManager`'s
  `EMT_TRANSPARENT_ALPHA_CHANNEL` material is routed to `SOLID3D_FS`, and
  `UnifiedParticleRenderer`'s point-sprite shader has a single `uTexture` --
  so a companion could never have been read. Fixed by a per-entry payload
  override the format was already designed for (`eqe1_format.h`: "v2 packs may
  mix payload formats per-entry"): `eqe1_builder --rgba8888-texture
  particle_atlas`, wired into `scripts/build_willeq_pack.sh`. Measured pack
  delta: 2,811,102 -> 2,830,206 B (+19,104); **exactly ONE of the 67 entries
  changed** (`particle_atlas`, payload 0->3, hasAlpha 0->1, 2,744 -> 21,844 B),
  the other 66 byte-identical; the file version moved 1 -> 2 and no companion
  entry was added. `data/willeq.eqe1.sha256` updated in the same commit;
  `PackReproducible` PASSES on the new pack. Pinned by
  `test_particle_atlas_alpha` (5 cases: 4 RED before, all GREEN after).
  Consequence for B162/B226: a sprite-name -> tile mapping can now change the
  SHAPE on screen, not only the tint. Prov: T400 Review.

- **B226** (B) `config/spell_effects.json` `styles.impact` /
  `styles.spellComplete` -- **fable's impact burst is two orders of magnitude
  smaller than the oracle's and is not visible in a capture.** MEASURED T389:
  Testcaster cast Frost Bolt six times in `qeynos2` with GM invulnerability
  off on a binary carrying T395's B204 fixes; all six reached the worker
  (`ParticleManager: Enqueued CreateSpellEffect 'Impact'` +
  `SimWorker: Created spell effect 'Impact'` x6, so T395's chain is confirmed
  live), and across **607 dumped frames at a measured 3.22 fps** the changed-
  pixel count in a 420x280 crop of the 3D scene around the caster never left
  its 300-1,400 idle-animation band; the one larger excursion is nameplate
  and chat text. The style is **36 particles of `sizeStartMin` 0.05 to
  `sizeEndMax` 0.15 world units, `emitterLifetime` 0.1 s, particle lifetime
  0.4-0.8 s**. The oracle paints hundreds of sprites over roughly a third of
  the screen for 2.0-3.3 s (T389 Review S2). Closes when a landed spell
  produces a burst an observer can see and a capture can resolve.
  **T401 (`4ff78bb4`, `review/T401/` §3) RESOLVED THAT MUCH: "not visible in
  a capture" is no longer true.** On an OBSERVING client (T389 measured the
  caster's own screen) a landed Burst of Flame moves **442 pixels** and a
  Frost Bolt **438** in a 180x160 crop, in unambiguous orange and cyan, at
  4.92 measured fps — plainly visible at 4x and visible unaided in the full
  800x600 frame. **The size question is now Will's**: it is resolvable but
  still far smaller than the oracle's third-of-a-screen burst, and the
  changed-pixel COUNT does not discriminate it from the idle animation (389
  pixels) — only the colour does. The binary carried NO T400 (the
  particle-atlas alpha, B225, was planned and unmerged at that tip). Frame pair:
  `review/T389/fable/T389_fable_54_impact_crop4x.png` versus
  `review/T389/oracle/T389_oracle_54_self_full_1024x768.png`. This is the
  live half of **B204**'s proof and the reason **B160**'s observer pair is
  still not worth taking. Prov: T389 Review S7. **ANNOTATED T400
  (this commit)**: the blocking precondition is gone. B225 is struck -- the
  atlas now carries its alpha on BOTH the raw-BMP and the EQE1 paths -- so an
  impact burst can finally draw sixteen distinguishable SHAPES instead of
  sixteen identical opaque squares. That does NOT resize the burst: this entry
  is still open on its own numbers (36 particles of 0.05-0.15 world units for
  0.4-0.8 s, against the oracle's hundreds of sprites over a third of the
  screen for 2-3 s). T400 deliberately did NOT retune `styles.impact` --
  changing the sizes and the sprite shapes in one unit would make the next
  capture pair unattributable. Retune it in its own unit, then take the
  capture; that unit now has a real atlas under it.

- **B227** (B) the projectile question (does the official client draw a Frost
  Bolt travelling between caster and target, and over how many frames) is
  **still unmeasured after four attempts** -- T389 could not frame one. The
  four failure modes, so the next unit does not repeat them: (1) a GM
  `#castspell` calls eqemu's `SpellFinished`, so there is no cast bar and no
  travel stage at all -- the Middle stage needs a REAL gem cast
  (`#scribespells N`, `#memspell <spell> <gem>`, `/cast <gem>`); (2) a hostile
  target closes to melee within seconds and melee **interrupts** a gem cast;
  (3) a heading written to `character_data` does not take on zone-in -- use
  eqemu's `#goto [x y z] [h]` (`/mnt/projects/akk-stack/code/zone/gm_commands/goto.cpp`), and note the
  convention T389 derived and confirmed: **`world_angle_atan2 =
  heading * (360/512) + 90`, +X north, +Y east**, so `h=128` faces -X (south)
  and `h=384` faces +X (north); (4) backpedalling to make distance walked the
  character out of `qeynos2` into `qeynos`. The recipe that should work: a
  real gem cast at an un-aggroed target ~90 units away, placed with
  `#goto x y z h`, fired immediately with no intervening screenshots. This
  blocks the travel-gate half of **B165** and the wire-or-delete decision in
  **B161**. Prov: T389 Review S5.

- **B228** (P) `tests/goldens/manifest.json` memory-coverage `eqe1MappedBytes`
  -- the visual gate is **RED at this tip for a reason outside any checkout**:
  `/mnt/projects/EverQuest_ETC1/willeq.eqe1`, the composite sheet, was
  rewritten on the shared NFS asset directory at **2026-09-05 21:20** and grew
  by **239 bytes**, so all three memory-coverage zones miss by exactly that
  amount (qeynos2 expected 119944431 got 119944670; gfaydark 118610523 ->
  118610762; befallen 113095019 -> 113095258). Every other gate check passes:
  the golden images match, CPU p95 is under ceiling in all three zones, and
  the per-zone budget block is clean. This is the SAME class of drift the
  manifest's own T303 note already records ("the +1,040 byte eqe1MappedBytes
  and +183 byte characterModelsBytes drift ... was traced to willeq.eqe1, the
  composite sheet rebuilt by T284/T287"), and it hits every branch equally --
  T389 changed only four source files, none of which can alter the mapped
  size of an archive on NFS. **A re-bless is Will's call and an executor must
  never self-bless**, so T389 left the goldens untouched and reported the
  gate as FAIL. Closes when the three values are re-blessed against the
  current archive, or the sheet is pinned so a rebuild is bit-identical --
  which is the better fix, because a shared asset that changes under three
  worktrees is the same hazard class as the git stash and the shared
  scratchpad. Prov: T389 Review S9.

## Added 2026-09-06 by T401 (owed live proofs 7, tip `4ff78bb4`)

- ~~**B250** (A) `src/client/audio/audio_manager.cpp` `loadSoundDataFromPfs` /
  `playSoundInternal` -- **every sound fable loads out of a `snd*.pfs` archive
  plays at exactly DOUBLE rate: an octave high and half as long.** MEASURED
  T401 (`review/T401/` §1d) on a bit-exact ALSA-loopback recording of a live
  session, matched against the archives' own bytes by normalised
  cross-correlation over a playback-rate scan of 0.5..3.0 in 0.01 steps.
  Three assets, three separate events, all **k = 2.00 and nothing anywhere
  else**: `BuyItem.WAV` 138 on a corpse loot (ncc **0.8292**, best at k=1.00
  is 0.03), `LevDn.WAV` 140 on an NPC death (**0.9959**), `SpelCast.WAV` 108
  on a gem cast (**0.762 / 0.728 / 0.768**, three trials). All three are
  **11,025 Hz** 16-bit mono assets; the band spectrum of the recorded
  `BuyItem` burst peaks at 8.4-9.4 kHz where the file's peaks at 4.1-4.6 kHz
  (ratio 2.05), and it lasts 0.26 s against the file's 0.516 s. So a player
  who hears these at all hears the wrong sound. **What is NOT measured is
  whether a 22,050 Hz asset is doubled too** -- the only one that played
  (`caveloop.wav`, 8-bit, distant emitter) never rose above the correlation
  floor -- so "the decoder ignores the file's rate and assumes 22,050" and
  "everything is doubled" are both still live, and one 22,050 Hz row
  distinguishes them. Whichever it is, the rate is being taken from
  somewhere other than the decoded WAV header. Closes when a PFS sound
  correlates at k = 1.00 in a live recording and a test pins the decoded
  sample rate of a known 11,025 Hz archive member.~~ **FIXED by T404 Leg A**
  (`4452b378`), **live re-check OWED**. The entry named the wrong two
  functions and so did the plan: the PFS path and the decoder are both
  correct, and the new decode assertions pass on the pre-fix tree (11,025 Hz
  on BuyItem/LevDn/SpelCast/SwordBlk, read straight from each member's RIFF
  header). `AudioMixer::render()`'s PCM loop did `ch.position++` once per
  OUTPUT frame and **never read `MixChannel::sampleRate` at all** -- every
  producer wrote that field and nothing consumed it. The device is opened at
  `AudioMixer::SAMPLE_RATE == 22050`, so an 11,025 Hz asset came out at
  exactly 2.00 and, by the same line, a 44,100 Hz asset would have played at
  half speed. That answers the entry's open question: it is **not** "the
  decoder assumes 22,050" and **not** "everything is doubled" -- it is "every
  asset plays at the device rate", which happens to double the whole shipped
  effects set because they are all 11,025. `position` is now a fractional
  source-frame cursor advanced by `sampleRate / SAMPLE_RATE` with linear
  interpolation; a device-rate channel steps by exactly 1.0 and is unchanged.
  `tests/test_audio_playback_rate.cpp`, 8 tests: pre-fix 7 fail at ratios
  1.985 / 1.999 / 1.989 against the assets' own headers, post-fix 8 pass.
  **OWED: the audible re-check on T401's loopback rig (ncc peak at k = 1.00)
  -- T404 took no server.** Prov: T401 Review §1d; T404 Review; B200; B036.

- ~~**B251** (A) `src/client/commands/combat_actions.cpp`
  `eqt::commands::CommandsComponent` (the `WITH_AUDIO` block after the
  miss/hit text) and the entity map's record for `MySpawnId()` -- **the local
  player's own `Entity` row is never updated when the server relocates him, so
  every sound and every range check that reads it uses the position he spawned
  at.** MEASURED T401 twice, in two unrelated symptoms.
  (1) `SwordBlk.WAV` 120 is played **30 times** in one combat session and is
  **absent from the recording at every rate** (best ncc 0.077): its log line
  reads `Sound play: id=120 file=SwordBlk.WAV pos=(428,-74,3)` while the
  player stands at `(146,-1009,51)` -- and `(428,-74,3)` is his own stored
  `character_data` row `x=-74, y=428, z=3` in wire order, i.e. the spawn pose,
  ~1,020 units away, where the 3D attenuation takes it to zero. The same
  session's `LevDn` at the *NPC's* position was audible, which is the control.
  (2) `SpellManager::validateCast` -> `checkRange` refuses a **self**-targeted
  gem cast with `Your target is out of range, get closer!` after a `#goto`:
  `checkRange: player=(0.0,12.0,3.0) target=(-1009.0,146.0,51.8)
  distance=1019.0 range=200` -- caster and target are the same character.
  So this is not an audio bug; it is one stale record with at least two
  reachable consequences, and it silently breaks any test that positions a
  character with `#goto` (T401 lost a whole run to it). Closes when a
  server-forced relocation updates the self entity, a live session shows the
  melee sound audible at the player's real position, and a test pins the
  self-entity position against an `OP_ClientUpdate`/zone-summon for
  `MySpawnId()`.~~ **FIXED by T404 Leg B** (`853bdf2b`), **live re-check
  OWED**. There is ONE authoritative position store and it is
  `m_game_state.player()` (T288); the self row is a PROJECTION of it, and
  only four sites wrote it -- the two `ZoneProcessClientUpdate` branches,
  `WarpToPosition` (`/warp`) and the spawn itself. Never the movement
  integrator, never the renderer's `PlayerPositionChanged` intent, never a
  zone-in warp or a death respawn; and the server does not routinely echo our
  own `OP_ClientUpdate`, so in a normal session the row held the spawn pose
  for the whole session. `eqt::PlayerFacade` now takes `m_entities` by
  reference and refreshes the projection from its two funnels --
  `SetPosLocal()` (integrator + renderer intent) and `SetPosition()`
  (discrete relocation) -- and nowhere else. **POSITION ONLY, deliberately:
  `ZoneProcessClientUpdate` stores `server_h` verbatim in the self row while
  writing `90 - server_h` into `PlayerState`, so mirroring the heading would
  rotate every self row by 90 degrees plus a mirror. A test pins that the
  heading is NOT mirrored.** `tests/test_self_entity_position.cpp`, 7 tests:
  pre-fix 4 fail, the self-targeted range check computing **977.78** against
  T401's live 1019.0 for the same shape; post-fix 7 pass. **OWED: the live
  proof (melee sound audible at the player's real position after a `#goto`)
  -- T404 took no server.** Prov: T401 Review §1e and §6.3; T404 Review.

- ~~**B252** (A/P) `refactor/BOARD.md` B203's own starting numbers --
  **the "live-measured pre-T383 baseline" B203 tells the next unit to start
  its A/B search from is WEATHER-INFLATED and 25% too high.** T377 measured
  fable's Ultravision at grey **x1.4483** and Infravision at **x1.2450**; T394
  later bisected and T396 proved that session was running under server rain at
  `weatherMod = 0.460`, which darkens the base a ratio is measured against
  while leaving the vision floor fixed. MEASURED T401 (`review/T401/` §4) on
  the **same constants** (T394 restored them byte-identically) with the
  modifier **proven 1.000 for the whole window** from the client's own log --
  the `onZoneEnter … ambient modifier 1.000` line plus the total absence of
  any `updateTimeOfDay: weatherMod … -> …` line, which since T396 fires on
  every change in both directions: **Ultravision grey x1.1606 (R 1.2078 /
  G 1.1658 / B 1.0649), Infravision grey x1.1693 (R 1.2754 / G 1.1076 /
  B 1.0596)**, video-stepped over 1,117 frames, five windows, each buff
  divided by the no-buff window before it. Against the oracle's grey fable is
  at **75%** (Ultravision) and **88%** (Infravision), not the 93%/99% the
  board records. Closes when B203's text no longer quotes the T377 numbers as
  a starting point -- done in the same commit -- and the re-solve is fitted
  against a clear-sky measurement. Prov: T401 Review §4d(1); B203; B211; T396.~~ **STRUCK T405 (`2edf3b3b`).** B203's text no longer quotes the T377 numbers as
  a starting point -- it no longer quotes ANY of them, because T405 re-measured
  both sides on a rig with the heading pinned as well as the weather and re-solved
  the constants against it. The re-solve is fitted against a clear-sky
  measurement, which is what this item asked for. Prov: T405 Review §1a, §3;
  `review/T405/`.

- ~~**B253** (A) the oracle side of B203's target table --
  **the official client's PER-CHANNEL vision ratios are composition-dependent
  and did not reproduce between two sessions at the same coordinates.**
  MEASURED T401 (`review/T401/` §4d(2)): Oraclealpha re-measured in the same
  session as the fable run, weather forced clear, gives Ultravision
  **R x1.9422 / G x1.2990 / B x1.5466 / grey x1.5419** against T377's
  **R x1.4581 / G x1.4633 / B x1.8507 / grey x1.5612**. The grey reproduces to
  1.2%; the channels do not, and the reason is in the frame -- T401's oracle
  is looking at a stone wall filling the left half of the crop where T377's is
  looking down an open street. **T386's own composition control fails on this
  pair: RMSE between the two sessions' oracle night stills is 10.3%, against
  the 2.7% T386 required to call two sessions comparable.** A vision buff
  multiplies the scene, so a per-channel ratio only means something between
  frames showing the same thing. B203's owed A/B search therefore cannot start
  from the per-channel table as written. Closes when one oracle capture is
  taken with the camera heading pinned and RECORDED (the official client's
  yaw on login is not the `character_data` heading), the composition control
  is inside 3%, and the per-channel table is re-taken against it. Prov: T401
  Review §4d(2); B203; B146.~~ **STRUCK T405 (`2edf3b3b`).** The heading IS pinnable and it was the whole
  problem. eqemu's `goto.cpp` takes an optional 4th argument and passes it to
  `MovePC`, so `#goto -74 428 3.751 160` places AND aims (T389 §5.3 had already
  found a `character_data` heading does not take on zone-in); h=160 is world
  angle 202.5 deg. With it recorded in the recipe, T386's composition control
  PASSES on the official client at **RMSE 0.389%** between two independent
  sessions -- against the 10.3% this item measured unpinned and the 2.7% bar --
  and the per-channel table duly REPRODUCES, to better than 0.2% on every
  channel: Ultravision R x2.2531 / G x1.4399 / B x1.7687, Infravision
  R x1.8744 / G x1.1956 / B x1.1936. That table replaces both earlier ones
  (T377's, weather-unknown, and T401's, unpinned). fable's own two sessions
  agree to 1.540%. Rig in `review/T405/scripts/`. Prov: T405 Review §1b, §1c,
  §2.

- ~~**B254** (A) `src/client/spell/spell_manager.cpp` the `OP_MemorizeSpell`
  handler -- **memorising a spell into an ALREADY-OCCUPIED gem leaves fable
  casting the OLD spell.** MEASURED T401 (`review/T401/` §6.4): after
  `#scribespells` filled the bar, `#memspell 93 0` produced the correct pair
  of server messages -- `MemorizeSpell response: slot=0 spell_id=467
  scribing=2` (un-memorise) then `slot=0 spell_id=93 scribing=1` (memorise) --
  and `/cast 1` then sent **`Sent CastSpell packet: spell=467, gem=0`**,
  `Beginning cast of 'Lightning Storm' (ID: 467)`. The gem into which nothing
  had been memorised (`#memspell 54 1` into an empty slot 1) took the new
  spell correctly, so the `scribing=1` arm works and the defect is in the
  order or the guard when a `scribing=2` for the same slot precedes it in the
  same batch. `#unmemspells` first is the workaround and made it correct in
  every later run. **A player hitting a gem and getting a different spell than
  the icon shows is class A**, and the wire evidence is a two-line replay.
  Closes when the two responses in that order leave gem 0 holding 93 and a
  test replays the pair.~~ **CLOSED by T404 Leg C** (`b75ac801`). Not an
  ordering bug and not a guard bug: **the arms did not exist.** The handler is
  `eqt::zone::ZoneComponent::OnMemorizeSpell` (`src/client/zone/zone_dispatch.cpp`,
  not `spell_manager.cpp` as this entry said) and it had two arms, both
  requiring `scribing == 0` -- the spellbook-scribe confirmation, and a bare
  `LOG_DEBUG` for a gem slot claiming "SpellManager handles this via other
  mechanisms". It does not: `m_spell_gems` moved only on our own local
  memorise timer and on the `PlayerProfile` bulk load, so nothing the server
  said about a GEM ever reached it. New
  `SpellManager::applyServerGemUpdate(gem_slot, spell_id, scribing)` plus the
  dispatch arm for `scribing` 1 or 2; one packet, one edit, applied in
  ARRIVAL ORDER. The un-memorise is unconditional (unlike `forgetSpell()`,
  which refuses while casting -- the server has already emptied the slot), and
  a response matching our own in-flight memorise is ignored so the
  `MemorizeProgress` animation is not cut short.
  `tests/test_memorize_into_occupied_gem.cpp`, 6 tests, wire replay: pre-fix 5
  fail with gem 0 still holding 467, post-fix 6 pass. Prov: T401 Review §6.4;
  T404 Review.

- **B255** (P) `/mnt/projects/EverQuest_ETC1/willeq.eqe1` -- **the shared UI /
  effects pack is rewritten under running units, and T401 caught it changing
  51 seconds into a live capture session.** MEASURED T401: the file went
  **2,811,102 -> 2,830,206 bytes at 23:09:57 on 2026-09-05**, alpha companions
  **1 -> 2** (T400's particle-atlas alpha, B225, whose CODE was not merged and
  is in nobody's binary). Two clients in the same unit therefore mmap'd
  different packs, and their own logs say so:
  `23:09:06 EQE1Loader: mmap'd '.../willeq.eqe1' (66 textures, 1 alpha, 1 raw,
  2811102 bytes)` versus `23:47:22 … (66 textures, 2 alpha, 1 raw, 2830206
  bytes)`. **So a live pixel measurement taken in one worktree can silently be
  a measurement of another worktree's unmerged asset** -- T401's fire/frost
  capture is "T401's code + T400's atlas", and it cannot separate the two.
  It also makes `ctest` red on every branch: `PackReproducible` fails (the
  installed sha256 no longer matches `data/willeq.eqe1.sha256`) and
  `visual_gate` fails on `eqe1MappedBytes` by **+19,343** in all three zones --
  which is NOT B240's +239, so B240's blessing numbers are already stale
  again. This is the same hazard class as the git stash (B-series
  `git-stash-is-cross-slot-shared`) and the shared session scratchpad: a
  mutable resource outside every checkout that no worktree owns. B228 and
  B230 name the drift; what is new here is that the window is **measured** and
  the consequence reaches captured PIXELS, not just a gate number. Closes when
  installing the pack is serialised against running units (a lock, the way the
  eqemu server is), or each worktree reads its own pack, so that a live
  capture can name the asset it ran against. Prov: T401 Review §0; B228; B230;
  B240; B225.

  **T405 (`2edf3b3b`) MEASURED THE WINDOW AGAIN AND IT IS WORSE: the shared pack was
  rewritten THREE TIMES inside one six-hour unit**, dated from the clients' own
  `EQE1Loader: mmap'd` lines -- 2,830,206 bytes (66 tex / 2 alpha) for its first
  two sessions, 19,994,910 (66 / 61) for the next five, 5,270,190 (126 / 61) for
  the last two, and 5,532,254 (125 / 61) by the time its ctest ran. That produced
  FOUR reds T405 did not cause (`visual_gate` on `eqe1MappedBytes`, short by an
  identical **2,721,391** in all three zones -- so B240's numbers AND B255's own
  +19,343 are both stale again; `PackReproducible`, whose BUILT sha matches the
  pin while the SHIPPED file does not; and `ParticleAtlasAlpha.{ShippedFileMatchesTheGeneratorPixelForPixel,PackEntryCarriesAlphaInBand}`),
  and it crossed a measurement CHAIN: T405's Newton steps were fitted on one pack
  and confirmed on another. T405 checked rather than assumed whether that
  invalidated its numbers -- the unbuffed night base on its measurement crop is
  0.004759 / 0.004779 / 0.004803 across the three packs (0.9% between group
  means, no step at either boundary), because the pack carries UI and particle
  textures and not the zone geometry the crop shows -- and its committed result
  rests on a matched pair on ONE pack. **That check was available by luck, not by
  design.** A live unit cannot pin a shared NFS asset it does not own, and a unit
  whose pixels are its evidence learns the asset moved only by reading its own
  log afterwards. Prov: T405 Review §6.

### Added by T406 (2026-09-06)

- ~~**B285** (B) `src/client/graphics/renderer/irrlicht_renderer.cpp`
  `createSoftwareCursor()` -- **the cursor is loaded under a cache name that
  no pack entry can ever match.** It calls
  `constrainedTextureCache_->getOrLoad("softwareCursor", ...)`, but the pack
  entry built from `data/textures/software_cursor.bmp` is named
  `software_cursor`; `findCompressedEntry()` only strips the extension and
  lowercases, so `softwarecursor` never matches. It works today purely by
  ordering luck: `createSoftwareCursor()` runs at `renderer_init.cpp:484`,
  **fourteen lines before** `getOrOpenEQE1("willeq")` at line 498, so
  `snapshotEQE1Loaders()` is still empty and the call falls through to the raw
  BMP decoder. Move the cursor load one line later, or open the pack one line
  earlier, and `getOrLoad()` takes the "EQE1 is configured but the texture is
  not in it" branch, which is `LOG_FATAL` + `exit(1)` -- on the DRM/KMS path,
  i.e. the Orange Pi, where the software cursor is the only cursor. T406 gave
  the pack entry the RGBA8888 override anyway so the two paths agree, but the
  name mismatch is unfixed. Closes when the cache name becomes
  `software_cursor` (and the ordering assumption is either removed or
  commented as load-bearing). Prov: T406 Review.~~ **CLOSED by T413 Leg B (`cd7a9bab`)**: the cache name is now `software_cursor`, the pack's own, and the comment says out loud that the initialisation order is no longer load-bearing. Pinned by `tests/test_software_cursor_pack_name.cpp` (4 tests), which READS the name out of `createSoftwareCursor()`'s source rather than restating it, looks it up in the real shipped `willeq.eqe1`, drives the real `ConstrainedTextureCache` with the pack ALREADY OPEN (the configuration the ordering was hiding), and pins the consequence with a death test -- with the pack open an unmatchable name is `ExitedWithCode(1)`, not a fallback. Pre-fix, with only the production literal reverted, 2 of the 4 fail naming 'softwareCursor'. Prov: T413 Review.

- **B286** (B) `src/client/graphics/spell_visual_fx.cpp` /
  `data/textures/spell_particle_fallback.bmp` -- **the spell particle fallback
  is a white square by construction, and no texture change can fix it.**
  `generateSpellParticleFallback()` writes `SColor(alpha, 255, 255, 255)`: a
  cosine falloff from the centre entirely in ALPHA over constant white RGB.
  Its only consumer sets the particle system to
  `EMT_TRANSPARENT_ADD_COLOR`, which `COpenGLES2Driver::
  applyMaterialRenderState()` maps to `glBlendFunc(GL_ONE, GL_ONE)`, and
  `SOLID3D_FS` computes `texColor * vColor` without premultiplying by alpha --
  so the source alpha reaches no visible channel and every texel of the 32x32
  quad adds full white. This is why T406 left the file 24-bit: converting it
  would have been a 33% size increase for zero pixels. The fix is one of
  (a) make the generator put the falloff in RGB (correct for an additive
  blend, and what the official client's Glowflare DDS does), or (b) draw the
  fallback alpha-blended like every other B266 consumer and convert the file.
  Reached only when the official client's `Glowflare03.dds` /
  `glittersp501.dds` / `Corona1.dds` are all missing, which is why it has
  never been reported. Prov: T406 Review. **T413 MEASURED IT AND BOTH
  PROPOSED FIXES ARE REFUTED (T413 Leg C, this branch's third commit): the shipped
  `data/textures/spell_particle_fallback.bmp` is 32x32 24-bit and ALL 3,072 of
  its pixel bytes are 255.** There is no falloff in ANY channel: the generator
  writes the cosine into ALPHA (`SColor(alpha, 255, 255, 255)`) and the 24-bit
  BMP write discards it, so the shape is destroyed at GENERATION time, not at
  blend time. So (a) there is no alpha for a shader to premultiply and (b) an
  alpha-blend material would sample alpha=1 everywhere and draw the same opaque
  white square. The only fix is asset-side -- the falloff in RGB, which is
  correct for the additive blend it is drawn with and is what the official
  client's Glowflare DDS does -- and that moves `data/textures/`,
  `data/textures.sha256`, the SHARED `/mnt/projects/EverQuest_ETC1/willeq.eqe1`
  and `data/willeq.eqe1.sha256` in one commit. **T413 did not do it because
  `PackReproducible --check` compares against the INSTALLED pack in every
  worktree**, so installing a new one turns the two units running in parallel
  red mid-flight; see **B322**. The reason string in
  `tests/test_texture_alpha_census.cpp` now carries the measurement. Class
  unchanged (B): the fallback is reached only when Glowflare03.dds,
  glittersp501.dds and Corona1.dds are all missing.

- ~~**B287** (C) `/mnt/projects/EverQuest_ETC1/willeq.eqe1`,
  `include/client/graphics/eqe1_format.h` -- **B266's fix cost 17.2 MB of shared pack and the
  same again in mapped bytes per zone, and the format has no cheaper way to
  carry an alpha-only texture.** Measured across T406's three commits: the
  pack went 2,830,206 -> 19,994,910 bytes (**+606%**), and `eqe1MappedBytes`
  rose by +17,184,047 in each of the gate's three zones. The reason is that
  59 textures moved from ETC1 (4 bits/texel, RGB only) to RGBA8888
  (32 bits/texel): a 256x256 entry with its mip chain goes 43,688 -> 349,528
  bytes, and 56 of the 59 are 256x256. The GPU cost is the same multiple --
  `benchmarks/audits/p13_texture_ledger` shows all 32 `storm_cloud_blend_*`
  uploaded in qeynos2 -- which lands on the Orange Pi's constrained texture
  budget, the platform where composite entities are already off for memory
  pressure. **Every one of the 59 is a CONSTANT-RGB image whose whole content
  is one 8-bit channel**, so an `A8` (or luminance+alpha) payload format would
  carry them at 8 bits/texel: ~4.4 MB instead of ~17.2 MB, with no visual
  difference at all. That is a format addition (`eqe1_format.h` payload id,
  builder mode, `uploadCompressed*` path, and a shader that reads the single
  channel as alpha), not a texture change, which is why T406 shipped the
  RGBA8888 form the plan specified rather than inventing one mid-unit.
  Needs Will's call: accept the 17.2 MB, or fund the compact payload.
  Prov: T406 Review; B261; B240.

---

## Added 2026-09-06 by T407 Leg B (the written-never-read field census)

`scripts/refactor/check_write_only_fields.py` (new, committed) parses every
struct/class body under `include/client/` and `src/client/` -- **6,414 data
members, 4,365 distinct names** -- and sweeps `src/ include/ tests/ tools/
benchmarks/` for every occurrence, classifying each as a write (`.name =`,
`->name =`, a compound assign, or a `: name(...)` / `, name{...}` constructor
initialiser) or a read. At tip `4dbeeedb` it reports **123 write-only fields**
(written somewhere, read nowhere) and **101 with no occurrence at all outside
their own declaration** -- 224 rows.

Matching is by IDENTIFIER, not by receiver type. That asymmetry is deliberate:
a name shared with another class can only ADD reads, so a collision can HIDE a
write-only field but can never invent one. **The report is a floor, and the
rows below are a queue, not a finding** -- each still needs the receiver
checked before anyone acts on it.

T407 deleted eleven (see below). The rest are boarded here.

- ~~**B290** (P) `scripts/refactor/check_write_only_fields.py` -- **the census is
  the queue; 213 of its 224 rows are untriaged.** T407 deleted eleven fields
  whose writes it also removed, compiler-proven at exit 0 with `tools/` in the
  build: `Application::m_lastUpdate`, `AudioManager::useDbForSounds_`,
  `CharacterModelView::parentSmgr_` and `::modelCenterY_`,
  `EntityRenderer::nameFont_` and `::currentCameraRegionIdx_`,
  `EquipmentModelLoader::mappingLoaded_`, `RenderPassTimer::currentPass_`,
  `PathfinderWaypoint::Implementation::FileName`, `SkyLoader::wldBuffer_`,
  `XmiDecoder::ticksPerQuarter_`. `tests/goldens/application_init_order_g04.txt`
  learned the `m_lastUpdate` deletion in place (entry 311 removed, count 314 ->
  313, both faults re-planted) per G correction 32. Closes when the remaining
  rows are triaged into delete / defect / exempt and the script is wired into
  ctest as a no-grow ratchet. Re-run it with
  `python3 scripts/refactor/check_write_only_fields.py --include-unused`.
  Found by T407 Leg B; the shape was named by T404 surprise 2
  (`MixChannel::sampleRate`).~~ **CLOSED by T414 Leg C**: all 83
  remaining write-only rows are classified in the script's new `EXEMPT` table --
  `wire` (the struct mirrors bytes on disk or on the wire: `EQE1Header::reserved`,
  `WldFragment37Header::param2`, the three `Sky*` records, `DoorVisual`'s two
  `_raw` bytes, `RDPPeerContext::selectedAudioFormat`), `owned` (B296's nine
  event_bus payload fields, T413's territory), `boarded` (B295, B298's 26 record
  fields, and the new B336-B346), `report` (B337). `--check` is wired into ctest
  as **`WriteOnlyFields`** (label `hygiene`) and fails BOTH ways: on a write-only
  field missing from the table, and on a table entry whose field is no longer
  write-only, so an exemption cannot outlive the row it excuses. Planted-fault
  proof of both halves in `refactor/T414_inert_config_keys_combat_tuning.md`
  §Review. The row count fell 111 -> 83 across T414's own legs.

- ~~**B291** (A) `include/client/graphics/entity_renderer.h`
  `EntityRenderer::skipTextureUpload_` -- **a constrained-mode knob that does
  nothing.** `src/client/graphics/renderer/irrlicht_renderer.cpp` calls
  `entityRenderer_->setSkipTextureUpload(config_.constrainedConfig.skipEntityTextureUpload)`
  with a real configured value; the member is stored and never read, so entity
  texture upload happens regardless. This is a live defect, not dead code: a
  caller supplies an intended value. Closes when the flag is consumed at the
  upload site or the config key and its plumbing are deleted together. Found by
  T407's census.~~ **CLOSED by T412 Leg A** (`3596e841`): the flag is read at the
  new `EntityRenderer::uploadPendingEntityTextures()`, the entity build path's
  single upload seam. There were **THREE** entity texture upload sites, not one --
  `ZoneLoadComponent`'s Step 8 (`renderer_zone_load_entities.cpp`) calls
  `uploadAllPending()` twice more, and both are now gated on the same config
  field; gating only the gameplay site would have left zone-in uploading
  everything. Skipping leaves the decoded entries PENDING, not dropped. The
  orangepi preset leaves `skipEntityTextureUpload` at the struct default
  `false`, and the visual gate cannot exercise it either way
  (`loadDebugOverrides()`'s only caller is `application.cpp`, i.e. `willeq`;
  the gate drives `gles2_zone_benchmark`). Pinned by
  `tests/test_entity_texture_upload_skip.cpp`, 4 cases.

- ~~**B292** (A) `include/client/audio/sfx_manager.h` `SfxManager::cacheMaxBytes_`
  -- **the sound cache has no ceiling.** `AudioManager` calls
  `sfxManager_->setCacheMaxBytes(soundCacheBytes)` with the configured budget;
  `setCacheMaxBytes()` stores it and nothing ever compares `cacheSizeBytes_`
  against it, so the SFX cache grows without bound on a memory-constrained
  target. Closes when eviction reads the ceiling, or when the setter and its
  caller go. Found by T407's census.~~ **CLOSED by T412 Leg B** (`37d35918`):
  LRU eviction mirroring `AudioManager::loadSound()`'s own sound-buffer LRU --
  `getWavData()` is the touch point (it is the read `play()` goes through),
  `setCacheMaxBytes()` enforces immediately so lowering the ceiling at runtime
  bites at once, 0 still means unlimited, and the cache is never evicted empty.
  The rule the sibling cache does not need: a WAV an ACTIVE `MixChannel` aliases
  (`play()` hands the mixer `wav->samples.data()`, read by the AUDIO thread) is
  never evicted -- over budget with a WARN beats a use-after-free. Note
  `src/client/audio/CLAUDE.md` had described this cache as an "LRU cache" all
  along; the doc was aspirational, not stale, and is now true. Pinned by
  `tests/test_sfx_cache_eviction.cpp`, 6 cases, two independent pre-fix plants.

- **B293** (B) `include/client/graphics/environment/particle_manager.h`
  `ParticleManager::entityDirCallback_` -- installed by TWO callers
  (`src/client/graphics/renderer/renderer_entities.cpp` and
  `tools/model_viewer/model_viewer_spell_test.cpp`) and invoked by none, so
  entity-direction-aware particle emission is unreachable. Closes when the
  callback is called or the setter, the member and the `EntityDirCallback`
  typedef are deleted with both call sites. Found by T407's census.

- ~~**B294** (A) `include/client/graphics/irrlicht_renderer.h`
  `IrrlichtRenderer::forcePvsUpdate_` -- set true in FOUR places
  (`src/client/graphics/renderer/renderer_init.cpp`,
  `src/client/graphics/player/renderer_camera.cpp`,
  `src/client/graphics/frame/renderer_frame_input.cpp`, and an inline setter in
  the header) to
  force a PVS visibility recalculation, and read nowhere. The comment on the
  member says "set when render distance changes"; changing render distance
  therefore does not force the recompute it asks for. Closes when the frame
  path consumes and clears the flag, or the four writes go with it. Found by
  T407's census.~~ **CLOSED by T412 Leg C** (`927994b8`) -- **the member and all
  four writes DELETED, with the proof.** The flag was a leftover, not a missing
  consumer: its one reader lived in `IrrlichtRenderer::updatePvsVisibility()`,
  which cached the camera's BSP region behind a 5-unit movement threshold in
  function-local statics, and `6914d99b` deleted that whole function when PVS
  moved to `SimulationWorker` (`portalCacheDirty_`, the other thing it reset, is
  gone from the tree too). Since then `SimBridgeComponent::buildSimulationInput()`
  republishes `input.renderDistance` and the camera position EVERY frame and
  `computeAll()` runs `calculateRegions()` + `computeVisibility()` over them
  unconditionally -- no threshold, no cache, nothing to invalidate; the one
  surviving cache (`computeRegionDepthMap()`'s) is keyed on region and is pure
  portal adjacency, independent of render distance. Consuming the flag would have
  meant inventing a skip that does not exist. Pinned by
  `tests/test_pvs_render_distance_trigger.cpp`: two behavioural cases on the
  replacement trigger (which pass on BOTH sides -- that is the point) plus a
  source guard, red at the base for all four writers. **`hideLoadingScreen()`'s
  comment was a truncated sentence** ("Without resetting here, the movement")
  left dangling by `6914d99b` -- an unfinished tombstone is what made the flag
  look deliberate.

- **B295** (B) `include/client/graphics/graphics_archive_index.h`
  `GraphicsArchiveIndex::lazyMode_` -- `buildIndex(eqPath, lazyMode, tick)`
  stores its `lazyMode` argument and never reads it, so the lazy/eager choice
  its callers make is discarded. Closes when the parameter is honoured or
  removed from the signature. Found by T407's census.

- ~~**B296** (A) `include/client/state/event_bus.h` -- **bridge event payload
  fields that every producer fills and no consumer reads.** The census names
  `CastingStateChangedData::castTimeRemainingMs` and `::castTimeTotalMs`,
  `SpellMemorizingData::progressMs`, `SpellGemChangedData::memorizeTotalMs`,
  `SkillActivationFeedbackData::cooldownMs`, `CursorItemChangedData::queueSize`,
  `TradeskillContainerOpenedEvent::containerName` / `::isWorldObject` /
  `::slotCount`, and `TradeskillContainerClosedEvent::wasWorldObject`. Each is
  a value the game thread computed for the renderer to draw -- cast bars,
  memorise bars, gem cooldown arcs, the tradeskill container title -- and the
  renderer never looks at it. This is the T264 event-reachability sweep's
  missing half: `test_reachability_sweep` pins that every event TYPE has a
  publisher and a consumer, and says nothing about its FIELDS. Closes when each
  field is drawn or dropped; a per-field clause in the reachability sweep would
  keep it closed. Found by T407's census.~~ **CLOSED by T413 Leg A (`89e773df`), per field**:
  - `CastingStateChangedData::castTimeRemainingMs` / `::castTimeTotalMs` --
    **DRAWN**. Oracle `EQUI_CastingWindow.xml` `<Gauge item="Casting_Gauge">`
    EQType 7, 116x10 at (3,19) in the 140x60 "Casting Time" screen. fable
    already drew that gauge but clocked it off a render-thread stopwatch; the
    bridge arm was a `LOG_TRACE` whose comment named `WindowManager`, deleted
    in `0f25cb57`. It now applies the authoritative remaining/total, stopwatch
    kept as the pre-first-sample fallback.
  - `SpellMemorizingData::progressMs` -- **DRAWN**. Oracle
    `EQUI_SpellBookWnd.xml` `<Gauge item="SBW_Memorize_Gauge">` EQType 9,
    100x8 at (204,312), FillTint (200,0,200), in the 510x360 `SpellBookWnd`.
    Drawn in fable's spellbook popup by `spellbookMemorizeGaugeRect()`, the
    same proportional rule `InventoryLayout::coinRect()` uses --
    `{360,428,438,435}` at 800x600. Second WindowManager-era `LOG_TRACE` gone.
  - `SpellGemChangedData::memorizeTotalMs` -- **DROPPED**. The oracle draws no
    gauge on a gem: `EQUI_CastSpellWnd.xml`'s `<SpellGem item="CSPW_Spell0">`
    .. `"CSPW_Spell8"` carry only a `SpellGemDrawTemplate`
    (Holder/Background/Highlight), no Gauge piece. Memorise progress is the
    spellbook's gauge, published on `SpellMemorizing`.
  - `CursorItemChangedData::queueSize` -- **DROPPED**.
    `EQUI_CursorAttachment.xml` is three 40x40 icon slots (`CA_Anim`,
    `CA_Anim2`, `CA_SpellGem`) with no count label: the official client never
    shows the queue depth. `InventoryState::cursorQueueSize()` (read + tested)
    is untouched.
  - `SkillActivationFeedbackData::cooldownMs` -- **BOARDED, panel missing**:
    **B320**.
  - `TradeskillContainerOpenedEvent::containerName` / `::isWorldObject` /
    `::slotCount` and `TradeskillContainerClosedEvent::wasWorldObject` --
    **BOARDED, panel missing**: **B321**.

  Pinned by `tests/test_bridge_payload_fields.cpp` (12 tests; pre-fix the TU
  does not compile against the reverted production). The per-field clause the
  entry asked for is NOT added to `test_reachability_sweep` -- the two
  compile-time pins in the new TU do that job for the dropped pair, and the
  general sweep is a separate piece of work. Prov: T413 Review.

- ~~**B297** (B) `include/client/graphics/environment/environment_config.h` and
  siblings -- **JSON config keys parsed into fields nothing reads.** The census
  names `DetailSettings::grassEnabled` / `plantsEnabled` / `rocksEnabled` /
  `debrisEnabled`, `BoidsSettings::flockSizeMin` / `flockSizeMax` /
  `scatterRadius` / `spawnCooldown`, `BoidsBudget::viewDistance`,
  `RainOverlaySettings::layerDepthMin` / `layerDepthMax` / `windTiltBase` /
  `windTiltIntensity`, `SnowOverlaySettings::layerDepthMin` / `layerDepthMax`,
  `StormCloudSettings::blendDuration`,
  `ConstrainedRendererConfig::enableItemIcons` / `frontToBackSorting`,
  `WeatherPresetValues::fogTransitionSpeed` / `maxBranchLevel`,
  `ZoneDetailConfig::isOutdoor`, `DisplaySettings::detailViewDistance`,
  `DetailType::atlasX` / `atlasY` / `windHeightBias`. Editing any of these in
  the shipped JSON changes nothing, silently -- the same defect class as B245's
  read-by-nobody config files, one level down. Closes when each is consumed or
  the key is removed from the loader and the shipped JSON together. Found by
  T407's census.~~ **CLOSED by T414 Leg A**
  (`315619f5`). The entry undercounted: **twenty-nine** shipped keys were inert,
  not twenty-two, and the eight most damaging were in a file B297 never named.
  **Eight were WIRED**, not deleted: `config/display_settings.json` ships fifteen
  keys and `renderer_init.cpp`'s static `loadDisplaySettingsFromFile()` read
  SEVEN. `renderDistance`, `environmentEffects.quality`/`.density`/
  `.reactiveFoliage`/`.enableFireGlowLighting`/`.enableFireGlowIcospheres`/
  `.maxFireGlowLights` and `detailObjects.density`/`.grass`/`.plants`/`.rocks`/
  `.debris` all name `DisplaySettings` fields with LIVE consumers --
  `IrrlichtRenderer::setRenderDistance()` and
  `OverlaysComponent::applyEnvironmentalDisplaySettings()`, which calls
  `DetailManager::setCategoryEnabled()` per category with a value the file never
  supplied. Only the parse line was missing. The parse moved to
  `src/client/graphics/display_settings_loader.cpp` (testable with no renderer,
  device or GL context) and the shipped file -- one machine's saved slider
  positions from the DELETED options window, never in effect -- was rewritten to
  the values that WERE in effect, so no behaviour and no visual golden moved.
  **Twenty-one were deleted**: `environment_effects.json`'s whole `detailObjects`
  and `boids` sections with `DetailSettings`/`BoidsSettings` (`getDetailObjects()`
  and `getBoids()` have ZERO callers -- a second, drifting copy of configuration
  that `detail_objects.json` and `SimulationWorker`+`BoidsBudget::fromQuality()`
  actually own), `detail_objects.json`'s `seasons` section (an exact duplicate of
  `SeasonalController`'s hardcoded table), `constrained_presets.json`'s
  `enableTextureAtlas` (its field was removed by E10), `stormClouds.blendDuration`
  (the crossfade is PRE-BAKED into `storm_cloud_blend_N.bmp`), the rain and snow
  `layerDepthMin`/`layerDepthMax`/`numLayers`, `BoidsBudget::viewDistance`,
  `WeatherPresetValues::fogTransitionSpeed`, `ZoneDetailConfig::isOutdoor`, the
  legacy `DetailType::atlas{X,Y,Width,Height}` block (superseded by `atlasTile`;
  nothing converted its pixels to UVs) and `DisplaySettings::detailViewDistance`.
  **Five families boarded**: B330-B335. New net:
  `ConfigKeyReachability.EveryShippedConfigKeyIsNamedByCode`, the KEY-level
  companion to B245's file-level census.

- **B298** (B) `include/client/inventory/item_instance.h`,
  `include/client/spell/spell_data.h`, `include/client/skill/skill_data.h`,
  `include/client/state/entity_manager.h` -- **server- and DB-sourced data
  parsed and never shown.** `ItemInstance` carries 21 such fields
  (`accuracy`, `damageShield`, `dotShield`, `spellShield`, `strikethrough`,
  `stunResist`, `shielding`, `enduranceRegen`, `hpRegen`, `manaRegen`, `delay`,
  `recLevel`, `maxCharges`, `lore`, `currentSlot`, the seven `heroic*`,
  `expendable`, `bardEffect`); `SpellData` carries `resist_diff`, `zone_type`,
  `teleport_zone`, `spell_fades`, `player_tag`, `environment_type`,
  `time_of_day`, `aoe_duration`, `push_up`, `allow_rest`, `is_dispellable`,
  `no_partial_resist`, `no_expend_reagent`; `SkillData` carries
  `animation_code`, `usable_while_sitting`, `usable_while_stunned`,
  `min_level`; `Entity` carries `guildId`, `anon_status`, `deltaHeading`;
  `RaceModelInfo` carries `genderVariants`, `neuterCode`. Each is a loader
  writing a value with no reader -- item tooltips, spell descriptions, skill
  gating and the `/who` guild/anon columns are the features they belong to.
  Closes per family as those features land; until then the loads are honest
  but inert. Found by T407's census.

- ~~**B299** (B) `include/client/combat.h` `CombatManager::m_assist_range`,
  `m_combat_range`, `m_spell_range`, `m_is_resting`, `m_rest_mana_threshold`,
  and `CombatTarget::last_considered` -- dead tuning state. `SetAssistRange()`
  and `SetCombatRange()` have exactly one caller each, `test_combat_manager.cpp`,
  whose own comment says the class has "NO public getters" and which asserts
  only `EXPECT_NO_THROW`; `m_is_resting` is written true/false by
  `combat_hunting.cpp` and consulted by nothing. NOT deleted in T407 because
  removing the setters requires editing that test, which T407's contract kept
  unedited. Closes when the setters, the members and the test's `EXPECT_NO_THROW`
  block go together, or a consumer appears. Found by T407's census.~~ **CLOSED by T414 Leg B**
  (`4ac66d13`). `m_assist_range`, `m_combat_range`, `m_spell_range`,
  `m_is_resting` and `CombatTarget::last_considered` are deleted with
  `SetAssistRange()`/`SetCombatRange()`, and `test_combat_manager.cpp`'s
  `EXPECT_NO_THROW` block is replaced by a real round-trip on the one setter that
  survives (`SetAggroRadius` -- `m_aggro_radius` IS read by `GetNearbyHostiles()`
  and the hunt scan): at radius 0 the hostile scan comes back empty. The common
  reason is one line: the server enforces reach and swing timers, the client
  animates. `/assist` is a server round-trip, melee reach is per-target
  `CalculateCombatRange(my_size, target_size, race)`, spell range comes from the
  spell row, and `SetCombatState(COMBAT_STATE_RESTING)` was already the resting
  flag `m_is_resting` duplicated. **Correction to this entry**:
  `m_rest_mana_threshold` is NOT write-only -- `CombatManager::ShouldRest()`
  reads it (`combat_hunting.cpp`). It was named from the census summary, not the
  census output, which never flagged it.

- **B300** (A) `src/client/graphics/environment/spell_effects_config.cpp`
  `buildTextureNameMap()` -- **the 16-tile procedural particle atlas cannot
  represent a large share of the sprites the corrected reader now resolves,
  because several of them are not abstract particles at all.** Split out of
  B162 by T409 Leg B, which mapped ten names and stopped here. With the
  0-based rule 36 of the 40 `spells.eff` names resolve to a real
  `SpellEffects/*.tga`; T409 cropped every one of them to the single frame
  its 4x4 sheet repeats (`review/T409/sprites/`) and found three groups.
  (a) **Mappable**: a pointed star with a bright core (`spela1`, `spela2`,
  `speld2`), a green burst (`spelb1`), an orange sunburst (`spelc1`), a
  green disc (`spele1`), an elongated ribbon (`spelj1`, `spelu1`), a mottled
  disc (`speln1`), a smoke ball (`spelz1`) -- these are the ten rows T409
  added. (b) **Figurative, and unmappable by construction**: `spelg1.tga` is
  a **SWORD**, `speli1.tga` a **flaming SKULL**, `spelk2.tga` a row of
  **skulls**, `spelh1.tga` a lightning-wreathed sphere. `GENI00_SPRITE` is
  the COMMONEST name in the whole file (32 slots) and it is the skull -- so
  the single most-used sprite in `spells.eff` is one the atlas can never
  approximate. They keep the `SoftCircle` fallback, pinned by
  `SpellSpriteTilesT409.TheFigurativeSpritesKeepTheFallback`. (c) **Round
  soft dots** (`spelm1`, `spelo1`, `spell1`, `spelv1`, `spelp1`) that the
  `SoftCircle` fallback already serves correctly, so a row would add
  nothing. **Will's call**: either fable draws the real `spel*.tga` files
  (which is B225's "nothing downstream can draw a `spel*.tga` anyway", and
  needs a texture path, not an atlas tile), or the atlas grows tiles for the
  figurative shapes, or those spells are accepted as visually approximate
  forever. Do NOT quietly widen the name -> tile table to cover (b) -- a
  skull drawn as a soft white dot is a wrong answer with a confident face on
  it. Prov: T409 Review Leg B; B162; B225.

**Exempt, recorded so nobody re-reports them.** The census also names fields
that are write-only BY CONSTRUCTION because they are serialised layout, and
these are not defects: `DDSHeader` / `DDSPixelFormat` (the decoder reads the
fields it needs and the struct mirrors the on-disk header),
`WldFragment12Header` / `WldFragment36Header` / `WldFragment37Header` /
`WldHeader::unk3`, `EQE1Header::reserved`, `Weather_Struct::pad1` / `pad2`,
`ActionPacket_Struct::unknown23` / `unknown25` / `force` / `hit_heading` /
`hit_pitch` / `instrument_mod`, `CombatDamage_Struct::unknown06`, and
`EffSoundEntry::UnkPad57` / `UnkPad58` / `UnkPad59` / `UnkRange64` /
`UnkRange72`. A wire or file struct must keep its shape whether or not the
client uses every field.

  **WILL RULED 2026-09-06:** draw the REAL sprites — effects as close to the original as possible. → T417 (atlas from SpellEffects/*.tga). Now class A.
- **B305** (P) The T410 scenario lane's THREE LIVE PROOF RUNS ARE OWED. The
  lane builds, its 20 headless state-machine cases are green, and the client
  was observed arming and reporting a verdict; but `zone_in`,
  `two_client_survive` and `cast_lands` have never been run through
  `scripts/scenario.sh` against eqemu, because `/mnt/projects/fable/.eqemu-lock`
  was held by T405 (slot2) for the whole of T410 and the protocol forbids
  waiting on it. Two specific things are UNMEASURED and could each turn a
  green lane red on first contact: (a) `cast_lands`'s prep recipe assumes
  `#memspell <spellid> <gem, 0-based>` accepts spell 46 (Ultravision) for a
  level-25 wizard -- T401 measured the SYNTAX with spells 93 and 54, not this
  spell for this class, and a `#memspell` the server silently refuses looks
  exactly like a cast that did not land; (b) `scripts/scenario.sh`'s
  snapshot/restore has never been exercised -- its `restore_snapshot()` is
  written from T377's and T401's lesson (the world rewrites character rows on
  shutdown, so restore only with world/zone/loginserver and the spire
  launcher killed) but has not itself been run. Closes when: the three runs
  are recorded in `review/T410/` with their exit codes and a byte-identical
  before/after snapshot diff. Prov: T410 Review.

- **B306** (P) An unattributed process ran a live client against eqemu while
  T405 held the lock. During T410 a smoke test intended to prove the
  could-not-start path (`--scenario zone_in` expecting exit 3 with no server)
  found the server UP: it logged `Testcaster` in, zoned to `befallen`, passed
  and exited 0 after 4.95 s. It sent no command and moved no character, but it
  is a lock violation and, if T405 was using `Testcaster`, it will have kicked
  that session. **The lesson is mechanical, not moral: a "there is no server"
  negative test is not safe to run without checking the lock first, because
  the server's absence is an assumption, not a fact.** `scripts/scenario.sh`
  already refuses on a held lock (exit 64); a bare `willeq --scenario` does
  not, and arguably should warn. Closes when: T405 confirms it was
  undisturbed, or the damage is identified and repaired. Prov: T410 Review.

  Prov: T406 Review; B261; B240.~~  **CLOSED by T411 (2026-09-06) -- and it needed no format addition.** The
  question was "accept the 17.2 MB or fund an A8 payload"; the answer was
  neither. The format ALREADY carries an alpha-only texture cheaply -- the ETC1
  `<name>_alpha` companion -- and the only reason T400/T406 could not use it
  was that no alpha-BLEND shader sampled unit 1 (B261). T411 gave the three
  blend shaders that sampler and cut `scripts/build_willeq_pack.sh`'s
  `--rgba8888-texture` list from **60 entries to 1**: every one of the 60
  dropped it, and the one that has it now (`ui_atlas`) was never on the list
  and is there for a different, measured reason -- the static UI's 1px-tall
  `drawRect` minifies the atlas 4:1, selecting companion mip level 2, where
  ETC1 decodes the isolated `white` texel as **217/255** and dims every 1px UI
  rect by 15% (`UIAtlasPixelTest.SpellGemReadyBorderIsGreen` read 172 against
  a wanted 200; decoded straight out of `ui_atlas_alpha` level 2 to confirm).
  The list is pinned by `kInBandAlpha` in
  `tests/test_texture_alpha_census.cpp`, and
  `NoPackEntryNeedsTheRgba8888Payload` fails on any addition without one.
  Measured: `willeq.eqe1` **19,994,910 -> 5,532,254 bytes (-14,462,656,
  -72.3%)** and `eqe1MappedBytes` **-14,462,656 in each of the gate's three
  zones** (qeynos2 137,128,478 -> 122,665,822; gfaydark 135,794,570 ->
  121,331,914; befallen 130,279,066 -> 115,816,410). Per 256x256 weather entry
  the pack payload goes 349,524 -> 43,704 plus a 43,704 companion, and the GPU
  upload 349,524 -> 87,376 (-75%). It does NOT return to the pre-T406 2.83 MB:
  the companions are new bytes (the pre-T406 BMPs were 24-bit and had no alpha
  at all). **The GPU saving is not visible in the gate**: `gpuTextureBytes` for
  the qeynos2 coverage run is 9,682,104 with BOTH packs, because that run
  activates no weather, spawns no particles and draws no UI, so none of the
  changed entries is resident. The alpha is now ETC1-compressed rather than
  exact 8-bit -- no image golden moved, and the remaining bless is the
  eqe1MappedBytes DOWN-move under B315.

### Added by T411 (2026-09-06)

- ~~**B315** (C) `tests/goldens/manifest.json` (the `memoryCoverage` block, the
  three `eqe1MappedBytes` values) -- **the gate's EQE1 memory baseline is stale
  by -14,462,656 in every zone and needs Will's bless, the same way the
  UP-move did.** B240's blessed numbers were T406's; T411 gave the alpha-BLEND
  shaders a companion sampler and cut the `--rgba8888-texture` list from 60
  entries to 1, so `/mnt/projects/EverQuest_ETC1/willeq.eqe1` went
  19,994,910 -> 5,532,254 bytes and every zone's mapped total fell by exactly
  that 14,462,656:

  | zone     | manifest (T406) | measured T411 | delta       |
  |----------|----------------:|--------------:|------------:|
  | qeynos2  |     137,128,478 |   122,665,822 | -14,462,656 |
  | gfaydark |     135,794,570 |   121,331,914 | -14,462,656 |
  | befallen |     130,279,066 |   115,816,410 | -14,462,656 |

  **No image golden moved and no CPU p95 ceiling was missed** (qeynos2 7.041 /
  gfaydark 5.620 / befallen 6.081 against 10.108 / 10.672 / 10.948) -- these
  three metrics are the gate's ONLY failures, and `git status tests/goldens/`
  is empty after the run. T411 did NOT bless: same standing rule as B240 --
  Will blesses in chat, someone then edits the three values BY HAND (never
  `--bless`, which would also rewrite the T69 per-zone memory block).
  Prov: T411 Review; B240; B287.~~ **APPLIED at the T411 merge under Will's B240 bless (2026-09-06, "bless B240" for the pack's final content): eqe1MappedBytes = 122,665,822 / 121,331,914 / 115,816,410; no image golden moved.**
- ~~**B316** (B) `src/client/graphics/ui/` consumers of the `ui_atlas` pack entry
  -- **the static UI had been drawing its atlas fully OPAQUE, and T411 turned
  its real alpha on; only the pinned pixels have been checked.** `ui_atlas` has
  always been an ETC1 entry with an ETC1 `ui_atlas_alpha` companion, and the
  whole static UI reaches it through `draw2DImage(..., useAlphaChannelOfTexture
  =true)` -> `UI2D_FS`, which until T411 read `texture2D(uTexture, uv).a` ==
  1.0 for an ETC1 texture (B261). Measured on the generated atlas:
  **77.1% of its 65,536 texels are not opaque and 64.0% are fully transparent**
  (42 alpha levels, min 0 max 255). So every sprite, bar, border and coin decal
  the UI draws was painted as a filled rectangle, and since T411 it is painted
  with its shape. That is the correct behaviour and exactly what B261 was
  about, but it is a real visual change on a path the visual gate does not
  photograph -- **no gate golden moved**, which is itself the finding: the
  gate's three zones capture no UI. What DID catch a piece of it is
  `UIAtlasPixelTest.SpellGemReadyBorderIsGreen`, which went red at 172/200 --
  traced not to the atlas alpha itself but to companion MIP selection (see the
  struck B287), and fixed by giving `ui_atlas` (and only `ui_atlas`) the
  in-band payload. All 14 `test_ui_atlas_pixels` cases are green with the alpha
  live, so no PINNED UI pixel changed; the unpinned rest of the UI is what
  nobody has looked at. Closes when someone runs the client and looks at it, or
  pins more of it. Prov: T411 Review section 4.~~
  **CLOSED by T416** (`refactor/T416_ui_atlas_review.md` Review): all fourteen
  static panels plus the four popups were captured headlessly through a real
  `UIAtlas` at 800x600 on a checkerboard, three ways -- pre-T411 (alpha
  ignored), T411 as merged, and fixed -- in `review/T416/{before,after,fixed}/`
  with per-panel contact sheets in `review/T416/crops/`. The tool is new and
  reusable: `tools/hwcaps/ui_panel_capture.cpp`. **Exactly ONE thing broke**:
  the purse's coin rows, the only place `slot_border_normal` (a 1px ring in a
  32x32 tile) is drawn onto a rect SMALLER than the tile (18px tall), so mip
  level 1 is selected and the ring has averaged into its own transparent
  interior -- the frame fell to a 17/255 contrast step. Fixed by drawing it
  from the uniform 4x4 `white` sprite through the new
  `UIRenderer::drawFrame()`, pinned by `UIAtlasCoinRowFrameTest` (2 cases,
  both RED pre-fix at edge 37 / interior 18). Everything else survived the
  alpha intact. What the sweep also turned up: the doubled "%%" health label
  (fixed here), and four items boarded rather than fixed:
  B350 (the look decision), B351 (six atlas regions with no consumer), B352
  (the pet button row) and B353 (a gate proposal).

---

## Added 2026-09-06 by T413

- ~~**B320** (B) `include/client/state/event_bus.h`
  `SkillActivationFeedbackData::cooldownMs`, and the panel it needs --
  **fable has no combat-abilities window, so a skill's cooldown has nowhere to
  be drawn.** `EverQuest` computes an adjusted cooldown
  (`src/client/core/everquest.cpp`) and publishes it on every skill
  activation; `IrrlichtBridge`'s `SkillActivationFeedback` arm is a
  `LOG_TRACE` that discards the whole payload, and no static panel has a slot
  for it. The oracle element is `EQUI_CombatAbilityWnd.xml`'s
  `<Gauge item="CAW_CombatEffectTimeRemainingGauge">` (EQType 26, 116x10 at
  (11,30)) with `CAW_CombatEffectLabel` above it, inside the 174x150
  `<Screen item="CombatAbilityWnd"> <Text>Combat Abilities</Text>`, plus the
  eight 40x40 `CAW_Button1..8` ability buttons. Closes when that panel exists
  and the field feeds its gauge; the field is left in place until then, so the
  producer does not have to be rewritten twice. Split out of B296 by T413.~~
  **CLOSED by T415 Leg B** (`task/T415-tradeskill-combat-windows`, the commit
  that added this line). The panel exists:
  `include/client/graphics/ui/combat_ability_panel.h` +
  `src/client/graphics/ui/combat_ability_panel.cpp`, with the oracle's
  geometry in `combat_ability_layout.h` at the window's own 174x150 --
  `Style_Sizable false`, so nothing is scaled and no rect can round. The
  `SkillActivationFeedback` arm now calls `applyCombatAbilityFeedback()` and
  `cooldownMs` drives `CAW_CombatEffectTimeRemainingGauge` as a
  TIME-REMAINING fill (1.0 at activation falling to 0.0). The eight
  `CAW_Button1..8` rows are a VIEW of `skillsPopupState_.skills` -- the list
  `/skills` already draws -- filtered to `SkillCategory::Combat` AND
  `isActivatableSkill()` AND value != 0, in skill-id order, capped at eight;
  there is no second source of skill data, and the rebuild is driven off the
  existing `SkillsSnapshot` and `SkillValueChanged` arms. A click pushes the
  existing `SkillActivateIntent`. The key is **Alt+C**, MEASURED at the
  oracle's Options > Keys > UI page (T377,
  `review/T377/oracle/T377_oracle_options_keys_UI_page3.png` and `page4`):
  "Toggle Combat Ability Window   Alt C", Alternate column empty -- not the
  Titanium-table guess the plan allowed for. Default CLOSED:
  `/mnt/projects/EverQuestP1999/defaults.ini` has NO `[CombatAbilityWnd]`
  section at all, so unlike `[BuffWindow]`/`[GroupWindow]` there is no shipped
  `Show=` to read either way. **DEVIATION, flagged:** the window's own
  `<Location>` is (50,50), which in fable's layout is the player status
  panel, so the window is anchored top-right instead -- under `targetInfo`
  and left of the spell-gem column. Size and every interior rect are the
  oracle's. Pinned by `tests/test_tradeskill_combat_panels.cpp` (8 cases) and
  the render-order golden. Live screenshot pair OWED to the next lock unit.

- ~~**B321** (B) `include/client/state/event_bus.h`
  `TradeskillContainerOpenedEvent::containerName` / `::isWorldObject` /
  `::slotCount` and `TradeskillContainerClosedEvent::wasWorldObject`, and the
  panel they need -- **fable has no tradeskill container window.**
  `EntitiesComponent` fills all four (`entities_component_world.cpp`) and both
  bridge arms are `LOG_TRACE`s that discard them; the container's NAME, its
  world-vs-inventory provenance and its slot COUNT are exactly a container
  window's title, close behaviour and grid size. Oracle:
  `EQUI_TradeskillWnd.xml`. Closes when that panel exists and the four fields
  feed it. Split out of B296 by T413.~~
  **CLOSED by T415 Leg A** (`913455bf`). The panel is
  `include/client/graphics/ui/tradeskill_panel.h` +
  `src/client/graphics/ui/tradeskill_panel.cpp`; its geometry is the oracle's,
  derived in `tradeskill_layout.h` from `EQUI_TradeskillWnd.xml`'s ANCHOR
  arithmetic (the container half of that window is anchor-positioned, not
  absolute -- the full derivation is written out in the header) and mapped
  proportionally onto `centerPopup` the way `InventoryLayout::coinRect()` maps
  the purse. `containerName` is the title (`COMBW_ContainerName`),
  `slotCount` bounds the ten `COMBW_RequiredIcon` cells, `isWorldObject`
  chooses which combine the Combine button sends, and either close event
  hides the window.
  **THE SURPRISE, and why the STOP condition did not fire:** the slot
  CONTENTS needed no new event. eqemu sends a world container's items as
  `ITEM_PACKET_WORLD_CONTAINER`, `InventoryManager::processItemPacket` maps
  index i to `WORLD_BEGIN + i` (T282), and `zone_dispatch.cpp`'s generic
  ItemPacket fallback publishes an ENRICHED `InventorySlotChanged` for it --
  name, icon and quantity included. That event was already crossing the
  bridge; `IrrlichtBridge`'s arm just dropped every slot id outside 0..29, so
  an open forge's contents arrived renderer-side and went nowhere. The fix is
  one `else` branch. `ZoneProcessClearObject` publishes the same event with
  `hasItem = false` for all ten slots, so the clear path needed nothing
  either. Pinned by `tests/test_tradeskill_combat_panels.cpp` (8 cases) and
  the render-order golden. Live screenshot pair OWED to the next lock unit.

- **B322** (B) `src/client/graphics/texture_generators.cpp`
  `generateSpellParticleFallback()`, `data/textures/spell_particle_fallback.bmp`,
  `data/textures.sha256`, `/mnt/projects/EverQuest_ETC1/willeq.eqe1`,
  `data/willeq.eqe1.sha256` -- **the spell particle fallback's shape is
  destroyed at GENERATION time, and fixing it is a SHARED-PACK unit.**
  T413 measured the shipped file: 32x32, 24-bit, and **all 3,072 pixel bytes
  are 255** -- solid white, no falloff in any channel. The generator computes a
  cosine falloff and writes it into ALPHA only
  (`SColor(alpha, 255, 255, 255)`); the 24-bit BMP write throws that channel
  away. This REFUTES both fixes B286 proposed: there is no alpha for a shader
  to premultiply, and an alpha-blend material would sample alpha=1 everywhere
  and draw the same opaque white square. The fix is the generator putting the
  falloff in RGB -- correct for the additive blend
  (`EMT_TRANSPARENT_ADD_COLOR` -> `glBlendFunc(GL_ONE, GL_ONE)`) this is drawn
  with, and what the official client's Glowflare DDS does -- which cascades:
  regenerate the BMPs, rewrite `data/textures.sha256`, rebuild the pack,
  update `data/willeq.eqe1.sha256`, **all in one commit**. **T413 did not do it
  and that is the point of this entry**: `PackReproducible --check` compares
  the rebuild against the pinned hash AND against the INSTALLED pack, which
  every worktree reads, so installing a new pack turns the suite red in every
  slot running a unit at the time -- T408 and T412 were both live. This needs a
  unit that owns the pack exclusively (the way T411 did). The measurement is
  recorded in `tests/test_texture_alpha_census.cpp`'s reason string for the
  file. Low urgency: the fallback is reached only when Glowflare03.dds,
  glittersp501.dds and Corona1.dds are ALL missing. Found by T413.

- **B330** (B) `include/client/graphics/environment/rain_overlay.h` --
  **the rain overlay cannot tilt.** `windTiltBase` / `windTiltIntensity` were
  parsed from `weather_effects.json` (clamped, ranged) into fields nothing read;
  T414 Leg A deleted the keys, the parse and the fields. Honouring them needs a
  rotated-quad draw: `RainOverlay::render()` tiles the streak texture with
  axis-aligned `draw2DImage` rects, so a screen-space angle has nowhere to go.
  Closes when the overlay can draw a rotated quad and the two keys come back
  together with a test. Found by T414 Leg A.

- **B331** (B) `include/client/graphics/weather_quality_preset.h` -- **lightning
  has no branches to bound.** `WeatherPresetValues::maxBranchLevel` carried
  0/2/4/6 per quality preset and the shipped `storm.maxBranchLevel` key carried
  4; there is no lightning-bolt branch generator anywhere in the tree, and
  `applyToWeatherConfig()` said so in a comment ("would need to be added to
  WeatherEffectsConfig"). T414 Leg A deleted the field, the four preset lines,
  the comment and the key. Closes when a branching bolt lands and re-adds all of
  them together. Found by T414 Leg A.

- **B332** (B) `include/client/graphics/constrained_renderer_config.h` --
  **`enableItemIcons` promised a RAM saving that nothing could deliver.** The
  `orangepi` preset set it false in code and `constrained_presets.json` set it in
  two presets; there is no item/spell icon texture loading in the tree at all --
  `static_panels.cpp` draws abbreviated item names with "placeholder until icon
  atlas" beside them, and `StaticPanels::iconId` is commented "for future icon
  atlas". T414 Leg A deleted the field, the parse and the shipped keys. Closes
  when an icon atlas exists and the flag gates its load. Found by T414 Leg A.

- **B333** (B) `include/client/graphics/constrained_renderer_config.h` --
  **`frontToBackSorting` was a second, dead spelling of a live feature.** The
  preset key parsed into a field nothing read. The real front-to-back zone draw
  is `manualZoneDrawEnabled_` (`/sort`, `RenderPassTimer::OnRenderPassPreRender`);
  this one never reached it. T414 Leg A deleted the field and the parse. Closes
  if a preset should be able to preset `/sort` -- then it wires to
  `manualZoneDrawEnabled_`, not to a new flag. Found by T414 Leg A.

- **B334** (B) `include/client/graphics/detail/detail_types.h` --
  **`DetailType::windHeightBias` has no vertex to act on.** The key was parsed
  per detail type and the field never read. `DetailChunk` builds a crossed quad
  with only BASE and TOP vertices and pushes `windInfluence_` = 0 at the base and
  `windResponse` at the top, so a "height at which wind is strongest" has no
  mid-height vertex to move. T414 Leg A deleted the parse and the field. Closes
  when the detail quad gains a mid vertex (or a vertex shader takes the bias as
  a uniform) and the key returns with a test. Found by T414 Leg A.

- **B335** (B) `config/weather_effects.json` `storm` -- **four shipped storm keys
  with no field at all.** `fogDensityMultiplier`, `fogColorR`, `fogColorG`,
  `fogColorB` and `lightningIntensityThreshold` were in the shipped file;
  `WeatherEffectsConfig::Storm` has no such members, and the loader's only
  acknowledgement was an empty `if (json.isMember("fogDensityMultiplier")) { }`
  whose body was two comments. So a storm has no configurable fog tint and
  lightning has no configurable intensity gate. T414 Leg A deleted the keys and
  the empty stub. Closes when the storm fog tint and the lightning threshold are
  real fields with consumers. Found by T414 Leg A.

- **B336** (B) `include/client/graphics/detail/detail_types.h`
  `ZoneDetailConfig::viewDistance` -- **the detail view distance in
  `detail_objects.json` is inert.** `globalSettings.viewDistance` (200.0 shipped)
  and the legacy root-level `viewDistance` both parse into the field, and
  `DetailManager` culls by `viewDistanceChunks_`, a hardcoded 2 (a 5x5 chunk
  grid). Not deleted with B297's siblings because the wiring is mechanical
  (`ceil(viewDistance / chunkSize)`) but NOT behaviour-neutral: 200/50 is 4
  chunks, a 9x9 grid, which changes both the visual gate and the frame cost.
  It hid from T407's census behind the reads of the two `viewDistance` fields
  T414 Leg A deleted. Closes when the chunk radius derives from the configured
  distance, with a measured before/after. Found by T414 Leg C.

- **B337** (B) `include/client/graphics/eq/race_model_loader.h`,
  `equipment_model_loader.h`, `include/client/graphics/tree_identifier.h` --
  **diagnostic aggregates filled and never printed.** `MemoryStats::
  loadedModelCount` / `variantMeshCacheCount` / `animatedMeshCacheCount` /
  `armorTextureCount` / `meshCacheCount` and `TreeIdentifier::Stats::
  treePatternCount` / `leafPatternCount` / `zoneTreeMeshCount` /
  `excludedMeshCount` are computed for reports that do not print them. Closes
  when each is printed (`/pmem`, the tree report) or dropped. Classified
  `report` in the census table. Found by T414 Leg C.

- **B338** (A) `include/client/graphics/constrained_texture_cache.h` --
  **two cache flags a caller sets and the cache ignores.** `bgThreadPool_` is
  handed a real `BackgroundThreadPool*` by a setter and read by nothing, while
  `renderer_zone_draw.cpp` carries a comment about "async background decode
  submits to bgThreadPool_"; `compressedTexturesAvailable_` is written four
  times by the ETC1 support probe and consulted nowhere. Same shape as the
  closed B291 (`skipEntityTextureUpload`): a caller supplies an intended value
  that changes nothing. Closes when both are consumed or both are deleted with
  their setters. Found by T414 Leg C.

- **B339** (B) `include/client/graphics/weather_effects_controller.h`
  `targetIntensity_` -- commented "For smooth transitions", written on every
  intensity change and on reset, read by nothing: weather intensity is applied
  outright, never interpolated toward a target. Closes when the transition
  exists or the member goes. Found by T414 Leg C.

- **B340** (B) `include/client/graphics/zone_lifecycle/zone_lifecycle_component.h`
  `entitiesLoaded_` / `expectedEntityCount_` -- **the header already documents
  them as write-only.** L09 deleted their zero-caller accessors and left the
  members and their four writes behind. Closes when they go with the writes, or
  a consumer for the zone-in entity gate appears. Found by T414 Leg C.

- **B341** (B) `include/client/graphics/overlays/overlays_component.h`
  `currentTargetInfo_` / `currentTargetLevel_` -- target-overlay state cached
  when the target changes and never read by the draw. The target outline and
  name plate get their data elsewhere. Closes when the overlay draws from them
  or they go. Found by T414 Leg C.

- **B342** (B) `include/client/inventory/inventory_manager.h` and
  `include/client/graphics/environment/particle_manager.h` -- **four registered
  callbacks that are never invoked.** `InventoryManager::moveItemCallback_`,
  `deleteItemCallback_`, `equipmentChangedCallback_` and
  `ParticleManager::entityDirCallback_` each have a setter a caller uses and no
  call site. A registered callback that never fires is worse than none: the
  registrant believes it is wired. Closes when each is called or the setter and
  member go together. Found by T414 Leg C.

- **B343** (B) `include/client/graphics/ui/character_model_view.h`
  `inventoryManager_` and `include/client/graphics/eq/animated_mesh_scene_node.h`
  `sceneManager_` -- stored collaborator pointers with no reader. Closes when
  each is used or dropped from the constructor/setter. Found by T414 Leg C.

- **B344** (B) `include/client/bridge/bridge_component.h`
  `m_audio_config_vendor_music`, `include/client/player/player_component.h`
  `m_duel_target_id`, `include/client/skill/skill_manager.h` `m_player_race` --
  three pieces of state stored at the point a decision is made and never
  consulted when the decision is taken: the vendor-music config reaches no audio
  call, `/duel` records the target id the reply path does not use, and the
  player race is stored for skill gating that does not gate on race. Closes per
  member. Found by T414 Leg C.

- **B345** (B) `include/client/graphics/camera_controller.h`
  `orbitAngleInitialized_`, `include/client/output/console_renderer.h`
  `m_curEnd` / `m_playerHeading` / `m_startTime`,
  `include/client/input/hotkey_manager.h` `ConflictInfo::binding1` / `binding2`
  -- write-only leftovers in three unrelated classes: an orbit-camera
  first-frame latch, three headless-console-renderer members, and the two
  binding strings a hotkey conflict report never formats. Closes per class.
  Found by T414 Leg C.

- **B346** (B) simulation/renderer per-frame payload fields with no consumer --
  `SimulationWorker`'s `EntityResult::shouldDeactivate` (set by the worker,
  ignored by the drain) and `SimulationInput::cameraTarget`,
  `EntityVisual::corpseYOffset` (computed per corpse, never applied to the
  node), `ParticleEmitter::spawnRadius_` (seven callers set it; the spawn does
  not use it), `SpellEffectInstance::spellID`, `RendererEvent::intData`,
  `SpellActionInfo::level2`. Same class as B296 one layer down: a producer
  computes a value for a consumer that never reads it. Closes per field. Found
  by T414 Leg C.

## Added 2026-09-06 by T415

- **B335** (B) `include/client/graphics/ui/tradeskill_layout.h`,
  `src/client/graphics/ui/tradeskill_panel.cpp` -- **the tradeskill window's
  whole LEFT side is not drawn: no recipe search, no known-recipe list, no
  favourites.** T415 Leg A drew the container half of
  `EQUI_TradeskillWnd.xml` (`COMBW_RightSideArea` -- the ten container cells,
  the container name, Combine, Close) and mapped it onto `centerPopup` from
  the oracle's own 400x525 frame, so the cells land where the official client
  puts them. The consequence is that the left 60% of fable's popup is empty:
  the oracle fills it with `COMBW_LeftSideArea`
  (`COMBW_SearchArea` -- `COMBW_MinTrivialEdit` / `COMBW_MaxTrivialEdit` /
  `COMBW_SearchTextEdit` / `COMBW_SearchButton`; `COMBW_RecipeListArea` --
  the `COMBW_RecipeList` listbox; `COMBW_FavoritesArea` --
  `COMBW_AddFavoriteButton` / `COMBW_RemoveFavoriteButton` /
  `COMBW_ShowFavoritesButton`).
  **The four opcodes it needs ALREADY EXIST and are already sent** -- checked,
  not assumed: `MerchantComponent` writes `HC_OP_RecipesFavorite`,
  `HC_OP_RecipesSearch`, `HC_OP_RecipeDetails` and `HC_OP_RecipeAutoCombine`
  (`src/client/merchant/merchant_vendor_bank.cpp:547/559/581/593`), reached
  from `CommandProcessor`. **What is missing is the CONSUMER**, and it is the
  T413 shape again one layer up: `ZoneProcessRecipeReply`
  (`merchant_vendor_bank.cpp:609`) parses `RecipeReply_Struct` -- recipe name,
  id, trivial -- and its whole body is a single `LOG_DEBUG`. The server's
  recipe list is arriving and being thrown away, so every recipe a search
  returns is already on the wire with nowhere to land. Closes when
  `ZoneProcessRecipeReply` publishes an event a recipe list on the popup's
  left side consumes. (A cheaper alternative, if Will would rather not have
  the feature: draw only the container half at the oracle's own proportion --
  a ~160x525-shaped popup -- so there is no empty region.) Also unimplemented from the same window and deliberately left as
  drawn-but-inert: `COMBW_ExperimentButton`, `COMBW_AutoInvButton`,
  `COMBW_UseCtrlCheckbox`, `COMBW_ContainerIcon` (fable's UI atlas has no item
  icon sheet), and `CAW_OpenCombatSkillSelectWnd` from the combat abilities
  window (fable has no combat skill select window). Found by T415 Leg A.

- **B336** (B) `src/client/graphics/ui/static_ui_input.cpp`
  `StaticUIInput::processKey` -- **ESC does not dismiss the tradeskill
  container window, and must not be made to naively.** `processKey()`
  dismisses the top centre popup (inventory, spellbook, skills, loot) and
  T415 deliberately did NOT add the container to that list: T290's rule is
  that the UI never closes itself, because an optimistic local close desyncs
  the server's "this container is open" state
  (`ctx_.active_tradeskill_object_id`, which also gates
  `InventoryManager::setWorldContainerOpen`). The correct ESC behaviour is to
  push `TradeskillCloseIntent` and leave the panel up until
  `TradeskillContainerClosed` comes back -- exactly what the window's Close
  button now does -- but `processKey()` has no established pattern for a
  dismissal that goes through the bridge, and the loot popup has the same
  latent question (ESC on an open corpse). Closes when ESC on either
  server-owned window sends its close intent instead of hiding the panel.
  Found by T415 Leg A.

---

## Added 2026-09-06 by T416

- **B350** (C) `tools/ui_atlas_builder.cpp` `generate()`, and every panel that
  draws `panel_background` / `slot_background` -- **since T411 the whole static
  UI is translucent, and the oracle's windows are opaque.** The atlas author
  gave `panel_background` alpha **180/255** and `slot_background` **200/255**,
  which nothing honoured until T411; now the 3D world shows through the chat
  window, the inventory popup, the loot list, the skills list and every panel
  background (`review/T416/crops/hud_all_3way.png`, `chat_3way.png`,
  `loot_3way.png`, `skills_3way.png` -- middle column is T411 as merged). The
  official client's inventory window is a solid dark plate
  (`review/T401/oracle/T401_oracle_inventory_window_crop.png`) and so is each
  IW_Money row
  (`review/T401/oracle/T401_oracle_purse_coinrows_crop4x.png`). Whether fable
  should match that, keep a deliberate translucency, or split the two (popups
  opaque, HUD panels translucent, which is what the official UI actually does)
  is a LOOK decision, not a defect -- one number per sprite in
  `ui_atlas_builder`, plus a pack rebuild and its two sha256 pins. Nobody has
  ruled on it, so T416 changed nothing here. Found by T416.

- **B351** (B) `include/client/graphics/ui/ui_atlas.h` `UISprite` -- **six of
  the twenty-one atlas sprite regions have no consumer at all.** A
  comment-filtered `UISprite::` census over `src/`, `tools/` and `tests/`
  finds zero uses of `ButtonNormal`, `ButtonPressed`, `ButtonDisabled`,
  `PanelBorder`, `ScrollIndicator` and `SlotBorderSelected`. Two of them have
  an obvious caller waiting: the loot popup's Loot All / Close buttons draw
  themselves with three `drawRect()` calls
  (`src/client/graphics/ui/loot_panel.cpp`, `drawButton`) instead of
  `ButtonNormal`/`ButtonPressed`, and the chat scrollbar thumb is a `drawRect`
  instead of `ScrollIndicator`. The regions cost atlas space and pack bytes
  either way. Closes when each is wired or removed from the builder, the JSON
  name map and the enum together. Found by T416.

- **B352** (B) `src/client/graphics/ui/static_panels.cpp` `renderPetPanel` and
  `include/client/graphics/ui/ui_layout.h` `petPanel` -- **the pet command
  button row is drawn OUTSIDE the panel and its last button is clipped away.**
  `petPanel` is `BAR_HEIGHT * 2 + MARGIN * 3` = 40px tall, and the row loop
  starts the buttons at y+34 with a ~14px line height, so the labels hang 8px
  below the frame (`review/T416/crops/pet_3way.png` -- visible in all three
  columns, so this pre-dates T411 and is not an alpha effect). The row also
  runs out of width: ten labels at ~24px pitch need ~240px in a 160px panel,
  and the `bx > right - 10` guard drops the tenth ("SH", Shrink) silently. The
  honest fix is the oracle's own `EQUI_PetInfoWindow.xml` shape (a grid of
  buttons, not a single text row), which is a layout decision -- hence B, not
  a one-constant edit. Found by T416.

- **B353** (P) `scripts/ci/visual_gate.sh` -- **proposal: the gate should
  photograph the UI.** Its three zones capture geometry only, which is why
  T411's atlas-alpha change moved no golden at all and needed T416 to look at
  it by hand. `tools/hwcaps/ui_panel_capture` now renders all sixteen panel
  captures headlessly in ~10 s with no server and no zone, so a `UiPanels`
  stage would be cheap; it would also add sixteen image goldens to the set
  Will has to bless on every UI edit. **The gate is Will's to extend** -- T416
  boarded this rather than doing it, per its own plan. Found by T416.
