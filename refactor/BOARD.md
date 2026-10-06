# BOARD — the open-work queue of record

- **Tree confirmed at:** DO NOT QUOTE A HASH FROM THIS LINE -- there was one,
  `3808ebe9`, and it sat here unchanged for roughly sixty units while the item
  bodies below were confirmed against much later tips (T445 struck it, board
  B458). The board is edited by nearly every unit, so a hash stamped once is
  stale by the next merge, exactly like the suite count below. Get the tip the
  board actually describes with `git -C <slot> log --oneline -1` on `fable`.
- **Date:** built 2026-09-03 by K00 Leg A (`refactor/K00_truth_reset.md`) and
  amended by every unit since; `git log --format=%cs -1 -- refactor/BOARD.md`
  is the last edit.
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
- **Every item states the unit that confirmed it, and that unit's merge is the
  tip it was confirmed against; nothing here is quoted from an older board.**

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

- **B990** (A) → issue #309. The custom orchestral SoundFont `synthus2.sf2`
  (12,036,844 B on disk) is held in the client as **24,160,712 B (23.16 MiB) of
  float samples** (`tsf` holds float; `au.soundfont_custom`, Pi velketor steady
  `refactor/B977/audio_ledger_census.tsv` — the same 24,162,944 B on x86). It
  loads on the first `playMusic` of a Velious/Luclin XMI stem (T504 deferred
  it from startup to `MidiPlayer::selectSoundFont(Custom)`) and is never
  released: it is 84 % of the audio ledger's measured worst (velketor,
  28,720,320 B at steady_60s) and what makes the audio ratchet (35 MiB) refuse
  to move the 32 MiB budget down (B977, #295; docs/decisions.md 2026-10-05).
  The original client (32 MB total) never decoded its font to float. **This
  defect moves first**; a re-ratchet of `memoryLedgerBudgets.audio` follows
  Will's number when it lands. Filed by B977, class::A, lane::pi.
- **B989** (A) → issue #308. Every played WAV is held **twice** as 32-bit
  float PCM: `AudioManager::bufferCache_` (the `au.sound_buffers` decode,
  ~2.0–2.4 MB after the B977 tour's combat) and `SfxManager::cache_`
  (`au.sfx_cache`, `preload()`'s second copy of the same samples), each capped
  separately at the preset's `soundCache` (8,192 KB on orangepi) — up to
  16 MiB of duplicated decoded PCM the 1999 client did not carry twice.
  Measured: b977-audio x86 befallen `au.sound_buffers` 2,322,320 +
  `au.sfx_cache` 310,640 B at steady_60s (723,600 B at the shutdown and
  zone_change points; Pi befallen 2,320,376 + 723,456 B at steady_60s,
  `refactor/B977/audio_ledger_census.tsv` owner rows). Moves SECOND, after
  B990, in the audio re-ratchet. Filed by B977, class::A, lane::pi.

- ~~**B852** (A) **Nothing verifies that Kunark and Velious zones load.** The CI
  gate renders three classic zones and nothing else; no test, script or gate
  opened a single Kunark or Velious zone. Will, 2026-09-20: "velious and
  kunark zones are part of the willeq target, so we need to verify that
  content works." (docs/decisions.md; the zone set is the 118-zone T490
  census.) **Swept by the B852 unit (kanban t_c10e8f98,
  `task/kb-b852-zone-content-sweep`):** `scripts/ci/zone_content_sweep.sh`
  ran all 118 zones offline (one gles2_zone_benchmark process each, the gate's
  memory-coverage invocation, heaviest first, per-zone timeout + watchdog).
  Result [measured 2026-09-20, x86/llvmpipe]: 117/118 clean — **every** Kunark
  and Velious zone loads clean; the one failing classic zone, **soldunga**
  (EQE1 31-char name truncation), is **closed by B910/#100** (2026-09-21: the
  pack is v3 and the client's loader reads the v3 name table) — and **hole**
  (loads and renders fine, peak RssAnon 261.2 MB, 5.2 MB over the 256 MB
  Pi ceiling, `#101`). One issue per distinct failing line: **#100** (soldunga,
  EQE1-tree defect — loads clean without `--eqe1-path`) and **#101** (hole,
  memory). Fixes are those issues, not this card. The sweep is committed
  (`refactor/B852/` holds the 118-row TSV + summary; `refactor/B910/` (renamed
  from `refactor/B100/` by B908) holds the
  1506-pack/183,058-entry v3-aware name census + captured output for the soldunga
  closure), with a 3-zone ctest
  smoke (qeynos2/sebilis/iceclad, label `slow`) in the test job; how to run it:
  `docs/ci.md` "The zone-content sweep". Related: B813/#7 (gate zone set),
  ~~B853/#99~~ (live zone-in sweep — swept clean by B853, `refactor/B853/`:
  all 46 Kunark/Velious zones B852 proved load clean now reach
  `IsFullyZonedIn()` live with the server's own spawns and doors, 2026-09-21).~~
  **STRUCK (2026-09-28 audit-hygiene pass): everything under this card is
  closed.** #98 (this card) closed; #100 (soldunga) fixed by MR !27 (filed as
  B100 by mistake — the issue number, not the board number; renamed to B910 by
  B908, EQE1 v3 name table); #101 (hole) closed 2026-09-23 on post-T504 evidence —
  T504's rendered Pi run measured hole's peak at **116.4 MiB**
  (`/mnt/projects/fable/review/T504/pi/runs_cap/hole/samples.tsv`, max
  `rss_anon_kb` 119224 = 116.43 MiB), well under the 256 MB ceiling, against
  this card's original x86/llvmpipe 261.2 MB (pre-T504). **The 261.2 MB
  figure and this card's whole sweep predate T504's entity/archive/texture
  memory work; nothing in CI pins hole's (or any B852-swept zone's) memory
  number going forward** — a regression net for it, if wanted, needs a
  GitLab issue of its own.

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

- ~~**B944** (A) An eviction under the texture-cache ceiling destroyed the
  texture while holders kept raw pointers.~~ **DONE 2026-09-30** (branch
  `task/B944-texture-eviction`, issue #262, `refactor/B944/README.md`). The
  gate's qeynos2 golden run at a 20 MiB ceiling SIGSEGVed in
  `ZoneBench::textureGroupId` (the benchmark's region materials held evicted,
  freed textures), and the CLIENT did too: an ASan build on the shipped EQE1
  path read a freed texture in `RaceModelLoader::createAnimatedNodeWithEquipment`
  (a cached `EQAnimatedMesh` material) on the first frame after zone-in; where
  it did not crash the guards painted the placeholder for the rest of the zone.
  **Eviction now releases the GL storage and keeps the object; the driver's
  first bind restores it** (`COGLES2Texture` "Residency",
  `constrained_texture_cache_residency.cpp`), from the mapped EQE1 pack or,
  on the S3D path, an `ArchiveTextureSource` over the zone/objects/sky
  archives. The 20 MiB golden run now exits 0 with all three frames
  pixel-identical to the goldens; `clear()` also stops leaking every ETC1
  alpha companion. Regression net: `tests/test_texture_eviction_residency.cpp`
  (ASan-clean). No budget, golden or gate assertion changed.
- ~~**B947** (A) → issue #265. S3D-path character/equipment textures had no
  restore source, so their eviction destroyed them and `RaceModelLoader`'s
  cached meshes kept the freed pointer.~~ **DONE 2026-09-30** (branch
  `task/B947-char-texture-restore`, `refactor/B947/README.md`). Every
  decode-path upload now carries a `TextureOrigin` (the archive member the S3D
  loaders read it from, or the file) plus a size + FNV-1a-64 digest, and a
  restore re-decodes only those bytes; a GLES2 entry with no source is PINNED
  (resident, never evicted again), never destroyed -- the destroy path is left
  only for the EDT_NULL test driver. Live, ASan, 8 MiB forced ceiling, S3D
  path: the tip's cache had 273 (qeynos2) / 216 (gfaydark) fewer entries at
  unload than this branch's -- the textures its evictions destroyed -- and this
  branch ran 0 ASan errors, 0 pinned, 0 restore failures on S3D and EQE1.
  Nets: `tests/test_texture_eviction_character_restore.cpp` (a real cached
  animated human mesh below its working set re-binds pixel-identical; red and
  ASan heap-use-after-free with the destroy path forced) and
  `tests/test_texture_eviction_residency.cpp`.
- ~~**B954** (A) → issue #272. `COpenGLES2Driver::createScreenShot()` leaked
  its whole pixel buffer on every call (`createImageFromData(...,
  ownForeignMemory=true, deleteMemory=false)`; `CImage` frees only with
  `DeleteMemory`), reached by `RdpComponent::captureFrameForRDP()` every frame
  while an RDP client is connected.~~ **FIXED** (B958/B954 unit,
  `refactor/B958/README.md`): `deleteMemory=true`. Pinned by
  `tests/test_screenshot_leak.cpp` -- 16 screenshots at 512x384 grow glibc's
  in-use heap and the ledger by 0 B; 12,648,448 B with the old argument (red).
  `saveScreenshot()`, `COGLES2Texture::lock()/unlock()` and every tool caller
  checked: no leak of their own.
- ~~**B955** (A) → issue #273. GPU buffer memory on the Orange Pi (Lima GEM,
  shmem-backed, unmapped) is on no ledger row and outside `owned =
  RssAnon - gpu_driver`; this kernel's DRM fds publish no `drm-*` memory keys.
  Measured by B952: 200 impostor textures (52.4 MB of pixels) moved system
  Shmem +55,220 kB and `gpu_driver` +0.20 MB, RssAnon/RssShmem 0.~~
  **FIXED by B955** (`task/B955-gpu-buffer-ledger`, closes on merge): the
  `gpu_buffers` row (`include/client/core/gpu_buffer_census.h`, the `[memgpu]`
  line, `gpuBuffers` in the JSON) reads the client's GEM pages from its own
  memory cgroup (the Pi rigs launch it in one). B978 folded the row into the
  64 MB target: `owned` = RssAnon − gpu_driver + gpu_buffers (added only when
  the row is OUTSIDE RssAnon), and the `[memgpu]` line now ends
  `owned_rss_anon=<RssAnon-gpu_driver> owned=<B>` (the separate
  `owned_plus_gpu_buffers=` field is gone, its absence pinned by the tests). Pi
  `steady_60s`: qeynos2 36.98 / gfaydark 35.77 / growthplane 25.12 MiB; the
  impostor proof sees +15.35 MiB for B952's 200 at 128. `refactor/B955/README.md`.
- **B961** (A) → issue #279. The scanout framebuffers (CMA on the display
  device, 2 x 3,686,400 B per client at 1280x720) and the client cgroup's
  kernel bytes (1.1-1.9 MiB) are on no ledger row; the first is visible only
  in debugfs (root). Measured by B955.
- ~~**B962** (A, memory) → issue #280. The client's GEM buffers are ~2x its
  logical GPU tally on the Pi.~~ **FIXED** (`refactor/B962/README.md`): a
  tagged GEM census (`scripts/pi/lima_bo_census.c` + `gem_owner_table.py`)
  put every buffer on an owner row. The 2x was two GEM buffers per EQE1
  texture (the mip chain uploaded with the default filter in place: level 0
  alone, then the chain) plus an unused second GL context and a duplicate
  player-region node; fixed, qeynos2 `steady_60s` 36.70 -> 22.52 MiB. The
  `[memgpu]` tally counts texture storage and the depth/stencil; the Pi rig
  fails `gpu_unnamed_over_bound` above 10 MiB. Follow-ups B971 (#289), B972 (#290).
- **B971** (C) → issue #289. The window surface's D24S8 depth/stencil is
  3.52 MiB of GEM at 1280x720; its only stencil user (`/portal`) is off on the
  orangepi preset. 16-bit depth changes pixels: Will's call. Measured by B962.
  **FIXED** (MR !84, head de31969c, pipeline #510 green; `refactor/B971/README.md`):
  the surface follows `enableStencilBuffer` end to end (preset + shipped JSON
  false → D16/no-stencil; device `eglChooseConfig` driven by
  `params.Stencilbuffer`, all three x86 sites + the ARM DRM device); a
  one-way invariant in `loadJsonOverrides()` (portal occlusion on ⇒ stencil
  on, the only stencil user) keeps `/portal` ON on D24S8; the benchmark
  mirrors production's surface request. Will approved the x86 goldens in chat
  2026-10-05 (re-blessed per his 2026-10-01 ruling; run1 vs run2 0 px; VM mesa
  24.04.2→24.04.4 isolated with a D24S8-on-.4 baseline build — 0 px). Pi:
  sweep 1/1 pass (nollvm, snapshot byte-identical); the surface REQUESTS
  D16/no-stencil (`EGL: Config depth=24 stencil=0` — stencil honored) but the
  Mesa 25.2.8/Lima config set offers no 16-bit depth window config, so
  `logical_depth_stencil` stays 3,686,400 B on the Pi and the ~1.76 MiB saving
  is not claimed there; gate poses vs B967 Pi frames: 0 px in all 15 (stencil
  drop is pixel-inert on Lima's shared 32-bit word). ARM benchmark now
  published by `publish_arm_build.sh`; runner `scripts/b971/run_gate_poses.sh`.
- ~~**B967** (C) → issue #285. The player light is quantized at large world
  coordinates on BOTH GPUs: the mediump `vWorldPos` varying.~~ **FIXED**
  (`refactor/B967/README.md`, MR !74 **merged into fable `2c7d5be4`
  2026-10-02**; `202df783` + `98846a79`): the player-light
  vector is formed in the VS (FP32, pre-scaled 1/64) and the FS is branchless
  with an attenuation LUT indexed linearly in d (B968's formula, light 0 from R
  on). Will, 2026-10-01: "I agree, re-bless the 15 goldens" -- done, manifest
  approval block only. Pi vs new x86 goldens: 13 of 15 frames in the tens
  (timorous_f2 3,175 -> 15, iceclad_f2 335 -> 6); the gfaydark residual
  (409/55/162 px >32, present with the player light OFF too) is not the player
  light and cause not isolated -- filed as B980.
- ~~**B968** (C) → issue #286. Past 64 units the player light gets brighter with
  distance on a surface that faces it.~~ **FIXED** with B967 (MR !74, merged
  into fable `2c7d5be4` 2026-10-02): the LUT
  now carries the per-vertex zone-light term `A(d) = 1/(c + l*d + q*d^2)` over
  d in [64, R], R = the d where the denominator = 255 (lightstone 9: R = 336.38);
  the light is 0 from R on by construction.
- ~~**B969** (A, blocked on B967) → issue #287. timorous_f2: 3,175 px >32, Pi vs
  the x86 golden.~~ **CLOSED** with B967's re-bless (MR !74, merged into fable
  `2c7d5be4` 2026-10-02): pLv-in-the-VS takes
  timorous_f2 to 15 px >32 (bar met: tens). The FP16 step lines were B967's
  interpolated `vWorldPos`; `refactor/B969/README.md` has the isolation.
- **B980** (A) → issue #299. gfaydark gate
  frames differ Pi vs x86 by 409 / 55 / 162 px >32 (f0/f1/f2) with the player
  light ON -- and f0 and f2 are EQUAL to B960's player-light-OFF counts
  (409/54/162), so this is not the player light. Cause not isolated. Measured
  by B967 (`review/B967/pi/gate/gfaydark_after/`, `arm_bin/before_d3375b65`).
- ~~**B973** (A) → issue #291. Post-load entity builds are Green-gated
  (`FrameComponent::processFrame`, "1 unit per GREEN frame";
  `EntityRenderer::processOneEntityBuildStep`), and the Pi is rarely Green in
  a populated zone: a crowd spawned after zone-in stays placeholder cubes (0 of
  200 GM-spawned NPCs built in 4 min, qeynos2 1280x720; 28 of 200 at
  800x600). A zone reload builds them. Measured by B964
  (`review/B964/pi/crowd/`).~~ **FIXED** (MR !72, `refactor/B973/README.md`):
  the `EntityBuildScheduler` — the first unit always runs in every governor
  state, further units within the state's slice (Green 20 % / Yellow 12 % /
  Red 6 % of the target frame), texture uploads their own one-texture units,
  create folds NodeSetup (B966); Pi new200 182/200 built (legacy 1/200),
  backlog drained 0.7 s after the last spawn. Follow-ups: #301 (B982),
  #302 (B983), #303 (B984).
- ~~**B979** (A) → issue #298. An impostor capture is an unbounded unit:
  single first-draw captures of 30–62 ms land whole in one Orange Pi frame
  (B964's hitch bar not met: worst single-frame capture work 43–46 ms against
  the 33.3 ms frame budget; `refactor/B964/README.md` "Against the bar").~~
  **FIXED** (MR !99, `refactor/B979/README.md`): the slow first capture
  decomposed on the Pi into three one-time GPU costs (a 4 MiB page GEM
  allocation 14–16 ms, the scratch FBO 3–14 ms, and the first 3D draw into
  the FBO + its first readback, Lima's deferred rasterization — 21–77 ms).
  All three moved to the loading thread (pre-created resources + a 3D
  textured-mesh priming draw + readback + glFinish, one-time per session,
  no-op for `WILLEQ_BASELINE=1`), and the capture unit became a unit of
  B973's `EntityBuildScheduler` (kind `Impostor`). Re-measured on the Pi:
  worst single-frame capture work qeynos2 47.68→**12.06 ms**, gfaydark
  46.20→**23.11 ms**, growthplane 26.37→**22.64 ms** (all ≤33.3 ms); x86
  `impostor_pixel_compare pixel` unchanged (234 rows), scheduler test
  11/11, ctest 5538/5538.

## B — implemented-but-unreached, and hygiene needing a code change

- **B988** (A) → issue #307. Will 2026-10-05: "make the nobocache flag opt-in
  (off by default) and fix the stale docs". A DRM client now leaves
  `LIMA_DEBUG` untouched (Lima's BO cache on); `WILLEQ_LIMA_NOBOCACHE=1`
  restores B986's `nobocache`; renderer init logs the effective policy.
  Same-binary Pi A/B: `/mnt/projects/fable/review/CODEX-AUDIT-1005/README.md`.
  Expected consequence, guard unchanged: the 10 MiB unnamed-GPU guard reads
  red again on the crowd/zone-hop tours until B972 (#290) lands
  (`refactor/B986_pi_gpu_guard.md`, superseded section). Branch
  `task/B988-lima-bocache-opt-in`, in review.
- ~~**B986** (B) → issue #305. The Pi unnamed-GPU guard failure was attributed
  to Lima BO-cache retention and fixed by configuring the DRM client before
  device creation.~~ **MERGED** with B975 (MR !73, fable `ee538d2c`,
  2026-10-05). Its always-on `nobocache` default is **superseded by B988**
  (opt-in; `refactor/B986_pi_gpu_guard.md`).
- ~~**B975** (B) — Entity name tags → issue #293, MR !73.~~ **MERGED** (MR !73,
  fable `ee538d2c`, 2026-10-05; #293 closed). Compact glyph runs, bounded
  reusable GPU input storage and cached atlas coordinates; final head
  `bcf5335f`, QA run 284 PASS. Its Pi CPU acceptance (1914 / 1955 us) was
  measured with `nobocache`; under B988's default the tool is not re-measured
  at the final head (`refactor/B975/README.md`, Not verified).
- **B987** (B) → issue #306. A crowd tour can end before the final visit's
  rendered readiness and steady-state window (B975 QA run 283). MR !82
  (`task/B987-crowd-readiness`) is OPEN at `2eed34f3bf6ccd99a79ff17f490ebd56a381e21f`
  (pipeline 519 green). QA run 306 FAILED it on `--cap` validation (F1: an
  accepted spelling disabled the x86 deadline); the fix is in — ONE duration
  definition (plain ASCII decimal 0..86400) enforced before any lock/stack/Pi
  resource in both wrappers, the sweep itself and b973, with the executed
  x86-deadline regression (RED 3/8 on `e3078ac5`, GREEN 8/8 on the head).
  The fresh post-fix Pi acceptance pair (batched/per-node qeynos2,
  `WILLEQ_LIMA_NOBOCACHE=1`) passed; `refactor/B987_plan.md` carries the table.
  Awaiting review; NOT merged.
- ~~**B985** (B) → issue #304. `scripts/pi_rendered_zone_sweep.sh` cannot run
  from a host whose checkout and `--out` are not NFS-mounted by the Pi.~~
  **FIXED in code, merged with MR !73** (fable `ee538d2c`, 2026-10-05): the
  sweep now rsyncs the runner scripts, config, tour, data and binary to
  `$PI_BIN_DIR/inputs` on the Pi and rsyncs each run dir back
  (`scripts/pi_rendered_zone_sweep.sh`, "stage the capable client on the Pi"
  and `run_one`). GitLab #304 is still OPEN with no status label.

- **B976** (B) → issue #294. In timorous_f2, `--shader-debug 5` shows the
  bottom plane's normal as +Y on llvmpipe and G=0 on Lima, so the player light
  lights it on x86 only. It is nearly black in the gate frame. Found by B969.

- **B981** (B) → issue #300. `tests/goldens/manifest.json`'s
  `memory.textureCacheBytesLimit` is 67,108,864 (64 MiB) in all five gate zones;
  live it is 53,477,376 (51 MiB) since B943's 2026-09-30 ratchet. Display-only,
  never asserted; the gate passes with it stale. Will's 2026-10-01 re-bless
  ruling covered the 15 goldens and the approval block only, so the B967 unit
  left it unchanged and filed it. Bless-or-document is Will's call.

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

- **B948** (B) → issue #266, **in review** (`refactor/B948/README.md`).
  `gpuTextureBytes` counted every ETC1 alpha chain twice:
  `uploadCompressedETC1()` wrapped the main texture with main+alpha bytes
  and the `_alpha` companion with the alpha bytes again. Fixed: the main
  texture is now wrapped with the main chain only (the cache entry's
  `sizeBytes` stays main+alpha, and the B944 restore re-adopts the split).
  `gpuTextureBytes` moves DOWN only: coverage run per zone −87,432 …
  −1,399,528 B (qeynos2 9,322,992 → 8,973,400), `textureCacheBytesUsed`
  byte-identical; no gate baseline carries the field (re-checked), so no
  blessed number moved. Repeatable:
  `tests/test_texture_etc1_alpha_count.cpp` (driver figure = main + alpha
  once each, through the B944 eviction/restore round trip; a no-alpha
  control).
- ~~**B949** (B) → issue #267. The texture cache's LRU is touched on lookup and
  restore, never on draw, so eviction order is load order and an on-screen
  texture can be evicted first and restored on its next bind (thrash;
  performance, secondary). Measured by B944: 85 evictions / 21 restores on the
  qeynos2 golden run at 20 MiB.~~ **DONE 2026-10-03** (branch
  `task/B949-texture-lru-draw-order`, issue #267, `refactor/B949/README.md`).
  A cheap per-frame "used" stamp on bind (`COpenGLES2Driver::FrameCounter`
  bumped in `beginScene()`, stamped into each texture by
  `COGLES2Texture::markDrawn()` on the `getOpenGLTextureName()` bind accessor)
  and one per-frame reorder (`ConstrainedTextureCache::endFrame()`, splice-based
  so the per-entry `lruIterator`s stay valid) puts the LRU list in
  least-recently-DRAWN order; equal stamps keep load/touch order. Same gate
  invocation at 20 MiB: 85/21 → 84/20 evictions/restores, frames still
  pixel-identical (0 differing pixels). Repeatable:
  `tests/test_texture_lru_draw_order.cpp` (a texture bound every frame survives
  load-pressure eviction; a no-draw control still evicts in load order). No
  budget, golden or gate assertion changed.
- **B950** (B) → issue #268. `ZoneMeshBuilder`'s layout depends on
  `EQT_HAS_TEXTURE_ATLAS`, a PRIVATE define of `willeq_core`;
  `tests/test_placed_object_meshes.cpp` constructs one without it (B944's own
  test hit the resulting ASan stack-buffer-overflow and defines it). Confirmed
  by B944 for its own test; the other by construction.
- **B952** (B) → issue #270, **in review** (`refactor/B952/README.md`).
  Impostor billboards part 1: the benchmark's impostor row (`tex.impostor`),
  sizes 64/128/256, unique/shared, measured on x86 and the Pi; budget
  candidates proposed for Will (blessed 2026-09-30: 128^2, 20 MiB). Part 2,
  the production port, is **B964**.
- **B964** (B) → issue #282, **in review** (`refactor/B964/README.md`).
  Impostor billboards part 2, the production port: atlas pages of 128^2
  cells, the blessed cap, governor-driven tiers, scheduled captures, one draw
  per page, the `tex.impostor` owner row; on only on `orangepi`, off under
  `WILLEQ_BASELINE=1`.
- **B974** (B, *cause unconfirmed*) → issue #292. On x86 (llvmpipe) a 200-NPC
  crowd puts the client at 2.0-2.3 GiB RssAnon, ~2 GiB of it `gpu_driver`,
  with impostors on or off; the Pi stays at 21 MiB. Confirm by attributing the
  llvmpipe objects (per-texture resources vs fragment-shader variants).
  Measured by B964 (`review/B964/x86/crowd/`).
- **B956** (B) → issue #274. Benchmark impostors have a dark silhouette
  fringe: bilinear sampling against the capture's (0,0,0,0) clear with the
  built-in shader's hard 0.5 alpha test. Measured by B952 (the largest
  single-pixel difference is background-vs-near-black, 60, at every size). B958 replaced the
  built-in fwidth() threshold (x86) with the constant 0.5: the x86 fringe
  narrowed (80 units, 128^2: silhouette mismatch 6.1 % -> 4.1 % of the
  footprint), and the Pi now draws it too (5.0 %) -- not gone. The production
  billboard (B964) divides RGB by the sampled alpha: 13-19 % fewer dark-edge
  pixels than the built-in alpha test, still not gone
  (`refactor/B964/README.md`).
- ~~**B963** (B) → issue #281. On Lima the benchmark gives every static
  hardware buffer its own 4 KiB-minimum GEM BO (qeynos2 9,355 BOs = 36.5 MiB).~~
  **CLOSED by B962** on the question it asked of the client: the tagged census
  shows the client creates NO static vertex/index buffers in pooled mode, so
  it does not have that shape (`refactor/B962/README.md` section 5). The
  benchmark's per-buffer VBOs remain, so a Pi benchmark GPU-memory figure is
  still not the client's -- read the client's `[memgpu]` row instead.
- **B972** (B, memory) → issue #290. Mesa's stream-upload buffers and Lima's
  draw-time BOs hold 3-6 MiB of the client's GEM at `steady_60s`, driven by
  client-array draws (2D UI, entities, zone pools, raw-GL particles). A static
  VBO for the zone pools measured worse (B962). Measured by B962. **2026-10-06
  split (Will, option 2, verbatim: "proceed with option 2"):** the 10 MiB
  `gpu_unnamed_steady` crowd-tour guard HOLDS and is not re-scoped. #290 (MR
  !94) ships only the self-contained, pixel-identical cuts (~3-6 MiB: impostor
  pages, the untagged raw-GL particle draw, optionally the T462 `UiQuadBatch`
  vertices); the zone-merge + entity-aux pool that keeps the guard red moves
  to **B997 (#317)** — bounded-VBO conversion (B975 pattern), NOT the rejected
  static VBO (B962). The guard reads red on the crowd tours until B997 lands
  (B988/#307 known condition).
- **B997** (B, memory) → issue #317. The B972 split-off: the zone-merge pool
  (6 × 1 MiB `u_upload` `draw:zone` slots, ~38 client-array drawCalls/frame,
  `renderer_zone_merge.cpp`) + the remaining `draw3d:client` pool hold the
  residual above the 10 MiB `gpu_unnamed_steady` guard (measured 2026-10-06:
  qeynos2 18.90 / gfaydark 24.5 MiB cache-on imp200far, the impostor proven
  NOT the lever by the `off200far` control — the B972 README in
  `refactor/B972/` on the `task/B972-mesa-stream-buffers` branch, MR !94).
  Conversion to bounded reused VBOs (partial `glBufferSubData`, the merged
  B975 `NameTagBatchNode` slack pattern); a static VBO is the measured
  anti-pattern (B962: +4.3 MiB, half qeynos2 fps). Cut behind #290/MR !94
  (re-measure the residual after it lands).
- ~~**B970** (B) → issue #288. `build-arm` fetched `irrlicht-1.8.5.zip` and
  `libpng-1.6.37.tar.xz` from downloads.sourceforge.net inside a Docker layer, so
  the 2026-10-01 SourceForge outage (HTTP 522, ~3 h) failed every job whose
  `docker/irrlicht-drm/*` change invalidated it.~~ **FIXED**
  (`refactor/B970/README.md`): both archives live in the NFS store
  `ci-arm/vendor/`, SHA256-pinned in `docker/vendor.sha256`, filled once by
  `scripts/ci/vendor_fetch.sh --ensure` and handed to the build as the `vendor`
  build context. The other Dockerfiles (`Dockerfile`, `arm64-noble`,
  `arm-cross`) still wget from SourceForge; none is in CI.
- ~~**B958** (B) → issue #276. Benchmark impostor billboards rendered inverted
  on Lima (Orange Pi).~~ **FIXED** (`refactor/B958/README.md`): Lima runs a
  `discard` whose comparison operand depends on a derivative INVERTED, and the
  built-in AlphaTest3D used `clamp(0.5 - fwidth(a), 0.1, 0.5)` whenever
  `GL_OES_standard_derivatives` was exposed. Every device now compiles the
  constant 0.5 threshold (as the zone shaders already did). Measured with
  `tools/hwcaps/gles2_alpha_test_probe` on both Pi Mesa builds; the B952 pixel
  test at 80 units, 128^2: interior/full 0.7 % -> 97.6 % on the Pi (x86 97.3 %).
  Follow-ups: #277 (B959), #278 (B960).
- ~~**B959** (B) → issue #277. The GLES2 program-binary cache was written to
  `config/shader_cache` relative to the CWD.~~ **FIXED** (`refactor/B959/README.md`):
  the host injects `eqt::ResolveShaderCacheDirectory()` through
  `eqt::ConfigureGles2ShaderCache()`; no directory = cache off. Pinned by
  `test_shader_cache_path` and two `DataPathRouting` censuses. Found by B958.
- ~~**B960** (B) → issue #278. Which production draws reached the built-in
  AlphaTest3D on the Pi before B958.~~ **ANSWERED** (`refactor/B960/README.md`):
  the inventory paperdoll for a model with a WLD-masked material (measured: the
  Iksar head, 59 draws per inventory session; Pi before/after 43-86 px >32 per
  viewer angle, after = x86), and one frame per newly built masked entity
  (B966). The world otherwise: 0 draws, and the 15 gate frames are bit-identical
  before/after on the Pi. Instruments: `WILLEQ_ALPHATEST_FWIDTH=1` (pre-B958
  test knob), `WILLEQ_ALPHATEST_TRACE=1`. The dead fwidth() zone variants were
  deleted. Follow-ups: #283 (B965), #284 (B966).
- ~~**B965** (A) → issue #283. The per-pixel player light was blown out on
  Lima.~~ **FIXED** (`refactor/B965/README.md`, `tools/hwcaps/gles2_player_light_probe`).
  Mali-400's FS ALU is FP16 even under `highp`, so `d2 = dot(pLv, pLv)`
  became +inf past 256 units. Lima's LINEAR sampler read the LUT's near end for
  that infinite coordinate. The LUT coordinate is now `clamp(d2 * uLUTScale, 0, 1)`,
  and the x86 goldens are pixel-identical. Pi gate frames, >32 px vs the golden:
  - gfaydark 23,842 / 202 / 4,531 → 429 / 66 / 163;
  - iceclad 19,495 / 32,245 / 6,904 → 2 / 29 / 335.

  The residual iceclad_f2 band edges are B967, closed with the B967 re-bless
  (MR !74). Follow-ups closed: #285 (B967), #286 (B968), #287 (B969); #288
  (B970) closed separately; the gfaydark Pi residual is #299 (B980).
- ~~**B966** (A, minor) → issue #284. A newly built entity draws one frame with
  unpromoted materials (driver built-in Solid3D / AlphaTest3D):
  `SceneNodeCreation` adds the node and `NodeSetup` promotes it a frame later
  (`entity_build_step.cpp`). Measured on the Pi: `a_whiskered_bat` in qeynos2.
  Found by B960.~~ **FIXED by B973** (MR !72): the create unit runs NodeSetup
  in the same unit, so the node is never drawn with its raw materials.
- **B970** (B) → issue #288. CI `build-arm` cannot build the ARM Irrlicht
  layer while downloads.sourceforge.net is down. `docker/Dockerfile.arm-noble`
  wgets the Irrlicht zip and libpng; HTTP 522 for all of B965's session (job
  1926). Fix: vendor both archives with pinned sha256. Found by B965.

## C — needs Will's decision

- **B017** (C) `src/client/guild/guild_component.cpp` → issue #5
  `eqt::guild::GuildComponent::SendGuildInviteAccept` —
  prints "You have declined the guild invite." on a path the server processes as
  an accept. T318 confirmed the decline is **unfixable client-side** (the server
  tests an already-remapped value). The open half is user-facing wording, and no
  copy should be invented. Prov: deferred item 37.

- ~~**B018** (B) first-person eye placement — `refactor/T301_first_person_eye_placement.md`
  is written and not dispatched; T309 landed the measured draw rule
  (`src/client/graphics/entity_first_person_draw.cpp`) but the eye sits at the
  static bbox crown, and Will judges the look on the live gate. Prov: deferred
  item 16.

  **WILL 2026-09-07:** no judgement needed: capture the official first-person frame at the same spot and match it. Reclassified B. → T431 (lock).~~
  **CLOSED by T431 (2026-09-07).** The "static bbox crown" half was already
  stale — T301 Leg B replaced it with the head-bone vertex cluster and T310 Leg C
  solved the fraction against the oracle. What T431 did is the thing the entry
  was actually asking for: re-capture the official client's own first-person
  frame at the same spot, heading, race and resolution, and match it.
  `kEyeHeightFractionOfSkull` **0.65 → 0.45**, `kCrownAboveEyeUnits`
  0.49 → 0.765, `kEyeHeightFractionOfStandingHeight` 0.925 → 0.887. The Human
  male eye now lands at the measured **6.02 ± 0.08 above its own feet** (it was
  6.30). T310's 0.65 was not wrong when it was taken: it was stated as an
  ABSOLUTE and T310 Leg B's own `collisionZOffset` fix then raised every eye
  0.119 units off the ground without anybody re-solving the fraction keyed to
  it. B399's heading fix is also confirmed live here — fable at `character_data`
  heading 160 now reproduces the official client's heading-160 frame, where T310
  had to use 480. Evidence, curves and error bars: `review/T431/README.md`;
  tests `HumanMaleEyeMatchesTheT431OracleSolve`,
  `DwarfMaleEyeIsUnderTheT431OracleUpperBound`,
  `FpTunablesTest.TheDefaultsAreTodaysMeasuredNumbers`. **The second race is a
  NEGATIVE and it is B470, not part of this closure.**

- **B470** (B) `include/client/graphics/first_person_eye.h` — **ONE GLOBAL → issue #113
  `kEyeHeightFractionOfSkull` CANNOT SATISFY EVERY RACE, MEASURED.** T431 solved
  the parameter against the official client for two races at the same spot and
  method: **Human male 0.43–0.45** (eye 6.02 ± 0.08 above its own feet) and
  **Dwarf male 0.08–0.10** (eye 3.58 ± 0.14). The shipped value is the Human
  male's, because that is the race the whole first-person arc was measured on,
  which leaves the Dwarf's eye about **1.0 units high**. The cause is visible in
  the derivation: the Dwarf's eye moves 2.74 units per unit fraction against the
  Human's 1.40, because its beard is skinned to `dwmhe` and roughly doubles the
  span the fraction is taken across — the symmetrised bound `max(minZ, -maxZ)`
  tames it but not enough. **The fix is NOT a per-race table** (that header
  exists partly to forbid one, and the derivation is already per-race from the
  mesh): re-derive the skull bound and re-check BOTH races against T431's
  frames. Pinned as a negative by
  `ShippedMeshEyeTest.OneSkullFractionCannotSatisfyBothMeasuredRaces` so it
  cannot be reported closed on arithmetic. Prov: T431 Review §3;
  `review/T431/sweeps/dwarf_eye{,2}/curve.tsv`.

- **B471** (B) `include/client/graphics/first_person_eye.h` → issue #114
  `kSitEyeDropFractionOfHeight` — the SEATED eye has never been A/B'd against
  the official client, and T431 widened the disagreement it inherited. T301
  Leg A reports the sit drop two ways that are consistent only if the standing
  height is 7.9: **3.6 units**, and **0.925 H standing → 0.47 H sitting**. T431
  re-solved standing to 0.887 H, so the applied drop is now 0.417 H = **2.78
  units** on the Human male mesh against Leg A's reported 3.6 — a 0.82-unit gap
  where it was 0.56. The expression is `(standing - 0.47)` deliberately, so the
  SEATED absolute stays where Leg A measured it, but nobody has checked that
  absolute against a frame. Closes when the seated eye gets the same live sweep
  the standing eye just had (the rig is `review/T431/scripts/` plus
  `scripts/camera_match_sweep.sh`; the official client's `/sit` works under it —
  `review/T431/probe/T431_sit_dwarf_sitting_h32.png`). Bounded by
  `FirstPersonEyePosture.SitDropIsAboutThreeAndAHalfUnitsForAHumanSizedBody`.
  Crouching and Lying are still unmeasured and still reuse the sit drop.
  Prov: T431 Review §2.

- **B472** (P) **THE CANONICAL MEASUREMENT SPOT IS CONTAMINATED BY A PLAYER
  CORPSE, and it is invisible to a tall race.** `character_corpses` id 42,
  Oraclealpha, qeynos2 `(-72.09, 427.83, 3.752)`, `is_buried=0`, dead since
  **2026-09-05 14:44:55** — 1.9 units from the `(-74, 428)` that T310, T361,
  T377, T386, T401, T405, T422, T426 and T428 all measure at. At level pitch the
  down-FOV is vFOV/2 = 24.6 degrees, so ground-level geometry at horizontal
  distance `d` is only in frame while `d > 2.18 x eye height`: a Human male eye
  at 6.02 needs 13.1 units of clearance and never sees it, a Dwarf at 3.58 needs
  7.8 and sees it fill **8–27 % of the frame**. It cost T431 two whole sweeps
  and was identified only from the corpse's own nameplate in a `/sit` frame.
  T431 buried it for its session and **restored `is_buried=0` as found**, so it
  is still there. A second corpse, **id 44** (Oraclealpha, `(-74.08, 427.53)`,
  2026-09-07), is T431's own — the rig character was killed at the spot by the
  wandering NPC *Nixx Darkpaw* — and is left `is_buried=1`. Closes when the
  measurement spot is either cleared (bury both) or moved, and the rig records
  which. Prov: T431 Review §4; `review/T431/probe/T431_sit_dwarf_sitting_h32.png`.

- ~~**B473** (A) **fable draws a player corpse STANDING UPRIGHT; the official
  client lays it down.** Same server row (`character_corpses` id 42), same
  minute, same spot, same resolution: the official client's first-person frame
  shows a body lying flat — a face and a forearm horizontal across the bottom of
  the frame (`review/T431/probe/T431_sit_dwarf_sitting_h32.png`) — while fable's
  frame at the same pose is filled top-to-bottom by an upright model, shoulder
  and hair, from a corpse two units away
  (`review/T431/sweeps/human_eye/eyeHeightFractionOfSkull_0.65_/BEST_frame_000051.png`,
  captured before T431 buried it). fable evidently spawns a corpse as an
  ordinary standing entity with no death pose. Closes when a corpse entity
  renders prone and a test pins the pose selection. Prov: T431 Review §0.~~
  **CLOSED by T454 (2026-09-07), and the cause was NOT the spawn path.** The
  pose is identified without inference: T346 measured the official client's own
  wire answer to its own death -- `OP_SpawnAppearance` type 14 (AT_ANIMATION)
  param **115 = ANIM_LYING**, byte-identical in all four self-death windows of
  T311 Leg A's capture -- and T431 photographed the result. fable renders that
  as the LAST frame of `d05`, held (`playAnimation(d05, loop=false)` then
  `SkeletalAnimator::setToLastFrame()`). T454 **measured** that path and it is
  correct: a Human male corpse built through the production `registerEntity()`
  + `buildEntityMesh()` route comes out `d05`, state Stopped, frame 12 of 13,
  progress 1.000. Nothing had ever tested it. The live defect is one hop
  later, in `EntityRenderer::updateEntityAppearance()`
  (`src/client/graphics/entity_lifecycle.cpp`), reached from the
  `EntityAppearanceChanged` bridge arm: it rebuilds the entity and then
  restores `currentAnimation` with `setEntityAnimation(..., loop=true, ...)`,
  which for a corpse restarts `d05` at **frame 0 -- the STANDING pose -- and
  loops the character falling over forever**. Measured before the fix: state
  Stopped / frame 12 became state Playing / frame 0. The restore is now
  skipped for a corpse, because the rebuild has already established the held
  pose and there is nothing to restore. Pinned by
  `CorpsePoseTest.AnAppearanceChangeDoesNotStandTheCorpseBackUp`
  (`tests/test_corpse_death_pose.cpp`, red before / green after), with three
  wire pins and two preservation pins beside it. **The live A/B against the
  oracle is OWED** -- T449 holds the eqemu lock and T454 opened no connection;
  see the T449 follow-up list. Player and NPC corpses are treated identically
  and that is measured, not assumed: eqemu writes `ns->spawn.NPC = 2` for
  every corpse of either kind
  (`/mnt/projects/akk-stack/code/zone/corpse.cpp:1809`), so the two are
  indistinguishable on the wire.
- **B040** (C) server-sent placeables and doors are measured by no gate — the → issue #6
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

- ~~**B021** (C) `scripts/ci/visual_gate.py` `untrackedBytes` — emitted and
  baselined but deliberately not asserted; gating it needs repeat-run data from a
  second machine first. Prov: archived board, T69.~~ **STRUCK by T491
  (2026-09-17), on Will's 2026-09-14 ruling ("I want 100% accounting of memory
  use and I want it controlled" — `docs/decisions.md`).** The question is not
  "gate the T69 estimate gap" any more; the estimate gap is superseded.
  `include/client/core/memory_ledger.h` attributes memory AT THE ALLOCATOR
  (malloc/operator-new interposition, whole glibc chunk charged to the calling
  thread's `MemoryScope` category) and `scripts/ci/visual_gate.py`
  `assert_memory_ledger()` now asserts, on the three golden zones AND the
  memory-coverage run: (1) bytes charged to NO category ≤ 1.0% of the ledger
  — **measured 87,456 B, 0.040–0.087%**; (2) the ledger covers every resident
  heap byte (`ledgerTotal ≥ smaps [heap] anon` — measured true on all four
  runs, 100.6–216.4 MB vs 59.2–178.7 MB); (3) non-heap anonymous RSS ≤ 24 MiB
  — **measured 15.6–15.8 MiB, and it is llvmpipe** (libLLVM JIT 6.6 MiB,
  ~298 unnamed anon mappings 6.7 MiB, libgallium 1.15 MiB), none of which
  exists on Mali; (4) allocator slack ≤ 35% of RssAnon (measured 8.6–12.6%);
  (5) each category against `memoryLedgerBudgets` in the orangepi preset.
  The ONE thing T69 asked for that is still open is the Pi-side number, which
  is T490 Leg B's, not a gate defect — see B812 below.

- ~~**B812** (B) The memory ledger's budgets and bounds are **x86/llvmpipe
  numbers** (T491, 2026-09-17). `startup`, `textures_driver` and
  `irrlicht_scene` deliberately carry NO budget in the orangepi preset ... one
  `/pmem` dump and one `gles2_zone_benchmark --memory-json` on the Pi at
  zone-ready would close it.~~ **STRUCK by T496 (2026-09-17): the Pi has now
  been measured, four runs, `review/T496/pi/table_categories.tsv`.** The x86
  figures do NOT transfer, and the direction is the opposite of what a budget
  derived from them would have assumed:

  | category | x86 (T491, llvmpipe) | **Orange Pi (T496, `ready`)** |
  |---|---:|---:|
  | `textures_driver` | 30.50 MiB | **0.56–0.67 MiB** |
  | `irrlicht_scene` | 25.4–27.9 MiB | **1.11–1.45 MiB** |
  | `startup` | 40.5–49.7 MiB | **26.82 MiB** |
  | non-heap anon | 15.8 MiB (llvmpipe JIT) | **5.0–52.2 MiB, and it is glibc's mmap'd malloc region, not the driver** |

  Two of the three are **40–50x smaller on Mali than on llvmpipe**, so budgeting
  them from x86 would have shipped a ceiling 40x too loose rather than a
  fiction in the tight direction. `startup` is stable to **0.003 % across all
  four runs** (28,125,352 / 28,126,096 / 28,125,480 / 28,125,496 B), and
  `textures_driver`/`irrlicht_scene` vary only with the zone, so **all three are
  stable enough to budget** — boarded as **B818** rather than set here, because
  moving `config/constrained_presets.json` is a budget change and this was a
  measurement leg. The `[memmap]` half is answered too: the Pi's non-heap anon
  is `[anon]` (30–34 mappings, 50.5 MiB at qeynos2 `ready`), which is glibc
  servicing large mallocs by `mmap`, **not** a Mali/DRM mapping — the ledger
  charges every byte of it. The *second* half of B812 (hook cost on ARM) is
  **NOT closed**: see B818.

- **B813** (A, gate coverage struck by B813 / t_9bab9dd4, 2026-09-20; resolves T490's B803 origin entry) `s3dZoneSourceMemoryBytes` did not describe the → issue #7
  worst zone. ~~24 MiB~~ **→ 32 MiB in T491 (2026-09-17)**: the T293 figure was
  extrapolated from the three golden zones, and T490 then MEASURED iceclad's
  S3D zone source at **24.55 MiB**, over the old budget on its own. Re-derived
  on that measured worst case × 1.25 = 30.7 → 32 MiB, in
  `include/client/graphics/constrained_renderer_config.h`,
  `config/constrained_presets.json` and the compiled-in `fromPreset` default,
  pinned by `test_constrained_renderer.cpp` and pinned EQUAL to the ledger's
  `zone_parse` budget (they bound the same bytes by two methods).
  **Struck 2026-09-20 (B813):** the gate now RUNS a Velious zone (iceclad)
  and a Kunark zone (timorous) alongside the three classic zones, so every
  ledger budget applies to both in CI. **OPEN FINDING from B813 (Will's
  number to move, not an executor's):** on the two new zones the allocator
  ledger charges more than the T491 budgets derived from the three small
  classic zones — iceclad `zone_parse` 32.3 MiB vs the 32 MiB budget (1.08%
  over its own named worst case), iceclad `zone_mesh` 2.1× and `spatial` 2.1×
  budget, timorous `zone_mesh` 2.5× and `spatial` 2.8×. Evidence:
  `refactor/B813/README.md`.
  **2026-09-28 — the budget raise is REVERSED (Will).** Commit e3a14c2a
  raised `zone_parse` 32→33 MiB, `zone_mesh` 28→29 MiB and `spatial` 4→12 MiB
  (and 6eb9b454 followed in `fromPreset` + `test_constrained_renderer.cpp`,
  plus `s3dZoneSourceMemoryBytes` 32→33 MiB) citing a 2026-09-23 "ruling" Will
  confirms he never made. All four values are back at their pre-e3a14c2a
  numbers (branch `task/B813-budget-revert`; `docs/decisions.md` 2026-09-28).
  Budgets ratchet DOWN only. The five-zone gate stays, split in CI:
  `gate` (blocking) = qeynos2/gfaydark/befallen, `gate-kunark-velious`
  (`allow_failure: true`) = iceclad/timorous, same assertions, same budgets
  (`docs/ci.md`). **Four OPEN DEFECTS, fix in code — NEED GITLAB ISSUES
  (not filed by the reverting card).** Live bytes measured 2026-09-28 on the
  revert branch (`refactor/B813/README.md` "2026-09-28"):
  1. ~~**iceclad `zone_parse` over budget**: 33,925,296 B vs 33,554,432
     (+1.11 %).~~ **FIXED 2026-09-29 (B903, #223): 31,673,040 B (94 %).**
     One heap block per BSP region (37,609) + a doubling-grown handle array;
     now 1,024-region slabs (`BspRegionPool`, `zone_bsp.h`) and an exact
     reserve. 55 % of what remains (18.8 MB) is per-region source geometry the
     CLIENT frees at Step 13 but the benchmark never did -- released since
     B914 (#5 below): 11,329,984 B (34 %).
  2. ~~**iceclad `spatial` over budget**: 8,682,512 B vs 4,194,304 (2.07×).
     e3a14c2a's message did not list this one; its 12 MiB raise covered it
     silently.~~ **FIXED 2026-09-28 (branch `task/spatial-budget-overrun`):
     2,786,288 B (66 %).** 99.9 % of the category was the software-occlusion
     occluder table (one hash node + one heap vector per region, and the
     benchmark's copy of the loop kept each vector's pre-truncation capacity),
     not BSP or collision data. See #4 and `refactor/B813/README.md`
     "2026-09-28 — spatial fixed".
  3. ~~**timorous `zone_mesh` over budget**: 29,415,360 B vs 29,360,128
     (+0.19 %).~~ **FIXED 2026-09-29 (B904, #224): 23,586,688 B (80 %).**
     Irrlicht `core::array` push_back growth slack kept in every finished mesh
     buffer; `finishMeshBuffer()`/`finishMesh()` (`zone_geometry.h`) fit them.
  4. ~~**timorous `spatial` over budget**: 11,606,448 B vs 4,194,304 (2.77×).
     With #2: the BSP/PVS structures scale with region count (iceclad has
     37,609 regions), and the budget came from zones that do not exercise
     that term. The fix is a change of representation, not of the number.~~
     **FIXED 2026-09-28 with #2: 3,019,840 B (72 %).** It was a change of
     representation: `RegionOccluderTable` (`software_occlusion_culler.h`)
     holds a zone's occluders in three flat arrays, and both zone loaders
     select them through one body (`region_occluders.h`);
     `test_region_occluders` pins 40 B/triangle + 8 B/region and the shared
     call. With #1 and #3 fixed (2026-09-29) iceclad and timorous are back in
     the one blocking `gate` job and `gate-kunark-velious` is gone.
  5. ~~the benchmark never runs the client's Step-13 release
     (`releaseTexturePixelData`, `releasePostLoadData`,
     `releaseRegionGeometry`), so its `zone_parse` holds a second copy of every
     region's source geometry the client drops.~~ **DONE 2026-09-29 as B914**
     (Will approved; see the B914 entry below).

- ~~**B914** (A) The gate benchmark never ran the client's Step-13 zone source
  release, so the gate measured region source geometry the client frees at the
  end of a zone load.~~ **DONE 2026-09-29** (branch
  `task/B914-bench-region-release`, Will approved the change the same day). The client's four
  inline releases are now one function, `S3DZone::releaseSourceAfterLoad()`
  (`s3d_loader.h`/`.cpp`), called by `loadZoneStep13_Cleanup()` and by the
  benchmark's `loadZone()` at the equivalent point. Ledger `zone_parse`
  iceclad 31,673,040 → 11,329,984 B (94 % → 34 %), timorous 25,877,552 →
  5,529,168 B. All image goldens are bit-identical and every metric and p95
  check passes. **No manifest field moved:** `s3dZoneSourceBytes`/`totalEstimate`
  were never blessed into `zones[].memory`, so B903's "lowers the exact-match
  baselines" did not apply. No budget changed. `test_zone_source_release`
  pins the shared call. Full table: `refactor/B914/README.md`.
  Unnumbered follow-up (B, *measured*, needs a GitLab issue):
  `WldLoader::releaseRegionGeometry()`'s return value, which the client logs
  at Step 13, counts the shared `materialData` once per geometry. iceclad
  reports 63.5 MB, but `s3dZoneSourceBytes` fell only 16.7 MB for the whole
  release.

- ~~**B943** (B) Six orangepi budgets sat far above anything measured.~~ **DONE
  2026-09-30** (branch `task/B943-ratchet-six-budgets`, issue #261,
  `refactor/B943/README.md`, `docs/decisions.md` 2026-09-30). Will: "lower the
  seven budgets, hold audio". `archives` 4 -> 3, `textures_decoded` 20 -> 2,
  `entity_models` 84 -> 38, `gpuTextureBytes` 64 -> 38, `textureCacheBytesUsed`
  (`textureMemoryBytes`) 64 -> 51, `characterModelsBytes` 40 -> 2 MiB (measured
  worst x 1.25, whole MiB; `refactor/B943/budget_ratchet_census.py`). The
  first derivation (21 and 20 MiB, from the EQE1-path census) was wrong for
  the two texture budgets: the gate's qeynos2 golden run is S3D-path and holds
  31.7 / 42.2 MB, and 20 MiB crashes it (B944, #262). **`audio`
  held at 32 MiB** on Will's word: its only Pi figure (25.7 MB) is a 2026-09-17
  qeynos2 reading that predates T504, and the rendered sweep had no classic
  zone. `textureMemoryBytes` is the real cache eviction ceiling, so that one is
  a behaviour change; Pi behaviour under 51 MiB was unverified here and is
  verified by B978 (51 -> 21 MiB, #296): 0 placeholder / pinned / restore-fail
  WARNs on Pi qeynos2 + gfaydark, the gate's qeynos2 run pixel-identical at a
  20 MiB ceiling (B944) and 21 MiB (B978's local gate run).

- ~~**B927** (B) Two of Will's 2026-09-29 rulings had not been applied: "bless
  iceclad and timorous" and "ratchet the zone_parse budget down".~~ **DONE
  2026-09-29** (branch `task/B927-bless-and-zone-parse-ratchet`,
  `refactor/B927/README.md`, `docs/decisions.md` 2026-09-29).
  (1) The memory-coverage `characterModelsBytes` baselines iceclad 1,522,794
  and timorous 1,465,276 are blessed as they stand. The manifest `note` no
  longer attributes them to the 2026-09-23 bless that never happened
  (e3a14c2a).
  (2) `zone_parse` and `s3dZoneSourceMemoryBytes` go from 32 MiB to 14 MiB, and
  the B803 pin holds. Derivation: the worst figure after the end-of-load
  release, taken over a 118-zone census (`zone_parse_census.tsv`), the x86
  client and the Pi client, is 11,714,736 B (x86 client, iceclad) × 1.25 →
  14 MiB. The gate's iceclad reading is 77 % of the new budget.
  **Findings, filed 2026-09-29:** (a) = B928 → issue #246; (b) = B929 → issue #247; (c) = B930 → issue #248; the gate's host-dependent slack check = B931 → issue #249.
  (a) the gate asserts only the end-of-run point, while mid-load `zone_parse`
  reaches 31.7 MB live and a 50.1 MB high-water mark in iceclad; both iceclad
  and timorous are above even the old 32 MiB;
  (b) growthplane `placeableObjectsBytes` is 28,687,788 against 24 MiB, a
  zone the gate does not run;
  (c) soldunga FATALs again on the EQE1 name `gnome_stronghold_floor_transition.bmp`,
  which is #100/B910's defect.
  The README carries a report-only table of every other budget against its
  measured worst.

- ~~**B932** (A) Nobody had measured an LLVM-less Mesa on the Orange Pi (Will,
  2026-09-29: "try the LLVM-less Mesa build").~~ **MEASURED 2026-09-30**
  (issue #250, branch `task/B932-llvmless-mesa`, `refactor/B932/README.md`).
  Mesa 25.2.8 from Ubuntu's own source package was built with
  `-Dllvm=disabled -Dgallium-drivers=lima` (`scripts/pi/build_mesa_nollvm.sh`)
  into `/home/user/mesa-nollvm` on the Pi. The system Mesa is untouched. The
  build is selected by a DT_RPATH on a copy of the binary, which survives
  setcap, and every run's maps prove it. kmscube VmRSS goes 55.9 → 17.3 MiB.
  In the rendered client, RssFile falls 25-30 MiB in all five zones, RssAnon
  falls 6.7 MiB at startup (the libLLVM/libgallium COW pages), and
  `gpu_driver` does not move. fps is identical, and 72/72 fixed-pose frames
  are pixel-identical. Findings: B933 → #251 (the printed 64 MB gap still
  counts `gpu_driver`), B934 → #252 (a cold shader cache adds 2.8 MiB of
  `gpu_driver`), B935 → #253 (class C: how the Pi gets the build).
- ~~**B933** (A) The printed 64 MB gap still counted `gpu_driver`, against Will's
  2026-09-29 ruling (GitLab #251).~~ **Closed** on branch
  `task/B933-gap-excludes-driver`: `over_target` / `overTargetBytes` /
  `gapToTarget` / `visual_gate.py` `[rss-target]` / the Pi rig tables are on
  `owned=RssAnon-gpu_driver`, `gpu_driver` is a listed-but-EXCLUDED row, the
  whole-RssAnon figure is `over_target_rss_anon`, the driver .so COW pages stay
  on the owned side, and the 256 MB bound is unchanged. Pinned by
  `MemoryLedger.TargetGapExcludesGpuDriver` and `test_visual_gate_ledger.py`
  check 7b.
- ~~**B935** (C) How the Pi gets the LLVM-less Mesa (GitLab #253).~~ **Decided**
  by Will on 2026-09-30, "ship the prefix" (`docs/decisions.md`), and
  **implemented** on branch `task/B935-ship-mesa-prefix`
  (`refactor/B935/README.md`). The prefix is a pinned deterministic tarball
  (`scripts/pi/mesa_nollvm.sha256`, store `ci-arm/mesa/<version>/`). CI
  publishes `ci-arm/<sha>/bin/willeq-nollvm` with an absolute DT_RPATH to the
  prefix's one Pi path. `$ORIGIN` is dropped under setcap's secure mode
  (measured), and the prefix is not relocatable. The Pi rigs run it by
  default, `WILLEQ_MESA=system` opts out, and every sweep row proves the
  loaded libgallium from maps (exit 67 on a mismatch).

- **B909** (A) The client binary's own memory ledger had never been measured, and
  its `spatial` is over budget (GitLab #229). **Measured 2026-09-29** on branch
  `task/B909-client-spatial-ledger` (`refactor/B909/README.md`). The live
  rendered client ran under the orangepi preset with `WILLEQ_BASELINE=1` on the
  eqemu VM lane, in gfaydark, dreadlands, iceclad and timorous.
  `scripts/client_ledger_capture.sh` + `scripts/client_ledger_table.py`.
  Client `spatial` was 8.49 / 9.35 / 8.13 / 13.76 MB against a 4 MiB budget.
  The benchmark, which is what the gate measures, reported 0.62–3.02 MB.
  **Collision copy FIXED here:** a pooled zone's collision triangles are read
  from the zone draw pools instead of copied (`RegionWorldTriangles`,
  `test_region_world_triangles`, bit-identical ray results). That frees
  2.8–8.6 MB per zone. `spatial` after: gfaydark 5,674,240 (135 %),
  dreadlands 4,194,576 (over by 272 B), iceclad 2,152,000 (51 %), timorous
  5,162,576 (123 %). Every other budgeted category is inside its budget in all
  four zones. **STILL OPEN** (#1 and #2 stay under B909 / issue #229, the
  client-spatial umbrella; #3-#5 have their own numbers):
  1. ~~**(B909, #229)** `sp.object_tris`, placeable objects' triangles
     pre-transformed per instance.~~ **FIXED by B909b**
     (`refactor/B909b/README.md`, branch `task/B909b-object-tris`):
     `ObjectCollisionSet` holds each distinct object mesh once (read in place,
     charged to `objects`; vertex-animated meshes as content-shared
     snapshots) plus a matrix and box per placement, with bit-identical rays
     (`test_object_collision_set`). Client `spatial` after, x86: gfaydark
     0.74 MB, growthplane 1.24, trakanon 0.93, overthere 1.11; Pi: gfaydark
     0.58, growthplane 0.97, trakanon 0.71, all under 4 MiB. The measured
     cause in Kunark/Velious: 86–99 % of placements are vertex-animated
     grass/plants with a private mesh each, which was **B936** (#254) for
     `objects` -- fixed by B936 (one mesh per geometry).
  2. **(B909, #229)** `sp.region_bounds`,
     `ctx_.regionBoundingBoxes`, is a `std::map<size_t, aabbox3df>` with one
     node per region: 1.72 MB in iceclad and 1.27 MB in timorous. A sorted
     flat array would be about 24 B per region plus the index. B909b left it
     (readers in eight TUs, a camera-controller pointer, a frame-render
     golden); it no longer holds `spatial` over budget in the zones B909b
     measured.
  3. ~~**B923 (#242)** the gate cannot see any of this.~~ **FIXED by B923**
     (`refactor/B923/README.md`, branch `task/B923-client-only-owners-gate`):
     the shared occluder table now takes ONE category, `spatial`, in both
     loaders (the client's Step-4 charge moved off `zm.occluders`); the client's
     own ledger is CI-visible via the manual `client-ledger-live` job and
     `ClientLedgerLive` (label `live`), and `ClientLedgerBudgetCheck` (hygiene)
     repeats the red contract with no server. Measured 2026-10-03 (x86,
     llvmpipe, rendered client, iceclad/Velious, `WILLEQ_BASELINE=1`): client
     `spatial` 4,605,600 / 4,194,304 (109.8 %) -- the client-only owners this
     unit exposed (`sp.occluders` 2,777,568 + `sp.region_bounds` 1,717,168 +
     rest, item 2 above). **Expected red on the client-side gate** until item 2
     (`sp.region_bounds` flat-array, #229) lands; the job is `allow_failure`, so
     it records the finding without blocking.
  4. **B924 (#243)** `gpu_driver` (llvmpipe) grows
     after `ready` by a different amount in each run, up to 325.5 MB at
     `steady_60s`. In one smoke run it reached 2.42 GB (about 0.9 MB per frame
     for 50 s, then a plateau). RssAnon peaks above the 256 MB failing bound in
     8 of 16 client runs. Unexplained. x86 only so far; the Pi is not measured.
  5. **B925 (#244)** the iceclad zone link drops on
     every run. The client logs "Zone connection lost" 72–75 s after launch,
     after UDP sends to the zone port are refused. `/q` is then held until the
     rig's 330 s deadline. Cause not investigated.
  6. ~~The Orange Pi table is owed.~~ **Pi A/B done 2026-09-29** (merge gate
     for the collision change; `refactor/B909/README.md` "Orange Pi A/B").
     fable `b1132c3f` vs this branch `506f9de2`, 3 interleaved legs per side,
     keyboard-driven movement via `scripts/pi/uinput_drive.py`. Pi `spatial`
     fell by 2.75 / 3.39 / 5.56 / 8.23 MB (gfaydark / growthplane / iceclad /
     timorous). Moving frame time was within +2.23 % in gfaydark, growthplane
     and timorous. iceclad's frame rate is bimodal on both binaries after the
     B925 reconnect. The first view (a `std::function` per triangle) cost
     +3.76 % in growthplane and was replaced by a direct index loop.
     The per-category Pi ledger table (every budget, not only `spatial`) is
     still owed (`docs/decisions.md` 2026-09-17).
  7. **B926** → issue #245. On the Pi, growthplane (26.9 fps)
     and timorous (26.3 fps) render under 30 fps while the player moves, on
     fable `b1132c3f` itself (the A side of the B909 A/B, 1280x720, orangepi
     preset, level 60, keyboard sequence). The owner's rule makes that a client
     defect. It is not caused by B909; the collision phase is 0.4 ms and 5.0 ms
     of a 37-38 ms frame. **B936 (2026-09-30)**: growthplane's frame was
     mostly the per-placement keyframe rewrite ("SimWorker Apply" 23.5 ms of
     38.9 ms); with one mesh per geometry it moves at 62.1 fps on the Pi.
     timorous was not measured.

- ~~**B936** (A, memory) → issue #254. Vertex-animated placed objects keep a
  private `IMesh` per placement.~~ **FIXED by B936** (branch
  `task/B936-shared-animated-placeables`, `refactor/B936/README.md`): every
  placement of a keyframe-animated geometry is on the same keyframe at every
  instant, so `PlacedObjectMeshes` shares one mesh per geometry (animated or
  not) and poses it once per keyframe change (`applyVertexAnimFrame()`);
  `test_placed_object_meshes`. Client `objects` @steady_60s, x86: growthplane
  47.1 → 9.7 MB, trakanon 27.5 → 7.6, overthere 21.9 → 7.9; Pi: growthplane
  40.3 → 6.7, trakanon 23.4 → 5.5. Pi moving frame rate growthplane 25.7 →
  62.1 fps, trakanon 37.5 → 61.2 (the per-placement keyframe rewrite was
  23.5 ms of growthplane's frame). The benchmark half (#247) is branch
  `task/B936b-bench-shared-objects` (MR !55); the five memory-coverage
  `placeableObjectsBytes` baselines were BLESSED by Will in chat 2026-09-30
  ("bless placeableObjectsBytes", `docs/decisions.md`). #247 acceptance 1 is in;
  acceptance 2 (the census job) remains.
- **B938** (A, memory) → issue #256. RESOLVED (MR pending): the per-placement
  `IMeshSceneNode` (~2.2 KB) and the four renderer-parallel vectors are
  replaced by a 168 B `PlacedObjectRecord` read by both draw paths; PVS zones
  allocate no node at all (growthplane scene graph 4,333 → 10 nodes). x86
  `obj.instances` growthplane 9.61 → 3.27 MB (the residual is the per-object
  light nodes, a separate concern); Pi `obj.instances` growthplane 6.59 →
  2.66, trakanon 4.47 → 1.76, gfaydark 3.33 → 0.65 MB (overthere no `ready`,
  B940; the trakanon row's `gpu_unnamed_over_bound` verdict is B988/#307's
  known condition); per-pixel: CI visual gate 0 mismatch, all five golden
  zones; size pinned by `tests/test_placed_object_record.cpp`.
  `refactor/B938/README.md`.
- **B939** (B) → issue #257. The placement phase of keyframe-animated
  objects was never measured against the official client (#254 acceptance 1);
  fable animates them in phase and B936 keeps that. The baked load pose is not
  keyframe 0.
- **B940** (A) → issue #258. overthere never reaches `ready` on the Pi: the
  zone port refuses UDP about 38 s into zone-in, during entity preparation,
  6 of 6 legs on fable and on B936. Same shape as B925 (#244).
- **B937** (B) → issue #255. Local `visual_gate` on `dev-u24` fails the
  memory-coverage iceclad allocator-slack bound (35.4 % vs 35.0 %, twice) on
  a commit the CI runner passes (33.2 %); the benchmark holds no B909b code.
  fable itself not yet gated locally, so host-only is not yet proven.

- ~~**B814** (A) One category is bigger than the whole target. T491 measured~~ → issue #8 **CLOSED 2026-09-23 by T504** (fable b09bb498): entity_models peak 64-74 → 14-20 MiB on x86, 56-62 → 12-24 on the Pi; the memory-coverage characterModelsBytes 23.9 MB → 1,445,267 (Will blessed 2026-09-22). Evidence on the issue; plans refactor/T504b_entity_source_pruning.md, T504c_wld_fragment_filter.md.
  `entity_models` at **64.56 MiB live / 77.29 MiB peak** on the memory-coverage
  run (gfaydark, 5 races, objects + EQE1) — on its own larger than Will's
  32–64 MB RSS goal (`docs/decisions.md` 2026-09-17). Its budget is set at
  84 MiB, which records where it is, NOT where it belongs. Next behind it:
  `startup` 40.5–49.7 MiB (llvmpipe, see B812), `objects` 28.9 MiB,
  `irrlicht_scene` 25.4–27.9 MiB, `zone_mesh` 18.9–20.3 MiB. These five are the
  shrink queue, in order, and the ledger prints exactly this list on every run
  (`[rss-target]`). Confirmed by T491 (measured).

  **T496 amends this (2026-09-17): the x86 queue is NOT the Pi's queue, and the
  Pi's is the planning list now.** `entity_models` measures **18.6–22.9 MiB on
  the Pi**, a third of its x86 figure, and three of the five names above are
  llvmpipe (B812). See B816.

- **B816** (A, *measured*) **The Orange Pi's own shrink queue.** T496 Leg A, → issue #9 — **T504 (2026-09-23) retired the zone-load rows:** Pi steady RssAnon gfaydark/dreadlands/iceclad/qeynos2 108/137/143/111 → 92/110/112/71 MiB (review/T504/pi/README.md); audio, startup, the Lima driver and malloc_slack remain.
  four runs at 1280x720 (`--drm --constrained orangepi`), every row naming the
  zone the client's own log says it entered (B802 read-back guard):
  `review/T496/pi/table_categories.tsv`. Largest-first at **`ready`**
  (qeynos2 / befallen):

  | bucket | qeynos2 | befallen | note |
  |---|---:|---:|---|
  | **`audio`** | **47.57 MiB** | **47.57 MiB** | **#1 on the Pi, and it does not appear in the x86 top ten at all.** ~23 MiB of it was a duplicate SoundFont — B817 |
  | `malloc_slack` | 33.66 | 21.94 | glibc arena the allocator never returned; driven by the 171 MiB `audio` startup peak |
  | `startup` | 26.82 | 26.82 | identical to 0.003 % across all four runs |
  | `zone_mesh` | 20.79 | 31.94 | |
  | `entity_instances` | 18.52 | 28.24 | |
  | `entity_models` | 22.87 | 18.59 | vs 64.56 MiB on x86 (B814) |
  | `game_state` | 7.23 | 8.62 | |
  | `spatial` / `zone_parse` | 6.28 / 5.80 | 5.53 / 7.00 | |

  **Startup alone**, identical on every run to within 0.003 %:
  `malloc_slack` **110.34 MiB**, `audio` **47.57 MiB** (peak **171.29 MiB**),
  `startup` **26.82 MiB**, `unaccounted` 3.4–3.5 MiB, everything else < 0.1 MiB.
  RssAnon at `startup` is **188.3 MiB** and `over_target` vs 64 MiB is
  **124.3 MiB** — T490's B801 finding ("the gap is the pre-zone baseline") is
  confirmed on the ledger, and the baseline is now ATTRIBUTED: it is
  `audio` + `startup` + the allocator slack those two created.

- ~~**B817** (A, *measured*, **fixed in T496**) **The client held two complete → issue #10
  copies of `synthus2.sf2`, and one of them was unreachable.**
  `AudioManager::initialize()` called `musicPlayer_->initialize()` BEFORE
  `musicPlayer_->setMidiPlayer()`, so `MusicPlayer` built its own fallback
  `MidiPlayer` and loaded the font into it; `setMidiPlayer()` then overwrote
  the pointer and left the fallback alive for the life of the process. A second
  defect underneath it: `~MidiPlayer` cleared `impl_->soundFont` and then closed
  only `gmSoundFont`/`customSoundFont` — `soundFont` aliases one of those ONLY
  on the dual-font path, so a font loaded by the single-font `init()` overload
  (exactly the one the fallback uses) was **never closed at all**.
  RED, in bytes, offline on x86: **24,160,112 B retained** against a 1 MiB
  bound; GREEN after, 3/3
  (`tests/test_music_player_soundfont_lifetime.cpp`). No audio output changes —
  `MusicPlayer` renders through the shared `MidiPlayer` either way.~~
  **STRUCK B849 (2026-09-18, issue #10 closed by the B849 unit)**: both halves
  verified on fable — the ordering fix is
  `src/client/audio/audio_manager.cpp:128-139` (T496 Leg B, `942c51e9`) and
  `MusicPlayer::setMidiPlayer()` releases the superseded fallback,
  `src/client/audio/music_player.cpp:95-106`; the destructor leak is closed in
  `src/client/audio/midi_player.cpp:92-106`, pinned by
  `tests/test_music_player_soundfont_lifetime.cpp`.

- **B818** (B, *items 1a/2/3/5 resolved by T496 Leg D -- see B819/B830/B831*) **Owed from T496, deliberately not taken there.** (1) The three → issue #115
  Mali budgets B812 now has numbers for (`startup` 26.82 MiB,
  `textures_driver` ≤ 0.67 MiB, `irrlicht_scene` ≤ 1.45 MiB) are **stable
  across four Pi runs and ready to enter the orangepi preset's
  `memoryLedgerBudgets`** — not set in T496 because that is a budget change in
  a measurement leg. (2) **The ledger hook cost on ARM is still unmeasured** —
  the second half of B812. The four Leg A runs emit no `frame_allocs` line
  because they are `willeq`, not the benchmark; one
  `gles2_zone_benchmark --memory-json` on the Pi closes it. (3)
  **`malloc_slack` is the Pi's #2 bucket at 110 MiB at startup and nothing
  bounds it.** It is glibc arena retention, and it tracks the `audio` peak that
  B817 has now removed; re-measure before treating it as its own item.
  (4) `gfaydark` reached `login_world` but not `ready` inside a 360 s watchdog
  on the Pi (no OOM, zone sources loaded in 403 ms) — an instance of B751's
  shape on a zone that used to load. (5) `test_memory_ledger.cpp`'s
  `MallocFamilyAllRouteThroughTheLedger` is **RED on the tip and was already red
  before T496**: its last assertion cannot hold, because `realloc(nullptr, 0)`
  returns a minimum-size chunk (48 B here) that the case never frees. A T491
  test defect, not a ledger defect; T496 left it alone to avoid colliding with
  the orchestrator's in-flight gate work on that file. (6) A task worktree lacks
  **two** gitignored symlinks, not one: `data/eq_client` (B815) **and
  `data/eqe1_client`**. Missing the second makes `visual_gate` print
  `memoryCoverage: SKIP ... a real coverage gap on this machine, not a pass`
  and still exit 0, so a worktree gate run covers less than main's without
  saying so. (7) **The T496 plan's own candidate list
  contained a unit error**: it read the Pi audit line's `texcache=44116` as
  44,116 **kB**; `load_mem_audit.cpp` prints that field with `%zu` from a
  `size_t` of BYTES, so the startup texture cache is **43 kB**, not 43 MiB, and
  was never a shrink candidate.

- ~~**B818.5** the ledger test is red on the tip~~ **RESOLVED as B819.**

- ~~**B819** (A, *measured*, **fixed in T496 Leg D**) **A ctest case was a → issue #11
  function of the optimisation level, not of the code under test.**
  `MemoryLedger.MallocFamilyAllRouteThroughTheLedger` passed in `main`'s
  RelWithDebInfo tree and failed in a worktree configured by **CLAUDE.md's own
  recipe**, which sets no `CMAKE_BUILD_TYPE` and therefore builds at `-O0`.
  Cause, PROVED on the exact body with `g++ -S` and a count of emitted calls,
  not inferred: at **-O0** GCC lowers `realloc(NULL, n)` to **`call
  malloc@PLT`**, the allocation really happens, the case discards the returned
  pointer and never frees it, and the ledger correctly reports glibc's 48-byte
  minimum chunk still live — RED. At **-O2** the pointer never escapes, so GCC
  **elides the allocation entirely (zero calls emitted)** and `live == before`
  — GREEN. Third instance of this trap in this tree (T400's protocol case,
  T491 Review §3's `FreeUnderAnotherScopeCreditsTheChargedCategory`). Fixed by
  asserting **what the ledger promises** — the pointer escapes through an `asm`
  barrier, is freed, and the case asserts charge-then-credit — because
  `realloc(ptr, 0)` and the minimum chunk size are both
  implementation-defined and neither is assertable. Green at `-O0` **and** at
  `-O2`: 20/20. **Adjacent and still open:** the configure command in
  `CLAUDE.md` names no `CMAKE_BUILD_TYPE`, so the documented way to set up a
  tree differs from the way the trees that gate merges are built (CI is
  `Release`); B849 could not touch that file (agent-protected) and left the
  item open. ~~Struck B849 (2026-09-18, issue #11 closed by the B849 unit):
  the test fix itself is `tests/test_memory_ledger.cpp:180-215` (T496 Leg D,
  `39fe0166`).~~

- ~~**B830** (A, *measured*, **fixed in T496 Leg D**) **The client read 125 MB of → issue #12
  sound archives it was configured never to keep, then freed them again —
  and that transient was the Pi's #2 bucket.**
  `AudioManager::scanPfsArchives()` indexes seventeen `snd*.pfs`
  (**124,397,766 bytes** on disk) at startup and got each name list by calling
  `PfsArchive::open()`, which reads the whole archive into one buffer AND
  copies every member's compressed blocks into `files_` — then RETAINED the
  opened archive whenever `lazyPfsLoading_` was false. **It is false there on
  every run**: the preset's constraints arrive afterwards, via
  `setMemoryConstraints()` from `BridgeComponent`, and every constrained preset
  sets `lazyPfsLoading = true`
  (`ConstrainedRendererConfig::calculateMemoryLimits()`). So the constraint
  arrived after the allocation it existed to prevent, and glibc kept the arena.
  New `PfsArchive::listFilenames()` streams the filename-table entry alone
  (directory walk → CRC `0x61580ac9` → inflate that entry block by block off
  the `ifstream`); purely additive, no existing caller changes.
  **x86 red → green, `Category::Audio` bytes, cold index:** peak
  **155,560,752 → 26,752,336**, live **150,590,448 → 25,678,336**.
  **Orange Pi, `startup`, confirming run, Leg A recipe verbatim:**

  | | T496 base | after B817 | **after B830** |
  |---|---:|---:|---:|
  | `audio` peak | 179,607,200 | 155,448,424 | **26,522,912** |
  | `malloc_slack` | 115,696,496 | 116,654,416 | **726,664** |
  | **RssAnon** | 197,492,736 | 174,211,072 | **58,331,136** |
  | **`over_target` vs 64 MiB** | 130,383,872 | 107,102,208 | **0** |

  **At `startup` the client is now INSIDE Will's 32–64 MB target: 55.63 MiB
  RssAnon, `over_target` 0, down from 188.34 MiB.** `malloc_slack` fell
  110.34 MiB → **0.69 MiB**, which confirms it was a symptom of this transient
  and not a bucket of its own — so it gets no board entry, per the
  orchestrator's ruling. At `ready` RssAnon is 169.5 MiB (zone data dominates
  there) against 186.4 before. Pinned by
  `tests/test_audio_startup_peak.cpp`, which also asserts the index did not
  drift (**17 archives, 2,127 `.wav` names, `listFilenames` ==
  `open`+`getFilenames`, zero mismatches**) and which forces a COLD index —
  a warm `snd_index_cache.json` short-circuits the scan and made both peak
  cases pass against the unfixed code (measured: peak 26,279,360 warm). No
  sound changes; placement and lifetime only.~~ **STRUCK B849 (2026-09-18,
  issue #12 closed by the B849 unit)**: verified on fable — the streaming
  index is `PfsArchive::listFilenames()`, `src/client/graphics/eq/pfs.cpp:304`
  (declared `include/client/graphics/eq/pfs.h:56`), called from
  `src/client/audio/audio_manager.cpp:975` (T496 Leg D, `39fe0166`), pinned by
  `tests/test_audio_startup_peak.cpp`.

- **B831** (C) **There is one ledger budget table and it is asserted on two → issue #13
  different machines.** `tools/hwcaps/zone_bench_loader.cpp` hard-codes
  `ConstrainedRendererConfig::fromPreset(ConstrainedRenderingPreset::OrangePi)`,
  and `scripts/ci/visual_gate.sh` runs that benchmark on **x86/llvmpipe**. So
  `config/constrained_presets.json`'s `memoryLedgerBudgets` must satisfy Mali
  and llvmpipe at once. T496 Leg A measured how far apart they are:

  | category | Orange Pi | x86 gate | Pi-derived budget ×1.25 | x86 would read |
  |---|---:|---:|---:|---:|
  | `textures_driver` | ≤ 0.67 MiB | 2.08–**30.50** MiB | ~0.93 MiB | **3,280 %** |
  | `irrlicht_scene` | ≤ 1.45 MiB | 24.01–**27.07** MiB | ~2 MiB | **~1,350 %** |
  | `startup` | 26.82 MiB | 40.53–**49.87** MiB | 34 MiB | **147 %** |

  This is why the orchestrator's T496 Leg D instruction to set all three could
  not be carried out: each one turns the visual gate permanently red, which is
  a knowingly-broken gate, not a budget. **`audio` WAS set** (32 MiB from the
  Pi's measured 24.53 MiB × 1.25) because the benchmark links no audio at all
  — `nm -C gles2_zone_benchmark | grep -c 'MusicPlayer\|MidiPlayer\|tsf_load'`
  is **0** — so it cannot conflict. **What would close this:** budgets keyed by
  rendering backend (Mali vs llvmpipe) rather than by preset alone, or a gate
  that runs the benchmark under a second, llvmpipe-derived budget set. Either
  is a schema + parser + gate change and a design call, not an executor's.

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

- **B161** (B) `include/client/graphics/spell_visual_fx.h` — seven public → issue #116
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

- **B165** (B) **ANNOTATED by T389 (2026-09-05): the oracle says both impact → issue #117
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

  **ANNOTATED T417 (this commit)**: B300 is struck and the sprite half of the
  question is closed -- every stage sprite name a record carries now resolves
  to the official client's own art on a real sheet, so "what does the client
  draw for this name" is no longer an open input to the projectile decision.
  What still blocks B165 is unchanged and is NOT a data question: it is
  B227's unmeasured travel capture. T417 could not help there either, because
  it never held the lock.

- **B195** (B) `include/client/graphics/spell_visual_fx.h` → issue #118
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
  `SwimmingStateChangedData` (`include/client/state/event_payloads_spell.h:113`) carries
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

- **B036** (A, feature — **WILL RULED 2026-09-03**) UI sounds are wanted: → issue #14
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
  the `FileSizeCeiling` ctest test. Largest survivors at that measurement were
  three test TUs riding the ceiling (1,492 / 1,367 / 1,316) plus
  `src/client/graphics/door_manager.cpp` 1,394 and
  `src/client/spell/spell_manager.cpp` 1,360; **T446 split the three test TUs**
  (`refactor/T446_test_tu_splits.md`), so the largest test TU is now
  `tests/test_eq_commands_equip_bags.cpp` at 654 lines. See `docs/decisions.md`,
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

- ~~**B060** (C)~~ **CLOSED by T428** (`refactor/T428_night_base.md`): Will ruled a
  FOURTH vision level and the solved night base (B280) turned it from a guess into a
  measurement. Oracle session o3, `#race 330` at a pinned camera at forced 22:00,
  bracketed by race-1 windows agreeing to 0.7%: **Froglok = x2.6803 R, x1.9478 G,
  x2.1753 B** over its own night base, against Ultravision's x2.7995 / x1.9517 /
  x2.4946 and Infravision's x2.2483 / x1.4121 / x1.3561 -- 5.5% under Ultravision on
  B and 60% over Infravision there, so it is its own level and not a rounding of
  either. `VisionType::FroglokVision` carries it (APPENDED as 3, because the
  enumerator values are wire values -- `BuffManager::computeVisionType()` returns
  literal 2 for UltraVision -- and `visionRank()` is the measured brightness order,
  without which an Ultravision buff would be a silent no-op on a Froglok). Two legs
  are OWED, both boarded: fable's own confirmation, blocked because a race change on
  SELF never reaches `setPlayerRace()` (**B433**), and the zone-LIGHT half, which
  inherits Ultravision's pair rather than inventing one (**B067**). Evidence:
  `review/T428/README.md` §4. Original text follows.

  ~~`EQT::Graphics::racialBaseVision()` — **MEASURED AGAINST THE ORACLE BY T361
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

  **T428 (2026-09-07) ADDED A ROW AND DID NOT MEASURE IT.** The fourth vision
  level (`VisionType::FroglokVision`, B060) takes Ultravision's `(intensity 1.0,
  redShift 0.0)` on the zone-light side, inherited from its nearest measured
  neighbour rather than invented; the comment at the site and
  `VisionSolvedT405Test.LampModifiersAreStillT360sLiterals` both say so out loud.
  T428 also gives B067 a much bigger sibling: **B431**, fable's zone lamp being
  worth far more relative to its ambient than the oracle's -- the lamp spot still
  misses by +34/+71/+32% after the night base is solved.

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

- **B110** (C) `src/client/graphics/eq/wld_fragments_mesh.cpp` → issue #15
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

  **T428 (2026-09-07) RE-SOLVED ALL SIX CONSTANTS and superseded the oracle table
  this entry closed against, for TWO independent reasons.** (1) The night base they
  are floors over moved 3.3x (B280), and T405's Ultravision floor sits BELOW the new
  base on G -- the buff would have been a no-op on that channel, the exact shape
  T405 itself found in the pre-T405 Infravision floor. (2) T405's oracle ratios were
  measured on the 700x420+40+40 crop, which is about HALF SKY at that spot, and the
  sky does not respond to a vision buff -- so the crop dilutes every ratio by a
  factor set by each client's own camera. Re-measuring T405's OWN two oracle videos
  on T408's geometry band, plus a third session, gives Ultravision x2.7995 / x1.9517
  / x2.4946 (three sessions inside 1%) where T405 read x2.2531 / x1.4399 / x1.7687.
  fable now matches to +0.9 / +1.2 / -4.0% (Ultravision) and +3.0 / +1.6 / +3.0%
  (Infravision). B203 stays CLOSED; this is the record of what replaced its numbers.

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
- **B241** (B) `scripts/build_willeq_pack.sh` / `tools/ui_atlas_builder.cpp` → issue #119
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

- ~~**B231** (B) `src/client/graphics/weather_effects_controller.cpp`
  `getAmbientLightModifier` -- how much the OFFICIAL client darkens for ACTIVE
  weather is unmeasured, and fable's number is invented. Raised by T396, which
  fixed the clear-weather rule (modifier exactly 1.0) and deliberately left the
  active-weather curve alone, per its plan. **WILL RULED 2026-09-06:** capture
  the official client under forced rain and mimic the curve.~~ **STRUCK T418
  (`26a2fa05`).** MEASURED, and the answer is that **the official Titanium
  client does not darken the world for weather AT ALL** -- not at intensity 2, 5
  or 9, not in rain and not in snow. It BRIGHTENS it. qeynos2, both clients,
  `#goto -74 428 3.751 160`, `rain_chance` zeroed, clock re-pinned before every
  state, each state paired with its own clear reference 30 s earlier, plateaus
  read out of a 5 fps capture, five oracle sessions (the Wine client crashed
  three times). Geometry band `560x110+200+350`, linear light, day:
  **rain 2 x1.2496, rain 5 x1.2552 and x1.2541 (two sessions), rain 9 x1.2549,
  snow 5 x1.2501** -- a 0.4% spread against a 0.03% clear-reference control, so
  **intensity does nothing and snow is rain**. Night agrees on the sign
  (x1.006 to x1.133, never below 1.0). It is **fog, not ambient**: the same
  frames' ratio grows monotonically with depth (R x1.42 on the nearest wall
  strip to x2.73 on the farthest) and the sky goes blue (.163,.290,.429) to grey
  (.285,.286,.348) -- no scalar on the scene ambient can produce that. So the
  committed curve is the plan's stated answer for exactly this case: a lookup
  table whose knots (rain 0/2/5/9/10, snow 0/5/10) are the measured intensities
  and whose every value is **1.000**, in
  `include/client/graphics/weather_ambient_oracle.h`. What IS mimicable and was
  mimicked is the FADE: **9.0, 9.0, 9.2, 9.4, 9.6 s** over the five clean day
  transitions, independent of intensity, against fable's 1.08 s at intensity 9;
  `kOracleWeatherTransitionSeconds = 9.3f`, residual <=3.3% on every
  measurement. Ten `WeatherDarkeningOracleT418Test` cases pin the table, the
  fade and the ramp (**8 red pre-fix**), and three of T396's cases were
  rewritten to assert the darkening TERM rather than the modifier (**2 red
  pre-fix**). Live: fable at rain 9 by day went **x0.4203 -> x0.9969** and at
  rain 2 **x0.9380 -> x0.9968**, inside the 0.7% session control, with **zero**
  `weatherMod` lines left in the post-fix log. The weather FOG is deliberately
  byte-for-byte unchanged (`currentDarkening_` still targets
  `maxDarkening * i/10` and still drives the blend -- the blend now reads it
  directly, because `1 - getAmbientLightModifier()` is identically zero under
  the measured curve and would have deleted the weather fog silently).
  Residues: the overcast sky/fog swap the oracle actually performs is **B360**
  and is still open; the three ramp copies (**B361**) and the modifier reaching
  the scene only on a time-of-day packet (**B362**) were both CLOSED by T423.
  Prov: T418 Review; `review/T418/`; B203; B211.

### Added by T397 (2026-09-05)

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

- ~~**B275** (B) `include/client/graphics/ui/ui_layout.h` `UILayout`, `include/client/graphics/ui/static_panels.h` `ChatPanelState` — **the client persists NO user settings.** Chat timestamps, panel visibility, chat scrollback size, UI colours and every panel position are computed or defaulted at startup and nothing writes them back: `saveLayout`, `loadLayout` and any preferences writer return zero hits under `src/`, `include/` and `tools/`, and the only two files that ever held such state (`config/chat_settings.json`, `config/ui_settings.json`) were the deleted WindowManager's and are gone (B245). So a player who moves nothing notices nothing, and a player who changes anything loses it at exit. Closes when a preferences file is written and re-read with a test, or when Will rules that a fixed static UI needs no persistence. Found by T402.

  **WILL 2026-09-07:** persist per character what is user-changeable: the 15 window open states + camera mode (measured); positions are static by design. → T432.~~
  **CLOSED by T432** (`refactor/T432_per_character_persistence.md`): the client
  now writes `<cache dir>/ui/UI_<Char>_<server>.ini` -- the official client's
  own filename, section and `Show=0/1` key shape -- and re-reads it at zone-in.
  **SEVEN windows, not fifteen**: a census of every user-toggleable window in
  the tree found `[InventoryWindow]`, `[SpellBookWnd]`, `[SkillsWindow]`,
  `[GroupWindow]`, `[PetInfoWindow]`, `[BuffWindow]` and `[CombatAbilityWnd]`,
  plus `[Camera] Mode=`. The other panel `isOpen` flags are server- or
  click-driven (loot, tradeskill container, item display), and
  `RA::ToggleOptions` reaches a `break;` -- fable has no options window at all
  (a `PopupType::Options` enumerator with zero uses). `F11`'s `allUIHidden_` is
  deliberately NOT persisted: a client that starts with no UI gives the player
  no way to learn why. An ABSENT `Show=` key keeps the default, which is
  `defaults.ini`'s own rule (T368), so a fresh character writes no file until it
  changes something. ONE persistence path: `FrameComponent::syncUiPreferences()`
  COMPARES the whole collected value against the last one it wrote, so no
  per-toggle save hook exists to forget. Pinned by 18 cases in
  `tests/test_ui_preferences.cpp` (4 red pre-fix against a probe that reproduces
  this tip's behaviour). Chat timestamps, scrollback and UI colours stay
  unpersisted -- Will's ruling narrowed the scope to window states and the
  camera. ~~**The live proof (toggle, camp, log in) is OWED to a lock unit**:
  T433 held the eqemu lock.~~ **LIVE-PROVEN T449 (this commit).** Testchar in
  qeynos2: `k` opened the Skills window and `alt+p` closed the Group window
  (default OPEN); the file appeared WHILE THE CLIENT WAS STILL RUNNING -- the
  per-frame compare, not only the `/camp` path -- carrying
  `[SkillsWindow] Show=1 [GroupWindow] Show=0`. After `/camp`, **Testmelee on
  the same account and server** logged in with the Group window OPEN and no
  Skills window (its own defaults, not Testchar's state) and wrote NO file
  until it changed something; one `k` then created a SECOND file,
  `UI_Testmelee_WillEQ.ini`, and Testchar's `diff`s identical. The read-back
  half shows again downstream: the merchant capture has the Skills window still
  open behind it, restored from the ini at zone-in. `review/T449/README.md` §2.
  ⚠ **And the feature turned out to WIDEN a crash**: a saved
  `[InventoryWindow] Show=1` reopens the paperdoll at login, and the next zone
  change segfaults the client -- **B474**.
- ~~**B280** (A)~~ **CLOSED by T428** (`refactor/T428_night_base.md`): the night
  ambient is now **(0.26244, 0.31147, 0.24003)**, solved live against the official
  client's own DAY/NIGHT ratio per channel on T408's geometry-only crop. fable's
  lamp-free day/night reads **R 4.4760 / G 3.6025 / B 3.7316** against the oracle's
  **4.4540 / 3.6220 / 3.8090** -- **+0.5% / -0.5% / -2.0%**, from +121% / +247% /
  +22%. The probe is `/nightambient <r> <g> <b>`; the constants live in
  `include/client/graphics/vision_ambient.h` with the whole day/night curve
  (`dayNightAmbient()`), and `tests/test_night_ambient.cpp` pins them. **The grey
  consequence below resolved itself**: with the bases matched, the two clients'
  Ultravision greys now agree to 0.4% with nobody aiming at the grey (Infravision's
  gap survives at ~7%, because the DAY chromaticities still differ -- the arithmetic
  stands, one of its two rows collapsed). **Three things came out of it and none of
  them is closed**: the ambient is uploaded twice and squares its own chromaticity
  (**B430**, and it is the root cause of T405's unexplained channel coupling); the
  lamp spot still misses by +34/+71/+32% because fable's lamp is too strong relative
  to its ambient (**B431**); and the re-solved Ultravision floor now lifts daylight
  ~2.5% (**B432**). Evidence: `review/T428/`. Original text follows.

  ~~`IrrlichtRenderer::updateTimeOfDay` night ambient (0.08, 0.08, 0.15) --~~
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

- **B325** (B) `include/client/graphics/zone_shader.h` `kBakedLightMode` / → issue #120
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

  **T428 (2026-09-07) SOLVED B280, so this entry's own trigger has fired**: the
  night ambient moved to (0.26244, 0.31147, 0.24003) and every row of T408's
  composition table was taken against the old one. The re-run is now possible and
  is owed. Note before re-running it that T428 found the residual is NOT uniform
  per channel even though it is uniform in grey -- the lamp spot still misses by
  +34/+71/+32% (B431) -- so a lamp-shaped term is back on the table for that spot,
  which is what B110 originally proposed and T408 measured away in grey.

- **B326** (B) `src/client/graphics/eq/zone_geometry.cpp` → issue #121
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

- **B281** (A) `src/client/graphics/renderer/irrlicht_renderer.cpp` / → issue #16
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
  behaviour mid-unit. Found by T399.~~ **CLOSED by T402 Leg C** (`refactor/T402_caches_dead_configs.md`): the parameter is gone (`bool loadRaceMappings()`), the S03 `checkFile` line for `config/race_models.json` is gone, and `renderer_init`'s fatal now says `Race model mappings unavailable: no willeq.db is open` — the only condition a false return has ever meant. **`config/race_models.json` is NOT deleted and that is not a STOP:** the census (`src/ include/ tests/ tools/ benchmarks/ scripts/ docker/`) shows it is the DB BUILDER's input — `tools/willeq_db_builder.cpp:356` `loadRaceModels(db, inputPath("race_models"))` (the loader itself is `tools/willeq_db/db_loaders_json.cpp:126` since T447 split the builder; both line numbers re-pointed by T456, whose drop of `loadEquipmentModels()` moved them) builds the `race_models` table from it, and `scripts/generate_race_colors.py`, `scripts/update_race_models_from_reference.py` and `scripts/generate_s3d_reference_json.py` read it too. It is build-time source, not a runtime file; only the RUNTIME requirement was wrong. Proven live: with `config/race_models.json` moved away the client starts, initialises graphics and logs `Race mappings using on-demand SQLite lookup`; with `data/willeq.db` moved away it fails naming the db (`failed to open data/willeq.db ... rc=14`, and in fact dies at the spell database before the renderer's check is reached). Pinned by `RaceNameNoDatabaseTest.RaceMappingsFailOnTheDatabaseAndNothingElse`, `RaceNameFromDbTest.RaceMappingsBindToTheDatabase` and the G04 init-order golden, which learned the deletion of entry 46 in place (correction 32).

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

- ~~**B160** — its owed live pair is **NOT PROVEN** and the reason is bigger than
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
  real there. Take the observer pair after B226.~~ **THE OWED PAIR IS
  TAKEN -- CLOSED by T422 (this commit).** B226 was closed first, in the same
  unit, exactly as this note said to. All four spells (54, 93, 41, 253) were
  self-cast by Testcaster in qeynos2 and every one of them DREW on a second
  fable client (Testbuddy, its own process and its own Xvfb, 25 units away,
  `/target` + `/face` on the caster), with the official client's own sprite
  art. Stills:
  `review/T422/fable/T422_fable_observer_<id>_<name>_1024x768.png`.
  Colours, measured identically for each (mean added RGB over the particle
  pixels against a temporal median of the pre-cast renders, normalised):
  **54 Frost Bolt .228/.363/.409 (B-dominant)**, **93 Burst of Flame
  .515/.339/.146 (R-dominant)**, 41 Weaken .377/.294/.329, 253 Whirling Wind
  .299/.248/.453. The fire/frost pair this entry actually asked for is
  unambiguous in both directions and matches the ORACLE's polarity (T389: 93
  .360/.379/.199 R-dominant, 54 .213/.284/.315 B-dominant). SHAPES differ per
  spell too and are in the stills for Will to judge. One colour does NOT match
  and is boarded rather than buried: Weaken is R-dominant in fable and
  G-dominant in the oracle -- **B378**.

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

- ~~**B265** (B) `data/textures/detail_atlas.bmp` /
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
  Prov: T403 Review §2.~~ **CLOSED by T427 (2026-09-07, this commit).** The
  file is regenerated and `NOT_REPRODUCIBLE` is now **EMPTY**:
  `build_textures.sh --check` reports `65 byte-identical, 0 drifted, 0 exempt`.
  **Nothing moved.** The gate PASSED against a temp pack built from the
  regenerated textures dir (`--textures-dir`, new flag; `data/eqe1_client`
  repointed at a staging tree) with **0 differing pixels in all nine golden
  frames** and **0 bytes** of `eqe1MappedBytes` delta in all three zones
  (124063684 / 122729776 / 117214272, exactly the manifest) -- the pack is the
  same 6,930,116 bytes because ETC1 is fixed-rate, so an entry's byte count
  cannot follow its content.
  **TWO CLAIMS ABOVE ARE WRONG AND ARE CORRECTED HERE.**
  (i) *"detail_atlas is drawn by every outdoor zone the visual gate covers"* --
  it is not drawn by ANY of them. `tools/hwcaps/gles2_zone_benchmark.cpp`
  never instantiates `DetailManager` and never names `detail_atlas`; a grep of
  the whole `tools/hwcaps/` directory for either returns nothing. No golden
  frame has ever held a detail-atlas texel, so the STOP that guarded this item
  for two units could not have fired.
  (ii) *cause (b) is not code drift.* The shipped footprint tiles are not
  near-black-where-the-generator-draws-colour: they carry the per-biome colour
  at **~0.71x** its value. At the 114 pixels per tile where the generator
  writes the base colour exactly (alpha = 1), shipped reads Grass (24,24,16)
  against (40,35,25), Dirt (32,24,16) against (50,40,30), Sand (104,88,56)
  against (140,120,80), Snow (128,136,152) against (180,190,210), Swamp
  (24,32,16) against (35,45,30), Jungle (32,24,24) against (45,40,35) -- every
  one a multiple of 8 and every one ~0.7 of the argument. That is **RGB
  PREMULTIPLIED BY ALPHA**, and it is the SAME retired-tool write path as
  cause (a), not a second, independent defect: premultiplication is invisible
  wherever alpha is 0 or 255, and `drawFootprintLeft`/`drawFootprintRight`
  (`detail_texture_atlas_grass.cpp:96` and `:180`) are the ONLY two writes in
  the entire atlas that emit a fractional alpha -- `SColor(alpha * 200, ...)`.
  Every other `SColor(` in the three atlas TUs is alpha 255 or the transparent
  fill. The twelve tiles that differ beyond quantisation are exactly the twelve
  the fractional alpha touches; the ratio distribution over a whole footprint
  tile runs 0..0.729, capped at alpha's own `200/255`. So B265 was ONE defect
  with two symptoms, and no code ever drifted from the asset.
  A separate, real question the census forced out -- the atlas is 24-bit while
  both its consumers alpha-BLEND it -- is boarded as **B410**, not folded in
  here.

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
  provably never sampled -> new **B286**), `detail_atlas.bmp` (B265 at the time: no
  build reproduced it, so a 32-bit rewrite looked like a pixel move in every
  outdoor gate zone -- **both halves of that were wrong; see the struck B265
  and the new B410**), and `rain_streaks.bmp` + `snow_flakes.bmp` (**no consumer at all** --
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

- **B267** (B) `tools/particle_atlas_builder.cpp` -- **a 118-line target that → issue #122
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
  was B265's `detail_atlas.bmp`, which no build reproduced until T427);
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

- ~~**B226** (B) `config/spell_effects.json` `styles.impact` /
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
  **ANNOTATED T417 (this commit)**: the sprite shapes are now the OFFICIAL
  CLIENT'S OWN ART, not sixteen procedural approximations (B300 struck), so
  T400's "changing the sizes and the shapes in one unit would make the next
  capture pair unattributable" has had its shape half spent. This entry is
  unchanged on its own numbers -- T417 deliberately did not touch
  `styles.impact`, and 36 particles of 0.05-0.15 world units for 0.4-0.8 s is
  still what ships. The live A/B against T389's oracle videos was T417's
  Leg C and did NOT run: T418 held the eqemu lock for the whole unit. That
  owed match is **B356**, and it is the next thing this entry needs.
  **ANNOTATED T420 (this commit)**: T420 did not touch `styles.impact` either,
  but it gave those lifetimes a SECOND consequence. The sprite sheets now
  animate at `16 / lifetime` cells per second, so the 0.4-0.8 s lifetimes are
  simultaneously too small, too short AND -- against T420's oracle bound of
  below ~3 cells/s -- roughly ten times too fast. One knob, three symptoms;
  retune all three in the one pass. Boarded as **B370**.~~ **CLOSED by T422
  (this commit).** The retune ran against a fresh oracle capture and the
  parameter table is in `review/T422/README.md` section 4. **Two corrections to this
  entry's own framing, both measured:**
  (a) **"two orders of magnitude smaller" conflated three different things.**
  The PER-SPRITE size was 1.6x out, not 100x (fable's shipped 0.05-0.15 world
  units rendered at a median 3.57 px against the oracle's 4.4-6.9; it is now
  5.53 px). The POPULATION was 8x out (81 combined against 561-715). The
  whole-burst EXTENT was an effect of the 0.4-0.8 s lifetime cutting the
  travel short, not a knob of its own -- `expandSpeed` 4.0 measured RIGHT
  against the oracle's ~4.2 units/s and was deliberately not touched.
  (b) **The shape was wrong in a way this entry never named**: the official
  client does not fire a one-shot burst at all. Its population rises
  monotonically with NO deaths for 1.96 s (spell 93) and 1.46 s (spell 41)
  before collapsing, so `burstCount` was the wrong mechanism and the style is
  now a `spawnRate` over a 0.6 s `emitterLifetime`. The residual ramp-then-cut
  shape fable still cannot express is **B376**; the fact that one style must
  serve spells whose oracle populations differ 20x is **B377**. Pinned by
  `tests/test_spell_burst_oracle_match.cpp` (9 tests, 8 red pre-fix).

- **B227** (B) the projectile question (does the official client draw a Frost → issue #123
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

  **WILL 2026-09-07:** measure with TWO characters duelling at a set distance (the standing spell rig). → the next lock unit.
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

- ~~**B286** (B) `src/client/graphics/spell_visual_fx.cpp` /
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
  glittersp501.dds and Corona1.dds are all missing.~~
  **CLOSED by T424 Leg A** (`0911184a`), the same commit that closed B322 --
  the two entries are the same defect seen from the consumer end and the
  generator end. The asset-side fix this entry called for is done: the cosine
  falloff is in RGB, the additive blend samples it, and the shared pack was
  rebuilt and installed in that one commit. See the struck B322 for the
  measured before/after profile and the pack delta.

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

- **B293** (B) `include/client/graphics/environment/particle_manager.h` → issue #124
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

- **B295** (B) `include/client/graphics/graphics_archive_index.h` → issue #125
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

- **B298** (B) ~~`include/client/inventory/item_instance.h`~~, → issue #126
  `include/client/spell/spell_data.h`, `include/client/skill/skill_data.h`,
  `include/client/state/entity_manager.h` -- **server- and DB-sourced data
  parsed and never shown.**
  **ITEM HALF STRUCK by T421 Leg A** (`f7b0fc22`): fable now has an item
  display window -- `EQUI_ItemDisplay.xml`'s own 400x190 `ItemDisplayWindow` at
  its own absolute (0,80), an `IDW_IconButton` and one `IDW_ItemDescription`
  STMLbox -- and the STMLbox's text is composed by
  `eqt::inventory::BuildItemDisplayText()`
  (`src/client/inventory/item_display_text.cpp`) from format strings quoted out
  of the official client's OWN string table,
  `/mnt/projects/EverQuestP1999/eqstr_us.txt`. **Fourteen of the item fields
  now have a drawn line**: `accuracy` (eqstr 9043), `damageShield` (5847),
  `dotShield` (5836), `spellShield` (9041), `strikethrough` (9045),
  `stunResist` (9044), `shielding` (9040), `enduranceRegen` (6184), `hpRegen`
  (5845), `manaRegen` (5846), `delay` (3008, with the weapon skill name),
  `recLevel` (3028), `maxCharges` (3003/3002), `lore` (3048) -- plus
  `expendable` (3001), `attack`, `avoidance`, `haste`, `endurance` and the
  whole 12457..12470 STR/DEX/STA/CHA/WIS/INT/AGI/HP/MANA/SV block, which were
  never drawn either. Their twelve rows are gone from
  `scripts/refactor/check_write_only_fields.py`'s EXEMPT table.
  **Two corrections to this entry's own census, both measured at
  `8f7e718c`:** (a) the count "21" over-reads -- the names listed are 24, and
  (b) **`expendable`, the seven `heroic*` and `bardEffect` have NO WRITER
  ANYWHERE** (`grep` over `src/ include/ tests/ tools/ benchmarks/`: the only
  hits are the declarations). They are not parsed-and-unshown; they are dead
  struct members, which is a different defect -- split out as **B375**.
  What remains under this id for the item struct is `currentSlot` alone, and
  its reason is now measured rather than pending: **the oracle has no element
  for it.** `EQUI_ItemDisplay.xml` is a title bar, an icon button and one free-text
  box; `Slot:` (eqstr 3000) is the item's EQUIPPABLE slot list, which the
  window draws from `slots`, and nothing anywhere shows where an item
  currently sits. Its exemption row says so.
  **SPELL HALF STRUCK by T425 Leg A** (`266189d4`). fable now has a spell
  display window -- and it is the SAME window, because **the oracle has no
  second one**: there is no `EQUI_SpellDisplay.xml` and no `SpellDisplay` /
  `SpellInfo` screen anywhere in `/mnt/projects/EverQuestP1999/uifiles/default`
  (79 `<Screen item=...>` declarations, none of them a spell display), and
  `EQUI_ItemDisplay.xml`'s own `IDW_IconButton` decals the SPELL sheet
  (`<NormalDecal>BuffIcons</NormalDecal>`). So T425 added no panel, no layout,
  no event and no scroll target: `eqt::spell::BuildSpellDisplayText()`
  (`src/client/spell/spell_display_text.cpp`) composes lines from the same
  `eqstr_us.txt` T421 quoted and ships them on the same `ItemDisplayShown`
  event. **Nine lines are drawn**, each quoting its id: `class_levels` (3004
  `Level Needed: %1` / 3005 `Level Needed: CANNOT USE`), `mana_cost` (3006),
  the duration (3102 `Spell Duration: %1.` / 3103 `Spell Duration:
  Permanent.`), `casting_skill` (3105 `Skill: %1`), `cast_time_ms` (3107
  `Cast Time: %1`), `range` (6730 `Range = %1`), `recast_time_ms` (9234
  `Recast Delay: %1`), plus `name` -> the `Style_Titlebar` row and
  `spell_icon` -> `IDW_IconButton`'s decal. Trigger: **SHIFT+left click** on a
  memorised gem or a spellbook row -- flagged as a deviation, since the oracle
  uses a right click (fable's right button is mouselook) and, unlike T421's
  inventory slot, plain left was already the cast/memorise action.
  **`spell_fades` is CLOSED and is not a window field**: it is the sibling of
  `cast_on_you`/`cast_on_other`, which `spell_packet_handlers.cpp` prints on a
  landing, so T425 prints it at `BuffManager`'s existing fade callback
  (`src/client/core/everquest.cpp`) when a PLAYER buff wears off.
  **Every other spell field stays hidden because the oracle has no line for
  it** -- measured by grepping `eqstr_us.txt`, not assumed, and each
  `WriteOnlyFields` exemption row now says which absence:
  `resist_type`/`resist_diff` (the table has NO spell-resist line at all --
  every "Resist" hit is a combat message, an AA name, or 9044, the ITEM
  stun-resist stat), `zone_type` and `environment_type` and `time_of_day` (no
  Indoor/Outdoor, environment or time format string), `teleport_zone` (no
  Destination/Location line), `player_tag` (a spells_us.txt internal tag),
  `target_type`/`aoe_range`/`aoe_duration`/`push_back`/`push_up`/
  `allow_rest`/`is_dispellable`/`no_partial_resist` (no such line), and
  `endurance_cost` (5809 `Endurance: %1` is the ITEM stat). **Seven of those
  came up as NEW `WriteOnlyFields` rows the moment the new test WROTE them to
  assert they stay hidden** -- the same shape as T421's `heroic*`. The
  reagents are the one line the oracle HAS (6732 `Reagents Needed: %1`) and
  fable cannot draw: it wants an item NAME and the client has no
  item-name-by-id source at all -- **B391**.
  **SKILL AND ENTITY HALVES STRUCK by T429 Leg B; B298 IS CLOSED.** Both were
  measured against the oracle's own XML and against `eqstr_us.txt`, and
  **neither half turned out to be a missing display** -- the five fields the
  census named are three different defects wearing B298's clothes, each now
  boarded on its own:
  * `SkillData::animation_code` -- not a display field at all (a skeletal
    animation code, `"t02"`), and a **duplicate store**: its one consumer,
    `SkillManager` (`skill_manager.cpp:719`), calls
    `getSkillAnimationCode(skill_id)` directly and never reads the copy.
    **B415 -- CLOSED by T440, the field is deleted.**
  * `SkillData::usable_while_sitting` -- a GATE predicate for a
    while-sitting check that does not exist; no sitting test appears anywhere
    under `src/client/skill/`. **B416 -- CLOSED by T440, the field is deleted:
    eqemu's only two sitting refusals are unreachable from fable.**
  * `Entity::anon_status` -- the oracle's anon lines are the /who formats
    5023/5024, whose `[ANONYMOUS]` is inside the format itself, and the /who
    path parses its OWN wire entries (`who_response.h`), never `Entity`.
    `EQUI_TargetWindow.xml` is a name label (`EQType 29`), an HP gauge
    (`EQType 6`) and a `%` label -- no level, no class, no guild, no anon.
    **B418 (struck by T443, which measured the same conclusion again:
    the /who path parses its own wire entries and never reads Entity)**.
  * `Entity::guildId` -- the oracle DOES print a guild and fable cannot:
    `LoginWorldComponent::WorldProcessGuildsList` is one `LOG_DEBUG` line
    that parses nothing, and `data/willeq.db` has no guild table, so there is
    no guild-id-to-name source anywhere. Same shape as **B391**'s item-name
    gap; printing the id would invent a line. **B418 -- the PARSE half is
    CLOSED by T443** (`OP_GuildsList` is now `eqt::guild::GuildNameTable`);
    the field is still write-only because nothing may DISPLAY it yet, which
    is **B455**.
  * `Entity::deltaHeading` -- a MOVEMENT field, the wire's delta heading and
    the sibling of `deltaX/Y/Z`; no eqstr line and no XML element. **B418
    (struck by T443; this field stays write-only and has no display at all)**.
  What T429 Leg B DID draw is the entity half's real display surface, the
  `/con` line: `eqt::commands::BuildConsiderText()`
  (`src/client/commands/consider_text.cpp`) composes it from eqstr **12239**
  `%1 %2 -- %3` with the faction phrase from **12212..12220**, through Leg
  A's fetch. **That found a live defect B390 predicted**: faction 8
  (`FACTION_THREATENINGLY`,
  `/mnt/projects/akk-stack/code/common/faction.h:34`) printed
  **"threatens you"**, a phrase in NO eqstr row -- the oracle's is 12218
  "glares at you threateningly". The difficulty tail stays fable's own
  wording (**B417**).
  `SpellEffectSlot::base2_value` also stays: the oracle prints no per-effect
  numbers in the display window.
  `SkillData` carries
  `animation_code`, `usable_while_sitting`, `usable_while_stunned`,
  `min_level`; `Entity` carries `guildId`, `anon_status`, `deltaHeading`;
  `RaceModelInfo` carries `genderVariants`, `neuterCode`. Each is a loader
  writing a value with no reader -- spell descriptions, skill
  gating and the `/who` guild/anon columns are the features they belong to.
  Closes per family as those features land; until then the loads are honest
  but inert. Found by T407's census. **Item half CLOSED by T421 Leg A**
  (see above); the `SpellData`, `SkillData`, `Entity` and `RaceModelInfo`
  families are still OPEN under this id.

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

- ~~**B300** (A) `src/client/graphics/environment/spell_effects_config.cpp`
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

  **WILL RULED 2026-09-06:** draw the REAL sprites — effects as close to the original as possible. → T417 (atlas from SpellEffects/*.tga). Now class A.~~
  **STRUCK T417 (this commit)**: fable draws the real sprites. The official
  client's own art now ships as a SECOND particle sheet,
  `data/textures/spell_sprite_atlas.bmp` -- 512x512, 8x8 cells of 64x64, 33
  used -- built by the new `tools/spell_sprite_atlas_builder` from
  `SpellEffects/spel*.tga` and packed into `willeq.eqe1` as ETC1 with an
  `_alpha` companion like every other entry. `SpellEffectsConfig::
  getTextureIndex()` no longer carries ANY name->tile row for a `spells.eff`
  name: on a miss it calls `SpellsEffReader::effectNameToTexture()` -- the
  reader's one implementation of the 0-based rule -- and looks the resulting
  file up in `include/client/graphics/environment/spell_sprite_atlas.h`, the
  shared layout header the builder also includes. There is no second name
  table, so the sheet and the client's addressing of it cannot drift. All
  twelve T389/T409 approximation rows are DELETED, the four figurative
  sprites included: the sword, the flaming skull, the row of skulls and the
  lightning-wreathed sphere now draw themselves. Addressing: a flat ordinal
  below `SpellSpriteAtlas::kBase` (64) is a procedural tile, at or above it a
  sprite cell; `UnifiedParticleRenderer` bins particles by SHEET as well as
  blend mode (four vertex arrays, not two) because the bound texture and
  `uAtlasRegionSize` are per-draw state, and a sprite particle with no sheet
  loaded is SKIPPED, never redrawn with the procedural atlas's UVs.

  **THREE MEASURED CORRECTIONS to what this entry and T409 recorded.**
  (1) **T409's "4x4 sheet of one repeated frame" is REFUTED.** All sixteen
  frames of all 67 `spel*.tga` were hashed: every file has between 5 and 16
  DISTINCT frames. They are animations -- `spelg1` is a sword rotating
  through sixteen headings. (2) **Frame 0 is the wrong frame**, and this was
  not a cosmetic choice: the sheets are fade-in cycles, SEVEN of the 33 files
  have a completely blank frame 0 (`spelb3`, `spelb4`, `spelc3`, `spelc4`,
  `speld4`, `spely3`, `spely4`), and `spelu2` -- Frost Bolt's impact --
  carries 12% of its peak alpha there. The first build of the atlas shipped
  seven empty cells. The rule is now the PEAK frame (greatest total alpha,
  ties to the lowest index). (3) **"36 of the 40 names resolve to a real
  file" is off by two.** 40 distinct names; 36 match the GEN rule; **34**
  resolve to a file that exists; those 34 name **33** distinct files
  (`GENE00` and `GENE01` share `spele1.tga`). The two that resolve to nothing
  on disk are `GENA20_SPRITE` -> `spela3.tga` and `GENA30_SPRITE` ->
  `spela4.tga`, neither shipped under `SpellEffects/`; they keep the
  procedural fallback, as do the four non-GEN names, which T417 gave named
  tiles (`I_SNOWFLAKESPRITE` -> Snowflake, `I_TFIRE_SPB` -> Ember,
  `I_TSMOKE_SPB` -> SmokeWisp, `GEN_Z_SPB` -> WispyCloud).

  Pack: 5,532,254 -> 5,881,918 B (+349,664), 126 -> 128 entries, sha
  `aca03e7f...` -> `584ba1fe...`; `eqe1MappedBytes` +349,664 in each of the
  three gate zones, which is the ONLY visual-gate miss (every image golden
  matched) -- **bless owed, see B357**. Pinned by
  `SpellSpriteAtlasFile.*` (6 cases, incl. `EveryCellEqualsItsSourceFrame`,
  which re-derives every cell from the source TGA rather than trusting a
  golden), `SpellSpriteRealArt.*` + `SpellSpriteAtlasLayout.*` (11), and
  `SpellSpriteSheetDrawTest.*` (6 pixel cases through a real headless GLES2
  point sprite, incl. Frost Bolt's `speld2`/`spelu2`). 17 red before, all
  green after; a planted 0.25 region size turns 4 of the 6 draw cases red.
  What this did NOT change is the burst's SIZE and LIFETIME -- that is B226,
  and its live A/B is **B356**. Prov: T417 Review.

- ~~**B355** (B) `tools/spell_sprite_atlas_builder.cpp` -- **the spell sprite
  sheets ANIMATE, and fable draws one frame of each.**~~ **STRUCK T420
  (this commit, base `ee1743a1`).** All sixteen frames of all 33 sprites now
  ship and a particle walks them over its own lifetime.
  * **Layout.** 512x512 / 64 px cells / 33 cells -> **1024x1024 / 32 px cells /
    32x32 grid, 528 of 1024 used**, cell = `sprite * 16 + frame`, so a sprite
    occupies exactly half a row and reads left to right in a contact sheet
    (`review/T420/sheets/`). The cell SHRANK because 33 x 16 at 64 px needs
    2048x2048 -- 5.6 MB of ETC1 pair on every zone's mapped budget and on a
    512 MB Orange Pi -- against 1.4 MB at 32 px, and a spell point sprite is
    clamped to 100 screen px and measures single digits to ~30 px in
    practice, so 32 px is still oversampled. 64 px frames are reduced 2:1 by
    an ALPHA-WEIGHTED integer box average (an unweighted one fringes every
    edge, because a transparent texel's RGB in these files is arbitrary and
    the additive blend samples RGB directly); 16 px frames are magnified 2x
    nearest; 32 px frames are copied.
  * **NO SHADER RESTRUCTURE WAS NEEDED and the plan's STOP did not fire.**
    The atlas offset was already computed on the CPU per particle per frame
    in `UnifiedParticleRenderer::render()`'s binning loop and handed to the
    vertex shader as an attribute. The flat ordinal a particle carries still
    names a **SPRITE**, never a cell -- `EmitterConfig::textureRegions[]` is a
    `uint8_t[4]` and 528 does not fit -- and the CELL is derived at draw time
    from `SpellSpriteAtlas::frameForNormalizedAge(p.getNormalizedAge())`.
    `SpellEffectsConfig::getTextureIndex()` did not change.
  * **The rule is ONE PLAYTHROUGH PER PARTICLE LIFETIME** (frame 0 at birth,
    frame 15 as it dies, clamped rather than wrapped). T420 MEASURED the
    oracle (`review/T420/oracle_frame_rate/`) and **refuted an effect-wide
    clock**: one render of Whirling Wind holds six sprites on one animation
    phase and eight on another simultaneously. Per-particle playback is what
    survived. The RATE could not be pinned -- the T389 capture holds only ~5
    new renders/s during Whirling Wind and ~1.2/s during Weaken, so Nyquist
    stops at 2.5 cells/s -- but the defensible bound is **below ~3 cells/s**,
    consistent with one 16-cell playthrough over the ~6 s effect. See
    **B370**: fable's 0.4-0.8 s particle lifetimes make that 20-40 cells/s,
    an order of magnitude fast, and the fix is a lifetime, which is B226/B356's.
  * **Two record corrections.** (a) T409's "one repeated frame" stays
    refuted. A sha1 census of all 16 frames of all 33 files gives **8 to 16
    BYTE-distinct frames** each, with exactly one file (`spelh1`) whose frames
    8..15 are byte-identical to 0..7. But an RMS distance matrix
    (`review/T420/oracle_frame_rate/sheet_cell_duplicate_matrix.txt`) shows
    `speln1`, `spela2` and `spelg1` -- byte-distinct at 16, 16 and 10 -- have
    frame f and frame f+8 within **0.0-0.3 of 255**, i.e. VISUALLY the same.
    So several sheets are effectively **8 phases played twice**, and the
    shipped 16 cells reproduce that faithfully rather than asserting 16
    different pictures. Both measurements are in the record because each
    answers a different question. (b) **These are NOT uniformly fade-in cycles**:
    six files have a completely blank frame 0 (`spelb3`, `spelb4`, `spelc3`,
    `spelc4`, `speld4`, `spely3`; `spely4` is a seventh at 256/11,418), FIVE
    PEAK at frame 0 and decay (`spela2`, `spelb2`, `spelj2`, `spelu1`,
    `spely2`), and the rest ramp to a peak in the middle. Do not re-write the
    justification as "they are fade cycles".
  * Pack **5,881,918 -> 6,930,494 B (+1,048,576, exactly 1 MiB)**, entry count
    unchanged at 128, sha `584ba1fe...` -> `14a6bd45...`; BMP 1,048,698 ->
    4,194,426 B. `eqe1MappedBytes` **+1,048,576 in each of the three gate
    zones, the ONLY visual-gate miss -- every image golden matched.**
    **Bless owed, see B371.** Pinned by `SpellSpriteAtlasFile.*` (8, incl.
    `EveryCellEqualsItsSourceFrame` over all 528 cells and
    `TheSixteenFramesOfASpriteAreNotAllTheSame`), `SpellSpriteSheetDrawTest.*`
    (9 pixel cases through a real headless GLES2 point sprite, incl.
    `AParticleWalksItsSheetFromBirthToDeath`) and `SpellSpriteAnimation.*` +
    `SpellSpriteAtlasLayout.*`. **14 red before, all green after.**
    Prov: T420 Review.

- ~~**B356** (B) `config/spell_effects.json` `styles.impact` -- **T417's Leg C
  did not run: the burst size/lifetime A/B against the oracle is still
  owed.** T417 was to match count, size and lifetime for spells 54 and 93
  against T389's oracle videos, one parameter at a time with weather and
  heading pinned (T405's rig). `/mnt/projects/fable/.eqemu-lock` read
  `T418 2026-09-06` for the whole unit and the protocol forbids waiting on
  it, so the plan's own STOP applied. Nothing was measured and nothing was
  changed. The gap is unchanged from B226: fable paints 36 particles of
  0.05-0.15 world units for 0.4-0.8 s, the oracle hundreds of sprites over a
  third of the screen for 2.0-3.3 s. **What T417 DID change is what each
  particle looks like**, so a capture pair taken now would differ from
  T389's in two ways at once and prove nothing about size -- take the A/B
  first, then the pair. Closes with the measured parameter table and a live
  capture. **ANNOTATED T420 (this commit)**: T420 changed what each particle
  looks like AGAIN -- it now ANIMATES -- so the "two things at once" warning
  above still stands and the A/B is still owed. Add the animation RATE to the
  parameter table when it is taken: it is the same lifetime knob (**B370**).
  Prov: T417 Review; B226.~~ **CLOSED by T422 (this commit,
  `refactor/T422_spell_effects_live_ab.md`).** The A/B ran: the official client
  was re-captured at 30 fps x11grab -- five to six times its own MEASURED
  4.4-5.8 renders/s, so every distinct render is sampled once instead of
  aliased -- with nameplates and corpses off (run1 proved they otherwise fake
  80-100 "particles" per render), and fable was captured the same way at its
  own measured 13-18 renders/s. The parameter table, each value with its probe,
  is in `review/T422/README.md` 4 and repeated in the T422 Review.
  Headline: combined peak population **81 -> 648** against the oracle's 561
  (Weaken) to 715 (Burst of Flame); the one-shot `burstCount` became a
  `spawnRate` over a 0.6 s `emitterLifetime` because the oracle EMITS
  CONTINUOUSLY for 1.5-2.0 s; lifetimes **0.4-0.8 -> 1.6-2.0 s** from a
  death-free rise; sizes **x1.57** from a live probe that moved the median
  sprite diameter 3.57 -> 5.53 px against the oracle's 4.4-6.9. `expandSpeed`
  4.0 was MEASURED RIGHT (the oracle spreads ~4.2 units/s) and left alone.
  Residuals: duration 2.16 s against 2.33-2.6 s (-12%), and the emission SHAPE
  is unmatched -- see **B376**. Pinned by
  `tests/test_spell_burst_oracle_match.cpp` (9 tests, 8 red pre-fix).

- ~~**B357** (P) `tests/goldens/manifest.json` memory-coverage
  `eqe1MappedBytes` -- **a bless is owed for T417's pack growth, and the
  visual gate is RED on `task/T417-real-spell-sprites` until it is given.**
  T417 added `spell_sprite_atlas` and its `_alpha` companion to
  `willeq.eqe1` (5,532,254 -> 5,881,918 B, 126 -> 128 entries, sha
  `aca03e7f...` -> `584ba1fe...`), and all three gate zones' mapped bytes
  rose by **exactly the pack's own byte delta, 349,664**: qeynos2
  122,665,822 -> 123,015,486; gfaydark 121,331,914 -> 121,681,578; befallen
  115,816,410 -> 116,166,074. **Every other gate check PASSED** -- every
  image golden matched (no golden moved, so the plan's STOP did not fire),
  CPU p95 under ceiling in all three zones (5.277/6.291/6.370 against
  10.108/10.672/10.948), budget block clean. The manifest was NOT touched:
  an executor never self-blesses. Closes when Will blesses the three numbers.
  Prov: T417 Review; the same class as B228 and B315.~~ **APPLIED at the T417 merge under Will's B240 bless: 123,015,486 / 121,681,578 / 116,166,074.**
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
  **ANNOTATED T449 (this commit) -- the three runs happened and the entry is
  PARTLY answered; it stays open on `two_client_survive` alone.**
  `zone_in` through `scripts/scenario.sh` exits **0**
  (`zoned in to 'qeynos2' after 4.82 s`), and `restore_snapshot()` -- the half
  of this entry nobody had exercised -- ran **four** times and reported
  `snapshot restored byte-identically` every time, so B305(b) is settled.
  B305(a) is settled the OTHER way from the way it was framed: `#memspell 46 0`
  is accepted and the client logs `Server memorised 'Ultravision' (46) into gem
  1`, so the syntax is fine -- but spell 46 has `spells_new.classes12 = 255`
  and the config's Testcaster is a Wizard, so every cast fizzles and the lane
  cannot pass as shipped (**B476**). With spell **288** it exits 0 on the first
  gem cast. `two_client_survive` exits **1** and its peer is structurally
  unable to hold (**B477**). Two further defects fell out of the same runs:
  **B475** (`up.sh`'s non-launcher path duplicates the container's own world)
  and **B478** (a re-run inside the buff duration can never pass). Evidence
  `review/T449/README.md` §5 and `review/T449/scenario/`. This entry closes
  when `two_client_survive` passes for the right reason.

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

- ~~**B322** (B) `src/client/graphics/texture_generators.cpp`
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
  glittersp501.dds and Corona1.dds are ALL missing. Found by T413.~~
  **CLOSED by T424 Leg A** (`0911184a`, `refactor/T424_fallback_multiplier_sprites.md`):
  `generateSpellParticleFallback()` now writes premultiplied white x cosine into
  RGB and leaves alpha opaque, which is exactly what the additive draw samples.
  Measured on the shipped file -- centre-to-edge R along y=16 went
  `255,255,...,255` to `12,37,61,85,108,130,151,170,188,204,218,230,239,247,251,254`,
  corner (0,0) 255 -> 0, distinct pixel byte values 1 -> 84. The file stays
  24-bit (32-bit would cost ~8x the pack bytes for a channel no shader reads).
  The full cascade ran in ONE commit: `data/textures/spell_particle_fallback.bmp`,
  `data/textures.sha256`, the shared pack, `data/willeq.eqe1.sha256`. Pack
  **6,930,494 -> 6,930,116 bytes (-378)**, sha `14a6bd45` -> `19e78745`,
  installed atomically at 20:27:44 with no `willeq`/ctest/gate process live
  (`main` had just finished its own `ctest -j6`); the pre-T424 pack is kept at
  `review/T424/willeq.eqe1.before_T424`. Pinned by
  `TextureAlphaCensus.SpellParticleFallbackCarriesItsFalloffInRgb` (the BMP's
  profile IS the generator's cosine, per texel) and
  `.SpellParticleFallbackPackEntryCarriesTheSameShape` (the pack entry's ETC1
  level 0 must encode to more than 8 distinct 8-byte blocks out of 64; the
  pre-fix pack encoded to exactly **1**, because a uniform image compresses to
  64 identical blocks -- so the shipped payload can be told apart from a flat
  square with no ETC1 decoder at all). Both FAILED pre-fix.
  **B286 goes with it** -- both of its proposed code-side fixes were already
  refuted by T413's measurement, and the generator fix is the one this file
  needed. The kNeverAlpha reason string in
  `tests/test_texture_alpha_census.cpp` is rewritten accordingly.
  ⚠ (APPLIED at the T424 merge under the B240 bless.) `eqe1MappedBytes` in `tests/goldens/manifest.json` was 378 bytes high in
  all three gate zones (124064062/122730154/117214650 expected against
  124063684/122729776/117214272 measured). T424 did NOT edit the manifest;
  the re-bless is the orchestrator's.


- ~~**B330** (B) `include/client/graphics/environment/rain_overlay.h` --
  **the rain overlay cannot tilt.** `windTiltBase` / `windTiltIntensity` were
  parsed from `weather_effects.json` (clamped, ranged) into fields nothing read;
  T414 Leg A deleted the keys, the parse and the fields. Honouring them needs a
  rotated-quad draw: `RainOverlay::render()` tiles the streak texture with
  axis-aligned `draw2DImage` rects, so a screen-space angle has nowhere to go.
  Closes when the overlay can draw a rotated quad and the two keys come back
  together with a test. Found by T414 Leg A.~~ **CLOSED by T427 (2026-09-07,
  this commit): THE ORACLE DRAWS VERTICAL RAIN, so the two keys were fiction
  and there is nothing to honour. No code change.** Measured off T418's oracle
  captures (`review/T418/oracle/`, five sessions of the official Titanium
  client in qeynos2). Method: consecutive-frame differences at the capture's
  5 fps isolate the moving streaks from a static scene; each difference image
  is sheared by a candidate angle and the variance of its column sums is
  maximised, which finds the streaks' principal axis to ~0.1 deg. Four
  independent aggregates, angle measured FROM VERTICAL:

  | state | frame-pairs | best angle | peak/median |
  |---|---|---|---|
  | day 11:00, rain intensity 9 (o1) | 99 | **+0.0 deg** | 4.30 |
  | day 11:00, rain intensity 5 (o5) | 174 | **+1.1 deg** | 3.02 |
  | night 22:00, rain intensity 9 (o4) | 174 | **+0.3 deg** | 1.39 |
  | night 22:00, snow intensity 5 (o4) | 174 | **+0.6 deg** | 1.16 |

  Twelve single-frame measurements (active-minus-clear stills at intensities
  2/5/9 plus snow, two crops each) span -3.3 deg to +1.9 deg and **disagree in
  SIGN between the sky crop and the wall crop of the same frame**, which is
  what a null looks like. **The angle does not move with intensity** (2, 5 and
  9 land within 1.1 deg of each other), so even a wind-driven tilt keyed on
  intensity has no support. fable's overlay already tiles a vertical streak
  texture with axis-aligned rects, i.e. it already matches. Evidence and the
  scan script: `review/T427/legA/`.

- **B331** (B) `include/client/graphics/weather_quality_preset.h` -- **lightning → issue #127
  has no branches to bound.** `WeatherPresetValues::maxBranchLevel` carried
  0/2/4/6 per quality preset and the shipped `storm.maxBranchLevel` key carried
  4; there is no lightning-bolt branch generator anywhere in the tree, and
  `applyToWeatherConfig()` said so in a comment ("would need to be added to
  WeatherEffectsConfig"). T414 Leg A deleted the field, the four preset lines,
  the comment and the key. Closes when a branching bolt lands and re-adds all of
  them together. Found by T414 Leg A.

- **B332** (B) `include/client/graphics/constrained_renderer_config.h` -- → issue #128
  **`enableItemIcons` promised a RAM saving that nothing could deliver.** The
  `orangepi` preset set it false in code and `constrained_presets.json` set it in
  two presets; there is no item/spell icon texture loading in the tree at all --
  `static_panels.cpp` draws abbreviated item names with "placeholder until icon
  atlas" beside them, and `StaticPanels::iconId` is commented "for future icon
  atlas". T414 Leg A deleted the field, the parse and the shipped keys. Closes
  when an icon atlas exists and the flag gates its load. Found by T414 Leg A.

- **B333** (B) `include/client/graphics/constrained_renderer_config.h` -- → issue #129
  **`frontToBackSorting` was a second, dead spelling of a live feature.** The
  preset key parsed into a field nothing read. The real front-to-back zone draw
  is `manualZoneDrawEnabled_` (`/sort`, `RenderPassTimer::OnRenderPassPreRender`);
  this one never reached it. T414 Leg A deleted the field and the parse. Closes
  if a preset should be able to preset `/sort` -- then it wires to
  `manualZoneDrawEnabled_`, not to a new flag. Found by T414 Leg A.

- **B334** (B) `include/client/graphics/detail/detail_types.h` -- → issue #130
  **`DetailType::windHeightBias` has no vertex to act on.** The key was parsed
  per detail type and the field never read. `DetailChunk` builds a crossed quad
  with only BASE and TOP vertices and pushes `windInfluence_` = 0 at the base and
  `windResponse` at the top, so a "height at which wind is strongest" has no
  mid-height vertex to move. T414 Leg A deleted the parse and the field. Closes
  when the detail quad gains a mid vertex (or a vertex shader takes the bias as
  a uniform) and the key returns with a test. Found by T414 Leg A.

- **B335** (B) `config/weather_effects.json` `storm` -- **four shipped storm keys → issue #131
  with no field at all.** `fogDensityMultiplier`, `fogColorR`, `fogColorG`,
  `fogColorB` and `lightningIntensityThreshold` were in the shipped file;
  `WeatherEffectsConfig::Storm` has no such members, and the loader's only
  acknowledgement was an empty `if (json.isMember("fogDensityMultiplier")) { }`
  whose body was two comments. So a storm has no configurable fog tint and
  lightning has no configurable intensity gate. T414 Leg A deleted the keys and
  the empty stub. Closes when the storm fog tint and the lightning threshold are
  real fields with consumers. Found by T414 Leg A.

- ~~**B336** (B) `include/client/graphics/detail/detail_types.h`
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
  **CLOSED T445.** `DetailManager::setZoneConfig()` is now the one place the
  radius is set and it derives it as `detailChunkRadius(viewDistance,
  chunkSize)` (`detail_types.h`), which both `loadZone()` branches route
  through; `tests/test_detail_view_distance.cpp` pins the arithmetic, the
  wiring and the shipped pair, and `WriteOnlyFields` dropped this row because
  the field is read. **Two corrections to the entry above.** (1) The shipped
  value is **150**, not 200 -- `config/detail_objects.json` has said 150 since
  it was written and `git log -S` finds no 200 in its history -- so the grid
  goes 5x5 -> **7x7**, not 9x9. (2) "changes both the visual gate and the
  frame cost" is **false on the gate half**: the gate runs
  `gles2_zone_benchmark`, which names neither `DetailManager` nor detail
  anywhere, so it never builds a chunk. Measured, not assumed -- no image
  golden moved, and `draws`/`polys`/`regions_drawn` are byte-identical in all
  three zones and all three frames (`review/T445/`). The real frame cost of
  the 7x7 grid is therefore still UNMEASURED and there is no instrument for it
  -- see **B460**. The oracle's own detail draw distance remains unmeasured
  too, so 150 is fable's number, not EverQuest's.

- **B337** (B) `include/client/graphics/eq/race_model_loader.h`, → issue #132
  `equipment_model_loader.h`, `include/client/graphics/tree_identifier.h` --
  **diagnostic aggregates filled and never printed.** `MemoryStats::
  loadedModelCount` / `variantMeshCacheCount` / `animatedMeshCacheCount` /
  `armorTextureCount` / `meshCacheCount` and `TreeIdentifier::Stats::
  treePatternCount` / `leafPatternCount` / `zoneTreeMeshCount` /
  `excludedMeshCount` are computed for reports that do not print them. Closes
  when each is printed (`/pmem`, the tree report) or dropped. Classified
  `report` in the census table. Found by T414 Leg C.

- ~~**B338** (A) `include/client/graphics/constrained_texture_cache.h` --
  **two cache flags a caller sets and the cache ignores.** `bgThreadPool_` is
  handed a real `BackgroundThreadPool*` by a setter and read by nothing, while
  `renderer_zone_draw.cpp` carries a comment about "async background decode
  submits to bgThreadPool_"; `compressedTexturesAvailable_` is written four
  times by the ETC1 support probe and consulted nowhere. Same shape as the
  closed B291 (`skipEntityTextureUpload`): a caller supplies an intended value
  that changes nothing. Closes when both are consumed or both are deleted with
  their setters. Found by T414 Leg C.~~
  **CLOSED by the `task/kb-b338-texture-cache-dead-flags` card (issue #17,
  2026-09-18)**: both members deleted with their setter
  (`setBackgroundThreadPool` and its `renderer_init.cpp` call), the two stale
  "bgThreadPool_" comments, and both `boarded` EXEMPT rows; WriteOnlyFields
  went RED with the exemptions removed and the fields present, GREEN after.
  The ETC1 probe was reduced to a diagnostic log (its member-writes were the
  only thing deleted) so it stays the sole consumer of
  `config_.enableCompressedTextures`.

- **B339** (B) `include/client/graphics/weather_effects_controller.h` → issue #133
  `targetIntensity_` -- commented "For smooth transitions", written on every
  intensity change and on reset, read by nothing: weather intensity is applied
  outright, never interpolated toward a target. Closes when the transition
  exists or the member goes. Found by T414 Leg C.

- **B340** (B) `include/client/graphics/zone_lifecycle/zone_lifecycle_component.h` → issue #134
  `entitiesLoaded_` / `expectedEntityCount_` -- **the header already documents
  them as write-only.** L09 deleted their zero-caller accessors and left the
  members and their four writes behind. Closes when they go with the writes, or
  a consumer for the zone-in entity gate appears. Found by T414 Leg C.

- **B341** (B) `include/client/graphics/overlays/overlays_component.h` → issue #135
  `currentTargetInfo_` / `currentTargetLevel_` -- target-overlay state cached
  when the target changes and never read by the draw. The target outline and
  name plate get their data elsewhere. Closes when the overlay draws from them
  or they go. Found by T414 Leg C.

- **B342** (B) `include/client/inventory/inventory_manager.h` and → issue #136
  `include/client/graphics/environment/particle_manager.h` -- **four registered
  callbacks that are never invoked.** `InventoryManager::moveItemCallback_`,
  `deleteItemCallback_`, `equipmentChangedCallback_` and
  `ParticleManager::entityDirCallback_` each have a setter a caller uses and no
  call site. A registered callback that never fires is worse than none: the
  registrant believes it is wired. Closes when each is called or the setter and
  member go together. Found by T414 Leg C.

- **B343** (B) `include/client/graphics/ui/character_model_view.h` → issue #137
  `inventoryManager_` and `include/client/graphics/eq/animated_mesh_scene_node.h`
  `sceneManager_` -- stored collaborator pointers with no reader. Closes when
  each is used or dropped from the constructor/setter. Found by T414 Leg C.

- **B344** (B) `include/client/bridge/bridge_component.h` → issue #138
  `m_audio_config_vendor_music`, `include/client/player/player_component.h`
  `m_duel_target_id`, `include/client/skill/skill_manager.h` `m_player_race` --
  three pieces of state stored at the point a decision is made and never
  consulted when the decision is taken: the vendor-music config reaches no audio
  call, `/duel` records the target id the reply path does not use, and the
  player race is stored for skill gating that does not gate on race. Closes per
  member. Found by T414 Leg C.

- **B345** (B) `include/client/graphics/camera_controller.h` → issue #139
  `orbitAngleInitialized_`, `include/client/output/console_renderer.h`
  `m_curEnd` / `m_playerHeading` / `m_startTime`,
  `include/client/input/hotkey_manager.h` `ConflictInfo::binding1` / `binding2`
  -- write-only leftovers in three unrelated classes: an orbit-camera
  first-frame latch, three headless-console-renderer members, and the two
  binding strings a hotkey conflict report never formats. Closes per class.
  Found by T414 Leg C.

- **B346** (B) simulation/renderer per-frame payload fields with no consumer -- → issue #140
  `SimulationWorker`'s `EntityResult::shouldDeactivate` (set by the worker,
  ignored by the drain) and `SimulationInput::cameraTarget`,
  `EntityVisual::corpseYOffset` (computed per corpse, never applied to the
  node), `ParticleEmitter::spawnRadius_` (seven callers set it; the spawn does
  not use it), `SpellEffectInstance::spellID`, `RendererEvent::intData`,
  `SpellActionInfo::level2`. Same class as B296 one layer down: a producer
  computes a value for a consumer that never reads it. Closes per field. Found
  by T414 Leg C.

## Added 2026-09-06 by T415

- ~~**B335** (B) `include/client/graphics/ui/tradeskill_layout.h`,
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
  window (fable has no combat skill select window). Found by T415 Leg A.~~
  **STRUCK T419 Leg A**: `ZoneProcessRecipeReply` publishes, and the left side
  draws. The decode is now the free function `eqt::merchant::DecodeRecipeReply`
  (`src/client/merchant/merchant_vendor_bank.cpp`), which fills a
  `TradeskillRecipeReceivedData` from the server's own struct --
  `RecipeReply_Struct { uint32 object_type; uint32 some_id; uint32
  component_count; uint32 recipe_id; uint32 trivial; char recipe_name[64]; }`,
  eqemu `/mnt/projects/akk-stack/code/common/eq_packet_structs.h:4313`, 84 bytes,
  one packet per matching recipe from `Client::SendTradeskillSearchResults`
  (`/mnt/projects/akk-stack/code/zone/tradeskills.cpp:866`).
  Two new `GameEventType`s carry it: `TradeskillRecipeReceived` (one reply) and
  `TradeskillRecipeListCleared`, published by the client's OWN
  `SendRecipesSearch` / `SendRecipesFavorite` because **eqemu marks neither the
  start nor the end of a reply run** -- the request is the only honest list
  boundary. `IrrlichtBridge` feeds both into `TradeskillPanelState::recipes`
  through `applyTradeskillRecipeReceived()` /
  `applyTradeskillRecipeListCleared()`, and `renderTradeskillRecipeList()` (new
  TU `src/client/graphics/ui/tradeskill_recipe_list.cpp`) draws
  `COMBW_LeftSideArea` from thirteen new rects in `tradeskill_layout.h`, all
  derived from the XML's anchors and written out there: `COMBW_SearchArea`'s
  three labels + three editboxes + `COMBW_SearchButton`,
  `COMBW_KnownRecipeText`, the `COMBW_RecipeList` listbox with its two
  `<Columns>` (Recipe Name 140, Trivial 50) and sixteen rows,
  `COMBW_FavoritesText` and the three favourites buttons. A row click pushes
  the new `RecipeDetailsRequestIntent`, whose consumer arm calls the
  `SendRecipeDetails()` that `/recipedetails` already used -- a second producer
  for an existing sender, not a second packet. **Two things the wire does NOT
  carry, so they are not drawn: the recipe's TRADESKILL** (the server filters by
  it in SQL before building the packet) **and its COMPONENT NAMES** (sent only
  in reply to OP_RecipeDetails) -- so a row shows a name, a trivial and a
  component count. The search box and the three favourites buttons are DRAWN
  and INERT, because fable's static UI has no text-entry widget outside the
  chat box: board **B365**. Pinned by `tests/test_recipe_list.cpp` (12 cases;
  pre-fix the file does not compile -- none of the symbols exist). Capture:
  `review/T419/after/tradeskill_800x600.png`,
  `review/T419/crops/tradeskill_after_2x.png`. **Live proof (open a forge, run
  `/recipesearch`, see the rows) is OWED to the next lock unit.**

- ~~**B454** (B) `src/client/graphics/ui/static_ui_input.cpp`
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
  Found by T415 Leg A.~~ **CLOSED by T443** (`refactor/T443_guild_list_esc_dumpscene.md`):
  `processKey()` takes a `TradeskillPanelState&` and its ESC handler asks the
  container FIRST -- it is the topmost centre popup, drawn after the loot
  popup -- pushing `TradeskillCloseIntent{objectId}` and leaving `isOpen`
  alone; only `applyTradeskillContainerClosed()` clears it. The pattern the
  entry says was missing was already there: the loot arm. All four
  server-owned windows now obey T290 on ESC -- loot (`CloseLootIntent`, T290),
  bank (`CloseBankIntent`) and merchant (`VendorToggleIntent`), both wired by
  T436 in `FrameComponent`'s escape handler and pinned here as invariants,
  and the container. Pinned by `tests/test_esc_server_owned_windows.cpp`
  (9 cases, 4 red pre-fix). **Live proof is OWED** -- T443 held no lock
  (T438 did). What the ORACLE does on ESC with a vendor or a container open
  is still unmeasured: T342's captures record merchant open/select/buy/sell
  and contain no ESC event at all, so fable's shape here is reasoned from
  T290, not measured. That measurement is **B456**.

---

## Added 2026-09-06 by T416

- **B350** (C) `tools/ui_atlas_builder.cpp` `generate()`, and every panel that → issue #18
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

  **T432 note (per-character ini, board B275).** The oracle's own
  `UI_<Char>_<server>.ini` carries per-window LOOK keys that bear directly on
  this decision and that fable does **not** honour: `BGTint.red/green/blue`
  (all `255` in every file checked), `BGType=1`, `Alpha=255`,
  `FadeToAlpha=255`, `Fades=true` with `Delay=2000`/`Duration=500`, `Locked`,
  and `[Main] UISkin=Default` / `AtlasSkin=Default`. So the official window
  background is a per-window **user-tintable, user-fadeable** plate whose
  shipped state is fully opaque (`Alpha=255`) and untinted -- which is
  corroboration for "the oracle's windows are opaque" from a second, textual
  source, and it says the opacity is a SETTING there rather than baked art.
  T432 persisted none of them (Will's ruling scoped this unit to open/closed
  state and the camera); they are listed here so whoever rules on B350 knows
  the oracle's own knobs. Measured in
  `/mnt/projects/EverQuestP1999/UI_Obian_project1999.ini`.

- ~~**B351** (B) `include/client/graphics/ui/ui_atlas.h` `UISprite` -- **six of
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
  name map and the enum together. Found by T416.~~
  **CLOSED by T424 Leg C** (`0911184a`): all six were DELETED from
  `tools/ui_atlas_builder`, `UIAtlas::mapSprite`'s nameMap and the `UISprite`
  enum together, and the atlas roster is now fifteen sprites with a consumer
  each. The disposition is "leftover", not "consumer waiting", and the reason is
  measured rather than aesthetic: **none of the six is oracle art**. Every one is
  a procedural gradient or a 1px border invented for the WindowManager UI deleted
  in `0f25cb57`, and the two with an obvious caller cannot take them as they are
  -- a 64x24 `button_normal` gradient stretched onto a loot button of another
  size, or a 32x32 one-pixel `panel_border` stretched across a 400px popup,
  invents chrome nobody measured against the official client. That is B350's
  open look question, and the honest starting point for it is
  `window_pieces01.tga`, not these regions; boarded as the new **B385**. Pinned
  by the new `tests/test_ui_sprite_roster.cpp` (5 cases, 4 RED pre-fix): the
  enum, the nameMap, the builder and the SHIPPED `ui_atlas.json` inside
  `willeq.eqe1` must name the same fifteen, and `EveryShippedSpriteHasAConsumer`
  fails on the next region that ships with nothing to draw it. The atlas change
  went into the SAME commit as Leg A's pack install so the shared pack was
  installed exactly once.


- ~~**B352** (B) `src/client/graphics/ui/static_panels.cpp` `renderPetPanel` and
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
  a one-constant edit. Found by T416.~~
  **STRUCK T419 Leg B**: the row is a GRID and it is inside the frame.
  `include/client/graphics/ui/pet_layout.h` (new) quotes
  `EQUI_PetInfoWindow.xml` -- a 154x142 `Style_Sizable` window with
  `PIW_PetHPGauge` (2,0) 108x26, `PIW_AttackButton` (2,28) 108x20 and a
  two-column grid of 54x20 cells at x=2/x=56 with rows at y=50/74/96 -- and
  `PetLayout::buttonRect()` is the only place a pet button rect is computed.
  `UILayout::computeLayout()` now sizes `petPanel` from the same two constants
  the grid is drawn from (`PET_BUTTON_ROWS` 2, `PET_BUTTON_H` 10), 40px -> 52px,
  so the grid cannot fall out of the frame the way the text run did. **Two
  deviations from the XML, both written out in that header rather than
  absorbed**: (1) fable's `PetButton` set is eqemu's TEN (Sit, Stop, Regroup,
  Follow, Guard, Taunt, Hold, GHold, Focus, SpellHold) and only five of them
  have a cell in the Titanium XML at all, so ten cells cannot be laid on a grid
  drawn for seven; (2) the grid is **5x2, not the oracle's 2x5**, because
  MEASURED at 640x480 `petPanel` starts at y=172 under a 94px group panel under
  a 70px status block with the buff bar's top edge at y=228 -- SIXTEEN pixels
  of slack, where the oracle's five-row grid needs 100. Restoring the oracle's
  shape needs a pet window that is not stacked in that column: board **B366**.
  Note also that this entry said the tenth button is "Shrink"; it is
  **SpellHold** (`PET_BUTTON_SPELLHOLD`, `include/client/pet_constants.h`) --
  fable has no Shrink pet button. Pinned by `tests/test_pet_buttons.cpp`
  (5 cases, **3 failing pre-fix**: `EveryButtonRectLiesInsideThePanel`,
  `TheTenthButtonHasItsOwnCell`, `ThePanelIsSizedFromTheGridsOwnConstants`).
  Before/after: `review/T419/crops/pet_before_after_4x.png`.

- **B353** (P) `scripts/ci/visual_gate.sh` -- **proposal: the gate should
  photograph the UI.** Its three zones capture geometry only, which is why
  T411's atlas-alpha change moved no golden at all and needed T416 to look at
  it by hand. `tools/hwcaps/ui_panel_capture` now renders all sixteen panel
  captures headlessly in ~10 s with no server and no zone, so a `UiPanels`
  stage would be cheap; it would also add sixteen image goldens to the set
  Will has to bless on every UI edit. **The gate is Will's to extend** -- T416
  boarded this rather than doing it, per its own plan. Found by T416.

---

## Added 2026-09-06 by T419

- ~~**B365** (B) `src/client/graphics/ui/tradeskill_recipe_list.cpp`,
  `src/client/graphics/ui/static_ui_input.cpp` -- **the tradeskill window's
  search box and favourites buttons are DRAWN and INERT.** T419 Leg A drew
  `COMBW_SearchArea` (`COMBW_MinTrivialEdit`, `COMBW_MaxTrivialEdit`,
  `COMBW_SearchTextEdit`, `COMBW_SearchButton`) and `COMBW_FavoritesArea`
  (`COMBW_AddFavoriteButton`, `COMBW_RemoveFavoriteButton`,
  `COMBW_ShowFavoritesButton`) because leaving a hole where the oracle's chrome
  belongs is what B335 complained about, but none of the seven is clickable and
  `TradeskillButton` deliberately has NO enumerator for them -- an enum name no
  hit test can return would be the same unreached-code defect one layer up. The
  reason is structural, not laziness: **fable's static UI has no text-entry
  widget outside the chat box** (`StaticUIInput` hit-tests slot grids and
  buttons; the only keyboard consumer is `processChatInput()`), so there is
  nothing for the three editboxes to be. The search itself is reached from
  `/recipesearch`, and the boxes echo what that command last asked for
  (`TradeskillRecipeListClearedData` carries the query and both trivial
  bounds). Closes when the popup grows a focusable edit field and the four
  buttons push intents -- `SendRecipesSearch` / `SendRecipesFavorite` already
  exist and are already reached from the command processor. Found by T419 Leg A.~~ **CLOSED by T421 Leg B**
  (`f35a4517`). fable now has a text-entry widget, and there is exactly
  ONE: `EQT::Graphics::TextEntryState` +
  `applyTextEntryKey()`/`focusTextEntry()`/`clearTextEntry()`
  (`include/client/graphics/ui/text_entry.h`,
  `src/client/graphics/ui/text_entry.cpp`). **`ChatPanelState` DERIVES from
  it** -- `inputText`, `cursorPos` and `inputActive` keep the names chat has
  always used, so every chat call site and every chat test is untouched -- and
  `FrameComponent::processChatInput()`'s inline key drain is now a call to
  that one body plus chat's own policy on the four actions it returns. The
  tradeskill window's three Editboxes are three more `TextEntryState`s on
  `TradeskillPanelState`, with a `TradeskillField focusedField` saying which
  holds the caret; `hitTestTradeskillField()` focuses one and a click
  elsewhere in the window blurs it. **`TradeskillButton` gained the four
  enumerators T419 withheld** -- `Search`, `AddFavorite`, `RemoveFavorite`,
  `ShowFavorites` -- and `hitTestTradeskillButton()` returns them. Enter in
  any of the three boxes, or a click on `COMBW_SearchButton`, pushes the new
  `RecipeSearchIntent{query, minTrivial, maxTrivial}` into the EXISTING
  `MerchantComponent::SendRecipesSearch()`; the favourites row pushes
  `RecipeFavoriteIntent{op, recipeId}` into the EXISTING
  `SendRecipesFavorite()`. No second packet and no second entry widget.
  **The keyboard GATE stays in one place**: `processChatInput()` is still the
  only caller of `setChatInputFocused()`, and it early-returns into
  `FrameComponent::processTradeskillSearchInput()` when a search box has the
  caret -- which is why `popKeyEvent`'s entry in
  `tests/test_read_and_reset_ownership.cpp` went 1 -> 2 call sites with the
  `getMouseWheelDelta` "two textual sites, one consumer, mutually exclusive"
  reason. **Two things the search box does NOT do**, both deliberate: a
  trivial bound that will not parse is 0 (= no bound, what `/recipesearch`
  sends when the player omits it), never a silent filter; and Tab is a no-op
  in a search box, because a recipe name is the server's vocabulary and
  T357's rule is that Tab with no vocabulary leaves the text alone.
  Pinned by `tests/test_search_entry.cpp` (9 cases).

- **B366** (C) `include/client/graphics/ui/ui_layout.h`, → issue #19
  `include/client/graphics/ui/pet_layout.h` -- **fable's left HUD column cannot
  hold the oracle's pet window, and which way to fix that is a look decision.**
  Measured at 640x480, the smallest supported resolution: `playerStatus` ends at
  y=70, `groupPanel` runs 74..168, `petPanel` starts at 172 and the buff bar's
  top edge is at y=228 -- **sixteen pixels of slack**. `EQUI_PetInfoWindow.xml`
  is a 154x142 window whose command buttons are a two-column grid of 54x20
  cells; fable has TEN pet buttons rather than that file's seven, so the
  oracle's shape needs five rows -- 100px at the oracle's own row height, and
  scaling it proportionally into 16px puts a 3px row under a 3px row. T419 Leg B
  therefore laid the ten out 5x2 in 20px (B352, struck) and boarded the shape
  here rather than silently inventing a HUD reflow. The options are Will's:
  (a) leave it -- the 5x2 grid is legible and inside the frame; (b) make the
  left column resolution-aware, so the pet window gets the oracle's 2x5 above
  some resolution; (c) give the pet window its own anchor off the stack, which
  is what the official client does (`PetInfoWindow`
  `<Location><X>50</X><Y>160</Y>`, `RelativePosition false` -- a free-floating
  window, not a stacked panel). Found by T419 Leg B.

- **B367** (P) `refactor/BOARD.md` -- **the id `B335` was issued twice, on the
  same day.** T414 Leg A used it for the four inert `storm` keys in
  `config/weather_effects.json`; T415 Leg A used it again for the tradeskill
  window's undrawn left side. T419 struck the T415 one; the T414 one is still
  OPEN and still called B335, so a bare "B335" reference is ambiguous and only
  file position tells them apart. Closes when the duplicate is renumbered (with
  every reference updated) or the id convention gains a check. Found by T419.

- ~~**B370** (B) `config/spell_effects.json` particle lifetimes vs the sprite
  animation rate -- **fable now animates the sprite sheets, but roughly an
  order of magnitude too fast.** T420 tied one 16-frame playthrough to a
  particle's lifetime, so the playback rate is `16 / lifetime`. `styles.impact`
  ships 0.4-0.8 s lifetimes, i.e. **20-40 cells/s**, against the oracle bound
  T420 could defend of **below ~3 cells/s** (consistent with one playthrough
  over Whirling Wind's ~6 s effect; the T389 capture's own ~5 renders/s stops
  a tighter measurement -- see `review/T420/oracle_frame_rate/`). The knob is
  the lifetime, not a new constant, and it is the SAME knob **B226**/**B356**
  hold for the burst's size and duration -- so retune it there, in one pass,
  rather than adding a second rate. Closes when the lifetimes are set against
  a measured oracle rate. Prov: T420 Review.~~ **CLOSED by T422 (this
  commit).** The lifetimes are now **1.6-2.0 s**, i.e. **8.0-10.0 cells/s**
  rather than 20-40. **Read the next sentence before quoting that number.**
  T422's oracle render rate is 4.4-5.8/s (Nyquist 2.2-2.9 cells/s), so a
  DIRECT cells/s measurement is still impossible and 8-10 is DERIVED from a
  directly measured particle LIFETIME under T420's own one-playthrough-per-
  lifetime rule. It therefore does not reach T420's "below ~3 cells/s", and
  that bound does not survive scrutiny: T420 could not measure a rate either
  and inferred it from the modal orientation class of a **mixed-age**
  population of stars holding across four renders -- a statistic over a
  mixed-age population is not a clock, and T420 itself rated it low confidence
  and separately refuted the effect-wide clock that argument needs. The
  lifetime, by contrast, is bounded below by a 1.46-1.96 s rise during which
  the oracle's population grows monotonically with **no deaths**, and above by
  Whirling Wind's steady population divided by its emission rate (1.4 s).
  Whether the ceiling can be raised at all: probed and reported in
  `review/T422/README.md` 1 -- `LP_NUM_THREADS` 1 -> 4 changes nothing,
  `MaxBGFPS=100` buys 5.00 -> 6.00 renders/s, and 640x480 is BLOCKED because
  the shared `/mnt/projects/eq-oracle/scripts/login-as.sh` drives the login screen by
  1024x768 screen coordinates. Pinned by
  `SpellBurstOracleMatch.SheetPlaybackRateFollowsFromTheMeasuredLifetime`.

- ~~**B371** (P) `tests/goldens/manifest.json` memory-coverage
  `eqe1MappedBytes` -- **a bless is owed for T420's pack growth, and the
  visual gate is RED on `task/T420-sprite-animation` until it is given.**
  T420 grew `spell_sprite_atlas` and its `_alpha` companion from 512x512 to
  1024x1024 (all sixteen animation frames of 33 sprites), so the pack went
  5,881,918 -> 6,930,494 B (+1,048,576, entry count unchanged at 128, sha
  `584ba1fe...` -> `14a6bd45...`) and each of the three gate zones' mapped
  bytes rose by exactly 1,048,576: qeynos2 123,015,486 -> 124,064,062,
  gfaydark 121,681,578 -> 122,730,154, befallen 116,166,074 -> 117,214,650.
  **Every image golden MATCHED** and no other assertion moved -- the three
  memory numbers are the whole failure. An executor may never self-bless;
  Will blesses in chat. Prov: T420 Review.~~ **APPLIED at the T420 merge under the B240 bless: 124,064,062 / 122,730,154 / 117,214,650.**
## Added 2026-09-06 by T418 (the active-weather oracle)

- **B360** (A) `src/client/graphics/renderer/irrlicht_renderer.cpp` → issue #20
  `IrrlichtRenderer::updateTimeOfDay` weather fog block /
  `WeatherEffectsController::getWeatherFogColor` -- **what the official client
  actually does for weather is swap the sky and the fog to an overcast state,
  and fable's version of that is unmeasured.** MEASURED T418 as the thing B231
  turned out to be: at the same spot, hour and heading, the oracle's rain frame
  is BRIGHTER than its clear frame by a factor that grows with DEPTH -- R x1.42
  / G x1.06 / B x1.01 on the nearest wall strip, R x2.73 / G x1.65 / B x1.25 on
  the farthest -- and its sky goes from (.163, .290, .429), blue, to
  (.285, .286, .348), grey. That is a fog colour and a sky swap, and it is the
  whole of the oracle's weather lighting. fable has both mechanisms already
  (`getWeatherFogColor()`, `getFogDensityModifier()`, `getRainFogSettings()`,
  and `SkyRenderer`), all driven by numbers nobody has compared with the
  oracle, and T418 deliberately left every one of them byte-for-byte alone
  because it had no target for them: its measurement solves the AMBIENT term
  only. Closes when the fog colour, the fog distances and the overcast sky are
  solved against the oracle the way the ambient now is -- the rig is
  `review/T418/scripts/` and needs no new tooling, just a fog-strength knob per
  the `/bakedlight` pattern so one session can sweep the family. Note the depth
  strips are the target, not the whole-frame grey: a whole-frame ratio mixes the
  sky swap and the fog blend, and the two are separate terms. Prov: T418 Review
  section 3; B231.
  **T426 (2026-09-07) TOOK THE TARGET TABLE AND NAMED TWO MECHANISMS; IT DID NOT
  SOLVE THE FAMILY.** With B399's framing settled both clients could finally be
  put in the same first-person pose, and the depth ramp is clean on the band
  y=180..300 (wall only; T426's first band, y=300..460, mixed wall/ground/trees
  and was not monotone). Full table: `review/T426/logs/T426_fog_table.txt`.
  * **Day rain 5, geometry, near x=40 -> far x=490.** ORACLE R 1.436 -> **2.544**,
    G 1.037 -> 1.401, B 1.001 -> 1.114 (reproduces T418's own 1.42 -> 2.73).
    **fable: 1.000 +- 0.001 on every strip and every channel** -- fable's weather
    fog reaches NO PIXEL, while the client is demonstrably in rain 5
    (`setWeather ... newType=1 newIntensity=5`, `SimWorker: Activated weather`).
    The blend in `updateTimeOfDay` is
    `blendFactor = weatherEffects_->getCurrentDarkening()`, and T418 flattened
    the darkening to the measured ambient curve (1.000 at every knot), so the
    lerp multiplies by ~0 and `getWeatherFogColor()` / `getFogDensityModifier()` /
    `getRainFogSettings()` change nothing. T418's comment at that line predicted
    this hazard; the measurement says it fires.
  * **Day rain 5, sky.** ORACLE (0.2141, 0.3585, 0.5117) blue ->
    (0.3160, 0.3159, 0.3933) grey, i.e. **R x1.475, G x0.881, B x0.769**.
    fable (0.2336, 0.1874, 0.1402) -> (0.0152, 0.0126, 0.0104), i.e.
    **x0.065 / x0.067 / x0.074** -- a 15x DARKENING where the oracle brightens R
    and greys the hue. Producer:
    `skyBrightness = 0.5 * pow(0.5, (intensity-1)/4)` (0.25 at intensity 5) into
    `SkyRenderer::setWeatherBrightness()`; the measured end-to-end factor 0.065
    says the sky renderer compounds it further. Sign, magnitude and hue all wrong.
  * **Night rain 5.** The oracle's night GEOMETRY term is below 8-bit
    quantisation at this crop (nine of ten strips are byte-identical), so there
    is no night geometry target -- consistent with T418's "the night signal is a
    fifth of the day's". The night SKY does move and moves the same way:
    normalised .001/.034/**.965** (deep blue) -> .369/.230/.401 (grey). fable's
    night sky goes to **0**.
  * **Why T426 committed no fog number.** The oracle's day term does not fit
    `active = clear(1-f) + F*f` with one `F`: least squares over the ten strips
    gives `F ~ (0.139, 0.119, 0.098)` linear with an inadmissible `f = 2.09` on
    the near strip and residuals to 0.022 -- the near strip's increments are
    (0.050, 0.004, 0.000) and the far strip's (0.056, 0.024, 0.007), whose
    channel ratios cannot come from one `F`. An additive model fails identically.
    **The oracle's weather is not a one-parameter fog lerp over this scene; the
    solve needs a model, not a sweep.** Picking numbers to make one crop match
    would recreate exactly what this item is about.
  * Rig, ready to re-run: `review/T426/scripts/t426_fable_weather.sh` (T418's,
    first person) and `t426_wall_strips.py` (THE measurement). The oracle side
    needs no new capture -- T418's stills are at the same spot, heading and clock.

- ~~**B361** (B) `src/client/graphics/sim_weather.cpp`
  `SimulationWorker::computeWeatherEffectsState` /
  `src/client/graphics/weather_effects_controller.cpp`
  `WeatherEffectsController::computeState` + `updateStormAtmosphere` --
  **the weather ramp had THREE copies and the only one under test was one
  production never executed.** Census over `src/ include/ tests/ tools/
  benchmarks/ docker/`: the reached path is `renderer_sim_bridge.cpp`
  snapshotting the controller, `SimulationWorker` advancing its OWN copy in
  `sim_weather.cpp`, and `applyState()` writing the result back;
  `WeatherEffectsController::computeState()` has **no** production caller (only
  T396's test), and `WeatherEffectsController::update()` -- the sole caller of
  `updateStormAtmosphere()` -- has **none at all**, so both its ramp and its
  storm-atmosphere body are dead. T418 made all three call one
  `advanceWeatherDarkening()` and pinned that by text
  (`EveryCopyOfTheRampCallsTheOneDefinition`) plus a numeric step-for-step
  equivalence between the worker form and the controller form, which is what
  makes the measured 9.3 s fade actually reach the screen. **The duplication is
  narrowed, not removed**: `computeState()` and `computeWeatherEffectsState()`
  are still two functions that must be kept in step by hand (the lightning
  timers and the transition progress are still copy-pasted), and
  `update()`/`updateStormAtmosphere()`/`updateRain()`/`updateSnow()` are still
  unreached. Closes when the dead `update()` path is deleted or wired, and the
  worker's copy is the controller's one function rather than a parallel body.
  Prov: T418 Review section 6; B231.~~ **STRUCK T423 Leg B.** The two copies
  nothing ran are DELETED --
  `WeatherEffectsController::computeState()`, `::update()`,
  `::updateStormAtmosphere()` and `::updateLightning()` (update()'s alone) --
  leaving `SimulationWorker::computeWeatherEffectsState()` as the only
  implementation, which is the one production executes. It is public + static
  now (a pure function of the input snapshot) and T396's and T418's nineteen
  weather cases drive it through the SAME snapshot/apply hop
  renderer_sim_bridge.cpp makes (`tests/weather_worker_step.h`). **The
  blindness was measured, not argued**: a 4x rate planted in the production
  copy left all nineteen green before the move and turns four RED after it
  (`TheTransitionTakesTheOracleMeasuredSeconds`, `ClearingTakesTheSame
  MeasuredTime`, `TheDarkeningRampsOverTheOracleTransitionTime`,
  `TheProductionHopMatchesTheBareRamp`). `updateRain()`/`updateSnow()` were NOT
  unreached, contrary to this entry's last sentence -- `updateRenderOnly()`
  calls both every frame from renderer_frame_input.cpp -- so they stay.

- ~~**B362** (A) `src/client/graphics/renderer/irrlicht_renderer.cpp`
  `IrrlichtRenderer::updateTimeOfDay` -- **the scene ambient is recomputed ONLY
  when a time-of-day packet arrives, so a weather change reaches the world's
  lighting at an arbitrary later moment, or never.** FOUND T418 by accident and
  it is the reason the unit's first live session measured nothing: with the
  PRE-FIX 0.460 modifier live in the controller, a full day block of forced rain
  at intensities 2, 5, 9 and snow produced geometry ratios of **0.998-1.014**
  and **zero** `updateTimeOfDay: weatherMod ...` lines in the client's own log
  (`review/T418/logs/fable_session_before.log.gz`). `updateTimeOfDay()` is the
  only production site that calls `composeSceneAmbient()` and
  `smgr_->setAmbientLight()`, and its callers are the OP_TimeOfDay handler and
  zone-in -- eqemu sends that packet at zone-in and on `#set time`, not on a
  timer. So between two time packets the weather modifier, the vision floor and
  the user ambient multiplier are all frozen at whatever they were. The unit's
  before/after pair had to force the recompute with a second `#set time` after
  the trigger (`REPIN_AFTER_TRIGGER=1` in the rig) to make the modifier visible
  at all. **This did not bite the committed fix** -- the measured modifier is a
  constant 1.000, so a stale recompute is harmless for weather -- but it makes
  every future weather/vision/lighting change latent, and it means B360's fog
  work cannot be measured live without the same workaround. Closes when the
  ambient is recomposed on the state that feeds it (a weather change, a vision
  change, an ambient-multiplier change) rather than only on a clock packet,
  with a test. Prov: T418 Review section 5.~~ **STRUCK T423 Leg A
  (`8b82b125`).** One dirty flag on `IrrlichtRenderer`
  (`markSceneAmbientDirty()` / `isSceneAmbientDirty()`), set by every writer of
  a non-clock ambient input and consumed once per frame by the new FIRST render
  pass `FrameComponent::recomposeSceneAmbientIfDirty()` (G05 golden entry 3,
  309 -> 310, learned in place). `updateTimeOfDay()` clears the flag, so the
  clock path is unchanged and a marked-then-clocked frame recomposes once.
  Writers: `ZoneLifecycleComponent::setWeather` (OP_Weather), the worker's
  weather `applyState` hop when the modifier actually MOVES (a lightning flash
  moves it with no packet either way), zone-enter's weather reset, the three
  vision writes through `refreshVisionLighting()` -- which now MARKS instead of
  recomposing inline, so T405's second path is gone and there is one -- and
  both diagnostics lighting toggles. `tests/test_scene_ambient_recompute.cpp`,
  11 cases, ten RED with the wiring reverted. **The fourth input named in this
  entry, the ambient multiplier, has NO WRITER AT ALL** -- see the new B380.

- ~~**B380** (B) `include/client/graphics/irrlicht_renderer.h`
  `ambientMultiplier_` -- **the user ambient multiplier has one reader and no
  writer.** Found T423 while enumerating the four inputs B362 says must trigger
  a recompose. Census over `src/ include/ tests/ tools/ benchmarks/`: exactly
  one read, `composeSceneAmbient()`'s second argument in `updateTimeOfDay()`,
  and zero writes anywhere. Its comment names "Page Up/Down", which no key
  binding produces -- the keys were presumably lost with the WindowManager UI
  or an earlier input map. So the term is a constant 1.0, it is not an input
  the T423 dirty flag can ever see move, and the "user-adjustable ambient
  light" the comment promises does not exist. Left in place rather than
  deleted because `composeSceneAmbient()`'s four-term ORDER is measured and
  pinned (test_vision_ambient.cpp, T396) and dropping a term would rewrite
  that signature for a reason unrelated to the oracle. Closes when either a
  writer is wired (a key binding or a slash command, with the multiplier
  marking the ambient dirty like every other input) or the term and its
  argument are removed together. Prov: T423 Review section 1; B362.

---

## Added 2026-09-06 by T421

- **B375** (B) ~~`include/client/inventory/item_instance.h`~~ -- **STRUCK by → issue #141
  T425 Leg B** (`73c79386`). Each of the nine was resolved against
  eqemu's `titanium.cpp` `SerializeItem()`, the wire this client actually
  reads, one verdict per member:
  * **the seven `heroic*` -- DELETED.** `SerializeItem()` never emits an
    `ItemData::Heroic*` field (grep over that function: zero hits), so there
    is nothing on the wire to parse; and eqstr_us.txt has no heroic-stat
    format string, so there would be nothing to draw either. Compiler proof:
    the tree builds with the members gone, and the seven `WriteOnlyFields`
    exemption rows went with them.
  * **`bardEffect` -- DELETED.** Titanium's wire carries FIVE `ItemEffect`
    blocks and no sixth: `Click`, `Proc`, `Worn`, `Focus`, `Scroll`, at
    fields 134-158. The bard data it does carry is the SCALAR pair
    `BardType`/`BardValue` at fields 41-42 -- a skill-modifier pair, not an
    effect -- and eqstr has no instrument-modifier item line for that either,
    so parsing it into a new scalar would produce another unread field. The
    parser's existing `// fields[41-42] = BardType, BardValue` comment stays.
  * **`expendable` -- WIRED.** It is not a wire FIELD; it is derived, and
    eqemu's `ItemInstance::IsExpendable()`
    (`/mnt/projects/akk-stack/code/common/item_instance.h`) is the
    definition: `Click.Type == item::ItemEffectExpendable || ItemType ==
    ItemTypePotion`. BOTH inputs are serialized by Titanium -- `Click.Type`
    is field 135 and `ItemType` is field 0 -- so the parser now derives it,
    and eqstr 3001 `EXPENDABLE     Charges: %1`, which
    `BuildItemDisplayText()` has printed since T421 and which nothing could
    ever reach, is live. Three cases in `tests/test_item_display.cpp` drive
    the REAL parser over a real serialized item: a click-type expendable, a
    potion, and an ordinary item that is neither.

- ~~**B375** (B) `include/client/inventory/item_instance.h` -- **nine
  `ItemInstance` members have NO WRITER at all.** `expendable`, the seven
  `heroic*` (`heroicStr`/`Sta`/`Agi`/`Dex`/`Wis`/`Int`/`Cha`) and `bardEffect`
  are declared, default-initialised and assigned by nothing:
  `TitaniumItemParser::parse()` (`src/client/inventory/inventory_item_parser.cpp`)
  never touches them, and a comment-filtered grep over `src/ include/ tests/
  tools/ benchmarks/` finds only the declarations. B298 listed them among the
  "parsed and never shown" item fields; that was wrong -- they are not parsed
  either, which makes them dead struct members (B290's class), not a missing
  display. **Two of the three groups also have no oracle line**, so drawing
  them would be an invention: `eqstr_us.txt` has no heroic-stat format string
  anywhere (heroic stats postdate this client) and no instrument-modifier item
  line. `expendable` DOES have one (eqstr 3001 "EXPENDABLE     Charges: %1")
  and `BuildItemDisplayText()` already prints it -- so that member closes the
  moment the parser fills it. Titanium's `SerializeItem` does carry the
  bard fields at `fields[41-42]` (`BardType`, `BardValue`), which the parser
  reads past with a comment. Closes when each group is either wired to its
  wire field or deleted. Found by T421 Leg A (the seven `heroic*` surfaced as
  `WriteOnlyFields` NEW rows the moment a test wrote them).~~

- ~~**B376** (B) `include/client/graphics/ui/static_panels.h`
  `SlotDisplayInfo::iconId`, `TradeskillSlotDisplay::iconId`,
  `LootItemDisplay`, `ItemDisplayPanelState::iconId` -- **fable has no
  item-icon atlas.** Every panel that shows an item carries the server's icon
  id (S2 put it on `InventorySlotChangedData` precisely so the renderer would
  have it) and not one of them has art to draw for it: the inventory popup
  paints the first three characters of the item's NAME in a 32x32 slot, the
  tradeskill grid does the same, and T421's item display window paints the
  icon NUMBER inside `IDW_IconButton`'s frame rather than a wrong picture.
  The oracle's own `<ButtonDrawTemplate><NormalDecal>BuffIcons</NormalDecal>`
  names the sheet; the shipped art is the client's `dragitem*.tga` /
  `objects*.dds` families, which nothing in the EQE1 pack pipeline indexes
  yet. Closes when an item-icon sheet joins the pack and the four consumers
  draw from it. Found by T421 Leg A.

  **WILL 2026-09-07:** CRITICAL. The official `uifiles/default/dragitem*.tga` sheets are the source. → T430.

  **CLOSED by T430 (2026-09-07), `20e9e3a9`.** All 1224 icons ship.
  `tools/item_icon_atlas_builder` packs the 34 `dragitem*.tga` sheets the
  oracle's `EQUI_Animations.xml` names under `A_DragItem` into
  `data/textures/item_icons{0,1}.bmp` (two 1024x1024 sheets, 25x25 cells of
  the native 40x40), and `include/client/graphics/ui/item_icon_atlas.h` is the
  one address book both the builder and the client's `ItemIconSheets` loader
  read. The mapping is QUOTED, not assumed: `<Vertical>true</Vertical>` makes
  a sheet COLUMN-major, which the usual `(id-500)%36` folklore gets backwards,
  and `dragitem34`'s partial fill (two whole columns plus one, thirteen icons)
  proves it independently of the tag. Four draw sites now paint art --
  the 22 equipment slots, the 8 general slots, the tradeskill container cells
  and `IDW_IconButton` -- plus a FIFTH the entry did not list: the loot rows,
  which needed `iconId` added to `LootItemAddedData` and `LootItemDisplay`
  because the producer had PARSED the icon from `field[22]` and thrown it away.
  Each keeps its old text placeholder as the no-art fallback, so a client
  without the pack looks exactly as it did. Pinned by
  `tests/test_item_icon_atlas.cpp` (9 cases, including the negative that a
  row-major reading is wrong) and `ItemIconDrawTest` in
  `tests/test_ui_atlas_pixels.cpp`. **The entry's merchant and bank windows do
  not exist** -- there is no merchant/bank/cursor panel in the static UI at all
  (the game side already threads `iconId` through `merchant.cpp` /
  `merchant_trade.cpp` for windows that were never built); that is **B429**.~~
- **B377** (P) `src/client/bridge/bridge_component_intents.cpp`
  `onRecipeFavoriteIntent`, `include/client/graphics/ui/tradeskill_panel.h`
  -- **fable keeps no tradeskill favourites list**, so the Add and Remove
  buttons cannot send a correct edit. eqemu's `OP_RecipesFavorite` carries the
  WHOLE favourites set, not a delta (`Client::SendTradeskillSearchResults`'s
  favourites arm reads a fixed array of ids), so an "add" is "send the old set
  plus one" and a "remove" is "send the old set minus one". T421 Leg B wired
  the three buttons because the alternative was leaving named enumerators no
  hit test could return, but with no stored set Add sends a one-element list
  and Remove sends an empty one -- correct only when the player has exactly
  the one favourite. Show is unaffected and correct (it re-fetches). Closes
  when `TradeskillPanelState` (or the game side) holds the acknowledged
  favourites set -- which the server itself supplies, since a favourites fetch
  returns exactly that list through the same `TradeskillRecipeReceived`
  stream. Found by T421 Leg B.

  with a test. Prov: T418 Review section 5.

  **WILL 2026-09-07:** LOW priority — favourites/recipe search did not exist launch→Velious (era rule).
## Added 2026-09-06 by T422 (spell effects matched live against the oracle)

- **B395** (B) `src/client/graphics/sim/renderer_sim_bridge.cpp` → issue #142
  `SimBridgeComponent::postSimulationInput` -- ~~`global.maxParticles` in
  `config/spell_effects.json` had NO CONSUMER.~~ **CLOSED by T422 (this
  commit).** The line read `pi.poolSize = 0; // Worker determines pool size
  from config` and the worker's own arm is `if (poolSize <= 0) poolSize =
  1024;` -- a hardcoded default that happened to equal the shipped config
  value, so the knob looked honoured and was inert. It did not bite while one
  landing was 81 sprites; T422 took that to 648, at which two overlapping
  landings exceed 1024 and would have been silently clipped, making every
  measured count wrong. The bridge now sends
  `SpellEffectsConfig::instance().getGlobal().maxParticles` and the config
  ships 4096. Pinned by
  `SpellBurstPoolSizing.TheOldHardcodedPoolClipsAndTheConfiguredOneDoesNot`
  (four simultaneous impacts, run at 1024 and at the configured pool, must
  differ) and `SpellBurstOracleMatch.TheConfiguredPoolHoldsTwoOverlappingLandings`.
  Prov: T422 Review.

- **B396** (C) `config/spell_effects.json` `styles.impact` / → issue #21
  `styles.spellComplete` -- **the official client's burst RAMPS and is then cut
  off, and fable's emitter cannot express that shape.** MEASURED T422 at
  4.4-5.8 renders/s: spell 93's population rises monotonically with no deaths
  through 11 -> 22 -> 188 -> 324 -> 502 -> 578 -> 637 -> **705** over 1.96 s and
  then collapses to 9 in **0.37 s**; spell 41 rises 204 -> ... -> **561** over
  1.46 s and falls over 0.97 s. A rectangular emission plus a lifetime tail --
  which is all `spawnRate` + `emitterLifetime` + `lifetimeMin/Max` can
  describe -- gives a rise, a plateau and a fall of comparable length, never a
  2 s ramp cut off in a third of a second. T422 matched the peak population,
  the duration and the sprite size instead and left the shape; fable's measured
  duration is 2.16 s against the oracle's 2.33-2.6 s. Closes with either a
  measurement showing the shape does not matter to a viewer, or an emission
  curve (a rate ramp plus an effect-scoped kill) in the config. **Class C: this
  is a "how faithful do we want to be" call, not a defect.** Prov: T422 Review 4.

- ~~**B397** (B) `config/spell_effects.json` `spellOverrides` -- **it is an empty
  object and always has been, so one `impact`/`spellComplete` setting has to
  serve spells whose oracle populations differ by a factor of 20.** MEASURED
  T422: the official client paints **715** sprites for Burst of Flame (93),
  **561** for Weaken (41) and a steady **25-36** for Whirling Wind (253), and
  its sprite sizes differ too (median 4.8-6.9 px, 4.4-6.7 px, and 8.0-10.5 px
  respectively). T422 set the single style to the wide-burst case, which is
  right for 93 and 41 and leaves 253 with ~20x too many sprites each ~31%
  too small. The three-tier hierarchy (global -> style -> per-spell) is
  implemented and documented in `CLAUDE.md`; `SpellEffectsConfig::getStyleSettings`
  takes a `spellId` and applies overrides. Only the DATA is missing. Closes
  when the spells whose oracle numbers are measured carry their own overrides,
  with the same before/after capture discipline. Prov: T422 Review 2, 4.~~
  **STRUCK T441 (this commit).** `spellOverrides` now carries the three spells
  T422 measured, and only those three: 93 at 530/662 (peak 715.2), 41 at
  416/519 (peak 561.0), and 253 as a CONTINUOUS effect -- 7.4/9.3 per second
  over a 3.6 s window at the style's 1.8 s mean lifetime, i.e. a steady 30.1
  lasting 5.6 s, with every size scaled 1.673x (T422 measured 5.53 px median
  against the oracle's 8.0-10.5 px band, midpoint 9.25). 54 Frost Bolt got NO
  override: T422 could not separate its merged ribbon into components, so
  there is no measured population to pin, and
  `SpellStyleOverrides.AnUnmeasuredSpellStillResolvesToTheStyle` asserts it
  still reads the plain style. The overrides are PARTIAL -- 93 and 41 name
  only `spawnRate` -- so the measured sizes, lifetimes and expand speed still
  come from the style, pinned by
  `SpellStyleOverrides.AnOverrideOnlyMovesWhatItNames`. Nine tests in
  `tests/test_spell_colour_and_overrides.cpp`; six of them were red before the
  data existed. The emission SHAPE is still not matched and remains **B396**.
  The live before/after capture is OWED to a lock unit (T438 held the eqemu
  lock; this unit was offline).

- ~~**B398** (A) `config/spell_effects.json` `resistColors` / `getSpellColor` --
  **fable's Weaken (41) burst is RED-dominant on screen where the official
  client's is GREEN-dominant.** MEASURED T422 on the OBSERVING client, mean
  added RGB over the particle pixels, normalised: fable **0.377 / 0.294 /
  0.329**; the oracle (T389, same normalisation) **0.448 / 0.572 / 0.191**.
  The fire/frost pair matches (fable 93 = .515/.339/.146 R-dominant against the
  oracle's .360/.379/.199; fable 54 = .228/.363/.409 B-dominant against
  .213/.284/.315), so the mechanism works and this is a per-spell colour
  lookup that is wrong for 41 specifically -- Weaken is a magic-resist spell
  and takes the `magic` row (0.78, 0.39, 1.0), which is purple, not the green
  the oracle draws. Whirling Wind (253) is also more blue than the oracle
  (fable .299/.248/.453 against .258/.309/.214). Closes when the per-spell
  colour is taken from the same place the official client takes it (the
  `spells.eff` record, not the resist type) or measured overrides are added.
  Prov: T422 Review 5.~~
  **STRUCK T441 (this commit) -- and the premise "a per-spell colour lookup"
  is REFUTED: the official client has no per-spell colour to look up.**
  The chain it actually walks is one of ART, end to end:
  `spells_us.txt` field **123 SPELLAFFECTINDEX**
  (`/mnt/projects/akk-stack/code/common/spdat.h:1653`) -> the record at that
  INDEX in `/mnt/projects/EverQuestP1999/spells.eff` (the index IS the
  position, T70) -> that record's three stage blocks, each opening with three
  32-byte sprite-name slots -> `GEN<L><d>0_SPRITE` resolving 0-BASED to
  `SpellEffects/spel<l><d+1>.tga` (T389, MEASURED) -> and the particle is that
  file's own texels. Nothing in it is a colour. T73 had already REFUTED the
  only colour-shaped candidate inside the 800 unknown bytes per stage block
  (the `float[12]` at 0x370 reaches 32.0, over 12x its claimed [0,2] range),
  and field 85 RESISTTYPE is a mechanic the client does not paint with.
  **The defect was that fable drew the right art and then multiplied it by a
  resist colour** -- `POINT_SPRITE_FS` computes `texColor * vColor` -- so
  speln1.tga's green discs went through the magic row (200,100,255) and came
  out red. `applyStageSpriteNames()` now drops the modulation to white
  whenever it installs a real stage's sprites (alphas untouched: they are the
  style's fade curve), and the three stages that were RESOLVED but whose names
  were being thrown away now route them: Source -> cast glow / spellComplete,
  Middle -> projectile trail. `resistColors` / `colorForVocabulary()` survive
  only for the ~946 spells with no usable `spells.eff` record, which is the one
  case where fable has no art to take a colour from
  (`SpellColourFromSpriteArt.ASpellWithNoStageKeepsTheFallbackColour`).
  Normalised alpha-weighted R/G/B over the Target stage's own cells of the
  SHIPPED sprite sheet, before -> after against the oracle:
  41 `0.578/0.422/0.000` R -> **`0.407/0.593/0.000` G** (oracle
  `0.448/0.572/0.191` G); 93 `0.803/0.177/0.021` -> `0.625/0.292/0.083`, R
  both sides; 54 `0.071/0.282/0.647` -> `0.148/0.325/0.527`, B both sides.
  **253 Whirling Wind does NOT land and is boarded as B453**: spela2.tga has
  no red in it at all, so no modulation of that art can reach the oracle's
  measured `0.258/0.309/0.214`. Eight tests in
  `tests/test_spell_colour_and_overrides.cpp`, four red before the fix; the
  Weaken case asserts the negative too, so a reintroduced tint is a red test.
  The live observer re-capture (T422's duel rig) is OWED to a lock unit.

- ~~**B399** (A) `src/client/graphics/player/renderer_camera.cpp` /
  `movement` -- **a server `#goto x y z h` does not turn fable's camera; the
  client's own heading echoes straight back over it.**~~ **STRUCK T426 (this
  commit, base `8f5e586a`) -- the STATED MECHANISM IS REFUTED and a different,
  real defect in the same handler is fixed.**
  * **`480` was never an echo: it is `160` correctly converted.** A player's own
    heading takes the `90 - server_deg` flip at every inbound edge and
    `SendPositionUpdate` inverts it on the way out. Wire 160 -> 112.5 deg ->
    337.5 deg -> **480** in the 512 format. T426 swept eight `#goto` headings
    (`review/T426/logs/fable_sweep1.log`): 160->480, 288->352, 32->96, 416->224,
    96->32, 224->416, 352->288, 0->128. Eight headings, eight distinct camera
    poses, nothing overwritten.
  * **And the camera turns to the RIGHT place.** `review/T426/probe/T426_fp_pairs.png`
    puts the official client and fable side by side at the same spot, the same
    `#goto`, both in FIRST PERSON, at wire headings 0/128/256/384/160/288: they
    match row for row. The official client's own compass calibrates the wire
    heading independently in the same frames -- h=0 -> N, 128 -> W, 256 -> S,
    384 -> E, dead centre (`review/T426/probe/T426_oracle_compass_sheet.png`).
    "The oracle faces open field and fable faces a wall" compared frames that
    both contain a wall on the left half and open field on the right.
  * **THE FRAMING TRAP that produced B399** (carry this forward): the oracle rig
    leaves the official client in FIRST person and fable's default is
    third-person follow, 20 units back. At the same heading that offset can put
    a wall across fable's whole frame where the oracle sees past it. Compare
    first person to first person (`/camera fp`) or a framing argument proves
    nothing.
  * **The real defect, fixed here.** `EntitiesComponent::ZoneProcessClientUpdate`'s
    SELF branch computes `self_heading = 90 - server_h` into `ctx_.self` and then
    handed `BridgeComponent::OnSpawnMovedGraphics` the RAW `h_player`, which that
    function converts to the 512 format and publishes as `PlayerMoved` -- i.e.
    straight into `setPlayerPosition` and the camera. One argument was serving two
    conventions (flipped for our camera, verbatim for the entity row, which must
    NOT be unified -- `docs/CLAUDE.md`). Live at zone-in the camera took 480, then
    160, then 480. Every server-forced relocation arriving as a self
    `OP_ClientUpdate` with no zone-change packet (a GM move, a server position
    correction) aimed the camera 90 degrees plus a mirror away and left it there
    until the player turned. Fixed by reading the store instead of the argument;
    pinned by `tests/test_self_relocation_heading.cpp` (3 cases, including one
    that fails if the fix leaks into the entity convention).
  Prov: T426 Review 1; `review/T426/README.md` section 1.

- ~~**B400** (A) `src/client/graphics/sim_particles.cpp` /
  `SpellVisualFX::createImpact` -- **a landing intermittently draws nothing on
  the CASTER's own client while drawing correctly on an observer.** MEASURED
  T422: in `review/T422/fable/T422_fable_after2_size_caster.mkv` the Weaken (41)
  landing at grab frame ~6126 produced no excursion above the noise floor on
  the caster's own capture (peak 72 components against a 63-72 floor), while
  the SAME landing at frame 6140 of the observer capture from the same session
  is a clear 139-pixel excursion, and the same spell in the immediately
  preceding session (`..._after_caster.mkv`, frame 6061) peaked at 365
  components on the caster. Both Burst of Flame (93) and Weaken (41) were
  missing from that one caster capture; Frost Bolt (54) and Whirling Wind (253)
  drew. All four `SimWorker: Created spell effect 'Impact'` lines are present
  in the log, so the effect reached the worker every time -- the loss is
  downstream. Suspect the T395 one-frame attach-position protocol interacting
  with the frustum test at a low frame rate, but that is UNCONFIRMED and the
  next unit must measure it rather than scope off this sentence. Closes when
  the drop is reproduced deterministically and fixed. Prov: T422 Review 4.

  argument are removed together. Prov: T423 Review section 1; B362.~~
  **CLOSED by T424 Leg B** (`70e5de2a`): DELETED, field and term together.
  T424 found the writer's grave that T423 could not: it was a Page Up / Page
  Down handler inside the renderer's **ADMIN mode** --
  `float ambientDelta = eventReceiver_->getAmbientLightDelta(); if (ambientDelta
  != 0.0f) { ambientMultiplier_ = clamp(...); updateTimeOfDay(...); }` -- and
  `cac31166` ("Remove Admin and Repair renderer modes, keeping only Player
  mode", Feb 2026) deleted that handler, its delta accumulator and its hotkey
  binding deliberately, with ~2,200 lines of other admin/repair features.
  Nothing was lost by accident, so there was no writer to restore: this was
  residue of a removed dev control, not a zone/config override. Removed with it:
  the per-channel `std::min(1.0f, ...)` clamp, which existed only for the
  multiplier (the day/night curve is already <= 1 and the weather modifier is
  deliberately allowed to overshoot for a lightning flash). `composeSceneAmbient()`
  is now `(dayNight, weatherModifier, vision)` and the pinned ORDER is three
  steps; `include/client/graphics/vision_ambient.h` says why. Compiler proof of
  zero callers: the whole tree builds with the field gone, tools and benchmarks
  included. Pinned by `VisionAmbientCompositionT396Test.NoUserAmbientMultiplierRemains`
  (RED with the leg reverted by patch, on two assertions).
  A future user-adjustable ambient arrives as a key binding or slash command
  WITH a writer and WITH `markSceneAmbientDirty()` -- which is B362's whole
  point: a term nothing can move is not an input.

- **B385** (C) `src/client/graphics/ui/loot_panel.cpp` `drawButton`, → issue #22
  `src/client/graphics/ui/static_panels.cpp` `renderChatPanel`,
  `tools/ui_atlas_builder.cpp` -- **the UI's buttons and its scrollbar thumb
  are hand-drawn `drawRect`s, and the oracle draws them from art.** T416 read
  this as "two of the six unconsumed atlas regions have an obvious caller
  waiting"; T424 measured that it is the other way round. The loot popup's
  Loot All / Close buttons are three `drawRect()` calls (a fill plus a top and
  a bottom 1px line) and the chat scrollbar thumb is one more; the six regions
  that looked like their destination -- `button_normal`, `button_pressed`,
  `button_disabled`, `panel_border`, `scroll_indicator`, `slot_border_selected`
  -- were procedural chrome invented for the deleted WindowManager UI, at fixed
  sizes (64x24, 32x32, 8x16) that stretch wrong onto the rects fable actually
  draws, so T424 deleted them rather than wire an invented look. The official
  client's buttons come out of `window_pieces01.tga`, the same texture T398
  lifted the four coin decals from, as 9-sliced pieces with pressed/disabled
  states; `EQUI_LootWnd.xml` and `EQUI_ChatWindow.xml` name the pieces. Doing
  it means teaching `ui_atlas_builder` a 9-slice lift and `UIRenderer` a
  9-slice draw, then a pack rebuild with its two sha256 pins -- and it is a
  LOOK change, which is why this is C and not B: it belongs with **B350**
  (the 70-78% opacity decision), not ahead of it. Nobody has ruled on either.
  Found by T424 Leg C while closing B351.

---

## Added 2026-09-06 by T425

- **B390** ~~(B) `src/client/inventory/item_display_text.cpp`,
  `src/client/spell/spell_display_text.cpp`~~ -- **STRUCK by T429 Leg A** (`3d59d342`).
  Both composers now take a `const EQT::StringDatabase&` and fetch every
  format by id through `eqt::EqStrLine()`
  (`include/client/display_eqstr.h`), which calls
  `StringDatabase::formatString()` -- the client's ONE `%n` substitution
  engine, the same `chat_component.cpp` uses. No second formatter was
  written. A table miss now draws NO line rather than a hardcoded fallback,
  and `tests/test_display_text_from_db.cpp` pins three things: a reworded
  eqstr file rewords the window (so the table is genuinely the source), an
  empty table draws nothing, and all **fifty-two** item ids plus **nine**
  spell ids exist in the shipped `eqstrings`. **The surprise: eqstr mixes two
  placeholder conventions.** 3030 and 3033 are C PRINTF formats --
  `(Casting Time: %2.1f)` -- not the positional `%1..%n` every other row
  uses; the positional engine renders them `(Casting Time: .1f)`, so
  `EqStrPrintfFloat()` handles that one shape in one place. Original entry:
  **the two display composers hardcode format strings the client already
  ships as data.** Every line they
  emit is quoted from `/mnt/projects/EverQuestP1999/eqstr_us.txt`, and that
  whole table is ALREADY in `data/willeq.db` as `eqstrings` (5,901 rows,
  loaded by `tools/willeq_db_builder.cpp`'s `loadEqStrings()`) and ALREADY
  reachable at runtime through `EQT::StringDatabase::getString()` /
  `formatString()`, which `EverQuest::m_string_db` owns and
  `chat_component.cpp` and `eq_strings.cpp` already use. So the two composers
  transcribe by hand what one `getString(3006)` would fetch, which means a
  localised `eqstr_XX.txt` would not reach them and a transcription typo is
  invisible. Not fixed in T425 because the composers are free functions that
  a test drives with no database and changing that is a design call (an
  injected string-lookup hook, the `who_response.h` shape, is the obvious
  form). Closes when both composers take their formats from the string table.
  Found by T425 Leg A.

- **B391** (B) `src/client/spell/spell_display_text.cpp`, → issue #143
  `include/client/spell/spell_data.h` `reagent_id` -- **the one spell-display
  line the oracle has that fable cannot draw.** eqstr 6732 is
  `Reagents Needed: %1` (6731 `Reagents Available: %1`), and `%1` is an item
  NAME. `SpellData` carries `reagent_id[4]` / `reagent_count[4]` and nothing
  else; the client has no item-name-by-id source anywhere -- `data/willeq.db`
  has no items table (its tables are `animation_sounds`, `dbstrings`,
  `eqstrings`, `equipment_models`, `equipment_textures`, `item_models`,
  `login_strings`, `metadata`, `music_files`, `race_models`, `sound_assets`,
  `spells`), and the wire only names an item inside an item packet the player
  happens to receive. Printing the ids would be inventing a line the official
  client never draws, so T425 drew nothing. Closes when an item-name lookup
  exists (the same gap `B376`'s icon sheet sits next to). Found by T425 Leg A.

### Added by T430 (2026-09-07)

- ~~**B429** (B) `src/client/merchant/merchant.cpp:103`,
  `src/client/merchant/merchant_trade.cpp:405` and `:693`
  (`data.iconId = data.hasItem ? item->icon : 0;`) -- **there is no merchant
  window, no bank window and no cursor-item slot in the static UI**, so three
  producers fill an `iconId` nothing can draw. T430 went looking for them to
  give them icons and found the panels absent: a grep for a
  `MerchantPanelState` / `BankPanelState` / cursor-item struct or render
  function under `src/client/graphics/ui/` and `include/client/graphics/ui/`
  returns NOTHING, and the seventeen-panel table in
  `include/client/graphics/ui/CLAUDE.md` lists no vendor, bank or cursor row.
  The commerce vocabulary is reachable only as slash commands (`/vendor`,
  `/vendorlist`, `/buy`, `/sell`, `/bank`), which is why the gap has been
  invisible. The oracle's own `EQUI_MerchantWnd.xml` gives its slot buttons
  `<ButtonDrawTemplate><Normal>A_RecessedBox</Normal>
  <NormalDecal>A_DragItem</NormalDecal></ButtonDrawTemplate>` with
  `<DecalSize><CX>40</CX><CY>40</CY></DecalSize>`, so the art and the layout
  source both exist now -- only the panels do not. Closes when a merchant and
  a bank window exist and draw their icons. Found by T430.~~ **CLOSED by T436
  (this commit).** All three windows now exist, each with a layout header that
  quotes its own oracle XML and a panel TU that computes no rect:
  `merchant_layout.h`/`merchant_panel.cpp` (`EQUI_MerchantWnd.xml`, scaled onto
  `centerPopup` because the window is `Style_Sizable true` and 505px tall from
  its `<Location>`), `bank_layout.h`/`bank_panel.cpp` (`EQUI_BankWnd.xml`,
  UNSCALED at 108x347 because it is `Style_Sizable false`, eight slots per the
  era file) and `cursor_attachment_layout.h`/`cursor_attachment.cpp`
  (`EQUI_CursorAttachment.xml`, `CA_Anim` 40x40 at the mouse). The five window
  bridge arms and `BankCurrencyChanged` had their LOG_TRACE stubs replaced by
  real appliers; `VendorItemAddedData` gained the `iconId` its producer had
  been parsing and dropping; and the enriched `InventorySlotChanged` arm --
  which covered 0..29 and the WORLD range and nothing else -- now routes slot
  30 to the cursor and 2000..2007 to the bank. Pinned by
  `tests/test_merchant_bank_cursor.cpp` (32 cases, including a four-case SOURCE
  CENSUS of the defect that runs and FAILS against the pre-T436 tree). NOTE:
  B429's own quote of `MW_SelectedItem`'s `A_RecessedBox`/`A_DragItem` template
  called it a "slot grid"; it is ONE button. `EQUI_MerchantWnd.xml` has no slot
  grid at all -- its stock is a `<Listbox>` with seven `<Columns>`, the first of
  which (24px, empty `<Heading>`) is the icon cell. ~~LIVE PROOF IS OWED: T436
  held no eqemu lock (T428 had it), so no real vendor or banker has been in
  front of these windows.~~ **LIVE-PROVEN T449 (this commit)**: a real vendor
  (`Mellisa_Purgor`, qeynos2) and a real banker (`#npctypespawn 772`) in front
  of both windows. The merchant list draws its icon column from T430's sheets,
  a row click fills `MW_SelectedItem` with the icon, the name and the price,
  Buy returns `You purchased Spell: Smite for 5g 1s 6c` with the coins
  deducted, the bank draws its eight slots, its four coin rows (10 pp after
  `/bank deposit`), the DISABLED Change button and the Done button, and the
  cursor attachment draws a summoned item's 40x40 icon under the pointer AND
  still draws it after a full zone change (T439/B444's cursor half).
  `review/T449/README.md` §3. Prov: T436 Review; T449 Review.

### Added by T427 (2026-09-07)

- **B410** (C) `data/textures/detail_atlas.bmp` / `tools/generate_textures.cpp` → issue #23
  (`m.push_back({"detail_atlas.bmp", BmpFormat::Bmp24, ...})`) --
  **the detail atlas ships 24-bit and both of its consumers alpha-BLEND it.**
  `DetailTextureAtlas::createAtlasImage()` fills the whole 512x512 with
  `SColor(0,0,0,0)` -- transparent black -- and paints 56 cutout tiles on it;
  `DetailChunk::rebuildMesh` (`detail_chunk.cpp:518`, `:681`) and
  `FootprintManager` (`footprint_manager.cpp:19`) both set
  `EMT_TRANSPARENT_ALPHA_CHANNEL`, which is exactly the alpha-blend shape T406
  converted 60 other files for under B266. This file was not converted with
  them only because B265 had it flagged as un-reproducible, and T403's census
  entry inherited that as its stated reason; T427 closed B265 and the reason
  no longer holds, so the question is now open on its merits and the census
  entry says so.
  It is **C, not B**: unlike the sixty, converting this one is a LOOK change,
  not a cascade. Every tile is drawn on a transparent ground, so today every
  grass blade, flower and footprint is a fully opaque quad with a black
  surround, and giving the atlas its alpha back changes what every outdoor
  zone draws. It is also NOT free -- a 512x512 ETC1 entry with mips is
  ~174,760 bytes and its RGBA8888 form ~1,398,100, though T411's companion
  route means the real cost is one more ETC1 entry, not the in-band payload.
  Nobody has looked at what fable's outdoor detail actually looks like beside
  the oracle's; that judgement is Will's and belongs with **B350**.
  Two things must NOT be assumed when this is taken up: (1) the visual gate
  will not catch it either way -- `gles2_zone_benchmark` never instantiates
  `DetailManager` (measured in T427), so a conversion moves zero golden pixels
  and needs live captures, not the gate; (2) `spell_particle_fallback.bmp` is
  the counter-example that says "alpha-blend" is not automatically the answer
  -- T424 measured its blend as additive and left it 24-bit deliberately.
  Found by T427 Leg B while closing B265.

## Added 2026-09-07 by T435 (door collision; found while ruling out B424)

- ~~**B435** (A, live defect) **a jumping player can "land" on the ceiling
  sentinel, 1,000 units up.** `findGroundZBspFiltered()` returns
  `feetZ + 1000.0f` as a "blocked / that is a ceiling, not a floor" sentinel
  (`src/client/graphics/player/renderer_player_collision.cpp:574`). Four
  jump-landing tests compare `feetZ <= groundZ` with **no upper bound** --
  `src/client/graphics/player/renderer_player_physics.cpp:434` (clear path),
  `:537` (blocked path), `:651` and `:666` (the HCMap paths) -- so fed the
  sentinel they all evaluate true and set `newZ = groundZ + modelYOffset`,
  teleporting the player 1,000 units up and ending the jump there. The swim
  clamp on the very same value is guarded (`:483`, `:581`:
  `maxReasonableGround = ctx_.playerZ + 100.0f`); the landings are not. The
  HCMap pair additionally guard only against `BEST_Z_INVALID`, which the
  sentinel is not. Not observed live -- found by reading, while T435 Leg C was
  ruling this family out as B424's mechanism -- so a repro is owed: jump under
  low geometry (a Kelethin platform underside, a dungeon ceiling) where the
  down-ray finds only the ceiling.~~ **CLOSED by T437**
  (`refactor/T437_ground_sentinels.md`). The magic floats on this path are
  gone: `findGroundZ()`, `findGroundZIrrlicht()` and
  `findGroundZBspFiltered()` all return an `EQT::Graphics::GroundZ`
  (`include/client/graphics/player/ground_query.h`) -- a floor, or NoFloor,
  with no conversion to float, so the COMPILER rejects a site that forgets to
  ask `hasFloor()`. It caught one the entry had not listed: the walking
  step-height path in `resolveIrrlichtClearPath()`. The four landings and the
  four swim clamps now go through `ShouldLandJump()` /
  `ShouldClampSwimToFloor()`
  (`src/client/graphics/player/ground_query.cpp`), device-free and tested.
  **The two `ctx_.playerZ + 100.0f` guards are DELETED, not kept**: they
  existed only to exclude the sentinel, which can no longer reach them, and
  they also wrongly excluded a genuine floor more than 100 units above.
  `tests/test_ground_sentinels.cpp`, 12 cases (5 red pre-fix). The live repro
  the entry owed is now **B437**: the BSP query needs a device and a loaded
  zone, so it cannot be driven headless.

- ~~**B436** (A, live defect) **`HCMap::FindBestZ()` silently returns a CEILING
  as ground.** `src/client/hc_map.cpp:793-803`: if the downward raycast finds
  nothing it re-casts UPWARD ("Try casting ray up if nothing found below") and
  returns that hit's z with no marker distinguishing it from a real floor.
  This is the only mechanism in the tree that literally resolves ground above
  the player, and its callers cannot tell -- `findGroundZ()`
  (`src/client/graphics/player/renderer_player_collision.cpp:73`) treats the
  answer as a floor, and the game-side `GetBestZ` -> `FixZ`
  (`src/client/zone/zone_component.cpp:1106`) writes it into the player
  position. It is off the live collision path today (the BSP path wins
  whenever `regionWorldTriangles` is non-empty, which is every real zone) and
  `FixZ` is gated by `abs_diff < 20.0f`, which is why it is not currently
  producing B424's 71 units -- but the fallback is a silent lie and either
  needs a distinct return (a "ceiling, not floor" flag) or deleting. Found by
  T435 Leg C.~~ **CLOSED by T437: DELETED.** The choice was made by reading
  what each caller could do with a ceiling, and the answer was nothing --
  `ZoneComponent::FixZ()` wrote it into the player position, `findGroundZ()`
  handed it to the landings and the swim clamp, and SimBoids' three queries
  placed creatures on it. A distinct "ceiling" return would have had ZERO
  consumers, which is this project's own recurring defect class, so the
  upward re-cast is gone and "nothing below" is `BEST_Z_INVALID`, which every
  caller already handled. It is not a lost recovery for a player sunk under
  the floor: the DOWNWARD cast already starts at `start.z + 10`
  (`src/client/hc_map.cpp`, `FindBestZ`), so any penetration under ten units
  is resolved without the re-cast, and past ten units "recovery" is a
  teleport. **Measured headless on real dungeon geometry** (T437 Review,
  `/mnt/projects/akk-stack/server/maps/base/befallen.map`): of 3,600 sampled
  columns queried from below the geometry, **1,709 would have returned a
  ceiling as ground**, one of them 48.16 units above the query point.
  Pinned by `tests/test_ground_sentinels.cpp` (`FindBestZT437.*`, hand-built
  V1 map files: floor below / ceiling only / both / empty column, plus the
  `FixZ` arithmetic).


## Added 2026-09-07 by T437 (ground sentinels; found while closing B435/B436)

- **B437** (A, live proof owed) **B435's live repro is still owed, and it → issue #24
  cannot be taken headless.** T437 fixed the ceiling sentinel and pinned it
  device-free, but the path that produced it --
  `PlayerCameraComponent::findGroundZBspFiltered()` --  reads
  `ctx_.zoneBspTree` and `ctx_.regionWorldTriangles`, both populated only by
  the zone loader with a live Irrlicht device, and the comparison it feeds
  sits inside `resolveIrrlichtClearPath()`, which additionally needs an event
  receiver, an entity renderer and a bridge. There is no headless harness for
  either. The B436 half WAS reproduced headless on real dungeon geometry (see
  that entry). What is owed: with a client under the eqemu lock, jump under
  low geometry -- a Kelethin platform underside or a Befallen ceiling -- and
  confirm the jump ends on the original floor rather than 1,000 units up.
  Prov: T437.

- ~~**B438** (B) **the BSP ground query answers "the floor is at your feet" when
    its ray hits NOTHING AT ALL, and a falling player therefore lands in mid-air
    over a hole.** `findGroundZBspFiltered()`
    (`src/client/graphics/player/renderer_player_collision.cpp`) initialises
    `float groundZ = feetZ;` and returns `GroundZ::Floor(groundZ)` when neither
    the near nor the full raycast hits, so `ShouldLandJump()` sees
    `feetZ <= feetZ`, evaluates true, and ends the jump wherever the player
    happens to be. The `!ctx_.zoneBspTree` early return does the same thing.
    T437 deliberately did NOT change this: it is pre-existing behaviour, the
    alternative (NoFloor, so the player keeps falling) has no test coverage and
    no measurement behind it, and turning it on inside a unit scoped to the
    ceiling sentinel would have mixed two behaviour changes in one diff. Deciding
    it needs the same live rig B437 needs -- walk off a gap in a zone whose BSP
    region has no triangles under the column and see what the oracle does.
    Prov: T437.~~
  **CLOSED by T440** (`refactor/T440_ground_miss_and_dead_stores.md`):
  `findGroundZBspFiltered()` answers `GroundZ::NoFloor()` on a total miss AND
  when `ctx_.zoneBspTree` is null. The verdict moved into the device-free
  `EQT::Graphics::ClassifyBspGroundRay()` beside T437's two decisions, so the
  three places that spelled the predicate inline cannot drift. Every consumer
  was verified: a falling player keeps falling and lands on the first real
  floor the 500-unit ray finds; the swim clamps do not clamp; the walking
  step-height path and the two wall slides already carried `hasFloor()` guards
  and now refuse the move instead of accepting a phantom floor at the feet.
  Pre-fix red: 3 of the 5 new `GroundMissT440` cases in
  `tests/test_ground_sentinels.cpp`, produced by restoring the defect in a
  standalone link (`review/T440/README.md`); 17/17 after. The live repro is
  still **B437**, which covers this fix too.


## Added 2026-09-07 by the orchestrator (Will's direction: doors, lifts, traps)

- **B420** (A, feature — **WILL 2026-09-07**) lifts, elevators, click-driven → issue #25
  switches and triggered traps are unimplemented. The server sends them as
  `doors` (opentype / name-coded; some may be placeables); fable's
  `DoorStateManager` + `src/client/graphics/door_manager.cpp` (1,394 lines)
  handle open/close only. Required: animate as the official client does,
  lifts/elevators become walkable MOVING surfaces the player rides, traps
  (blades/saws/spikes) damage in range as the server dictates. First test:
  Greater Faydark's lifts to Kelethin. Baseline: the official client. → T433
  (survey + oracle capture), then the implementation units it names.

- **B430** (B) **The Options window's General page is nineteen checkboxes with → issue #144
  no setting behind any of them.** T434 drew the whole page at the oracle's
  offsets (`options_layout.h`, from `EQUI_OptionsWindow.xml`) and wired only
  what exists, so `OGP_FastItemDestroyCheckbox` .. `OGP_AutoConsentGuildCheckbox`
  plus `OGP_EnvSoundsCheckbox` and `OGP_CombatMusicCheckbox` are drawn greyed
  and are NOT hit-testable (`hitTestOptionsControl` skips
  `OptionsLayout::Setting::None`, pinned by
  `OptionsWindowT434.ADisabledControlIsNotHitTestable`). Several have a real
  server or client counterpart that could be wired one at a time --
  `OGP_AnonymousCheckbox` and `OGP_RoleplayingCheckbox` are `/anon` and
  `/roleplay`, `OGP_LootAutosplitCheckbox` is `/autosplit`,
  `OGP_CombatMusicCheckbox` is `CombatMusicManager::setEnabled()` (which has
  no caller outside its own class) -- each needs an intent or a config field,
  which is why none was invented here. Prov: T434.

- **B431** (B) **The Options window's Display page has one wired control out → issue #145
  of forty-seven.** `ODP_ClipPlaneSlider` drives `setRenderDistance()`; every
  other control on `OptionsDisplayPage` is greyed. The near ones:
  `ODP_PCNamesCheckbox`/`ODP_NPCNamesCheckbox` -- fable has ONE nametag
  toggle (`DiagnosticsComponent::toggleNameTags()`, Ctrl+F3), not the
  oracle's separate PC and NPC flags, so wiring either to it would make one
  control silently move the other; `ODP_MaxFPSSlider` and `ODP_GammaSlider`
  have no setting at all; `ODP_SetFullscreenButton`/`ODP_SetWindowedButton`/
  `ODP_VideoModesButton` are mode switches an EGL-surfaceless or DRM client
  does not have; the four fade/alpha sliders belong to the window chrome
  T432 measured (`Fades`, `Delay`, `Duration`, `Alpha`, `FadeToAlpha` in the
  official UI ini) and fable's panels do not fade at all; and the nested
  `ODP_ParticlesTabs` box is drawn with its three sub-tab captions and the
  Spell page's rows, but the sub-tabs are NOT clickable because all nine
  controls behind them are disabled. `config/spell_effects.json` and
  `config/display_settings.json` between them hold real density/opacity
  knobs, so the particle page is the most wireable of these. Prov: T434.

- ~~**B432** (B) **The Options window's volume sliders can drift from the audio
  manager, because the only volume intents carry a DELTA.**
  `MusicVolumeChangeIntent` / `EffectsVolumeChangeIntent` take a `float
  delta`, and nothing publishes the resulting absolute volume back to the
  renderer, so `FrameComponent` keeps its own record of what it last told the
  audio manager (`optionsAppliedMusicStep_` / `optionsAppliedSoundStep_`) and
  sends the difference. A `/volume`, `/music` or `/sound` slash command
  (`commands_spells.cpp`) sets the audio manager ABSOLUTELY and the panel
  never hears about it, so the slider then shows a stale value and its next
  drag applies a delta computed from that stale value. Two ways out: an
  absolute set-volume intent, or a `VolumesChanged` event published wherever
  a volume moves (four sites: the config apply in
  `bridge_component_ui_audio.cpp`, the two delta intents, the three slash
  commands). Not a live defect until a player mixes the two paths. Prov: T434.~~
  **STRUCK T439 (base `d1458444`): BOTH ways out, because they are halves of one
  round trip.** `MusicVolumeChangeIntent` / `EffectsVolumeChangeIntent` now carry
  an ABSOLUTE `int step` 0..20 -- the oracle's own slider unit, moved to the
  device-free `include/client/audio/volume_steps.h` so the game thread can speak
  it -- and `BridgeComponent::PublishVolumesChanged()` is the ONE publisher of the
  new `GameEventType::VolumesChanged`, called from all five sites that move a
  volume (the config apply in `InitializeAudio()`, both intent handlers, and
  `/musicvolume` + `/effectsvolume`). It READS the audio manager back rather than
  echoing what it was handed, so the event states what the audio manager holds
  after its own clamping. `EQT::Graphics::applyVolumesChanged()` writes it into
  `OptionsPanelState`, so `optionsAppliedMusicStep_` / `optionsAppliedSoundStep_`
  are DELETED and there is exactly one record of a volume on each side of the
  bridge. The volume hotkeys moved to the same unit
  (`getMusicVolumeStepDelta()`, +/-2 steps = the old +/-0.1f exactly) and now add
  to the published step, so a hotkey press moves the slider too. Pinned by
  `B432Census.*` x3 + `OptionsVolumeT439.*` x4 in
  `tests/test_options_merchant_residue.cpp` -- including
  `ASlashCommandThenADragLeavesTheAudioManagerOnTheSlider`, the board's own
  scenario over the real functions. **`/volume` (MASTER volume) is deliberately
  NOT in the round trip**: the Options window has no master slider, so there is
  nothing to keep in sync -- see the new **B449**. ~~**LIVE PROOF IS OWED**~~
  **LIVE-PROVEN T449 (this commit)**: `/musicvolume 25` typed into the chat bar
  -- a slash command behind the window's back, the exact case B432 was about --
  moves the Options slider to 25 %, and a click on the slider logs
  `Options: music volume 90 %` then `Music volume: 90%` from the AudioManager.
  The values persist as `[Options] Music=5 SoundVolume=20 ClipPlane=19` on the
  oracle's 0..20 scale. NOT proven audibly: miniaudio selects its NULL device
  on this host, so there is no ALSA stream to capture.
  `review/T449/README.md` §4.

- **B433** (B) **The Options window's Mouse, Keys, Filters, Colors and Mail → issue #146
  pages are drawn and entirely inert.** Every control on all five is
  `OptionsLayout::Setting::None`. Per page: MOUSE -- fable's player mouse-look
  multiplies by hardcoded `0.5f`/`0.3f` in `renderer_player_physics.cpp` and
  `CameraController::mouseSensitivity_` is set once to `0.2f` and never
  again, so `OMP_MouseSensitivitySlider` has no knob to turn and the five
  checkboxes (invert Y, look spring, mouse look, wheel zoom, handedness) have
  no flags; KEYS -- `OKP_KeyboardAssignmentList` could be filled READ-ONLY
  today from `HotkeyManager::getBindingsForAction()` /
  `keyCodeToName()` (the oracle's own three columns are Command 180 /
  Keypress 75 / Alternate 75), which is the cheapest of these to close, and
  rebinding on top of it needs a writer plus persistence (bindings are
  GLOBAL, `config/hotkeys.json`, not per character -- T432); FILTERS -- fable
  has no chat filter model at all; COLORS -- fable has no user colour table
  (T432 ruled UI colours out of scope); MAIL -- there is no mail feature and
  it is arguably post-Velious anyway, so this page may be the one to DELETE
  rather than fill. Prov: T434.

- ~~**B434** (B) `PopupType::Options` is still a bare enumerator with zero uses.
  T434 built the Options window as an independent window with its own
  `OptionsPanelState` and its own oracle placement (`EQUI_OptionsWindow.xml`
  is `RelativePosition false` at `<Location>(90,47)</Location>`), NOT as one
  of the mutually exclusive centre popups, so the enumerator stayed unused
  rather than being pressed into a role the oracle does not give it. It is
  now the only member of `PopupType` with no producer and no consumer
  (`Vendor`, `Bank` and `Trade` at least appear in tests). Delete it, or
  decide the centre-popup family is what it should have been. Prov: T434.~~
  **STRUCK T439 (base `d1458444`): DELETED,** with the T434 reading confirmed --
  `EQUI_OptionsWindow.xml` is `RelativePosition false`, so the window is drawn
  and hit-tested above every centre popup and opening it closes nothing. It was
  never a centre popup and is not pressed into being one. The three comments
  that named the enumerator (`options_panel.h`, `frame_component.h`,
  `test_options_window.cpp`) were rewritten rather than left as claims about a
  symbol that no longer exists. Pinned by `B434Census.*` x3.
  **ONE CORRECTION to this entry, measured:** its parenthetical is wrong. A
  comment-blind grep over `src/ include/ tests/` finds only `None`, `Inventory`
  and `Vendor` used anywhere -- `Bank` and `Trade` appear in NO test, and
  `Spellbook`, `Loot` and `Skills` have no use either. Five more dead
  enumerators, boarded as the new **B450**.


  the CASTER's own client while drawing correctly on an observer.**~~ **STRUCK
  T426 (this commit, base `8f5e586a`): reproduced deterministically and fixed.**
  * **Mechanism.** eqemu sends TWO `OP_Action` packets for one cast (T395):
    `effect_flag 0` to everyone in range, `effect_flag 4` to the target and the
    caster only. `irrlicht_bridge.cpp`'s `SpellCastVisualComplete` arm therefore
    runs TWICE on the caster and ONCE on an observer, and it calls
    `SpellVisualFX::removeCastGlow(casterId)` unconditionally, BEFORE the
    `isCastAnimation` gate that creates the burst. `removeCastGlow` issued
    `ParticleManager::removeSpellEffectsForEntity()`, whose worker arm removes
    every effect whose CASTER **or** TARGET is that entity -- and a self-target
    cast (`#castspell`, any self-buff) puts the caster's own spawn id in both.
    So the SECOND packet's teardown destroyed the impact burst and the
    spell-complete burst the FIRST had just created; the observer, which never
    receives the second packet, kept its copy. The T395 attach-position/frustum
    suspicion B400 recorded is not it, and the intermittency is the emission
    window racing the second packet.
  * **Fix.** The removal command carries an optional style filter
    (`SpellEffectDef::name`); empty keeps the old "everything on this entity"
    behaviour (`removeAllForEntity`, a despawn), and `removeCastGlow` names
    `{"CastGlow", "CastSpray"}`. Pinned by `tests/test_caster_own_landing.cpp`
    (4 cases) on the real chain -- a real `SpellVisualFX` on an EDT_NULL device
    driving a real `ParticleManager` whose drained commands go to a real
    `SimulationWorker`, no hand-built commands. Two of the four fail pre-fix;
    T397's `test_spell_effect_removal` (the unfiltered contract) stays green.
  * A live caster/observer re-capture of the four spells is **owed** -- T426 spent
    its live budget on Leg A's six-heading oracle A/B and Leg B's fog table.
  Prov: T426 Review 2; T422 Review 4.


---

## Added 2026-09-07 by T429

- ~~**B415** (B) `include/client/skill/skill_data.h` `animation_code` --
    **a duplicate store nothing reads.** `initializeSkillData()` copies
    `getSkillAnimationCode(skill_id)` into the struct, and the one consumer
    that wants a skill's animation code -- `SkillManager`, at
    `src/client/skill/skill_manager.cpp:719` -- calls the free function
    directly instead. So the field has a writer, a plausible reader, and zero
    actual reads. Measured by T429 Leg B while closing B298's skill half; it is
    NOT a missing display (there is no eqstr line and no
    `EQUI_SkillsWindow.xml` element for an animation code). Closes when the
    field is deleted or `SkillManager` reads it. Found by T429 Leg B.~~
  **CLOSED by T440**: the field is DELETED. Deleting is what removes the
  duplicate store -- `SkillManager::playSkillAnimation()` already calls
  `getSkillAnimationCode(skill_id)`, so the table is the source and a second
  copy could only go stale. `skill_data.h` carries a tombstone naming the
  consumer, and the `EXEMPT` row went out of
  `scripts/refactor/check_write_only_fields.py` with it.

- ~~**B416** (B) `include/client/skill/skill_data.h` `usable_while_sitting`,
    `src/client/skill/` -- **a gate predicate with no gate.**
    `initializeSkillData()` sets it true for Meditate (31) and false for
    everything else, and no sitting test exists anywhere under
    `src/client/skill/` (`grep -i sitting` over `src/client/skill/*.cpp` and
    `include/client/skill/*.h` returns only the declaration, the assignment,
    and an unrelated `"p07" // Sitting (meditation pose)` animation comment).
    The same shape as `requires_combat` before T325, except that one at least
    had a predicate. Deciding whether fable should refuse a skill while sitting
    needs the eqemu handlers read per opcode the way T325 read them for
    in-combat. Found by T429 Leg B.~~
  **CLOSED by T440: the field is DELETED, and the eqemu handler text decided.**
  The whole zone server has exactly two sitting refusals for a player-initiated
  action -- `Client::RangedAttack()` at
  `/mnt/projects/akk-stack/code/zone/special_attacks.cpp:976` and
  `Client::ThrowingAttack()` at line 1597, each an `IsSitting() ||` term in a
  refusal list -- and NEITHER is reachable from fable:
  `Client::OPCombatAbility()` routes to them only when
  `ca_atk->m_atk == EQ::invslot::slotRange`, while
  `CombatManager::UseAbility()` (`src/client/combat.cpp:645`) hardcodes that
  field to 100, and Archery(7)/Throwing(51) are not in `isActivatableSkill()`.
  Not one of the seventeen dedicated skill opcode handlers in
  `/mnt/projects/akk-stack/code/zone/client_packet.cpp` reads the appearance.
  The only stand-up gate in the server is for SPELLS
  (`/mnt/projects/akk-stack/code/zone/aa.cpp:1323`, `STAND_TO_CAST`) and it
  stands the caster UP rather than refusing. The field was also backwards as a
  gate -- true for Meditate(31) alone, so a predicate over it would have
  refused every OTHER skill while sitting, the T325 over-strictness again.

- **B417** (B) `src/client/commands/consider_text.cpp` -- **the `/con` → issue #147
  difficulty tail is fable's wording, not the oracle's.** eqstr 12239 is
  `%1 %2 -- %3`; T429 fills `%1` and `%2` from the table (name, and
  12212..12220 for the faction phrase) but fills `%3` with fable's own
  `cons <colour>` text. The oracle's own candidates are 12223..12238 --
  sixteen lines, several near-duplicates of each other ("looks kind of risky,
  but you might win." at 12228 vs "looks kind of risky..you might win." at
  12230) -- and the shipped data carries **no con-colour -> line mapping**;
  that table is in the official client's executable. Picking six of sixteen
  would be inference, so T429 drew fable's tail and said so. Closes when the
  mapping is MEASURED against the official client (we control the server, so
  a con at each colour band is a scriptable observation). Found by T429 Leg B.

- ~~**B418** (B) `include/client/state/entity_manager.h` `guildId`,
  `deltaHeading`; `include/client/eq_types.h` `anon_status`;
  `src/client/login_world/login_world_component.cpp`
  `WorldProcessGuildsList` -- **the three entity fields B298 named, and the
  one of them that is a real gap.** T429 Leg B measured all three against the
  oracle: `deltaHeading` is a movement field with no line and no element,
  `anon_status` duplicates state the /who path already carries on its own
  wire entries, and **`guildId` is the real one** -- the official client
  prints a player's guild and fable cannot, because
  `WorldProcessGuildsList` is a single `LOG_DEBUG` that parses none of
  `OP_GuildsList`, and `data/willeq.db` has no guild table. So there is no
  guild-id-to-name source in the client at all, the same shape as **B391**'s
  missing item-name lookup. Closes when `OP_GuildsList` is parsed into a
  lookup (which also unblocks a guild column wherever one belongs). Found by
  T429 Leg B.~~ **CLOSED by T443** (`refactor/T443_guild_list_esc_dumpscene.md`) on its
  stated close condition: `OP_GuildsList` IS parsed now, into
  `eqt::guild::GuildNameTable` (`include/client/guild/guild_name_table.h`),
  owned by `LoginWorldComponent` and reachable as `GuildNames()`. The wire
  shape is quoted, not inferred, and one fact in it is the opposite of the
  obvious reading: **the array index IS the guild id** -- Titanium's
  `ENCODE(OP_GuildsList)` writes `out->Guilds[g.guild_id].name`
  (`/mnt/projects/akk-stack/code/common/patches/titanium.cpp:819`) into a
  fixed `uint8 head[64]; GuildsListEntry_Struct Guilds[1500]` of 96,064 bytes
  (`/mnt/projects/akk-stack/code/common/patches/titanium_structs.h:1412`), sending no id field and appending nothing,
  so a "walk the entries in order" reading misnames every guild on any server
  whose ids are not dense from zero. Guild ids >= `MAX_GUILD_ID` (1500,
  `/mnt/projects/akk-stack/code/common/patches/titanium_limits.h:287`) are dropped by the server and are not
  addressable at all. Pinned by `tests/test_guild_name_table.cpp` (13 cases,
  1 red pre-fix). **The three entity fields are NOT closed by this and stay
  write-only**: T443 measured where the lookup could be DISPLAYED and found
  nowhere it may be drawn yet -- the /who guild fragment is a wire STRING the
  server already fills (`sprintf(guildbuffer,"<%s>", guild_mgr.GetGuildName(...))`,
  `/mnt/projects/akk-stack/code/world/clientlist.cpp:824`, and the same in
  `/mnt/projects/akk-stack/code/zone/entity.cpp:4970`), so /who needs no lookup; `EQUI_TargetWindow.xml`
  has no guild element (T429); and the overhead name tag has no measured
  oracle format. That display half is **B455**.

---

## Added 2026-09-07 by T433 (doors, lifts, switches and traps: the census)

**B420 annotation (census, from T433's read-only SQL over `peq.doors`).**
19,249 door rows / 429 zones; **6,099 rows / 140 zones** inside Will's era
(`zone.expansion <= 3`). Sixty-five distinct `opentype` values exist and
**fable gives distinct treatment to seven** (0/5/56 rotate 90 deg over 0.5 s;
100/105 spin at 42.35 deg/s; 50/53/54 are never created). Every other
opentype — 59 lifts, 40 levers, 55/57/58, 71, 76/77, 120 traps, all of them —
takes a default path whose `openHeading == closedHeading`, so the door changes
state and **rotates by exactly zero degrees**. There is no translation code in
the renderer at all and `door_param` is never read. **Lifts are opentype 59**;
the era set with real travel is gfaydark 69/77/80 (`FAYLEVATOR`,
`door_param` 68/98/69, `invert_state` 1) with six `FELE2` call buttons
(`triggerdoor` → the lift, `door_param` 1), plus paineel 25, citymist 70,
mistmoore 3, acrylia 27, echo 7. Era traps: soldunga `SPEARDOWN` x2
(opentype 120), sebilis `SBDOOR103` x14 + bazaar x4 (opentype 71), steamfont
`WMBLADE` x3 + airplane `AIRWMBLDS` x2 (opentype 105). 1,370 rows carry a
`dest_zone` (691 at opentype 58, 416 at 57); 591 carry a `triggerdoor`.
**Correction to the plan's premise: there is no `lift`, `trigger_door` or
`trigger_type` field in the wire `Door_Struct`, and no `lift` column in
`peq.doors`** — chains and destinations are resolved server-side and reach the
client only as `OP_MoveDoor{doorid, action}` or a position change. Full tables
in `refactor/T433_doors_lifts_survey.md` Review; raw dumps in
`review/T433/data/`.

- ~~**B421** (A, feature — **gates B422/B423/B426**) **the official client cannot
  be made to click a door under the Xvfb/xdotool oracle rig, so every door
  timing in fable is still an invented constant.** T433 turned on eqemu's own
  arrival log (`#logs set gmsay 10 3`; `client_packet.cpp:4671` logs every
  `OP_ClickDoor` before any gating) and ran **six** click styles (plain click,
  press-hold-release, double-click, right-click, the project's `jclick` with
  its 1px move, hover-then-click) plus a 30-point grid over the lift, at 3 and
  at 10 units, on the gfaydark lift AND on an ordinary opentype-0 `FAYDOOR`:
  **zero `client handle` lines**. The log was live throughout (NPC-driven
  `[Doors] [ForceOpen]` lines streamed continuously) and world clicks do reach
  the client (an item-on-cursor click raised the client's own drop-confirm
  dialog), so this is the client's 3D door pick refusing synthetic input, not
  lost input. The click-free routes are closed too: `Mob::HandleDoorOpen()`
  skips every door with `door_param > 0` (i.e. every lift and every button), and
  `#door` is a geometry editor with no open/toggle. Two candidate fixes, one
  needing Will: (a) drive the pointer with real multi-frame motion / find the
  client's door-use keybind in `eqclient.ini`; (b) a one-line Perl quest
  calling `ForceOpen()` — server *content*, not server code, needs a ruling.
  Deliverable once unblocked: one recorded lift cycle with per-frame platform
  Z (travel time, speed profile, dwell, whether the player is carried), plus
  the rotation duration of one opentype-0 door and the spin rate of one
  100/105 — the three constants fable currently invents. Evidence:
  `review/T433/probe/T433_oracle_clickstyles_doorslog_6styles.png`,
  `review/T433/video/T433_grid_lift69.mkv`. Found by T433 Leg B.~~ **CLOSED by T438** (`refactor/T438_lift_oracle.md`). Route (d) is what worked: a one-file Perl player quest calling eqemu's own `quest::forcedooropen()`, installed for the capture and removed with the world down. `Doors::ForceOpen` (`/mnt/projects/akk-stack/code/zone/doors.cpp:658`) emits the SAME `OP_MoveDoor` a click emits, so the recordings ARE the official client's animation. **Correction to this entry and to T438's own first pass: the door-use keybind DOES exist** -- the client's Options > Keys page (category All) lists "Use centerscreen -- Keypress **U**" (`review/T438/probe/T438_oracle_options_keys_use_centerscreen_U.png`; Will named it mid-unit). It does not fire either: twelve `u` presses across an ordinary FAYDOOR (facing at 12.6 units and touching), the lift platform (on the screen centre at 10 and 4 units) and a FELE2 call button produced no `OP_ClickDoor` with the Doors log at Debug 3 to gmsay AND file, and a control proves the key reaches the game rather than the chat bar (the chat input box stays empty). Route (b) adds the diagnostic that the pointer IS tracked -- a hover raises the Actions window's `<Ctrl S>` tooltip -- so what refuses synthetic input is the 3D WORLD-INTERACTION PICK, mouse and centre-screen key alike. Route (c) cannot work by construction: `Mob::HandleDoorOpen()`'s only caller is `mob_ai.cpp:985`, NPC AI. All three deliverables measured: see B422/B423 and `review/T438/README.md`. **Rig lesson**: a zone shipping `quests/<zone>/player.lua` SHADOWS a `player.pl` beside it; the fixture has to go in `quests/<zone>/v0/player.pl`.

- ~~**B422** (A, feature — needs B421) **opentype 59 lifts do not move.** fable's
  `DoorVisual` has no translation state at all; `update()` only lerps a Y
  rotation and, for opentype 59, lerps it between a value and itself.
  `DoorState::doorParam` is stored and read by nothing. Work: a translation
  animation on `DoorVisual` driven by `door_param`, open = far end, closed =
  spawn, with `invert_state` applied to the wire action byte. Applies only to
  opentype 59 with `door_param > 0` — the 21 era rows with `door_param` 0/-1
  (grobb signs, acrylia torches, arena tower) are decorative and must keep
  today's behaviour. **STOP: direction and timing come from B421's video, never
  from the `door_param` arithmetic.** Found by T433 Leg A.2.~~ **CLOSED by T438.** Lifts translate. `DoorVisual` gains `doorParam`/`isLift`/`LiftCycle`
(`include/client/graphics/door_motion.h`, `src/client/graphics/door_motion.cpp`), the wire `door_param`
is threaded from `DoorSpawnedData` and the zone-load snapshot through `createDoor`/`registerDoor`,
and `DoorManager::update()` (now in `door_manager_animation.cpp`) moves the pivot's Y.
**MEASURED, not derived from the arithmetic** (the entry's own STOP): travel is `door_param` units at a
CONSTANT **8.67 units/s**, a **20.5 s** dwell at the far end and an automatic return -- all from ONE
`OP_MoveDoor`, and a second packet mid-cycle is ignored (proven: the run with a 45 s server close timer
has identical motion to the run with a 5 s one). At rest the platform sits at `pos_z + door_param`, the
FAR end. Two lifts of different `door_param` settle the speed and the dwell at once: door 69 (param 68)
28.328 s open-to-return, door 77 (param 98) 31.788 s. `door_param` 0/-1 rows still do not move.
Pinned by `tests/test_door_motion.cpp` (16 cases; 4 red pre-fix). Evidence: `review/T438/README.md` s2b.

- ~~**B423** (A, feature — needs B421 and B425) **a lift is not a walkable moving
  surface.** Two halves beyond B425's missing writer: (1) a translating door's
  collision triangles must be re-baked or transformed each frame, and
  `rayTestDoorsAndObjects()`'s `if (door->isAnimating) continue;` — correct for
  a hinged door, since EQ lets you walk through one mid-swing — is exactly
  backwards for a lift, which is animating for its whole ride; (2) fable owns
  player position and sends it to the server, so "the player rides it" means
  adding the platform's per-frame Z delta to the player while their feet are on
  it. **STOP: whether the official client carries the player or merely keeps
  colliding with a rising surface is B421's measurement.** Found by T433.
  **T435 annotation:** half (a) -- the missing writer -- is done and boarded
  separately as the now-closed B425. What remains here is unchanged: a
  TRANSLATING door's re-bake (the hook exists -- `DoorManager::update()` marks
  a door's collision dirty on every frame it moves), lifting the
  `isAnimating` walk-through suppression for lift opentypes
  (`RayTestDoorCollision()`, `src/client/graphics/door_collision.cpp`), and
  carrying the player. All three still wait on B421's measurement.~~ **CLOSED by T438 except the live proof of the carry (-> B449).** (1) The re-bake: a lift's collision is
baked from its LIVE pose into both slots (`door_manager_collision.cpp`), and `DoorManager::update()` marks
it dirty on every frame the platform moves. (2) The walk-through suppression is scoped: `DoorRuntimeState`
gains `isLift` and `RayTestDoorCollision()` skips an animating door only when it is NOT a lift
(`src/client/graphics/door_collision.cpp`) -- pinned by
`DoorCollisionRay.AnAnimatingLiftStillCollides`, red pre-fix. (3) The carry:
`PlayerCameraComponent::carryPlayerOnLifts()` (`src/client/graphics/player/renderer_player_lift.cpp`),
called from `renderer_frame_input.cpp` right after `syncDoorCollision()`. **The oracle DOES carry the
player** -- measured on gfaydark lift 69 with the rig character parked on the platform: z
5.81 -> 10.93 -> 19.98 -> 29.30 -> 39.47 -> 49.87 -> 58.65 at the platform's own 8.67 units/s
(`review/T438/logs/trace_lift69_ride.tsv`).

- ~~**B424** (A, live defect) **fable resolves ground ~71 units too high at the
  gfaydark Kelethin lift.** `#goto 340 137 6` then `/loc` returns
  `137.0, 340.0, 77.0`, and `#goto 350 137 8` returns `137.0, 350.0, 77.0` —
  reproduced in two independent sessions at two coordinates. The **official
  client on the same server at the same spot returns z `-0.09`** (the forest
  floor). fable puts the player on the Kelethin platform 70+ units above, with
  the camera inside a tree trunk. This blocks all lift work in gfaydark: an
  executor cannot stand next to the lift.
  Evidence: `review/T433/fable/T433_fable_lift69b_A_beside_lift_1024x768.png`,
  `…_C_on_lift_settled_…png`, `review/T433/logs/fable_session_lift69*.log`
  vs `review/T433/probe/T433_aim_a384_1024x768.png`. Found by T433 Leg B.5.

  **T435 Leg C (offline, STOPPED at the plan's own STOP): this entry's stated
  mechanism is WRONG, and the leg stopped rather than invent the right one.**
  fable did not resolve ground too high; in that session it resolved ground
  **not at all**. `PlayerCameraComponent::resolveMovementCollision()`
  (`src/client/graphics/player/renderer_player_physics.cpp:317`) is gated on
  `isMoving || isJumpMoving || isSwimMoving`, and
  `review/T433/logs/fable_marks_lift69b.tsv` records no movement key between
  the `#goto` and the `/loc`. Corroborating: the SimulationWorker PVS anchor is
  computed from `ctx_.playerX/Y/Z`
  (`src/client/graphics/sim/renderer_sim_bridge.cpp:276`) and changes exactly
  twice in the whole session -- at zone-in, and at the `#goto` -- then holds,
  so the RENDERER stayed at the goto position. `/loc` reads the GAME thread's
  store, so the two DIVERGED: renderer ~6, game state 77.0. Neither
  `findGroundZBspFiltered()` nor `HCMap` can produce +71 anyway --
  `findGroundZBspFiltered` casts DOWN from at most `currentZ + 2` and its only
  above-the-player return is the `feetZ + 1000` ceiling sentinel
  (`renderer_player_collision.cpp:574`). The self-echo hypothesis is dead on
  evidence: `ZoneProcessClientUpdate`'s unconditional "RECEIVED OUR OWN
  ClientUpdate" line (`src/client/entities/entities_component_updates.cpp:508`)
  appears exactly ONCE per session, at zone-in, 48 s before the `#goto`. Every
  writer of the game-thread player z was enumerated (`PlayerFacade` is the only
  gateway; `EverQuest::SetPosition` has zero production callers; `FixZ` is
  double-gated by `abs_diff < 20.0f`) and each is excluded by the log.
  **What is still missing is a per-write trace of
  `PlayerFacade::SetPosLocal`/`SetPosition` across one `#goto` at this spot --
  a LIVE, instrumented run under the eqemu lock. Nothing offline settles it**,
  because an offline geometry probe answers "what is the floor here", which is
  a different question from "which handler wrote 77.0". Reword the headline to
  **"after a same-zone `#goto` the game-thread player z diverges from the
  renderer's and reports the Kelethin platform height"** when this is taken up.
  An offline probe IS feasible if the geometry question is also wanted, with no
  graphics device: `WldLoader::parseFromArchive("gfaydark.s3d", "gfaydark.wld")`
  -> `getBspTree()` -> `BspTree::findRegionIndexForPoint()` ->
  `WldLoader::getGeometryForRegion()` (the same call `rebuildRegionMesh()` uses,
  `src/client/graphics/zone_lifecycle/renderer_zone_draw.cpp:954`), plus
  `HCMap::LoadMap`/`FindBestZ` against
  `/mnt/projects/akk-stack/server/maps/base/gfaydark.map`;
  `tools/bsp_region_finder.cpp` is the working device-free precedent.~~ **CLOSED by T438 Leg C, and BOTH stated mechanisms were wrong.** It is not a ground query and it is not
a packet. `PlayerFacade::SetPosLocal`/`SetPosition` now carry their CALL SITE
(`__builtin_FILE()`/`__builtin_LINE()` defaults, `MOD_MOVEMENT` TRACE), and one `#goto 340 137 6` named it
(`review/T438/logs/fable_session_legc.log.gz`): `zone_component.cpp:138` wrote the CORRECT
`(137.0, 340.0, 6.0000)` at 10:35:27.519, and 3 ms later `movement_state.cpp:569` -- `UpdateJump()`'s arc --
overwrote it, ending at 10:35:28.488 with `movement_state.cpp:553` writing `jumpStartZ` FLAT: the z
captured before the relocation. T433's 77.0 is the same arithmetic with a `jumpStartZ` taken on the
Kelethin platform, and it also explains T435's renderer/game-thread divergence, because `SetPosLocal`
writes only the game-thread store. Fixed in `PlayerState::setPosition()` -- the discrete-relocation path,
the one store -- which cancels an in-flight jump and rebases `jumpStartZ`; `setPositionNoEvent()`, the
per-tick integrator, is deliberately untouched. Pinned by `tests/test_jump_relocation.cpp` (4 cases; 2 red
pre-fix). Live after: `/loc` reads `137.0, 340.0, 6.0`.

- ~~**B425** (A, live defect) **doors have no collision anywhere in fable, and
  the code that would give them collision has no writer.** `DoorCollisionData`
  (`include/client/graphics/renderer_types.h:414`) and
  `ctx_.doorCollisionData` are *read* by `rayTestDoorsAndObjects()`
  (`src/client/graphics/player/renderer_player_collision.cpp:336`), which is
  wired into both `checkCollisionBspFiltered()` (walls) and
  `findGroundZBspFiltered()` (standing surfaces). A whole-tree grep over
  `src/ include/ tests/ tools/ benchmarks/ docker/` finds two `clear()` calls,
  one `.size()` in a log line, one context binding and that one read —
  **nothing ever inserts**. The map is always empty, so every door in every
  zone is walk-through and non-standable. `hasCollision` is likewise never
  assigned; its comment ("false for invisible, teleporter, spinning, lifts,
  hazardous") describes an intent that was never implemented, and
  `DoorManager::getDoorSceneNodes()` — documented "for collision detection" —
  has exactly one production caller, a **material swap** in
  `diagnostics_debug.cpp:296`. The reachability pattern again. Found by T433
  Leg A.2.~~ **CLOSED by T435 Leg A** (`refactor/T435_door_collision.md`). The writer now exists: `DoorManager::computeDoorCollision()` bakes a door's world-space triangles in both settled poses, `markCollisionDirty()` marks it at every point the geometry can change (createDoor, registerDoor, buildDoorMesh, rebuildSingleDoor, rebuildPlaceholderDoors, recomputeAllBspRegions, and every frame a door is animating), `clearDoors()` retracts it, and `PlayerCameraComponent::syncDoorCollision()` -- called once per frame right after `DoorManager::update()` in `src/client/graphics/frame/renderer_frame_input.cpp` -- drains that into `ctx_.doorCollisionData`. All three `doorCollisionData.clear()` sites now call `markAllCollisionDirty()` so the map and the dirty set cannot desync. `hasCollision` is assigned from the OPENTYPE by `DoorOpentypeHasCollision()` (`src/client/graphics/door_collision.cpp`), never from the name -- see the T435 Review for the table and its evidence. The door half of the ray test moved out of `PlayerCameraComponent` into the free `RayTestDoorCollision()` in the same TU, so it is testable without a renderer. Pinned by `tests/test_door_collision.cpp` (17 cases: 8 red pre-fix). The stale `getDoorSceneNodes()` "for collision detection" comment is corrected in place.

- **B427** (B, unmeasured decision) **opentypes 50/53/54 are dropped on the → issue #148
  floor with nothing behind the decision.** `INVISIBLE_OPENTYPES = {50, 53, 54}`
  (`door_manager.cpp:22`) makes `createDoor()`/`registerDoor()` return before
  any mesh, node, collision or state exists — **1,310 era rows**, including 926
  `CRATEG` (53), 115 city `DOOR1` and 16 gfaydark `FAYLEVATOR` (54). The server
  visibly opens and closes them: T433's session logged
  `[Doors] [Process] door open and timer triggered door_id [93] open_type [54]`
  repeatedly. Whether the official client draws them is **unmeasured**; settle
  it against the oracle (B421's rig) rather than keeping the guess. Found by
  T433 Leg A.
  **T435 annotation:** `DoorOpentypeHasCollision()` lists 50/53/54 as
  non-colliding explicitly, even though `createDoor()`/`registerDoor()` still
  return before any door record exists, so that if this item ever makes them
  real they do not silently become walls.
  **T438 annotation:** B421's rig now EXISTS and is a one-file Perl quest
  calling `quest::forcedooropen()`, installed at `quests/<zone>/v0/player.pl`
  (see the struck B421 and `review/T438/scripts/t438_player.pl`), so this is
  measurable today: install the fixture, park the official client where a
  50/53/54 row is, force it open, and see whether anything moves. T438 did not
  spend a session on it.

- ~~**B428** (A, live defect) **`invert_state` is never applied to the incoming
  door action byte.** `ZoneProcessMoveDoor`
  (`src/client/entities/entities_component_world.cpp:130`) reads
  `is_open = (action == 0x03)`. eqemu emits `OPEN_DOOR 0x02 / CLOSE_DOOR 0x03`
  for a normal door and `OPEN_INVDOOR 0x03 / CLOSE_INVDOOR 0x02` for one with
  `invert_state = 1` (`/mnt/projects/akk-stack/code/zone/doors.cpp:37-40`), so **every `invert_state = 1`
  door in the game reads backwards in fable** — including all three Kelethin
  lifts (`FAYLEVATOR` 69/77/80). `DoorState::invertState` is populated from the
  spawn packet and then used by nothing on this path. Found by T433 Leg A.3.~~ **CLOSED by T435 Leg B** (`refactor/T435_door_collision.md`), **and this entry had the direction backwards.** `is_open = (action == 0x03)` is the INVERTED-door mapping: `/mnt/projects/akk-stack/code/zone/doors.cpp:37-40` defines `OPEN_DOOR 0x02`, `CLOSE_DOOR 0x03`, `OPEN_INVDOOR 0x03`, `CLOSE_INVDOOR 0x02`, so 0x03 means OPEN only for an `invert_state = 1` door. fable therefore read the three Kelethin `FAYLEVATOR` lifts RIGHT by accident and read every ordinary door in the game BACKWARDS -- the far larger defect. `src/client/entities/entities_component_world.cpp` now branches on `door->invertState`. Pinned by `tests/test_door_invert_state.cpp` (5 cases: the 3 normal-door ones red pre-fix, the 2 inverted-door ones green before and after).

- **B426** (B, feature — needs B421) **switches, levers, traps and teleporters → issue #149
  are all 0-degree no-ops.** The client's whole job for each of these is
  animation + sound; the mechanism is server-side and already reaches fable.
  (a) *Chains*: eqemu resolves `triggerdoor` itself (`doors.cpp:520-540`), so
  one `OP_ClickDoor` returns two `OP_MoveDoor`s and fable need only animate
  both — but opentypes 40, 55, 66, 76, 77 and the opentype-59 buttons must stop
  animating zero degrees first (457 era rows carry a `triggerdoor`).
  (b) *Traps*: era set is soldunga `SPEARDOWN` (120) x2, sebilis `SBDOOR103`
  (71) x14, bazaar `BAZIGATE30x` (71) x4, plus `WMBLADE`/`AIRWMBLDS` (105)
  which fable already spins. **The damage is the server's** and arrives as
  ordinary `OP_Damage`, which fable handles — the client work is animation and
  sound only. (c) *Teleporters*: `EQ::ValueWithin(m_open_type, 57, 58) &&
  HasDestinationZone()` → `MovePC()` (`doors.cpp:540`), a server push; the era
  set is the opentype-0/5 zone doors carrying a `dest_zone`, which fable
  already animates, so this is verify-only. **STOP: each opentype's motion
  (which axis, how far, how long) is a separate oracle measurement — do not
  generalise from opentype 0.** Found by T433 Leg C.
  **T438 annotation:** the rig is unblocked (the struck B421) and the METHOD is
  settled. A Perl `quest::forcedooropen(<doorid>)` fixture plus 30 fps x11grab
  stepped frame by frame gives a rotation duration to ~0.1 s; a 200 ms quest
  timer appending `$client->GetZ()` to a TSV gives a ~4 Hz motion trace that is
  independent of the official client's ~5 renders/s. T438 measured opentype 0,
  opentype 59 and the 105 spinner that way. 40/55/57/58/66/71/76/77/120 and
  opentype 100 are still unmeasured, but each is now a session rather than a
  blocker.

### Added by T436 (2026-09-07)

- **B440** (B) `include/client/graphics/ui/cursor_attachment_layout.h`, → issue #150
  `/mnt/projects/EverQuestP1999/uifiles/default/EQUI_CursorAttachment.xml` --
  **two of the CursorAttachment screen's three pieces have no fable producer.**
  The oracle's screen is `CA_Anim`, `CA_Anim2` and `CA_SpellGem`, all three at
  the same `<Location>(5,5)</Location>`, so at most one is ever visible; the
  official client shows `CA_Anim` for an item and `CA_SpellGem` for a spell
  being scribed off a scroll. fable's cursor holds an ITEM and nothing else --
  `InventoryManager`'s `cursorQueue_` is a queue of `ItemInstance` -- so T436
  drew `CA_Anim` and left the other two undrawn rather than inventing a state
  to fill them. Closes when there is a cursor-held SPELL to draw (the scribe
  path already exists: `ScribeSpellIntent` carries a `sourceSlot`), or when it
  is established that Titanium never puts one there and the two elements are
  recorded as deliberately unused. Found by T436.

- ~~**B441** (C) `src/client/graphics/ui/bank_panel.cpp` (`BW_ChangeButton`), → issue #26
  `include/client/graphics/ui/bank_layout.h` -- **the bank window's Change
  button is drawn greyed and is not hit-testable.** The oracle's Change opens a
  coin-denomination dialog that fable does not have, and the underlying
  capability DOES exist game-side (`BankCurrencyConvertIntent` ->
  `OP_MoveCoin` bank->bank with `cointype1 != cointype2`, T269) -- what is
  missing is a way for one click to supply the from/to/amount triple it needs.
  `/bank convert <from> <to> <amount>` remains the only route. Drawn disabled
  rather than wired to a guess, the T363 rule T434 made mechanical. Closes on a
  decision about what gesture supplies the triple. Found by T436.~~
  **STRUCK (UIGAP-small): there is no dialog -- no EQUI_*.xml declares one and
  the client's own help/tips/ctxbank.html says Change converts all coin, bank
  and person, to the fewest coins. Wired as one click (`BankMakeChangeIntent`,
  `coin_change.h`, `/bank change`), pinned by `tests/test_bank_change.cpp`.**

- **B442** (B) `src/client/graphics/ui/static_ui_input.cpp` → issue #151
  (`StaticUIInput::processCommerceInput`, the bank-slot arm) -- **a bank slot
  can be hovered and cannot be used.** Moving an item into or out of the bank
  is a `MoveItem` between the bank slot and the cursor, and fable's static UI
  latches no drag and no click-to-pick-up: the click arm sets `hoveredSlot` and
  returns. `SendMoveItem(fromSlot, toSlot, quantity)` and `MoveItemIntent` both
  exist and `/equip` already drives them, so the protocol side is complete --
  what is missing is the gesture. The same gap makes the merchant window's SELL
  button reachable only when something else has already put an item on the
  cursor. Closes when the inventory/bank/cursor click model has a pick-up and a
  put-down. Found by T436.
  **OFFLINE HALF DONE B442 (2026-10-05, branch `task/B442-cursor-gesture`)**:
  `eqt::inventory::CursorGesture` + `CursorGestureIntent` -- left click picks up
  / places / swaps / merges in the inventory, bank and loot (to the cursor)
  windows; a stack opens the QuantityWnd, SHIFT = whole, CTRL = one; right /
  ALT+left = item display; a purse row opens the QuantityWnd for coin -- all
  per the oracle ledger review/UIGAP-ORACLE/151/gestures.tsv;
  `/pickup` `/putdown` `/cursor` `/coin`; `tests/test_cursor_gesture.cpp`.
  **B442b (2026-10-06, branch `task/B442b-gesture-completion`)** finished the
  offline gesture set, each case citing its gestures.tsv row in
  `tests/test_cursor_gesture_completion.cpp`: right-click on a carried bag
  toggles its window (rows 19-21; `InventorySlotChangedData::bagSlots`,
  `CursorGestureOp::ToggleBag` -> the `/bag` body); the right-click item
  display shows only while held, Alt+left keeps it (rows 24-27); coin rides
  the cursor as dragitem 644-647 with its count strip (row 29, new event
  `CursorCoinChanged`, B442's render-side `coinHeld_` shadow deleted) and the
  bank's coin rows move on OP_MoveCoin slot 2 (rows 43-44); a motionless world
  click with an item held and `IW_Destroy` raise the reusable
  `EQUI_ConfirmationDialog` widget with eqstr 9057 / 2043 (new event
  `ConfirmationRequested`), Yes sends `OP_GroundSpawn` (empty) / `OP_MoveItem
  30 -> -1` (rows 31-35); `QTYW_SliderInput` takes typed digits (row 5).
  STILL OWED (#151 stays open): the live `pickup_roundtrip` scenario and
  two-client wear check (Leg V), trade/give windows, a right-click display for
  corpse rows, loot-to-cursor vs auto-inventory (the oracle did not capture
  loot; the existing LootItemIntent behaviour is unchanged), the confirmation
  dialog's unanswered auto-dismiss timeout (not measured), world clicks on an
  NPC with an item held (the give gesture, the trade/give unit's).
  **UIGAP-residue (2026-10-06):** loot (left = cursor, right = auto_loot 1,
  rows 46a/46c) and the dialog timeouts (drop 20 s, destroy 10 s, UIGAP-live2
  run3/run5) are now MEASURED and IMPLEMENTED; the trade/give windows landed
  with B896; a corpse row's right click is the auto-loot (row 46c), not a
  display. Still owed: the live `pickup_roundtrip` scenario and the
  two-client wear check.

- **B443** (B) `src/client/graphics/ui/merchant_panel.cpp` → issue #152
  (`renderMerchantPopup`, the trade button) -- **which of Buy and Sell the
  merchant window shows is a design choice, not a measurement.**
  `EQUI_MerchantWnd.xml` gives `MW_Buy_Button` and `MW_Sell_Button`
  byte-identical anchors (`10 / 44-from-bottom / 74-from-left /
  6-from-bottom`, both of them), so the official client can only ever show one
  of the two, and the file says nothing about when it swaps. T436 chose "an
  item on the cursor means SELL, otherwise BUY" because the list holds the
  MERCHANT's stock and the cursor is the one player-owned item the window can
  name. The oracle's actual rule is unmeasured. Closes by watching the official
  client at a vendor with and without a held item (the T342 rig). Found by
  T436.
  **MEASURED (UIGAP-ORACLE 2026-10-05, review/UIGAP-ORACLE/152/merchant.tsv)
  and IMPLEMENTED by B442b (2026-10-06):** the button is the side of the LAST
  selection (a merchant row -> Buy, an inventory item -> Sell; Buy on open),
  and the cursor is never used -- an inventory click with a merchant open
  SELECTS the item (yellow frame) and Sell sends `OP_ShopPlayerSell` from its
  slot; Sell on a stack opens the QuantityWnd. T436's cursor default is gone.
  Owed: the eqstr 4020 / 12026 price tells and the sell offer in
  `MW_SelectedPriceLabel`; the inventory window opening beside the merchant
  (fable's merchant shares centerPopup with the inventory popup, so today a
  sale is selected from a bag window or with the popup reopened).

- ~~**B444** (B) `src/client/core/everquest.cpp` (the zone-in inventory snapshot,
  `for (int16_t slot = 0; slot < 30; ++slot)`) -- **the zone-in snapshot
  publishes slots 0..29 only, so a cursor item and the bank's contents are
  invisible until something else moves them.** T436 gave slot 30 and
  2000..2007 consumers; the generic ItemPacket choke point in
  `zone_dispatch.cpp` publishes them when the server sends them (bank-window
  population is one of the two cases its own comment names), but the
  OnGameStateComplete snapshot loop stops at 30. A character who zones in
  holding something on the cursor therefore draws no attachment until the next
  MoveItem. Closes when the loop covers `CURSOR_SLOT` and the bank range.
  Unverified live -- T436 held no lock. Found by T436.~~
  **STRUCK T439 (base `d1458444`).** The loop iterates
  `eqt::inventory::ZoneInSnapshotSlots()` -- a named table in
  `inventory_constants.h` rather than a second literal range, so the loop and the
  census cannot drift -- covering 0..29, `CURSOR_SLOT` (30) and
  `BANK_BEGIN`..`+7`. **EIGHT bank slots, not `BANK_COUNT`'s sixteen**
  (`BANK_SNAPSHOT_COUNT`): the era's `EQUI_BankWnd.xml` declares
  `BW_BankSlot0..7` and 2008..2015 have no consumer at all; `BANK_COUNT` is left
  at 16 because that is the WIRE's range. `SyncInventorySlotToGameState()` was
  already correct for both new families (a no-op for the cursor,
  `setBankOccupied` for the bank), so no game-state mirror changed. Pinned by
  `B444Census.TheZoneInSnapshotCoversTheCursorAndTheBank` and two behaviour cases
  that drive the REAL appliers with every published slot and require each to be
  claimed by exactly one consumer family -- so a slot published with nowhere to
  land is now a red too. ~~**LIVE PROOF IS OWED** (no lock; T428 holds it).~~
  **LIVE-PROVEN T449 (this commit) FOR THE CURSOR HALF ONLY**: an item on the
  cursor is still drawn after a zone change
  (`review/T449/fable/T449_t439_01_cursor_survives_zone_in_zoom.png`). **The
  bank-slot half is still unproven and cannot be reached from the UI at all --
  see B481**: nothing in fable's inventory UI picks an item up to the cursor,
  so there is no click path that puts one into a bank slot. The bank COINS were
  proven instead.
---

## Added 2026-09-07 by T428 (the night base, B280; the fourth vision level, B060)

- **B445** (A) `src/client/graphics/renderer/irrlicht_renderer.cpp` → issue #27
  `IrrlichtRenderer::updateTimeOfDay` -- **the scene ambient is uploaded to the
  zone shader TWICE, and the second copy is the first one normalised to its own
  largest channel**, so the ambient's chromaticity is applied squared. The two
  lines are `zoneShader_->setAmbientColor(r, g, b)` and, four lines later,
  `setTintColor(r/maxAmb, g/maxAmb, b/maxAmb)`; the vertex programs then compute
  `tintOnly = baseLighting * uTintColor` where `baseLighting` already carries
  `uAmbientColor`. MEASURED T428, live, at the T405 spot: raising ONLY the night
  ambient's R from 0.08 to 0.24 dropped the frame's G by **9.4%** and its B by
  **16%**; raising only G dropped R by **17%**; raising only B dropped R and G by
  **25%**. No physical ambient does that. **This is the root cause of "the three
  channels are COUPLED", which T405 found, pinned in prose and could not
  explain** (`include/client/graphics/vision_ambient.h`, and T405 Review §3's
  "the fit has to be a 3-D Newton step with the measured cross-terms"). It is
  also why every night measurement collapses onto one curve per channel in
  `x = A^2 / max(A)` -- `review/T428/logs/points_nolamp.json` is the eighteen
  points that show it. **T428 did NOT change it**: the night base and all three
  vision floors were solved WITH the double application in place, so removing it
  invalidates every one of them and is a unit of its own. The sun term is tinted
  too, so this is not a pure re-scale. Closes when the tint is either given a
  measured job of its own or deleted, and the affected constants are re-solved
  live in the same session. Prov: T428 Review §1d, §2; B280; B203.

- **B446** (A) `src/client/graphics/renderer/irrlicht_renderer.cpp` + → issue #28
  `src/client/graphics/sim_animation.cpp` -- **fable's zone LAMP is worth far
  more, relative to its ambient, than the official client's is, and the night
  ambient cannot absorb it.** T428 solved the night base at the lamp-free spot
  and then measured the committed value at T367's lamp spot (33, 375, 8) h160,
  same crop, same session protocol: fable's day/night there reads
  **R 2.043 / G 2.539 / B 5.702** against the oracle's **1.521 / 1.485 / 4.317**
  -- **+34% / +71% / +32%**. That is a big improvement on the pre-T428
  **+82% / +256% / +43%** (the ambient was most of it), but it is not a match,
  and the residual is lamp-shaped: at the lamp spot the non-ambient term is
  **half** the frame (fitted P_R = 0.0906 against a wanted 0.1909), so no ambient
  triple satisfies both spots at once. **This is the term T408's grey-only
  uniformity test could not see**: ×2.27 vs ×2.09 in grey is uniform, but per
  channel the two spots want different things. Closes when the zone point-light
  path (intensity, attenuation, or the vision modifiers on it -- B067) is solved
  against the oracle at the lamp spot with the night base held at T428's value.
  Prov: T428 Review §0, §2; B280; B067; T408 Review §4.

- **B447** (B, unmeasured decision) `include/client/graphics/vision_ambient.h` → issue #153
  `kUltravisionAmbientGain` x `kUltravisionTint` -- **Ultravision now lifts
  DAYLIGHT by ~2.5%, and whether the official client does that is unmeasured.**
  Reaching the oracle's x2.7995 R at night needs a floor of
  `(0.51320, 0.51411, 0.49219)`, which is 2.6% and 2.8% ABOVE the day ambient
  (0.5) on R and G -- so the "a floor does nothing at noon" property T361 chose
  the mechanism for no longer holds exactly. Measured live on the committed
  constants (T428 conf2): daylight under Ultravision reads **R x1.0239, G
  x1.0259, B x0.9414**. Infravision is still invisible by day on every channel.
  The oracle side is NOT settled: T428's o3 day block read x1.028 / x1.022 /
  x1.030, but that session's own unbuffed day control drifted **30-48%** over the
  same four minutes, so the signal is inside the drift. Closes when one oracle
  session measures day-with-Ultravision against a day control that holds (re-pin
  `#set time 11 0` immediately before EVERY day window, the way T405 does at
  night), and fable's floor is either accepted or the mechanism changed. Pinned
  meanwhile by
  `VisionAmbientTest.DaylightIsUntouchedByInfravisionAndBarelyMovedByUltravision`.
  Prov: T428 Review §3b.

- **B448** (A) `src/client/graphics/player/renderer_camera.cpp` → issue #29
  `PlayerCameraComponent::setPlayerRace` -- **a server-side race change on the
  player's OWN character never reaches the vision system.** `setPlayerRace()` is
  called from `IrrlichtRenderer::registerEntity()` (T348, B056), i.e. when an
  entity is REGISTERED; an illusion/`#race` on a character already in the world
  produces no `Player race N -> base vision:` line at all. MEASURED T428 twice:
  `#race 330` was submitted and confirmed in the chat log in both confirmation
  sessions (`Chat submit: '/say #race 330'` in
  `review/T428/logs/fable_session_conf{1,2}.log.gz`) and the vision base never
  moved -- the frame ratio across the window is 0.95-0.99, i.e. drift only. Two
  consequences: an illusion spell that changes race does not change what the
  player can see in the dark, and **T428's Froglok floor could not be confirmed
  on fable's side** (it is solved from the same measured curve as the other two
  and bracketed by them, but the live pair is OWED). Closes when a race change
  on self reaches `setPlayerRace()` and a live session shows the fourth level's
  ratio against the oracle's x2.6803 / x1.9478 / x2.1753. Prov: T428 Review §4;
  B060; B056.

- (T428 also boarded the DocCitations red at BOARD.md:6877 as its B434; the orchestrator had already fixed it on fable at 6aadff59, so that entry is not carried.)

---

### Added by T439 (2026-09-07)

- **B449** (B) `src/client/commands/commands_spells.cpp` (`/volume`) -- → issue #154
  **MASTER volume is the one volume fable can change that nothing publishes
  back, because the Options window has no slider for it.** T439 closed B432 by
  making the music and effects volumes a round trip (an absolute intent out,
  `GameEventType::VolumesChanged` back). `/volume` was deliberately left out:
  `EQUI_OptionsWindow.xml`'s General page declares `OGP_MusicVolumeSlider` and
  `OGP_SoundVolumeSlider` and no master control at all, so there is nothing to
  keep in sync and adding one would be a fable invention of the class T363
  forbids. It is boarded rather than forgotten because the ASYMMETRY is a
  trap: a future master-volume control or readout must extend
  `VolumesChangedData` and `BridgeComponent::PublishVolumesChanged()` rather
  than inventing a second mechanism beside them. Zero user-visible effect
  today. Found by T439.

- ~~**B450** (B) `include/client/graphics/ui/static_panels.h` (`enum class
  PopupType`) -- **five of the seven remaining enumerators are named by
  nothing in the tree.** T439 deleted `Options` (B434) and re-took the census
  that entry got wrong: a comment-blind grep over `src/`, `include/` and
  `tests/` finds `None`, `Inventory` and `Vendor` and nothing else.
  `Spellbook`, `Bank`, `Loot`, `Trade` and `Skills` are named by no production
  code and no test. This is NOT the B434 defect repeated -- each of those five
  windows exists and is drawn; they simply track their own visibility on their
  own panel state (`BankPanelState::isOpen`, `LootPanelState`,
  `SkillsPopupState`, ...) rather than through this enum, exactly as T436's
  bank and merchant windows do. So the open question is whether `PopupType`
  should shrink to `{None, Inventory, Vendor}` or whether centre-popup mutual
  exclusion should route through it after all -- the second reading is the
  alternative B434 offered, and T434 and T436 both declined it in practice.
  One decision, then a small deletion. Found by T439.~~ **CLOSED by T442** (`refactor/T442_dead_renderer_members.md`). The reading the board offered second is the one T434 and T436 had already taken in practice, so `PopupType` SHRANK to `{None, Inventory, Vendor}` -- the three the tree names -- and the five that named nothing went. Each of those windows keeps drawing; each tracks its own visibility on its own panel state, as it already did. Pinned by `tests/test_dead_renderer_members.cpp` (`B450Census`, 3 cases, 1 red pre-fix), and `B434Census.PopupTypeHasNoOptionsEnumerator` learned the new set.
---

## Added 2026-09-07 by T440

- ~~**B451** (B) `src/client/graphics/player/renderer_player_collision.cpp`
  `PlayerCameraComponent::checkCollisionIrrlicht()` -- **the surviving twin of
  the stub T440 deleted.** It is a `LOG_WARN(... "CALLED -- old meta selector
  path!")` plus `return false` ("nothing blocks you"), reached from five
  `useBspFiltered ? checkCollisionBspFiltered(...) : checkCollisionIrrlicht(...)`
  ternaries in `renderer_player_physics.cpp` (the horizontal check, the two
  swim ceiling checks and the two wall slides). `useBspFiltered` is
  `ctx_.zoneBspTree && !ctx_.regionWorldTriangles.empty()` evaluated inside
  `resolveIrrlichtCollision()`, which the caller enters only when
  `regionWorldTriangles` is already non-empty -- so, exactly as with
  `findGroundZIrrlicht()`, the false arm is unreachable. T440 deleted the
  ground half because the plan named it and left this one alone rather than
  widening scope. Closes the same way: delete the function, delete the five
  ternaries, learn the G06 golden in place. Found by T440.~~ **CLOSED by T442** (`refactor/T442_dead_renderer_members.md`), exactly as boarded: the function, the five ternaries and the now-dead `useBspFiltered` selector (its two function parameters included) are gone, and `checkCollisionBspFiltered()` is called unconditionally at all five sites. G06 learned the edit IN PLACE -- five collapses plus the selector declaration, 463 -> 456, zero insertions, zero reorders. Pinned by `tests/test_dead_renderer_members.cpp` (`B451Census`, 3 cases, 2 red pre-fix). <!-- RETIRED-OK: the entry names the symbol it retired, which is the point of a struck entry -->

- ~~**B452** (B) `include/client/graphics/irrlicht_renderer.h`
  `IrrlichtRenderer::fallbackMeshNode_` (bound into `RendererContext` as
  `fallbackMeshNode`, `renderer_context.h:163`) -- **a member nothing ever
  creates.** It is `= nullptr` at its declaration and the only two
  assignments in the tree are `= nullptr`
  (`src/client/graphics/zone_lifecycle/renderer_zone_lifecycle.cpp:180`,
  `src/client/graphics/zone_load/renderer_zone_load.cpp:426`); no site anywhere assigns a scene
  node to it. Six other sites nonetheless test it, remove it, drop it, swap its
  material to the debug material, re-parent it and count it in the scene
  breakdown (`src/client/graphics/zone_lifecycle/renderer_zone_draw.cpp:321,599,720,841`,
  `src/client/graphics/diagnostics/diagnostics_component.cpp:214`,
  `src/client/graphics/diagnostics/diagnostics_debug.cpp:282,359,458,477`). Grepped over `src/`,
  `include/`, `tests/`, `tools/`, `benchmarks/` and `docker/`: no creator. The
  "single zone mesh" fallback that its name refers to is really
  `ZoneLifecycleComponent::createZoneMesh()` writing `ctx_.zoneMeshNode`.
  Closes by deleting the member and its dead handling, or by finding the
  creator that was lost and restoring it -- decide by reading
  `createZoneMesh()`. Found by T440 while confirming the non-BSP zone path.~~ **CLOSED by T442** (`refactor/T442_dead_renderer_members.md`) by DELETION, not restoration: `createZoneMesh()` reads and there is no creator to bring back -- it is the real single-zone-mesh fallback (`ctx_.zoneMeshNode = ctx_.smgr->addOctreeSceneNode(...)`, `src/client/graphics/zone_lifecycle/renderer_zone_draw.cpp:183`) and the no-BSP/PVS install path calls it. The member, its `RendererContext` binding, the `renderer_init.cpp` ctor argument and all eleven handling sites are gone, along with `drawRegionMesh()`'s `SIZE_MAX` sentinel branch, whose only caller was the fallback draw. Pinned by `tests/test_dead_renderer_members.cpp` (`B452Census`, 3 cases, 2 red pre-fix). <!-- RETIRED-OK: the entry names the symbol it retired, which is the point of a struck entry -->
---

## Added 2026-09-07 by T442

- ~~**B453** (B) `src/client/graphics/diagnostics/diagnostics_component.cpp`
  (`DiagnosticsComponent`'s scene breakdown, the `otherNodes` census around
  line 213) -- **the breakdown never subtracts `ctx_.zoneMeshNode`, so in a
  no-BSP zone the one real zone mesh is reported as an UNACCOUNTED node.**
  Every other zone-side node the scene graph can hold is subtracted and
  inserted into `knownNodes` -- the collision node, the placeholder, the
  camera, the two lights, the sky and weather sets, the entities -- and
  `zoneMeshNode` is not, although `createZoneMesh()` adds it to the root
  scene node (`addOctreeSceneNode(mesh, nullptr, -1, 256)`,
  `src/client/graphics/zone_lifecycle/renderer_zone_draw.cpp:183`). `/dumpscene` in Plane of Sky
  (T440 measured it as the ONE playable zone with no BSP tree and no regions)
  therefore prints `unaccounted=1` and enumerates the zone mesh itself as an
  unidentified `octree` node. Diagnostic only -- nothing renders differently,
  and the enumeration below the log line still names the node -- but the
  count is the thing the line exists to make trustworthy. Found by T442 while
  deleting the dead sibling that WAS subtracted there (the struck B452): the
  subtraction guarded on `getParent()`, so it never fired either, and the
  breakdown has been blind to the zone mesh for as long as both have existed.
  Closes by subtracting `zoneMeshNode` the way the collision node is
  subtracted, and by saying what `/dumpscene` should read in a BSP zone
  (where the node is null and the regions carry the geometry).~~ **CLOSED by T443** (`refactor/T443_guild_list_esc_dumpscene.md`):
  `zoneMeshNode` is subtracted and inserted into the known set exactly the way
  the collision node is. The eleven ad-hoc `otherNodes--; knownNodes.insert(x);`
  pairs became ONE call to `computeSceneNodeCensus()`
  (`include/client/graphics/diagnostics/scene_node_census.h`), which populates
  both halves together, so a category that is subtracted but not suppressed in
  the enumeration is no longer expressible. The arithmetic is unchanged --
  entity subtrees whole, two nodes per in-graph door, sky and weather by their
  own counts and collectors, everything else one node. **What `/dumpscene`
  should read in a BSP zone** (the question the entry left open): `unaccounted=0`
  there too, and by the same rule -- `zoneMeshNode` is null in a BSP zone and a
  null entry is a no-op, while the geometry is in `regionMeshNodes`, which was
  always subtracted. That is why the count read 0 in every BSP zone and hid this
  for as long as the member has existed. Pinned by
  `tests/test_scene_node_census.cpp` (10 cases, 1 red pre-fix), which builds a
  scene on an EDT_NULL device and asserts BOTH sides: the zone mesh accounted
  (`unaccounted=0`) and the pre-fix set (`unaccounted=1`), so a revert is a red
  test rather than a wrong number in a log nobody reads.


---

### Added by T441 (2026-09-07)
  `createZoneMesh()`. Found by T440 while confirming the non-BSP zone path.
- **B455** (C) `data/textures/spell_sprite_atlas.bmp` / `spela2.tga` -- → issue #30
  **Whirling Wind (253) draws blue where the oracle measured near-neutral,
  and no colour rule fable can apply will fix it, because the art it is
  required to draw has no red in it.** T441 closed B398 by taking the colour
  from the sprite the official client names (spells.eff record 35, every slot
  `GENA10_SPRITE` -> `spela2.tga`, T389's measured 0-based rule) instead of
  from the resist type. That moved 253 from `0.299/0.248/0.453` (T422, live,
  observing client) to `0.000/0.260/0.740` (the untinted art, normalised
  alpha-weighted over all sixteen frames of the cells it actually samples) --
  strictly closer to the oracle in blue, but still blue-dominant where T389
  measured `0.258/0.309/0.214`. **`spela2.tga` is zero-red**, so R = 0.258
  cannot be produced by ANY multiplication of it. Three explanations are open
  and one measurement separates them: (a) T389's reading is noise -- 253 is
  the faintest of the four effects, 0.64 % of frame area over 1,829 pixels,
  and a background-median residual at that level carries the caster's own
  animation; (b) the official client draws 253's persistent effect from
  somewhere other than the Target stage -- field 193 `nimbus_effect` is 0 for
  it, but the "continuous" stage T422 observed was never traced to a record;
  (c) the 0-based rule picks the wrong file for `GENA10` specifically.
  **Class C** because it needs a live re-measurement of one spell against the
  oracle before any code moves, and inventing a colour to close the gap is
  precisely the guess Will's ruling forbids. Pinned meanwhile by
  `SpellColourFromSpriteArt.WhirlingWindDrawsItsOwnArtUntintedAndIsStillBlue`,
  which fails if the residual ever closes. Found by T441.

- **B456** (B) `src/client/graphics/spell_visual_fx.cpp` → issue #155
  `createCastGlow()` / `createSpellComplete()` / `createProjectile()` --
  **three stages now draw the official client's sprite art, and none of the
  three has been seen on screen.** Until T441 only the Target stage's names
  reached an emitter (T389); the Source and Middle stages were resolved,
  their PRESENCE was used to gate the effect, and their sprite names were
  then discarded, so the cast glow, the spellComplete burst and the
  projectile trail drew procedural tiles tinted by resist type. T441 routed
  all three, which is the same rule B398 closed on -- but T422's live
  measurement covered only the landing: it used `#castspell` on both sides
  (no `OP_BeginCast`, so no cast glow at all) and self-casts (no travel
  stage). So the pixels these three now produce are UNMEASURED against the
  oracle, and `spellComplete` in particular changed from white sparkles to
  whatever the Source stage names -- for 93 Burst of Flame,
  `spelc2`/`spelc4`/`spelc2`. Closes with a live gem-cast A/B on a character
  that can cast the measured spells, capturing the cast glow and the travel
  trail alongside the landing. Note that `createSpellComplete()` deliberately
  did NOT gain a Source-stage suppression gate the way `createCastGlow()`
  has one: adding it would silently delete the burst for every spell whose
  record has an empty Source block, which is a behaviour change no
  measurement supports. Found by T441.

- ~~**B457** (P) `include/client/graphics/vision_ambient.h` lines 35, 177 and 195
  -- **three comments in the vision header still name `0.08/0.08/0.15` as the
  night ambient, and T428 moved it.** Line 148 of the SAME file defines
  `kNightAmbient = {0.26244f, 0.31147f, 0.24003f}`, solved live against the
  oracle's per-channel day/night ratio; the three sentences above and below it
  describe the base a floor is imposed over, so anyone solving or sanity-checking
  a vision floor against them solves against the pre-T428 base. That is exactly
  the mistake T428 found in T405's floors (solved against the old base, off by
  2-3x), and the header is the one place a future unit will read. Doc/comment
  only -- `kNightAmbient` is what the code uses, so no behaviour is wrong today.
  Not fixed in T444 because that unit's scope forbids code changes outside the
  checker tables. **Also worth noting as the reason it survived: neither
  `DocClaims` nor `DocCounts` nor T444's new coverage table scans C++ comments
  -- every doc net in the tree stops at `*.md`.** Found by T444.
  **CLOSED T445.** All three comments name `kNightAmbient` / `kDayAmbient`
  instead of a triple, and the file now carries a DO-NOT-WRITE-THE-LITERAL
  banner saying why. The net T444 said did not exist is
  `scripts/refactor/check_comment_claims.py`, registered as the ctest test
  **`CommentClaims`** (label `hygiene`): the C++-comment analogue of
  `DocClaims`, a table of (file, retired pattern) rows matched against comment
  text only -- a hand-rolled scanner, so a string literal cannot open a comment
  and the constant's own definition is never a hit -- with the same
  `RETIRED-OK` line exemption and the same "a row naming a missing file is an
  error" rule. Pre-fix it failed on exactly lines 35, 177 and 195; post-fix it
  is green. **A finding while rewriting them:** the line-33 claim that the day
  ambient is "already above every floor here" is WRONG -- Ultravision's and
  Froglok's floors (gain x tint) exceed 0.5 on R and G, so those two buffs lift
  a noon frame by ~0.014. Corrected in place and boarded as **B459**.

- ~~**B458** (P) `refactor/BOARD.md`, header block -- **the board's own "Tree
  confirmed at" line still reads `3808ebe9` / 2026-09-03, K00's tip.** Roughly
  sixty units have merged since, the item bodies below are confirmed against
  much later tips (T441's B455/B456 cite work from 2026-09-07), and the header
  says in its own words that "items are confirmed against the tree at the tip
  above". A new agent is told to start here, and the first fact it reads is
  four days and sixty units stale. The suite line already solves the general
  case correctly -- it refuses to state a number and names the measuring
  command instead -- and the same treatment fits here: either restamp the line
  every time the board is edited, or replace the hash with
  `git -C <slot> log --oneline -1`. `DocCounts`'s NAMED_COMMAND table is the
  existing mechanism for "do not pin this, name the command". Found by T444.
  **CLOSED T445, by the second option: the hash is GONE, replaced by the
  measuring command.** The header line now reads like the suite line two lines
  below it -- "DO NOT QUOTE A HASH FROM THIS LINE", with
  `git -C <slot> log --oneline -1` named -- and the Date line says built-then-
  amended rather than pretending to one stamp. The "items are confirmed against
  the tip above" bullet was rewritten too, because with no tip above it it had
  nothing to point at: each item names the unit that confirmed it, and that
  unit's merge is its tip. Ratcheted in `check_doc_counts.py`: a NAMED_COMMAND
  row for the command, and a FORBIDDEN row whose regex fails on
  `**Tree confirmed at:** \`<hash>\``. Pre-fix `DocCounts` failed on
  `refactor/BOARD.md:3`; post-fix it is green. Restamping was rejected: the
  restamp is exactly what sixty units did not do.

---

## Added 2026-09-07 by T443

- **B459** (C) `src/client/graphics/entity_mesh_build.cpp` (both name-tag → issue #31
  creation sites), `include/client/state/entity_manager.h` `guildId` --
  **fable now HAS a guild-id -> name lookup (the struck B418) and still draws
  no guild anywhere, because the one surface the oracle uses has never been
  measured.** T443 searched for a display site and eliminated every candidate
  but one: the /who guild fragment is a wire STRING eqemu already fills
  (`/mnt/projects/akk-stack/code/world/clientlist.cpp:824` and
  `/mnt/projects/akk-stack/code/zone/entity.cpp:4970` both `sprintf` `"<%s>"` from `GetGuildName()`), so
  the /who path needs no lookup and `who_response.h` never reads `Entity` at
  all; `EQUI_TargetWindow.xml` is a name label, an HP gauge and a `%` label
  with no guild element (T429 Leg B); `OP_GuildStatus`'s reply is composed
  server-side with the name already in it
  (`/mnt/projects/akk-stack/code/zone/client_packet.cpp:8728`). That leaves the OVERHEAD NAME TAG, and
  fable draws it as ONE line (`addTextSceneNode` with the built-in font, which
  has no newline handling -- a second line needs a second node). **No oracle
  capture of a guild line on the name tag exists**: the whole `review/` tree
  and every `refactor/*.md` were searched and none records the tag's content,
  the tag has no `EQUI_*.xml` definition because the 3D renderer draws it
  rather than the SIDL window system, and `eqstr_us.txt` has no bare
  `<%1>` row (only 1083/1084, the guild-war sentences). Adding it would be
  inventing a format, which T363 forbids. **Closes when the official client is
  captured with a GUILDED character in view and the tag's format is read off
  the pixels** -- note `review/T132/README.md` and `review/T100/README.md:43`
  record that the test characters have no guild, so the capture needs a guild
  created by GM command first. Class C because "should fable draw a guild
  line, and where" is a look decision once the format is known. Found by T443.

- **B460** (B) `refactor/TESTING_CHARTER.md` / the oracle capture set -- → issue #156
  **what the official client does when ESC is pressed with a merchant, a bank
  or a tradeskill container open has never been measured.** T436 gave the
  merchant and bank an ESC arm and T443 gave the container one, all three in
  the T290 shape (push the close intent, wait for the server's Closed event),
  and that shape is right for the DESYNC reason regardless. But whether the
  oracle closes those windows on ESC AT ALL is a separate question, and
  T443's plan pointed at T342's captures for the answer: T342 recorded
  merchant open, select, buy and sell (`review/T342/T342_events.tsv`) and
  contains no ESC event of any kind. So fable's ESC behaviour on all four
  server-owned windows is reasoned, not measured. Closes when a lock unit
  presses ESC on each with the official client running and records what
  happens. Cheap and scriptable -- we control the server and the vendor.
  Found by T443.
  **UIGAP-small (B460 partial):** the ORDER is now the measured one (most
  recently opened first, one per press; `esc_window_stack.h`,
  `tests/test_esc_window_stack.cpp`). Still unmeasured, so this stays open:
  ESC on the merchant/bank/trainer specifically, and whether a
  window-closing press also clears the target (fable: it does not).

---

## Added 2026-09-07 by T438 (the oracle door measurements)

- **B461** (A, live proof owed) **the lift ride is implemented and unit-proven → issue #32
  but has never carried a player in a live fable session.**
  `PlayerCameraComponent::carryPlayerOnLifts()`
  (`src/client/graphics/player/renderer_player_lift.cpp`) runs once per frame
  after `syncDoorCollision()` and is covered by `LiftRideTest` +
  `DoorCollisionRay.AnAnimatingLiftStillCollides`, and the ORACLE side is
  measured (the official client carries the rider exactly -- z 5.81 -> 58.65 in
  step with the platform, `review/T438/logs/trace_lift69_ride.tsv`). Two live
  attempts on the gfaydark lift 69 did not carry, and the reason is not the
  carry code: after a `#goto` onto the platform the player FLOATS at the
  commanded z instead of settling onto the surface, because
  `resolveMovementCollision()` is gated on `isMoving || isJumpMoving ||
  isSwimMoving` (T435 Leg C). `/loc` read exactly the goto's `5.0` for the whole
  ride window, so the ride probe correctly declined. **Work:** repeat with the
  player WALKING onto the platform (hold `w` from beside it) rather than
  teleporting, and assert the trace's z climbs at 8.67 units/s. One real bug was
  already found this way and fixed -- the probe keyed its BSP region on
  `ctx_.currentPvsRegion`, the CAMERA's PVS region, where
  `findGroundZBspFiltered()` derives it from the PLAYER's own point, and the
  lift sits in region 1883. Found by T438.

- **B462** (B, unmeasured decision) **the opentype-100 spin rate is assumed, → issue #157
  not measured.** T438 measured a 105 spinner (steamfont `WMBLADE`) at a full
  360 degrees in 4.267 s = **84.4 deg/s**, and `DoorMotion::kSpinDegreesPerSecond`
  applies it to opentypes 100 and 105 alike because fable already used one
  constant for both and nothing suggests they differ. Nobody has watched a
  100 spin. Era 100s are near at hand -- steamfont `SHAFT` 1/3/4 sit at ground
  level a few hundred units from the windmills, and there is a `QEYLAMP` in
  qeynos -- and the method is settled (30 fps x11grab, autocorrelate the
  object's horizontal centroid; the half-period shows as a strong NEGATIVE peak,
  which is the trap that produced fable's original 42.35). Found by T438.

- **B463** (A, live defect) **fable's jump is 25 units high, and the number is a → issue #33
  debug value.** `MovementComponent::UpdateJump()`
  (`src/client/movement/movement_state.cpp:545`) reads
  `const float jump_height = 25.0f;  // Maximum height of jump (increased for
  visibility)`, over a fixed 1,000 ms parabola. A real EverQuest jump is a few
  units; 25 is taller than a Kelethin storey, and T438's Leg C trace watched the
  player arc from 14.49 to 39.49 and back four times in one session. The
  landing bug that made this dangerous is fixed (the struck B424), so this is now
  a look/feel defect rather than a position corruption -- but it is unmeasured
  in both height and duration and both should come off the oracle: the rig that
  measures it is the same one T438 used (a 4 Hz server-side position trace via a
  Perl quest timer while the official client jumps). Found by T438 Leg C.

- ~~**B464** (B)~~ **CLOSED 2026-09-07 (T448).** The door-spawn debug dump
  named the loop INDEX where the door id belongs, so a two-door packet whose
  doors are 69 and 70 printed "Door 0" and "Door 1" -- which is what cost T438 a
  wrong turn (the line looked like it showed `door_param == 0` for the gfaydark
  lift, when the record it was naming was a different door's; the server does
  send 68, `/mnt/projects/akk-stack/code/zone/entity.cpp:988`).
  **The board's second claim was WRONG and T448 measured it:** `data` is
  `p.Data() + offset`, i.e. already that door's own 80-byte record, so the eight
  bytes were never taken from the packet base and no line ever printed the first
  door's bytes. The line now formats through
  `eqt::entities::FormatDoorSpawnDump(loopIndex, record)`, which prints the
  record's own id and keeps the loop index as an explicit "packet slot".
  `tests/test_entities_component.cpp` pins both halves
  (`DoorSpawnDumpTest.EachLineNamesItsOwnDoorIdNotTheLoopIndex` -- RED before
  the fix -- and `.EachLinePrintsItsOwnRecordsBytesNotTheFirstDoors`, which was
  already green and is the pin against a "fix" that regresses it). Found by
  T438, closed by T448.

---

### Added by T445 (2026-09-07)

- **B465** (C) `include/client/graphics/vision_ambient.h` -- **Ultravision and → issue #34
  Froglok vision are NOT inert in daylight, and nobody has checked whether the
  oracle's are.** The floor a vision type imposes is gain x tint, and for
  Ultravision that is (0.5132, 0.5141, 0.4922) and for Froglok (0.5059,
  0.5134, 0.4677) against `kDayAmbient` 0.5 on every channel. R and G are
  ABOVE it, so both buffs raise a noon frame by ~0.014 on two channels, ~2.6%
  on R. Infravision (0.4481, 0.4320, 0.3639) is entirely below 0.5 and is
  genuinely inert. The header asserted the opposite for years ("fable's day
  ambient is 0.5 on every channel, already above every floor here"); T445
  corrected the sentence but did not touch a constant, because the file's own
  rule is that a constant moves only when a live session says so. This is a
  DESIGN question, not a defect: T361's measured mechanism is a floor, and a
  floor that happens to clear the day ambient by 1.4% may be correct. Closes
  when a live A/B at 11:00 with and without Ultravision says whether the
  oracle lifts a daylight frame. Found by T445.

- **B466** (B) `tools/hwcaps/gles2_zone_benchmark.cpp`, → issue #158
  `src/client/graphics/detail/` -- **the detail (foliage) system has no
  coverage in any performance lane, so its frame cost cannot be measured.**
  The visual gate is the only automated frame-cost instrument in the tree and
  it runs `gles2_zone_benchmark`, which names neither `DetailManager` nor
  detail anywhere -- it never constructs a chunk. T445 proved this rather than
  inferring it: doubling the resident chunk grid from 25 to 49 left `draws`,
  `polys` and `regions_drawn` byte-identical in all three zones and all three
  frames, and moved no golden pixel. So B336's wiring shipped with its cost
  unmeasured, and the same is true of every future detail change: density, the
  wind controller, foliage disturbance and footprints are all invisible to CI.
  There is not even a counter -- the gate CSV has no detail column, though
  `DetailManager::getActiveChunkCount()` and `getTotalPlacementCount()` exist
  and are public. Closes when a lane exercises detail with a metric that
  would move: a detail column in the gate CSV plus a benchmark pose that
  builds chunks, or a separate detail benchmark. Found by T445.
## Added 2026-09-07 by T450 (the third derived asset gets a recipe and a pin)

- ~~**B467** (B) `tools/willeq_db_builder.cpp` `writeMetadata()`,
  `data/willeq.db` -- **the shipped database was the one derived asset with no
  recipe script, no pinned hash and no test, and it could not have had one:
  the builder stamped `metadata.build_date = __DATE__ " " __TIME__`, so
  RECOMPILING the tool changed the file with byte-identical inputs.** Measured
  pre-fix in T450: two rebuilds of `willeq_db_builder` from an untouched tree
  produced `c5bc7e29...` and `cee904e8...` -- same 3,788,800 bytes, one
  differing metadata row. That is why T447's split proof had to normalise a row
  instead of comparing sha256s (T447 Review, flagged there and not boarded),
  and why `data/textures.sha256` and `data/willeq.eqe1.sha256` had no sibling.
  Found by T447, opened and closed by T450.
  **CLOSED T450.** The row is now `metadata.input_digest`: FNV-1a/64 over each
  input's ROLE plus its contents, never a path, so every worktree and every
  staged install agree. Nothing read the old stamp -- the client reads only
  `metadata.version` (`src/client/willeq_database.cpp:101`) -- so no consumer
  moved and `kWilleqDbExpectedVersion` did NOT change (still 7). The input set
  is now named once in `main()` and both the loaders and the digest read that
  table, so a new input cannot be loaded without moving the digest.
  `scripts/build_willeq_db.sh` is the recipe (`--check` rebuilds to a temp path
  and compares; the install is an atomic `mv` in the destination directory),
  `data/willeq.db.sha256` is the pin, and **`DbReproducible`** (ctest, label
  `hygiene`, ~1.5 s, no pack and no GPU) is the ratchet. Post-fix the two
  rebuilds are byte-identical (`14f09de8...`). `data/willeq.db` was rebuilt
  once and committed with its pin; **exactly one row differs from the old
  file** -- `metadata.build_date` -> `metadata.input_digest` -- with the
  32,414 data rows and the file size unchanged.

~~- **B468** (A, unconfirmed live) `src/client/graphics/eq/model_loading.cpp`
  `RaceModelLoader::getArmorTexture()`, `src/client/willeq_database.cpp`
  `getAssetLocation()` / `getAssetsInArchive()` -- **two SQL queries run against
  `s3d_assets`, a table the shipped `data/willeq.db` has never contained, and
  one of them is on a reachable armor-texture path.** Measured in T450:
  `sqlite3 data/willeq.db .tables` lists eleven tables plus `metadata` and
  `sqlite_stat1`, and `s3d_assets` is not among them. It is only ever created
  by `loadS3dIndex()`, which runs only when `willeq_db_builder` is given
  `--s3d-index` -- and **nothing in the tree passes it**: no script, no
  CMake rule, and no `s3d_index.json` is checked in (`tools/s3d_index_builder
  --scan` can produce one by hand, which is the whole of its provenance).
  `getAssetLocation()` is reached from `getArmorTexture()`, which
  `src/client/graphics/eq/mesh_building.cpp:210` and `:273` call while building
  a character mesh, so every on-demand armor-texture lookup returns `nullptr`
  from a `SELECT` against a missing table rather than from a miss. What is NOT
  measured is whether a player ever sees a consequence: armor textures may be
  coming from the EQE1 path in practice, which would make this dead code with a
  misleading failure mode rather than a live defect. Closes when a live session
  says which -- then either the input joins the recipe
  (`scripts/build_willeq_db.sh`, one more table in the pin) or the three
  functions and the loader are deleted. Do NOT add the table to the shipped
  database "to be safe": it would change the pinned bytes for a consumer nobody
  has shown is reached. Found by T450.~~ **STRUCK by T452 (this commit): CONFIRMED A LIVE DEFECT, and fixed.**

  What was unmeasured -- whether a player ever saw a consequence -- is now
  measured, offline, against the client tree. A character body texture is
  named `{race}{part}{variant:02d}{page:02d}.bmp`, and the variant partitions
  the archives: **00-04 are in `global_chr.s3d`** (already merged into
  `globalTextures_`), **10-16 are robes**, which `mesh_building.cpp` rewrites
  to `clk<nn><page>.bmp` (also in `global_chr.s3d`, 42 pages), and **17-23
  live ONLY in `global17_amr.s3d` .. `global23_amr.s3d`** -- 1,784 textures
  that NOTHING ELSE in the client opens. `getArmorTexture()` was the sole
  route to them, so `isTextureAvailable()` asked for e.g. `hufch1701.bmp`, got
  `nullptr`, declined the variant override, and the whole Kunark/Velious armor
  tier rendered as the BASE SKIN. The EQE1 side was never the problem: all
  1,784 names are in `data/eqe1_texture_index.json` and all seven archives
  have `.eqe1` packs, so the pixels were there the whole time.

  Neither of the two options the plan named was taken, and the deviation is
  deliberate. Populating `s3d_assets` would have put a ~50k-row scan of every
  S3D into a database whose own board entry says not to grow it, to duplicate
  an index the client already ships. Deleting the route outright would have
  left the armor tier broken for good. Instead the archive is DERIVED from the
  name (`ArmorArchivesForTextureName()`, `model_loading.cpp`): the variant
  digits name the archive, and the other six are probed after it -- because
  `haft2202.bmp` is the one measured file that is NOT in its own archive (it
  is in `global20_amr.s3d`). No database, no index file, nothing new shipped,
  and `data/willeq.db` was NOT rebuilt (the `--s3d-index` input only joined
  T450's digest when the flag was passed, so the shipped bytes and their pin
  are untouched).

  Deleted with it: `getAssetLocation()`, `getAssetsInArchive()`, `struct
  AssetLocation`, two prepared-statement members, `loadS3dIndex()`, the
  `s3d_assets` DDL, the `--s3d-index` flag, and `tools/s3d_index_builder`
  (whose `.json` fed only that flag and whose `.bin` no runtime code has ever
  read). Also `loadArmorTextures()` -- gutted to a flag-setter in 2026-03 and
  called once -- and `armorTextureIndex_` / `struct ArmorTextureRef`, a map
  with no writer at all, which made `MemoryStats::armorTextureCount` read 0
  forever. Pinned by `tests/test_armor_texture_archive.cpp` (9 cases) and by
  the general net `WilleqDatabaseShippedContent.EveryTableTheClientQueriesExists`,
  which takes the table census from `src/client/willeq_database.cpp` itself --
  the test that would have caught this six months earlier.

---

## Added 2026-09-07 by T451 (the loader/editor splits)

- **B469** (B) `tools/zone_line_editor.cpp`, `tools/zone_line_editor/` -- **the → issue #159
  zone line editor's SAVE path cannot be exercised without a human at a
  keyboard, so it has no regression net of any kind.** `saveZoneLineData()` is
  reachable only from `handleInput()`'s Ctrl+S arm, and the editor takes input
  through the patched Irrlicht device's **evdev** reader, not X11 events -- a
  run under Xvfb prints `evdev: No keyboard found` and `xdotool` cannot reach
  it. T451 proved the LOAD path byte-identical across its split by diffing the
  tool's whole stdout on three real zones (befallen, qeynos2, gfaydark) before
  and after, and had to fall back to the structural proof (a verbatim
  contiguous move, shown by a line-multiset diff) for the save path alone. The
  same gap covers every edit operation: nothing in ctest constructs a
  `ZoneLineEditor`. Closes when the tool grows a non-interactive entry point --
  a `--load-and-save <zone>` that runs `loadZoneLineData()` then
  `saveZoneLineData()` and exits would make the round trip a one-line ctest
  fixture, and would also answer whether the round trip is byte-stable at all
  (it reads the file it is about to overwrite, so it may not be). Found by
  T451.

---

### Added by T452 (2026-09-07)

- ~~**B474** (B, hygiene) `tools/willeq_db/db_loaders_json.cpp`
  `loadEquipmentModels()` -- **the `equipment_textures` table has 2,623 rows in
  the shipped `data/willeq.db` and NO reader anywhere in the client.** Measured
  in T452 while closing B468: a tree-wide grep for the string
  `equipment_textures` across `src/ include/ tests/ tools/ benchmarks/
  docker/ scripts/` returns the builder that writes it, and nothing else --
  every `src/` hit is an include of
  `include/client/graphics/eq/equipment_textures.h`, an unrelated header
  holding the `EquipSlot` enum. `WilleqDatabase` has no
  `getEquipmentTexture()` and names the table in no SQL. The rows are the
  17 `*equip*.s3d` archives' texture directory (`gequip.s3d` 843, `lgequip.s3d`
  459, ...), i.e. the same name->archive shape `s3d_assets` had, for held
  items rather than worn armor. So this is the SECOND half of B468's finding
  and the half that is merely dead rather than broken: it ships bytes nobody
  reads, in the one file that has a reproducibility pin. Closes when either a
  consumer is written (the equipment/held-item texture path is the obvious
  one -- check whether it currently resolves by another route first, the way
  T452 had to) or the table is dropped from the builder and the database
  rebuilt and re-pinned under T450's recipe. **Do not drop it without checking
  the equipment texture path**: B468's lesson is that "nothing reads it" and
  "nothing CAN read it because the route is broken" look identical from a
  grep. Found by T452.~~ **CLOSED by T454 (2026-09-07).** The check
  B468's lesson demanded was made and the route is HEALTHY, not broken:
  `EquipmentModelLoader::buildEquipmentIndex()`
  (`src/client/graphics/eq/equipment_model_loader.cpp:916`) indexes the same 17
  `*equip*.s3d` archives LIVE at zone load into `textureIndex_`, and
  `getTextureRef()` (:978) is what the mesh build asks -- the table was
  redundant, not load-bearing. The writer, the `CREATE TABLE`, the prepared
  statement and the whole `"textures"` parse are gone from
  `loadEquipmentModels()`; the database was rebuilt and re-pinned under T450's
  recipe in one commit. Row diff is the table and nothing else: 2,623
  `equipment_textures` rows + its schema + its `sqlite_stat1` line out,
  `metadata.total_entries` 32,414 -> **29,791**, `metadata.input_digest`
  UNCHANGED (`c39f5cfbb75e5744` -- the inputs did not move, only the loader),
  `metadata.version` still 7 and not touched. New converse net:
  `WilleqDatabaseShippedContent.EveryTableTheDatabaseShipsIsOneTheClientQueries`.

- **B475** (B, hygiene) `src/client/graphics/eq/model_loading.cpp` → issue #160
  `RaceModelLoader::getArmorTexture()` -- **each lookup opens and fully
  inflates a whole armor archive to extract one texture, and an entity needs
  about twelve of them from the same archive.** `PfsArchive::open()` inflates
  every entry into `files_`, so a `global17_amr.s3d` open is ~290 entries and
  4.1 MB inflated; T452 measured **~1.0 s cold (NFS) and ~5 ms warm** per
  open. A fully armored entity asks for ~6 body parts x ~2 pages, all in the
  same archive, so it pays that ~12 times. T452 kept the per-lookup shape on
  purpose -- it is exactly the shape the code already had, and the two obvious
  fixes (cache the opened `PfsArchive`, or harvest all of an archive's
  textures into `armorTextureCache_` on first hit) both hold ~4 MB of raw BMP
  resident per archive touched, which is a real cost on the Orange Pi, where
  composite entities are already off for memory pressure
  (`docs/decisions.md`). A third option -- harvesting only the entries whose
  race code matches the entity -- costs nothing extra and was not measured.
  Closes when one of the three is measured against the constrained preset
  rather than argued. NOT a regression: before T452 this function resolved
  nothing at all, and the code it replaced in 2026-03 opened all seven
  archives EAGERLY for a ~60 MB spike. Found by T452.


---

### Added by T454 (2026-09-07)

- ~~**B476** (B, hygiene) `tools/willeq_db/db_loaders_json.cpp`
  `loadEquipmentModels()` -- **the `equipment_models` table is the SAME finding
  as B474 and is still shipping.** 1,000 rows (model_id -> archive / wld /
  actor / textures) in `data/willeq.db`, and `src/client/willeq_database.cpp`
  names the table in no SQL -- it is the ONLY sqlite consumer in the tree
  (`grep -rl sqlite src/client include/client` returns that TU and its header
  and nothing else). The model index resolves by the same live route the
  textures do: `EquipmentModelLoader::buildEquipmentIndex()` fills
  `equipmentModelIndex_` from the 17 `*equip*.s3d` archives and `getModelRef()`
  is what `EntityPrepWorker::prepEquipmentModels()` asks. T454 left it in place
  deliberately -- its contract named `equipment_textures` only, and B468's
  lesson is that dropping a shipped table needs its own deliberate "does it
  resolve another way" check rather than being swept along. It is the one entry
  in `EveryTableTheDatabaseShipsIsOneTheClientQueries`'s `kUnreadButShipped`
  list, so it cannot be forgotten. Closes when the table is dropped and the
  database rebuilt + re-pinned under T450's recipe (the exemption comes out of
  the test with it), or a consumer is written. Found by T454.~~ **CLOSED by T456 (2026-09-08).** The B468 check was made deliberately and the model route is HEALTHY, not broken:
  `EquipmentModelLoader::buildEquipmentIndex()`
  (`src/client/graphics/eq/equipment_model_loader.cpp:180`) fills
  `equipmentModelIndex_` live from the 17 `*equip*.s3d` archives and
  `getModelRef()` (:972) is what `EntityPrepWorker::prepEquipmentModels()`
  (`src/client/graphics/entity_prep_worker.cpp:406`) asks -- redundant, not
  load-bearing. The whole `loadEquipmentModels()` loader, its `CREATE TABLE`,
  its prepared statement and its input entry are gone, and the database was
  rebuilt + re-pinned under T450's recipe in ONE commit. Row diff is the table
  and nothing else: 1,000 `equipment_models` rows + its schema + its
  `sqlite_stat1` line out, `metadata.total_entries` 29,791 -> **28,791**,
  eleven tables -> **ten**, `metadata.version` still 7 and NOT touched.
  `metadata.input_digest` DID move (`c39f5cfbb75e5744` ->
  `956f69a5686c98bb`) and correctly so -- unlike T454, which changed only the
  loader, T456 removed `data/equipment_models.json` from the INPUT SET, taking
  the builder from ten inputs to nine. `kUnreadButShipped` in
  `EveryTableTheDatabaseShipsIsOneTheClientQueries` is now EMPTY and the test
  asserts that it is.

- ~~**B477** (B) `tests/test_corpse_death_pose.cpp` -- **the ASYNC entity build
  path's copy of the corpse death-pose ladder is still unmeasured.**
  `src/client/graphics/entity_build_step.cpp:420-438` holds a second, textually
  duplicated copy of the d05/d02/d01 + `setToLastFrame()` block that
  `src/client/graphics/entity_mesh_build.cpp:365-395` has; T454 pinned the
  synchronous `registerEntity()` + `buildEntityMesh()` route with a real
  EDT_NULL device and a real Human male mesh, but `processOneEntityBuildStep()`
  never reached `meshBuilt` in that harness (no prep worker is running), so the
  async copy is asserted by nothing. Two copies of a pose rule, one tested.
  Closes when the async path is driven to `Built` in the same test and
  asserted, or the two copies are merged into one helper both call. Found by
  T454.~~ **CLOSED by T456 (2026-09-08), BOTH ways.** The two copies are merged into one
  function -- `ApplyCorpseDeathPose()`
  (`include/client/graphics/corpse_death_pose.h`,
  `src/client/graphics/corpse_death_pose.cpp`) -- which both
  `entity_mesh_build.cpp` and `entity_build_step.cpp`'s MeshFinalize phase now
  call; AND the async path is driven to `Built` and asserted in
  `tests/test_corpse_death_pose.cpp`
  (`CorpsePoseTest.TheAsyncBuildPathHoldsTheSameDeathPose`, plus
  `BothBuildPathsLandOnTheSameFrame`). The reason T454 could not reach it: the
  selection gates at the top of `processOneEntityBuildStep()` reject an entity
  whose `cachedBspRegion` is unset and whose background prep never ran, and
  neither is settable from outside. The lever that works is the player-entity
  priority (`setPlayerSpawnId()`), which bypasses those gates for one step;
  after that the entity is 'in progress' and needs no gate. The Placeholder
  phase also returns immediately unless the race model is already cached, so
  the test calls `preloadModelData()` -- the same call the prep worker makes.
  These assertions are a PRESERVATION pin: the duplicate was already correct,
  and it passed pre-merge as well as post-merge. Found by T454.

- ~~**B478** (B) corpse recognition is derived FIVE times from the same two
  fields and the copies disagree. The canonical predicate is
  `IsCorpseByFields()` (`src/client/corpse_predicate.cpp:75`: flag OR npc_type
  2/3 OR one of three name forms WITH a trailing id), but
  `src/client/bridge/bridge_component.cpp:464`,
  `src/client/application_zone_loading.cpp:239` and `:349` each recompute
  `npc_type == 2 || npc_type == 3` inline plus a LOOSER `name.find("corpse")`
  with no id check, and `include/client/state/entity_manager.h:74`
  `isAnyCorpse()` is a fifth form with no name test at all.
  `include/client/corpse_predicate.h` already carries a "do not write a second
  corpse test" note, and there are four. They agree on every case a server
  actually sends (eqemu writes `NPC = 2` for every corpse,
  `/mnt/projects/akk-stack/code/zone/corpse.cpp:1809`), so this is hygiene,
  not a live defect -- but the loose name check would classify a live NPC
  literally named "a corpse beetle" as a corpse and freeze it in a death pose.
  Closes when the three inline copies call `IsCorpseByFields()`. Found by T454.~~ **CLOSED by T456 (2026-09-08), and the census here UNDERCOUNTED.** There were
  not five derivations but EIGHT -- the canonical `IsCorpseByFields()` plus
  SEVEN copies. The four this entry names, and three it missed, all found by
  the new source census:
  * `src/client/combat_hunting.cpp:251` `IsTargetSuitableForHunt()` --
    `name.find("'s corpse")`, and doubly wrong: that is only the PLAYER-corpse
    spelling, so every npc corpse (`a rat`s_corpse17`) passed through as a live
    hunt target while a live "a corpse beetle" would not have;
  * `src/client/combat_hunting.cpp:482` `ListHuntTargets()` -- same substring,
    same two ways of being wrong;
  * `src/client/combat.cpp:724` -- three chained substring tests, the last a
    bare `find("corpse")`.
  All seven now call `IsCorpseByFields()` (or `IsCorpseEntity()`, its Entity
  overload). The net that keeps the eighth from appearing is
  `CorpseRecognition.NoProductionSiteDerivesCorpsenessFromANameSubstring`
  (`tests/test_corpse_death_pose.cpp`), a comment-filtered census over `src/`
  and `include/`; it named all five offending FILES pre-fix and is green
  post-fix. It carries ONE exemption, boarded as B479.
  `CorpseRecognition.TheStateLayerAgreesWithTheOnePredicate` is the behaviour
  half and was RED pre-fix. Found by T454.

---

## Added 2026-09-07 by T449 (the owed live proofs)

- ~~**B482**~~ (A) `src/client/graphics/ui/character_model_view.cpp` /
  `src/client/graphics/overlays/` / the GLES2 driver's
  `bindAlphaCompanionUnit()` -- **fable SEGFAULTS on a zone change whenever the
  inventory popup is open.** The paperdoll's animated mesh node survives the
  zone unload holding textures the unload destroyed, and the first frame in the
  new zone dereferences one:
  `CharacterModelView::renderToTexture()` -> `CSceneManager::drawAll()` ->
  `EQAnimatedMeshSceneNode::render()` -> `COpenGLES2Driver::setMaterial()` ->
  `applyMaterialState` -> `applyMaterialRenderState` ->
  `bindAlphaCompanionUnit()` -> `COGLES2Texture::getOpenGLTextureName()`, on
  the RENDER thread out of `FrameComponent::renderStaticUI()`. Reproduced twice
  (qeynos2 -> qeynos and qeynos -> qeynos2), once under gdb for the backtrace,
  and **A/B controlled**: the identical zone change with
  `[InventoryWindow] Show=0` in `UI_<char>_<server>.ini` completes two zone
  loads with no fault. `dmesg` carries `traps: willeq[2804830] general
  protection fault` at the second the log stops. **T432's persistence makes it
  reachable with no player action at all** -- a character who ever left the
  inventory open is logged back in with the paperdoll already rendering, and
  the next zone kills the client. Evidence: `review/T449/README.md` §0,
  `review/T449/logs/t449_crash_gdb.log`,
  `review/T449/logs/t449_control_inventory_closed.log`. NOT fixed here: the
  unit's STOP allows only a one-line pinned fix, and a null guard inside
  `bindAlphaCompanionUnit()` would paper over a dangling `ITexture*` rather
  than stop the paperdoll holding it. Found by T449.~~ **FIXED T457 (8900efa3, merged 728749e6): the paperdoll lives in its OWN scene manager (`createNewSceneManager`), invisible to `ConstrainedTextureCache::clear()`'s reference scan; `ZoneLifecycleComponent::unloadZone()` now calls `Overlays().onZoneUnload()` BEFORE the mesh caches and the texture cache clear, releasing the model; pinned by `tests/test_paperdoll_zone_unload_order.cpp` (3 red→green). LIVE A/B OWED (T457 paused before it).**

- ~~**B483**~~ (P) `scripts/eqemu/up.sh`, `scripts/scenario.sh` -- **the
  non-launcher bring-up starts a SECOND world and loginserver on top of the
  ones the akk-stack container starts by itself.** Measured: 45 s after a bare
  `docker compose up -d mariadb eqemu-server`, with no `up.sh` run at all, the
  container is already running `/tmp/spire-launcher eqemu-server:launcher
  start` and its world. `up.sh`'s default path then launches `./bin/world` and
  `./bin/loginserver` anyway and its port checks pass instantly against the
  container's own. Which world a client reaches is a race: this unit saw
  `Got response from login server but could not find world server WillEQ` once
  and two `cast_lands` runs stall forever at `[LOADING][Phase 4/15] Loading
  characters` -- both indistinguishable from a client defect. `scenario.sh`
  calls the non-launcher path, so **every B305 run is on this coin toss**. The
  one-line candidate is `up.sh --launcher` in `scenario.sh`; it is not taken
  here because it wants a pin and there is no shell-level test for the lane.
  Found by T449.~~ **FIXED T457 (8900efa3): `up.sh` adopts a running fleet — cold run one world, second run adopts, still one world. Correction: the container's auto-launcher starts the ZONE fleet only (no world/login); T449's duplicate world was its own earlier fleet.**

- ~~**B484** (A) `scripts/scenario.sh` (`SPELL=46`), → issue #35
  `scripts/scenario_configs/cast_lands.json` -- **the `cast_lands` scenario's
  default spell cannot be cast by its own default character, so the lane can
  never pass as shipped.** `spells_new` row 46 (Ultravision) has
  `classes12 = 255` and `cast_lands.json` runs **Testcaster, class 12, a
  Wizard**; eqemu answers every attempt with `Your spell fizzles!`. This is
  not level or skill: the run was repeated with the character at level 60 and
  every one of its 100 `character_skills` rows at 200 and fizzled 3/3 again.
  B305's suspicion about the `#memspell` SYNTAX is settled the other way --
  `#memspell 46 0` is accepted and the client logs
  `Server memorised 'Ultravision' (46) into gem 1`, so the memorise is fine and
  the CAST is what fails. Proven with a substitute: spell **288** (Minor
  Shielding, `classes12 = 1`) landed on the first gem cast and the scenario
  exited **0**, `spell 288 landed on Testcaster 2.93 s after gem cast #1 (buff
  slots 0 -> 1)`. Closes by picking a spell the config's class can cast (and
  asserting that at run time rather than in a comment).
  `review/T449/logs/cast_lands_direct*.log`. Found by T449.~~ **STRUCK
  2026-09-19 (issue #35 closed by the B484 card,
  `task/kb-b484-cast-lands-default-spell`)**: the default is 288 in all three
  places the lane reads one (`scripts/scenario.sh` `SPELL=288`,
  `ScenarioOptions::castSpellId`, `ApplicationConfig::scenarioSpellId`), and
  `scripts/scenario.sh` now re-checks it at run time -- for cast_lands, before
  the client launches, it reads the config's character's class and level and
  `spells_new.classes<N>` (the per-class minimum cast level; 255 = the table's
  "no class carries it" sentinel, eqemu's own convention in its spell table)
  and exits **72** naming spell, class and level instead of fizzling three 20 s
  windows. Measured the real mechanic on the lane: neither fizzle model is a
  hard block -- a level-1 Wizard on spell 46 (min level over all classes: 26)
  rolls a 95% fizzle, so the same run fizzled 3/3 once and landed 1/3 minutes
  later. Re-measured: the shipped lane exits 0 (`spell 288 landed on
  Testcaster 2.58 s after gem cast #1`), and `--spell 46` fails fast in under
  a minute with `spell 46 has no minimum level for
  (spells_new.classes12 = 255 ...)`. `refactor/T506_cast_lands_default_spell.md`
  (evidence: `refactor/B484/`).

- **B485** (B) `scripts/scenario.sh`, `src/client/scenario/scenario_two_client_survive.cpp` → issue #161
  -- **`two_client_survive`'s peer terminates 0.15 s after it zones in, so the
  observer's hold can only ever be satisfied by the server's LINKDEAD
  retention.** The peer is launched as `--scenario zone_in`, which reaches its
  verdict immediately and unwinds the client
  (`Zone connection complete` 21:07:25.710, `Application shutdown complete`
  21:07:25.746). The observer nevertheless saw a second player for 91.6 s and
  then failed with `second player vanished 91.59 s into the hold`. **A shorter
  `--hold` would have made this run PASS for a reason that is not the one the
  scenario claims** -- which is the exact failure T410 built this scenario to
  replace. The peer needs a scenario that HOLDS (a `zone_in` that honours
  `--scenario-hold`, or the peer running `two_client_survive` itself).
  `review/T449/scenario/two_client_survive_*.log`. Found by T449.

- **B486** (B) `src/client/scenario/scenario_cast_lands.cpp` -- **a `cast_lands` → issue #162
  re-run inside the buff's own duration can never pass.** Re-casting the same
  spell refreshes the slot it already occupies, so the baseline multiset does
  not move and the scenario reports `spell 288 never landed after 3 gem casts
  (buff list unchanged at 1 slot(s))` -- while the client's chat shows all
  three casts SUCCEEDING. `ABuffThatWasALREADYThereIsNotALanding` is doing
  exactly what it was written to do; the gap is that the prep clears
  memorisation (`#unmemspells`) and nothing clears buffs. Measured with spell
  288 (buffduration 270 ticks) twice inside its own duration.
  `review/T449/logs/cast_lands_direct_spell288_lvl1.log`. Found by T449.

- **B487** (B) `src/client/input/hotkey_manager.cpp`, `config/hotkeys.json` -- → issue #163
  **the chat input stays open after a line is submitted, so the next hotkey
  press lands as TEXT in the chat bar.** The client's own startup warnings name
  the cause and print them every run: `ConfirmDialog and SubmitInput both bound
  to Return in global mode`, `ConfirmDialog and OpenChat both bound to Return
  (global conflicts with player)`, `SubmitInput and OpenChat both bound to
  Return`. Measured live: after `/say #set time 11 0` the following `ctrl+b` /
  `alt+p` / `ctrl+F5` presses appeared as the characters `/pl` on the chat
  line and no window toggled. `Escape` does close the input -- and in the one
  press that was observed it closed the topmost popup in the same press, which
  is a second question this entry does not answer. Class B because a player
  can always press Escape; it cost this unit three attempts at a two-key
  sequence. Found by T449.

- ~~**B488**~~ (A, unowned side) **a zone change into `airplane` (Plane of Sky) → issue #36
  leaves the client stuck at `[LOADING][Phase 5/15] Connecting to zone` for
  ever, with the game thread already at the arrival position.** `#zone
  airplane` is refused below level 46 (`zone.min_level = 46`, answered as
  `Zone change DENIED (success=-7)`); at level 50 the server accepts it, sends
  `zone=71 instance=0 pos=(10.0, 0.0, 5.0)`, `/loc` reads the arrival
  `10.0, 0.0, 4.4` -- and the renderer never leaves the old zone and no further
  loading phase is printed. The launcher fleet had twelve idle dynamic zone
  processes at the time. Which side owns it (a zone process that never booted
  vs. a client that never reconnected) is UNMEASURED and needs the zone
  process's own log. **This is what blocks B453's `/dumpscene` proof**, since
  T440 measured Plane of Sky as the one playable zone with no BSP tree.
  `review/T449/logs/t449_control_inventory_closed.log`. Found by T449.~~
  **STRUCK 2026-09-19 (re-measured live, issue #36, `task/kb-b488-airplane-zone-stuck`):
  NOT reproducible on a build containing T457 — six airplane entries and four
  qeynos2<->airplane round-trips all reach `Phase 15/15`, with the world log,
  the airplane zone process's own log and the client agreeing on
  `172.16.200.99:7001` (world sent it, client dialed it, exactly one zone
  process owns it and accepts the client in 0.5 s). The original observation
  is most consistent with the **B483 double-world race**, fixed by T457 and
  documented as the contamination of that very T449 session (its own README
  discards the first `zone_in` run for it); the UDP zone connect has no
  timeout, so a hand-off to a port nobody answers on stalls at Phase 5/15
  forever — every clause of the filed symptom, no client code involved. The
  client path was unchanged between T449's base and the re-measurement build.
  B453's `/dumpscene` unblock stands: airplane loads its map (7611 BSP regions)
  and navmesh cleanly. Evidence: `refactor/B488/`.**

- **B489** (B) `src/client/graphics/ui/static_ui_input.cpp` -- **nothing in → issue #164
  fable's inventory UI picks an item up to the cursor.** A left-click on an
  inventory slot opens T421's item display window; the only producer of a
  cursor item is the server's `InventorySlotChanged` for slot 30/33 (a
  purchase, a loot, a `#summonitem`). So a player cannot move an item between
  slots, into a bank slot, or onto a merchant to sell -- the sell arm exists
  and reads `cursor.hasItem`, but only the server can put something there.
  Found while trying to prove T439/B444's bank half of the zone-in snapshot,
  which is why **the bank SLOT (as opposed to the bank COINS) is still
  unproven live**. Found by T449.

---

## Added 2026-09-07 by T455 (the zero-caller census, `refactor/T455_zero_caller_census.md`)

**One finding, thirteen owners.** T455 took the FUNCTION half of this project's
dominant defect class as a script rather than by hand:
`scripts/refactor/check_zero_callers.py` collects every free function and every
public/protected member function declared under `include/` and `src/`, sweeps
`src/ include/ tests/ tools/ benchmarks/ docker/` for uses with comments and
string literals stripped, and reports the ones with no use anywhere. **4,430
candidates, 449 zero-caller hits.** Thirty-two of them were residue T455
DELETED outright (`common/util/strings`, the whole of `common/util/file`, three
`position.h` overload sets); the remaining **417** are grouped below, one entry
per subsystem, and are listed row by row in the script's `EXEMPT` table so the
count ratchets and no NEW dead function can be added silently (ctest
`ZeroCallers`).

Read these three limits before working any of them — they are why a row here is
a LEAD, not a verdict:
- **A caller count is not reachability** (T442/B451: `checkCollisionIrrlicht()` <!-- RETIRED-OK: citing the deleted function is the point of the warning -->
  had five call sites and was still unreachable). The converse also holds: a row
  here has zero callers, but that is not proof the feature is unwanted.
- **"Nothing calls it" and "nothing CAN call it because the route is broken"
  look identical from a grep** (T452/B468).
- The census matches by IDENTIFIER, not by receiver type, so a name collision
  can only ADD uses. It therefore MISSES dead functions and never invents one;
  the 417 are a floor.

Each entry closes when every row it owns is either wired (with the caller named)
or deleted, and its rows removed from `EXEMPT`. A row deleted individually is
fine — the table fails on a stale entry, so it cannot rot.

- **B490** (B, unreached) **The environment/particle subsystem's query and → issue #165
  control API: 35 zero-caller public functions across 11 headers under
  `include/client/graphics/environment/`.** Emitter, flock, boids, overlay and
  storm-cloud accessors and setters that no renderer pass, no config load and
  no slash command reaches — `ParticleManager::registerExternalEmitter()` /
  `unregisterExternalEmitter()`, `setWind()`, `getAtlasTexture()`,
  `setWeatherLights()`; `FlockController::setDestination()` / `scatter()` /
  `setCollisionDetection()`; `RainOverlay::getSettings()` /
  `SnowOverlay::getSettings()`. Note the overlap with the FIELD census: B339
  and B346 already own `WeatherEffectsController::targetIntensity_` and
  `ParticleEmitter::spawnRadius_`, which are the same subsystem's unread
  members. Found by T455.

- **B491** (B, unreached) **The S3D/WLD asset-loader query API: 49 zero-caller → issue #166
  public functions across 10 headers under
  `include/client/graphics/eq/`.** Loader accessors
  (`S3DLoader::getGeometry()` / `getObjects()` / `getObjectCount()` /
  `getLightCount()`, `WldLoader::getTrackRef()`, `SkyLoader::getLayerCount()`),
  the whole of `SkeletalAnimator`'s playback and frame-trigger control surface
  (`pauseAnimation()`, `resumeAnimation()`, `setBlendingEnabled()`,
  `setBlendDuration()`, `setEventCallback()`, `addHitFrameTrigger()`,
  `addFootstepTriggers()`, `removeFrameTrigger()`, 16 rows), and the
  `equipment_textures.h` / `race_codes.h` inline helpers. The animator group is
  the interesting one: it is a complete blend-and-trigger feature with no
  caller at all, which is the T39/B024 shape. Found by T455.

- **B492** (B, unreached) **The detail/foliage subsystem's query and control → issue #167
  API: 24 zero-caller public functions across 8 headers under
  `include/client/graphics/detail/`.** `DetailManager::adjustDensity()` (its
  comment says "For keyboard +/- controls" and no key is bound to it),
  `clearSeasonOverride()`, `setFootprintConfig()` / `getFootprintConfig()`,
  `getTotalPlacementCount()`, `isFoliageDisturbanceEnabled()`;
  `FoliageDisturbance::addDisturbanceSource()` / `clearSources()` —
  i.e. nothing ever adds a disturbance; `SurfaceMap`'s four bounds accessors;
  `SeasonalController::hasOverride()` / `getOverride()`. Related to the closed
  B330-B336 field rows in the same subsystem. Found by T455.

- **B493** (B, unreached) **The paperdoll's input API and the output-renderer → issue #168
  factory: 15 zero-caller public functions across
  `include/client/graphics/ui/character_model_view.h` and
  `include/client/output/`.** `CharacterModelView::onMouseDown()` /
  `onMouseMove()` / `onMouseUp()` / `isDragging()` / `setRotationY()` — the
  panel IS drawn, and B343 already records that it holds an
  `inventoryManager_` it never reads; this is the other half, a drag-to-rotate
  interaction with nothing routing mouse events into it. Plus
  `createRenderer()` (`output_renderer.h`) and
  `GraphicalRenderer::getRenderQuality()`. Found by T455.

- **B494** (B, unreached) **Renderer component and helper API: 127 zero-caller → issue #169
  public functions across 28 headers under `include/client/graphics/`** — the
  largest group, and the one T448's ten deletions did not reach.
  `EntityRenderer` alone has 33: the whole debug offset/rotation control set
  (`adjustOffsetX/Y/Z`, `adjustRotationX/Y/Z`, `resetOffsets()` and their six
  getters), the per-entity animation query set (`getEntityAnimation()`,
  `hasEntityAnimation()`, `getEntityAnimationList()`,
  `isEntityPlayingThrough()`, `getEntityPoseState()`), `setOcclusionCuller()`,
  `setNameTagDistance()`, `isPlayerInFirstPersonMode()`. Then
  `CameraController` (11), `TextureAtlas`'s whole preload path (10:
  `preloadFromFile()`, `uploadPreloadedPage()`, `uploadPreloadedPageAsync()`,
  `finalizePreload()`), `ZoneShaderManager` (9, including the four wind
  material-type getters and `appendAtlasPageTextures()`), `SkyRenderer`'s strip
  upload (`beginStripUpload()` / `continueStripUpload()` /
  `finalizeStripUpload()` / `isStripActive()`), `WeatherSystem` (8),
  `ConstrainedTextureCache::freeze()` / `isFrozen()`, and
  `RendererContext::Slash()` — a Batch-L sibling accessor nothing calls.
  `SpellVisualFX::removeBuffAura()` / `removeAllForEntity()` are in this group
  and are ALREADY owned by **B195**: do not delete them independently of B161,
  they are the leak fix if the creation path is wired. Split this entry before
  working it. Found by T455.

- **B495** (B, unreached) **InventoryManager's bank/cursor/callback API and the → issue #170
  unreached `inventory_constants.h` helpers: 41 zero-caller public functions
  across 3 headers.** `getBankItems()`, `getSharedBankItems()`,
  `getSellableItems()`, `isBankBag()`, `getBankBagSize()`, `isBankBagEmpty()`,
  `clearBankSlots()`, `swapItems()`, `destroyItem()`, `destroyCursorItem()`,
  `popCursorItem()`, `getBagSize()`, `dumpInventory()`, the three
  `set*Callback()` registrars (whose stored members B342 already owns as
  write-only), and eleven `inventory_constants.h` slot helpers
  (`isAnyBankSlot()`, `calcSharedBankBagSlotId()`, `getParentBankSlotAny()`,
  `getBankBagIndexAny()`, `isContainerSlot()`, ...). The bank and inventory
  windows read `GameState`, not the manager, which is why a whole bank API can
  sit here with a working bank window. Found by T455.

- **B496** (B, unreached) **GameState module setters and getters with no → issue #171
  caller: 30 zero-caller public functions across 7 headers under
  `include/client/state/`.** `PlayerState`'s seven stat setters
  (`setSTR/STA/CHA/DEX/INT/AGI/WIS`) — the server sends the values and nothing
  stores them; `bindHeading()`, `setCharacterId()` / `characterId()`,
  `setVelocity()`, `setMovementSequence()`; `WorldState::setZoneLoading()` /
  `setZoneLoadProgress()` / `zoneLoadProgress()` / `zoneLoadStatus()` — a
  zone-load progress channel with neither a writer nor a reader;
  `TradeState`'s three partner-item accessors; `SpellState::castingTargetId()`,
  `memorizingSpellId()`; `InventoryState`'s three equipment-total getters;
  `GroupState::updateMemberStats()`. Found by T455.

- **B497** (B, unreached) **Top-level manager API with no caller: 26 → issue #172
  zero-caller public functions across 9 headers directly under
  `include/client/`.** `Application::getGameMode()` / `getActionDispatcher()` /
  `getCommandProcessor()` / `getEverQuestClient()` — four component accessors
  on the top-level orchestrator, none reached; `CombatManager::LoadSpellSet()`
  / `SaveSpellSet()` / `GetMemorizedSpells()` / `GetNearbyAllies()`;
  `TradeManager`'s slot API (`addItemToTrade()`, `removeItemFromTrade()`,
  `hasPartnerItem()`, `setOwnMoney()`, `handlePartnerItemRemoved()`);
  `DoorStateManager::getDoorsInRadius()` / `removePendingClick()` /
  `clearPendingClicks()`; `ZoneLines::addZonePoint()` / `getZonePoint()`;
  `WilleqDatabase::getAnimationSound()`; `GetChatTypeName()`;
  `getCombatSkillAnimation()`. Found by T455.

- **B498** (B, unreached) **The input-injection API: 15 zero-caller public → issue #173
  functions across `include/client/input/`.** `NullInputHandler`'s eleven
  `inject*` methods (`injectSpellCast()`, `injectHotbar()`, `injectTarget()`,
  `injectLoot()`, `injectChatMessage()`, `injectMoveCommand()`,
  `injectMoveToEntity()`, `injectFace()`, `injectFaceEntity()`,
  `injectStopMovement()`, `injectRawCommand()`) plus
  `GraphicsInputHandler::injectTargetRequest()` / `injectLootRequest()` and the
  two `ConsoleInputHandler` mode predicates. This is the automation surface
  T410's scenario lane does NOT use: `ScenarioRunner` drives `EverQuest`
  through `IScenarioWorld` instead. Decide whether the injection API is the
  scenario lane's missing input half or dead weight from the pre-scenario era
  — do not delete it without reading `include/client/scenario/scenario.h`
  first. Found by T455.

- **B499** (B, unreached) **Audio query API with no caller: 12 zero-caller → issue #174
  public functions across 7 headers under `include/client/audio/`.**
  `AudioManager::isAutoAttackMusicPlaying()` / `isVendorBankMusicPlaying()` /
  `isUse3DAudio()`, `AudioMixer::getSfxVolume()`, `EffLoader::getMp3Index()`,
  `SoundBuffer::loadFromPCM()`, `WeatherAudio::getThunderTimer()`,
  `ZoneSoundEmitter::getXmiIndex1()` / `getXmiIndex2()`, and `MusicPlayer`'s
  three OpenAL-era methods (`enableSoftwareRendering()`,
  `isSoftwareRendering()`, `reinitializeOpenAL()`) — the client uses miniaudio,
  so those three are residue from a backend that is gone. Found by T455.

- **B500** (B, unreached) **The `IGameMode` implementations' accessors: 10 → issue #175
  zero-caller public functions across the three mode headers.**
  `AutomatedMode::getGameState()` / `queueCommand()` / `getNextCommand()` /
  `hasPendingCommands()` / `setEventCallback()` — a whole command queue that
  T410's scenario lane replaced with `setAutomationCallback` and did not
  remove; plus `HeadlessMode::consoleInput()` / `consoleRenderer()` /
  `getGameState()` and `GraphicalMode::getGameState()` /
  `getRendererType()`. Note B345 already owns three write-only
  `ConsoleRenderer` members, which is the same corner of the tree. Found by
  T455.

- **B501** (B, unreached) **`common/` utility API with no caller: 12 → issue #176
  zero-caller public functions across 5 headers.**
  `PerformanceMetrics::getTimings()` / `resetGameplay()` / `getElapsedMs()` /
  `markZoneLoadStart()` / `markZoneLoadEnd()` / `getCategoryName()` — a
  zone-load timing channel nothing marks; `ClampLower()`; `Random::Shuffle()`;
  `Logger::GetMutex()`. This is the remaining `common/util` residue after
  T455 deleted `strings` and `file`. ~~The KSM heap helpers
  (`MarkHeapStart()`, `MeasureHeapUsage()`, `PageAlignVectorAligned()`)~~:
  B941 (#259) deleted the whole `ksm.h` header when its last user,
  `RaycastMesh`, moved to plain vectors. Found by T455.

- **B502** (B, unreached) **Gameplay-domain manager API with no caller: 21 → issue #177
  zero-caller public functions across 9 headers
  (`skill/`, `spell/`, `chat/`, `movement/`, `player/`).**
  `SkillManager::isSkillReady()` / `getSkillsByCategory()` /
  `getActivatableSkills()` / `toggleSkill()` / `isSkillActive()`;
  `SpellAnimations::getTargetAnimation()` / `getSchoolCastAnimation()` /
  `getAnimationDuration()` / `isValidAnimation()` — the whole spell-animation
  lookup table, unreached, in a client that plays cast animations by another
  route; `BuffManager::getSpellDatabase()`;
  `ChatMessageBuffer::getRecentMessages()` / `hasNewMessages()` /
  `clearNewMessageFlag()` / `getChannelName()`;
  `MovementComponent::IsSwimming()` / `GetMovementSpeed()` /
  `WaterStateRef()`; `PlayerComponent::PerformEmote()`;
  `SkillData::getValueString()`; `SpellData::isInstantCast()`. Found by T455.

- **B503** (P, process) **The zero-caller census is ONE PASS and does not see
  cascading death.** T455 deleted 24 functions from `common/util/strings`, re-ran
  the census, and `Strings::SecondsToTime` and `Strings::ToUnsignedInt` <!-- RETIRED-OK: the tombstone for the two functions this unit deleted -->
  appeared as NEW hits — their only callers had been the functions just
  deleted. A third pass found none, so the tree is at a fixed point today, but
  the same will happen to whoever works B490-B502: **after deleting a group,
  re-run `check_zero_callers.py` until it stops producing new rows, and only
  then trust the count.** The same shape hid `File::Exists()`, which showed one
  use (the free `Exists()` wrapper) that was itself dead — the whole
  `common/util/file` translation unit was dead behind a single live-looking
  reference. Found by T455.

---

### Added by T457 (2026-09-08), entered by the orchestrator at merge

- ~~**B504** (class A, process): `scripts/eqemu/up.sh` polls the zone log for
  `Zone bootup type`, which this eqemu build never prints (it prints
  `Bootup Booting [<zone>]`), so a cold `up.sh` times out after 60 s and exits 1
  on a zone that booted correctly. `scripts/scenario.sh` turns that into
  operator code 65. Evidence: `review/T457/logs/up_sh_cold_container.log`.~~
  **STRUCK 2026-09-19 (issue #90 closed by the B504 unit, `task/kb-b504-up-sh-boot-marker`)**:
  re-measured on the VM — the current build (binaries 2026-02-24,
  `CURRENT_VERSION 23.10.3-dev`) DOES print `Zone bootup type [Static]` on a
  cold start-zone boot, so the literal "never prints" defect does not reproduce
  on it; what was real and is now fixed is the structural weakness — a single
  unpinned grep string with no failure fast-path and no liveness check: a
  bogus shortname cost the shipped check the full 60 s timeout (measured 111 s
  run) where the zone had said `Zone Bootup failed :: Zone::Bootup` ~5 s in.
  `scripts/eqemu/up.sh` now calls `eqemu_zone_booted` (env.sh): marker AND the
  shortname's process alive, with `Zone Bootup failed` as a hard stop;
  `scripts/eqemu/zone_booted_selftest.sh` re-measures the marker on every run.

---

## Added 2026-09-13 by T460 (cursor eviction, PlayerMoved heading unit)

- ~~**B520**~~ (A) CLOSED by T460. **The DRM/KMS software cursor vanished at the
  first zone unload, for good.** `IrrlichtRenderer::createSoftwareCursor()`
  never `protectTexture()`'d it. `ConstrainedTextureCache::clear()` (zone
  unload) and `evictUntilAvailable()` (LRU; the startup-loaded cursor is always
  the oldest) evicted it. `SingleTexturePointerGuard` nulled
  `softwareCursorTexture_`, and nothing reloaded it. Fixed by
  `AcquireSoftwareCursorTexture()` (`software_cursor_texture.h`), which
  protects it; pinned by `tests/test_software_cursor_eviction.cpp`. **Live
  proof (T459):**
  - Before the fix, every Pi run logged the texture NULL on frame 2, the first
    in-world frame, with zero LRU evictions. The cursor was therefore never
    drawn in a session.
  - On slot3 @2f0bd914, runs P6 and P7 logged no NULL transition all session.
  - Stills are in `review/T459/pi/after_T460/`.
- ~~**B521**~~ (A) CLOSED by T460. **`PlayerMovedData::heading` published in
  degrees where the renderer reads EQ 0..512**, at two of the eight bridge
  publishers: `MovementComponent::UpdateMovement` (`/moveto`, `/follow`,
  auto-attack chase, every tick) and `WarpToPosition` (`/warp`). The camera
  heading was deg*360/512 of the right one for every game-driven move. Fixed
  at both sites; `tests/test_player_moved_heading_units.cpp` pins the unit for
  every publisher (source census, count 8) and the consumer. **Live proof
  (T459):**
  - Baseline P3: `/move` flipped the camera heading 437.17 → 76.39 in one
    frame; x86 showed the same.
  - Post-fix P6: 864 writes, all 371.6–372.8.
  - This is not Will's jerk: keyboard runs never reach this publisher (B508).
- ~~**B522**~~ (A) CLOSED by T463. **`StormCloudLayer::cloudFrames_` were nulled
  on eviction and never reloaded, so every storm after the first zone unload
  showed a static placeholder dome.** Confirmed pre-fix by
  `tests/test_environment_texture_eviction.cpp`: after `clear()` (both unloads)
  and under LRU pressure, every one of 32 storm steps drew the placeholder,
  and one texture was seen where a cycle shows 32. Fixed by re-acquiring an
  evicted frame at its point of use (`StormCloudLayer::reacquireFrame()` from
  `updateBlendedTexture()`), not by protection (~11 MB) and not in
  `onZoneEnter()` (it would spend that budget in every zone); see
  `refactor/T463_evicted_texture_no_reload.md`.
- ~~**B523**~~ (A) CLOSED by T463. **`TumbleweedManager`'s texture was a
  USE-AFTER-FREE after the first zone unload**, not just a missing texture.
  No listener, no protection, no reload; the texture is baked once into
  `tumbleweedMesh_`'s buffer material. Confirmed pre-fix on EDT_NULL: with no
  tumbleweed spawned when `clear()` ran, the mesh buffer AND the next zone's
  first spawned node both held a pointer the driver no longer listed (freed),
  and still did after a second unload. Under LRU pressure the live node went to
  the placeholder. Fixed by `protectTexture("tumbleweed")` in `init()`: it is
  64x64 and renderer-lifetime.
- ~~**B524**~~ (A) CLOSED by T463. **`BoidsManager`'s `creature_atlas` degraded to
  the placeholder after the first zone unload, for good.** Confirmed pre-fix:
  `creatureMaterial_` (what `render()` binds) held the placeholder after each
  of two `clear()`s and under LRU pressure. Fixed by
  `protectTexture("creature_atlas")` in `loadCreatureAtlas()` (64x64,
  renderer-lifetime); the T124 swap guard stays as the net.

  **The class is closed by a census**, not by these three fixes alone:
  `TextureCacheHolderCensus` (`tests/test_texture_cache_holder_census.cpp`)
  makes every `getOrLoad()` call site under `src/client/graphics` and
  `include/client/graphics` declare a lifetime policy (Protected /
  ListenerReload / ZoneScoped / NotOnGles2) and checks the marker each policy
  stands on. A new site, or a changed call count, fails until it declares one.
- **B525** (A, *lead*, for T459) **Two camera-heading writers remain possible → issue #37
  while moving.** T460 found no client-side per-tick echo:
  `OnGraphicsMovement` publishes no `PlayerMoved`, and `UpdateMovement`
  returns without console movement. Unmeasured:
  - (a) whether eqemu returns our own `OP_ClientUpdate` to us. That path runs
    the `ZoneProcessClientUpdate` self branch, then `OnSpawnMovedGraphics`,
    then `PlayerMoved`, then `setPlayerPosition`, which overwrites the camera
    heading and re-arms `playerPositionIsSeed_`. Count
    `POS S->C [SELF]` lines during a keyboard run at debug level 1.
    **MEASURED by T459, and refuted:** P6 (Pi, slot3 @2f0bd914) had 0 self
    updates and 0 bridge writes across two keyboard forward runs (643
    frames). The session's only self update was the zone-in confirmation.
  - (b) keyboard input during an active `/follow` or auto-attack chase, where
    the game thread publishes `PlayerMoved` every tick while the render thread
    integrates input.

  Found by T460.

---

## Added 2026-09-13 by T463 (evicted environment textures)

- ~~**B550**~~ CLOSED by T467 (`refactor/T467_tumbleweed_lazy_init_and_session_identity.md`
  Review). **Confirmed in code, refuted as a live path.** Red:
  `tests/test_environment_manager_construction.cpp` drives the renderer's own
  `applyEnvironmentalDisplaySettings()` on an EDT_NULL device with the real
  cache, and the unfixed binary exited 1 at
  `[FATAL] TumbleweedManager: Constrained texture cache not set`. Not reachable
  in a shipped build: `IrrlichtRenderer::cachedDisplaySettings_` has ONE writer,
  the once-only load in `initLoadingScreen()`, which builds the manager from
  that same value, and the only `tumbleweedManager_.reset()` is in
  `shutdown()`. Fix: the boids, tumbleweed, detail and weather-effects managers
  are built only by `OverlaysComponent::ensure*()`
  (`src/client/graphics/overlays/overlays_env_managers.cpp`), which both init and the lazy site
  call; the census case fails on a second construction under `src/`. Original
  entry, kept for the record:
  **The lazy `TumbleweedManager` creation never gives it the
  texture cache, and `init()` exits the process without one.**
  `OverlaysComponent::applyEnvironmentalDisplaySettings()`
  (`overlays_hud_env.cpp`, "Tumbleweed Manager: lazy create if toggled on")
  constructs the manager and calls `init()` with no
  `setConstrainedTextureCache()`. Its two siblings in the same function (boids,
  detail) do pass the cache. `TumbleweedManager::init()` then logs
  `Constrained texture cache not set` at FATAL and `std::exit(1)`s. The
  function runs from Step11 of every zone load. It reaches this branch only
  when `rollingObjects` was false at renderer init and true at a later zone
  load; renderer init creates the manager with the cache whenever the setting
  is already on. Unmeasured: whether anything changes that setting between
  zones in a shipped build. The fix is one statement, but it is outside T463's
  territory (`overlays/`). Found by T463.

---

## Added 2026-09-13 by T459 (Orange Pi live diagnosis)

Evidence for every entry is under `review/T459/` (README there). Pi runs used
Will's recipe: `--drm --renderer gles2 --constrained orangepi -r 800 600`, no
`--eqe1-path`, Testcaster in nro. Input was injected through `/dev/uinput`,
because the Pi had no keyboard or mouse attached (B510).

- ~~**B505**~~ (A) CLOSED by T462 (`592b2302`). `renderChatPanel()` now draws
  from `ChatWrapCache` (`chat_wrap_cache.h`), which wraps only messages that
  arrived since the last frame (keyed by `ChatMessageBuffer::appendedCount()`)
  and rebuilds on a width, timestamp or clear() change. Pinned by
  `TheChatWrapRunsOncePerNewMessageNotOncePerFrame` (was 2,000 wraps for ten
  frames over 200 messages, now 200) and by a line-for-line comparison against
  a verbatim copy of the old loop. Pi, 1280x720, 600 chat lines over five
  minutes standing: Static UI 19.1-21.8 ms at 30.0 fps (before) -> 1.06-1.10 ms
  at 60.0 fps (after), `review/T462/pi/raw/{B2r,A2}_*`. The live buffer is 500
  messages, not 1,000. Original entry:
  **The static chat panel re-formats and word-wraps its WHOLE
  buffer every frame, so the static-UI cost grows with session length.**
  `renderChatPanel()` (`static_panels.cpp`) calls
  `formatMessageForDisplay()` on all `ChatMessageBuffer` messages (cap 1000)
  every frame. For each wrapped line it binary-searches `getTextWidth()`, and
  only then picks the visible lines. Measured on the Pi, standing still with
  the inventory closed (P1, Will's binary): `Static UI` went from 11.9 ms to
  22.0 ms over 5 minutes of guard-fight chat spam. Scene draw held at
  4–7 ms, so static UI was 69% of the frame. Fix belongs in a follow-up unit:
  wrap on insert or on width change, not per frame.
- ~~**B506**~~ (A) CLOSED by T462 (`592b2302`, `96590c78`). The account found
  the Pi runs the FALLBACK UI path (no atlas, board B540): every rectangle an
  immediate `draw2DRectangle()` and every text entry one `CGUIFont::draw()`;
  instrumented pre-fix at 1280x720, Static UI 17.9 / 26.0 ms closed/open, of
  which text flush 8.2 / 11.8 ms. Now both paths queue and flush runs of
  same-state quads as at most two draws (`UiQuadBatch`), and the text flush
  lays every plain entry's glyphs into the same runs. Pinned red-then-green by
  `tests/test_static_ui_frame_cost.cpp`, including exact pixel identity with the
  per-command, per-entry flush on the atlas and the fallback path. Pi, Will's
  flags, 1280x720, 3 closed/open cycles: before 30.0 fps in both states with
  Static UI 7.8 / 14.6 ms; after **60.0 fps in both states, Static UI 1.34 /
  2.46 ms** (text 2 draws, quads 15 / 38 draws), `review/T462/pi/raw/{B1rr,X1,A1}_*`.
  No render-target cache was needed. Original entry:
  **Opening the inventory costs +6–7 ms of static UI per frame on
  the Pi, and vblank quantisation turns that into Will's "halving".** Standing
  still, 15–20 s windows interleaved closed/open, wall-clock fps:
  - P2 (Will's binary): 48.4 closed → 37.9 open, static UI 11.1 → 17.0 ms.
  - P5 (slot binary): 38.2 → 30.0 fps, 10.7 → 17.7 ms.
  Scene draw is unchanged and endScene reads ~0 ms. Every open window in P5
  sits at exactly 30.2 fps (33.1 ms), and the first closed window in P2 at
  59.3 fps. `CIrrDeviceFB::drmPageFlip()` waits for the previous flip, a
  60 Hz vblank, so a frame over 16.7 ms is held to 33.3 ms. **Refuted:** the
  paperdoll RT as the cost. T459's timer in `updatePaperdollForFrame()`
  measures about 0.2 ms average; the RTT re-renders (~2.1 ms) about 1 frame
  in 12, plus a one-time model build of up to 243 ms on first open. The
  remaining ~6 ms is the popup draw submission and `UIRenderer::endFrame()`,
  which issues one `draw2DImage` per quad. It compounds with B505.
- ~~**B507**~~ (A) **CLOSED by T464 (2026-09-13).** Root cause: a FRAME
  mismatch, not a wrong or stale region. `regionBoundingBoxes` is in the EQ
  Z-up frame (`ZoneLoadComponent::loadZoneStep2_BSP()` builds it from EQ
  vertices; `SimBridgeComponent` and the zone debug draw read it that way), and
  the clamp read `MaxEdge.Y` -- a horizontal extent -- as the ceiling. Measured
  from nro's own BSP: region 1217 holds the player's feet, eye and camera at
  T459's pose and its box max is (2761.75, **-171.47**, 10.81), i.e. the logged
  "ceiling at -172.0" beside a player at y -181.57. The clamp now reads
  `MaxEdge.Z` of the player's EYE region (looked up by the controller itself),
  and a box top at or below the eye is no ceiling; at T459's pose the camera
  sits at 10.0. Pinned by `tests/test_camera_bsp_ceiling.cpp` (constructed
  cases red before the fix: 3.0 instead of 10.0; nro cases on the real zone).
  Original entry:
  **Follow camera BSP "ceiling" clamp uses an AABB whose top is
  far below the player, latching the camera at `eyeHeight*0.5`.**
  `CameraController::setFollowPosition()` reads
  `regionBoundsMap_[bspPlayerRegion_].MaxEdge.Y`. `bspPlayerRegion_` is fed
  from the SimulationWorker's `currentPvsRegion`, a frame late and only on a
  region change. In nro, P3 forward-run-1 logged `BSP camera ceiling:
  limiting height to 3.0 (ceiling at -172.0)` at 12:13:52.098, with the player
  at z −11 to +29. The camera height over the player dropped from 10.0 to 3.0
  and stayed there for the rest of both runs (630 frames). **Reproduces off
  the Pi:** the x86 `build-gles2` `/move` along the same path hit the same
  `ceiling at -172.0` near (2617,−183) for 42 frames. It is a latched state,
  not a frame-to-frame alternation. Camera territory (T461).
  **T462 addendum (1280x720, Will's resolution):** reproduced on the Pi with
  the fable-tip binary. `review/T462/pi/raw/C1_before_camera_trace_1280`,
  both keyboard forward runs from Testcaster's nro pose: horizontal follow
  distance constant at 20.0, height over the player 10.0 then 3.0 with
  exactly one change per run (`camera_runs.txt`), the same latch.
- **B508** (A, *unconfirmed*; **narrowed by T464**) **Will's "Follow camera jerks rapidly between → issue #38
  two perspectives while running forward" is NOT reproduced by injected
  keyboard forward runs.** T464 found and removed a two-state mechanism in the
  code: the ceiling clamp's region was the SimulationWorker's CAMERA region,
  i.e. the region of the position the clamp itself sets, fed back a frame late.
  Where a BSP split with a vertical component lies between the clamped (3) and
  unclamped (10) camera heights and the two regions' boxes straddle the
  threshold, the height alternated (a constructed BSP gave 3,10,3,10,3,10).
  On T459's own nro path that precondition holds at 0 of 201 steps, so it is
  not proven to be Will's jerk. The clamp is now a function of the player's pose
  alone; a remaining flip needs the POSE to alternate (B525 (b)). Measured:
  - P3: two 10 s runs, 800 frames, CAMERA:TRACE after every writer. Zero A-B-A
    flips in yaw, distance or height; zero backward player steps; zero bridge
    `setPlayerPosition` writes during either run (physics was the only camera
    writer).
  - P4: four 12 s legs on four headings, one camera state per leg.

  Real camera defects found on the way: B507 (latched height) and B521 (the
  `/move` heading flip, live-confirmed in P3: heading 437.17 → 76.39, yaw
  127° → −126°, on the Pi and on x86). T460's B525 (a) is measured by T459's
  post-fix run. Confirm with Will's own terrain and input, or a run where
  B525 (b) applies.

  **T462 addendum (1280x720, Will's resolution): still NOT reproduced.** The
  coordinator named resolution, and the lower frame rate it brings, as the new
  variable. Fable-tip binary (`review/T462/pi/bin_before/willeq`), Will's
  flags at `-r 1280 720`, Testcaster in nro, keyboard only (uinput W, D),
  Follow camera, `--log-module=CAMERA:TRACE`. Analysed by
  `review/T462/scripts/analyze_camera_runs.py` (camera and player put in the
  same axis frame first):
  - C1 (no dumps): run 1, 445 frames, frame dt 27.0 ± 6.8 ms (max 51);
    run 2, 562 frames, 21.4 ± 4.3 ms. Player step 1.31 / 1.04 per frame, zero
    zero-steps and zero backward steps. Follow distance 20.0 ± 0.01 throughout,
    zero yaw jumps over 8°, zero camera-offset A-B-A alternations. The only
    discontinuity is B507's 10 → 3 height latch, once per run.
  - C2 (frame dump every frame, 60 frames over a measured 56 s --
    `review/T462/pi/T462_pi_C2_before_follow_forward_1280x720_capture.mp4`
    and its `.txt`; capture fps is not performance): 19 and 21 traced frames
    in the two runs, height constant 10.0, distance 20.0, zero jumps.
  The live candidates are unchanged from T459: B507 on Will's terrain, and
  B525 (b).
- **B509** (A, *unconfirmed*) **"Spell gem bar missing the whole session" is → issue #39
  not reproduced in the rendered buffer.** On the Pi, DRM matched
  `800x600@60Hz` and GBM created an 800x600 surface. Dumped frames (P1, P4)
  and the x86 `build-gles2` contrast both show the gem column at x 764–796:
  A1 holds "Mino" (spell 288, Testcaster's only memorized gem) and A2–A8 are
  empty. **Refuted on this display:** the DRM mode-fallback lead. Remaining
  candidate: the physical display overscanning away the rightmost ~36 px,
  since the column sits 4 px from the edge. Readback cannot see that. Confirm
  by Will looking at the right edge of the physical screen.

  **T462 addendum (1280x720, Will's resolution; Will reports no overscan):
  the gem column is in the rendered buffer at 1280x720 too.** Fable-tip
  binary, Will's flags at `-r 1280 720`: the log reads `DRM: Found matching
  mode 1280x720@60Hz for requested 1280x720` and `GBM: Surface created
  1280x720 XRGB8888`. `UILayout::computeLayout(1280, 720)` -- the inline
  function `renderer_init.cpp` calls with the configured size -- puts
  `spellGems` at (1244,388)-(1276,672), gem 1 at (1244,388)-(1276,420) and
  gem 8 at (1244,640)-(1276,672)
  (`review/T462/ui_layout_rects_800x600_1280x720.txt`, printed from the same
  header). The in-world dump
  `review/T462/pi/gems/in_world_fullframe_1280x720_before_C2_frame30.png` and
  its 4x crop of x 1180-1280, y 370-690 show eight slot boxes in that column,
  the first labelled "Mino"/A1 and the rest A2-A8 (a description of the file,
  not a visual judgement). With no overscan and the buffer holding the column,
  what remains is something between the buffer and the panel: a scan-out
  crop/scaling of the 1280x720 mode by that display, or a session state that
  neither T459 nor T462 reproduced. The next discriminating capture is a photo
  of the physical screen next to a buffer dump of the same frame.
- **B510** (P) **Pi live input recipe: `/dev/uinput`.** The Pi normally has no
  USB keyboard or mouse attached. `review/T459/scripts/uinput_inject.py`
  (run with sudo) creates a keyboard (has `KEY_A`) and a mouse (has `REL_X`)
  before the client starts. `CIrrDeviceFB::initEvdev()` picks both, and
  presses follow the real evdev path. It triggers on `Graphics loading
  complete. Game ready!`. The obvious `Zone ready - player entity created`
  line never fires in these sessions, because `networkReady()` is false when
  the player entity is created. Two traps: never edit a bash script while it
  is running (P5 lost its copy step to that), and copy frame dumps with
  `cp -a`, because `encode_capture.sh` derives fps from mtimes.
- ~~**B511** (P) **x86 live runs from an NFS worktree stall at "Finalizing
  connection... (40%)".**~~ **RESOLVED by B751 / MR !29 (2026-09-23, fable 67526073):** without the capability the client now asks for `net.core.rmem_max` instead of a hard-coded 212992, and logs the granted buffer at INFO; the Pi zone-in stall this was the x86 face of is root-caused on #73 (42 % kernel drops of the zone-in burst). The setcap recipe still gives more (8 MiB forced) on a filesystem without `nosuid`. Original text: The dev VM's `net.core.rmem_max` is 212,992 (the Pi's
  is 1,048,576), and `setcap` fails on NFS ("Operation not supported"), so the
  1 MB `SO_RCVBUFFORCE` never happens. Recipe: copy the binary to a local
  filesystem, `setcap cap_net_raw,cap_net_admin+ep` it, and run it with
  `WILLEQ_DATA_ROOT` and the working directory set to the slot.
### Added by T456 (2026-09-08)

- **B479** (B) `src/client/graphics/entity_renderer.cpp:370` → issue #178
  `EntityRenderer::markEntityAsCorpse()` -- **the client synthesises a corpse
  display name that does not match the one the server uses.** It appends
  `"'s corpse"` to the visual's name, with NO entity id, guarded by a
  substring test for the same suffix. eqemu's `Corpse::CalcCorpseName`
  (`/mnt/projects/akk-stack/code/zone/corpse.cpp:629`) always appends the
  entity id -- `"<base>'s corpse<id>"` for a player, ``"<base>`s_corpse<id>"``
  for an npc -- which is exactly what `MakeCorpseName()`
  (`include/client/corpse_predicate.h`, T185) reproduces, digit-stripping and
  all. So a corpse whose death this client witnessed carries a name a player
  cannot reach by typing it, and one that arrives already named carries the
  server's. The substring guard is the one exemption in T456's census net
  (`CorpseRecognition.NoProductionSiteDerivesCorpsenessFromANameSubstring`),
  because it asks a different question -- "does this display name already
  carry the suffix" -- rather than deciding corpse-ness. Closes when the name
  comes from `MakeCorpseName()` (the visual is keyed by spawn id, so the id is
  in hand) and the exemption comes out of the test. **Wants an oracle check
  first**: what the official client puts on the nameplate of a corpse it
  watched die is measurable and has not been measured. Found by T456.

- **B480** (B, hygiene) `data/equipment_models.json` (1.1 MB, tracked) and → issue #179
  `tools/equipment_index_builder` -- **an input and its builder that now feed
  nothing.** T454 dropped `equipment_textures` and T456 dropped
  `equipment_models`, so `loadEquipmentModels()` is gone and this JSON is no
  longer read by any tool, script, test or client path (grep over `src/
  include/ tests/ tools/ benchmarks/ docker/ scripts/`: the only remaining
  hits are `tools/equipment_index_builder.cpp` writing it and two comments).
  It is not a broken route -- the equipment model and texture indexes both
  resolve LIVE from the 17 `*equip*.s3d` archives at zone load
  (`EquipmentModelLoader::buildEquipmentIndex()`), which is why both tables
  were droppable. T456 left the file and its builder in the tree deliberately:
  dropping a shipped INPUT is a different question from dropping a shipped
  TABLE, and B468's lesson is that each gets its own check. Closes when both
  are deleted, or a consumer is written. Found by T456.

---

### Added by T461 Leg A (2026-09-13, the official camera set)

- **B530** (C -> ORACLE-OWED, T461 Leg B) **The official cameras' GEOMETRY and → issue #40
  the user cameras' UNITS are not in any client file.** The files settle the
  set, the names and an order (`eqstr_us.txt` 9500-9505, `eqgame.exe`'s
  `FIRST_PERSON_CAMERA` .. `TETHER_CAMERA`) and that the user cameras are
  persisted (`eqclient.ini` `[Options]` `Camera1-*`/`Camera2-*`). They do NOT
  settle: the Overhead, Chase and Tether cameras' distance/pitch/height (fable's
  Chase is its old follow camera, unmeasured; Overhead and Tether are
  `kOverheadPlaceholderRig` / `kTetherPlaceholderRig` in
  `include/client/graphics/player/camera_rig.h`, marked ORACLE-OWED); whether a
  user camera holds its offset relative to the player's heading (as fable does)
  or to the world; the unit and zero of `Camera1-DirHeading` (192/277) and
  `Camera1-Pitch`; whether `Camera1-Height` is the look-at height or the
  camera's; what `Camera1-Zoom=90` and `Camera1-Change=1` mean; which camera a
  character logs in with. Method: `/camera set` A/B search against oracle stills
  per `match-the-oracle-dont-infer-it`. Found by T461.
  **Narrowed by T466 (2026-09-13)** -- settled on the oracle: the login camera is
  **First Person** (untouched login frame 0.232 vs First Person, >= 0.897 vs
  the rest); User 1/2 and Tether are **world-anchored**, First Person / Overhead
  / Chase heading-relative (B580); the official FOV is the **same in every
  camera** (Chase vs First Person scale 1.00), which fable now uses;
  **Overhead measured** (pitch 83, dist 45, height 3 -- `kOverheadRig`); the
  Chase pose measured as a rig (az 0, pitch 6, dist 4.0, height 7.0, edge 0.738,
  bracketed) but NOT applied, because fable's Chase is
  `CameraController::setFollowPosition()` (eye 6 + 4, 20 behind, look-ahead),
  not a rig; the ini keys are not read at launch (B585). **Still owed:** the
  Tether and User 1/2 geometry (coarse sweeps did not converge, and the rig
  cannot express a world azimuth -- B580), the Camera1-* units, the zoom steps.
  **Narrowed again by T471 (2026-09-14)** -- APPLIED: fable's login camera is
  First Person (`kDefaultCameraMode`; a saved `[Camera] View=` still wins), and
  the rig holds a WORLD azimuth for User 1/2 and Tether (B580 closed). **Still
  owed:** the Tether and User 1/2 numbers and which world direction they start
  at (B651), the Camera1-* units, the zoom / pitch / slide / raise steps (B653),
  and Chase: fable's follow camera does NOT reproduce the Chase fit (B650).

- **B531** (C -> ORACLE-OWED, T461 Leg B) **The official camera KEYS are not in → issue #41
  any client file.** `eqgame.exe` names the actions (`TOGGLECAM`, `ZOOMIN`,
  `ZOOMOUT`, `CENTERVIEW`, `PITCHUP`, `PITCHDOWN`, `LEFT_CAM`, `RIGHT_CAM`,
  `FORWARD_CAM`, `BACK_CAM`, `PITCHUP_CAM`, `PITCHDOWN_CAM`) and `eqstr_us.txt`
  labels some (6536 Toggle camera view, 6537/6538 Zoom in/out, 6692-6698 Raise /
  Lower / Tether-slide right / left / Pitch up / down / Center camera), but an
  `eqclient.ini` `[KeyMaps]` line exists only for a key the user CHANGED (T382).
  Owed: whether the official client's TOGGLECAM key is F9 (fable binds F9 on
  Will's ruling) and the order it cycles in; the zoom step and the Mouse Wheel
  Zoom step; the default keys of the four orbit/pitch actions fable added
  UNBOUND (`HotkeyAction::CameraOrbitLeft/Right`, `CameraPitchUp/Down`) and of
  raise/lower/tether-slide/center, which fable does not implement. Method:
  screenshot Options > Keys > **Camera** (`EQUI_OptionsWindow.xml`
  `OKP_KeyboardFilterCombobox` lists a Camera category), T377's method. Found
  by T461.
  **Narrowed by T466 (2026-09-13)** -- the oracle's Options > Keys > Camera page
  (`review/T466/oracle/T466_oracle_options_keys_Camera_page1_root_1024x768.png`):
  **Toggle camera view = F9** (fable's binding is correct), Zoom in NPAD7 /
  INSERT, Zoom out NPAD1 / DELETE, Raise Alt NPAD8 / Alt UP, Lower Alt NPAD2 /
  Alt DOWN, Tether-slide right Alt NPAD6 / Alt RIGHT, left Alt NPAD4 / Alt LEFT,
  Pitch up Alt NPAD9 / Alt PGUP, down Alt NPAD3 / Alt PGDOWN, the six direct
  cameras unbound; no Center-camera row on that page (Center view is NPAD5 /
  HOME under Movement). **Cycle order measured** (each direct camera + F9 scored
  against all six): First Person -> Overhead -> Chase -> User 1 -> User 2 ->
  Tether -> First Person, fable's order. No F9 message is printed (chat and a 2 fps
  OCR of the whole screen). **fable's orbit/pitch actions were NOT bound**:
  under the rig only Pitch up changed User 1 (tether-slide, raise and zoom
  scored at re-grab noise), and the official Pitch up tilts the view UP, the
  opposite of fable's `CameraPitchUp` (+pitch raises the camera and looks down),
  so binding Alt+PgUp to it would ship a mirrored key. Alt+Left/Right and
  Alt+PgUp/PgDn collide with no fable binding (both lookups match modifiers
  exactly). Still owed: the tether-slide / raise / zoom effect and step.
  **Narrowed by T471 (2026-09-14)** -- every measured key is BOUND, in
  `setupDefaults()` and `config/hotkeys.json`: Zoom in Numpad7/Insert, Zoom out
  Numpad1/Delete (beside fable's +/-), Raise Alt+Numpad8/Alt+Up, Lower
  Alt+Numpad2/Alt+Down (new `CameraRaise`/`CameraLower`), Tether-slide right
  Alt+Numpad6/Alt+Right and left Alt+Numpad4/Alt+Left (`CameraOrbitRight/Left`,
  which ARE `RIGHT_CAM`/`LEFT_CAM`), Pitch up Alt+Numpad9/Alt+PageUp, down
  Alt+Numpad3/Alt+PageDown. Census before binding: no fable binding used any of
  those key+modifier pairs; the bare arrows (movement) and PageUp/PageDown (UI
  scroll) keep their meaning, because `getAction()` and the movement poll both
  compare modifiers exactly (`T471_TheMeasuredCameraKeysCollideWithNothing`).
  **The "mirrored Pitch up" above was WRONG:** fable's `CameraPitchUp` arm has
  applied `orbitCamera(0, -step)` since T461 (commit `a91411bb`, never changed),
  which LOWERS the camera and tilts the view up -- exactly the oracle's Alt+PgUp
  (the scene moved down the screen). T466 read the action name, not the arm.
  `PitchUpTiltsTheViewUpAsTheOfficialKeyDoes` passed before any T471 change and
  pins it. F10, F11, L and Alt+C untouched (B582). Still owed: what Raise,
  Lower and Tether-slide move on the official client and every step (B653).

- ~~**B532**~~ (A) **CLOSED by T464 (2026-09-13)** -- confirmed from code with a
  red test, not live. Both loaders now call
  `PlayerCameraComponent::placeCameraForZoneIn()`, which RE-ENTERS the current
  mode (so first person's name tag and projection are re-applied for the new
  zone) instead of forcing Chase; the Chase default for a character with no
  saved view moved to the identity load in `FrameComponent::syncUiPreferences()`.
  `tests/test_camera_zone_in_preference.cpp`: User 1, First Person and an
  in-session User 2 survive a second zone load with the ini unchanged (all three
  red before the fix -- Chase, and `View=4` gone); a second character with no
  file still gets Chase and writes nothing. Original entry:
  **Every zone load forces the camera back to
  Chase, and the persistence then saves that over the player's choice.**
  `Application`'s two zone-graphics loaders (`application_zone_loading.cpp`,
  step "7. Camera") call `setCameraMode(Chase)` on every zone load, while
  `FrameComponent::syncUiPreferences()` loads the ini only once per
  character/server identity (`setUiPreferencesIdentity()` returns early on the
  same identity). So after the first zone change a player in first person or a
  user camera is put back in Chase, and the next save-on-difference writes
  `View=3`. Pre-dates T461 (it did the same with `Follow`); read from the code,
  not seen live. Confirm by: zone in, `/camera user1`, zone through a zone line,
  `/camera` and read `UI_<char>_<server>.ini`. Found by T461.

- **B533** (B, unreached) **What T461's deletion of the free-fly camera left → issue #180
  write-only.** T461 deleted `CameraController::update()` with the Free mode it
  was the body of. That made two SETTERS pointless: `setMoveSpeed()` and
  `setMouseSensitivity()` are still called once from `IrrlichtRenderer`'s init
  (`irrlicht_renderer.cpp`), but `update()` was the only reader of
  `moveSpeed_` and `mouseSensitivity_`. `WriteOnlyFields` flagged
  `mouseSensitivity_` and it is exempted against this entry in
  `scripts/refactor/check_write_only_fields.py`. The GETTER half of the same
  residue -- `getMoveSpeed()`, `getRotateSpeed()`, `setRotateSpeed()`,
  `getYaw()`, `getPitch()`, `setOrientation()`, `getHeadingEQ()`,
  `setOrbitAngle()` / `getOrbitAngle()` / `adjustOrbitAngle()` -- was ALREADY
  zero-caller before T461 and is **B494**'s CameraController group (exempted in
  `check_zero_callers.py`); T461 did not add to it. Work the two together:
  deleting the CameraController fly/orbit state is one change touching
  `camera_controller.*` and the two init lines. Not done by T461 because those
  lines were T460's territory in that wave. Found by T461.

- **B534** (B, unreached) **The output-layer camera route is a second, stale → issue #181
  camera vocabulary.** `output::CameraMode` (`include/client/output/output_renderer.h`)
  is still `Free, Follow, FirstPerson`; `GraphicalMode::processInput()` consumes
  `InputAction::ToggleCameraMode` into `GraphicalMode::cycleCameraMode()`, which
  calls `IOutputRenderer::cycleCameraMode()`'s empty default because
  `IrrlichtRenderer` does not derive from that interface, and
  `GraphicalRenderer::cycleCameraMode()` / `getCameraModeString()` rotate the
  old three. Nothing a player reaches goes through it. The same shape B043
  recorded for wireframe/HUD/name tags. Found by T461.

- ~~**B535**~~ (A) **CLOSED by T464 (2026-09-13).** Contract established: a
  file that loads REPLACES the defaults, which are only the fallback for an
  unreadable file (`loadFromFile()` returns before clearing on an open/parse
  failure; `saveToFile()` writes the full set). So the file now carries all of
  them: Alt+C, L, F10, Shift+F10, Shift+F11 added.
  `HotkeyShippedFileTest.T464_TheShippedFileCarriesEveryDefaultBinding` compares
  the tables binding by binding through `Application::loadConfigFile()`'s exact
  sequence (red before: those five). F11 is unchanged: `getAction()` prefers
  the Global `ToggleAllUI`, so F11 toggles the UI with or without the file, and
  the file binds `MusicVolumeUp` nowhere -- the one exemption, oracle-owed as
  **B561**. Census correction: FIVE bindings were missing, not six (F11 was
  never reachable as music-up). Original entry:
  **`config/hotkeys.json` drops six default bindings in
  production.** `Application::loadConfigFile()` calls
  `HotkeyManager::resetToDefaults()` and then `loadFromFile("config/hotkeys.json")`,
  and `loadFromFile()` CLEARS every binding before reading the file. The file
  has no entry for `ToggleCombatAbilities` (Alt+C, the oracle's measured key,
  T415), `CycleObjectLights` (L), `MusicVolumeUp/Down` (F11/F10) or
  `EffectsVolumeUp/Down` (Shift+F11/F10), all of which `resetToDefaults()`
  binds -- so those keys do nothing in a real run while every
  `resetToDefaults()`-based test passes. Census: the actions named in
  `addBinding(HotkeyAction::X` calls minus the JSON's keys, so it is a LOWER
  bound (a default bound by any other spelling is not counted). Note F11 is
  also `ToggleAllUI`, so restoring MusicVolumeUp needs a decision. T461 put F9
  in both places and pinned both (`test_hotkey_manager.cpp`
  `T461_TheShippedHotkeysJsonAgreesF9`). Found by T461.

## Added 2026-09-13 by T464 (camera and key-binding live defects)

- ~~**B560**~~ CLOSED by T467 (`refactor/T467_tumbleweed_lazy_init_and_session_identity.md`
  Review). **Confirmed in `FrameComponent`, refuted as a live path.** Red:
  `tests/test_ui_preferences_identity.cpp` -- after an identity change to a
  character with no file, the unfixed renderer held the previous character's
  skills / combat-ability / Options windows open, group / pet / buff closed,
  sliders 3/4/17 and both user rigs. Carry-over fields: all seven toggleable
  windows plus `[OptionsWindow]`, the three `[Options]` sliders and the eight
  `User1*`/`User2*` rig values (the camera MODE was already reset by T464). Not
  reachable today: one process logs in one character
  (`ApplicationConfig::character`, passed once to the single `EverQuest` built
  in `Application::initEverQuestClient()`), and `/camp` ends the process
  (`PlayerComponent::ZoneProcessLogoutReply()` -> `RequestQuit()`), so the
  "`/camp` to character select" confirm step does not exist. Fix: an identity
  change FROM a character arms a reset (`uiPrefsResetPending_`) and the next
  `syncUiPreferences()` applies `UiPreferences{}` and the default rigs before
  reading the new file, then applies the three Options settings. The first
  identity of a run is not reset. Original entry, kept for the record:
  **A second character in the same process
  inherits the first one's window states and user-camera rigs.**
  `FrameComponent::syncUiPreferences()` starts the load from `collect()` -- what
  the client currently holds -- so a key the new character's file does not
  state, or every key when it has no file, keeps the PREVIOUS character's value
  rather than the panel struct's default. T432's comment calls that "keeps the
  panel struct's own default", which is only true for the first identity of a
  run. T464 fixed the camera MODE half (it is now reset to Chase before the
  load); the seven `Show=` windows and `User1*`/`User2*` rigs still carry over.
  Confirm by: `/camp` to character select, log in a second character with no
  `UI_<char>_<server>.ini`, look at the windows and `/camera get`. Found by T464.

- **B561** (C, ORACLE-OWED) **F11 is bound to two actions in the defaults.** → issue #42
  `HotkeyManager::setupDefaults()` binds F11 to `ToggleAllUI` (Global) and
  `MusicVolumeUp` (Player); `getAction()` prefers Global, so F11 has only ever
  toggled the UI, and `config/hotkeys.json` binds `MusicVolumeUp` nowhere. T464
  kept that and exempted exactly this binding in
  `T464_TheShippedFileCarriesEveryDefaultBinding` (the exemption fails once it
  matches nothing). Settle with the official keymap: Options > Keys (T461
  Leg B / T377's screenshot method) for "Toggle all UI" and "Music volume up".
  Found by T461 (B535), boarded by T464.
  **Settled by T466 (2026-09-13), fix owed as B582:** the oracle's UI category
  lists **F10 = Toggle UI Display** and **F11 = Toggle netstat**; no volume key
  appears on the Camera, UI or Commands pages. So neither of fable's F11 meanings
  is the official one. Re-classed to the decision in B582.

- ~~**B562** (B) **The shipped `config/hotkeys.json` binds eight actions the → issue #182
  compiled-in defaults do not**: `CancelDialog`, `CancelInput`,
  `ChatAutocomplete`, `ConfirmDialog`, `ResetUIDefaults`, `SaveUILayout`,
  `ToggleUILock` (all Global) and `ToggleZoneLights` (Ctrl+F4). B535's converse:
  when the file cannot be read (`loadFromFile()` logs an ERROR and the defaults
  stay in effect) those keys vanish. Census by action name, T464
  (`addBinding(HotkeyAction::X` in `setupDefaults()` vs the file's keys). Fix
  shape: add them to `setupDefaults()` (some may be dead actions -- check each
  has a consumer first) and extend the T464 test to compare both directions.
  Found by T464.~~ **STRUCK (2026-09-28 audit-hygiene pass): fixed by MR !34
  (`src/client/input/hotkey_manager.cpp` `setupDefaults()` now carries all
  eight bindings), pinned by
  `T464_TheShippedFileCarriesEveryDefaultBinding`, issue #182 closed.**

- ~~**(B, needs a GitLab issue)** MR !34's fix commit notes that 7 of the 8
  actions it added to `setupDefaults()` still have no dispatch consumer:
  `hotkey_manager.cpp:476` says "ToggleZoneLights is the ONE of the eight
  with a dispatch consumer" (`RendererEventReceiver` -> `RA::ToggleZoneLights`).
  `CancelDialog`, `CancelInput`, `ChatAutocomplete`, `ConfirmDialog`,
  `ResetUIDefaults`, `SaveUILayout` and `ToggleUILock` are bound keys with
  nothing behind them -- the B562 fix made the binding table match the
  shipped file, it did not wire the seven actions to anything. Remains open.~~
  **STRUCK (UIGAP-small, B905 / #225): the seven -- and `SubmitInput`, found
  by the new census -- were DELETED (no oracle counterpart; as Global
  bindings they shadowed Tab/Escape/Return), Ctrl+Q `Attack` got its receiver
  arm, pinned by `tests/test_hotkey_dispatch_census.cpp`.**

- **B563** (P) **The region box a camera or sim reads is built in one place
  and re-derived in a test.** `tests/test_camera_bsp_ceiling.cpp` copies
  `ZoneLoadComponent::loadZoneStep2_BSP()`'s box loop statement for statement
  to measure nro's real boxes, because the loop is inline in the step body. A
  free function (`ComputeRegionBoundingBoxes(const WldLoader&, const BspTree&)`)
  would let the test call the production code and document the EQ Z-up frame
  at its source -- the frame B507 got wrong. Not done by T464: `zone_load/` was
  outside its territory. Found by T464.

---

## Added 2026-09-13 by T462 (static UI per-frame cost)

Evidence for every entry is under `review/T462/` (README there).

- **B540** (C) **Will's Orange Pi recipe runs the whole static UI on → issue #43
  `UIRenderer`'s placeholder FALLBACK path: no UI atlas art and no item
  icons.** `renderer_init.cpp` opens `willeq.eqe1` and loads `UIAtlas` (and
  with it T430's item icon sheets) only when `--eqe1-path` is set, and the
  recipe in `review/pi/README.md` (`--drm --renderer gles2 --constrained
  orangepi -r ...`) sets none. Measured: neither T462 Pi log of the fable-tip
  binary (`B1rr`, `X1`) contains `UIAtlas: loaded`, and the instrumented
  pre-fix binary reports 0 commands reaching `UIRenderer::endFrame()` every
  frame -- the fallback draws immediate `draw2DRectangle()`s instead. So the
  UI Will judges on the Pi is dark placeholder boxes with text abbreviations,
  not the oracle art the x86 captures and the visual gate show. Whether the Pi
  recipe should pass `--eqe1-path` (and pay for the pack under the orangepi
  budgets, `docs/decisions.md` EQE1 all-or-nothing) is Will's call. T462 made
  both paths cheap; it did not change which one runs.
- **B541** (P) **Two Pi-rig traps, each of which silently wastes a run.**
  (1) A binary copied OUT of a checkout (e.g. `review/<UNIT>/pi/bin_before/`)
  has no `config/`/`data/` tree within three parents, so `ResolveDataPath()`
  finds nothing and the client exits at startup with `Required files missing:
  config/constrained_presets.json, data/item_models.json` -- while the uinput
  injector sits out its 240 s trigger deadline. Set `WILLEQ_DATA_ROOT` to the
  checkout (`review/T462/scripts/run_pi.sh` does). (2) Without
  `--log-level=INFO` the client logs nothing at INFO, so neither the injector's
  trigger (`Graphics loading complete. Game ready!`) nor `FRAME TIMING
  BREAKDOWN` ever appears: the run plays no input and yields no data. T462
  lost four of its twelve Pi invocations to these two. Also: `pkill -f
  <pattern>` over ssh matches its own remote command line and kills the ssh
  session (exit 255); use `pkill -x willeq` and a `[u]input` bracket pattern.

---

## Added 2026-09-13 by T465 (the HeadlessShutdownTest load flake)

- ~~**B570** (P) **`HeadlessShutdownTest.NonLifecycleCommandIsHeldNotDroppedOrApplied`
  fails in loaded `ctest -j6` runs and passes standalone.** Every verifier
  re-ran it by hand; three merged tips on 2026-09-13 each had it as the only
  red.~~ **CLOSED by T465** (`refactor/T465_headless_shutdown_load_flake.md`).
  The test ran the client under `timeout 5` and failed when the client had not
  printed its `CMD outcome=held` ack within 5 s of spawn. The three recorded
  failures show two shapes: no output at all (T461's run, test 115 of 4762,
  right after a build) and the console banner with no ack line after it (the
  T456 and T463 chain runs). Neither is a client defect. The held command is
  only ever peeked, never consumed, and a stuck client SIGTERMed by hand exited
  in 0.16-1.7 s (three of three). Injecting the two stalls into the unmodified
  test reproduces both shapes: 6 s before start, 5/5 fail; 6 s after the
  banner, 10/10 fail. The rewritten test fork/execs the client, waits on the
  ack line itself with a 120 s hang bound (one NFS `timeo=600` retransmit plus
  margin), then asserts SIGTERM exits 0. Against the same injected stalls it
  passed 5/5 and 10/10. Its config moved from the fixed
  `/tmp/t30_headless_shutdown_test/` to a per-process `mkdtemp()` directory.
  The `sqlite rc=14` shape in EXECUTOR_PROTOCOL (T360) was a misreading:
  before T399 the client opened the bare relative `data/willeq.db`, which
  cannot exist in ctest's working directory, so that line printed on every run.

- ~~**B571**~~ **CLOSED by T477.** All seventeen names below, plus twenty
  more sites spelled as bare `"/tmp/..."` literals that this entry's
  `temp_directory_path` census could not see (three in `test_eqe1_loader.cpp`,
  fourteen in `test_constrained_renderer.cpp`, one in `test_pcap_step_through.cpp`,
  and the pack fixtures of `test_eqe1_alpha_companion_mip.cpp` /
  `test_alpha_blend_companion.cpp`), now live in a per-process directory:
  `willeq_test::ScopedTempDir` (`tests/scoped_temp_dir.h`, `mkdtemp()`,
  removed with its scope). The pcap step-through log keeps a `getpid()`
  suffix instead, because reading it afterwards is that case's purpose.
  `TmpPathCensus` (`tests/test_tmp_path_census.cpp`) re-takes the census on
  every ctest run over every C++ source in `tests/` and fails on a temp path
  whose statement carries no `getpid`, `mkdtemp`/`XXXXXX` or `ScopedTempDir`;
  five waivers, each with a reason, cover paths that never touch the
  filesystem. Original entry:
  **Seventeen fixed `/tmp` names are shared by every
  worktree slot that runs the suite.** `/tmp` is the host's local disk, one
  directory for slot1..3, t1, t2 and `main`, and each of these tests removes
  and rewrites a name with no pid or test-name in it, so two slots running
  the same test at once write the same file:
  `t376_clicks_recorded.sleq` (`test_click_senders_packet_sender.cpp`),
  `t375_op_report_shape.sleq` (`test_op_report_shape.cpp`),
  `t360_spawn_appearance_recorded.sleq` and `t360_death_burst_recorded.sleq`
  (`test_self_death_sequence.cpp`), `t379_login_recorded.sleq` and
  `t379_world_recorded.sleq` (`test_login_world_senders_packet_sender.cpp`),
  the five `t378_*_recorded.sleq` (`test_direct_senders_packet_sender.cpp`,
  `WithRecordedSession`), `willeq_t171_roundtrip.json` /
  `willeq_t171_coexist.json` / `willeq_t171_missing.json`
  (`test_inspect_answer.cpp`), `t70_truncated_spells.eff`
  (`test_spells_eff_reader.cpp`), and the output DIRECTORIES
  `p47_entity_viewer_regression` (`test_entity_viewer_regression.cpp`) and
  `t284_composite_open` (`test_eqe1_composite_open.cpp`). No red has been traced
  to any of them; the window is a recorder file removed or reopened under a
  reader in another slot. Every other `temp_directory_path()` user in `tests/`
  already appends `getpid()` (census: `grep -n temp_directory_path tests/*.cpp`,
  2026-09-13). The fix is the same one-liner per site T465 applied to
  `test_headless_shutdown.cpp` (`mkdtemp()`, or a `getpid()` suffix). Not done
  by T465: nine files outside its territory. Found by T465.

---

## Added 2026-09-13 by T467 (lazy environment managers, UI identity reset)

- ~~**B590**~~ **CLOSED by T468** (door texture eviction guard never
  registered). Confirmed for LRU eviction, refuted for zone unload.
  - **Confirmed:** `tests/test_door_texture_eviction.cpp`,
    `AHiddenDoorNeverHoldsAFreedTextureUnderBudgetPressure`, uses the door
    manager `setupInstantScene()` really builds and a real textured door mesh.
    With the door PVS-hidden (pivot detached), an LRU eviction left its node
    material AND its cached mesh buffer naming a texture the driver no longer
    listed. That is a use-after-free, drawn again when PVS shows the door.
  - **Refuted for unload:** `unloadZone()` runs `clearDoors()` (every door node
    and the door mesh cache dropped) before `ConstrainedTextureCache::clear()`,
    so no door material survives into `clear()`. Pinned by
    `DoorsDrawTheirRealTextureAfterAZoneUnloadAndTheNextZoneBuild`.
  - **Fixed:** `ZoneLifecycleComponent::ensureDoorManager()` is now the ONE
    `DoorManager` construction body and registers the guard;
    `setupInstantScene()` and `loadZoneStep7_Doors()` both call it. The
    uncalled `IrrlichtRenderer::loadGlobalAssets()` is deleted, and its
    `check_zero_callers.py` row with it.
  - **Pinned** by `DoorManagerConstructionSource.TheDoorManagerHasOneConstructionSite`
    and a new marker on `TextureCacheHolderCensus`'s `zone_geometry.cpp` row.
  - The stale ParticleManager comment is corrected.

B591–B599 unused.

---

## Added 2026-09-13 by T468 (door texture guard)

- **B620** (B, *unconfirmed live*) **A zone-geometry texture evicted by the LRU → issue #183
  stays the placeholder for the rest of the zone, and a door's cached mesh
  hands that placeholder to later doors.**
  - `RegionMeshTextureGuard` and (since T468, registered) `DoorTextureGuard`
    swap an evicted texture to the placeholder. Nothing re-uploads it: the
    `GPUUpload` door and region rebuild queues fire only for a texture that
    ARRIVES, not one that was evicted. The guard's own WARN says "until the
    texture is re-uploaded".
  - For doors it is worse: `DoorManager::doorMeshCache_` keeps the swapped
    mesh, so a door of the same model built later in the zone
    (`getNearestDoor()`'s demand build, the door rebuild queue) reuses the
    placeholder even though the texture is loadable.
  - Not a use-after-free; T468's tests allow the placeholder on purpose.
  - Confirm by: an eviction count on the Pi in a door-heavy zone. P13/P13b
    measured zero evictions in four configurations, so this may be
    unreachable at today's budget.
  - Found by T468.

- ~~**B621**~~ **CLOSED by T470** (`DoorManager` leaks door meshes). Both
  boarded paths CONFIRMED, plus a third, larger one T468 did not name.
  - **Measured** with `getReferenceCount()` on the real `DoorManager` that
    `setupInstantScene()` builds (EDT_NULL device, real cache), in
    `tests/test_door_texture_eviction.cpp`. Holders other than the test's own
    grab, before -> after the fix:
    - `ARegisteredDoorsPlaceholderMeshDiesWithItsNode`: the `registerDoor()`
      placeholder (every door of every zone load) 2 -> 1 while drawn, 1 -> 0
      after Step 7's rebuild destroyed its node. **Not in B621's text.**
    - `AMeshBuiltWhileItsTextureIsPendingDiesWithItsDoor`: 2 -> 1, then 1 -> 0
      after `clearDoors()`.
    - `InvalidatingTheDoorMeshCacheReleasesTheCachesReference` (by name,
      lowercase name and `""`): 2 -> 1 after the invalidate, 1 -> 0 after
      `clearDoors()`.
    - `ACreatedDoorsPlaceholderDiesAtTheMeshSwap`: the `createDoor()`
      placeholder 2 -> 1, and 1 -> 0 after Path 2's `setMesh()` swap.
    - `DoorsSharingACachedMeshReleaseItExactlyOnceAtZoneChange`: green before
      and after (3 nodes + cache = 4, then 0 at `unloadZone()`). It pins that
      the fix does not over-drop.
  - **Fixed** with one ownership rule, written on `doorMeshCache_`
    (`door_manager.h`): `findDoorMesh()` / `createPlaceholderMesh()` return
    ONE caller-owned reference, cached or not, and the caller drops it once
    the node has grabbed its own. The cache takes its own `grab()` in
    `cacheDoorMesh()` and releases it in `invalidateMeshCache()` and
    `clearDoors()`. Before, the cache borrowed the creation reference and the
    callers dropped only on failure branches.

- ~~**B622**~~ **CLOSED by T470** (`Application::loadZoneGraphics()` had no
  caller). Deleted, with its declaration.
  - Zero callers in `src/ include/ tests/ tools/ benchmarks/ docker/`. The full
    `build/` tree (tools included) links without it.
  - Cascade census, two steps:
    - Step 1: every call it made (`setZoneLineBoundingBoxes`,
      `setupInstantScene`, `storeZoneEnvironment`, `registerEntity`,
      `registerDoor`, `placeCameraForZoneIn`, `SetPlayerGraphicsEntityPending`,
      `getZoneBspTree`, `CreateZoneLoadSnapshot`, `m_pendingBspTree*`,
      `m_graphicsCompleteReady`) is also made by `loadZoneGraphicsOnThread()`,
      `checkLoadingComplete()` or `application.cpp`.
    - The one exception is `IrrlichtRenderer::isProgressiveLoadingActive()`.
      It was left with test callers only, and `ZeroCallers` failed on it as a
      NEW tests-only function, so it was deleted too. Its two tests
      (`test_zone_load_component`, `test_zone_stream_component`) now read
      `Context().progressiveLoadingActive`, the same member.
    - Step 2: `progressiveLoadingActive_` has many live readers. Fixed point.
  - T464's `placeCameraForZoneIn()` call in the live loader is kept, and
    `CameraZoneInSourceTest` now counts 1.

B623–B629 unused.

---

## Added 2026-09-14 by T470 (door mesh leaks)

- ~~**B640**~~ **CLOSED by T475 (2026-09-14)**, with B670: the arrival path
  that alone could ask `rebuildSingleDoor()` to re-texture a real door mesh is
  deleted, so the missing branch has nothing to serve. The gap would come back
  only with an async texture producer, and
  `TextureArrivalPathCensus.NoHalfOfTheDeletedArrivalPathIsBackInCode`
  (`tests/test_door_texture_eviction.cpp`) fails on that and says the branch is
  owed. What a player can see is still B671. (Original text below.)
  ~~(B, unreached -- narrowed from A by T473) A door whose real
  mesh was built while its texture was pending is never re-textured.~~
  - **T473: the live half is REFUTED.** The gap in `rebuildSingleDoor()` is
    real, but nothing can ask it to re-texture such a door. The arrival hop
    (`renderer_gpu_upload.cpp`, a `Texture`/`CompressedTexture` result whose
    `callbackKey` high byte is 3) has had no producer since Batch T's T04
    (`aaee58d1`) wrapped the cache's async upload queue in `#if 0`, and T326
    (`77c842b4`) deleted it. Nothing under `src/ include/ tools/ tests/
    benchmarks/ docker/` now sets that key, and nothing writes
    `pendingAsyncUploads_`. So `doorTextureRebuildQueue` is never pushed, and
    Priority 8 / P1.6 never run. Details: B670.
  - Pinned by `DoorTextureArrivalSource.NoTextureUploadIsSubmittedWithTheArrivalKey`
    (`tests/test_door_texture_eviction.cpp`). It fails, and names this entry,
    the moment an async texture producer returns; a re-texture branch is owed
    then, not before.
  - Closes with B670: deleting the dead arm closes this too. What a player can
    actually see (a door built without its texture stays untextured) is
    B671.
  - (T470's original text follows.)
  - `ZoneLoadComponent::loadZoneStep7_Doors()` queues the door into
    `pendingTextureDoors()`. On arrival, `renderer_gpu_upload.cpp` calls
    `invalidateMeshCache()` and pushes the id to `doorTextureRebuildQueue`,
    which `renderer_frame.cpp` (Priority 8) and `renderer_zone_stream.cpp`
    (P1.6) drain through `DoorManager::rebuildSingleDoor()`.
  - `rebuildSingleDoor()` does nothing for that door: Path 1 needs
    `!meshBuilt` and Path 2 needs `usePlaceholder`, and a door with a real
    mesh has `meshBuilt=1, usePlaceholder=0`.
  - Measured by a throwaway T470 probe on the B621 seam: `rebuilt=0`, the mesh
    unchanged, and the node texture `nullptr` both before and after the
    texture arrived.
  - Unconfirmed live: nobody has measured whether a door texture is ever still
    pending at Step 7 in a real session.
  - Found by T470.

B641–B649 unused.

---

## Added 2026-09-14 by T473 (door texture arrival)

- ~~**B670**~~ **CLOSED by T475 (2026-09-14): deleted, not restored**
  (orchestrator's decision; Will's standing decision #1 makes load time
  secondary, so there is no case for an async texture producer). Deleted, each
  with zero producers and readers proved first:
  - the `Texture`/`CompressedTexture` arm of `processCompletedUploads()` /
    `processOneGPUResult()` and its sort, the two texture request types, both
    worker bodies and every texture field of `UploadRequest`/`UploadResult`;
  - `pendingTextureRegions_`, `textureRebuildQueue_` + `TextureRebuildEntry`,
    `pendingTextureDoors_`, `doorTextureRebuildQueue`, `pendingTextureObjects`,
    `objectTextureRebuildQueue` (the context references, renderer members,
    unload clears, the progressive-status log fields and completion gate);
  - FrameComponent Priority 8/9, ZoneStreamComponent P1.6/P1.7 and the byte-3
    reprioritise branch;
  - `ConstrainedTextureCache::setGPUUploadThread()`, `clearPendingAsync()`,
    `gpuUploadThread_`, `pendingAsyncUploads_`;
  - the cascade: `ZoneStreamComponent::buildDeferredObject()` (its only
    callers were the drains), then `ZoneLoadComponent::treeIdentifier()` (its
    only caller was that body), and `TextureAtlas::uploadPreloadedPageAsync()` /
    `setPageTexture()` (zero callers, B494 rows; the last texture submitter).
  - Net: `TextureArrivalPathCensus` (two cases). Details in
    `refactor/T475_delete_dead_texture_arrival_path.md`'s Review. (Original
    text below.)
  ~~(B, unreached) The whole GPU texture-ARRIVAL arm has had no producer
  since March, and so has everything downstream of it.~~
  - **The arm.** In `ZoneLifecycleComponent::processCompletedUploads()` and its
    copy `processOneGPUResult()` (`renderer_gpu_upload.cpp`), the `sourceType == 3`
    branch of a `Texture`/`CompressedTexture` result:
    - `registerTexture` + `clearPendingAsync`;
    - `EntityRenderer`'s `registerUploadedTexture`;
    - the region, door and object hand-offs.
  - **The producer is gone.** `callbackKey = uint64_t(3) << 56` was set only by
    `ConstrainedTextureCache`'s async upload queue, which also wrote
    `pendingAsyncUploads_`.
    - Batch T's T04 (`aaee58d1`, 2026-03-16) wrapped that queue in `#if 0` when
      it made `getOrLoad()` + `uploadTexture()` synchronous.
    - T326 (`77c842b4`) deleted the block.
    - Today's only `GPUUploadThread::submit()` callers are the atlas pages
      (`TextureAtlas`, key `atlasType << 32`, high byte 0) and region VBOs
      (`VertexBuffer`, a type the arm does not take). Grepped `src/ include/
      tools/ tests/ benchmarks/ docker/`.
  - **Dead downstream, each written but never consumed (or the reverse):**
    - `pendingTextureRegions_` -> `textureRebuildQueue_`: its consumer was
      already removed by T67, so this one is dead at both ends;
    - `pendingTextureDoors_` (written by Step 7) -> `doorTextureRebuildQueue`
      -> `renderer_frame.cpp` Priority 8 and `renderer_zone_stream.cpp` P1.6;
    - `pendingTextureObjects` (written by Step 6 and `buildDeferredObject`)
      -> `objectTextureRebuildQueue` -> Priority 9 and P1.7;
    - `ConstrainedTextureCache::setGPUUploadThread()` / `gpuUploadThread_`:
      written, never read;
    - `pendingAsyncUploads_`: read by `isPending()` / `hasPendingWork()`,
      never written.
  - **Harmless today, not free.** The three pending maps grow with every
    missing texture until `unloadZone()`. `hasPendingWork()` and the two
    rebuild-queue reads gate the end of progressive loading, on state that
    can never become true.
  - **Decision owed:** delete the arm and its queues (B640 closes with it), or
    restore an async producer (B640 becomes live, and its re-texture branch is
    owed). The tripwire is
    `DoorTextureArrivalSource.NoTextureUploadIsSubmittedWithTheArrivalKey`.
  - Found by T473.

- **B671** (A, *unconfirmed live*) **A zone-geometry mesh built without its → issue #44
  texture draws that way for the rest of the zone, and some paths do not even
  record it.** This is the reachable form of B640, and a sibling of B620.
  - **How a texture comes out missing** in `ZoneMeshBuilder::buildTexturedMesh()`
    with a synchronous cache (it "arrives" never):
    - `uploadTexture()` fails because the budget is exhausted;
    - the decode fails;
    - the name is not in the zone's texture map;
    - the pixel data was released and the texture is no longer cached (LRU
      eviction). Object pixel data is released when progressive loading
      completes (`renderer_zone_stream.cpp`), and all zone data at Step 13
      when `releaseTextureDataAfterUpload` is set.
  - **Holders that can end up that way:**
    - **doors:** Step 7 (logged), and `createDoor()`, IrrlichtBridge's live
      spawn, which records nothing;
    - **objects:** Step 6 (logged). `buildDeferredObject()` was deleted by T475:
      it was reachable only from the dead arrival drains;
    - **regions:** `rebuildRegionMesh()` (logged).
    - T475 deleted the dead maps these used to feed (B670); the three log lines
      stayed, reworded to "missing textures (not re-textured later, board
      B671)", so the grep below still matches.
  - **What it looks like:** not measured. The material's texture slot is
    null, so whatever the GLES2 driver binds for a null unit 0 is drawn,
    not the model's art.
  - **T470's "pending" test state models this, not async:** its door texture
    has its data cleared and is not in the cache.
  - **Confirm by:** a Pi session log grepped for "SEQ Step7: door .* missing
    textures", "SEQ Step6: obj.* missing textures" and "rebuildRegionMesh: .*
    missing textures". The visual gate runs `gles2_zone_benchmark`, not the
    client's Step 6/7, so it cannot answer this.
  - Found by T473.

B672–B679 unused.

---

## Added 2026-09-14 by T475 (texture-arrival path deleted)

- ~~**B690**~~ **CLOSED by T477.** All three remnants are deleted:
  `AssetType::ZoneTexture` (value 1 is now a documented gap; the other values
  are explicit, so no key moved), the `gpu_upload_thread.h` /
  `work_priority.h` include block in all four `constrained_texture_cache*.cpp`
  TUs, and the fmt format header in `texture_atlas.cpp`. The enumerator was not
  positionally significant: `WorkPriorityKey` is compared in-process only,
  never persisted, serialised, table-indexed or decoded (the one `>> 20` in
  `src/` is `door_manager_mesh.cpp`'s colour hash). The cascade stopped at
  once: nothing became dead with them. Two neighbours were NOT made dead by
  T475 and are boarded instead: **B710** and **B711**. Original entry:
  **Leftovers of the deleted texture-upload path that no
  net can see.** T475 deleted every function and field of the async
  texture-arrival path (B670); three non-function, non-field remnants are left:
  - `AssetType::ZoneTexture` (`include/client/graphics/work_priority.h`) has no
    production user: its two users were the deleted
    `TextureAtlas::uploadPreloadedPageAsync()` and the deleted byte-3 branch of
    `ZoneStreamComponent::onPvsRegionChanged()`. Only
    `tests/test_work_priority.cpp` names it.
  - The four `constrained_texture_cache*.cpp` TUs include
    `include/client/graphics/gpu_upload_thread.h` and
    `include/client/graphics/work_priority.h` and use nothing from either (pre-existing; the last use in the cache went
    with T326).
  - `src/client/graphics/texture_atlas.cpp` includes the fmt format header for
    the deleted async page upload alone.
  - Enumerators and includes are not in `ZeroCallers` or `WriteOnlyFields`.
    Fix: delete, or decide the enumerator is kept for `WorkPriorityKey`'s
    ordering contract (its test pins the order).
  - Found by T475.

B691–B699 unused.

---

## Added 2026-09-14 by T477 (upload leftovers, shared /tmp names)

- ~~**B710**~~ **CLOSED by T480.** `renderer_types.h`, `renderer_draw_types.h`
  and `renderer_event_receiver.h` no longer include `gpu_upload_thread.h` or
  `work_priority.h`, and `irrlicht_renderer.h` no longer includes
  `work_priority.h`. Its `gpu_upload_thread.h` include stays because it is live.
  `renderer_zone_draw.cpp` and `renderer_zone_stream.cpp` now include
  `work_priority.h` directly. The full `build/` tree (client, tests and every
  tool target) and the ARM build compiled with all four removed at once.
  Original entry:
  **Four renderer headers include `work_priority.h` and
  do not use it; three of them include `gpu_upload_thread.h` and do not use it
  either.** `include/client/graphics/renderer_types.h`,
  `renderer_draw_types.h` and `renderer_event_receiver.h` include both and name
  no symbol from either; `irrlicht_renderer.h` includes `work_priority.h` and
  names nothing from it (its `gpu_upload_thread.h` include is live:
  `gpuUploadThread_`, `pendingGPUResults_`). Not a one-line delete:
  `renderer_zone_draw.cpp` and `renderer_zone_stream.cpp` use `WorkPriorityKey`
  and `AssetType` only through these transitive includes (only
  `entity_prep_worker.cpp` includes `work_priority.h` directly), so each needs
  a direct include first, and with four headers carrying the same include a
  compile proves nothing until all four are gone. Left out of T477 on scope:
  B690 named only the cache TUs, and these are hot shared headers. Found by
  T477 (census: grep for each header name and each symbol over `src/`,
  `include/`, `tests/`, `tools/`, `benchmarks/`).
- ~~**B711**~~ **CLOSED by T480.** `Door` (2), `EntityTexture` (4) and
  `LightEffect` (5) are deleted and left as commented gaps. `ZoneMesh` = 0,
  `EntityMesh` = 3 and `Icon` = 6 keep their values, and the new
  `WorkPriorityKey.SurvivingAssetTypeValuesAreUnchanged` pins them. The three
  values were not positionally significant. Nothing casts to `AssetType`,
  switches over it, or indexes a table with it. The key is only compared, in
  `GPUUploadThread` and `BackgroundThreadPool`. The only decode of bits 23-20
  is the test's own. `BitLayout`, `EqualityAndInequality` and
  `SortVectorByPriority` now use `EntityMesh`. Original entry:
  **Three more `AssetType` enumerators have no
  production user.** `Door`, `EntityTexture` and `LightEffect`
  (`include/client/graphics/work_priority.h`) are named only by
  `tests/test_work_priority.cpp`. The live ones are `ZoneMesh`
  (`renderer_zone_draw.cpp`, `renderer_zone_stream.cpp`), `EntityMesh`
  (`entity_prep_worker.cpp`) and `Icon` (`renderer_zone_stream.cpp`). None of
  the three was ever a T475 producer, so they are not part of B690's cascade;
  deleting them means rewriting `WorkPriorityKey.BitLayout`, which is written
  around `Door` = 2. Grepped over `src/ include/ tools/ benchmarks/ docker/
  scripts/ docs/`. Found by T477.

B712–B719 unused.

---

## Added 2026-09-13 by T458 (tests-only callers)

- **B600** (B, unreached) **The audio sound-category layer is never → issue #184
  constructed.** `CombatMusicManager` (`combat_music.cpp` IS in the client's
  `AUDIO_SOURCES`) is named by no file outside its own header and `.cpp`;
  `CreatureSounds`, `UISounds`, `PlayerSounds` and `WeatherAudio` are worse --
  `creature_sounds.cpp`, `ui_sounds.cpp`, `player_sounds.cpp` and
  `weather_audio.cpp` are in NO client target (`CMakeLists.txt`
  `AUDIO_SOURCES`), so only their test binaries link them, and the two
  production mentions (`sound_assets.h`, `zone_lifecycle_component.h`) are
  comments. Their 23 tests-only rows looked "live-owner" in the census only
  through name collisions (`getVolume`, `getSoundId`). Also here:
  `DoorSounds`' four unused static lookups (the class itself is live) and
  `AudioManager::preloadSound`. 28 rows in `check_zero_callers.py`'s
  `EXEMPT_TESTS_GROUPS`. Wire or delete per class; `WaterSounds`, the one
  that was pure residue, T458 deleted. Found by T458.

- **B601** (B, unreached) **GameState module mutators with no production → issue #185
  writer.** `CombatState::updateTargetHP/updateTargetLevel`,
  `GroupState::findMemberBySpawnId/setMemberInZone/updateMemberSpawnId`,
  `InventoryState::setBagInfo/setCursorQueueSize/setEquipmentStats`,
  `PetState::updatePetHP/updatePetMana`,
  `PlayerState::loadFromProfile/setCurHP/setPositionAndHeading`,
  `SpellState::addScribedSpell/clearGem/setMemorizing/updateMemorizeProgress`,
  `WorldState::setZoneId/setZoning`, `GameState::resetForZoneChange` -- 20
  rows, each called only from `tests/test_state_modules*.cpp` (and
  `test_self_death_sequence.cpp` for `setZoneId`). Not measured here: whether
  the fields they write are written live by another path, and whether a zone
  change leaves stale module state without `resetForZoneChange()`.
  `WriteOnlyFields` covers the first half. Found by T458.

- **B602** (B, unreached) **EntityManager's query and mutation API with no → issue #186
  production caller.** `findEntityByExactName`, `findPetByOwner`,
  `forEachEntity`, `forEachEntityMutable`, `getAllPets`,
  `getEntitiesInRange`, `getNearestCorpse`, `getPetOwnerId`, `markAsCorpse`,
  `updateEntityEquipment`, `updateEntityHP` -- 11 rows, all reached only from
  `tests/test_state_modules.cpp`. T362 (B070) already made
  `findEntityByName` the one name resolver; these are the rest of the
  surface. Found by T458.

- **B603** (B, unreached) **Gameplay-manager control API with no production → issue #187
  caller.** `CombatManager::EnableAutoLoot/GetHighestPriorityTarget/
  SetAggroRadius/SetFleeThreshold/ToggleAutoFire`,
  `SpellManager::beginCastById/setOnCastComplete/setOnSpellFade/swapGems`
  (the two callbacks are INVOKED in `spell_manager.cpp` and
  `spell_packet_handlers.cpp` but nothing registers them),
  `BuffManager::clearEntityBuffs/removeBuff`,
  `ActiveBuff::getTimeString/isAboutToExpire`,
  `GroupComponent::DeclineGroupInvite`, `PlayerComponent::SetMoveSpeed`,
  `InventoryManager::pickupMoney/pickupPartialStack/placeOnMatchingStack/
  returnCursorMoney`, `HotbarModel::assignSlot/clearSlot`,
  `ZoneLines::checkMovement`, `WhoAllEntry::isLinkdead`, the free
  `calcBankBagSlotId` -- 24 rows. Found by T458.

- ~~**B604** (A, *unconfirmed live*) **The server's cast interrupt never reaches
  `SpellManager::handleInterrupt()`.** `src/client/zone/zone_dispatch.cpp`
  routed `HC_OP_InterruptCast` to a log line reading "Cast interrupted - spell
  manager handles this via other packets". Found by T458.~~ **CLOSED by T469
  (2026-09-14)** (`refactor/T469_cast_interrupt_unhandled.md`): CONFIRMED from
  the eqemu source and a wire-byte test, not live. No other packet cleared the
  cast: `Mob::InterruptSpell` sends the caster an 8-byte `OP_InterruptCast`
  (no spell id) plus an `OP_ManaChange` whose `keepcasting` fable only logged,
  so the cast stayed on, the gem stayed locked, the next cast was refused
  client-side as `AlreadyCasting`, and `SpellManager::update()`'s duration+5 s
  fallback finally reported it as a SUCCESS (recast started, impact burst).
  `ZoneComponent::OnInterruptCast` now routes it to `handleInterrupt()`, and
  `handleManaChange()` ends a not-yet-confirmed cast on `keepcasting 0` (the
  fizzle path, which sends no InterruptCast). Pinned by
  `tests/test_spell_interrupt_wire.cpp` (6 of 10 red before the fix). The live
  proof is **B630**; a residual path is **B631**.

- **B605** (B, unreached) **Input, command and console API with no production → issue #188
  caller.** `CommandProcessor::setDefaultChannel/setEchoEnabled/
  unregisterCommand`, `HotkeyManager::isMovementKey/modeNameToEnum`,
  `HotkeyBinding::toString`, `ConsoleRenderer::setStatusUpdateInterval` (the
  one tests-only method on a class the census sees no live use of), and
  `focusTextEntry` -- 8 rows. The hotkey rows sit beside T466's territory
  (`hotkey_manager.cpp`) and were not touched. Found by T458.

- **B606** (B, unreached) **Renderer, asset and data API with no production → issue #189
  caller.** `IrrlichtRenderer::setNewUIEnabled`,
  `EntityRenderer::loadEquipmentModels/loadGlobalCharacters` (not called even
  by the zero-caller `loadGlobalAssets`, B494, which T468 deleted; since T485
  `tools/hwcaps/held_item_candidate_search` calls both, so they left the
  tests-only census bucket -- still no PRODUCTION caller),
  `ZoneLifecycleComponent::applyStoredZoneEnvironment` (its header already
  calls it a superseded duplicate of `renderer_zone_load_env.cpp`'s apply path
  "boarded for a measured wire-or-delete decision" -- but no board entry
  existed; this is it), `RDPServer::setCertificate`,
  `BackgroundWorkQueue::pollAll/setBatchHooks`,
  `StringDatabase::loadDbStrFile/loadEqStrFile` (the legacy text loaders),
  `WilleqDatabase::getLoginString`, and eight free helpers:
  `reloadRaceMappings`, `LiftTravelSeconds`, `TargetTypeRestrictsCreature`,
  `toString(SpellVisualSource)`, `parseFormattedMessageText`,
  `optionsClipPlaneStep`, `optionsVolumeFraction`, `optionsVolumeStep` -- 18
  rows. Found by T458.

- **B607** (P) **The zero-caller census INVENTED dead functions through a
  stream operator, and one of them was a T455 exemption.**
  `check_zero_callers.py` scored `outFile << SerializeUiPreferences(p);` as a
  DEFINITION, because `<`, `>` and spaces are all "type characters" to its
  lead test -- the one direction T455 said the census must never get wrong.
  It reported the live `SerializeUiPreferences` as tests-only, and it had
  reported `getCreatureTypeName` (called at `flock_controller.cpp:530`,
  `ss << getCreatureTypeName(...)`) as zero-caller, which T455 boarded under
  B490. T458 fixed the rule (a WHITESPACE-BOUNDED `<<`/`>>` in the lead, or a
  lead with no identifier at all, is a use) and struck the stale row. A first
  cut that matched a bare `>>` hid four genuinely dead functions whose return
  types close a nested template (`InventoryManager::getBankItems` ...) and was
  caught only by re-measuring every bucket after the change -- do that after
  any classifier edit. The same unit added the tests-only half of the ratchet
  (`EXEMPT_TESTS_GROUPS`: 36 `seam`, 145 `observer`, 110 boarded as
  B600-B606) and saw B503's cascade again: deleting 31 rows exposed five more
  (`Strings::ConvertToDigit`, `Random::Real`, `Strings::IsNumber`, <!-- RETIRED-OK: T458's own tombstone for the cascade it deleted -->
  `Strings::IsFloat`, and `EQ::ValueWithin`, which had looked live only
  through `position.cpp`'s same-named local template). `observer` is a
  class-level argument, not a per-row proof. Found by T458.

## Added 2026-09-14 by T482 (how a confirmed cast ends)

- **B760** (A, *unconfirmed live, rare*) **A cast's result code can say → issue #45
  Interrupted for a success, and can be late.** `SpellManager::noteZonePacket()`
  settles a released cast as a stop on the first packet after the keepcasting 0
  that is not `OP_MemorizeSpell` or `OP_LinkedReuse`. eqemu sends some packets
  unreliably (`OP_SpawnAppearance`, `OP_ClientUpdate`, HP and group updates:
  `/mnt/projects/akk-stack/code/zone/mob.cpp` `Mob::SendAppearancePacket`,
  `/mnt/projects/akk-stack/code/zone/groups.cpp`), and the reliable stream may hold
  the tail back behind them, so one can arrive inside the tail. The gem's recast
  is still right, because `handleSpellBarRefresh()` starts it from the
  MemorizeSpell alone. Only the `SpellCastComplete` result is wrong, and nothing
  in production reads it (B603). In a quiet zone the result also waits for the
  next packet. Confirm by recording, on the B630 live run, whether anything
  arrives between the two tail packets.

- **B761** (A, *unconfirmed live*) **A bard song with → issue #46
  `Character:EnableBardMelody` false has no end packet, so it ends on the
  duration + 5 s timeout with no result.** A song's tail is `OP_MemorizeSpell`
  alone, and only when that rule is on
  (`/mnt/projects/akk-stack/code/zone/spells.cpp` `Mob::CastedSpellFinished`,
  bard branch; the rule defaults to true in
  `/mnt/projects/akk-stack/code/common/ruletypes.h`). Before T482
  the song's own `OP_Action` completed it. No bard has been cast on fable, with
  the rule on or off. Confirm with a bard on the live server.

- ~~**B762** (A, *unconfirmed*) **fable tracks 8 spell gems and Titanium has 9.**~~ **CLOSED by T483 (resumed, B770 option (a); struck T486).** `EQT::SPELL_GEM_COUNT = 9` is stored (the PlayerProfile read, both `OP_MemorizeSpell` arms, the `SpellManager` arrays) and `EQT::VISIBLE_SPELL_GEM_COUNT = 8` is shown and offered, matching the Default skin; `tests/test_ninth_spell_gem.cpp`. The original entry follows for the record.
  `EQ::MAX_SPELL_GEMS` is 8 (`include/client/spell/spell_constants.h`), while
  Titanium's `CastingSlot` has `Gem1`..`Gem9`
  (`/mnt/projects/akk-stack/code/common/patches/titanium_limits.h`). Every
  gem-indexed arm, including T482's `OnMemorizeSpell` scribing 3 arm, drops
  slot 8. Found by T482 and not measured: check whether the ninth gem is shown
  and castable on a character that has it.
  **Narrowed by T483 (source only, no live run).** Titanium carries 9 slots on
  the wire (PlayerProfile `mem_spells[9]` at 4360). But eqemu refuses a cast
  from slot 8 unless the caster has AA 1071 Mnemonic Retention
  (`/mnt/projects/akk-stack/code/zone/client_packet.cpp:4387-4392`; rule `Spells:RequireMnemonicRetention`,
  default true, `/mnt/projects/akk-stack/code/common/ruletypes.h:534`). The oracle's loaded skin (`UISkin=Default`)
  parks `CSPW_Spell8` at (-5000,-5000) with no size, so the official client
  shows 8 gems. What remains is the design question in B770. The full census of
  every 8 is in `refactor/T483_ninth_spell_gem.md` Review.

## Added 2026-09-14 by T483 (the ninth spell gem)

- **B770** (C) **What should fable do with gem 9?** The facts (T483 Review): → issue #47
  the gem is on the wire; casting from it is server-gated on the Mnemonic
  Retention AA (1071: cost 3, max rank 1, level 55, every class but
  Warrior/Monk/Rogue, `aa_expansion=8`), with the rule on by default; memorising
  into it is NOT gated; fable parses no AA ranks (`ZoneProcessRespondAA` only
  logs); the oracle's default skin hides the gem off screen, while
  `uifiles/default_old` places it at (1,264), 40x32. Options: (a) keep 8 and
  match the oracle skin, reading `mem_spells[8]` only so a spell held there is
  not lost silently; (b) parse the AA ranks and show gem 9 only when rank 1 is
  held, which is new AA tracking; (c) show 9 always and let the server refuse.
  (c) contradicts the oracle's skin. Also recorded: fable spells the gem count
  four different ways (`EQ::MAX_SPELL_GEMS`, `eqt::state::SPELL_GEM_COUNT`, a
  local constant in `zone_entry.cpp`, `SpellGemPanelState::GEM_COUNT`), plus a
  literal 8 in `ui_layout.h:112`, and `EQT::SPELL_GEM_COUNT = 9` is used by no
  gem code. Whichever option is chosen, those should become one constant.
  Before any change, read the live `rule_values` row for
  `Spells:RequireMnemonicRetention` under a lock.
  **DECIDED 2026-09-14: option (a), by the orchestrator. Will may veto.** The
  standing rule is that the official client is the golden, and its Default
  skin shows eight gems. T483 implemented it: `include/common/packets/common.h`
  now holds the only two gem counts. `EQT::SPELL_GEM_COUNT` (9) sizes storage:
  SpellManager, SpellState, the PlayerProfile read and both `OP_MemorizeSpell`
  arms. `EQT::VISIBLE_SPELL_GEM_COUNT` (8, citing the XML geometry) bounds
  everything shown or offered: the panel, the layout, the hit test,
  `beginCastFromGem`, memorize/forget/swap, `/cast` `/mem` `/forget`, the gem
  keys and the hotbar's spell search. `EQ::MAX_SPELL_GEMS`,
  `eqt::state::SPELL_GEM_COUNT`, `SpellGemPanelState::GEM_COUNT` and the
  constant local to `zone_entry.cpp` are gone. No AA ranks are parsed (option
  (b) was not chosen). Pinned by `tests/test_ninth_spell_gem.cpp` and
  `CommandProcessorTest.Cast_NinthGem_RefusedClientSideWithoutDispatching`.
  Live checks done by B851 (2026-09-19, issue #97): rule_values read
  (default ruleset = true) and the ninth-spell-across-zone check; see
  `refactor/B851_gem9_retention_live.md`.

## Added 2026-09-14 by T469 (the server's cast interrupt)

- **B630** (A, *owed live proof*) **A real server-interrupted cast has been → issue #48
  watched on fable.** T469 wired `HC_OP_InterruptCast` and
  `ManaChange keepcasting 0` from the eqemu source
  (`/mnt/projects/akk-stack/code/zone/spells.cpp` `Mob::InterruptSpell`,
  `Mob::StopCasting`) and proved it with wire bytes
  through `ZoneComponent::DispatchZonePacket`, with no server (T466 held the
  lock). Owed to the next lock holder, two-client: caster starts a 3 s+ cast
  and moves (or a GM `#interrupt`s it); assert on the CASTER that the cast bar
  clears, the gem is castable at once (no `AlreadyCasting`, no recast), the
  chat shows "Your spell is interrupted.", and no impact burst appears 5 s
  later; assert on the OBSERVER, targeting the caster, that its target cast bar
  and cast glow clear and it reads "<name>'s casting is interrupted!". Also
  force a fizzle (a low-skill caster) and check the same caster-side
  assertions. Record the packet order both clients receive; the test assumes
  InterruptCast precedes ManaChange (spells.cpp queues them in that order).
  **T482 adds three caster-side legs** (no lock, wire tests only):
  (1) a normal nuke on an NPC: the log shows ManaChange keepcasting 0 and then
  `OP_MemorizeSpell` scribing 3 for the gem, and the gem goes into recast with
  one impact burst; (2) a recast-timer stop after BeginCast (desync the gem, e.g.
  `#memspell` the spell back into a refreshing gem and cast it at once): the
  cast bar clears on the ManaChange, the gem is castable, chat shows
  SPELL_RECAST, and no success is logged; (3) a buff that does not take hold
  (cast a buff on a target that already has a stronger one): the same as (2)
  apart from the message. Record whether any packet arrives between the
  ManaChange and the MemorizeSpell.

- ~~**B631** (A, *unconfirmed live, rare*) **A cast the server stops AFTER BeginCast
  with only a ManaChange is still reported as a success.**~~ **CLOSED by T482**
  (`refactor/T482_cast_ended_by_stopcasting_or_resist.md`), from the eqemu source
  and wire-byte tests; the live leg is owed under B630. The packets DO separate
  the two: eqemu's success tail is ManaChange keepcasting 0 followed by
  `OP_MemorizeSpell` scribing 3 (`/mnt/projects/akk-stack/code/zone/spells.cpp`
  `Mob::CastedSpellFinished`), and `StopCasting` never sends the second.
  keepcasting 0 now releases the cast at once, the MemorizeSpell makes it a
  Success with the recast, and any other packet first makes it Interrupted.
  The "resisted spell" half was wrong as written: a fully resisted nuke is a
  success in eqemu (mana and recast consumed), and before T482 fable already
  completed it on the effect_flag 0 action. The real defect in that family was
  a beneficial buff that does not take hold, which fable reported as a success
  with a recast. Pinned by `tests/test_spell_cast_end_wire.cpp`. Original text:
  `Mob::CastedSpellFinished` calls `StopCasting()` -- `OP_ManaChange`
  `keepcasting 0`, no `OP_InterruptCast` -- when the spell or item recast timer
  has not expired (`/mnt/projects/akk-stack/code/zone/spells.cpp`, the two
  `SPELL_RECAST` arms). By then
  BeginCast has confirmed the cast, and a SUCCESSFUL finish sends the same
  bytes (`SendSpellBarEnable`), so `SpellManager::handleManaChange()`
  deliberately ignores it (pinned by
  `SpellInterruptWire.KeepcastingZeroAfterBeginCastIsNotAnInterrupt`). The cast
  therefore ends only through `SpellManager::update()`'s duration+5 s
  fallback, which calls `completeCast(true)`: gem recast started, impact burst
  painted. The same fallback also reports as success any confirmed cast that
  ends with no `OP_Action effect_flag 4` (a resisted spell). Confirm by
  measuring what the official client does with a post-BeginCast
  `keepcasting 0` (does it end the cast bar at once?) before changing the
  rule; the path needs a client/server recast desync to reach. Found by T469.

---

## Added 2026-09-13 by T466 (T461 Leg B: the official cameras, keys and held items on the oracle)

Evidence for every entry below is in `review/T466/` (README names each file).

- ~~**B580**~~ (A) **CLOSED by T471 (2026-09-14)** -- see the closing note at the
  end of this entry. **The official User Defined 1/2 and Tether cameras are anchored
  to the WORLD, not to the player's heading; fable's rig is heading-relative.**
  Measured: the oracle character `#goto`'d from heading 160 to 288 (+90 deg) and
  each camera ENTERED after the turn. Edge score h160 vs h288: First Person
  0.837, Overhead 0.689, Chase 0.680 (turned with the player); User 1 0.084,
  User 2 0.120, Tether 0.193 (same as a re-grab, 0.03-0.11). fable's
  `computeCameraRigPose()` adds the player heading to the azimuth, so a fable
  user camera swings with the player where the official one holds its world
  direction. A fitted fable azimuth is therefore valid only at the heading it
  was fitted at (h160). Fix shape: a world-azimuth rig for User1/User2/Tether
  (the rig struct, `computeCameraRigPose`, and `/camera` help). Found by T466.
  **Closed by T471:** `cameraAzimuthFrame()` (`camera_mode.h`) names World for
  User 1, User 2 and Tether and PlayerHeading for the rest, and
  `computeCameraRigPose()` ignores the heading for a World azimuth. The azimuth
  is NOT captured from the heading on entry, though the plan proposed that:
  T466's marks (`review/T466/logs/oracle_marks_session.tsv`) show every camera
  entered afresh through its own Ctrl+F binding after the `#goto ... 288`, and
  those entries gave the same view as at 160 -- so a stored world azimuth, held
  across entries, is what the oracle does. `/camera get` prints
  `az_frame=world` or `az_frame=player_heading`. Red before, green after:
  `UserAndTetherCamerasHoldAWorldDirectionWhenThePlayerTurns` (turn in place and
  re-entry after the turn), `AWorldAzimuthIgnoresTheHeading`,
  `GetPrintsTheModeTheFourValuesAndThePose`. The world direction the official
  cameras START at is unmeasured: B651.

- ~~**B581**~~ (A) **CLOSED by T471 (2026-09-14)** -- see the closing note at the
  end of this entry. **`/camera chase` from a rig camera leaves the camera at the rig's
  pose.** fable baseline session: after `/camera overhead` then `/camera chase`,
  `/camera get` prints `mode="Chase Camera"` with `node camPos=(438.471,-99.279,
  78.175)` -- the Overhead node pose, byte for byte
  (`review/T466/fable/sessions/baseline_h160/camera_replies.txt`).
  `PlayerCameraComponent::setCameraMode()` places first person and rig modes on
  entry, but Chase only moves when `setFollowPosition()` next runs (a player
  move). A stationary player switching to Chase sees the previous camera.
  Found by T466.
  **Closed by T471:** `setCameraMode()` now calls
  `CameraController::setFollowPosition()` on entering Chase, with the movement
  path's own arguments (G06 statement 402), beside T74's first-person and
  T461's rig placement. Red before, green after:
  `CameraChaseEntryTest.EnteringChaseFromAnyOtherCameraPlacesTheChaseCameraAtOnce`
  (a real EDT_NULL camera node; from Overhead, First Person, User 1, User 2 and
  Tether the node must land on the follow pose at T466's baseline pose). Not
  live-proven: no lock was assigned.

- **B582** (C) **fable's F10/F11 and L do not mean what the official keys mean.** → issue #49
  Measured on the oracle's Options > Keys (UI category): **F10 = Toggle UI
  Display, F11 = Toggle netstat, L = Toggle Leadership Window, Alt+C = Toggle
  Combat Ability Window** (fable's Alt+C matches). fable binds F11 =
  `ToggleAllUI` (and `MusicVolumeUp`, B561), F10 = `MusicVolumeDown`,
  Shift+F10/F11 = effects volume, L = `CycleObjectLights`. No volume action was
  found on the Camera, UI or Commands pages (the Commands list ends at "Close
  Inventory Bags"). Settles B561's question: the official toggle-UI key is F10,
  and F11 is netstat. Rebinding non-camera keys was outside T466's territory.
  Found by T466.

- **B583** (P) **The eq-oracle Wine client pointed at the dead eqemu address.**
  `~/eq-oracle/client/eqhost.txt` and `client2/eqhost.txt` still said
  `10.0.30.23:5998` after the 2026-09-12 VM move; the login failed with "A
  connection to the server could not be reached". T466 repointed both at
  `172.16.200.99:5998` (backups in `review/T466/data/`). Any other copy made
  from the old install needs the same edit. Found by T466.

- **B584** (P) **A "training dummy" NPC stands exactly on the canonical camera
  spot.** `#loc` on "a training dummy (189)" in qeynos2: XYZ `-74.00, 428.00,
  3.10`, heading 160 -- the pose T310/T361/T377/T401/T405/T422/T426/T428/T431
  and T466 measure at. It is not in `spawn2` (a runtime spawn, likely the "zone
  controller" NPC beside it) and it covers the player in every third-person
  frame. Same class as B467's corpse. Found by T466.

- **B585** (P) **The official client does not read the `[Options]`
  `Camera1-*`/`Camera2-*` keys at launch and does not persist camera changes.**
  Measured twice: (1) User 1 moved by Pitch-up x10 and the other adjustment keys,
  then a clean exit (Quit -> Logout -> Exit -> Yes): no Camera key changed in
  `eqclient.ini`. (2) `Camera1-Heading` 0->90 and `Camera2-DirHeading` 277->0
  written before a relaunch: the client rewrote both back to 0/277 during login
  (mtime 22:15:07, before entering world) and the User 1/2 frames scored 0.094 /
  0.136 against the default-ini frames (re-grab noise). So those keys cannot be
  used to learn the units of DirHeading/Pitch/Height/Zoom; they are the client's
  own write-out. Found by T466.

- ~~**B586**~~ (A, *owed*) **The held-item attack animation was not captured on the → issue #50
  oracle.** Set A (Short Sword primary, Gloomingdeep Lantern secondary) was
  captured idle from four cameras, but auto-attack at 8 units behind the
  training dummy produced no swing in 40 stepped frames. Owed: an attack capture
  (closer, or a target that is certainly in melee range and in front) for all
  five items. fable's side is owed too: `/target a training dummy` in a
  `--script` run replied `No target found matching 'a training dummy'`, so no
  fable attack was driven either. Sets B (1H blunt + shield) and C (2H) were not
  captured on either client. The spot is worse than B584 says: fable's own NPC
  list at zone-in showed **thirteen** `a_training_dummy000..013` spawns at
  (428, -74, 3.0) in the client frame (ids 185-200), stacked on one point.
  Found by T466.
  T472 Leg A (2026-09-14) changed nothing here: offline only; the attack
  captures are T472 Leg B's.
  **CLOSED by T478 (issue #50), struck 2026-09-20 (card kb-chase-closures).**
  T478 paid the whole capture debt: the `/target` cause was the underscore spawn
  name (all four T478 sessions targeted `a_training_dummy000` and
  auto-attacked), and attack captures landed on both clients for every set --
  A, B, C and IT10. What T478 found in them is the live record now: B722 (#62)
  and B723 (#63).

- ~~**B587**~~ (A, *unconfirmed*) **CLOSED by T478 (live) -- see the T478 record
  below; struck 2026-09-20 (card kb-chase-closures, issue #51).** At the camera that
  reproduces the official Chase → issue #51
  view, fable draws the player model facing the camera.** fable session
  `heldA_sword_lantern_spotA_h160` (Testchar at qeynos2 (-81.39, 431.06, 3.751)
  h160): the rig az 0, pitch 6, dist 4, height 7 -- whose world view matches the
  oracle's Chase frame (wall on the left, edge 0.738 at the canonical spot) --
  shows Testchar's FACE and chest; the official Chase camera at the same
  character pose shows the BACK of the head. fable's side views agree (az 90
  shows the back, az 270 the chest). Frames:
  `review/T466/sheets/T466_heldA_sword_lantern_oracle_top_fable_bottom.png`.
  First person and the camera azimuth are both oracle-matched at this heading
  (T431, this unit), so the difference is in the player MODEL's yaw. Heading
  asymmetry is load-bearing (players and NPCs have different default rotations):
  do not "fix" this from one frame -- confirm with a two-client capture where
  the official client observes fable's character. Found by T466.
  **NARROWED by T472 Leg A (2026-09-14): the cause is found, fixed and pinned
  offline; the live proof is owed (T472 Leg B).** It was neither the camera nor
  the load-bearing 90-degree offset. The player's node had TWO heading writers:
  `EntityRenderer::updatePlayerEntityPosition()` passes the renderer's heading
  (`90 - server`, the flip in `ZoneProcessPlayerProfile`), but only when the
  player moves or turns; the zone-in build and the self echo in
  `ZoneProcessClientUpdate` carry the self row's RAW server heading. T466's
  Testchar never moved, so he wore the player bake (+90) over an unflipped
  heading -- 135 degrees wrong at server heading 160, which reproduces all three
  of T466's frames (az 0 face, az 90 back, az 270 chest). Measured offline: the
  player path at `90 - s` and the NPC path at `s` draw identical hands, and the
  azimuth-0 rig then sees the BACK. Fix: `EntityRenderer::syncLocalPlayerHeading()`,
  called by `FrameComponent::processFrameSimulation()` after the update flush and
  before equipment placement; no conversion site and no camera code changed.
  Pinned by `HeldItemTest.StationaryPlayerIsDrawnAtTheRendererHeading` and
  `HeldItemFrameOrder.PlayerHeadingIsSyncedAfterUpdatesAndBeforeEquipment`.
  Owed: the oracle Chase capture of a stationary fable character (and the
  two-client observation the heading note asks for).
  **Closed by T478 (live), struck 2026-09-20 (card kb-chase-closures, issue #51):**
  T478's Chase sweep at h160 on Testchar, not moved since zone-in, shows the
  back of the head, back and shoulders, matching the official Chase frame
  (`fable/sessions/chase_sweep_h160`, frame 163; `review/T478/README.md`). The
  offline pin `HeldItemTest.StationaryPlayerIsDrawnAtTheRendererHeading`
  (tests/test_held_item_attachment.cpp) re-verified green on this card.

- ~~**B588**~~ **CLOSED by T485** (lantern half closed by T472, blade half by T485's
  searched correction; see the T485 block). Original text: (A, *unconfirmed*) **Held items in the hand: the sword's blade points
  the opposite way and the lantern is not drawn.** Idle, set A, same sheet as
  B587 (oracle User 1 and Tether crops on top, fable az 90 / az 270 below).
  Descriptive, not judged: the official client shows the Short Sword in the
  right hand with the blade hanging DOWN and outward from the fist, and the
  Gloomingdeep Lantern (item 9979, IT48) hanging in the LEFT hand at the hip.
  fable shows the Short Sword in the right hand with the blade standing UP above
  the fist (about a half turn about the hand's horizontal axis, as far as the
 frames show), and no lantern in the left hand from either side. Attack-pose
 comparison owed (B586) -- that comparison was captured by T478 and its
 findings live on as B722/B723 (kb-chase-closures). Found by T466.
  **T485 (slot1, in flight, `refactor/T485_held_item_transform_candidate_search.md`) OWNS this item** — the lantern half is already false since T472, and T485 strikes or narrows the rest; nobody else scopes a fix off it (T486 note).

---

## Added 2026-09-14 by T471 (T466's camera measurements applied)

- **B650** (A) **fable's Chase camera does not reproduce the official Chase → issue #52
  geometry.** T466 fitted the oracle's Chase frame as a rig: az 0, pitch 6, dist
  4.0, height 7.0 above the feet (edge 0.738, bracketed; dist +/-0.5 -> 0.767 /
  0.839). fable's Chase is `CameraController::setFollowPosition()`, not a rig.
  Computed at its defaults with no collision, for T466's Testchar
  (collisionZOffset 3.119, `baseline_h160/camera_replies.txt`: z 3.119, feetZ
  0.000): camera **20.0** behind the player and **13.119** above the feet,
  looking at a point 10.0 AHEAD at 9.119 above the feet, view pitch **7.595**
  degrees down (its view ray crosses the player's axis at 10.452 above the feet,
  so as a rig about that point: pitch 7.595, dist 20.177, height 10.452). The
  fit: camera **3.978** behind and **7.418** above the feet, looking at the
  player's axis at 7.0, pitch **6.0** down. The camera is 16.02 units farther
  back (x5.03), 5.70 higher, and pitched 1.60 degrees more; the fit's own
  sensitivity rejects a distance 0.5 off. Not applied by T471 (plan: record and
  board). Fix shape: make Chase a heading-relative rig at the fit, keeping the
  wall collision -- the visual gate does not read this camera, but B587's model
  yaw and B588's held items are judged through it. Found by T471.

- **B651** (C -> ORACLE-OWED) **Which world direction User 1, User 2 and Tether → issue #53
  start at.** T471 made the three hold a WORLD azimuth (B580) but kept the
  placeholder numbers (User 1/2 azimuth 0, Tether az 0 pitch 10 dist 30 height
  5): T466's sweeps did not converge, and a best-of-a-coarse-sweep value is not a
  measurement. Also unmeasured: whether the official direction is a fixed
  constant or is taken from the player's heading at some earlier moment (login,
  the first entry) -- T466's two entries were both after login at the same
  session start, so they cannot tell. A pre-T471 per-character ini's
  `User1Azimuth`/`User2Azimuth` was heading-relative and is now read as a world
  azimuth (the key did not change; the official client does not persist camera
  changes at all, B585). Method: enter User 1 at two different LOGIN headings
  and A/B the world view. Found by T471.

- **B652** (C) **fable's own zoom keys collide with the official meanings of → issue #54
  those keys.** T471 bound the official Zoom in Numpad7/Insert and Zoom out
  Numpad1/Delete and KEPT fable's `CameraZoomIn` = Plus (and Shift+Plus in the
  shipped file) and `CameraZoomOut` = Minus (and NumpadSubtract in the file). On
  the oracle's Camera page (T466) Shift+- / Shift+= are Clip plane in/out and
  Numpad- is Take a screenshot; Toggle mouse look is F12, which fable binds to
  Screenshot. None collides with another fable binding; they collide with what a
  player of the official client expects. Same decision class as B582 (Will is
  deciding the UI-page keymap). Found by T471.

- **B653** (A, *owed*) **What Raise, Lower and Tether-slide do on the official → issue #55
  client, and every camera key step.** T466 pressed Tether-slide right x10,
  Raise x10 and Zoom in x5 in User 1 and scored no change (0.047 / 0.022 /
  0.013), while Pitch up x10 in the same session did move it -- so the keys
  reached the client and these three do not act on User 1. T471 therefore bound
  Tether-slide and Raise/Lower to the TETHER camera only (azimuth, and look-at
  height), a choice by the key names, not a measurement; "right" is the viewer's
  right. Steps are fable's: 5 degrees (`kCameraOrbitKeyStepDeg`), 1 unit
  (`kCameraRaiseKeyStepUnits`), 2 units per zoom notch. Owed: each key in Tether
  (and Chase, for Zoom and Raise) on the oracle, with the image shift per press.
  Found by T471.

- ~~**B654**~~ (A, *unconfirmed*) **The `--egl-x11` device drops every numpad digit → issue #56
  key.** `docker/irrlicht-drm-x86/CIrrDeviceFB_X11.cpp` looks a key up with
  `XLookupKeysym(&xev.xkey, 0)` -- index 0, which for a keypad digit is its
  NumLock-OFF keysym (`XK_KP_Home`, `XK_KP_Up`, ...) -- and
  `x11KeysymToIrrlicht()` has cases only for `XK_KP_0`..`XK_KP_9`, so it returns
  0 for all ten. The official camera keys T471 bound on Numpad1-9 (and Alt+ them)
  are therefore unreachable in an X11-windowed run; the DRM/evdev device maps
  `KEY_KP7` -> `KEY_NUMPAD7` regardless of NumLock and is unaffected, and the
  Insert/Delete/Alt+arrow/Alt+PageUp alternates work on both. From code, not
  run: T471 had no display. Outside T471's territory. Found by T471.~~
  **STRUCK 2026-09-20 (fixed live, issue #56, `task/kb-b654-x11-numpad`):
  the keysym -> EKEY_CODE decision moved to the display-free header
  `docker/irrlicht-drm-x86/x11_keysym_map.h`, which maps the ten NumLock-level-0
  navigation aliases (KP_Home/Up/Prior/Left/Begin/Right/End/Down/Next/Insert,
  measured against Xvfb's default keymap) to the same KEY_NUMPAD0..9 as the
  digit keysyms, so a numpad digit press delivers its digit in BOTH NumLock
  states. Pinned display-less (test_x11_keysym_map, RED: all ten aliases -> 0)
  and end-to-end on Xvfb (test_x11_numpad_e2e: real CIrrDeviceFB under
  WILLEQ_EGL_X11, xdotool KP_7/KP_1 -> CameraZoomIn/CameraZoomOut through the
  shipped HotkeyManager, RED: both presses delivered key 0; GREEN: 103/97 ->
  the two actions; Alt+ variants probed too). CI propagation is part of the
  fix: scripts/ci/irrlicht_input_stamp.sh stamps the lib's committed inputs
  and the CI build job rebuilds the warm lib on mismatch, so a committed
  device-source change can no longer stay invisible to CI (docs/ci.md,
  "Device-source changes are caught by the input stamp"). Evidence:
  `refactor/B654/`.**

---

  **NARROWED by T472 Leg A (2026-09-14): both halves fixed and pinned offline
  against the S3D data; the oracle comparison is owed (T472 Leg B).**
  (1) LANTERN: IT48's actor is hierarchical (`IT48_ACTORDEF` -> 0x11 ->
  `IT48_HS_DEF` -> `LANTERN01_DAG` -> `LANTERN01_DMSPRITEDEF`) and
  `EquipmentModelLoader` indexed a mesh only by the name `IT48_*`, so the model
  was never indexed and every attach path skipped it (the T466 run logs
  `Skipping equipment model ref: ... equipment 48 (secondary)`). The census over
  the seventeen `*equip*.s3d` archives found 19 such actors (IT2, IT46-IT48, IT62,
  IT97, IT140-IT142, IT146, IT148-IT151, IT154, IT160, IT410, IT411, IT10402).
  Fix: `resolveEquipmentActorGeometry()` (new `equipment_actor_geometry.cpp`) walks
  the reference chain, adds each mesh's centre and bakes rest DAG transforms; the
  name rule stays as the fallback. (2) BLADE: at idle R_POINT's X axis points UP
  the forearm and an item's long axis is its +X, so the pre-T472 `(180,0,0)` --
  a half turn about X, which leaves X alone -- stood the blade up. Shields'
  painted faces (IT200-IT203) are item +Y and SHIELD_POINT's +Y points away from
  the body, so Y must be kept: the correction is a half turn about the item's Y
  (`computeHeldItemTransform()`), with the item origin (its grip, once the centre
  is added) on the bone -- the bounding-box grip offset and T65's `(180,0,180)`
  are gone. Pinned by `tests/test_held_item_attachment.cpp` and
  `FirstPersonWeaponAttachmentTest.ThirdPersonIdleBladeHangsBelowTheGrip`.
  Offline blade proof used IT1 (a one-handed sword); the Short Sword's own IT
  number was not available offline. Owed: set A/B/C oracle-vs-fable captures.

## Added 2026-09-14 by T472 (held items and model facing, Leg A)

- ~~**B660**~~ **CLOSED by T476.** Skinned items drew with every vertex piece at
  its own bone's origin. The cause: their 0x10 (flag 0x200) lists the skinned mesh
  AFTER its bones, `parseFragment10()` never read that list, so the mesh was found
  only by name and no rest pose applied. `SkeletonTrack::meshRefs` now holds it and
  `resolveEquipmentActorGeometry()` places each vertex piece by its bone's
  accumulated rest transform. Census (`review/T476/census/`): 1,081 index items,
  before 736 chain / 266 name-only / 79 unresolved, after 735 chain / 346 skinned /
  0 / 0; 282 of 286 skinned actors were wrong (4 have an identity rest pose).
  Pinned by `HeldItemTest.SkinnedItemIsPlacedByItsSkeletonRestPose`,
  `HeldItemTest.EverySkinnedItemIsPlacedByItsSkeletonRestPose` and
  `EquipmentItemCensus.EveryItActorIsClassified`. No live look yet.

- ~~**B661**~~ **CLOSED by T485**: no axis rule is needed -- one correction holds IT10's -Z
  down the leg and the blades' +X forward at once (see the T485 block). Original
  text: (A, *unconfirmed*, narrowed by T476 -- **STOP hit**) **Not every
  item's long axis is its +X, and no rule from the S3D data says which way to
  turn it.** IT10 spans z -3.35..0.25 about its origin and holds along R_POINT's
  Z. T476 took the census and stopped before writing an axis rule. Evidence
  (`review/T476/census/equip_census_after_b660.csv`, the 1,081 index items):
  - no frame to derive from: neither 0x14 nor 0x2D carries a transform, so the
    mesh's own axes are the only frame the item has;
  - the extent ratio (longest / next) is continuous, with no gap: 621 items are
    below 2 and the 1.0-6+ histogram has no empty bin, so no threshold separates
    "long" from "compact" without choosing one by argument;
  - the shields must not move but disagree with each other: IT200-IT202 read X-
    (ratios 1.46, 1.00, 1.18) and IT203 reads Z+ at 1.003;
  - the elongated Z class (109 items) is mostly armour-family archives (lgequip
    56, lgequip_amr 29, lgequip2 13) and only 4 are used by a database item, so a
    rule written for IT10 would also turn items that may never be held.
  Needs the oracle: capture an IT10 item held by the official client (T472 Leg B,
  set B/C) before any axis change. Found by T472.

- ~~**B662**~~ **CLOSED by T485**: `tools/hwcaps/held_item_candidate_search` builds the
  entity through `EntityRenderer::createEntity()` + `updateMainThreadEntityState()`
  and captures it from three cameras (NPC path; the player bake is pinned by
  `tests/test_held_item_attachment.cpp` instead). Original text: (P) **There is no scripted offline capture of an IN-WORLD entity with
  held items.** `tools/model_viewer` is interactive only (F12 writes
  `model_viewer.png` in the working directory) and has its own attach copy;
  `tools/hwcaps/ui_inventory_capture` renders the paperdoll (`CharacterModelView`),
  which shares `computeHeldItemTransform()` with `EntityRenderer` but not its
  player bake or the scene the camera sees. T472's before/after pair is the
  paperdoll. Found by T472.

B663–B669 unused.

T476 (2026-09-14) closed B660, narrowed B661 and added no entries; B700–B709
unused.


---


---

## Added 2026-09-14 by T474 (Chase camera geometry to the oracle fit)

- **B650 NARROWED by T474** -- APPLIED: Chase is a heading-relative rig at T466's
  fit (`kChaseRig` az 0, pitch 6, dist 4.0, height 7.0; `camera_rig.h`), placed
  through `CameraController::placeChaseCamera()` (rig wall pull-in + T464's
  eye-region ceiling clamp); `setFollowPosition()` and the follow-distance API
  are deleted. LIVE on x86 at T466's pose (qeynos2 -74 428 3.751 h160, the
  fitted Human male): the node sits at the fit to 3 d.p., and on T466's own
  crop and oracle still the shipped default Chase scores **0.7383** (T466's fit
  minimum 0.7379; the pre-T474 binary scores **0.9951**). The 3x3x3 re-fit at
  that view reproduces T466's bracket to <= 0.001. Correction to T471's B650
  arithmetic: the pre-T474 camera at that spot sat 11.500 above the ground, not
  13.119 -- the eye-region ceiling clamped it. **Still open:** B721 (the
  crop-dependent Chase fit, superseding B683 -- pointer updated 2026-09-20, card
  chase-closures-fix), B681 (races), B680 (does the official Chase zoom).

- **B680** (C -> ORACLE-OWED) **Whether the official Chase camera zooms.** The T474 → issue #57
  plan requires fable's zoom keys and wheel to act on the Chase distance, and
  they do (T474). T466 pressed Insert x5 then Delete x10 in the official Chase
  camera and saw no visible change (`review/T466/oracle/T466_oracle_keys_chase_*`;
  B653 owes the step sizes). If the official Chase does not zoom, fable's does
  something the oracle does not. Method: the same key presses in Chase, scored
  frame against frame. Found by T474.

- **B681** (C -> ORACLE-OWED) **How the Chase fit scales with race and posture.** → issue #58
  T466 fitted ONE character (Human male). T474 scales the rig UNIFORMLY about
  the feet by eyeAboveFeet / 6.023 (T431's oracle-solved Human male eye), using
  the head-bone eye fable derives per race (`scaleChaseRigForEye()`,
  `PlayerCameraComponent::chaseEyeAboveFeet()`), refreshed only while the player
  is STANDING (a sit does not rescale). None of that is measured: every race but
  a Human male, and every posture, is unmeasured. Known wrinkle: T431 left
  fable's Dwarf male eye at 4.53 against the oracle's 3.58, so a fable Dwarf's
  Chase scale is 0.752 where the oracle's eye ratio would give 0.594. Leg B did
  not reach the second race (it STOPPED on B683). Method: `#race 8` on
  Oraclealpha and Testchar at spot 1, oracle Chase still, fable `/camera set`
  sweep in Chase (the four values are the fitted character's, `/camera get`
  prints the scale). Found by T474.

- **B682** (B) **Two Chase behaviours changed shape when the follow camera became a → issue #190
  rig.** (1) The follow camera eased back out after a wall pull-in at 30 units/s
  (`ZOOM_RECOVERY_SPEED`); a rig is placed exactly every frame, so the Chase
  camera now snaps back when the wall clears. (2) The ceiling clamp's floor was
  a fixed half eye height (3.0) above the node origin; `placeChaseCamera()`
  floors it at the player's eye. Neither was measured on the oracle; both are
  written down in `camera_controller.h`. Found by T474.

- ~~**B683**~~ (A, **T474 STOP**) **SUPERSEDED by B721 -- see the T478 record
  below; struck 2026-09-20 (card kb-chase-closures, issue #59).** **The Chase re-fit at a second view disagrees with → issue #59
  T466's fit on distance.** Same spot and pose, heading 288, scored against
  T466's own h288 oracle Chase still on the crop that reproduces T466's h160
  numbers (`400x600+0+100`, recovered by reproduction:
  `review/T474/scripts/t474_recover_t466_chase_crop.py`): pitch 6 height 7 ->
  dist 2.0 0.8966, 2.5 0.8883, 3.0 0.8585, **3.5 0.7847**, 4.0 0.8038; pitch 6
  dist 3.5 height 6.5 0.7879, 7.5 0.9231; pitch 4.5 / 7.5 at dist 3.5 -> 0.9401 /
  0.8790. The h288 minimum is dist 3.5 (height 6.5-7.0), T466's fit is dist 4.0,
  and T466's stated sensitivity is +/- 0.3 -- so per the plan both are reported
  and neither is picked; `kChaseRig` stays at T466's fit, which the h160 re-fit
  reproduces exactly. The h288 bowl is shallow (dist 3.0 at height 6.5 = 0.8039,
  equal to dist 4.0) and the crop was recovered for the h160 view, where the
  near wall is at the left; at h288 the wall fills the frame. Owed (not run,
  Leg B stopped): a fresh oracle h288 grab, a crop chosen for that view, a
  genuinely second spot, and B681's second race. Found by T474.
  **SUPERSEDED by B721 (T478 record below), struck 2026-09-20 (card
  kb-chase-closures, issue #59).** T478 reproduced T474's h288 "dist 3.5" on
  T474's wall crop as the pitch-6 slice of a diagonal pitch/height valley whose
  minimum is off that plane, so the crop explains the disagreement and the
  clean-spot re-grab does not settle the fit. B721 (issue #61) carries the
  remaining work.

- ~~**B684**~~ **CLOSED by T480.** `PlayerState::setFollowDistance()` is deleted,
  and so is its `check_zero_callers.py` EXEMPT row. The cascade stopped at once.
  `followDistance()` and `m_followDistance` are not unreached: the follow logic
  in `movement_update.cpp` and `movement_component.cpp` reads them. The field
  is not write-only either, so it stays. It now always holds its 10.0f
  initializer. The original entry's "rest of that unreached state" was wrong
  about the getter. Original entry:
  **`PlayerState::setFollowDistance()` has no caller, and a name
  collision hid it.** The zero-caller census matches by name; its one "use" was
  `CameraController::adjustFollowDistance()` calling the controller's own
  `setFollowDistance()`, which T474 deleted with the follow camera. T474 added it
  to `check_zero_callers.py`'s EXEMPT under B496 (GameState setters with no
  caller) rather than delete game-state API outside its territory.
  `PlayerState::followDistance()` / `m_followDistance` are the rest of that
  unreached state. Found by T474.

---

## Added 2026-09-14 by T478 (fresh oracle grabs: Chase at two headings, held-item sets A-C, IT10)

Media and every number: `review/T478/README.md`; the plan's Review.

- **B584 NARROWED by T478** -- the cause is found. The "training dummies" are
  runtime spawns persisted by eqemu's **`zone_state_spawns`** table (npc 77002,
  `spawn2_id` 0): every `#npctypespawn` a unit leaves in qeynos2 is saved at zone
  shutdown and restored at the next boot, which is how thirteen stacked up on the
  canonical spot. T478 removed the 14 rows for its session and restored them at the
  end, as the plan required, so **they are still in the database**. Removing them
  for good is a DB decision for Will (B720).
- **B586 NARROWED by T478** -- attack captures taken on both clients. fable's
  `/target` failed for T466 because `a training dummy` (spaces) cannot substring-
  match the spawn name `a_training_dummy000`; `/target a_training_dummy` works (all
  four T478 sessions: "Targeting: a_training_dummy000", "Auto-attack enabled",
  `ZoneProcessDamage` during the attack steps). The official client attacks with F8
  plus Q, chat "You try to slash/crush/punch a training dummy". What is still open
  is B722.
- **B587 CLOSED by T478 (live).** fable default Chase, Testchar not moved since
  zone-in: the back of the head, back and shoulders, as the official Chase frame
  (`fable/sessions/chase_sweep_h160`, frame 163).
- **B661 NARROWED by T478** (~~now CLOSED by T485~~ -- this line is the T478 record; the live entry is struck above) -- the oracle frame is captured. No item that can be
  held uses IT10 (every IT10 item is an arrow, ammo slot only), so both clients were
  given the model with `#wc 7 10`. Official (Tether, idle): the arrow hangs DOWN along
  the right leg, tip near the foot. fable: the arrow is horizontal, pointing forward
  (az 90) or back (az 270) from the hand. See B725.
- **B680 NARROWED by T478** -- the wheel does not zoom the official Chase. Video
  `review/T478/oracle/T478_oracle_b680_chase_zoom_keys_and_wheel.mkv`: Insert x5,
  Delete x10, wheel up x5, wheel down x10 in Chase all score 0.002-0.141 against the
  pre-zoom still (idle floor), before vs after 0.079. Positive control in First
  Person: wheel down x5 moves the view (0.787) and wheel up x10 returns it exactly
  (0.000). Insert/Delete have NO positive control (no change in First Person
  either), so whether those keys zoom anything is still unmeasured. fable's wheel
  zooms Chase; the official one does not.
- **B683 SUPERSEDED by B721.** T474's h288 "dist 3.5" is reproduced on T474's wall
  crop as the pitch-6 slice of a diagonal pitch/height valley (h288 p6/h7: dist 3.5
  0.791 < dist 4 0.817), whose minimum is off that plane. The clean-spot re-grab does
  not settle the fit; see B721.

- **B720** (C) **The qeynos2 `zone_state_spawns` rows keep the training dummies → issue #60
  (and any future GM spawn) alive across zone boots.** 14 rows of npc 77002 on or
  near the canonical camera spot, restored by T478 per protocol. Decision: delete
  them for good (and add "no lingering `#npctypespawn`" to the eqemu protocol, or
  have units `#depop` their spawns), or keep them as a deliberate melee target.
  Found by T478.
- **B721** (A, **T478 STOP**) **The Chase fit depends on the scoring crop, and no → issue #61
  rig is the minimum at both headings on either crop.** Fresh official grabs on the
  clean spot (three per heading, re-grab floor 0.02-0.08). T474's recovered crop
  `400x600+0+100` (the wall; it leaves out the player): h160 minimum p6 / d4 / h6.75
  (0.662), h288 minimum p7.5 / d4 / h7.5 (0.695). Distance agrees at 4.0, but pitch
  and height are two and three grid steps apart along a diagonal valley. A
  player-centred crop `424x668+300+100`: h160 p6 / d2.5 / h6.5 (0.766; d2 0.770, d3
  0.790), h288 p4.5 / d2.0 / h6.5 (0.795; d1.5 0.887, d2.5 0.878 in that session).
  The official figure's head is about 1.6x the width of fable's at the shipped Chase
  (`review/T478/sheets/T478_chase_h{160,288}_oracle_vs_fable_default_vs_fable_minimum.png`).
  Default Chase (p6 d4 h7): 0.763 / 0.817 on the wall crop, 0.800 / 0.946 on the
  player crop. The same rig scores up to 0.033 differently between two sessions.
  `kChaseRig` unchanged. Owed: a metric that uses the whole frame without being
  dominated by the model's shading (e.g. a silhouette or keypoint measure of the
  head), or a direct measure of the official camera's distance. Found by T478.
- **B722** (A, *owed*) **No swing pose is visible in the official client's Tether → issue #62
  frames while its chat logs the swings.** Every distinct rendered frame of 16 s of
  auto-attack (sets A, B, IT10: 111 / 93 / 100 frames): the arms keep the idle pose
  while chat logs "You try to slash / crush / punch a training dummy, but miss!". Set
  C (2H): the blade angle differs between frames. fable's dumped frames show swing
  poses on every set. Not yet known whether the official client skips the swing on a
  miss, or whether the Wine capture misses it. Found by T478.
- **B723** (A, *unconfirmed* -- **CONTAMINATED, do not act on it**: T485 read → issue #63
  `review/T478/logs/oracle_setB_db.log` and the inventory write that put the shield
  on Oraclealpha sits beside garbage rows, so "no shield visible" may mean the
  official client never had one to draw; a clean re-grab of set B is owed before
  any shield work) **Set B: fable draws a kite shield on the left arm;
  the official client shows no shield.** Shield of the Forlorn (1523, IT202) in the
  secondary slot, idle, four official views (Chase, User 1, User 2, Tether): no
  shield visible; fable az 90 / az 270: a kite shield on the left arm, face outward.
  Found by T478.
- ~~**B724**~~ **CLOSED by T485** (the searched correction puts the 2H blade forward and
  level with the crossguard vertical; `review/T485/T485_after_c20_*`). Original text: (A, *unconfirmed*) **Set C: the 2H sword is held differently.** Spiny Two
  Handed Sword (2894, IT10004). Official (Tether, User 2): roughly horizontal in
  front of the waist, pointing at the dummy. fable (az 180, az 270): hanging
  vertically from the right hand to near the ground. Found by T478.
- ~~**B725**~~ **CLOSED by T485** (the same correction hangs the arrow's -Z down the leg;
  `review/T485/T485_after_c20_*`). Original text: (A, *unconfirmed*) **IT10 (arrow model) is held along a different axis**
  -- the oracle evidence B661 asked for: official down along the leg, fable
  horizontal (sheet `review/T478/sheets/T478_held_IT10_idle_oracle_top_fable_bottom.png`).
  Also set A: the official blade runs diagonally down and forward from the fist,
  fable's straight down along the leg. Found by T478.
- ~~**B726** (A, *unconfirmed*) **fable's qeynos2 sky and distant trees are drawn
  wrongly at daylight.** In every T478 fable frame (x86 `build-gles2`,
  `--eqe1-path`, `#set time 11 0`) the sky is a tan/orange mottled texture and the
  distant trees are pale white silhouettes; the official client at the same spot and
  time shows a blue sky with clouds and green trees. Not investigated (outside
  territory). Found by T478.~~ **STRUCK T484 (`task/T484-sky-colour`, offline).**
  The SKY half was one defect: `SkyRenderer::updateSkyLayerColors()` wrote the
  time-of-day tint into the dome's vertex colours and the GLES2 driver uploads
  `SColor`'s stored bytes unconverted, so SOLID3D_FS received the 11:00 tint
  (140,175,220) as (220,175,140). Measured headless on the real assets through
  the real `SkyRenderer`: dome (126.0,117.3,104.0) before, against T478's live
  (127,118,104); (80.2,117.3,163.6) after. Pinned by
  `tests/test_sky_dome_tint_channel_order.cpp`. The TREES half is not the sky
  and moves to **B782**; the live confirmation at T478's pose is **B783**; the
  driver convention the fix works around is **B780**.
- **B727** (P) **`#equipitem` is not a command on this server** ("Command '#equipitem
  14' not recognized"), so an official-client held set cannot be changed in world
  with GM commands: `#summonitem` lands on the cursor and there is no equip. T478
  changed sets by writing inventory rows with the character camped and the zone
  bounced (`review/T478/scripts/t478_oracle_set_by_db.sh`). Found by T478.
- **B728** (P) **The oracle driver's chat-command retry sends Escape, which clears
  the target.** `t466_o.sh` / `t474_o.sh` `cmd` press Escape before a retry; T478's
  first set A attack ran with no target ("You no longer have a target.", then "You
  must first click on the being you wish to attack!"). Take and prove the target
  AFTER any retried command (T478's `t478_oracle_held_set_capture.sh` v2). Found by
  T478.
- **B729** (P) **`eq_remote` drops stdin (`ssh -n`), so `eq_docker exec -i ... <
  file` imports nothing and exits 0.** T478's first restore deleted rows and re-
  imported nothing; the row-count diff caught it and the lock was kept. Pipe a dump
  with a plain `ssh` (T478's `t478_restore_v2.sh`), and count the rows after.
  Found by T478.

## Added 2026-09-14 by T484 (the daylight sky's vertex tint, B726)

- **B780** (C) `docker/irrlicht-drm/COpenGLES2Driver.cpp` → issue #64
  `drawVertexPrimitiveList()` + the `drawMeshBuffer()` VBO path -- **every vertex
  colour reaches the GLES2 shaders with red and blue exchanged.** Both paths give
  GL the four bytes at `S3DVertex` offset 24 as `GL_UNSIGNED_BYTE` RGBA; an
  `irr::video::SColor` is a u32 0xAARRGGBB, stored B,G,R,A on a little-endian host.
  MEASURED T484 (headless GLES2, `SkyDomeTintChannelOrderTest.DriverDeliversVertexColourBytesInMemoryOrder`):
  a pure-red vertex colour over `normalsky` reads (0,0,190). Grey colours are
  immune, which is why it went unseen. T484 compensated for the SKY only
  (`include/client/graphics/gles2_vertex_color.h`) and did NOT touch the driver,
  because **the zone shaders read `aColor.rgb` as baked light through the same
  upload** (`zone_geometry.cpp` `bakedVertexColor()` packs it as `SColor(A,R,G,B)`,
  and qeynos2's baked means are B 26.8 / G 53.5 / R 53.1 per
  `test_baked_vertex_lighting.cpp`), so on the GPU zone baked light is B/R-swapped
  today, and T405/T428's live-solved lighting constants were solved WITH that swap.
  *Unconfirmed* for the zone path: T484 did not render a baked-light mesh. Other
  non-grey built-in-program consumers found by grep: the
  tumbleweed per-vertex colours, the boids and `ParticleManager` billboard quads,
  the door placeholder meshes (`door_manager_mesh.cpp`) and `detail_chunk.cpp`.
  **T486 addendum (reading, no render): "not rendered" was wrong for at least
  three of these.** `ParticleManager::render()` builds each billboard's four
  `S3DVertex` with `SColor(alpha, r, g, b)` from the particle's own colour
  (`particle_manager.cpp`, the quad draw through `drawVertexPrimitiveList`) and
  is called EVERY frame from `FrameComponent::renderParticleEffects()`
  (`renderer_frame_render.cpp`, gated only on `isEnabled()` and `zoneReady`), so
  every non-grey particle -- fire, spell sprites, weather -- is drawn R/B-swapped
  today. The tumbleweed mesh bakes two browns per vertex
  (`tumbleweed_manager.cpp`, `SColor(255,180,150,100)` / `(255,140,110,70)`) and
  is drawn whenever a tumbleweed is alive; the door placeholder prism bakes
  `colorFromName()` per vertex (`door_manager_mesh.cpp`) and is drawn for every
  door whose `_obj` mesh is missing. None of the three was measured; the sky was.
  The decision between fixing the driver and per-consumer helpers stays Will's.
  Closes on Will's call between fixing the driver (one place, then re-solving the
  lighting constants live, the B445 queue) and a per-consumer helper like T484's.
  If the driver is fixed, T484's test fails by design and the helper goes.
- **B781** (A, *unconfirmed*) `src/client/graphics/sky_color_table.cpp` → issue #65
  (moved there from `sky_renderer_colors.cpp` by T495)
  `SkyColorsAt()` -- **the time-of-day tint table is invented,
  not fitted to the oracle.** With the channel order fixed the headless zenith
  reads (80.2,117.3,163.6); T478's official sky crop is (93,135,172) and
  `normalsky.bmp` alone is (147,172,190). Whether the official client tints the
  dome at all at 11:00, and by what, is unmeasured; the sky crop is also a blend
  of dome, clouds and horizon fade, so the two numbers are not directly
  comparable. Closes with a live A/B of the sky crop at several hours against the
  oracle (lock), after B783. Found by T484. **NARROWED by T495:** the question is
  now purely "are the ANCHOR VALUES right". T495 proved the arithmetic BETWEEN
  the anchors was wrong (B789/B790) and fixed it without touching a single
  anchor, and `tests/test_sky_color_interpolation.cpp` now asserts every
  interpolated colour lies inside its anchors' hull -- so a future re-fit of the
  table changes the anchors and nothing else, and the ten anchor sets are the
  whole of what is unmeasured. T484's headless zenith figure (80.2,117.3,163.6)
  is at 11:00, a CONSTANT arm of the table, so it is unaffected by T495 and
  stays the number to A/B.
- **B782** (A, *unconfirmed*) **the distant qeynos2 trees are pale in fable** -- → issue #66
  B726's second half. T478 h160 sheet canopy crop: fable (138,153,160), official
  (47,63,54). NOT the sky: fog starts at 250 (T478 run.log `Fog: start=250,
  end=300`) with the trees much nearer, and the dome is drawn with depth write off
  behind them. The 160 trees are wind-shader objects (run.log "fixup pass ... trees
  found=160, wind applied=160"). Not investigated (object/tree rendering is outside
  T484's territory). Found by T478, narrowed by T484.
- **B783** (P) **T484's sky fix has no live frame at T478's pose** -- Leg B, lock.
  Re-take T478's h160 Chase still on a tip with T484 merged (`--eqe1-path`, `#set
  time 11 0`, `#set weather 0`) and report the same sky crop beside the official
  (93,135,172). The offline proof is `review/T484/`. Found by T484.

---

> **Orchestrator note (2026-09-14):** the two sections below were written on the UNMERGED branch `task/T479-skeleton-mesh-list-loaders` (commits `64c9273d` T479, `dfb3a0e8` T481) and copied here so the board of record carries them. B730–B732 are fixed ONLY on that branch; the branch is held because the T479 build is OOM-killed on the Pi (B750) until Will picks a lever (B752). Will must also bless the qeynos2 `placeableObjectsBytes` gate figure (1,365,564 → 1,389,324) before that branch can merge.

## Added 2026-09-14 by T479 (the 0x10 skeleton mesh list in the character and object loaders)

Census of every 0x10 in the 94 global, 2,592 zone-chr and 654 zone-object
skeletons: `review/T479/census/` (`skeletons.csv`, `objects.csv`), taken by
`SkeletonMeshListCensus.CharacterAndObjectArchives`.

- **B730** (A) **FIXED ON THE HELD BRANCH ONLY — `task/T479-skeleton-mesh-list-loaders` (commit `64c9273d`), UNMERGED** (Pi OOM B750, lever B752, qeynos2 gate bless owed); on `fable` this is still open. Re-marked by T486 (the copied text said CLOSED). Text as written on that branch: `S3DLoader::loadObjects()` dropped every → issue #67
  placement of an actor whose 0x14 reaches a 0x11 skeleton and no 0x2D mesh:
  644 placed actors, **95,666 placements in 158 zones** -- trees (196 actors,
  80,768 placements, meshes hung from bones: emeraldjungle 22,174, trakanon
  14,920, wakening 13,833), and torches, braziers and flags (447 actors, 14,897
  placements, skinned meshes listed after the bones). Such an actor now resolves
  through `resolveEquipmentActorGeometry()` (bone meshes and listed skinned
  meshes at their rest pose), one geometry shared by all its placements. Pinned
  by `SkeletonMeshListLoaders.SkeletalObjectIsPlacedAtItsRestPose` (thedeep
  TDTORCH301 0/16 -> 16/16, qeynos2 TEMPLELIFE 0/1 -> 1/1). **Moves one
  Will-blessed gate number:** `memoryCoverage` qeynos2 `placeableObjectsBytes`
  1,365,564 -> 1,389,324 (TEMPLELIFE and TEMPLELIFEB); no image golden moved.
  No live look; Pi cost of the added placements unmeasured (B735).

- **B731** (A) **FIXED ON THE HELD BRANCH ONLY — `task/T479-skeleton-mesh-list-loaders` (commit `64c9273d`), UNMERGED** (Pi OOM B750, lever B752, qeynos2 gate bless owed); on `fable` this is still open. Re-marked by T486 (the copied text said CLOSED). Text as written on that branch: A character mesh the race-code NAME PREFIX → issue #68
  handed to two skeletons was skinned twice, because `applySkinning()` works in
  place on the shared mesh: CL_HS_DEF's "CL" took CLF_* and CLM_* (akanon, races
  88/457 Clockwork Gnome), KA_HS_DEF's "KA" took KAF_*/KAM_* (kaladima/b, race
  94 Kaladim Citizen), and powar's two STG_HS_DEF skeletons shared STG_DMSPRITEDEF.
  Vertices were off by 5-8.5 units, the animation copies too. Each character now
  skins its own copy, and the prefix no longer takes a mesh another skeleton
  lists. Pinned by `SkeletonMeshListLoaders.CharacterMeshIsSkinnedOnceByItsOwnSkeleton`.

- **B732** (A) **FIXED ON THE HELD BRANCH ONLY — `task/T479-skeleton-mesh-list-loaders` (commit `64c9273d`), UNMERGED** (Pi OOM B750, lever B752, qeynos2 gate bless owed); on `fable` this is still open. Re-marked by T486 (the copied text said CLOSED). Text as written on that branch: `S3DLoader::loadCharacters()` never read the → issue #69
  meshes a skeleton lists. 49 zone-chr skeletons (19 names: KAR, AAM, PHX, GTD,
  BKN ...) whose listed meshes carry no race-code prefix stored no model; 8
  names missed listed meshes -- GOM (body and head: its two bone meshes stopped
  the prefix), SSN (head SNNHE00_DMSPRITEDEF), UDK (HSN_*), SRG (cape), TRT, CCD,
  GOJ, IVM. Fitting listed meshes are now parts. The real loader's output over
  all 419 chr archives before/after is `review/T479/census/chr_loader_dump.diff`:
  no global archive and no gate zone moved. Pinned by
  `SkeletonMeshListLoaders.EveryMeshASkeletonListsIsAPart`. Of the affected codes
  only CLM/CLF, KAM/KAF, SSN, STG and IVM have a `race_models` row.

- **B733** (A, *unconfirmed*) **The race model builder selects parts by NAME, so → issue #70
  a listed part with another name is still dropped.** `RaceModelLoader` keeps
  `{CODE}_DMSPRITEDEF` + `{CODE}HE00_DMSPRITEDEF` whenever either exists
  (`loadModelFromZoneChr`, `loadModelFromCachedChr` in `model_loading_chr.cpp`,
  `searchAndBuildModel` in `model_loading_search.cpp`, `model_loading.cpp`,
  `variant_loading_animation.cpp`). The character now carries SSN's head
  SNNHE00_DMSPRITEDEF and SRG's cape, but that selection would drop both.
  Confirm with a model-build test for race 146 (Spectral Sarnak). Found by T479.

- ~~**B734**~~ **CLOSED on `task/kb-b734-object-geometries-posed` (issue #71).** `loadObjects()` now poses skinned
  object geometries (vertex pieces on a 0x10 skeleton, flag 0x200) through T476's
  `resolveEquipmentActorGeometry` — the same rest-pose path the equipment actors
  use, not a second skinning implementation — and stores them in
  `S3DZone::objectGeometries` under the ACTOR name the placement and
  `DoorManager::findDoorMesh` use (thedeep: `TDTORCH301`), with the MESH name
  (`STOR301`) aliased to the SAME shared_ptr so no geometry is copied and
  `getMemoryUsage()` accounts each distinct geometry once. thedeep's `TDTORCH301`
  now has non-degenerate bounds (x[-0.568,0.565] y[-0.492,0.490] z[0.906,4.067]);
  the `objectGeometries` byte census (per-key sum of `getFullMemoryUsage`) goes
  157,538 -> 171,968 for thedeep (20 -> 21 keys; the actor key is added, the
  skinned mesh's own raw geometry is REPLACED by the rest-pose one, and the
  mesh name aliases the same shared_ptr — 20 distinct geometry pointers both
  before and after, so no geometry is copied twice; `getMemoryUsage()` dedupes
  the alias) and 1,798,921 -> 3,169,268 for bothunder (20 skinned keys: the
  rest-pose geometry also carries the skeleton's bone meshes, which the
  un-posed storage omitted). Pinned by `test_door_loading.cpp`
  (`B734ObjectGeometriesTest`, headless, asset-gated by
  `tests/asset_requirement.h`). `refactor/B734_object_geometries_posed.md`.
  Found by T479.

- ~~**B735**~~ **MEASURED by T481 -> B750.** The Pi cost of B730's placements:
  qeynos2 and nro fit on the T479 build; emeraldjungle (+22,174) is OOM-killed
  during the object build. warslikswood and most thedeep runs could not zone in on
  either build (B751), so their cost is still unmeasured. Found by T479.

- **B736** (B, *unconfirmed*) **`S3DLoader::loadZone()` rejects 20 chr archives → issue #191
  with "No geometry"** -- global3_chr.s3d and nineteen `global*_chr2.s3d` -- so any
  model or texture only they hold is unreachable through `RaceModelLoader`.
  Measured by the T479 dump (`LOAD_FAILED` rows); not checked whether they hold
  models. Found by T479.

B737–B739 unused.

## Added 2026-09-14 by T481 (the Pi cost of T479's skeletal object placements)

Runs, tables and scripts: `review/T481/` (`pi/table_all_runs.tsv`, `pi/raw/<run>/`,
`pi/oom_evidence/`). Pi at 1280x720, `--constrained orangepi --eqe1-path`, before
= fable-tip ARM build, after = T479 rebased on `bb248847`.

- **B750** (A, *T479 branch only, not on fable*) **The T479 build is OOM-killed on the → issue #72
  Orange Pi loading emeraldjungle.** The kernel killed `willeq` (total-vm 809,476 kB,
  anon-rss 212,592 kB) at `Building objects [22000/24505]`; swap was exhausted
  (SwapFree 0, MemAvailable 15 MB), exit 137. RSS+swap grew about 7.6 kB per placed
  object (296 MB at 5,000 -> 424 MB near 21,800), and no budget warning fired first.
  The fable build loads the same zone (2,331 objects) at 323.5 MB RSS+swap with 46 MB
  left. qeynos2 (478 -> 480) and nro (345) fit on both builds; thedeep fit on the one
  T479 run that zoned in (414 objects). T479 must not merge for the Pi until a lever
  (B752) lands. Pinned only by the live run: `review/T481/pi/raw/emeraldjungle_2_after/`.
  Found by T481.

- **B751** (A, *cause unconfirmed*) **On the Pi, a login into a dynamic zone often → issue #73
  stalls at `[Phase 9/15] (40%) Finalizing connection`**, right after the client's
  weather packet: no "Zone connection complete", no asset load, while the server holds
  the character in-zone (5-minute saves, linkdead only when the client is killed).
  Measured on BOTH builds: thedeep 3 of 4 logins, warslikswood 4 of 4. emeraldjungle's
  one fable-build login did not stall. The static fleet zones qeynos2 and nro reached ready
  in 9 of 9 logins. Idle zones are not the cause: every qeynos2 success connected to a
  zone that had gone idle. Not B511 (that is x86 NFS `rmem_max` 212,992; the Pi's is
  1,048,576). Logs: `review/T481/pi/raw/{thedeep,warslikswood}_*`, the eqemu zone logs
  on the VM. Found by T481.

- **B752** (C) **Which lever makes B750 fit on the Pi is Will's call.** Candidates, → issue #74
  none tried:
  - per-object build or draw distance (PVS or distance), or a distance for the 196
    skeletal tree actors, because every placement's mesh is built eagerly;
  - instancing a shared mesh with a per-placement transform instead of the per-placement
    copy (where the copy is made -- node or world-space merge -- is unmeasured);
  - bringing object geometry under the `orangepi` preset's RAM budget, which raised no
    warning before the kill;
  - a placement cap on the Pi preset, which drops objects.
  Found by T481.

- **B753** (P) **Three rig traps from T481's Pi runs.**
  1. Replacing a script on NFS with `mv` from the dev box while the Pi's bash is
     executing it kills that bash when it next reads the file (stale handle).
     T481 lost `warslikswood_2_after`'s exit code that way. Ship changes under a new
     file name.
  2. `pkill -f PATTERN` inside `ssh host '...PATTERN...'` or under `sudo` matches its
     own command line and kills itself or its parent. Bracket one character:
     `pkill -f "[u]input_inject"`.
  3. T459's injector waits the whole `--trigger-deadline` after the client has died.
     A watchdog must kill the injector as well as the client.
  Scripts: `review/T481/scripts/run_pi_v3.sh`, `uinput_inject_v3.py`. Found by T481.

- **B754** (P) **Pi memory: read RSS+swap, not VmRSS, and expect swap in use.** In
  T481's 18 runs swap was 18-88 MB even where the zone fit, and the killed run's RSS
  stayed near 200-250 MB while swap filled. Runs that never loaded a zone
  (B751 stalls) still peaked at 196-235 MB VmHWM, so for small zones the resident
  peak is set before the zone load. `/pmem`'s `[Placeable Objects]` prints 0.1 MB
  steps, too coarse for a gate-sized change such as qeynos2's +23,760 bytes.
  Found by T481.


## Added 2026-09-16 by T492 (the three-player LLM-benchmark corpus rig)

- ~~**B784**~~ (A) `scripts/scenario.sh` `restore_snapshot()` -- **the "restore with the
  world DOWN" step does not actually keep the world down, and is slow enough for
  that to matter.** It stops world/zone/loginserver with `pkill` INSIDE the
  eqemu-server container, but that container's entrypoint supervisor brings the
  fleet straight back (the launcher B483 documents), and the restore then issues
  one `ssh` round trip PER ROW -- ~90 rows, **measured at 2 min**. MEASURED T492:
  a teardown that killed the processes and restored 90 rows over those two
  minutes found `Testbuddy`'s row written back underneath it (zone 2 / -64 / 428
  / 3.75 / 160 instead of the snapshot's zone 36 / 0 / 0 / 3 / 0) while the other
  three rows, whose characters were not logged in, came back clean -- so the
  symptom only appears for a character that was in world, which is every
  character a live unit touches. T492's recorder does it the way T478's restore
  v2 did: `docker stop` the eqemu-server container (mariadb stays up) and send
  the whole restore as ONE statement batch -- **25 s, byte-identical**
  (`scripts/llmbench/teardown.sh`, `record.sh` `restore()`). `scenario.sh` is
  UNCHANGED and still has both halves of the defect. Found by T492.~~
  **FIXED T493**, and the mechanism was WORSE than the entry says. Two things
  were measured on the local lane before anything was changed:
  (1) the `pkill` chain **killed its own shell**. The container-side
  `bash -c "pkill -f /tmp/spire-launcher; pkill -f 'bin/world'; ..."` matches
  the FIRST pattern against its own command line, so the exec died with rc 143
  and the other three `pkill`s never ran at all -- **27 world/zone processes
  were still alive when the restore finished**. The supervisor never even had
  to respawn anything.
  (2) **the zone writes its in-memory copy of a character back when the ZONE
  SHUTS DOWN, not when the client disconnects.** A row moved to a sentinel
  while `Testbuddy` was in world survived the client's death by 60 s untouched,
  and went back to the zone's copy the instant the zone got SIGTERM. So the old
  restore printed `snapshot restored BYTE-IDENTICALLY` **and was then undone by
  the `down.sh` that followed it** -- which is why nothing in the tree had ever
  caught this: the after-snapshot diff runs before the teardown that does the
  damage. Reproduced exactly (`review/T493/`): sentinel 1111/2222 restored,
  teardown SIGTERM, `Testbuddy` back at 0/0.
  The fix is ONE shared implementation, `scripts/eqemu/restore_lib.sh`
  (`eqemu_world_stop` = `docker stop` the eqemu-server container, mariadb is a
  separate container and stays up; then one statement batch), sourced by
  `scripts/scenario.sh`, `scripts/llmbench/teardown.sh` and
  `scripts/llmbench/record.sh` -- the three duplicates are gone. Measured on
  the local lane: **restore 17 s, byte-identical through the teardown, 0 world
  processes alive**, against the old path's 22 s local (2 min over ssh) and a
  clobbered row. `scripts/scenario.sh` also now restores on the `--keep-server`
  path, which it previously skipped entirely, leaving a moved character behind
  for the next unit.
  Pinned by `scripts/eqemu/restore_selftest.sh`, reached as
  `scripts/scenario.sh --selftest`: it logs a character in, makes the snapshot
  and the world disagree on purpose, restores, runs `down.sh`'s SIGTERM step
  and only THEN diffs, and asserts the restore's wall time and that no
  world/zone process survived it. `--legacy` runs the pre-T493 restore and
  FAILS (exit 66), so the net is known to test something. Real end-to-end proof:
  `scenario.sh zone_in` exit 0, byte-identical, 44 s total.
- **B785** (B) `review/T478/scripts/t478_o.sh` (and T466's and T377's copies of → issue #192
  it) -- **the oracle driver stamps its marks with `date -u` while every willeq
  `run.log` stamps LOCAL time**, and the two are the halves of every joined
  corpus. Both hosts are UTC today, so nothing measured so far is wrong; the
  moment a rig host is not UTC, marks and logs are silently offset by the zone
  and no check in the tree notices. T492's copy (`scripts/llmbench/o.sh`) uses
  local time; the T478 originals are untouched, being evidence. Found by T492.
- **B786** (P) **`/mnt/projects/eq-oracle/CLAUDE.md` and `README.md` are stale on
  all three facts a session needs before it can start.** They give the eqemu VM
  as `10.0.30.23` (it moved to 172.16.200.99 on 2026-09-12), the lock as
  `/mnt/projects/fable/.eqemu-lock` (it is `/mnt/projects/.eqemu-lock` ON the
  eqemu host, taken with `eqemu_lock_take`), and say "**NOT installed: wine** --
  that is prerequisite #1" (wine has run the client on the dev host since T310
  and on hermes-ext since 2026-09-13). `env.sh` beside them is correct, which is
  what makes the prose dangerous. Found by T492.
- **B787** (B) **willeq at `-d 4 --log-level=DEBUG` is 90% per-frame GRAPHICS → issue #193
  visibility counts.** MEASURED T492 on hermes-ext: 60,891 of 67,407 lines in the
  first 90 s of a session are `[DEBUG] [GRAPHICS]` `ObjectVis/ZoneLightVis/
  ObjLightVis/EntityVis/RegionVis` lines, one set per frame -- 6.7 MB of log for
  90 s of play, ~2.7 MB/min, which is what a consumer of the log (the LLM
  benchmark's agent loop, any live-unit grep) has to read past. The counts are a
  profiling aid, not per-frame news; TRACE is the level for them. T492 works
  around it with `--log-module=GRAPHICS:INFO`. Found by T492.
- **B788** (B) `/mnt/projects/eq-oracle/scripts/enter-world.sh` -- **the Enter → issue #194
  World flow has no settle and its "4 attempts" are not the budget they look
  like.** `wait_for_text <n>` is n OCR POLLS, not n seconds, and each poll is an
  `import` + `convert` + `tesseract` over a 3D scene under llvmpipe, so one
  attempt is ~90 s and the four are ~6 min. The character-select scene takes
  ~2 min to render the first time and clicks issued before it is up are dropped
  silently (hermes-ext note 2026-09-13), so attempts 1-3 are usually spent
  clicking at nothing. MEASURED T492: 5 min 12 s from character select to in
  world, succeeding only on the fourth attempt, twice in a row; with a 60 s
  settle first it is one attempt. T492's launcher adds the settle around the
  rig's script (`scripts/llmbench/oracle_start.sh`); `enter-world.sh` itself is
  UNCHANGED, being the shared rig. Found by T492.

## Added 2026-09-16 by T492 Leg E (Will's ruling on the corpus sheets)

- ~~**B789** (A) **the qeynos2/qeytoqrg sky renders RED in fable, not blue.**
  **Will's ruling, 2026-09-16, looking at T492's sheets: this is a bug, not a
  rig artifact.** MEASURED on `review/T492/llmbench/players/p2/frames/
  15:38:13.227_F_sky_p2_overhead_window.png`, crop `600x60+200+10` (a sky-only
  band at the top of the frame), mean **(80, 29, 29)** of 255 -- red is 2.75x
  green and 2.75x blue. The official client's frame at the same step is
  `players/p1/frames/15:38:09.793_F_sky_p2_overhead_window.png`. Binary:
  `build-gles2` at `566ac88a` (T484's channel-order fix IS in it),
  `--eqe1-path`, 1024x768, qeynos2 at the server's own time.
  **This is NOT B781.** B781 is "the time-of-day tint TABLE is invented, not
  fitted": its measured headless zenith is (80.2, 117.3, 163.6) -- a blue that
  is merely the wrong blue. A band whose red is nearly triple both other
  channels is a different failure, downstream of whatever B781 is about, and
  T484's B726 channel-order fix is already in this binary. Not investigated:
  Will ruled it a bug and ruled that T492 does not chase it. Found by T492.~~
  **STRUCK T495 (`task/T495-sky-colour-interp`, offline) -- B789 AND B790 ARE
  ONE DEFECT.** `SkyRenderer::interpolateSkyColors()` lerped each channel as
  `a.getRed() + (b.getRed() - a.getRed()) * t`, and `SColor::getRed()` returns
  `irr::u32`: on every ramp where a channel DECREASES the subtraction wrapped to
  ~4.29e9, `* t` in float zeroed the low bits, and the `& 0xff` in SColor's
  constructor left 0, 64, 128 or 192. The garbage reached THREE consumers -- the
  dome vertex tint, `currentClearColor_`, and `getRecommendedFogColor()` ->
  `IrrlichtRenderer::updateTimeOfDay()` -> `driver_->setFog()` -> `uFogColor`,
  which every fogged pixel is mixed toward. That is both boards: one arithmetic,
  two consumers. **Path correction:** both entries cite the frames under
  `review/T492/llmbench/players/...`; they are actually under
  `review/T492/llmbench-v1-camera-steps-bad/players/...` (T495 re-measured
  there). MEASURED through the production code on the broken commit, with
  the corpus's own game times (`review/T492/llmbench/players/p2/run.log`:
  zone-in at **17:25**): tint **(161,58,58)**, clear colour = tint/2 =
  **(80,29,29)** -- byte for byte B789's measured sky band, and T495 re-measured
  the crop to confirm it is the CLEAR COLOUR rather than the fog: the flat pixel
  **(80,29,29)** occupies **35,748 of the 36,000 px** of B789's sky crop and
  **33,893 of 96,000** of B790's tree band, i.e. the scene background showing
  through wherever the dome or the geometry is not; at 16:45/16:49 the
  fog was **(171,180,0)** / **(172,180,0)**, blue exactly zero; at 18:30 all
  three anchors collapsed to **(0,0,0)**. Fixed by lerping in float
  (`src/client/graphics/sky_color_table.cpp`, `lerpChannel()`); after, 17:25
  tint **(161,151,156)** / clear **(80,75,78)**, 16:49 fog **(172,180,192)**.
  Pinned by `tests/test_sky_color_interpolation.cpp` (pure, 8 cases) and two new
  cases in `tests/test_sky_dome_tint_channel_order.cpp` (headless GLES2, real
  dome: 16:49 (91.66,120.02,**0.00**) -> (91.66,120.02,**150.85**), 17:25
  (92.24,**38.86,43.09**) -> (92.24,**101.25,115.97**)). The visual gate is
  unaffected (zero pixel diff; `gles2_zone_benchmark` builds no sky/fog stack).
  `review/T495/`.
- ~~**B790** (A) **untextured pink/magenta trees and pink distant blocks in
  fable.** **Will's ruling, 2026-09-16, on the same sheets: a bug.** MEASURED on
  `review/T492/llmbench/players/p2/frames/
  15:46:54.538_H_cam_p2_qeytoqrg_chase_window.png`, crop `800x120+120+200` (the
  horizon/tree band), mean **(88, 35, 38)** of 255 -- again red/magenta
  dominant -- against the paired official frame
  `players/p1/frames/15:46:49.474_H_cam_p2_qeytoqrg_chase_window.png` at
  **(16, 30, 23)**, which is green dominant. Magenta is the conventional
  missing-texture colour, so a texture that never bound is the first place to
  look. **This is NOT B782.** B782 is "the distant qeynos2 trees are PALE",
  measured as fable (138, 153, 160) vs official (47, 63, 54) -- a desaturated
  grey-blue, not magenta, and in a different zone. Not investigated by T492.
  Found by T492.~~ **STRUCK T495 -- SAME DEFECT AS B789, and this entry's record
  needed three corrections.** (1) **Nothing is magenta.** T495 re-measured all
  three T492 corpora: the ONLY magenta in any of them is the target-highlight
  outline. This entry's own evidence frame means **(88,35,38)** -- dark RED, not
  pink. fable's actual missing-texture placeholder is a 0xFFFF00FF checkerboard
  (`ConstrainedTextureCache::getPlaceholderTextureInternal()`), mean ~(143,16,143)
  with R ~= B, and no band in any corpus frame shows it. (2) **The
  missing-texture lead is dead** -- the only texture-failure line the run emits
  is the door rebuild's "60 doors (0 failed, 0 with missing textures)", and no
  frame carries the placeholder's colour, so "a texture that never bound is the
  first place to look" sent the next reader the wrong way. (3) **The plan's
  EQE1 claim was WRONG and T495 corrects it rather than repeating it:** these
  runs did NOT load "one EQE1 entry plus sky". `EQE1-USAGE` for the qeynos2 leg
  lists **8 archives open, 48,696 KB mapped, 511 lookups** -- including
  `qeynos2` (152 entries) and `qeynos2_obj` (110), i.e. the zone and object
  textures DID come from EQE1. ("Loaded EQE1 archive config: 1 entries" is the
  archive-config file, not the loaded set; it is what the earlier reading
  mistook.) That does not rescue the missing-texture lead -- 0 wasted opens, 0
  idle zone archives -- but the sentence "EQE1 is not implicated because it was
  barely used" is not a thing anyone may quote. What the band actually is:
  `uFogColor`. Leg E's game time is **16:49**, where the broken fog was
  (172,180,0) with blue EXACTLY zero, and the far field of
  `llmbench/players/p2/frames/16:43:43.983_H_cam_p2_qeytoqrg_chase_window.png`
  measures (170.0,178.0,**0.75**) over the sky AND the distant trees. Fix,
  tests and after-values are on B789. T495's own re-measure of this entry's two
  crops: fable (88.02,35.37,38.03) with **zero magenta pixels** (none with R,B >
  120, G < 80), official (16.39,30.94,23.83). Fix, tests and after-values are on
  B789.
- **B791** (B) **nothing hides fable's renderer command replies without hiding → issue #195
  the whole UI.** Every registry-3 command answers through `chat()`, which puts
  it in the in-game chat window -- so a captured fable frame with the UI on
  carries `Screenshot saved: <path>`, `[CAMERA-GET]` dumps and combat lines that
  the official client would never show, on top of the scene being compared.
  The only lever is `/hideui` (`renderer_slash_commands.cpp`, T301 Leg D),
  which is all-or-nothing: it also removes the chat window, which is part of
  what a verification corpus wants to capture. T492 Leg E uses `/hideui` for
  its camera and held-item steps for exactly this reason and says so in the
  corpus README; every visual gate since T63 has fought the same overlay (the
  comment at the `/hideui` site says so). What is missing is a way to keep the
  UI and route command replies to the log only. Found by T492.
- **B792** (P) **a camera-identification score is only valid under the reference
  stills' own scene conditions, and nothing enforces that.** T466's six
  direct-mode stills -- the references every camera fit and every "which camera
  is this?" check scores against -- were taken at `#set time 11 0`,
  `#set weather 0`, pose pinned with `scripts/camera_match_pose.sh`, UI off.
  MEASURED T492 Leg F: with those conditions set, ten F9 grabs identified at
  **0.058-0.191** with the next-best >= 0.784; from the SAME client at the same
  spot minutes later WITHOUT them, the same scoring put every grab at
  **0.45-0.56** and named the wrong camera (User 1 for First Person), so a
  closed-loop `camto` that required < 0.30 never confirmed anything and a whole
  recording was discarded. The conditions are documented only inside T466's and
  T478's own scripts; `scripts/scenario.sh` and any new harness will omit them
  and get plausible-looking numbers that are wrong. T492 added
  `record.sh --oracle-conditions` for its own rig. What is missing is a single
  documented "oracle conditions" helper the scoring script itself can point at,
  or a refusal in `camera_match_score.py` when it is handed a candidate whose
  conditions were not stated. Found by T492.
- ~~**B793** (A) **the T492 corpus's `held=` truth values are wrong, and one of → issue #75
  them was never applied.** MEASURED T492 Leg G by `scripts/llmbench/verify_labels.py`
  against the run's own inventory snapshot: the `E_heldA_p2_*` steps claim
  Testcaster holds `slot13=9998, slot14=9979` while `character_data`/`inventory`
  give **14337** and **20656** (Testbuddy's `13=9997,14=9979` does match, so the
  check discriminates). Worse, the `/equip 23 13` the `E_heldB_*` steps depend on
  was REFUSED by the client -- *"Bread Cakes* cannot go in Primary (13) -- wrong
  slot type for this item"* -- so every `held=13=9990` claim is false: slot 23 is
  food, not a weapon. `refactor/T492_llmbench_three_player_corpus.md`'s
  `make_timeline.py` hardcoded both from a misread of the inventory dump and
  nothing checked them until Leg G. Fix: read the held values FROM the snapshot
  at compile time rather than hardcoding, and pick a slot that actually holds a
  weapon for the `/equip` step. The affected steps are parked
  `expect=unlabelled` with `why=`. Found by T492.~~
  **STRUCK 2026-09-20 (issue #75, `task/kb-b793-timeline-held-from-snapshot`):
  `make_timeline.py` now takes `--db-before <corpus>/shared/db_before/` -- the
  same file `verify_labels.py` scores held= against -- and derives every held=
  claim from its inventory.tsv + snapshot.tsv (reference snapshot committed at
  `scripts/llmbench/fixtures/T492/db_before/`); a missing snapshot is a hard
  error (exit 2), never a fallback to constants. The regenerated timeline's
  `E_heldA_p2_*` rows claim 14337/20656 and Testbuddy's stay 9997/9979; the
  `/equip` step drops its after-claim and the `E_heldB_*` frames are parked
  `expect=unlabelled` with a `why=` -- no repo file establishes a general
  slot's eligibility for Primary (13) (no slot-type/slots bitmask in the
  snapshot, no item TYPE in data/item_idfile.tsv [measured]), so the after-
  state is not guessed. Pinned by `test_timeline_held_snapshot.py`
  (`LlmBenchTimelineHeldFromSnapshot`, hygiene; RED on the pre-fix tree: the
  committed timeline claimed 9998 where the snapshot says 14337).~~
- **B794** (P) **a camera-identification threshold validated with one client in
  world does not transfer to a frame with three.** B792 is that the scene
  conditions must match T466's stills; this is the other half. MEASURED T492:
  with `#set time 11 0` / `#set weather 0` applied and the official client
  verifiably in Chase, the score against T466's Chase still is **0.415-0.495**
  (best by a factor of 1.7-2.3 over every other camera) where the same check
  single-client scored **0.058-0.191**. The other two players stand 10 units
  either side of Oraclealpha and are IN frame; T466's references have no other
  players. So `< 0.30` rejects a correct identification, which stopped T492 Leg
  G's recording (correctly -- the gate did its job). What is needed is a
  calibration under three-client conditions, or a best/next-best MARGIN
  criterion instead of an absolute one; raising the threshold until it passes is
  choosing the answer. `[assumed]` that the extra players are the whole of the
  difference -- the pose there is placed by SQL rather than pinned with
  `camera_match_pose.sh`, and the two were not separated. Found by T492.
- ~~**B795** (A) **the official client's "Tip of the Day" dialog sits over the → issue #76
  frame after entering the world, and nothing in the oracle rig dismisses it.**
  MEASURED T492 Leg H: with all three clients in world and a target set, the
  four candidate crops of the official TARGET window OCR'd as `es`, `a`, `a` and
  **`ow Another Tip`** -- the last is the dialog's own "Show Another Tip"
  checkbox, so the dialog is occupying the region the target window uses. The
  same crop reads `Testcaster` cleanly on a Leg C frame
  (`review/T492/llmbench/players/p1/frames/15:27:43.497_C_target_p1_p2_window.png`),
  so this is the dialog, not the crop. It stopped T492 Leg H's target gate and
  is the remaining blocker for the target steps. ~~It also means any official
  frame taken soon after entering the world may have the dialog over it --
  `review/T492/llmbench-legH/players/p1/frames/` has examples, and earlier units'
  post-login grabs should be re-checked before being trusted.~~ **The re-check
  half is DISCHARGED by T499**: `scripts/oracle/find_tip_dialog.py` (two
  independent signals, both required -- tesseract over the dialog rect and the
  title bar's invariant chrome bitmap matched anywhere in the frame; validated
  on three known positives and a known negative before the sweep) scanned
  **1,183 archived official frames across 17 units** and found
  **382 dialog, 799 clear, 2 unreadable** -- `review/T499/tip_dialog_audit.tsv`.
  **363 of the 382 are T492's own**; the other **19 are in six units** (T377 1,
  T386 1, T389 1, T401 3, T466 2, T478 11) and **not one of them fed a score**:
  13 are cited at all (`review/T499/dialog_frame_citations.tsv`), every citation
  is a grab log line or a "what this file shows" README row, and the two that
  back a claim back an OCR reading of chat/inventory text, not a number.
  T466's login-camera identification is the near miss: its scoring crop is
  `660x450` upper-left and the dialog's leftmost column in that frame is
  **664** -- clear by 4 px, by luck, not by design. T478's Chase fit and
  held-item sheets take only `_noui_` frames and all of those are clear, and
  T492 Leg F's F9 cycle -- the 20 grabs that validated the MARGIN criterion at
  min ratio 4.90 -- are clear too (F10 hides the dialog with the UI).
  **The one contaminated score is T492's own: board B833.** T499 also measured
  that this window has context-sensitive variants at the same rect, which this
  entry's title does not cover: board **B834**. Fix: dismiss it in
  `oracle_start.sh` after `enter-world.sh` (Escape, or a click on its close box,
  verified by OCR that "Another Tip" is gone) -- NOT by waiting longer, since it
  is modal until dismissed. `camto` is unaffected: it passed at ratio 1.993 with
  the dialog in frame. Found by T492.~~ **STRUCK B849 (2026-09-18, issue #76
  closed by the B849 unit)**: the fix as written is on fable —
  `scripts/llmbench/oracle_start.sh:34-49` (`[Tips] ShowTips=0`, T492 Leg I,
  `ed2ae5e5`) plus the runtime OCR-verified dismiss at :84-110, and the
  re-check half was already struck as discharged by T499 (`98a5931d`,
  `scripts/oracle/find_tip_dialog.py`, 1,183 frames swept). Nothing of B795
  remains; the after-action variants are the separate B834.
- ~~**B796** (A) `/equip <from> <to>` -- **the slot numbers the command takes are
  NOT `inventory.slot_id`; general slots are off by one.**~~ **STRUCK T494
  (`task/T494-equip-slot-convention`).** The off-by-one was real and is fixed;
  what B796 got wrong is WHERE it came from and what the experiment proved.

  **The mapping, MEASURED from the server source.** fable's inventory model,
  its wire and every `MoveItemIntent` are TITANIUM-numbered; `inventory.slot_id`
  is SERVER (RoF2) numbering; and nothing in fable converted between them,
  because the conversion lives entirely on the server --
  `/mnt/projects/akk-stack/code/common/patches/titanium.cpp:3576`
  `ServerToTitaniumSlot()` and `:3666` `TitaniumToServerSlot()`, with
  `/mnt/projects/akk-stack/code/common/emu_constants.h:155-156` aliasing `EQ::invslot` to `RoF2::invslot`.
  Identity to waist (20); **server 21 is slotPowerSource and has NO Titanium
  slot at all**; ammo 22<->21; general 1-8 23-30 <-> 22-29; cursor 33 <-> 30;
  general bags `4010 + 200*bag + idx` <-> `251 + 10*bag + idx`; bank, shared
  bank, trade and world identity. So `/equip 21 13` reached model slot 21 =
  the AMMO slot = DB 22, which is why Testcaster's **Shuriken** (her DB slot
  22) answered a command about slot 21. Fixed by `serverSlotToClient()` /
  `clientSlotToServer()` in `include/client/inventory/inventory_constants.h`
  (ported field for field, with a round-trip guard on the ranges the server's
  own table is not injective over) applied at exactly two seams:
  `parseUserSlot()` IN and `describeSlot()` OUT.

  **The experiment's seed was itself wrong, and its success half was an
  artefact.** `scripts/llmbench/record.sh --seed-weapon` inserted item 9997 at
  `slot_id=21` -- the power source -- so the dagger was never serialised to a
  Titanium client for EITHER character and both `/equip 21 13` commands were
  about the ammo slot. Testbuddy's reported SUCCESS was **not** an equip:
  `scripts/llmbench/timeline_legH.tsv:27` records, from the inventory table at
  compile time, that Testbuddy was ALREADY holding `13=9997 Dagger* (IT5)`
  before the command ran, and the local DB still shows `68 | 13 | 9997` today.
  The E_heldA/E_heldB pair cannot tell "already holding it" from "just equipped
  it" -- boarded as B811. The seed is now DB slot 23 and the timelines type
  `/equip 23 13`.

  **Proved live, observed.** `scripts/scenario.sh equip_observed` on the local
  lane, 2026-09-17: the actor logs in empty-handed, runs `/equip 23 13` and
  exits; an OBSERVING second client that sends nothing reports *"the peer's
  material slot 7 went 0 -> 5"* -- 5 being Dagger*'s IT5 -- and exits 0.
  Struck by T494.

## Added 2026-09-16 by T493 (scenario.sh restore keeps the world down)

- ~~**B797**~~ (A) STRUCK 2026-09-17 by T497 (fable ec1e9d77): readiness is `eqemu_stack_ready` = `server_process_running` AND an in-container `/dev/tcp` probe; `eqemu_port_open` renamed `eqemu_host_port_reachable` (reachability only); `ready_selftest.sh --legacy` exit 70 / fixed exit 0 [measured]. `scripts/eqemu/env.sh` `eqemu_port_open()`, and its callers
  `scripts/eqemu/up.sh` (`wait_for "<x> port ..."`) and
  `scripts/llmbench/record.sh` ("eqemu already up (5998 and 9000 open)") --
  **a TCP port probe cannot tell a live eqemu stack from a dead one on the
  local lane.** 5998/9000/7000 are PUBLISHED container ports, so
  `docker-proxy` completes the `connect()` on the host whether or not anything
  inside the container is listening. MEASURED T493: with the eqemu-server
  container running but `world`, `zone` and `loginserver` all at **zero**
  processes, both `eqemu_port_open 5998` and `eqemu_port_open 9000` returned
  success. The consequences are live: `up.sh` prints `loginserver port 5998:
  ready (0s)` and `world port 9000: ready (0s)` and exits 0 against a stack
  that is not running, and `record.sh` skips `up.sh` entirely on the same
  evidence -- T493 lost a 3.5-minute run to exactly that (the client never
  reached the world and the selftest reported 67). `server_process_running`
  (T457) already exists and is the honest check;
  `scripts/eqemu/restore_selftest.sh` uses it and runs `up.sh`
  unconditionally, but `up.sh` and `record.sh` are untouched. Found by T493.
  **STRUCK by T497.** There is now ONE readiness definition, `eqemu_stack_ready`
  (`server_process_running` + `eqemu_container_port_open`, a `/dev/tcp` connect
  inside the container's own netns, where docker-proxy cannot answer);
  `eqemu_port_open` was RENAMED to `eqemu_host_port_reachable` so every call
  site had to be revisited, and a grep now shows zero inline `/dev/tcp` probes
  outside `env.sh`. `up.sh`'s four inline probes, `record.sh:200`,
  `f9_cycle.sh:48` (B821), `restore_selftest.sh`'s post-up pair and `status.sh`
  all go through it. `scripts/eqemu/ready_selftest.sh` is the net and the
  proof: MEASURED 2026-09-17 on the local lane, `--legacy` (the old port-only
  definition) exits **70** -- it called the stack READY with world=0
  loginserver=0 zone=0 while `compose ps` still showed the container Up and
  both host ports still answered -- and the same run against
  `eqemu_stack_ready` exits **0**. `up.sh` now waits a REAL 2 s for world's
  in-container port after starting it, where it used to print `ready (0s)`.
- ~~**B798**~~ (B) STRUCK 2026-09-17 by T497: `eq_container_pkill` refuses a self-matching pattern (rc 2); `scripts/eqemu/down.sh`, `scripts/eqemu/restore_selftest.sh` and the camera_match scripts re-pointed at it [measured]. `scripts/eqemu/down.sh` -- **its `pkill` pattern is the right
  one and `scenario.sh`'s was not, and nothing said so.** `down.sh` uses
  `pkill -TERM -f '/bin/(loginserver|world|zone)( |$)'`, which does not match
  its own `bash -c` wrapper; the `restore_snapshot()` chain T493 deleted used
  `pkill -f /tmp/spire-launcher` first, which does. Any future container-side
  `pkill` has the same trap waiting for it, and there is no shared helper: both
  call sites spell the pattern out. Small, but it is the exact shape of the
  defect B784 turned out to be. Found by T493.
  **STRUCK by T497.** `eq_container_pkill <signal> <ere>` (`env.sh`) is the one
  helper: it builds the command and refuses a pattern that matches the command
  string it is about to run (`REFUSING self-matching pkill pattern: ...`,
  return 2). MEASURED: `/tmp/spire-launcher` is refused with rc 2, while
  `'/bin/(loginserver|world|zone)( |$)'`, `'[s]pire-launcher'` and
  `'/bin/zone( |$)'` all pass the guard. Every container-side `pkill` in the
  tree now goes through it -- `down.sh`'s two, `restore_selftest.sh`'s step-7
  SIGTERM, `camera_match_pose.sh`'s and `camera_match_sweep.sh`'s zone bounces
  (neither was named in B798) and the new `ready_selftest.sh` -- with ONE
  deliberate exception: `restore_selftest.sh`'s `--legacy` branch, which
  reproduces the defect on purpose and carries a comment saying so.
## Added 2026-09-14 by T485 (the held-item correction chosen by search against the oracle)

**SUPERSEDED 2026-09-17 by T485 Leg B (see that section below): the correction
this paragraph describes, c20, was WRONG -- Will ruled it 90 degrees out. The
shipped correction is the IDENTITY, the test names below no longer exist, and the
"only the four X->-Z candidates put a blade forward" scoring was against a target
that is now known to be a misreading. Kept as the record of how it was reached.**

T485 (2026-09-14) CLOSED B588 (blade half; T472 had closed the lantern half),
B661, B662, B724 and B725, and marked B723 contaminated. `bone_attachment.h`'s
`kHeldItemCorrection` is now candidate c20 -- item X -> bone -Z, Y -> Y, Z -> X,
a quarter turn about the item's Y -- chosen by rendering all 24 proper signed
permutations through the one production placement (`placeHeldItem()`, which
`entity_equipment.cpp`, the paperdoll and the model viewer all call now) on a
Human male at o01 with IT10649, IT10004 and IT10, and measuring each item's long
axis in the body frame (`review/T485/data/candidates.tsv`, one sheet per
candidate under `review/T485/candidates/`). Only the four X->-Z candidates put a
blade forward (angle 23-24 deg to forward; every other candidate 67-157) and of
those only Z->+X hangs the arrow down (23 deg to down); with X->-Z and Z->+X
fixed, det +1 forces Y->+Y, so c20 is the unique proper candidate and the
shields' painted face (item +Y, T472) stays outward as a consequence. The measurement that overturned T472: at o01 frame 0 the forearm
runs (0.96, 0.07, -0.27) from elbow to fingers (the elbow is bent, the hand at
the belt -- which is also why the official client's grip sits at belt height in
`review/T478/oracle/*User2*`), R_POINT's -Z is that same line (0.958, 0.06,
-0.284) and its X (0.28, 0.03, 0.96) points UP, perpendicular to the forearm;
T472 read X as "up the forearm" and negated it, which is what hung the blade
straight down (`review/T485/data/rpoint_frames.txt`). Tests
`HeldItemTest.OneHandedBladePointsForwardAndLevelAtIdle`,
`TwoHandedBladeIsLevelInFrontOfTheWaistAtIdle`, `ArrowHangsDownTheLegAtIdle`,
`FirstPersonWeaponAttachmentTest.ThirdPersonIdleBladePointsForward` and
`BoneAttachmentTest.ShippedCorrectionIsAQuarterTurnAboutTheItemsY` pin it (all
red under T472's C, green under c20).

- ~~**B755**~~ **STRUCK by T498** (`task/T498-item-models-vs-server`, local lane).
  The drift is MEASURED and it is not one bad row: `data/item_idfile.tsv` (a
  committed export of peq's `SELECT id, name, idfile FROM items`, 117,944 rows,
  `scripts/eqemu/export_item_idfile.sh`) against the JSON's 3,661 rows gives
  **1,226 mismatch, 902 not-on-server, 115,185 server-only, 0
  non-numeric-idfile**, plus a **collision census of 8 materials over 9 server
  items**. 2894 is confirmed (`2894 | Spiny Two Handed Sword | IT10004` against
  the file's 10009) and is one of 1,226. The ROOT CAUSE turned out not to be the
  file: `appearance.equipment[7|8]` had two feeders in two different units, and
  the file only ever decided the LOCAL player's own weapon -- see the struck
  B823 below for the fix, and **B827** for what is left, which is Will's call.
  Original text: (A, *unconfirmed*) **`data/item_models.json` disagrees with the server's
  `items.idfile`, so fable can draw a DIFFERENT MODEL from the official client for
  the same item.** MEASURED T485 (offline): `data/item_models.json` and
  `data/willeq.db`'s `item_models` both map item 2894 (Spiny Two Handed Sword) to
  model **10009**, while T478 read **IT10004** off the eqemu `items` table for the
  same item -- and fable's live set C frame
  (`review/T478/sheets/T478_held_C_idle_oracle_top_fable_bottom.png`) drew a
  different blade from the official client's. The held-item correction is not
  affected (both are X+ blades in the T476 census, and T485 searched with IT10004),
  so this is not a T485 regression; the defect is the data file. `item_models.json`
  is hand-carried, 3,661 rows, and NOTHING checks it against the server -- 2894 is
  the first row anyone has compared, so the drift's size is unknown. **Next
  measurement (needs the lock, ~5 min):** dump eqemu's `SELECT id, idfile FROM
  items` and diff all 3,661 rows against `item_models.json`; a row-count of
  mismatches decides whether this is one bad row or a stale table. Found by T485.
- **B756** (A, *unconfirmed*) **The blade's pitch does not match the oracle's, and → issue #77
  the o01 loop's own swing is larger than the mismatch.** With c20 the blade
  continues R_POINT's -Z. MEASURED over the whole loop
  (`review/T485/data/rpoint_frames.txt`, 41 distinct frames, re-scored by
  `review/T485/data/score_over_loop.py`): that direction pitches from **16.5 deg
  below level at frame 0 to 66.6 deg below at frame 24** and back, and its forward
  component drops under 0.7 on 16 of the 41 frames. The contact sheets and the
  ctest pins are at one frame near the top of that swing (-23.7 deg). The official
  User 2 / Tether crops (`review/T485/oracle_crops/A_*_zoom.png`,
  `C_user2_zoom.png`) read the short sword level to slightly UP -- which also
  contradicts T478 README section 4's own prose for set A ("diagonally down and
  forward"), so the two readings of the same frames disagree and neither is a
  measurement. **What is NOT established:** that a signed permutation cannot close
  it -- the oracle frames' phase in the idle loop is unknown, and 16 deg to 67 deg
  is a wide enough band to contain "level to slightly up" at some phase but not at
  frame 0. What would settle it: an official-client video of the idle loop (not
  stills -- `dont-sample-transients-with-stills`) with fable's loop stepped beside
  it, or a bone-frame dump from the official client, which is not available.
  Needs the lock. Found by T485.

B757–B759 unused.

## Added 2026-09-17 by T490 (the Iceclad OOM: the PVS was stored regions²/8)

Census of all 118 original/Kunark/Velious zone archives (regions, PVS storage,
WLD tally, parse peak/retained heap): `review/T490/x86/pvs_census.tsv`, taken by
`review/T490/scripts/pvs_census.cpp`. Pi runs: `review/T490/pi/`.

- ~~**B799**~~ **CLOSED by T490.** `BspRegion::visibleRegions` was a dense
  `std::vector<bool>` sized to the zone's region count, decoded for EVERY region
  at WLD parse time -- **regions²/8 bytes per tree**, 176,804,610 B for iceclad's
  37,609 regions. On the Orange Pi that OOM-killed the client at
  `loadZoneSequential: [55%] Computing spatial data` (`loadZoneStep2_BSP`),
  AUDIT-0914 anon-rss 318,500 kB. A second term of the same shape sat beside it:
  `SimBridgeComponent::buildSimulationZoneData()` pre-built
  `pvsVisibleBoundsIndices`, one candidate list per camera region --
  `sum(visible-per-region)` `size_t`s, **257,796,088 B for iceclad**,
  224,283,968 B for warslikswood, 293,052,304 B for cabeast, and 22.9 MiB for
  the GATE zone gfaydark. The PVS is now kept as the WLD's own fragment-0x22 RLE
  string in one arena on the tree (iceclad 4,832,309 B, a 36.6x reduction) and
  decoded on demand through a bounded 16-slot cache; the candidate list is built
  for one camera region at a time. Pinned by `ZoneBspPvs*` (15 cases, four real
  zones incl. iceclad and timorous) and `SimPvsCandidates` (4 cases). RED proof
  `review/T490/x86/redproof/`: restoring the dense form takes iceclad's retained
  parse heap from 28,869,232 B to 205,197,400 B and fails five cases. Found by
  AUDIT-0914, fixed by T490.

- **B800** (A, *measured*) **Iceclad and Timorous still OOM on the Orange Pi → issue #78
  after B799 -- the eager per-region mesh build is the next term.** With B799
  fixed the client passes `loadZoneStep2_BSP` (which now costs +10 MB anon,
  184,444 -> 194,732 kB) and is killed in the mesh build instead. Same-rig A/B,
  2026-09-17: the fable-tip ARM binary dies **inside** step 2 (last audit step
  `01_S3D`, peak RssAnon 326,724 kB); the T490 binary logs `02_BSP` and dies
  after it (peak RssAnon 318,880 kB). timorous the same (`02_BSP`, 236,044 kB).
  `loadZoneStep4_Regions()` builds one `IMeshSceneNode` + mesh + hardware buffer
  per region, eagerly, for all 37,609 -- heaptrack puts `uploadMeshHWBuffers` at
  31.04 MiB for iceclad against 11.09 MiB for firiona (9,686 regions) and
  2.10 MiB for wakening (`review/T490/x86/heaptrack_peak_sites.md`). **That the
  step is step 4 is measured** (the run logs `02_BSP` and never `04_Regions`,
  and there is no step 3); **that `uploadMeshHWBuffers` is the term on the Pi is
  ASSUMED** -- the heaptrack numbers are x86/llvmpipe, and the Pi's buffer
  allocation happens inside the Mali driver where the client cannot see it.
  Confirming it needs a per-step audit line inside step 4. This is
  **B752's lever question** (per-object/per-region build distance, instancing,
  or the removed `ConstrainedMeshCache`) applied to region meshes rather than
  placed objects, so it is Will's call in the same way. Runs:
  `review/T490/pi/raw/{iceclad_T490,iceclad_BEFORE_fable,timorous_T490c}`.
  Found by T490.

- **B801** (A, *measured*) **The client is ~190 MB of anonymous memory BEFORE it → issue #79
  loads a zone, and a zone that loads costs approximately nothing on top.** nro
  on the Pi, the full `[LOAD-MEM-AUDIT]` ladder at the T490 tip
  (`review/T490/pi/raw/nro_T490c`): `00_start` RssAnon **192,284 kB** ->
  `13_Cleanup` **190,548 kB**, a net *decrease* across all fifteen steps; peak
  RssAnon for the whole run 199,396 kB with 1,828 kB of swap; qeynos2 the same
  shape (172,512 -> 157,516 kB, peak 184,316 kB). Against Will's
  2026-09-17 target (32-64 MB RSS) the gap is ~126 MB and **all of it is the
  pre-zone baseline** -- no amount of zone-load work reaches 64 MB. Whatever
  holds that 190 MB at `00_start` is unattributed: the audit line's own
  `accounted_kb` is 43 and `untracked_kb` 228,621. T491 owns the ledger; this
  is the number it has to explain first. Found by T490.

- **B802** (P) **A Pi/live sweep that sets `character_data` between runs must
  wait out the previous client's LINKDEAD flush, then read the row back.** The
  server re-saves the character tens of seconds AFTER the client process dies,
  silently clobbering a zone written in between, and the next run logs into the
  PREVIOUS zone with no error anywhere. Four T490 runs were wasted this way
  (a timorous run entered iceclad; an eastwastes run, a cabeast run and a
  warslikswood retry all entered stonebrunt) and the memory figures looked
  perfectly plausible. Same family as B784. The recipe that works is in
  `review/T490/scripts/pi_sweep.sh`: settle 75 s, `update`, `select` it back and
  refuse to launch on a mismatch, then re-check the client's own
  `=== Entering <zone> ===` line afterwards and mark the run invalid if it
  disagrees. **Every table must carry the zone the client actually entered, not
  the one requested.** Found by T490.

- **B803** (B, *measured*) **`orangepi.s3dZoneSourceMemoryBytes` (24 MiB) does → issue #196
  not describe the largest zone in the game.** T293 derived it from gfaydark
  (7.52 MiB x 2.34 x 1.25); iceclad's WLD tally at the T490 tip is 25,737,912 B
  = 24.55 MiB, 572,088 B over, and its retained parse heap is 27.53 MiB. Nothing
  is red because `scripts/ci/visual_gate.py` applies the preset to the three
  GOLDEN zones only (qeynos2 2.68 MiB, gfaydark 5.83 MiB, nro 1.46 MiB) -- i.e.
  the gate's zones are not the worst case and nothing says so. T490's
  `test_zone_bsp_pvs` bounds at 32 MiB instead, derived from the measured maxima
  over all 118 zones. Either the preset moves to 32 MiB or the ledger records
  why the gate zones suffice. T490 did not touch the presets or the gate (T491's
  territory). Found by T490.

## Added 2026-09-17 by T485 Leg B (Will's ruling: the correction is the IDENTITY)

**WILL'S RULING, 2026-09-17, verbatim** -- this is the measured fact the held-item
transform now rests on, and it replaces every reading of a still that came before it:

> "ALL held items are wrongly oriented in both. In the 'after' (c20) sheet the item's
> long axis FOLLOWS the forearm; it must be PERPENDICULAR to the forearm, a fist
> grip, so c20 is off by 90 degrees. In the 'before' (T472 diag(-1,1,-1)) sheet they
> are off by 180 degrees."

`kHeldItemCorrection` is now the **IDENTITY** (item axes straight to the bone's).
Two independent routes reach it and they agree:

1. **From the ruling alone, no picture.** A rotation about the ITEM's Y is a signed
   permutation only at 0/90/180/270 -- c00 identity, c20 (what T485 shipped), c02
   (T472), c22. "c20 is 90 out" gives {c00, c02}; "c02 is 180 out" gives {c00}.
2. **From the geometry.** MEASURED: R_POINT's -Z **is** the forearm
   (`humfo_r -> humfi_r`), dot 1.001 at all 41 frames of `o01`, so the angle between
   the forearm and a rigidly attached item is FRAME-INDEPENDENT. Averaged over the
   loop (`review/T485/data/score_perpendicular.py`): c00 blade **89.1** / arrow 1.1;
   c20 blade **1.1** (it lies along the forearm -- what Will saw) / arrow 90.9; c02
   blade 90.9 / arrow **178.9**. The sign then comes from the official idle, which
   hangs the arm at the side: the arrow runs from the hand at the hip DOWN past the
   knee (13 deg off vertical) so `Z -> +Z`, and the blades run FORWARD out of the
   fist, which at the hanging frame is bone +X, so `X -> +X`; det +1 forces `Y -> +Y`.

Sheets: `review/T485/T485_v2_after_c00_IDENTITY_frame25_sheet.png` and
`..._before_c20_shipped_frame25_sheet.png`, plus all 24 in
`review/T485/candidates_frame25/`, rendered at **o01 frame 25** -- fable's
arm-hanging frame, not frame 0 where the elbow is bent.

- ~~**B722**~~, ~~**B724**~~, ~~**B725**~~ **re-closed by T485 Leg B** against Will's
  ruling rather than against the T478 prose. The first closure (2026-09-14) was
  against a reading of the crops that Will has now judged wrong; the tests that
  pinned it went red and were rewritten on the frame-independent criterion.
- ~~**B588**~~ stays closed; the lantern half gains a finding, B804.
- **B804** (P) **A pin can be green on geometry that is upside-down.**
  `HeldItemTest.LanternHangsBelowTheLeftHand` asserted "the mean vertex is below the
  hand" and was GREEN under T472's correction, which MEASURED T485 points the
  lantern's own long axis straight UP (+0.964 at the arm-hanging frame), and green
  under T485's first pick, which points it backward (-0.93 forward). Under the
  correct identity it went **red by 0.011 units**. A mean-vertex proxy does not
  constrain orientation; the pin is now the angle to the left forearm (27.7 deg,
  frame-independent, and red at 150 / 118 for the two wrong corrections). Look for
  the same shape wherever a test asserts a centroid instead of an axis. Found by
  T485 Leg B.
- ~~**B805** (A) **`held_item_candidate_search` compared its 24 candidates at → issue #80
  DIFFERENT poses.** `ISceneManager::drawAll()` calls `OnAnimate()` with the DEVICE
  timer, so the skeleton moved between one candidate's capture and the next: every
  candidate after the first was measured and drawn at a frame other than the one
  intended, and with `--frame` it was snapped straight back off the requested frame
  (c20's blade read 40.3 deg to the forearm instead of 1.2). FIXED by pausing the
  animator for the sweep (`pauseAnimation()`, after the per-frame dump, which needs
  it running). The 2026-09-14 `candidates.tsv` was taken with the pose drifting;
  it was regenerated. Any offscreen tool that measures a skeleton across several
  `drawAll()` calls has this trap. Found by T485 Leg B.~~ **STRUCK B849 (2026-09-18,
  issue #80 closed by the B849 unit)**: the pause is on fable at
  `tools/hwcaps/held_item_candidate_search.cpp:420`
  (`node->getAnimator().pauseAnimation()`, T485 Leg B, `a232e4d8`).
- **B756 RE-SCOPED by T485 Leg B** -- B756 said the blade's PITCH does not match the
  oracle and blamed a residual a signed permutation cannot close. With the identity
  the pitch question changes shape: what does not match is the POSE (fable's o01
  arm-hanging frame is 24.5 deg off straight down, the official idle reads nearer
  vertical), not the correction. B756 stays open, re-scoped, and still needs the
  lock and a stepped video.

## Added 2026-09-17 by T495 (the sky/fog colour lerp underflow, B789/B790)

- ~~**B806** (B) `src/client/graphics/detail/detail_texture_atlas_flora.cpp`
  `drawShell()` -- the same u32 underflow as B789, in the detail atlas's shell
  spiral: `baseColor.getRed() - 30` on accessors that return `irr::u32`.~~
  **STRUCK ON ARRIVAL by T495** -- it is two lines, so it was fixed rather than
  boarded, and it is recorded here because a swept pattern that leaves no trace
  gets re-found. It was producing CORRECT pixels: all three shell colours
  (255,250,240 / 255,220,210 / 230,210,180) are >= 180 in every channel, so
  nothing underflowed, `data/textures/detail_atlas.bmp` does not move and
  `TexturesReproducible` stays green. The clamp is a guard against a future
  darker shell colour, not a pixel fix. That census is the whole tree: a
  comment-filtered grep for `get{Red,Green,Blue,Alpha}() -` over `src/`,
  `include/` and `tools/` returns exactly ten hits -- the nine lerp lines T495
  replaced and this one.
- **B807** (B) **the T492 corpus pairs fable sky frames with official frames → issue #197
  that contain no sky**, so the corpus cannot answer "what colour should the sky
  be". MEASURED T495: the official frames B789 and B790 name as their pairs
  (`players/p1/frames/15:38:09.793_F_sky_p2_overhead_window.png` and
  `15:46:49.474_H_cam_p2_qeytoqrg_chase_window.png`) show a wall/ground with the
  Tip-of-the-Day dialog over them (B795) -- no sky band at all. T495 measured
  B789's official crop at **(19.31,20.55,19.12)**, a near-black grey whose
  commonest pixel is (14,15,14) at 3,709 of 36,000: that is not a sky. B790's
  "official (16,30,23), which is green dominant" is therefore a reading of
  GROUND, not of sky, and the two numbers in that entry were never comparable.
  A sky A/B needs the oracle pointed UP with the dialog dismissed; T466's
  camera-step recipe plus B795's dismissal is what would produce it. This is
  what B781 has been waiting for since T484. Found by T495.
- **B808** (C) **`SkyRenderer` never logs the colour it chose, or the hour it → issue #81
  chose it for**, so a wrong sky can only be diagnosed by re-deriving the
  function offline. T495 had to write an emulator of `calculateSkyColors()`
  (`review/T495/scripts/sky_color_emulator.cpp`) and sweep it against the corpus
  band means to discover that the frames were taken at 17:25 -- the run log
  records the game time only at zone-in, and `updateTimeOfDay()` logs nothing at
  all on the sky path (the one `updateTimeOfDay:` line in
  `src/client/graphics/renderer/irrlicht_renderer.cpp` is the weather
  modifier, and it fires only when that modifier MOVES). One `MOD_GRAPHICS` DEBUG line per tint change --
  hour, zenith, horizon, fog, tint, clear -- would have made this a log read.
  Will's call because it is a logging-policy question (which module, which
  level, and whether a per-minute line is acceptable in a corpus run, given
  B791). Found by T495.

## Added 2026-09-17 by T494 (the /equip slot convention, B796)

- **B809** (B) **eqemu never broadcasts a WEAPON `OP_WearChange` when a weapon → issue #198
  is equipped by `OP_MoveItem`, so another player's weapon does not appear in
  fable until they respawn.** `/mnt/projects/akk-stack/code/zone/inventory.cpp:2205` computes the material
  slot with `Client::SlotConvert2()`, and that switch
  (`/mnt/projects/akk-stack/code/zone/client.cpp:4128-4154`) has cases for the seven ARMOR slots ONLY:
  `slotPrimary`(13) and `slotSecondary`(14) fall through to its
  `uint8 slot2 = 0` default, so the packet sent describes the HEAD -- whose
  material did not change -- and the dedupe at
  `/mnt/projects/akk-stack/code/zone/mob_appearance.cpp:420-428` then drops it entirely. MEASURED T494 on
  the local lane: the row moved `23 -> 13` in `inventory` (polled in the
  database while the run was live) and the observing client logged **not one**
  `OP_WearChange` in the 150 s that followed, while the same client had logged
  a burst of nine for that character at its spawn. The weapon materials ARE
  correct at spawn (`Mob::SendWearChangeAndLighting` runs over
  `textureBegin..LastTexture`, which includes `weaponPrimary`=7), which is why
  T494's live proof makes the actor log out and back in. This is a SERVER
  defect, not fable's, but the user-visible consequence is fable's: a party
  member who draws a weapon mid-session stays empty-handed on your screen.
  Decide whether to patch akk-stack (`SlotConvert2` wants
  `slotPrimary -> weaponPrimary`, `slotSecondary -> weaponSecondary`) or to
  accept it as era behaviour -- the official client has the same server. Found
  by T494.
- **B810** (B) **`LootItemIntent`'s corpse slot numbers have the SAME → issue #199
  Titanium/server split B796 was, and nothing converts them either.** The
  server's pair is `titanium.cpp:3641` `ServerToTitaniumCorpseSlot()` /
  `:3727` `TitaniumToServerCorpseSlot()`, and it is NOT the same table as the
  possessions one: general1-8 shift by 1, the cursor range by 3, and
  `POSSESSIONS_COUNT + slotAmmo` by 4, over a domain that starts at
  `invslot::slotGeneral1`. T494 fixed the possessions/bank/trade/world/bag
  table and deliberately did not touch this one (out of scope). Anything that
  prints or takes a corpse slot number is suspect; start at whatever pushes
  `LootItemIntent`. Found by T494.
- ~~**B811** (A) **The llmbench held-item check cannot tell "already holding it" → issue #82
  from "just equipped it", and that is how B796's own experiment produced a
  false positive.** `E_heldA_*` records what the inventory table says at
  COMPILE time and `E_heldB_*` records what it should say after the equip, but
  nothing asserts the two DIFFER, and nothing asserts the character started
  empty-handed. MEASURED T494: `timeline_legH.tsv:27` shows Testbuddy already
  at `13=9997 Dagger* (IT5)` before its `/equip` step ran, so the step that
  moved nothing was read as a success and B796 recorded a contradiction it did
  not have. The fix shape is the one `equip_observed` uses: refuse to score a
  held-item step whose BEFORE state already matches its AFTER state. Found by
  T494.~~ **STRUCK 2026-09-19 (issue #82, `task/kb-b811-llmbench-held-before-after`)**:
  the gate lives in `scripts/llmbench/timeline_held_check.py` (shared by the
  compile gate and the ctest net `LlmBenchHeldBeforeAfterHygiene`,
  `tests/cmake/hygiene.cmake`). `compile_timeline.py` now refuses to compile a
  timeline whose `/equip` step targets a slot the character's own last
  recorded `held=` already fills with the same item -- non-zero exit, before
  any frame is recorded -- and `verify_labels.py` marks an already-recorded
  held-B row whose BEFORE state matches its AFTER state as
  `db_inventory=...no-op... [FAIL]`, so it can never carry a grading label.
  Measured on the committed timelines: it rejects `timeline_legH.tsv:31`
  (H0_equip_p3) and the same-shape `timeline_legI.tsv:31`; p2's `/equip 23 13`
  in the same files is a genuine move (its slot 13 held 14337, not 9997) and
  still compiles, as do `timeline.tsv`, `timeline_dry.tsv` and `legE`/`legF`/
  `legG` (their equip rows name no destination item, so the legacy
  `db_inventory` check covers them unchanged).
- ~~**B815** (C) **A task worktree has no `data/eq_client`, and three ctest cases → issue #83
  fail as if they were real reds.** `tests/CMakeLists.txt:114` defaults
  `EQ_CLIENT_DATA_PATH` to `${CMAKE_SOURCE_DIR}/data/eq_client`, which is a
  gitignored symlink present in `main` and absent in every fresh worktree, so
  `ComprehensiveZoneTest.ClassicZones`, `ComprehensiveZoneTest.OverallSummary`
  and `DoorLoadingUnitTest.MultipleZonesHaveObjectGeometries` fail with
  `SKIP (not found)` for every zone -- MEASURED T494, and green the moment the
  symlink is made (or `EQ_CLIENT_PATH` is exported). Everything else in the
  suite either skips cleanly or does not need the assets, so these three read
  as a regression to anyone who has not hit it before. Either make the tests
  SKIP like their siblings when the path is absent, or create the symlink in
  the worktree setup. Found by T494.~~ **STRUCK 2026-09-19 (issue #83 closed
  by the B815 card, `task/kb-b815-asset-tests-skip`)**: `tests/asset_requirement.h`
  (`requireEqClientAssets`, one shared helper) makes an absent asset root a
  GTEST_SKIP locally and a hard FAIL under `WILLEQ_REQUIRE_ASSETS=1`; the three
  aggregate cases (plus the whole `ComprehensiveZoneTest` /
  `DoorLoadingUnitTest` suites) now gate on it, `.gitlab-ci.yml`'s `test` job
  exports the variable so CI can never skip them silently, and
  `test_asset_requirement` pins both outcomes.

## Added 2026-09-17 by T497 (readiness that docker-proxy cannot fake)

- **B820** (B) **Every readiness probe in the lane tests TCP, and the CLIENT → issue #200
  speaks UDP.** Even the fixed `eqemu_stack_ready` proves "the server links are
  up", not "a client can log in": TCP 5998 is the loginserver's **servertalk**
  listener for WORLD SERVERS
  (`/mnt/projects/akk-stack/code/loginserver/world_server_manager.cpp:30-36`,
  `EQ::Net::ServertalkServer::Listen`) and TCP 9000 is world's zone link, while
  a Titanium client logs in over **UDP** 5998 and plays over UDP
  (`client_manager.cpp:105-107` builds an `EQStreamManager` on `titanium_port`).
  [measured, from the server source.] A UDP port does not refuse a `connect()`,
  so the honest client-facing check is not a probe at all -- it is a login, i.e.
  `scripts/scenario.sh zone_in`. Nothing in the tree says this, and the next
  person to "improve" readiness will reach for a UDP probe that always
  succeeds. Either document the limit where the definition lives (T497 did, in
  `env.sh` and CLAUDE.md) or give the lane a cheap real-login smoke gate.
  Found by T497.
- **B821** (C) **`scripts/llmbench/f9_cycle.sh:48` carried the same host-port → issue #84
  gate B797 named, and B797 missed it.** B797 listed `up.sh`, `record.sh` and
  `status.sh`; the fourth call site was `f9_cycle.sh`, which skipped `up.sh` on
  `eqemu_port_open 5998 && 9000` exactly like `record.sh`. T497 found it only
  because the rename forced a full re-grep, and fixed it. The open question is
  the process one: a board entry that enumerates call sites is a snapshot, and
  nothing checks the enumeration. Worth a convention (name the grep, not the
  list) before the next one. Found by T497.
- **B822** (C) **There is no shell test lane.** `restore_selftest.sh` (T493) → issue #85
  and `ready_selftest.sh` (T497) are real regression nets for real defects --
  both with a `--legacy` mode that proves they fail on the defect -- but
  nothing runs them: they are not in ctest, not in CI, and reachable only by a
  human typing the path (or `scripts/scenario.sh --selftest` for the first).
  They also need the eqemu lock and ~2 minutes each, so they cannot simply join
  the default ctest suite. Proposal: `scripts/eqemu/*_selftest.sh` is the
  convention, each takes the lock itself, each exits 0/64/6x-7x, and a single
  driver beside them (`selftests.sh`, which does not exist yet -- T498 had to
  de-cite this line because `DocCitations` reads a backticked path as a
  citation and was RED on the tip because of it) runs all of them for a live
  unit's pre-flight.
  Needs Will's call on whether that lane is worth owning. Found by T497.

B816-B819 used by T496 (Leg A-D). B820-B822 are T497's.

## Added 2026-09-17 by T498 (item_models.json vs the server's items.idfile)

- ~~**B823**~~ **CLOSED by T498, and recorded because the SHAPE will recur.**
  **`appearance.equipment[7|8]` had two feeders carrying two different units,
  and nothing named the type.** For the client's whole history the LOCAL
  player's weapon slots took `item->itemId`, a DATABASE item id
  (`entities_component_appearance.cpp`, `UpdatePlayerAppearanceFromInventory()`),
  while every peer and NPC took the server's **material** -- spawn parse,
  `entities_component_updates.cpp` and `ZoneProcessWearChange()` -- which for a
  weapon is `atoi(&item->IDFile[2])` (`Mob::GetEquipmentMaterial()`,
  `/mnt/projects/akk-stack/code/zone/mob_appearance.cpp`). Nine render sites
  papered over it with `getModelIdForItem()` + "if that missed, use the number
  as a model id", so both units "worked" and the disagreement was invisible.
  [measured] Weapon idfiles are >= 10000 and the JSON's keys stop at 8456, so
  the peer lookup ALWAYS missed and only the local player was ever drawn from
  the file. T498's fix is the wire: Titanium serialises `IDFile` as static field
  3 (`titanium.cpp:3359`) and fable's parser skipped it
  (`inventory_item_parser.cpp` carried the comment `// fields[3] = IDFile` and
  nothing else). `ItemInstance::idfileModel` now parses it, the feeder uses it,
  and the item-id lookup is gone from all nine render sites -- `equipment[]` is
  uniformly a MATERIAL / IT number.
~~- **B824** (C) **`EquipmentModelLoader::loadItemModelMapping()` builds a map → issue #86
  nothing reads.** [measured] It parses all 3,661 rows of
  `data/item_models.json` into `itemToModelMap_` at load, and
  `getModelIdForItem()` -- the only thing that would want it -- queries
  `WilleqDatabase::getItemModel()` (the `item_models` table) instead. The JSON
  at runtime is a startup `checkFile` (`application.cpp:242`) plus an input to
  `willeq.db`; the db table is the live lookup. T498 left the loader alone and
  only stopped it THROWING on a non-digit key (`std::stoul`), which is what a
  `"_provenance"` header would have done to it. Delete the map, or delete the
  query and read the map -- one of the two, and it is a decision about which
  source of truth the tools get. Found by T498.~~
  **STRUCK by the B824 unit (`task/kb-b824-item-model-map-dead`, kanban
  t_20a88e62, 2026-09-19):** the map is deleted and the db table confirmed the
  live lookup. `getModelIdForItem()` is now the only item->model route,
  answering from `WilleqDatabase::getItemModel()` with no JSON parsed at
  runtime; `data/item_models.json` stays (startup `checkFile`, input to
  willeq.db). The deletion is pinned twice: `test_item_model_mapping` (db
  resolves an item with no JSON map loaded) and the `ItemModelMapDeletion`
  hygiene test (`scripts/refactor/check_item_model_map.py`) ratchets the map's
  identifiers out of the tree and the db query in. The source-of-truth call
  (db over JSON map) was Will's (board B824, 2026-09-18).
- **B825** (C) **The JSON covers 3,661 of peq's 117,944 items, and an → issue #87
  uncovered one used to fall silently to `modelId = itemId`.** [measured]
  115,185 server items have no row. Every render site's fallback was `if
  (modelId < 0) modelId = equipmentId`, i.e. "draw the item id as a model
  number", with no log line -- which is how `renderer_zone_load_entities.cpp`
  came to carry a P10b comment about a giant rat whose missing model aborted an
  entire zone load. T498 removed the lookup from the render sites, so the
  runtime no longer depends on the coverage at all; what is left is the offline
  tools, which do hold item ids and do hit the gap. Decide whether the tools
  want the full table (117,722 rows with a numeric idfile) or should report a
  miss. Found by T498.
- **B826** (C) **No derived data file in the tree carries provenance.** → issue #88
  [measured] `data/item_models.json` is a bare `{"<itemId>": <modelId>}` map
  with no header at all -- no source, no host, no date, no rule -- which is why
  nobody could tell a hand-edit from an export, and why the drift B755 names
  went six months unnoticed. `data/willeq.db` has had `metadata.input_digest`
  since T450 and it is the only one. T498 proposes the convention for JSON:
  a `"_provenance"` object as the first key, holding generator, source, source
  host, source row count, UTC timestamp and the RULE applied -- the shape
  `scripts/check_item_models.py --rebuild` already writes and
  `data/item_idfile.tsv`'s `#` header already carries. Needs Will's call on
  whether to apply it to the other derived JSONs. Found by T498.
- **B827** (C) **Regenerating `data/item_models.json` from the server is a → issue #89
  VISUAL change and is NOT done.** [measured] 2,128 of its 3,661 rows disagree
  with the export, so rewriting the file changes what
  `tools/model_viewer` and `tools/hwcaps/ui_inventory_capture` draw for a third
  of the items they can be asked about -- T498's own STOP condition ("a
  wholesale swap is a visual change, not a data fix"), and 497 of the keys are
  below peq's first item id, so they were never item ids and nobody knows what
  they were. The runtime no longer reads any of it (B823), so this is now a
  tools-only question with no user-visible consequence, which is why T498
  stopped rather than swapped. What exists instead: the drift is enumerated in
  `data/item_models_drift_baseline.txt` and ratcheted in both directions by the
  `ItemModelsMatchServer` ctest test, so it cannot grow and a fixed row cannot
  stay listed. `python3 scripts/check_item_models.py --rebuild` does the swap
  in one command when Will wants it, and `data/willeq.db` + `.sha256` must be
  rebuilt in the SAME commit or `DbReproducible` goes red. Found by T498.

## Added 2026-09-17 by T499 (the B795 re-check of every archived oracle frame)

- **B833** (B) **6 of the 24 scored rows in T492's delivered corpus → issue #201
  (`review/T492/llmbench/verify_labels.tsv`) were scored on an official frame
  with the Tip of the Day dialog in it.** [measured T499] The camera check in
  `scripts/llmbench/verify_labels.py` scores the official p1 frame against
  T466's `_noui_` direct-mode stills over `CROP = "1000x480+12+40"` in ROOT
  coordinates -- x 12..1012, y 40..520. The dialog's root rect is
  x 663..1023, y 259..564, so it covers **349 x 261 px = 19.0% of the scoring
  crop**, and the reference stills have no dialog in them. The six rows, with
  their frames, are in `review/T499/t492_verify_labels_provenance.tsv`:
  `A_cam_p2_firstperson` (PASS, 0.545 best, next 0.903, ratio 1.66),
  `A_camget_p2` (PASS, ratio 1.62), `A_cam_p3_firstperson` (PASS),
  `A_camget_p3` (PASS) and `F_sky_p2_overhead` / `F_sky_p3_overhead`
  (INCONCLUSIVE). **The four PASSes carry grading labels into the corpus that
  went to the grader**, and their ratios (1.62, 1.66) are the two closest to
  the 1.5 margin in the whole table -- which is what a 19% common-mode region
  would do. [assumed] that the dialog is the whole of the difference; nobody
  re-scored anything here, and T499 was forbidden to. What would settle it:
  re-run `verify_labels.py` on those six labels with the dialog rect masked out
  of both crops, or re-record the four `A_*` steps behind Leg I's dismissal
  (Leg I's own 63 frames are **0/63 dialog**, so the rig can already do it).
  Not class A: the score decided a corpus LABEL, not a shipped constant, and
  `review/T492/README.md` says the corpus is not blessed until Will has looked
  at the sheets. Found by T499.
- **B834** (B) [UIGAP-small: fable now HAS the tip windows -- `tip_window.h`, `tests/test_tip_window.cpp`; what stays open is the oracle half below: whether `ShowTips=0` suppresses the context tips on the official client] **The tip window is not only "Tip of the Day" at login -- it has → issue #202
  context-sensitive variants that pop at the SAME rect after an in-world
  action, and nothing has ever verified that `[Tips] ShowTips=0` suppresses
  them.** [measured T499] The same 360x300 window at (664,234) carries a
  different CAPTION depending on what the player just did, across five units:
  `Tip of the Day` (login -- its body then names a category, "Etiquette Tip",
  "Safety Tip", "Gameplay Tip", "Convenience Tip", "Performance Tip", which is
  NOT the caption), `Consider Tip window` (T478, after attacking an NPC that
  cons red), `Death Tips` (T389 and T492, after dying), `Inventory Tips` (T401
  and T478, on opening the inventory), `Looting Tips` (T377, on opening a
  corpse's loot window), `Sitting Tips` (T492, on `/camp`) and
  `Keyboard Options Tips` (T377, T466, on the Options>Keys page -- these two are
  the sweep's only `unreadable` rows: OCR read the caption but the chrome match
  reached 0.907 against a 0.95 bar, because the Options window sits over part of
  the bar). The title bar is byte-identical apart from the centred caption and
  the two alpha-blended end caps -- which is why T499's detector matches the
  invariant columns only and lets OCR say which window it is. B795's fix as written ("dismiss it in
  `oracle_start.sh` after `enter-world.sh` ... verified by OCR that 'Another
  Tip' is gone") dismisses ONE window at ONE moment. [measured] T492 Leg I,
  which is the only run with `ShowTips=0`, has 0 dialog frames in 63 -- but its
  31 steps contain no sit, death, loot, consider or options action, so it is
  evidence for the login window and **for nothing else**. What would settle it:
  one offline-able probe -- `ShowTips=0`, then `/sit` and an options page, and
  OCR the rect. Until then any oracle grab taken right after a loot, a death, a
  con, a sit or an inventory open is suspect wherever the run's crop reaches
  past x=663. Found by T499.
- **B835** (P) **`review/T492/llmbench/` -- the delivered corpus -- has 103 of
  its 251 official frames with the dialog over them, and its README says
  nothing about it.** [measured T499] `llmbench-legE` is 139/153 and
  `llmbench-v2-legI-replaced` 34/36; only `llmbench-legI` is clean (0/63). The
  frames are the grader's evidence for every `match` and `discrepancy` step
  pair, and the eight `sheets/T492_match_*.png` contact sheets Will blessed are
  built from them, so a grader or a reader who crops the right-hand third of an
  official frame is reading UI chrome. This is a documentation defect, not a
  measurement one -- B833 covers the numbers. Fix: a line in
  `review/T492/llmbench/README.md` and in `review/T492/README.md` naming
  `review/T499/tip_dialog_audit.tsv` as the per-frame index. Found by T499.

## Added 2026-09-17 by T500 (the `entity_models` byte split)

- **B836** (P) **The `entity_models` byte split, and the order of the `ready`
  queue it sets.** [measured T500] `RaceModelLoader::getMemoryStats()` tallied
  ten terms and nothing ever checked they added to anything: on x86 with twelve
  global_chr races they covered 30,858,350 of the 59,275,152 bytes the ledger
  charged to `Category::EntityModels`. The split now closes to **-1.28 %**
  (pre-fix) / **-0.88 %** (post-fix) against live REQUESTED bytes, and the
  terms it needed are in `refactor/T500_entity_models.md`'s Review. The
  `ready`-point queue behind it is unchanged: 1. `entity_models` (this unit),
  2. `entity_instances` (share the index array), 3. `startup` (Pi attribution
  leg first), 4. `zone_mesh` (merge/selector ordering first). Closed by T500 on
  x86; **the Pi half is B843**.
- ~~**B837** (A) **CLOSED by T500.** `getMergedTextures()` called
  `loadNumberedGlobalModels()` for global2-7_chr.s3d's TEXTURES and the call
  retained every CharacterModel in all six archives -- parts, rawParts deep
  copies, SkeletonTrack and a full animated skeleton per race -- for the life
  of the process. 15,486,203 B [measured T500, x86, twelve races]. The
  characters are now loaded per archive on demand by
  `ensureNumberedGlobalCharacters()`, which
  `loadModelFromNumberedGlobal()` calls; nothing is dropped. Red:
  `EntityModelSharingTest.TheTexturePathAloneRetainsNoNumberedGlobalCharacter`.
  **It changes nothing on the orangepi preset** -- see B843.~~ **STRUCK B849
  (2026-09-18)**: landed by T500 Leg B (`68af49f6`), the per-archive loader is
  on fable at `src/client/graphics/eq/model_loading.cpp:98` and pinned by
  `tests/test_entity_model_sharing.cpp:237`; no issue was ever opened for it,
  so there is no record to close -- the Pi half remains open as B843.
- **B838** (B) **`zone_mesh` after the VBO upload: 21.80 MB live at `ready` on → issue #203
  the Pi that the draw does not read.** [measured T496/T500] After
  `uploadMeshHardwareBuffers()` the draw binds the VBO only, but the CPU arrays
  are still read by `ZoneMergeBuffers::build()` (first frame after loading;
  `mergeZoneDraws: true` on orangepi), `createTriangleSelector()` (load step 9)
  and the client-array fallback. Largest-looking, smallest real win, highest
  risk. Needs the merge/selector ordering settled first. Deferred by T500.
- **B839** (B) **`startup` is 28.12 MB live at every point on the Pi and has NO → issue #94
  Pi-side attribution.** [measured T496 Leg D, re-measured T500: 28,134,104 B
  at `ready`, qeynos2, and it does not move between `startup` and
  `steady_60s`.] The x86 heads (cursor 8.70 MB, loading screen 7.47 MB) were
  wrong in DIRECTION on Mali (board B812), so nothing is known about what this
  bucket holds on the target. Needs its own measurement leg before any fix.~~
  **STRUCK B839 (2026-09-19, issue #94 closed by the B839 unit, x86 lane)**:
  the bucket now names every one of its bytes permanently. A per-thread
  startup sub-owner label (`StartupScope`, the 16 high bits of the ledger
  header's category word) splits the `startup` category into rows
  (`startup.cli`, `startup.client`, `startup.willeq_db`, `startup.eq_strings`,
  `startup.spells`, `startup.graphics`, `startup.login`, `startup.mode`,
  coarse `startup.application` underneath, `misc` catch-all last) that are a
  partition of the category by construction; `[memledger-startup]` lines
  (MOD_MAIN, INFO) ride as a sibling of the `[memledger]` line at every
  lifecycle point (`recordPoint`), no debug build needed. x86 headless run
  [measured 2026-09-19]: TOTAL 356,832 B / 964 blocks = willeq_db
  161,856 + application 77,536 + login 74,848 + client 24,752 + cli 10,736
  + mode 7,104 + misc **0** — the 5 % bar in `tests/test_memory_ledger.cpp`
  `MemoryLedger.HeadlessClientNamesAtLeast95PercentOfItsStartupBytes`
  (which spawns a headless client and reads the table it prints) measures
  0 %. `eq_strings` / `spells` / `graphics` exist as rows but hold 0 B in a
  headless run (no graphics, no spell-string pass). The Pi's 28,134,104 B is
  expected to read the same way [assumed, not re-measured on the Pi by this
  unit]: `./willeq -c config.json --no-graphics --log-level=INFO` (the
  `[memledger-startup]` lines are MOD_MAIN INFO) — the numbers differ from
  x86 by design (B812), the NAMES are what this closed. Shrink candidates
  from the x86 table: `startup.application` 77,536 B (action layer / input
  bridge / console bridge, no narrower scope) and `startup.login` 74,848 B
  (the login connection manager's buffers, held until the next connection).
- **B841** (P) **A byte freed late is not a byte returned: the arena keeps it.**
  [measured T496 Leg D, re-measured T500] Between `ready` and `steady_60s` on
  qeynos2 the ledger frees 17.4 MB and RssAnon falls 0.09 MB, because
  `malloc_slack` goes 31.58 -> 47.67 MB. Any future memory unit must state
  whether its win is a byte NEVER ALLOCATED or a byte freed before the peak;
  only those two move RSS. `malloc_trim()` has never been tried here and is the
  obvious next question.
~~- **B843** (A) **The `[modelmem]` tally over-counts `entity_models` by 125 % on → issue #96
  the Pi and closes to -0.9 % on x86.** [measured T500 Leg C,
  `review/T500/pi/raw/qeynos2_T500after/modelmem.txt`] On the Pi at
  `zone_load_complete` the tally is 49,778,752 B against 22,146,989 B of live
  requested bytes in the category. The x86 offline test loads every model
  through `ensureModelDataLoaded()`, whose `MemoryScope` is `EntityModels`; the
  Pi runs with `graphicsArchiveIndex_` set and its `TextureInfo` pixels
  (`tex_global` 6.95 MB + `tex_zone` 2.35 MB + `tex_otherchr` 9.37 MB) are
  allocated under other scopes, so `RaceModelLoader` TALLIES bytes another
  category OWNS. Nothing is lost -- the ledger's own total is unaffected -- but
  100 % accounting means a shared structure has one owner, and today it has
  two. Fix: decide who owns a shared `TextureInfo` and make the tally agree.
  Do NOT "fix" it by changing the ledger's categories without Will.
  **RULED by Will 2026-09-17 (`docs/decisions.md`, memory rulings, item 3):** a
  shared block is reported ONCE with every claimant listed (`X: cat1, cat2`),
  never added into each claimant's total.~~ **STRUCK for x86 by B843 (this
  branch):** the tally now keys on `TextureInfo::ownerCategory` (stamped by the
  load path that parsed each archive under its own `MemoryScope`). The five
  `tex_*` terms count only pixels whose owner IS `entity_models`; every
  foreign-owned block -- its pixels PLUS the map node + key + `TextureInfo`
  object that hold it (the `shared_ptr` control block stays outside this
  tally, as the T500 (d) baseline never charged it) -- is pulled out of the
  `entity_models` sum and reported ONCE, with every claimant named, on a
  `[modelmem-shared]` line (`X (pixels P + storage S): owner, [claimants]`). `modelMemTotal()` excludes
  `sharedTextureBytes`, so a shared block is never added into a claimant's
  total. The x86 offline reproduction (`tests/test_entity_model_sharing.cpp`,
  `B843SharedPixelsAreNotClaimedByEntityModels`, archive-index path) was +2.76 %
  over the ledger's live requested bytes before this and closes to +1.88 %
  after (within the existing test's 2 % bound); the
  `TheSplitClosesAgainstTheLedger` case stays green at -0.88 %. **Pi
  confirmation is owed to the orchestrator** (Lane note: offline x86; the Pi
  confirming run is the orchestrator's, not this worker's).
- **B844** (P) **The orangepi preset never had B837's defect, so T500's Leg B
  win does not reach the Pi.** [measured T500 Leg C] `getMergedTextures()`
  guards its numbered-global load with `!graphicsArchiveIndex_`, and the Pi
  sets the archive index, so `src_char_numbered` and `src_skel_numbered` read
  0 there before and after. `entity_models` live at `ready` moved 23,985,576 ->
  23,985,552 on qeynos2 (**-24 B**) and 19,805,912 -> 19,805,904 on befallen
  (**-8 B**). The x86 win is real for every non-archive-index consumer (the
  tools, the tests, `--no-graphics` rigs) and worth having, but the Pi's
  `entity_models` is still 23.99 MB and `over_target` is still 91 MB. The
  Pi-side head is now `src_skel_other` (4.11 MB) + `src_char_other` (2.69 MB)
  -- `otherChrCaches_`, the ZONE `_chr.s3d` archives -- and `dup_geometry`
  (5.38 MB). That is the next unit's target, not this one's.

## Added 2026-09-17 by T501 (CLAUDE.md exceeded the hermes context-file cap)

~~- **B846** (P) **The root `CLAUDE.md` was 66,660 bytes, past the 62,914-byte
  context-file cap the hermes kanban workers read it under, so they silently
  lost the MIDDLE of it** -- rules, not decoration: the derived-asset recipes,
  the scenario-lane exit codes and the whole Key Patterns section all sat in the
  dropped span. [measured T501] `wc -c CLAUDE.md` = 66,660 at `5e6f9afe`; the
  cap is 6% of a 256K-token window at 4 bytes/token. There was no ceiling on the
  file and nothing would have reported one being crossed. Found and fixed by
  T501.~~ **STRUCK by T501 (this commit):** the file was audited claim by claim
  (`review/T501/claude_md_audit.tsv`, 147 rows -- 129 current, 12 stale, 3 wrong,
  3 unverifiable), every stale/wrong row was fixed, and the unit-by-unit
  narrative moved out behind links to `refactor/completed/` and
  `docs/decisions.md`: **66,660 bytes -> 55,319 bytes (-17%)**. A 60,000-byte
  ceiling for `CLAUDE.md` is now enforced by
  `scripts/refactor/check_file_sizes.sh --all`, i.e. by the `FileSizeCeiling`
  ctest test -- shown RED against the pre-T501 file and GREEN against the new
  one.

## Added 2026-09-17 by kanban pilot

~~- **B840** (B) **`RaceModelData::decodedTextures` has zero writers.**
  [measured T500, re-confirmed at this tip] `race_model_loader.h`'s
  `std::vector<DecodedTexture> decodedTextures` is a dead field: the
  background-decode/main-thread-upload scheme it was for is done by
  `ConstrainedTextureCache` instead. It costs 24 B per RaceModelData and no
  more, so it is hygiene, not a leak -- but `DecodedTexture` itself exists only
  for it. T500 did not delete it: it is outside the unit's measured claim and
  a deletion wants its own reachability check (B468's lesson).~~
  **STRUCK by the B840 unit (`task/kb-b840-decodedtextures-dead-field`,
  kanban t_a817842a, 2026-09-17):** the field is deleted and the reachability
  check is now mechanical. `WriteOnlyFields` (scripts/refactor/
  check_write_only_fields.py) ratcheted only the write-only half of its
  census, and its bare-identifier sweep counted the DIFFERENT member
  `EquipmentPrepData::decodedTextures` (written at
  entity_prep_worker.cpp:462, read at :470) as a use of this one -- the
  name collision that hid it. The unit added a receiver-typing sweep (a
  qualified use is attributed to the receiver's own type where the tree
  types the receiver; an untyped use still counts for every type, so the
  logic can only remove hiding) and an `EXEMPT_UNUSED` table ratcheted both
  ways, exactly like `EXEMPT`. With the field present and un-exempted the
  test is RED on exactly `RaceModelData::decodedTextures`; after the
  deletion it is GREEN, and the write-only half's 67 classified rows are
  unchanged. The new sweep surfaced five more fields the old name matching
  masked the same way (CombatDamage_Struct's force/hit_heading/hit_pitch,
  TextureInfo::animationDelayMs, WldFragment2CHeader::size9); all are in
  the table with a reason.

~~- **B842** (P) **`--log-module` takes ONE `MOD:LEVEL` per flag and silently
  eats a comma-separated list.** [measured T500] `logging.h:878` splits on the
  first `:` and hands the rest to `ParseLevelName()`, so
  `--log-module=MAIN:INFO,GRAPHICS:INFO` sets MAIN to whatever that returns for
  `"INFO,GRAPHICS:INFO"` -- the first T500 Pi run came back with
  `ledger_lines=0` and an otherwise perfect exit 0. Two flags work. Fix: reject
  or split on commas, and say so in `--help`; a rig that asks for a log level
  and gets silence is the same defect class as B784's self-killing `pkill`.~~
  **STRUCK by the B842 unit (`task/kb-b842-log-module-list`):**
  `InitLogging()` now splits `--log-module=` values on commas into
  `MOD:LEVEL` pairs, and a pair whose level name does not parse is rejected
  with a line on stderr naming the bad token (the module keeps "use global
  level" -- never silently mapped to LOG_NONE). Pinned by three
  `tests/test_logging.cpp` cases (comma list, `MAIN:BOGUS` rejection,
  bad-pair-does-not-poison-neighbours), shown RED on the pre-fix code.
  `--help` now says the list form.

~~- **B850** (A) **`--log-module` silently maps an unknown MODULE name to
  MAIN.** [measured 2026-09-18 on fable 6d5e43ed] B842 made
  `ApplyLogModulePair()` reject an unparseable LEVEL loudly, but the MODULE
  half still goes through `ParseModuleName()`, whose fallback is
  `return MOD_MAIN;` -- so `--log-module=NTE:DEBUG` (a typo for NET) sets
  MAIN to DEBUG and says nothing. The same fallback silently mapped typos on
  the `--log-only` / `--log-exclude` paths, the config `logging.modules`
  object and the `/logmodule` command, and the empty/too-long pair message
  printed a stray `[` and `]` around the token.~~
  **STRUCK by the B850 unit (`task/kb-b850-log-module-unknown-module`,
  kanban t_fcfa2fd8, 2026-09-18):** user-typed module names now go through a
  `TryParseModuleName()` that returns bool, and every site that reads one is
  loud: `ApplyLogModulePair()` rejects the pair naming the token on stderr
  (module untouched), `ApplyLogOnly()` / `ApplyLogExclude()` skip the token
  and name it, `InitLoggingFromJson()` skips an unknown `logging.modules`
  key and names it, and `/logmodule` returns a `Failure` (module AND level
  half, which shared the same silent `LOG_NONE` fallback).
  `ParseModuleName()` had zero callers left once that check was in (the
  ZeroCallers census refused the merge), so the MOD_MAIN fallback is deleted
  with it -- no "any module will do" caller exists. Pinned by six
  `tests/test_logging.cpp` cases (`NTE:DEBUG` rejection,
  `NET:TRACE,NTE:DEBUG,UI:DEBUG` neighbours, whitelist/blacklist typos, the
  stray-bracket message, the try-parse itself), shown RED on the pre-fix
  code. → issue #92

- **B845** (P) **The three `AudioStartupPeak` cases share one on-disk
  `snd_index_cache.json` and flake against each other under `ctest -j`.**
  [measured T500] `TheSteadyStateIsTheSoundFontsAndLittleElse` failed its own
  `ASSERT_TRUE(cold.scanReallyRan())` guard -- "the archive scan was
  short-circuited by a warm index cache" -- in a `-j4` run and passed
  immediately when the three were run alone. `ColdSoundIndex` removes the cache
  in its constructor, but a sibling case running concurrently rewrites it
  before this one scans. The guard did its job (the case refused to measure
  nothing rather than passing hollow), so this is an isolation defect, not a
  measurement one. Fix: give each case its own `WILLEQ_CACHE_DIR`, or put the
  three in a ctest RESOURCE_LOCK. Nothing to do with T500's change; found by
  its final suite run.
  [closed 2026-09-17, task/kb-b845-audio-peak-isolation] Root cause [measured]: `ColdSoundIndex`'s
  per-process `WILLEQ_CACHE_DIR` (`/tmp/t496_audio_peak_<pid>`) already
  isolated the WRITE, but `loadPfsIndexCache()` -> `ResolveCachePathForRead()`
  fell through the empty per-process dir to the SHARED `<exe dir>/cache`
  (`build/bin/cache/`) -- which 15 other test binaries (no `WILLEQ_CACHE_DIR`)
  also read and write. The fixture kept that shared file away with a rename to
  a shared `.t496bak` (hide in the ctor, restore in the dtor) -- cross-process
  mutable state; a sibling's step landing between this case's hide and its read
  short-circuits the scan. Reproduced deterministically against the original
  code by pre-planting the sibling-in-flight on-disk state (shared file absent,
  `.t496bak` present holding the valid warm index) and restoring the shared
  file before the victim's read: `Value of: cold.scanReallyRan() / Actual:
  false / Expected: true / the archive scan was short-circuited by a warm index
  cache`, `1 FAILED TEST`. Fix at the root: a set `WILLEQ_CACHE_DIR` is now
  AUTHORITATIVE in `CacheDirCandidates()` (`src/client/core/data_paths.cpp`) --
  read and write are confined to that directory with no shared fallback; every
  production caller leaves the variable unset, so no shipped path changes. The
  fixture no longer renames anything: it creates a per-process directory,
  points the resolver at it, and EXPECTs the resolver honoured it (read reports
  a MISS, not a shared hit; the write lands in the per-process dir), so a
  regression that reintroduces a fallback fails the case itself instead of
  measuring nothing. After: the same trigger passes 10/10 with the scan really
  running (`[measured] audio live after initialize(): 25681280 bytes`,
  `scanReallyRan()` true), and 10/10 rounds of `ctest -R AudioStartupPeak -j3`
  are clean. A ctest `RESOURCE_LOCK` on the three cases (hand-registered in
  `tests/cmake/audio.cmake`) is the second belt, not the fix.


## Added 2026-09-17 by T502 (no CI; hermes workers OOM at 4 GiB)

~~- **B847** (P) **There is no CI.** Every build and every ctest run happens
  wherever an agent happens to be, and when that is a hermes kanban worker the
  worker is capped at 4 GiB and is OOM-killed while linking -- so the suite is
  not run at all on the path a worker takes, and a red tip can reach `fable`
  with nobody having compiled it. [measured T502] the project's GitLab
  (`willeq/willeq`, id 1) had `ciConfigPathOrDefault` = `.gitlab-ci.yml` and no
  such file; the one registered runner (id 1, shell executor, `concurrent = 1`)
  had never run a job for this project.~~ **STRUCK by T502 (this commit):**
  `.gitlab-ci.yml` adds four stages -- `hygiene` (no C++ build), `build`
  (Release, `-j8`), `test` (full ctest plus the intermittent RDP case with
  `--repeat until-pass:3`) and `gate` (`visual_gate`, 3 in-script retries,
  **not** `allow_failure`) -- on the runner, which executes on **hermes-ext**
  (16 cores, 31 GB: the job log's own `hostname; nproc; free -g`), not in a
  4 GiB worker. Every job refuses any host but hermes-ext (Will's 2026-09-17
  ruling that the Inspur inference box must never build) and is bounded to 8 of
  16 cores at `nice -n 10` so a pipeline cannot starve a kanban worker on the
  same host. `docs/ci.md` documents how to read a pipeline, the warm-tree
  trade-off and how to retry the gate. First green pipeline and the per-stage
  wall times are in `refactor/T502_gitlab_ci.md`.

B828–B829 and B832 unused. B836–B845 used by T500 (B840, B842, B845 closed by the kanban pilot, below T501's section).

B848 used by the kanban pilot (issue #4, migration card). B849 (issue #91), B850 (#92), B851 (#97), B852 (#98, the A-section entry above) and B853 (#99, swept clean 2026-09-21, `refactor/B853/`) were filed straight to GitLab issues by the orchestrator 2026-09-18/20 and have no board entry of their own (docs/issues.md: the issue is the record). B854 closed 2026-09-23: Pi zone-in sweep 46/46 zones, 43 clean (iceclad stall, templeveeshan seed_failed, sleeper zone_boot_failed).

## Added 2026-09-23 by T504 (the load-step memory audit, merged as fable b09bb498; B751 as 67526073)

Defects found and fixed INSIDE the audit (struck at birth; the commit is the record, the plan documents hold the measurements: `refactor/T504b_entity_source_pruning.md`, `T504c_wld_fragment_filter.md`, `T504d_hc_map_bvh_compactness.md`):

- ~~**B859** (A) `ThreadLogBuffer` only flipped its double buffer on the owning thread's next append, so an idle thread's pending log lines never drained~~ -- 3278da58 (+ `LoggingTest.QuietAfterBurst_*`).
- ~~**B860** (A) `S3DLoader` opened the same archive five times per zone load and `EquipmentModelLoader` re-parsed the same WLD per item~~ -- single open (`parseFromOpenArchive`), equipment parse cache (b0d6cff7).
- ~~**B861** (A) every character archive's WLD was parsed TWICE per load (loadZone + loadCharacters)~~ -- b793fa99; gfaydark entity_models peak 63.7 -> 51.9.
- ~~**B862** (A) a character-archive load built a combined "zone" geometry of all its meshes, inflated every texture twice and opened the archive a second time, none of it read~~ -- 7bfbdaa8; peak 43.6 -> 25.9 (gfaydark).
- ~~**B863** (A) `PfsArchive::open` read the file AND copied every member's blocks (2x the archive at open, 1x retained, 3,234 allocations)~~ -- mmap, 3d5d31f2; zone_parse peak -6 MiB/zone.
- ~~**B864** (A) `EQE1TextureIndex::load` built a 26 MiB jsoncpp DOM of the 1.9 MB index on EVERY zone load -- the whole `archives` peak~~ -- streaming parser, 329a2e1b; archives peak 35 -> 22.
- ~~**B865** (A) the equipment index was rebuilt on every zone load from 17 archives, 11 of them outside `GlobalLoad.txt`~~ -- era six, once per process, 31c198a0; archives peak 22 -> 5.4.
- ~~**B866** (A) every 0x22 region filled a private PVS store that `addRegion` copied into a growing arena (13 MiB transient for 4.6)~~ -- in place + exact reserve, 31c198a0.
- ~~**B867** (A) the combined zone geometry (19.5 MiB vertex+triangle copy in iceclad) was built on the PVS path that only reads its texture names and bounds~~ -- materials+bounds summary, 31c198a0.
- ~~**B868** (P) the branch's CI `build` job had failed since 108ba056 (`-Werror=format-truncation` in `FormatTimestamp`), so the test job never ran on it~~ -- bounded copy, 31c198a0; suite then run locally (4,987 tests).
- ~~**B869** (A) T504b's render-thread deferral handed model lookups to a prep worker that tests and tools do not have -- never loaded (test_held_item_attachment SEGV)~~ -- the worker request reports acceptance, 31c198a0.
- ~~**B870** (B) `[modelmem]` double-counted the adopted global_chr other-chr entry and charged a merged-texture map copy per model although models share one snapshot (split off by 47 %)~~ -- counted once, 31c198a0; split +0.25 %.
- ~~**B871** (A) `RaceModelLoader::getMemoryStats` and three tools listed `s3d_loader.cpp` without the ledger; `test_graphics` lacked `era_model_sources.cpp`~~ -- CMake lists fixed, 3d5d31f2 / 31c198a0.
- ~~**B872** (A) `PfsArchive` accepted a zero-length filename-table entry (`filenameLength - 1` wrapped)~~ -- 8ef2e108.
- ~~**B873** (A) T504b's per-race prune dropped the characters the NEXT race needed, so each archive was parsed once per race (gfaydark: 6x global_chr, 6x gfaydark_chr)~~ -- race-set prefetch, cdbce199; preload time -40..-70 %.
- ~~**B751**~~ see its own entry (#73): the Pi zone-in stall = 42 % kernel drops of the zone-in burst behind a 425 KiB fallback buffer -- MR !29.

Follow-ups filed as records (not started):

- **B855** (A) the inflated zone WLD is the largest remaining zone_parse transient (iceclad 17.4 MiB) -- stream or partially inflate it. -> issue #106
- **B856** (A) libLLVM + libgallium are 33.5 MiB of file-backed RSS on the Pi even on Lima -- an LLVM-less Mesa build. -> issue #107
- **B857** (C) skel_containers -- the largest compactness term left in entity_models; needs its own plan per Will's rev-1 rule. -> issue #108
- **B858** (C) zone-in Phase 10 is keyed on ONE unreliable packet (our own OP_ClientUpdate); a lost datagram is a permanent stall. -> issue #109

- **B874** (B) `--pose-log` (Deliverable B, issue #218). PROVEN LIVE 2026-09-29 (`refactor/B874/live/README.md`): a zoned-in rendered client driven by `--script` wrote the log, and `scripts/pose_live_capture.sh` (ctest `PoseLogLiveCapture`, label `live`) checks it against the server's `#loc`, a `#save`d `character_data` row and kChaseRig -- position and heading agree through the documented transform. The test was RED on two findings it measured -- the rendered heading ignored `/turn`, movestate read idle through a `/move` walk -- now B911/B912 below, both fixed; the rerun on the VM lane is 39 PASS / 0 FAIL (`refactor/B874/live/run-20260929T042131Z/`). Also measured: client z 0.63 below the server's, and nothing but `#save` wrote the moved pose to `character_data`. OWED: Deliverable A -- a server-authoritative pose log for the OFFICIAL client (B875/B876).

- ~~**B911** (A) `/turn` (and `/face`) changed the server's heading but not the rendered one. `EqActionHandler::setHeading()` wrote the store and the wire; the store's `PlayerMoved` goes to the STATE layer's EventBus, which never reaches the renderer.~~ **FIXED + VERIFIED LIVE** (task/B911-B912-command-movement-render): it now publishes the renderer-bridge `PlayerMoved` through `MovementComponent::PublishSelfPoseToRenderer()`. Pinned by `tests/test_command_movement_render.cpp` (`TurnCommandReachesTheRenderedHeading`, `FaceLocationCommandReachesTheRenderedHeading`; red without the fix: rendered 28 vs game 128, the live numbers). Live: rendered heading 128 7 ms after `/turn 90`, `#loc` check delta 0. -> issue #230
- ~~**B912** (A) a command-driven walk (`/move`, `/moveto`, `/follow`) logged movestate 0 in every row, and the walk's end (and the arrival snap onto the target) never reached the renderer. `IrrlichtBridge` dropped `PlayerMovedData::isMoving`; `StopMovement()` published nothing.~~ **FIXED + VERIFIED LIVE**: the bridge hands `isMoving` to `PlayerCameraComponent::setPlayerGameDrivenMoving()` (latched until the next `PlayerMoved`; `isMoving()` = keyboard flags OR it), `StopMovement()` publishes the final pose with isMoving false after a command walk (`HasCommandMovement()`, asked before the clear, so keyboard movement is untouched), and a server self-update no longer carries velocity-as-isMoving. Pinned by `MoveCommandIsNonIdleWhileMovingAndIdleAfter` (red with either half reverted). Keyboard movement was never affected: `KeyboardMovementIsNonIdleWhileHeldAndIdleAfter` passes before and after (code + offline real-renderer test; NOT measured live -- the rig drives only `--script`). The "14 distinct positions" was not coarse stepping: one new position per rendered frame for the whole (0.23 s) walk. Live: movestate 1 on exactly the 15 walking rows, 0 on all 299 after. -> issue #231
- **B916** (C) → issue #235. Open questions left by B911/B912, none implemented: (1) render-side interpolation of a command walk when the renderer runs faster than the game tick (today both are ~60 Hz on x86 so every frame gets a new position; on a slower game tick positions would repeat) -- a behaviour decision; (2) the player model's animation during a command walk: the renderer's `updatePlayerMovementAnimation()` keys on keyboard input only, so from code the model likely does not play its walk cycle -- unmeasured, needs a video; (3) `MovementComponent::Face()` (auto-attack facing, `CombatManager`) still changes the heading without a renderer publish -- same class as B911, left alone because it can fire while the keyboard owns the pose; (4) a pathed `/move` stops up to 5 units short (`UpdateMovement`'s `waypoint_reached` radius; live 4.49 and 4.53 units) -- B874 finding 4's cause.

B874-B876 are the hermes pose-oracle ruling (docs/decisions.md 2026-09-23, issues #218-#220); B877-B887 unused (T505 renumbered its block to B888-B901 after allocating B874+ concurrently); B902 onwards are allocated on GitLab (B911/B912 above are #230/#231). Allocate from the MAX `B###:` issue title on GitLab, not from this file.

T505 (2026-09-23) -- Will's rulings: feature gaps vs the official client are class B on GitLab; every gap carries an oracle recipe, a repeatable CI test and parallel legs (docs/issues.md § Feature-gap issues). Filed as records (not started):

- **B888** (B, feature) native character creation: the official two-page screen and progression, home city (no tutorial), post-Velious races/classes excluded. → issue #204
- **B889** (B, feature) capability-level CI suite: a README-row → oracle → test → CI-lane matrix, nightly live + pi stages. → issue #205
- **B890** (B, feature) boats/transport. → issue #206
- **B891** (B, feature) swimming speeds are guesses, drowning and the Air Remaining window are missing. → issue #207
- **B892** (B, feature) levitation never applies to the player (eqemu skip-self appearance; derive from own buffs). → issue #208
- **B893** (B, feature) two unmeasured jump models, fall damage never sent. → issue #209
- **B894** (B, feature) forage and fishing are sent but never measured or shown. → issue #210
- **B895** (B, feature) tracking window and direction messages. → issue #211
- **B896** (B, feature) player trade window (callbacks are an empty stub). → issue #212
- **B897** (B, feature) friends window with friends + ignore lists; `/friends` alias conflict. → issue #213
- **B898** (B, feature) skill trainer window (reply decoded then dropped). → issue #214
- **B899** (B, feature) era tradeskill container window + Combine; recipe window is post-Velious. → issue #215
- **B900** (B, feature) single ranged shot impossible, arrow never drawn; /autofire era uncertain. → issue #216
- ~~**B901** (B, feature) item linking~~ -- OUT OF BASELINE (post-Velious), recorded and closed. → issue #217
- Feature-gap plans (oracle + test + legs) appended to existing issues: #25 lifts/switches/traps (B420), #151 item pick-up/put-down (B442), #116 spell visuals (B161), #144 Options controls (B430; #145/#146 pointed at it).

## Added 2026-09-29 by the rendered Pi sweep (#102, branch task/B854-rendered-pi-sweep)

Every entry below comes from `refactor/B854/RENDERED_SWEEP.md`, measured on the Orange Pi (fable 3c6ea8b3, binary sha256 6888f621...). Issues filed 2026-09-29 (B917-B922; the spatial entry is B909, issue #229).

- ~~**B917** (A, client) → issue #236. A server-directed zone move DURING the initial zone load kills the client.~~
  **CLOSED by `task/B917-B918-midload-zone-change` (2026-09-29).** Root cause: ONE
  shared `ZoneLoadSnapshot` -- the game thread overwrote it on the mid-load
  zone change (the running loader read it by reference), then finalised the
  FIRST load as the second (`Ready!` with nothing built, BSP tree mislabelled)
  and CLEARED it while the render thread started the second load from it
  (`.s3d` / `qeynos.s3d`). User-reachable: death while zoning in, a GM
  summon, a kick. Fix: `eqt::ZoneLoadHandoff` (post / take / generation /
  finalise only the awaited load). Pinned by `tests/test_zone_load_handoff.cpp`
  (RED without the fix). **Live, local lane, rendered x86**: reproduced 2/2
  pre-fix (`--cpus 0`; `.s3d` FATAL, exit 0), fixed 3/3 end in qeynos
  rendered with a clean `/q`. Not run on the Pi. `refactor/B917/README.md`.
- ~~**B918** (A, client) → issue #237. The client exits **0** after a FATAL loader failure.~~
  **CLOSED by the same branch.** Outside `--scenario` a FATAL-ended run now
  exits **4** (`kClientFatalExit`, `eqt::scenario::ProcessExitCode()`), a clean
  `/q` 0; `--scenario` stays 0..3. Covers the loading-thread failure and
  `LoadZoneSources()`'s six FATAL-then-quit sites (found live: a relic seed
  exited 0). Pinned offline (RED without); **live**: the relic
  LoadZoneSources FATAL exits 4 (was 0). The loading-thread FATAL -> 4 is
  offline-only (the B917 fix removed its only known live trigger).
- **B909** (A, memory) → issue #229. The live `spatial` ledger category is over its orangepi budget (4 MiB) at `ready` in 25 of 45 rendered Pi zones.
  - Worst: growthplane 42.1 MB, trakanon 29.0, overthere 18.0, emeraldjungle 14.6, skyfire 13.3.
  - The CI gate asserts the budget over five zones in the offline benchmark only.
  - No other budgeted category was over at `ready` in any zone.
  - This is a defect to fix in code. The budget is NOT to be raised (docs/decisions.md 2026-09-28).
- **B919** (A, memory) → issue #238. The rendered Pi client is over the 64 MB design yardstick in 44/45 zones at peak RssAnon: median 98.0 MB, worst growthplane 159.2 MB.
  - Only nurga is under (60.3 MB).
  - At `steady_60s` the median is 89.4 MB and 41/45 are over.
  - The largest ledger categories at `ready` are `gpu_driver` (18.4 to 20.2 MB; #1 in 30/45 zones) and `game_state` (up to 51.2 MB, emeraldjungle; #1 in 11).
    B941 (#259) shrinks `game_state` on its branch (emeraldjungle 53.6 → 34.8 MB on the Pi); these figures are the pre-B941 sweep.
  - It is under the 256 MB failing bound everywhere: worst VmHWM 218.7 MB, and no OOM.
  - The shrink queue for the Pi should be planned from these rows (docs/decisions.md 2026-09-17 addendum).
- **B920** (A, tooling) → issue #239. The CI-published ARM binary (`ci-arm/<sha>/bin/willeq`, on NFS) carries no file capability.
  - The documented setcap recipe fails silently if the copy goes to the Pi's `/tmp`: tmpfs is mounted `nosuid` there, so file capabilities are ignored.
  - What works: copy to the Pi's ext4 home, then `sudo setcap cap_net_raw,cap_net_admin+ep`. `scripts/pi_rendered_zone_sweep.sh` does this, and all 57 runs got a 16 MiB forced rcvbuf.
  - Every Pi recipe (`docs/ci.md` "The ARM runner and Pi runs", CLAUDE.md) should say so, or `scripts/pi/` should own a staging helper.
- **B921** (B, rig/content) → issue #240. charasis's safe point (0,0,4) is lethal to Testchar at level 49 and at level 60.
  - The server logs `ZonePC ... zone_mode [3]` (ZoneToBindPoint, "Death Only") 1 to 8 s after `ready`, in all 4 attempts.
  - So charasis's rendered steady state is unmeasured. It needs a survivable seed point or a protected character.
  - Also unexplained: the mid-load kicks of growthplane and templeveeshan on their first attempt, at level 49 against min_level 46. Both passed on retry.
- **B922** (C, tooling) → issue #241. `eqemu_lock_release` (`scripts/eqemu/env.sh`) removes the lock without checking the owner. `pi_lock_release` does check.
  - A late cleanup from one run can drop a lock another run has just taken. Two back-to-back smoke runs came within a second of this on 2026-09-29.

## Added 2026-09-29 by task/chase-constants-pin (B875 re-audit; issue #219 stays open)

- **B875** (B, ORACLE-OWED) **Chase camera constants: fitted ONCE from the official client, pinned by ctest.** → issue #219
  **Pinned now -- fable's CURRENT constants, NOT an official-client measurement:**
  `ChaseRigPinned` (`tests/test_chase_rig_pinned.cpp`, device-free: kChaseRig's
  four fields, kChaseFitEyeAboveFeet, the 62.8 horizontal FOV, the race scale at
  a second eye, the production pose at 4 headings x 2 positions x 2 eyes) and
  `ChaseRigPinnedE2E` (`tests/test_pose_log_e2e.cpp`: the camera a real headless
  renderer places in Chase mode, 4 headings x 2 positions, and its FOV) compare
  against committed literals in `tests/chase_rig_expected.h`, so a 0.001 edit of
  any constant is red (RED runs in `refactor/B875/README.md`). The values are
  corroborated at rest only by one live FABLE run (`refactor/B874/live/`,
  run-20260929T022314Z: pitch 6.0000, azimuth 0.0001, distance
  4.0002). This catches drift; it does not prove oracle parity.
  **Owed:** the official-client fit itself -- manual correspondences on static
  geometry from official stills at server-logged poses (needs #218 deliverable
  A and the oracle rig), designed in `refactor/B875/FIT_PLAN.md`; the solver
  (`tools/chase_fit/`, `chase_rig_fit`) is validated offline on a synthetic
  camera that is not kChaseRig (`tests/test_chase_rig_fit.cpp`).
  **MR !31 (`task/kb-chase-oracle-session`) must NOT be merged as it is:** its
  `refactor/B875/chase_fit_golden.json` is circular (points projected with the
  reference camera, then "fit" to the reference; it cites a pose no capture was
  taken at) and its `ChaseConstantsFitPinned` test checks only the JSON's
  structure (green with the distance at 4.001). Both are superseded by the tests
  above.

## Added 2026-09-30 by task/B941-game-state-memory (issue #259)

- **B941** (A, memory) → issue #259. `game_state` was the largest owned category on the Pi (emeraldjungle 53.6 MB at `ready`): the collision map (`HCMap` + `RaycastMesh`) and the Detour navmesh. **Fixed on the branch, MR pending review**: placed-model collision triangles are indexed per placement instead of a triangle soup, both loads stream and free their buffers, the navmesh's unused `dtLink` tail is trimmed, refused navmesh tiles are freed. Queries bit-identical (`GameStateIdentity.*`, x86).
  - Pi A/B `game_state` at `ready`: emeraldjungle 53.6 → 34.8 MB, growthplane 26.3 → 22.7, gfaydark 13.8 → 11.5, timorous 35.2 → 30.5; owned at `steady_60s` emeraldjungle 102.4 → 83.5 MB.
  - Owner rows `gs.*` under `WILLEQ_LEDGER_OWNERS=game_state`. Evidence and "Not verified": `refactor/B941/README.md`.
  - Open: the Pi's timorous Player Movement phase was measured on one leg per side (821 vs 1,986 us); the zone would not reach `ready` on either side in eight more attempts.
- **B942** (A, pathing) → issue #260. `PathfinderNavmesh::FindPath` passed capacity 2048 for 256-entry straight-path stack arrays. Fixed on the B941 branch. No repeatable test yet (needs a navmesh path crossing more than 255 poly edges).
- **B945** (A, memory) → issue #263. Timorous `game_state` `misc` is 6.2 MB at `ready` on x86; 3.1 MB of it is one `GameEvent` staging buffer the game thread keeps after the zone-in burst. **Closed 2026-10-05** — refuted as a retention defect (the burst is in flight and freed at the first post-load forward, in the base too); bounded steady-state capacity fix merged; see the "Added 2026-10-05 by task/B945-event-staging-capacity" section at the end of this file and `refactor/B945/README.md`.

## Added 2026-09-30 by task/B951-collision-map-arrays (issue #269)

- **B951** (A, memory) → issue #269. After B941 the collision map (`HCMap` + `RaycastMesh`) was still 5-11x its `.map` (emeraldjungle 17.88 MB: indices 7.42, leaf lists 3.77, vertices 4.40, nodes 1.68, marks 0.62). **Fixed on the branch, MR pending review**: the unchanged tree is re-laid-out -- triangles stored once in leaf order as uint16 offsets from a per-64-triangle base, a leaf = its owned range + 3-byte references for triangles an earlier leaf owns, 28-byte nodes, no per-triangle marks. emeraldjungle 17.88 → 10.63 MB, timorous 9.31 → 5.89, growthplane 4.76 → 3.18, gfaydark 6.13 → 4.18. Every `GameStateIdentity` query digest unchanged (x86 and ARM); Pi rays 9-53 % faster. Owner rows `gs.collision_map.{verts,tris,refs,nodes,leaves,flags}`. Evidence and "Not verified": `refactor/B951/README.md`.
  - Pi A/B `game_state` at `ready`: emeraldjungle 34.8 → 27.5 MB, growthplane 22.7 → 21.2, gfaydark 11.5 → 9.5; owned at `steady_60s` emeraldjungle 76.6-77.7 → 69.4 MB; Player Movement not slower in any zone; timorous no verdict (zone link, every leg of both sides).
  - Open: the plan's "well under 2x the `.map`" bar is not met (emeraldjungle 3.0x) -- see B957.
- **B953** (A, threading) → issue #271. `RaycastMesh::raycast()` wrote a shared frame counter and per-triangle marks while the render thread and the game thread cast into the same `HCMap` (a data race that could skip a triangle). Fixed on the B951 branch (`raycast()` is const); `RaycastMeshConcurrency.TwoThreadsGetTheSingleThreadedAnswers` fails 13/20 on the old code.
- **B957** (C, memory vs frame) → issue #275. The collision map is still ~3x its `.map`. Two measured levers (child bounds derived from the parent: nodes to ~40 %; 5-byte delta triangles: emeraldjungle -0.37 MB) each made the Pi's cache-warm rays 11-31 % / 17 % slower. Will's call.

## Added 2026-10-05 by task/B945-event-staging-capacity (issue #263)

- **B945** (A, memory) → issue #263. **Refuted as a retention defect; merged as a bounded steady-state fix.** The issue's "3.1 MB `GameEvent` staging buffer never given back" is the zone-in spawn burst in flight in `GameStateSyncBridge::m_forwardQueue`: alive at the `ready` ledger point (recorded inside the load window on x86, where the render thread is not forwarding), freed at the FIRST `forwardStagedEvents()` after the load — which the base already does. Matched x86 before/after (base 7a621b9c vs fixed, `[b945-cap]` diagnostic: `retained_cap=0` after every forward in BOTH; `misc@ready` 6.23 → 6.23 MB) and the required matched Pi before/after (emeraldjungle — timorous does not reach `ready` on the Pi, the known B925/B940 class — `misc@ready` 316,944 → 317,824, `misc@steady_60s` 1,243,736 → 1,244,768, i.e. identical; no multi-MB single-block `GameEvent` buffer at either point, the top `game_state` block is the 16.4 MB navmesh, owned not misc) both show the issue's number is a measurement artifact, not a leak.
  - The merged change: both drain sites (`GameStateBridge::drainEvents()`, `forwardStagedEvents()`) carry a high-water halving rule so a burst's buffer is returned once the queue drops below half the peak; bounded to steady state (a live burst can never force its own shrink; no re-alloc per frame). 11/11 `test_game_state_sync_bridge` (4 new burst/drain capacity cases).
  - Evidence: `~/kb-evidence/b945-event-staging/{x86-before,x86-after,pi-before-ej,pi-after-ej}`; full tables and the verdict: `refactor/B945/README.md`.
  - Open: none. Recommend closing #263 as not-a-retention (see the README verdict).
  - **Re-review findings fixed 2026-10-06** (review card t_0d1891b6, CHANGES REQUESTED, doc-only): README steady_60s `game_state` row re-cited to the `point=steady_60s` lines (28,450,392 / 28,457,392, delta +7,000 — the previously cited pair was the `zone_change_before_unload` point); "Not verified" section added; the "Repeatable test" pipeline citation now names pipeline #531 on the code head 428f0ff3 (hygiene/build/test/gate success; build-arm and client-ledger-live manual) instead of the pre-README pipeline #512. The final head's docs-only delta is re-verified by its own pipeline (id recorded on the fix card). MR !85 description now carries `Closes #263`.
