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

- **B002** (A) `src/client/eq_action_handler.cpp` `EqActionHandler::toggleAutorun`
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

- **B005** (B) `src/client/ucs.cpp` / `include/client/ucs.h` `UCSConnection` — 413
  lines of live-looking code with zero callers anywhere in `src/`, `include/` or
  `tests/`; its entry point is commented out at its one call site in
  `EverQuest::WorldProcessSetChatServer`. T297 measured that cross-zone `ooc`,
  `auction`, `tell` and `guild` all work without it. Closes when: the TU, the
  header, the CMake entry and the commented connect path are deleted with a
  tombstone. Prov: deferred item 4 (`eq_ucs.cpp` was already deleted by T326;
  this is the remaining half).

- **B006** (B) the S2 leftovers — eight TUs include
  `include/client/graphics/irrlicht_renderer.h` and never name `IrrlichtRenderer`
  (`eq_bridge_ui_audio`, `eq_bridge_intents`, `eq_player_info`, `_misc`,
  `_social`, `_buffs`, `application_frame`, `application_config`);
  `EverQuest::InitGraphics` still takes an `IrrlichtRenderer*`; the asset loaders
  (`include/client/graphics/eq/pfs.h`, `include/client/graphics/eq/wld_loader.h`) still live in `EQT::Graphics`; and
  `door_manager.cpp` reaches for the static `EverQuest::GetDebugLevel()`. Closes
  when: each is removed by the K unit that owns its file. Prov: the S2 residue.

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

- **B013** (B) `src/client/graphics/renderer_slash_commands.cpp` `/newskills` —
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

- **B019** (B) `include/client/eq.h` `EverQuest::m_guild_name` — declared and
  default-initialised, **zero references anywhere** in `src/`, `include/` or
  `tests/` (K01 measured with `grep -rw` at `e888e89c`). The reachability
  pattern again: nothing parses a guild name off the wire into it and nothing
  reads it. K01 left it alone (structural unit, no drive-by fixes). Closes
  when: the member is deleted, or a guild-name source and reader are wired.
  Prov: K01 Review, `refactor/K01_guild_component.md`.

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

- **B019** (C) server-sent placeables and doors are measured by no gate — the
  benchmark's `placeableObjectsBytes` (`tools/hwcaps/zone_bench_memory.h`) is
  summed over the zone's own S3D archive and never connects to a server, so
  eqemu's `object` and `doors` tables are counted by nothing. Closing it needs a
  live-client memory harness that does not exist; the decision is whether to
  build one. Prov: the archived board's "SERVER-SENT PLACEABLES AND DOORS ARE
  UNMEASURED BY ANY GATE" section.

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
  (`command_processor_registration.cpp`), `EverQuest::ProcessBridgeIntents` 580
  (`eq_bridge.cpp`), `Application::initialize` 569 (`application.cpp`),
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

- **B030** (B) `include/client/eq.h` `EverQuest::m_swim_up` / `m_swim_down` with
  `SetSwimUp` / `SetSwimDown` — zero readers, zero setter callers. Closes when:
  deleted or wired to the swim path. Prov: K07 Review.

- **B031** (B) `include/client/eq.h` `EverQuest::m_last_movement_update` — zero
  references tree-wide. Closes when: deleted. Prov: K07 Review.

- **B032** (A) `src/client/movement/movement_component.cpp` `MovementComponent::StartMoveForward`
  / `StartMoveBackward` / `StartTurnLeft` / `StartTurnRight` — the keyboard
  movement START half has zero callers while the STOP half is called (T316
  recorded the keep; the switch has no on position). Closes when: the input
  path calls the Start* methods and a live keypress moves the player, or the
  cluster is deleted together with its Stop* half. Prov: K07 Review, T316.
