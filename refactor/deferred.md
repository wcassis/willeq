# Deferred / Known-Open Items

Live list of open work items extracted from "done" batches and audits
(2026-07-22 doc reconciliation). **This file is a research record, not a TODO
list — when an item is fixed, mark it closed in place (re-title the heading,
add the evidence: a commit hash, a file:line, or a named test) and leave the
body intact. Never delete a section.** Its history has repeatedly been
load-bearing for later units (T46 resolved a conflict between two units by
consulting T43's classification table). Sources named inside entries (e.g.
`arch_design_update.md`, `audit_results_post.md`) are historical docs — do not
edit those.

**T53 (2026-08-05) hygiene pass**: walked every section against the tree at
`607a19c3`, re-titled entries with grep/git-log evidence, and added the index
below. Full verdict table, before/after open-item counts, and everything that
could not be settled cheaply are in `refactor/T53_deferred_board_hygiene.md`'s
Review — read that before scoping the next unit off this file.

## Index — genuinely open items (T53, 2026-08-05)

**Each item below is anchored by its exact `## ` section heading text, not a
line number** — `grep -n '<the quoted text>' refactor/deferred.md` finds it
regardless of how much the file has grown since. This file only ever grows
by append (never-delete rule), so any line number written down today is
guaranteed to drift as later units insert content above it; heading text
does not. Grouped by kind; ungrouped placement in the body is unchanged
(additive only).

**Architecture / needs a ruling**
- TWO `state::GameState` instances exist, unconnected — needs a decision, not
  a drive-by. **T78 (2026-08-12) did the census the decision needs — see
  `refactor/T78_two_gamestate_analysis.md`'s Review, or this section's own
  T78 addendum, before restating the problem.** Heading: `## TWO
  \`state::GameState\` instances exist, unconnected`.
- Command-registry overlap (~34 names span two registries; T51 closed 3, most
  still open). Heading: `## Command-registry overlap and the bare
  \`/target\` precedence gap`.
- `InitGraphics` keeps its `IrrlichtRenderer*` parameter; asset-loader
  namespace relocation (`WldLoader`/`PfsArchive`/`BspTree` out of
  `EQT::Graphics`); `EverQuest::GetDebugLevel()` static call from
  `door_manager.cpp` — all three confirmed still true, T53. Heading:
  `## Decoupling residue — CLOSED by S2` (see its "Still open from that
  audit" list).

**Client correctness bugs**
- ~~`willeq.db` schema version bumped but never validated at open.~~ **FIXED
  by T56 (2026-08-10).** Heading: `## willeq.db schema version is bumped but
  never validated`.
- ~~`--log-level=` is clobbered by `SetDebugLevel(0)` in `main.cpp`.~~
  **FIXED by T56 (2026-08-10).** Heading: `` ## `--log-level=` is clobbered
  by `SetDebugLevel(0)` in main.cpp ``.
- ~~`ConstrainedMeshCache` is never constructed anywhere in `src/` — confirmed
  still true, T53.~~ **RESOLVED BY REMOVAL, T67 (2026-08-11)**: the class
  was deleted from production rather than wired (Will's decision — never
  reproduced eviction pressure, no way to turn it on). Reference pointer to
  the removed implementation (commit `49844c72` + paths) is in the entry
  body and in `src/client/graphics/CLAUDE.md`. Heading: `` ## ConstrainedMeshCache
  is never constructed — RESOLVED BY REMOVAL (T67, 2026-08-11) ``.
- ~~`SpellData::effects` is never populated (`EQT::SpellInfo` has no effects
  field) — confirmed still true, T53.~~ **RESOLVED BY POPULATION, T68
  (2026-08-11).** `EQT::SpellInfo` now carries the 12-slot effect_id/base/
  base2/max/formula columns plus `spell_affect_index`, and
  `SpellDatabase::queryAndCache()` populates `SpellData::effects` from them,
  interpreting the file's markers (effect id 254 / formula 100 = unused) at
  that consumer boundary. This unblocked buff stat totals (verified: a real
  STR buff now applies its real base_value instead of always 0),
  `hasEffect(UltraVision/InfraVision)`, and `isDamageSpell()`/`isHealSpell()`
  discrimination (verified against real damage/heal spells) —
  `spell_effects.cpp:73` and `buff_manager.cpp:500` are no longer dead code.
  This is **step 1 of the spell-effects rewrite** Will scoped (T68 plan): the
  correct `spells.eff` reader (256 records x 2792 bytes = 8-byte header + 3 x
  928-byte blocks, indexed by the now-carried `spell_affect_index`/
  `SpellData::spell_affect_index`) and the data-driven visual layer are still
  ahead. No era filtering is required by decision: `spells.eff` carries only
  the pre-Luclin sprite vocabulary, so a post-Velious spell referencing a
  valid index renders with a classic-era visual, which Will has ruled
  correct. Heading: `## From F00 test repair`.
- Auto-loot on kill never wired to the authoritative death event; shift+click
  loot path unverified. Heading: `## Auto-loot on kill has NEVER worked
  against a real server`.
- ~~`race=415` missing model data still crashes graphics mode — confirmed
  still reproducing, T53.~~ **STALE: actually FIXED by T38 (2026-08-04), one
  day before T53's pass ran; T53 missed it, corrected by T56 (2026-08-10).**
  The crash itself is closed; a genuine data/content remainder (placeholder
  cubes past `config/race_models.json`'s race_id 367 ceiling) is now tracked
  as its own item, see the next bullet. Heading: `` ## `race=415` missing
  model data still crashes graphics mode ``.
- NEW (T56, 2026-08-10): `race=415`/`race=330` render as placeholder cubes,
  a data/content gap (not a crash, not a code bug) — needs asset
  verification and a visual acceptance only Will can give. Heading:
  `` ## `race=415`/`race=330` render as placeholder cubes ``.
- blackburrow/crushbone render entirely blank in the benchmark — not
  root-caused, needs a live render to settle. Heading: `## From Batch H`.
- No unattended character creation on a fresh eqemu DB — confirmed still
  true, T53. Heading: `## From Batch H` (same section as above).
- "Player BSP region is SIZE_MAX" stale-zone-name race — not chased.
  Heading: `## "Player BSP region is SIZE_MAX" with stale zone name`.
- Pale fog-colored flat shapes in gfaydark/qeynos2 goldens — not chased.
  Heading: `## Pale fog-colored flat shapes in gfaydark/qeynos2 goldens`.
- gfaydark zone-in FATAL, missing race=330 model data — open, possibly same
  class as race=415. Heading: `## Eviction hygiene LANDED (P17` (see its
  "NEW (unfixed, pre-existing, graceful)" bullet).
- RaceModelLoader/equipment_model_loader ignore `textureMasked()` for
  non-composite meshes — needs its own unit with live visual evidence.
  Heading: `## P20 live-visual pair` (see its "NEW (P21 audit)" bullet).
- Magenta/black checkerboard missing-texture placeholder on some qeynos2
  rooftops/awnings — not chased. Heading: `## P20 live-visual pair` (same
  section, "New unrelated bug spotted" bullet).
- `ConstrainedTextureCache`'s public query API (`hasAlpha`/`getTexture`/etc.)
  doesn't account for the masked-variant mangled cache key — found by P50,
  not fixed. Heading: `## P50 (2026-07-31): ConstrainedTextureCache's
  public query API doesn't` (heading text wraps onto a second `##` line
  in the file — search for `P50` if the exact string doesn't match).
- Two `applyPaletteMask` plumbing gaps: `mesh_building.cpp`'s on-demand
  `loadTextureFromBMP()` fallback, and `animated_mesh_creation.cpp`'s
  equipment texture-variant swap path. Heading: `## P37 follow-ups` (near
  the end, in the P46d sub-entry's "Open follow-up items" bullet).
- Succor/evacuate/death-bind-respawn zone-position convention — **CLEARED by
  T55 (2026-08-10)**: all three are convention-clean (death/bind is producer
  C, now documented and tested). The real, now-fixed defect was a different
  one: forced-relocation position propagation to the state layer (T39
  pattern surviving the sweep that declared it closed). T55 deliberately
  left the `PlayerProfile` zone-in handler's own copy of this same gap open;
  **CLOSED by T58 (2026-08-11)** — see its "T58 addendum" in the same
  section. Only the per-frame movement writes remain unconverged, by
  decision, needing their own event-storm/perf investigation. Heading:
  `` ## `#goto` same-zone teleport lands at the DB position `` (see the "T55
  addendum" and "T58 addendum" at the end of that section).
- The broader "UI-gesture-only reachability" pattern — narrowed to 6
  confirmed instances by T48/T51/T52, not retired; more producer-less
  intents likely remain. Heading: `## PATTERN: real features reachable
  only through a UI gesture`.

**FIXED but the board still said open — corrected by this pass** (plus one
later correction: `race=415` missing model data — T38 fixed it 2026-08-04,
T53 missed the re-title on 2026-08-05, T56 corrected it 2026-08-10; see
that heading above, now itself in the "Client correctness bugs" list with
a strikethrough, and the meta-finding inside the entry)
- `InventoryManager`/`SpellManager`/`SkillManager`/`HotbarModel` bridge
  wiring headless — the entry (filed as a T45 finding, "NOT FIXED") was
  stale: T46 (`b5f5c1d6`) fixed exactly this via
  `EverQuest::wireBridgeToSubsystems()`. Re-titled in place. Heading:
  `` ## `InventoryManager`/`SpellManager`/`SkillManager`/`HotbarModel`
  never get their own bridge wired headless ``.
- RDP `stop()` use-after-free — entry said "fix still open"; T35 (`a835969`)
  landed the join-then-close reorder. Re-titled. Heading: `## RDP
  frame-update race — ROOT-CAUSED (T03b)`.
- Headless client aborts/never-exits on piped shutdown with no server —
  T30 (`fa31ca97`) fixed it. Re-titled. Heading: `## Headless client aborts
  on shutdown and never exits`.
- Non-FATAL log output never reaching stdout — T31 (`c167acdc`) fixed it
  (periodic `drainLoop()` no longer depends on `FlushThreadLog()`).
  Re-titled. Heading: `## Non-FATAL log output never reaches stdout during
  normal operation`.
- Item links losing 11 characters — fixed within the same T34 unit
  (`7a1e48b9`, after the board entry was written); pinned by
  `tests/test_formatted_message.cpp` (Scaled Curskin case). Re-titled.
  Heading: `## Item links in chat lose 11 characters of the item name`.
- `OnPetCreated()` `#ifdef EQT_HAS_GRAPHICS` residual (noted "not yet fixed"
  inside T43's own PATTERN entry) — fixed in the same T43 commit
  (`fabf0246`); the entry's residual note was simply never updated.
  Re-titled inline. Heading: `` ## PATTERN: `EQT_HAS_GRAPHICS` gates around
  non-graphics code `` (see its "Plus one known residual" bullet, near the
  end of that entry).

**Test coverage gaps (NEW, found answering a status question, T53 2026-08-05)**
- 8 `DoorManagerGraphicsTest.*` skips — gated on `DISPLAY`/Irrlicht-device
  creation in `SetUp()` (`tests/test_door_loading.cpp`); environment-gated by
  design, not re-examined for coverage loss.
- 6 `InventoryModelViewTest.*` skips — **RESOLVED by T54 (2026-08-10),
  `tests/test_inventory_model_view.cpp`.** The fixture itself (its six
  unconditionally-skipped `TEST_F`s plus ~150 lines of now-unreachable
  live-EQEmu-server/DISPLAY helper machinery) was deleted — the subject,
  the paperdoll UI (`WindowManager`/`InventoryWindow`/`CharacterModelView`,
  deleted in `0f25cb57`), was gone, not merely environment-gated. Three of
  the six assertions had live production code behind them at a lower layer
  (`RaceModelLoader`/`EQAnimatedMeshSceneNode`, still alive and unchanged)
  and were ported headless (`EDT_NULL` device, no server/DISPLAY) into
  `RaceModelAnimationHeadlessTest`: `ModelHasTexturesApplied_Headless`,
  `LoadsForRaceAndGender_Headless`, and
  `CreateAnimatedNodeWithEquipment_Headless` (the last one found and
  documented, not silently dropped, that
  `createAnimatedNodeWithEquipment()` does NOT attach weapon geometry —
  see that test's comment for the full trace). The other three
  (render-to-texture, survives-zoning, and the original
  `ModelAnimatesProperly` — already superseded pre-T54 by
  `ModelAnimatesProperly_Headless`) were pure view predicates with no
  production code left to assert on; deleted, not weakened. See the new
  "inventory character preview" entry below for the feature-loss record,
  and T54's own plan/Review (`refactor/T54_inventory_view_test_reckoning.md`)
  for the full port-vs-delete accounting.
- `PfsArchiveContentTest.DdsFileValid` — gated on EQ client files / archive
  DDS-content availability (`tests/test_pfs_archive.cpp`); asset-presence
  gated, not re-examined.
- `SoundBufferTest.LoadSwingWav` — gated on `Swing.WAV` presence
  (`tests/test_sound_assets.cpp`); asset-presence gated, not re-examined.
- **"2 SpellManagerGems skips" reconciled**: no note by that name exists in
  *this* file (searched exhaustively) — but a live `ctest --test-dir build`
  run confirms the number itself is accurate on the current tip: exactly 2
  of 25 `SpellManagerGems.*` tests skip —
  `MemorizeSpellSuccessStartsMemorizationAndPublishesEvent` and
  `MemorizeSpellBlockedWhileAlreadyCastingOrMemorizing`
  (`tests/test_spell_manager.cpp:326-356,473-...`). The other 23 have their
  own `if (!hasDb())` guards too, but `willeq.db` is a tracked binary and is
  present, so those pass; these 2 specifically skip on `if (id == 0)
  GTEST_SKIP() << "no Warrior-usable spell found in scanned range"`
  (lines 337, 482) — a scanned-spell-not-found condition, not a
  missing-DB one. So "they still skip on the current tip" is confirmed
  true, and the gating condition is now on record. If a fuller note lives
  in `refactor/testing_progress.md`, that reconciliation is out of this
  unit's territory (deferred.md only).

**Perf / tooling residuals (lower priority, all narrowly scoped)**
- Z5.3 empty-draw measurement (1-triangle buffer isolation) — TODO, Batch Z.
  Heading: `## From Batch Z`.
- Batch E table in `progress.md` has empty commit columns — cosmetic tracker
  hygiene. Heading: `## Tracker hygiene`.
- P52's `B_e205` absolute magnitude reads ~21-24% high even on the corrected
  path — flagged as directionally-trustworthy-not-KPI-clean, not
  re-diagnosed for budget. Heading: `## P52 (2026-08-01): GPU-sync
  characterization` (heading wraps onto a second `##` line; search `P52` if
  the exact string doesn't match — see the "CLOSED by P52b" paragraph near
  the end of that entry for this specific caveat).
- `docker/irrlicht-drm/CIrrDeviceFB.cpp`'s `waitForFlipComplete()` swap-floor
  question — refined by P52b (real near-e0 effect, negligible at load), not
  independently re-measured beyond that. Heading: `## P41 landed (2026-07-30,
  merged \`edcd72a\`): lighting lever characterized, report-only`.

**A note on staleness**: this Index is a snapshot as of commit `607a19c3`
(T53). As of *this* commit, every heading text above was verified present
verbatim via `grep -n` against the file (see the T53 Review in
`refactor/T53_deferred_board_hygiene.md` for the sample-check log). If a
future reader can't find one, the section was likely re-titled again by a
later unit — search for distinctive words from the description instead
(e.g. the unit ID in parentheses, or a quoted symbol name).

## FIXED: ParticleManager::renderUnified use-after-free (P34, 2026-07-29)

- Root cause confirmed: `atlasTexture_` was loaded once (init(), one-shot,
  guarded by `initialized_`) via `ConstrainedTextureCache::getOrLoad`/
  `uploadTexture("particle_atlas")` — an ordinary LRU cache entry — but
  `ParticleManager` was NEVER registered as a `TextureEvictionListener`,
  unlike every sibling texture-caching subsystem (AnimatedTextureManager,
  SkyRenderer, DetailManager, StormCloudLayer self-register; region/entity
  meshes and the software cursor have their own guards — see
  renderer_zone_merge.h). Exact freeing sequence:
  `IrrlichtRenderer::unloadZone()` runs SYNCHRONOUSLY on every zone
  transition (P17) and unconditionally calls
  `constrainedTextureCache_->clear()`, which notifies listeners then
  `driver_->removeTexture()`s every cached entry including
  `"particle_atlas"` — with no listener, `atlasTexture_` (and
  `particleMaterial_`'s texture slot) were left dangling. The next zone's
  first `renderUnified()` call dereferences it
  (`atlasTexture_->getDriverTextureHandle()`) — SIGSEGV, matching P33b's
  gdb backtrace exactly. Unconditional on every zone unload (not
  memory-pressure-gated like most of this bug class), hence the
  reliable ~1-2s-after-zone-in timing.
  **Fix**: `ParticleManager` now implements `TextureEvictionListener`
  (self-registers/deregisters in `setConstrainedTextureCache()`/
  `~ParticleManager()`, mirroring AnimatedTextureManager's idiom — the
  single-named-entry variant of this codebase's established two-layer
  pattern, not the scene-graph-scanning RegionMeshTextureGuard variant,
  since there's no node collection to scan here); `onTextureEvicted()`
  nulls `atlasTexture_` + clears `particleMaterial_`'s texture slot the
  instant eviction happens, and `onZoneEnter()` (runs every zone visit,
  always after `unloadZone()`'s `clear()`) re-acquires the atlas if null.
  particle_manager.h/.cpp only. Verified: 6-step bar clean,
  `particle_race_stress` clean (100k iters), and 8 live eqemu qeynos2
  sessions post-fix (including a 3m45s single-zone stay) show zero
  atlas-related crashes — see refactor/P34_particle_atlas_uaf.md Review.

## FIXED: SimulationWorker HCMap use-after-free on zone transition (P35, 2026-07-29)

- Root cause confirmed: NOT a missing join on the renderer side (that
  pairing — `stop()` -> `thread_->join()` -> `clearZoneData()` in
  `stopSimulationWorker()` — was always correct). The bug was on the GAME
  thread: `EverQuest::CleanupZone()` (`eq_zone.cpp`) queues a
  `CollisionMapChanged{nullptr}` bridge event and then IMMEDIATELY
  synchronously frees the outgoing zone's `HCMap` (`m_zone_map.reset()`),
  well before the renderer even starts the new zone's loading thread (the
  only place `stopSimulationWorker()`/`unloadZone()` actually runs). In
  the window between those two points — which can be seconds — the
  outgoing zone's `SimulationWorker` thread is still running with its
  `zoneData_.hcMap` pointing at the now-freed `HCMap`; every NPC
  ground-snap tick in that window (`sim_entity_sync.cpp` ->
  `HCMap::FindBestZ()`) was a use-after-free, matching all 4 recorded gdb
  backtraces exactly. Confirms the hypothesis this entry was filed with.
  **Fix**: `EverQuest::m_zone_map` (`eq.h`) is now
  `std::shared_ptr<const HCMap>` instead of `std::unique_ptr<HCMap>`, with
  the same shared_ptr (not a raw `.get()`) copied through every boundary
  it crosses — `CollisionMapChangedData::map` (event_bus.h),
  `ZoneLoadSnapshot::zoneMap`, `IrrlichtRenderer::collisionMap_`/
  `setCollisionMap()`, and `SimulationZoneData::hcMap`
  (simulation_worker.h). The game thread's `reset()` now only drops ITS
  reference; the object survives until every renderer-side holder also
  releases its copy (via `clearZoneData()`/`setCollisionMap()`), so it
  simply outlives every holder instead of dangling under whichever thread
  is slowest to finish with it. `HCMap` is accessed as `const` everywhere
  now (all its read paths were already const methods). 9 files touched
  (payload-type plumbing); all other consumers needed zero code changes
  (`operator->`/`operator bool` work identically on the shared_ptr).
  Verified: 6-step bar clean (1406/1407 ctest, ARM clean, goldens
  untouched), `particle_race_stress` clean, and a live eqemu session
  driving 6 consecutive befallen<->qeynos2 transitions (the exact P34 7/7
  repro) plus a 243s single-zone soak, both crash-free — see
  refactor/P35_hcmap_transition_uaf.md Review.

## Tree wind update unreachable on live GLES2 (P32 finding) — PARTIAL: wind animation FIXED by P33 (blessed); the eqemu port-collision item below is still open, needs Will's call

T53 (2026-08-05) heading note: the body already documents its own
resolution (P33 restored `TreeWindController::update()` and was blessed by
Will 2026-07-29) — this is a re-title only, no new evidence needed, the
original heading just never caught up with the body it introduces.

- `TreeWindController::update()` — the only code advancing wind time
  and the weather-multiplier transition — was ALREADY unreachable on
  the live GLES2 path before P32 (gated on a CPU-instancing tree count
  only the deleted software path populated). The weather multiplier
  has always been effectively frozen at 1.0. P32 preserved that exact
  behavior. RULED by Will 2026-07-28 ("trees are supposed to move;
  only still when the tree wind animation setting is disabled") →
  **P33 LANDED (merged `dd0912e`) with a REFRAMING finding**: the GPU
  wind trees run off IrrlichtRenderer::windTime_, which was ALREADY
  incrementing unconditionally — trees were already animating in
  production, and the real bug was the INVERSE: the animatedTrees
  setting gated nothing. P33 fix: update() restored (weather
  multiplier transitions live again) + weatherMult forced to exactly
  0.0 when disabled (traced GLSL: strength becomes exactly 0 → true
  rest pose). Gate byte-stable by construction (benchmark never calls
  IrrlichtRenderer) + double-run proof; bar green 1376/1377.
  Evidence: P33b stills + P33c videos (qeynos2; gfaydark blocked by
  the eqemu port bug below). **BLESSED by Will 2026-07-29** (video
  verdict). Wind amplitude stays tunable (uWindParams) if ever wanted.
- **eqemu ENVIRONMENT bug (pre-existing, now bit twice)**: the
  akk-stack image's entrypoint static-zone fleet collides with
  configured static zone ports — P29 measured captures landing on
  7012+/7026 (vs tests' hardcoded 7000, testing session's T03a), and
  P33 found gfaydark's configured static port occupied → world
  redirects to a wrong port → infinite login/world/zone-transfer loop
  (gfaydark UNREACHABLE for live sessions). qeynos2-via-#zone remains
  the reliable route. Real fix is server-container config (outside
  sanctioned executor scope) — needs Will's call on whether/how to
  adjust the akk-stack zone fleet.

**STANDING CROSS-SESSION POINTER (2026-07-28): the testing campaign
tracks its work in `refactor/testing_progress.md` (charter:
`refactor/TESTING_CHARTER.md`). Perf session: read its "Handoffs"
section each batch. Handbacks land in `refactor/TESTING_HANDBACK.md`.**

## → FOR THE TESTING SESSION: mutation-testing intel (benchmark session, 2026-07-29) — CLOSED

- The model-eval benchmark session (separate clone) ran mutation
  testing against b68c32a and found: **`wld_loader.cpp` has the most
  covered lines in the repo (952) but killed only 1 of 8 injected
  faults** — the suite executes the parser broadly and asserts almost
  nothing about it. Coverage there is misleading; kill rate is the
  better prioritization signal. Consider assertion-strengthening for
  the WLD parser (and kill-rate spot checks on other "well-covered"
  parsers) in a future T-batch.
- **DONE — WLD half (T10, merged `bd60ee3`, 2026-07-28).** Independently
  measured the baseline as **0/8** (marginally worse than the reported
  1/8) and strengthened it to **4/8**. Confirms the signal. Two dead-code
  findings fell out: `resolveTextureName` is unreachable and the
  per-mesh header bbox fields are read by nothing.
- **DONE — the other half (T22, merged `1559271`, 2026-07-31).** 43
  mutations across six parsers with test binaries: **24 → 37 killed**
  after assertion-strengthening (`spell_database` 2/7→7/7,
  `string_database` 6/8→8/8, `eqe1_loader` 5/7→6/7, `pfs` 5/7→6/7,
  `xmi_decoder` 3/7→5/7, `s3d_loader` 3/7→5/7). Confirms the intel's
  thesis a third time: `ZoneLightLoadingTest` asserted
  `EXPECT_GE(int, 0)` — a tautology that let a total zone-light-loading
  break survive — and `S3DLoader`'s own texture map was inspected by no
  test at all. Survivors representing unobservable behavior (a debug
  counter, a discarded return value, metadata nothing reads) were
  documented rather than papered over. **This item is closed; thanks for
  the pointer, it was worth three units.**
- Their coverage numbers also confirm the T01 gap map: renderer/
  simulation splits (irrlicht_renderer, simulation_worker,
  entity_renderer) at 0% — their benchmark would use coverage there
  as task-verification oracles if it ever exists (FYI, not a request).

## Desktop-GL elimination — COMPLETE (P32, 2026-07-28)

- P31 identified Phase 4 as blocked on a product decision (entry points
  had to close before the deletions were provably safe). Will's
  2026-07-28 scope-ruling addendum (`docs/decisions.md`) resolved it:
  `--renderer` limited to `gles2`, constrained presets made
  renderer-independent, ARM `--no-gles2` Lima build retired. P32
  (`refactor/P32_phase4_final.md`) executed exactly that sequence —
  entry points removed first, then `RenderingBackend::Software`/
  `::OpenGL` + `EDT_BURNINGSVIDEO`/`EDT_OPENGL` call sites deleted,
  `AnimatedTreeManager` deleted (its two live consumers extracted onto
  the pre-existing `TreeWindController`/`TreeIdentifier` TUs), and the
  ARM Lima build retired. The desktop-GL elimination roadmap
  (`refactor/desktop_gl_elimination_plan.md`) is now COMPLETE — see its
  Phase 4 entry and `refactor/P32_phase4_final.md`'s Review for full
  evidence.
- NEW BASELINES (P32): GLES2 tree 1361/1362, headless tree 939/940
  (sole failure = ALSA exception in both). Executor protocol baseline
  note should track ~136x, not 1235.
- Root `.dockerignore` added (P31) — docker builds no longer copy
  ~12GB of local build trees.

## Handback items owned by the perf session (Will, 2026-07-28) — ALL THREE ITEMS CLOSED (per their own inline markers)

T53 (2026-08-05) heading note: all three bullets below already carry their
own DONE/FIXED/RESOLVED markers (P28, P29+T03a, and the .eqemu-lock
process fix) — heading-only re-title for scan-ability, no new evidence
needed.

- **eq.h EQT_HAS_GRAPHICS misclassification — FIXED by P28 (`1cae489`,
  merged `1c4...`/`06cfed7`-adjacent; 2026-07-28)**: all five guard
  blocks reclassified (only renderer-facing API stays guarded);
  headless compile restored 351→0 errors; nogfx suite 812/813; all
  graphics trees at baseline. Combined-tree verification post
  P28+P30 merge: canonical GLES2 tree 1234/1235 equiv (segfault+gate
  failures were STALE main artifacts from Jul 25 — rebuilt, both
  green: gate PASS, headless test PASS). Two flagged seams for later:
  InitializeAudio's constrained-preset read is graphics-gated (no
  headless memory-budget concept — seam documented in P28 Review);
  BspTree/PfsArchive/WldLoader formally reclassified as base data
  (pfs/wld_loader moved to CLIENT_CORE_SOURCES). ORCHESTRATOR
  PRACTICE NOTE: main's gitignored artifacts (third_party static
  Irrlicht, build-gles2 binaries) age silently while slots rebuild —
  any verification run from main MUST rebuild them first (this cost a
  false-alarm investigation on 2026-07-28). Original diagnosis:
  — supersedes the earlier "GRAPHICS=OFF bitrot" framing below with a
  SHARPER diagnosis): the five guard blocks in eq.h enclose large
  amounts of NON-graphics code (GameTick, m_quit_requested,
  InventoryManager, CommandRegistry, hotbar model, EQ client path, the
  163-line member block at ~1689-1853). Correct fix = move non-graphics
  members OUT of the guards (leaving only renderer-facing API), NOT
  adding matching guards to the .cpp files — note this reverses the
  direction of the small hotbar-accessor guards added by the 55b101e
  quick fix. ~33k lines of base-layer source (all 18 eq*.cpp, combat,
  inventory, spell/skill managers, application) are in no test binary
  because of this; blocks the testing campaign's highest-risk backlog
  item (connection flow). Macro-name indirection to confirm:
  CMake option EQT_GRAPHICS vs macro EQT_HAS_GRAPHICS. Unit-sized;
  needs judgment on the mixed 163-line block.
- **Capture fixtures — DONE (P29, merged `8fdcd24`, 2026-07-28)**:
  canonical home `/mnt/projects/fable/captures/` (NFS, README inside),
  `data/captures` symlinks in main + slot3 (other worktrees: create the
  same symlink as needed; .gitignore fixed to match the symlink).
  session.sleq (2,943 records) + session.pcap (5,603 pkts, login/world/
  befallen/qeynos2) + self-created golden; replay deterministic (3
  runs). 10 tests skip→PASS; desktop skips 38→24.
  **→ FOR THE TESTING SESSION (tests/ territory) — CLOSED, T03a
  (merged `02d591d`, 2026-07-28)**: the remaining 4 FAILs + 8 skips
  shared ONE root cause — `tests/pcap_test_utils.h` + three test files
  hardcoded zone port 7000, but the akk-stack image's entrypoint
  auto-boots ~15 static zones claiming 7000-7011, so real captures land
  on 7012+ (befallen 7012, qeynos2 7026 in this fixture). T03a
  implemented BOTH suggested fixes: handshake-based autodiscovery
  (`OP_SessionResponse` sender = server port) plus `WILLEQ_ZONE_PORT` /
  `WILLEQ_ZONE_IP` overrides. 4 FAILs + 8 SKIPs → 12 PASS, and
  compression assertions were verified rather than weakened.
- **Process fix DONE**: .eqemu-lock protocol added to
  EXECUTOR_PROTOCOL.md (the testing session correctly called out that
  P25's session ran the server without a lock — the protocol predated
  the charter; now every executor checks/creates/removes the lock).

## Desktop-GL elimination — ROADMAP LANDED (P24, 2026-07-28)

- Standing decision (docs/decisions.md 2026-07-28): GLES2 is the only
  3D renderer. Full evidence-backed inventory + phased unit plan:
  **`refactor/desktop_gl_elimination_plan.md`** (the authority — this
  entry is just the pointer). Headlines: ~76/90 targets already
  GLES2-clean; 14 link-broken in the GLES2 tree (mechanical, Phase 0 —
  same never-declared-full-link-deps family as the IdentityMaterial
  item below); GLES2-tree ctest 1112 passing with ONE genuine new
  regression: **RaceModelAnimationHeadlessTest.ModelAnimatesProperly_
  Headless segfaults 100% on the GLES2 tree, passes on desktop
  (EDT_NULL fixture, crash in TestBody per gdb)** — Phase 1
  investigation unit, hard blocker for suite certification. Phase 3
  gate SATISFIED: P25 LANDED (`10d95f6`, merged 2026-07-28) — X11-window
  EGL backend, opt-in via `WILLEQ_EGL_X11` env var (deviation from the
  planned --egl-x11 CLI flag: private-header constraint; still explicit
  opt-in, no DISPLAY autodetect, surfaceless default byte-identical,
  gate PASS). Full input translation (keyboard/mouse/wheel/close),
  resize declined round 1 (fixed-size WM hints). Windowed live-session
  evidence: benchmarks/audits/p25_x11_window/EVIDENCE_MANIFEST.txt. Tools: 5 port /
  6 tools RETIRED by Will 2026-07-28, executed by P26 (`2c6a1f9`,
  merged `daefaf8`: sources → tools/retired/, targets removed).
  **Phase 0 EXECUTED (P26)**: both trees register identical 1235 tests
  (byte-identical name lists); GLES2 tree 1233 pass — only the ALSA
  host exception + the Phase-1 RaceModelAnimationHeadlessTest segfault
  (re-confirmed sole genuine failure). model_viewer's break was an
  EQT_DRM-only conditional gate; the other 7 targets had no backend
  conditional at all. GLES2 full-suite recipe: separate tree with
  EQT_BUILD_TESTS=ON WITH_RDP=ON (the build script's own config
  intentionally builds only willeq+benchmark). **Phase 1 EXECUTED
  (P27, merged `7521f32`)**: root cause was class (a) — the patched
  device fork never implemented EDT_NULL (constructor probed DRM/EGL
  unconditionally; createDriver() had no EDT_NULL case → null
  VideoDriver → no scene manager; createDeviceEx expects EDT_NULL
  devices to self-construct a null driver, an invariant every stock
  device upholds). Fixed EDT_NULL-scoped in BOTH device files (ARM
  path provably inert — only EDT_OGLES2 ever constructed there);
  bonus pre-existing fix: P26's 8 targets were missing the X11 link
  line P25's windowed backend requires (isolated via stash — not the
  segfault fix's doing). **GLES2 TREE NOW CERTIFIED: both trees
  1234/1235 with only the ALSA exception; 5x-consecutive pass proof
  both trees.** **Phases 2+3 EXECUTED (P30, merged `06cfed7`,
  2026-07-28)**: GLES2 config is THE primary tree (CLAUDE.md +
  EXECUTOR_PROTOCOL bar updated); system-Irrlicht FIND fallback and all
  X11/Xxf86vm/GL branches removed; resolved-Irrlicht now mandatory via
  either EQT_GLES2_IRRLICHT_DIR or ARM's direct cache vars, with clean
  configure-time FATAL naming the bootstrap (negative-tested); APPLE
  branches kept (ambiguous, never-supported target — flagged);
  X11/GLX log message fixed. Lockstep done by orchestrator:
  benchmark-clone addendum (build_errors_response.md) + charter
  wording. NEW SMALL ITEM: `docker/Dockerfile` (non-ARM image) still
  apt-installs desktop Irrlicht and configures plain — now fails at
  configure BY DESIGN; retire or update that image (fold into Phase
  4/5). — **FIXED by P31 (`4f2e9417`, "Phase 4+5 ... Dockerfile GLES2
  recipe"), T53 (2026-08-05) verification**: current `docker/Dockerfile`
  bootstraps the same patched GLES2 Irrlicht fork (comment at line 14)
  instead of apt-installing desktop Irrlicht. REMAINING: Phase 4 (delete
  dead source paths: BURNINGSVIDEO / AnimatedTreeManager /
  RenderingBackend::Software+OpenGL / EDT_OPENGL loading-screen branch) +
  Phase 5 (full doc sweep + decisions.md completion marker) — await Will's
  go. — **STALE, T53 (2026-08-05): this "REMAINING" was superseded the same
  day it was written.** The "Desktop-GL elimination — COMPLETE (P32,
  2026-07-28)" section earlier in this file (which this entry's own
  Phase-numbering predates) reports Phase 4 EXECUTED and the whole roadmap
  COMPLETE — see that entry and `refactor/desktop_gl_elimination_plan.md`.
  Treat this specific "await Will's go" as historical, not a live blocker.
  The
  willeq-benchmark clone's
  build_errors_response.md guidance (desktop build as default) stays
  CORRECT until the migration phases land — update it in lockstep with
  Phase 2/3.

## Build-config latent defects (found 2026-07-27 by an outside Release build)

An external benchmark setup building at `412d457` with
`-DCMAKE_BUILD_TYPE=Release` exposed two defects our default builds
(empty build type, -O0) never trip:
- **8 tool targets missing `${IRRLICHT_LIBRARY}` link dep — FIXED
  `55b101e` (2026-07-27)**: added the library per the zone_line_editor
  precedent. Root mechanism for the record: at -O0 no reference to
  `irr::video::IdentityMaterial` is emitted so they linked by luck; at
  -O3 the `ISceneNode::getMaterial` comdat carries the extern reference.
  Verified: Release+tests build clean (103 binaries), Release ctest
  1234/1235 (only the documented RDP intermittent; note the Audio ALSA
  exception PASSES on this host in Release — env-dependent as always).
- **`EQT_GRAPHICS=OFF` — RESOLVED by P28 (2026-07-28)**: the real fix
  was the eq.h guard DECLASSIFICATION (move non-graphics members out),
  not more guards — see the handback section near the top of this
  file. Headless compile + suite fully restored (812/813).

## Decoupling residue — CLOSED by S2 (2026-07-23, see refactor/S2_decoupling.md)

EntityPoseState moved to state layer; dual movement channel collapsed to
intent-only (was a render-thread call into game state + doubled position
sends); InventorySlotChanged events now carry display fields (the "dead"
inventoryManager_ pointer was actually a live cross-thread read — audit
claim corrected); bridge payloads typed (HCMap*/PathfinderNavmesh*);
PlayerPositionUpdate struct deleted; eq.cpp graphics includes pruned 12→2.

Still open from that audit (**T53, 2026-08-05: all four re-verified by
grep against current tree, all still true**):
- `InitGraphics` keeps its `IrrlichtRenderer*` parameter (drives
  initLoadingScreen + initDeferredGraphics); full removal needs
  Application-side init restructuring. Confirmed:
  `include/client/eq.h:1166` — `bool InitGraphics(int width, int height,
  EQT::Graphics::IrrlichtRenderer* renderer, ...)`.
- Asset-loader namespace relocation (`WldLoader`, `PfsArchive`, `BspTree`,
  … out of `EQT::Graphics` into a shared asset layer) — own unit.
  Confirmed: both classes still declared under `namespace EQT { namespace
  Graphics { ... } }` (`include/client/graphics/eq/pfs.h`,
  `include/client/graphics/eq/wld_loader.h`).
- Renderer reads spell static data (spell_visual_fx.cpp) and item DB
  (equipment_model_loader.cpp) — acceptable (static data).
- `EverQuest::GetDebugLevel()` static call from door_manager.cpp.
  Confirmed: `src/client/graphics/door_manager.cpp:955`.
- `output/graphical_renderer.h` legacy interface — dead-code deletion
  candidate (IrrlichtRenderer does not inherit from it). Confirmed the
  narrow claim (`class IrrlichtRenderer` has no `: public
  output::GraphicalRenderer` base) — but the header is NOT simply dead:
  `src/client/application.cpp:1869` does
  `dynamic_cast<output::GraphicalRenderer*>(renderer)` and 2 other files
  include it (`src/client/output/graphical_renderer.cpp`,
  `include/client/mode/graphical_mode.h`). The cast is always null for the
  real (Irrlicht) renderer, so the interface is currently unreachable in
  practice, but deleting the header would need checking those 3 sites
  first — not a pure delete-and-done as "dead-code candidate" implies.

## From Batch U / ui_fixes.md

- **Input-lag fix (original Chunk 1) deferred** — application-loop 1 ms sleep +
  60 fps input gating (application.cpp ~438/462) causes 17–20 ms input latency.
  Was waiting on D21b; D21b is done, so this is now unblocked.
- Target casting bar not yet in new UI (irrlicht_bridge.cpp:748).

## From arch_design_update.md / audit_results_post.md

- **Unified priority queue only partially implemented** — `BackgroundWorkQueue`
  instances (zone load, deferred work, BSP preload) remain strict FIFO;
  mesh/texture rebuild queues sort by distance only, not full `WorkPriorityKey`.
- **Door/object placeholder cube** — only asset type without a placeholder
  (arch_design_update.md §7 TODO).
- **Entity/equipment texture failure path** allocates GPU texture before budget
  check (post-audit §3.4, High).
- **GL calls on render thread as fallback** when GPU upload thread unavailable
  (texture_atlas.cpp:187,357) — desktop GL always takes this path (§7.2, High).
- **Unbudgeted `driver_->addTexture()` calls** — sky, animated textures,
  animated trees, zone geometry fallback, software cursor (§3.5, Medium).
- **Budget caches not thread-safe** — ConstrainedTextureCache /
  ConstrainedMeshCache have no mutex; blocks future multi-worker expansion (§3.6).
- `loadDisplaySettingsFromFile()` on render thread (irrlicht_renderer.cpp:3454).
- Dead code in zone_lines.cpp — three `#if 0` blocks (~130 lines) +
  `hasBspZoneLines()` (always false), `debugTestCoordinateMappings()`.
- RDP peer threads not joined until `stop()` (§1.3).

## From F00 test repair (2026-07-22)

- ~~**`SpellData::effects` is never populated.** `EQT::SpellInfo`
  (include/client/willeq_database.h) has no effects field, so
  `SpellDatabase::queryAndCache()` leaves every spell's effects array empty,
  and `isDamageSpell()`/`isHealSpell()` can never return true via this path.
  The old file-based loader populated it; the SQLite migration dropped it.
  Whoever owns the spell-effects data path should restore or formally remove
  it.~~ **RESOLVED BY POPULATION, T68 (2026-08-11)** — see the `## Client
  correctness bugs` entry above for the full writeup; this is step 1 of the
  spell-effects rewrite arc, not the whole rewrite.
- Case-insensitive and partial-name spell search no longer exist
  (`getSpellByName` is exact-match `WHERE name=?`); old tests covering them
  were deleted in F00. Restore only if a consumer needs them.

## Batch G — CLOSED (2026-07-23, see refactor/G_decomposition.md)

loadZoneSequential decomposed into 20 step methods (largest 408 lines);
RegisterCommands split into 3 domain TUs each under 1,500; StartCombatMovement
overloads co-located.

Files still over the 1,500-line warn ceiling (all under the 3,000 hard
ceiling; accepted): renderer_zone_load.cpp (2,795 — now structured as step
methods), eq_entities.cpp (2,438), eq_player_info.cpp (2,006),
eq_movement.cpp (~1,975), eq_bridge.cpp (1,837), renderer_frame.cpp (1,583),
renderer_player_physics.cpp (1,570), eq_merchant.cpp (1,558), plus the
accepted cores eq.cpp (~3,600) and entity_renderer.cpp (3,201).

## From Batch H (2026-07-23) — PARTIAL: spire-launcher item RESOLVED (marked inline); blackburrow/crushbone and char-creation confirmed still open, T53 (2026-08-05)

- **blackburrow and crushbone render entirely blank in the benchmark** at
  every camera position tried, even with all culling disabled (~2000-4000
  draws submitted, zero visible output). befallen renders fine. Not
  root-caused (H02 review speculates a BSP above/below-ground split issue).
  Could be a real rendering bug affecting those zones in production —
  investigate.
- **No unattended character creation on a fresh eqemu DB** — willeq lacks
  OP_CharacterCreate client-side, and eqemu has no console/GM command to
  create a character from nothing. seed.sh stops cleanly on a fresh install.
  Closing this means implementing the Titanium char-create handshake in the
  client (H03 review documents the server-side flow to match).
- ~~spire launcher "wedged"~~ RESOLVED 2026-07-23: never wedged — the H03
  executor invoked `spire eqemu-server:launcher start` directly, an internal
  entry point whose process check matches its own cmdline ("already
  running" by self-match). The sanctioned path is `spire spire:launcher
  start` (the container's `start` alias), verified to boot the full fleet.
  up.sh keeps direct-binary single-zone boot as CI default (lighter: 1 zone
  vs ~15) with `--launcher` for the sanctioned path.
- The container's `process-watcher.pl` kills any process using >30% CPU for
  60s whose name isn't allowlisted (gcc/mysql/world/etc.) — `zone` and
  `spire` are NOT allowlisted. A busy zone process during automated runs
  could be killed mid-test. Watch for this in CI; the allowlist lives in
  akk-stack assets (server-code-adjacent — flag before changing).

## Performance gap on real hardware (H04 baseline, 2026-07-23) — HISTORICAL: the headline 30fps gap this section opens with was later closed (see "KPI STATUS — befallen at the 30fps line (P11b...)" and P14 below); read as an arc, not a live blocker

T53 (2026-08-05) heading note: most sub-items already carry their own
FIXED/CLOSED/RESOLVED/DEAD markers inline (CMA leak overturned, v1 loader
fixed, format shootout decided). Genuinely still-open residuals from this
section are already carried forward into later sections (occlusion budget
— closed by P14; per-vertex-vs-per-pixel lighting — decided by P41/P42;
live-game-only full-population per-frame loops at sim_entity_sync.cpp:179
etc. — not independently re-verified by T53, flagged as
cannot-settle-cheaply without a live profiling session).

- **All three golden zones render below the 30fps KPI on the Orange Pi**
  (p95, 800×600, performance governor): qeynos2 ~28.8, gfaydark ~24.3,
  befallen ~22.4 fps (RGBA path — P01 confirmed RGBA numbers are stable/
  trustworthy; EQE1 columns are not, see below). P01 also CLOSED Will's
  lighting-vs-culling question: lighting is never applied to culled
  geometry (traced sim_visibility.cpp:91-149 → renderer_sim_bridge.cpp:
  700-705 → renderer_zone_draw.cpp:266-373), and culling is worth ~27x
  (947ms p95 with both culling systems off vs ~35ms on, qeynos2). This is the measured target for all
  future perf work; scripts/pi/bench_pi.sh + compare_pi.sh regression-track
  it automatically. Candidate levers already in this file: unified priority
  queue, GL-on-render-thread fallback, pointer-key draw order, render-loop
  blocking items.
- **P08 (2026-07-25) decomposed the CPU frame** (canonical befallen replay,
  production config, pinned poses bf355/472/626/791/917/987): submission
  (`setMaterial`+`drawMeshBuffer` loop) is 87-96% of CPU draw time on both
  x86 and Pi; Mali400/Lima per-draw cost 2.15-12.55 ms/100 draws synthetic
  (5.9-16.3 real). Ranked levers — RE-RANKED by P08b (2026-07-25,
  gameplay-load reality check; calibrated N=205 → median 38 visible
  entities, matching H04's live cap; P08's 200-entity "stress" config
  turned out already gameplay-representative by visible count):
  1. **Entity draw-path: composite entities LANDED by P08d (2026-07-26,
     blessed by Will; eligibility-gated: !isPlayer && !hasBodyPartOverride
     && !hasEquipmentTint; static quality evidence pixel-identical).
     Preset flip CARRIED 2026-07-26 ("bless flip") — composites live on
     the orangepi preset for eligible NPCs. Remaining work in this lever:**
     - ~~Builder-extension unit~~ **DONE by P08f (2026-07-26,
       b9e1a2a)**: --zone/--all-zones support, 76 new sheets (286 total,
       all audits clean), golden-zone coverage befallen 10.5→100%,
       qeynos2 21.4→100%, gfaydark 5.6→94.4%; 5/5 spot-checks
       pixel-identical; 8/8 befallen races resolve compositeData=YES.
       Sheets on NFS — live everywhere immediately. Residuals:
       (a) WOL_HS_DEF collision (gfaydark vs global6, different content;
       needs zone-qualified naming or runtime disambiguation — small
       follow-up if that model matters); (b) --all-zones sweep not yet
       run (documented invocation: entity_composite_builder --all-zones
       --zones-json docs/reference/pre_luclin_zones.json --eqe1; run it
       before playing non-golden zones with composites); (c) several new
       race codes lack numeric EQ race-ID mappings in race_codes.cpp
       (found in passing, unfixed).
     - **Animated-path crumple — RESOLVED by P12b (2026-07-27)**: the
       benchmark uploaded each entity's per-instance animated
       instanceMesh_ as a STATIC GLES2 hardware buffer before the first
       pose; COpenGLES2Driver::drawMeshBuffer() draws HWBufferMap
       entries from the frozen VBO snapshot forever (never checks
       setDirty/ChangedID) → CPU animation silently discarded.
       Benchmark-only (differential-tested); fix = drop the HW-buffer
       registration, use production's CPU-vertex-array path. Animated
       benchmark evidence is TRUSTWORTHY again. VertexMapping
       one-to-many patch LANDED (loop closed, byte-identical rendering,
       zero regressions); ModelAnimatesProperly replaced with a running
       headless variant + test_inventory_model_view wired into ctest
       (suite now 1235). NEW follow-ups: (a) **driver footgun**:
       COpenGLES2Driver static HW buffers never honor dirty updates —
       document/guard so production never HW-buffers an animated mesh;
       (b) **Animated composite delta — P18 INVESTIGATED, STOP with
       narrowed evidence (2026-07-27)**: original suspect list exhausted.
       Lighting flag is dead code at runtime (entity_renderer.cpp
       overrides mat.Lighting=false uniformly under the GLSL shader);
       normals through instance-mesh skinning disproven EMPIRICALLY
       (vertex-dump diagnostic: bit-identical Pos/Normal between 13-buffer
       and 1-buffer paths at the same anim frame); vertex color ruled out
       (both builders hardcode white). Note max_channel_delta is 182-224/255
       — the "low magnitude" was the MEAN only; the delta is structural,
       not FP noise. NEW LEAD: composite forces TextureWrapU/V=CLAMP_TO_EDGE,
       multi-buffer leaves REPEAT; a UV-extent-aware clamp patch REGRESSED
       the diff to 83-92% (reverted), pointing at GLES2 driver texture-object
       wrap-param state caching when a shared/cached ITexture* is used by
       materials wanting different wrap modes (draw-order-dependent stomping).
       P22 (2026-07-28) then REFUTED the wrap-param cache lead by
       instrumentation: wrap glTexParameteri is applied UNCONDITIONALLY
       per setMaterial (no dedup exists), 7,707 instrumented draws show
       0 readback mismatches and 0 shared-texture wrap conflicts
       (composite atlas textures are per-entity unique GL objects,
       never shared with multibuffer's) — the stomping scenario cannot
       arise. Bonus driver fact: NO mipmaps exist anywhere
       (regenerateMipMapLevels has zero callers; MIN_FILTER never
       *_MIPMAP_*), killing the mip-pollution framing too. The delta
       REMAINS OPEN; surviving suspect = per-path mesh construction
       differences (mesh_building.cpp caller-side "fixes" proved
       REGRESSIVE per P18 — a future unit needs a different angle,
       e.g. per-buffer UV/geometry stream comparison at the GL level).
       Static remains pixel-identical; not visually user-reported.
       Reviews: P18 + P22 plan files;
       (c) **P11b**: true composite
       perf magnitude on Pi (animated e205 replay) — P12b's optional
       phase was skipped.
       P12's original escalation record:** ownership verdict is SHARED PRODUCTION
       CODE (RaceModelLoader/EQAnimatedMesh/SkeletalAnimator) — the
       benchmark drives the identical path with the identical idiom;
       "production animates fine" is UNVERIFIED belief (the one test
       that would catch it, test_inventory_model_view
       ModelAnimatesProperly, is unconditionally GTEST_SKIP'd). Crumple
       present from frame 0 (not drift). Parked VertexMapping patch
       EMPIRICALLY NOT this bug (byte-identical A/B; real latent issue,
       still parked). Ruled out: synthetic OnAnimate driving, P05
       animation-track merge, per-buffer mapping population, bone-index
       correspondence, matrix convention, bone scale/translation.
       BEST LEAD for P12b: a raw vertex piece with bounding box wider
       than the whole skeleton's bone span → suspected per-vertex
       bind-pose disagreement between applySkinning's load-time bake
       and runtime frame-0 reconstruction. P12b should also UN-SKIP/
       repair ModelAnimatesProperly, and FIRST answer the discriminating
       question: do NPCs visibly animate correctly in live play
       (Pi/GLES2 and desktop paths)? If yes, there IS a path delta the
       bisection missed; if no one can confirm, this may be a live
       defect. Also noted by P12: a visual_gate foliage pixel mismatch
       on slot2 with ZERO source diff (environment/Xvfb suspected —
       cross-check against P11's slot1 gate run before trusting).
     - **Live-client animation verification** of composites (production
       clip selection + Will's eyes) before/with the preset flip.
     - gles2_composite_audit has an unrelated pre-existing
       device-creation bug (P08d worked around it; small fix someday).
     P08c original verdict for the record:** Head-to-head at gameplay load (befallen replay
     p95 speedup): batch 18.1% / composite 33.1% / lod 35.5% (worst
     visuals — flat silhouettes) / direct-gl BROKEN (garbled textures, no
     perf win — disqualified; genuine bug, undebugged per plan).
     Composite generalizes (56.0% on qeynos2) and — key finding —
     **production already has the full pipeline built and dormant**
     (EntityRenderer::setCompositeEntityPath + entity_composite_builder
     tool + 210 pre-baked sheets at /mnt/projects/EverQuest_ETC1/;
     ConstrainedRendererConfig::enableCompositeEntities defaults false,
     absent from orangepi preset). Landing unit = flip the preset flag +
     verify real assets + Will's quality verdict on PRODUCTION rendering
     (the benchmark's composite stand-in is not representative) + tier-2
     impact. Projected befallen p95 279.7 → ~187ms — real but NOT
     KPI-closing alone; lever #2 still needed after.
  2. **Cross-region zone draw merge — LANDED IN PRODUCTION by P10
     (2026-07-26, fable; flag-gated mergeZoneDraws default OFF +
     /mergedraws toggle; REDUCED shape per plan authorization: merge
     active only when the zone is fully resident, permanent per-visit
     fallback on first eviction — which covers ALL zones today because
     ConstrainedMeshCache is NEVER CONSTRUCTED in production, see new
     item below). In-app A/B on real x86 client vs eqemu: FP-class-only
     diffs (scattered singles, max_delta<=1) at every geometry pose;
     mechanism confirmed active (draw-count INFO line). Bonus fixes:
     /screenshot debug command + EGL-surfaceless capture; a real
     game/render-thread GL-context violation in --script dispatch.
     **CRASH FIXED by P10b — and FLIPPED 2026-07-27 (a8a9b03): Will
     blessed after P11b's full-stack measurement. mergeZoneDraws=true
     on the orangepi preset; the Pi now ships merge + composites
     (befallen avg 25.11ms/39.8fps, p95 35.32ms/28.3fps on the
     canonical replay). The P09→P11b arc is closed.** Real root cause (deferred-loading hypothesis
     falsified): ConstrainedTextureCache eviction under qeynos2's 64MB
     budget left DANGLING ITexture* in region materials because
     clearTextureReferences() only scans the live scene graph while
     manual-draw mode detaches ALL region nodes (1898/1898 detached) —
     **a latent PRODUCTION bug: the unmerged path carried the same
     dangling pointers and escaped only because it touches just the
     visible subset per frame.** Fixed properly: RegionMeshTextureGuard
     eviction listener scans regionMeshNodes_ directly (both paths now
     safe); build() defensive (skip+WARN); merge-activation gate now
     also watches the TEXTURE cache (merge auto-disables in zones that
     evict — qeynos2 provably never activates under the current 64MB
     budget; befallen engages: 10→4 draws, 60% on the sampled view).
     6/6 matrix crash-free; A/B FP-class only (max_delta=1). Bonus
     fixes: dangling softwareCursorTexture_; ParticleManager race
     workaround properly applied (real fix still owed). NEW small
     items: (a) qeynos2 merge coverage requires either a texture-budget
     bump or merge-under-eviction tolerance (only matters if qeynos2
     ever drops below KPI — it is above KPI today); (b) Testchar's
     befallen restore coordinate hits a pre-existing local map/BSP
     FATAL (flagged, unfixed); (c) two pre-existing post-camp
     shutdown-thread aborts (already documented class). Perf case = P09c benchmark numbers (befallen zone-only
     p95 173.8→25.5ms). FUTURE follow-ups: full shared-VBO/eviction
     reconciliation (unit-sized; needed only when lazy streaming gets
     wired); zone objects remain unmerged (inspection-verified).
     P09-series history for the record:** A-gate
     settled the architecture: per-frame client-side-index gather ("the
     1999 way") costs 0.147ms where N=1000 separate VBO draws cost
     59.17ms on Lima (~400x; streamed-IBO/coalescing also fine) — Lima
     dynamic-index handling is NOT pathological. Prototype
     (--merge-zone-draws, default off, in tree) hits P08's ceiling
     exactly (bf472: 1308→14 draws, 98.9%) but pixel-identity FAILS two
     ways: (a) small 2-44px z-fight diffs from intra-run reorder —
     expected class, needs a stable secondary sort key or documented
     tolerance; (b) UNATTRIBUTED texture corruption at golden views
     (up to 31% of frame — textured surfaces render as flat lit
     gradients; ruled out: hash collisions, GL errors, vertex format,
     index rebasing, overflow; leading hypothesis: per-mesh-buffer
     SMaterial-carried shader state, e.g. the MaterialTypeParam
     atlas-page pattern, copied from the run's FIRST buffer only).
     STOP condition honored — Pi perf phase not run.
     **P09b (2026-07-25) root-caused the big corruption: a PRE-EXISTING
     PRODUCTION DRIVER BUG** — COpenGLES2Driver::getOpaqueAlphaFallback()
     lazily binds its 1x1 white fallback WITHOUT glActiveTexture(unit 1)
     first, clobbering unit 0's diffuse texture for the process's first
     no-companion draw (invisible one-buffer glitch unmerged; whole-
     surface corruption when that draw is a coalesced run). Benchmark
     worked around it (first-draw-unmerged + SMaterial::operator==
     grouping); **the driver itself still has the bug — fix it in
     production (tiny: select unit 1 before the lazy-init bind), which
     also removes the benchmark workaround.** Remaining P09b status:
     canonical repro 148k→2k px, but 14/15 poses keep residuals — (a)
     ±1-3px alpha-test edge FP noise from baked-world-space vs
     per-region-matrix transform order (inherent to the design; the fair
     bar is bake-vs-bake, or a one-time visual bless of the FP delta),
     (b) at least one MORE unattributed contributor at qeynos2/befallen
     goldens (max_delta up to 209; suspects named in review: wind-shader
     per-buffer uMeshYBounds packing, atlas unit-1/AtlasSolid3D paths).
     **P09c (2026-07-26) CLOSED the thread: driver fixed in production**
     (glActiveTexture before lazy-init bind; the bug was BAKED INTO 2/9
     goldens — persistent corruption, re-blessed by Will 2026-07-25),
     merge mechanism PROVEN correct (merged vs baked-unmerged:
     max_delta=0 at 13/15 poses, 1 LSB at 2 — narrowed, documented), the
     old "unattributed contributor" fully attributed to the
     architecturally-inherent FP transform-order delta (baked
     world-space vs per-region matrices; up to 44k px / max_delta ≤104;
     contact sheet at benchmarks/contact_sheets/p09c_fp_delta/ — needs
     Will's one-time judgment when scoping the landing), and **the Pi
     numbers are in: befallen zone-only p95 173.8→25.5ms (draws
     253→7.9, 96.9%); befallen combined e205 p95 279.4→65.8ms (-76.4%);
     qeynos2 zone-only 19.3→17.1ms.** KPI: merged+today's-entities
     measured 65.8ms; projected ~44ms (~23fps) stacked with P08c
     composite — merge alone doesn't close 30fps; entity work needed in
     addition. **NEXT: the production landing unit** (sketch drafted in
     P09/P09c reviews: cell-or-zone-scoped baked VBOs, mesh-cache
     eviction keys, per-region lighting/occlusion preserved by the
     gather design, FP-delta bless).
  3. **Occlusion occluder-budget increase** — unchanged; cheap interim.
  4. Within-region merges: DEAD (~0% ceiling).
  - P08b method notes: --animate is nonrepresentative in the benchmark
    (4.5-5x visible inflation — don't use for perf runs); static
    --camera-pose runs don't render entities at all (pre-existing; use
    frame-indexed extraction from replays instead); tier-2's befallen
    39.5ms p95 vs the replay's 279.7ms is pose selection (3 curated views
    vs worst-case-inclusive path), so tier-2 alone understates the KPI
    gap.
- **CMA "leak" OVERTURNED (P03 controlled measurement):** no texture format
  leaks CMA kernel-side (true-leak check with cache drops: all formats
  <=+60kB per run). P02's "reboot-only drain" was PAGE CACHE borrowing
  movable CMA pages while reading pack files — a plain drop_caches restores
  it mid-session (52->102MB observed), and RGBA drained MORE than ETC1
  (proportional to bytes read). The P02 abort protocol reacted to a
  harmless-but-reclaimable condition; telemetry stays (it's how we learned
  all this) but thresholds should key on reclaim behavior, not raw CmaFree.
- ~~v1 loader whole-pack heap loading~~ **FIXED by P04 (2026-07-24)**:
  mmap + lazy referenced-only archive opening. Result: -107 to -110MB
  resident per zone; the historical /pmem "untracked RSS" mystery solved
  (the EQE1 open step's invisible heap was 110-112MB of it; steady
  untracked now ~21-24MB); EQE1 stall bursts eliminated on the FULL 4.7GB
  production dir (8/8 burst-free, p95 spread <=0.27ms); Pi steady RSS
  92-95MB. Bonus fixes: zone archive/wld released post-BSP (-7.5 to
  -12.7MB), ZoneLoadSnapshot lifetime, dead snapshot fields, a pre-existing
  loader-vector data race. Remaining named gap: entityprep bytes accessor
  for the audit line; live willeq zone-in audit pending next server
  session.
- **Format shootout verdict (P03, awaiting Will's decision):** measured
  fairly, ETC1 wins steady-state speed (-3.5 to -12.4% vs RGBA), GPU memory
  (8x), pack bytes, and cold load (NFS-transfer-bound → smallest pack
  fastest). Quality cost is real (29.5dB vs 8888). RGB565/4444 is now a
  fully implemented, measured fallback (+2.3-7.6% vs ETC1, 46.4dB visually
  transparent, no companion machinery). Draft recommendation in
  refactor/P03_payload_format_shootout.md: KEEP ETC1, fix the loader.
- **Bonus from P03:** the EQE1 upload path was silently compiled OUT of x86
  builds (guard widened) — ETC1 packs now work under llvmpipe, enabling
  future ETC1-path goldens/CI coverage on the VM.
- **EQE1-vs-RGBA (P01 correction of the H04 headline):** H04's "EQE1
  consistently slower" was partly a measurement-methodology artifact — its
  harness never varied A/B ordering (pi_bench_run.py), and P01's ABBA runs
  showed strong order/thermal effects. The REAL finding: RGBA p95 is stable
  within 2% across sessions while **EQE1 is erratic (>49% spread,
  30.7-46ms, no order correlation)** — the mystery is EQE1's instability,
  not a uniform slowdown. H1 (H02d fetch asymmetry) DISCONFIRMED; H2
  (filtering) refuted from source; H4 (Lima ETC1 sampling) best-supported
  but unprofiled. P02 re-measures with randomized interleaving + CmaFree
  telemetry (EQE1 variance may be CMA/allocation-correlated).
- **Per-vertex vs per-pixel lighting on real geometry (experiment 6b)** —
  the one P01 experiment that got zero data before the Pi panic; the
  benchmark hard-codes per-pixel (zone_bench_loader.cpp:209). Historical
  synthetic data showed ~3x cost for the model switch. Top priority on
  hardware resume (P02) — point-light COUNT is confirmed free (flat
  34.6-35.3ms across --lighting 0..8).
- **Pi kernel panic under dense benchmark cycling**: died between runs
  (zero GPU activity in the window) after 41 DRM init/teardown cycles in
  ~45min; H04's gentle 4 runs were fine. Consistent with cumulative
  kernel-side GEM/CMA state. P02 protocol: CmaFree sampled between runs
  (downward ratchet = the leak signature), capped runs/session, scripted
  reboots between matrices (fstab now persists NFS), kmsg follower running
  on the VM for lead-up capture; serial console for the trace itself.

## Outskirts anomaly (P05→P07, 2026-07-24) — CLOSED (P07d thread close + Q01 harness alignment); one optional lever left

- **P07 (merged 3c2b801)** adds the hybrid floor Will approved: in area
  mode the nearest `adaptiveLightingAreaPPFloor` regions (default 1)
  always keep per-pixel (their area still accumulates). Contact sheet
  (benchmarks/contact_sheets/p07_pp_floor/) confirms the near-black
  artifact is gone — anomaly/corridor poses byte-identical to count-mode,
  city pairs visually indistinguishable. All software gates green.
- **THREAD CLOSED (P07d, 2026-07-24)** — see docs/decisions.md
  "Zone lighting budget: COUNT mode stays production default" for the
  full chain and rationale. Area+floor flip was REJECTED at golden
  review (near-field blackouts of frame-dominant geometry); radius mode
  proved lossless savings don't exist (player light R=712 units covers
  the zone). Count/area/floor/radius all remain flag-gated; production
  unchanged. task/P07c-default-flip parked unmerged (rejection evidence
  in its contact sheets).
- ~~**Harness alignment (salvaged from P07c, still worth doing):** visual
  gate + Pi tier-2 baseline currently measure FULL per-pixel, not the
  production count-mode adaptive config — add the production flags to
  both, regenerate goldens (count-mode look), re-bless with Will,
  re-capture tier-2. Small unit; fold into the next harness-touching
  batch (path drift is 6 occurrences counting P07d — do record/replay
  first or together).~~ — **DONE by Q01 (2026-07-25, merged in the Q
  batch at `f502298`)**: harness aligned to the production count-mode
  config, goldens regenerated and **blessed by Will** after 3 review
  rounds (final fixture = count mode + Greater Lightstone level 9),
  Pi tier-2 baseline re-captured under the count config (all 6 combos
  faster). The record/replay half landed in the same unit — canonical
  1000-frame paths at `benchmarks/paths/`. Struck 2026-08-02 (stale
  since 2026-07-25).
- Optional future lever (only if lighting cost resurfaces post-P08):
  radius-mode margin<1 perceptual-knee study (mechanism landed in P07d,
  config `adaptiveLightingRadiusMargin`).
- **Pi froze HARD while idle** (2026-07-24 ~12:40 UTC, before P07 ran any
  timed invocation): monitor showed NO error output (Will confirmed) and
  kmsg follower captured no trace — a hard hang, not a panic, so a serial
  console would likely capture nothing either. Distinct from the P01
  panic (which followed 41 dense DRM cycles); this one was idle. If it
  recurs, consider a hardware watchdog (`/dev/watchdog`, sunxi-wdt exists
  on H3) so the Pi self-recovers instead of needing physical power cycles.
- ~~After the Pi numbers: default-flip decision (orangepi preset →
  area+floor), golden re-bless with Will, Pi tier-2 baseline rerun.~~ —
  **MOOT: the area+floor flip was REJECTED at golden review and the
  whole thread closed by P07d** (see the THREAD CLOSED bullet above and
  `docs/decisions.md` "Zone lighting budget: COUNT mode stays production
  default"). Count remains the shipped default; area/floor/radius stay
  flag-gated. The separate radius-mode flip question was also settled
  independently — NOT a candidate (P19, its own section below). Struck
  2026-08-02.

## Outskirts anomaly history (P05→P06, 2026-07-24) — mechanism landed, superseded by P07 above

- P05 root cause (flat region-COUNT budget, renderer_zone_draw.cpp) fixed by
  P06: flag-gated area-mode budget (`adaptiveLightingBudgetMode`, k=0.75),
  merged 23aee56, **default remains `count` everywhere — no behavior
  change yet**. Outskirts poses: area-mode within 0.1-1.6% of
  all-lightweight (2.2-2.6x gap closed). Whole-run: 11.0→12.2 fps.
- **Structural finding gating the default flip (orchestrator analysis of
  P06 CSVs):** area-mode is statistically all-lightweight — whole-run avg
  81.71ms vs all-LW's 81.47ms, median per-frame ratio 1.002, per-pixel
  meaningfully engaged in only 89/1000 frames. Mechanism: the
  camera-containing region nearly always straddles the near plane → the
  conservative fallback counts it as full-screen → budget exhausts at
  region 1. So flipping the default = zone per-pixel lighting effectively
  OFF ~91% of the time (entity per-pixel budget unaffected). Options for
  Will: (a) flip anyway if the visual delta is acceptable (contact sheet),
  (b) refine the estimator first (near-plane AABB clipping, or a
  world-extent discriminator so only genuinely huge regions trigger the
  full-screen fallback — small unit), (c) P05 candidate #2, frame-time-
  driven rolling budget (bigger, most robust). Scene graph exonerated
  (P05 hypothesis B refuted by code audit).
- **Live-game-only follow-up (benchmark-blind):** several per-frame loops
  iterate FULL zone populations regardless of visibility:
  sim_entity_sync.cpp:179, door_manager.cpp:964,
  animated_texture_manager.cpp:383, entity_castbars.cpp:149. Real mechanism
  in code, unquantified — needs a live-client measurement session (tour
  machinery + frame timing) to size before bounding.

## Benchmark camera path — CLOSED by Q01 (2026-07-25, see refactor/Q01_path_replay_harness_align.md)

- `--dump-path`/`--path-file` record/replay landed; canonical 1000-frame
  paths for the three golden zones committed at `benchmarks/paths/`.
  **All future perf-unit moving runs must REPLAY a canonical path** —
  record-vs-replay proven byte-identical on metric columns (x86 R0=R1=R2),
  and geometry columns byte-identical x86↔Pi. Orbit fallback now warns
  loudly + is in the help text. `benchmarks/pi_runs/` unignored (stranded
  P01-P04/H04 CSVs from both slots salvaged into git).
- Residual: entity placement now samples the resolved measured-frame trace
  (not the raw navmesh polyline arc length) — a future `--entities` +
  moving-path run WITHOUT replay places entities differently than the
  P05-P07-era runs did (those were never cross-comparable anyway).

## Radius mode as production default — RESOLVED: NOT a flip candidate (P19, 2026-07-27)

- **P19 re-measured under the FULL final stack** (merge + composite
  scenegraph + --animate + occluder 256 + production config, N=205,
  canonical 1000f replays, both zones, Pi/Mali400): radius is slower
  than count everywhere — befallen +3.3% avg / +6.2% p95, qeynos2
  +1.3% avg / +1.8% p95. Decisive detail: befallen count p95 = 32.39ms
  (under the 33.3ms 30fps line) vs radius p95 = 34.38ms (over it) —
  the flip alone would surrender the KPI that occluder-256 just closed.
  Radius losslessness RE-CONFIRMED under the new stack (x86 pixel spot:
  0/480000 px diff vs full-PP, with and without the stack). Verdict:
  keep count as shipped default; radius stays flag-gated as the
  provably-lossless option. Re-open only if a future unit changes the
  demotion-path cost structure (befallen demoted on 44.3% of frames —
  the per-frame demotion recompute is the plausible overhead locus).
  Full tables in refactor/P19_radius_flip_remeasure.md Review +
  benchmarks/pi_runs/p19_radius_flip_remeasure/.

## Radius mode as production default — original Q03 analysis (2026-07-25, superseded by P19 above)

- Under the honest Greater-Lightstone fixture (Q03; level 9, ×3 boost,
  R=336), radius mode is PROVABLY lossless (pixel-identical to full-PP at
  all 15 x86 poses incl. 6-10 real demotions per pose) while count mode
  provably unlights lit geometry at any view with >20 visible regions
  (7/15 poses, up to 69% px). But radius timed 1.1% (moving) to 8-17%
  (city static) SLOWER than count in Q03's Pi measurement — count buys its
  speed by unlighting reachable geometry. GLS reach tripled radius
  demotions vs the level-10 first pass; a flip unit should re-measure
  timing under the FINAL fixture (Q03 amendment: boost + slot-0 fill were
  added after the Pi timing runs), and per the P07c lesson a flip needs
  its own golden/tier-2 re-bless. Will has conceptually endorsed
  radius-limited pp lighting ("what's stopping efficient light calculation
  in a radius around the player") — the remaining question is purely perf.

## KPI status + the composite measurement gap (P11 synthesis, 2026-07-26) — SUPERSEDED: every bullet here is struck through and closed inline (P14, P11b); see "KPI STATUS — befallen at the 30fps line (P11b, 2026-07-27)" below for the actual resolution

- **Stacked measurement (P11, befallen combined e205 canonical replay)**:
  merge+composite-scenegraph = avg 54.66 / p95 65.93ms (15.2 fps p95) —
  does NOT cross 30fps. BUT the composite half of the stack measured ~0%
  because the benchmark's scenegraph route uses STATIC entities
  (getMeshForRace), and composite meshes are built ONLY on the ANIMATED
  path (getAnimatedMeshForRace → buildCompositeMeshFromGeometry, single
  SMeshBuffer = 1 draw/entity — orchestrator-verified in code).
  So: P08c's -33% (stand-in) unreliable; P11's ~0% (static scenegraph)
  measured the wrong route; **the TRUE production composite magnitude
  has never been measured and is gated on fixing the animated-path
  crumple (P12b)**. Production's flipped flag is directionally real
  (1 draw per animated entity) with unmeasured magnitude.
- ~~**Occluder budget: DEAD lever on befallen** (P11: +0.9-2.7% at 2x and
  uncapped). Production knob exists
  (occlusionMaxOccluderRegions, default 16) — one-line flip if another
  zone ever shows headroom; do not spend more on this.~~ — **REVERSED by
  P14 (2026-07-27)**: P11's verdict was measured PRE-stack. Under the
  merge+composite stack, 16→256 takes befallen whole-run p95 from
  35.320 to 32.313ms — it is the lever that closed the KPI gap. Blessed
  by Will and **shipped** (`9c5617b`, C++ orangepi preset; mirrored into
  `config/constrained_presets.json` by P39). Struck 2026-08-02.
- **Residual to 30fps** (at merge + static-entity dispatch): ~57-60%
  submission (entity draws dominate), ~37-39% GPU sync. The entity
  lever through the REAL animated path is the critical path.
- **--all-zones sweep DONE** (P11 side deliverable): 120 zones, 191 new
  sheets (477 total on NFS), 184s, 97 collisions logged, 39 failures
  all sleeper (DR2_HS_DEF texture won't fit 2048) — audit clean.
- Gate arbitration: P11's slot1 visual gate PASSED → P12's slot2
  zero-source-diff foliage mismatch is environment-specific
  (Xvfb-session suspected), not a gate regression.
- ~~**NEXT: P12b** — fix the animated-path crumple (bind-pose lead in the
  P12 review), un-skip ModelAnimatesProperly, THEN re-measure the
  stacked number through animated composite entities = the real KPI
  verdict. Will's live-animation answer is load-bearing twice over
  (crumple liveness + whether flipped composites render correctly in
  play).~~ — **DONE. Will's gates-video verdict (2026-07-27) settled the
  liveness question: models animate CORRECTLY in the real client, both
  multibuffer and composite → the crumple is benchmark-context-only and
  production animation is exonerated.** P11b then delivered the real
  measurement (p95 −36.9% / avg −54.6% from composites; see the P11b
  KPI section below). The remaining benchmark-side `--animate` garbling
  is tracked as its own root-cause unit, still open. Struck 2026-08-02.

## Live x86 client bugs (anim-video-capture session, 2026-07-26) — PARTIAL: 4 of 5 items closed (marked inline), 1 still open

T64 (2026-08-11) heading note: the missing-equipment-entry item, listed as
still-open below, was ALREADY WARN+skip in `fable` by the time T64 checked
(same wording as the parked branch's fix) — closed, evidence in
`refactor/T64_video_capture_extraction.md`'s Review. That was the last of
the three "capture-blocking workaround" items keeping the branch parked;
all three are now independently resolved in `fable` (saveScreenshot guard
and the particle race were already closed inline below by P23/T53). T64
extracted the `--dump-frames-dir`/`--dump-every` capture flag itself
(`include/client/application.h`/`src/client/application.cpp` only) onto
`fable`, added a `--dump-max-frames` bound and a first-class
`scripts/ci/encode_capture.sh` encode step, and it is now documented in
`CLAUDE.md`. **The rest of `task/anim-video-capture` (6c48bb4, slot2) is
superseded** — the `constrained_presets.json` scratch presets were
session-scoped scaffolding (not needed now that the flag is a supported
tool), and the renderer_frame.cpp/renderer_zone_load.cpp/
irrlicht_renderer.cpp changes are all already in `fable` via better fixes
(P23's real race fix, not the isEnabled() gate workaround). The branch can
be deleted once someone confirms nothing else on it is wanted — not done
here (T64 only extracts, does not clean up branches per protocol).

T53 (2026-08-05) heading note: saveScreenshot guard, the particle race, and
post-camp shutdown aborts are marked FIXED/CLOSED inline below with
commits. Still open: the befallen first-frame SIGSEGV (worked around by
zone substitution only, not root-caused).

Found while recording the P12 animation-evidence videos (real client,
llvmpipe, eqemu). Workarounds live on parked branch
task/anim-video-capture (6c48bb4, slot2) — NOT merged; each needs a
proper fix:
- **befallen SIGSEGVs on the very first rendered frame** on x86 GLES2
  even with vanilla `--constrained orangepi`, no capture flags —
  pre-existing, severe (adjacent to the H02-era blackburrow/crushbone
  blank-render item?). Zone had to be substituted (qeynos2) for capture.
- renderer_zone_load.cpp: one NPC with a missing item_models.json
  equipment entry ABORTS the whole zone load (FATAL) — should be
  WARN+skip. **CLOSED — already WARN+skip in `fable` (T64, 2026-08-11
  confirmed by direct read of `renderer_zone_load.cpp:1614`,
  identical wording/behavior to the parked branch's fix).**
- saveScreenshot() guard — FIXED by P23 (`e702341`, 2026-07-28):
  widened to EQT_HAS_DRM || EQT_HAS_GLES2; x86 GLES2 build now compiles
  the safe glReadPixels path (nm-verified), ARM path byte-identical.
- ParticleManager::renderUnified race — FIXED FOR REAL by P23
  (`e702341`, 2026-07-28): TWO races in SimulationWorker (unsynchronized
  input_ read during computeAll; worker starting a new compute into a
  buffer not yet consumed → swap onto mid-write index). TSAN evidence:
  55 warnings incl. 2 heap-use-after-free before; 0 across 3×100k-iter
  soaks after. Stress tool committed: tools/particle_race_stress.cpp
  (manual, TSAN invocation documented in P23 Review). The P10b-era
  isEnabled() gate on renderUnified REMOVED — NOTE the behavior
  restoration: unified fire/spell particles now render whenever
  zoneReady, independent of atmosphericParticles (they were incorrectly
  suppressed under the shipped default since the workaround landed).
  WATCH ITEM: if any zone's KPI replay contains fire emitters
  (befallen braziers?), the next Pi measurement may show a small
  frame-time delta from particles rendering again — attribute it here,
  don't treat it as a regression mystery.
- Post-camp shutdown aborts — CLOSED by P36 (`4b30404`, merged
  `e8efd87`, 2026-07-29). Root cause: UpdateMovement() ran on TWO
  threads (game thread + the 500ms m_update_thread); when the update
  thread won the race to process the deferred post-logout disconnect,
  CleanupZone → StopUpdateLoop → join() ran ON m_update_thread =
  self-join EDEADLK abort. Fix: update thread no longer calls
  UpdateMovement (game thread already drives it; CombatManager tick
  kept) — self-join structurally impossible. Causality proven by
  revert-and-reproduce. ALSO: SIGTERM had no handler (default-killed;
  now flag + requestQuit). Exit matrix all clean: 5×/camp exit-0
  (2 post-transition) + SIGTERM + script-end + headless.
  Residual small items: window-close exit verified by inspection only
  (no WM in sandbox); action::CommandProcessor's /q is dead code
  (chat-registry /q works) — cleanup candidate.

## Sheet quality DONE + deployed (P16, 2026-07-27)

- UV-inset seam fix (gutters rejected — would have inflated
  power-of-2-tight sheets up to 4x; inset keeps packing byte-identical,
  shrinks JSON uvOffset/uvScale to the safe interior). Tile-cap
  unroll discipline (cap=1, mathematically exact for GL_REPEAT-sampled
  character textures) + a justified one-line backward-compatible
  runtime change (fmod UV wrap in mesh_building.cpp, no-op when
  uncapped). **67.9% aggregate sheet reduction (813→261MB), worst
  outlier 16x smaller, no sheet grew.** Audit 498/498 clean. DEPLOYED
  to live NFS with full dated backup
  (/mnt/projects/EverQuest_ETC1_backup_20260727); post-swap spot
  render verified. NOTE: also cuts the composite share of texture
  memory ~3x — relevant to the cross-zone eviction fix sizing.

## P20 live-visual pair: pop-in RESOLVED (by design), foliage alpha real site found (2026-07-27) — PARTIAL: 3 items closed inline, 3 still open

T53 (2026-08-05) heading note: pop-in (non-bug), foliage alpha (FIXED
P20b, blessed), and door/entity material gap (FIXED P21) are closed
inline below. Still open: RaceModelLoader/equipment_model_loader ignoring
`textureMasked()` (needs its own unit with live visual evidence), the
magenta/black checkerboard placeholder on qeynos2 rooftops (not chased),
and — closed as a non-issue, listed only for completeness — the stale
mergeZoneDraws SIGSEGV retraction.

- **Geometry pop-in: NOT a bug** — controlled same-pose experiment
  (`--zone-load manual` vs automatic) proves it's deferredAssetLoading's
  progressive build; ~1-3s wall-clock fill after arriving in an unbuilt
  area (real-time measurement, not a capture-rate artifact; likely
  LONGER on Pi hardware). Mitigation candidates (proposed only):
  (a) camera-heading/velocity-aware WorkPriorityKey prioritization;
  (b) larger pre-warm halo at zone-in/teleport before releasing the
  loading screen; (c) per-tier deferredAssetLoading knob. Evidence:
  benchmarks/audits/p20_live_visual/EVIDENCE_MANIFEST.txt.
- **Foliage alpha FIXED by P20b (`ece11f0`), BLESSED by Will
  2026-07-27** (same-pose evidence, qeynos2 gates + gfaydark nekpine;
  bless page on the standing artifact). Historical diagnosis:
  AnimatedTreeManager is DEAD CODE on the GLES2 client (only runs under
  EDT_BURNINGSVIDEO — loadZoneStep11_Env gate); on the real GPU path
  trees build correctly via ZoneMeshBuilder::buildTexturedMesh
  (H02d-correct) and then a WIND-SHADER pass UNCONDITIONALLY CLOBBERS
  MaterialType (`renderer_zone_stream.cpp:559-576`,
  `renderer_zone_load.cpp:2288-2334`) with no masked/ETC1-vs-RGBA
  classification → companion variant with unbound unit 1 → opaque-white
  fallback → solid quads. Ground truth: nekpine.bmp is WLD
  TransparentMasked with REAL DXT5 alpha (47/53% hard cutout) — NOT
  citywlivy-class. P20's ed310e1 tree-path change is a correct H02d
  mirror but dormant on GLES2.
- **Door/entity setShaderMaterialTypes gap — FIXED by P21 (`6bde3e9`,
  2026-07-28)**: 2→3-arg RGBA variant plumbed through door_manager +
  entity_renderer + all call sites. REAL for doors: PAWHOOK chain doors
  (pchain.bmp, 6 placed instances in qeynos2; masked, RGBA-decoded)
  previously got EMT_TRANSPARENT_ALPHA_CHANNEL_REF=14 — NOT one of the
  GLES2 driver's compiled programs — now get the compiled RGBA
  alpha-test shader (258); numeric MaterialType trace is the evidence
  (door too small for salient stills, honestly noted). Entity side
  LATENT: correctly wired but currently unreachable, because of the
  next item.
- **NEW (P21 audit): RaceModelLoader/equipment_model_loader IGNORE
  textureMasked()** — `mesh_building.cpp` (both buildMeshFromGeometry
  AND buildCompositeMeshFromGeometry) and `equipment_model_loader.cpp`
  hardcode EMT_SOLID for every material, never consulting the WLD
  masked flag. Masked character-archive textures exist (zomch0001/
  zomlg0001 befallen zombies, beahe0008 qeynos2) and render OPAQUE
  today via this upstream gap. If/when fixed, P21's classification in
  entity_renderer is already wired to route them correctly. Needs its
  own unit with live visual evidence (zombie flesh gaps / bone helm
  holes are era-authentic transparency candidates) + composite-path
  care (composite meshes share one buffer — per-material alpha-test
  may not compose; check before promising).
- **New unrelated bug spotted**: magenta/black checkerboard
  missing-texture placeholder on some qeynos2 rooftops/awnings
  (approach_01/02 stills). Not chased.
- **Stale-crash retraction for the record**: the P08d-retake qeynos2
  first-frame mergeZoneDraws SIGSEGV does NOT reproduce on current fable
  (direct observation, stock orangepi preset, clean session +
  screenshots) — consistent with P10b's fix. No capture preset
  workaround needed anymore.

## Eviction hygiene LANDED (P17, 2026-07-27) — closes the P15 follow-ups

- **Cross-zone accumulation FIXED, mechanism deeper than P15 thought**:
  ConstrainedTextureCache::clear() existed and was correct, but its sole
  trigger — the queued "/unloadzone_internal" RendererCommand pushed by
  CleanupZone() — is only drained by PreRenderTick(), which
  Application::render() SKIPS on every early-return branch taken during
  a zone transition (handshake + loading-thread-active). The unload sat
  unapplied until after the NEW zone finished loading. Fix: unloadZone()
  now runs synchronously in Application::loadZoneGraphicsOnThread() (on
  the loading thread, which owns the GL context) before the new zone
  uploads anything; the queued push is removed (late-firing, it would
  have wiped the NEW zone's textures). PROOF: 5-hop multi-hop session
  (befallen↔qeynos2×2 + gfaydark) with per-zone ledger reset — eviction
  counts flat and zone-local (befallen 0, qeynos2 30, every visit), no
  accumulation.
- **Entity-node dangling-ITexture* guard LANDED** (P15 bug (a)):
  EntityMeshTextureGuard mirrors P10b's RegionMeshTextureGuard, covers
  EQAnimatedMeshSceneNode materials_ + instanceMesh_. Verification also
  surfaced a SECOND dangling-pointer hole: RaceModelLoader's shared
  per-race mesh cache's own ZoneMeshBuilder::textureCache_ was never
  cleared on zone unload (clearTextureCache() had zero callers) — wired
  into clearMeshCaches() + defensive isLiveTexture() at the crash site.
- **Stale-zone-name-vs-position snapshot race FIXED** (P15 bug (b)):
  SetZoneBspTree() tags the tree with its zone; CreateZoneLoadSnapshot()
  only trusts a tag-matching tree, else falls back to the safe
  no-tree-yet path instead of the SIZE_MAX FATAL.
- mergeZoneDraws spot: qeynos2 still correctly gated OFF — its own
  standalone footprint exceeds the 64MB budget (not cross-zone leakage;
  the P13b "would merge ACTIVATE in qeynos2 post-fix" hypothesis is
  answered: no, qeynos2 is genuinely over budget on its own).
- **NEW (unfixed, pre-existing, graceful)**: gfaydark zone-in FATAL —
  missing race model data, race=330; unrelated to eviction work, needs
  its own look (likely race_codes.cpp mapping gap class). T53
  (2026-08-05): possibly the same class as the still-open `race=415`
  missing-model FATAL confirmed by T32's 2026-08-03 re-test elsewhere in
  this file — not verified as the same root cause, flagging the
  connection for whoever picks either up.

## Eviction mystery SOLVED + re-zone crash fixed (P15, 2026-07-27) — follow-ups CLOSED by P17 above

- **Re-zone snapshot crash FIXED**: ProcessDeferredZoneChange signaled
  the zone load AFTER CleanupZone cleared m_current_zone_name → empty
  snapshot zoneName → loadZoneGraphicsOnThread FATAL. Path shared by GM
  #zone AND real zone-line crossings. Fix: OnGameStateComplete is now
  the sole re-zone trigger (matches initial zone-in). 8 crash-free
  teleport sessions + one full clean befallen→qeynos2 transition.
- **EVICTION MYSTERY SOLVED — REPRODUCED**: cross-zone accumulation.
  ConstrainedTextureCache (and the ledger) is NEVER reset across zone
  unloads — befallen→qeynos2 in one process = 27 evictions (all
  zone_region, cumulative uploads 817). P10b's 71-eviction scenario =
  a longer multi-zone session. **FOLLOW-UP UNIT NEEDED: release the
  outgoing zone's textures at zone unload** (the 1999-honest behavior;
  also shrinks the merge-activation gate's false trips).
- **TWO NEW BUGS found (documented, unfixed, blocked the 3+ hop
  verification)**: (a) intermittent dangling-ITexture* in ENTITY mesh
  materials post-eviction — the P10b RegionMeshTextureGuard protects
  REGION nodes only; entity nodes have the same hazard class and need
  the same guard; (b) a stale-zone-name-vs-position snapshot race
  (separate from the fixed bug).
- mergeZoneDraws regression spot: clean (matches P10b behavior).

## KPI STATUS — befallen at the 30fps line (P11b, 2026-07-27)

- **The true composite magnitude (animated production path, Pi,
  canonical befallen e205 replay): p95 −36.9%, avg −54.6%, entity
  draws/entity −66%.** Retires P08c's −33.1% (stand-in path, own cull
  bug, not a real dispatch) and P11's ~0% (static route — composite
  meshes only build under animation).
- **Full stack (merge + animated composites): avg 25.11ms = 39.8 fps
  (8.2ms UNDER budget — first time in the campaign); p95 35.32ms =
  28.3 fps (2.02ms over).**
- **Residual is now GPU-BOUND**: at full-stack p95, GPU sync 17.5ms
  (49.6%) vs submission 9.3ms (26.5%) — a reversal of every prior
  unit's CPU-submission-dominant finding. The p95 tail is driven by
  bf472's dense zone-geometry pose specifically.
- **P14 (2026-07-27) MEASURED THE OCCLUDER LEVER UNDER THE STACK — IT
  CLOSES THE GAP**: occlusionMaxOccluderRegions 16→256 takes whole-run
  p95 from 35.320ms to **32.313ms (0.99ms UNDER the 30fps line)**;
  uncapped ties 256 (saturated). bf472: total −10.6%, GPU-sync −9.5%,
  submission −23.1%, CPU occlusion cost +0.44ms. Reverses P11's
  pre-stack "not worth it" verdict. LANDED 2026-07-27 (blessed by Will;
  note: the C++ preset default was the real landing site — no JSON
  plumbing exists for this key, contrary to P14's claim). With it: befallen avg 39.8fps / p95 30.9fps —
  **the 30fps KPI fully met at p95 for the first time.**
- Calibration notes: N=205 → median 38 visible holds on the animated
  path (P08b's 4.5-5x inflation bug is gone post-P12b); watch the
  --frames-vs-path-length placement-density artifact documented in
  P11b's review.
- ~~NOTE: achieving these numbers in live play requires the
  mergeZoneDraws preset flip (composites already live).~~ — **FLIP
  LANDED 2026-07-27 (`a8a9b03`)**: `mergeZoneDraws: true` is in the
  shipped `orangepi` block of `config/constrained_presets.json`
  (verified in tree 2026-08-02), so live play gets the full measured
  stack — merge + composites + occluder-256. Struck 2026-08-02.
- CAUTION (P51, 2026-08-01): the p95 numbers in this section come from
  pre-`4386398` binaries and carry the fixed-function-entity benchmark
  bias. **34.3-34.6ms is the canonical KPI-guard band now**, not the
  32.24-32.5 figures above. Kept as the historical record of the arc.

## ConstrainedMeshCache is never constructed — RESOLVED BY REMOVAL (T67, 2026-08-11)

- Exhaustive grep (verified independently by orchestrator): no
  construction site exists for ConstrainedMeshCache anywhere in src/ —
  the lazy/progressive region-mesh streaming documented in
  src/client/graphics/CLAUDE.md ("mesh cache active → texture pixel
  data retained", 24MB budget, eviction) is NOT wired into production;
  regions are always built eagerly. Docs overstate reality; either wire
  it (with the P10 full shared-VBO reconciliation) or trim the docs and
  the dead conditional paths (renderer_zone_stream.cpp/renderer_zone_draw
  guard on a pointer that is never non-null).
- **T67 (Will's decision, 2026-08-11): removed the class from production**
  rather than wiring it — the eviction pressure it exists to relieve was
  never reproduced beyond one unconfirmed observation (P10b's "71
  evictions", vs zero across four later independently-measured
  configurations), it had been independently written twice (this class,
  plus once more on the deleted `mem` branch) and wired zero times, and
  there was no way to even turn it on (`meshMemoryBytes` was computed per
  preset and consumed by nothing). `constrained_mesh_cache.h`/`.cpp`
  (385 lines) deleted outright, along with ~40 guarded call sites and an
  entire background-mesh-build subsystem that existed only to feed it
  (see `refactor/T67_remove_constrained_mesh_cache.md`'s Review for the
  full per-file accounting). The one genuinely-live piece,
  `estimateMeshSize()`, was relocated verbatim to
  `estimateMeshSizeBytes()` in `include/client/graphics/eq/zone_geometry.h`
  / `.cpp` rather than deleted. Full protocol verification bar passed,
  including the visual gate (byte-identical rendering — this was
  dead-code removal, not a behavior change) and both x86/ARM builds.
- **This closes the item WITHOUT answering whether progressive
  region-mesh streaming is ever wanted.** If Pi memory pressure is later
  actually measured (it has not been, across five independent sessions'
  worth of attempts), the implementation is fully recoverable, not
  reimplemented from scratch: last shipped at commit `49844c72`
  (2026-03-08, "PVS direct index lookup, SimWorker copy elimination, DRM
  page flip fix, stderr buffering"), `include/client/graphics/
  constrained_mesh_cache.h` + `src/client/graphics/constrained_mesh_cache.cpp`
  — `git show 49844c72:src/client/graphics/constrained_mesh_cache.cpp`
  retrieves it in one command (this is `fable`'s own history, not the
  deleted `mem` branch — permanently recoverable, no archive tag needed).

## P10b's "71 evictions" scenario still unreproduced; new zone-transition crash found (P13b, 2026-07-27)

- P13's own audit (three measurement runs) and P13b's bounded side-quest (two
  more, ledger-instrumented) — four independent single/multi-zone qeynos2
  configurations total across two units — have now all measured **0
  evictions** under the shipped 64MB `orangepi` budget, whether composites
  are on/off, `mergeZoneDraws` is on/off, or a shipped vs custom-mirrored
  preset is used. P10b's original "71 evictions" (befallen→qeynos zone
  transition, mergeZoneDraws crash repro session) remains the only recorded
  observation of it.
- P13b's multi-zone-in-one-process attempt (befallen → `#zone qeynos2` GM
  teleport, no teardown — the leading hypothesis per P13's own Deviations:
  "evictions accumulating across MULTIPLE zone visits in one process
  lifetime") hit a **new, reproducible crash** before it could observe
  qeynos2's cumulative eviction count:
  `[FATAL] [GRAPHICS] loadZoneGraphicsOnThread: snapshot has empty zone name
  — aborting` (SIGSEGV), triggered specifically by the `#zone <shortname>`
  GM-command zone-transition path. Not investigated (out of P13b's bounded
  side-quest scope) — this is now the concrete blocker for whoever next
  attempts the multi-zone-accumulation repro: either fix this crash, or find
  a different zone-transition mechanism (a real client-walked zone line
  instead of the GM teleport command) that doesn't hit it.
- The eviction-tracing ledger machinery (P13's `TextureUploadLedger`) is
  fully built, verified working, and ready to capture the exact
  victim-order/thrash reconstruction the moment ANY future session actually
  reproduces eviction pressure — nothing further needs building, only a
  session shape that reaches it.
- **CLOSED by P15 (2026-07-27)**: the `#zone` empty-zone-name crash above is
  fixed (`ProcessDeferredZoneChange` in `src/client/eq_zone.cpp` no longer
  sets `m_zone_load_requested` immediately after `CleanupZone()` clears
  `m_current_zone_name` — that premature signal raced the game thread's
  snapshot creation ahead of the new zone's actual data arriving via
  `OnGameStateComplete()`, which is now the sole trigger for both initial
  zone-in and every re-zone, GM-teleport or zone-line alike). And the
  eviction mystery is **answered**: cross-zone texture accumulation
  reproduces evictions — a `befallen` (safe spot) → `#zone qeynos2` session
  hit 27 evictions (all `category=zone_region`, i.e. `befallen`'s now-stale
  geometry textures evicted to make room for `qeynos2`'s), cumulative
  uploads=817, 0 thrash. Root mechanism: `ConstrainedTextureCache` /
  `TextureUploadLedger` are never reset on zone unload (persist for the
  process lifetime), so a second zone's uploads share the first zone's
  budget headroom. Full ledger + condensed console evidence in
  `benchmarks/audits/p15_eviction/`. See `refactor/P15_zone_crash_eviction.md`
  Review for the complete writeup, including two NEW, separate, NOT-fixed
  bugs uncovered while verifying (both out of P15's scope, flagged below).

## Entity mesh materials hold dangling ITexture* after eviction — FIXED by P17 (2026-07-27)

- **CLOSED.** P17 landed BOTH halves: (a) `EntityMeshTextureGuard`
  (`renderer_zone_merge.h/.cpp`, the entity sibling of P10b's
  `RegionMeshTextureGuard`, owned by `IrrlichtRenderer` as
  `entityMeshTextureGuard_`) covering `EQAnimatedMeshSceneNode`'s
  per-instance `materials_`/`instanceMesh_`; and (b) a defensive
  `isLiveTexture(currentTex)` check at the exact call site named below
  (`animated_mesh_creation.cpp`, now ~line 456) — verified present in
  tree 2026-08-02. The guard alone was NOT sufficient and P17 says so:
  `buffer` there is the race's SHARED RaceModelLoader-cached base mesh,
  a different object set than the guard covers, and it can go dangling
  via the zone-unload full `clear()` even with zero LRU evictions —
  which is exactly why the original report saw crashes at a 256MB
  no-eviction budget. Live repro (befallen → qeynos2 → befallen 2nd
  visit) confirmed the mechanism before the fix. Struck 2026-08-02.
  Original report follows for the record:

- `RaceModelLoader::createAnimatedNodeWithEquipment` (`src/client/graphics/eq/animated_mesh_creation.cpp:433`)
  reads `currentTex->getName()` off an `ITexture*` cached on a mesh buffer's
  material without checking whether `ConstrainedTextureCache` has since
  evicted (and thus destroyed) that texture. Intermittent SIGSEGV/SIGABRT
  (heap-corruption-class, not always reproducible — sometimes the freed
  memory hasn't been reused yet) during entity mesh building whenever
  eviction pressure hits while building a populated zone's entities.
  Confirmed independent of eviction volume — a 256MB no-eviction
  `--constrained` budget still crashed at the same call site, so this is a
  general dangling-reference bug, not strictly gated on eviction. **Region**
  meshes already have the equivalent protection (P10b's
  `RegionMeshTextureGuard`, confirmed live in this unit's regression-spot
  log: `"RegionMeshTextureGuard: swapped N region-mesh material
  reference(s) to evicted texture -> placeholder"`) — entity meshes have no
  such guard. Shape of the fix: extend an equivalent guard/listener to
  `EntityRenderer`/`RaceModelLoader`'s cached `ITexture*` references, or
  have `ConstrainedTextureCache::evictTexture()` null out entity-held
  references the same way it already does for region meshes. Not attempted
  here (P15 is scoped to the `#zone` snapshot crash + eviction *recording*,
  not eviction fixes) — flagged for whoever picks up the eviction fix, since
  it's the direct answer to "why does eviction matter beyond a budget
  number."

## "Player BSP region is SIZE_MAX" with stale zone name (found by P15, NOT chased)

- Separately from the above, one `#zone` verification run hit `[FATAL]
  [MAIN] Player BSP region is SIZE_MAX! pos=(428.00,-74.00,3.00)
  zone=befallen` — i.e. the *target* zone's (qeynos2) coordinates paired
  with the *source* zone's name/BSP tree still resident, inside
  `CreateZoneLoadSnapshot()`. This is a different inconsistent-snapshot
  timing window than the one P15 fixed (that one was empty-name; this one
  is stale-name-with-new-position) and was not root-caused — reproduced at
  higher debug levels (`-d 5`) and under `gdb`, not reliably at `-d 3`,
  suggesting a timing-sensitive race in how position vs. zone-name updates
  land relative to snapshot creation across the `#zone`/re-zone sequence.
  Out of P15's scope (the assigned crash's signature — empty zone name —
  never recurred once fixed); flagged for whoever next touches zone-load
  snapshot timing. Per this same plan's own precedent (the pre-existing
  BSP-FATAL at Testchar's documented restore coordinate), not chased
  further here.

## Pale fog-colored flat shapes in gfaydark/qeynos2 goldens (H02c/H02d observation)

- Distant geometry renders as flat fog-colored (pale) shapes against the
  darker sky — visually jarring; likely fog color/sky color mismatch or fog
  applied to geometry but not sky. Pre-existing (byte-identical across the
  alpha fixes); visible in gfaydark goldens. Investigate fog pipeline vs
  zone fog color data. Will's eye flagged the general area; needs its own
  unit.

## Production draw-order nondeterminism — CLOSED by Q02 (2026-07-25, see refactor/Q02_draw_order_key.md)

- Both pointer-key sites in renderer_zone_draw.cpp now use the H02b
  FNV-1a texture-name hash (`textureGroupId()`, byte-for-byte mirror of
  zone_bench_loader.cpp's — keep the two copies in sync). Straggler sweep
  confirmed no other pointer-derived draw/sort keys in src/ or include/.
  Unblocks the future full-app llvmpipe golden tier.

## RDP frame-update race — ROOT-CAUSED (T03b) — **FIXED by T35 (2026-08-04)**

**CLOSED, T53 (2026-08-05) verification.** This entry's own title said "fix
still open" but the fix landed and the entry was never updated.
`RDPServer::stop()` (`src/client/graphics/rdp/rdp_server.cpp:325-342`) now
does `running_.store(false)` → `listenerThread_.join()` → `Close()`/`free()`
— join-before-close, exactly T03b's prescribed ordering. Commit `a835969`
("Merge T35: fix the RDPServer::stop() use-after-free, retire its waiver").
Per `refactor/EXECUTOR_PROTOCOL.md`'s verification-bar note, the
`RDPServerFrameTest.UpdateFrameMultipleTimes` "intermittent" ctest waiver
was formally retired in the same unit — a real crash, not a flake, as this
board warned. See `refactor/T35_rdp_stop_uaf.md` Review for the full
repro/verification evidence. Original report follows for the record:

- **RDPServerFrameTest.\* segfaults intermittently** (~once per <=10
  stress repetitions, confirmed on unmodified fable during F18
  integration; also seen under parallel ctest load in F01/F09/F24 runs).
  A real race in the RDP path, not test noise.
- **ROOT CAUSE FOUND — T03b (2026-07-28), handed to the perf session in
  `refactor/TESTING_HANDBACK.md` Round 3. Do not re-investigate from
  scratch.** It is a **use-after-free in `RDPServer::stop()`**: `Close()`
  runs before `join()` while `listenerThread()` is still holding fetched
  handles. Evidence was 12 kernel GP faults at an identical libwinpr3
  offset (the symbol resolution was double-checked against
  `readelf -lW` — the kernel reports the containing segment base, and
  libwinpr3's executable LOAD starts at vaddr `0x48000`). The correct
  ordering is `running_.store(false)` → `join()` → `Close()` → `free()`.
- **Still reproduces at fable tip (2026-07-31, testing session):**
  1-in-5 standalone in one worktree (`UpdateFrameAfterStart`) and under
  parallel ctest load in another (`UpdateFrameMultipleTimes`). Which
  family member fires varies with timing — treat the whole
  `RDPServerFrameTest`/`RDPServerLifecycleTest` set as one flake family
  until the ordering is fixed.
- **Standing caution (T03b lesson):** this sat behind a "known
  intermittent flake" waiver for weeks. A segfault is never a flake; it
  is a crash with a timing precondition.

## TWO `state::GameState` instances exist, unconnected — the structural cause of the whole dead-state bug class (T39, 2026-08-04) — NOT FIXED, needs a ruling

**This is the root the entry below was a symptom of.** T39 found it while
looking for where to wire `CombatState`, and it invalidated the wiring
site the plan had suggested.

- `EverQuest::m_game_state` — a plain value member (`eq.h`).
- `Application::m_gameState` — a **separately heap-allocated**
  `std::unique_ptr<state::GameState>` (`application.cpp:179`).

They are distinct objects, each auto-wiring its own submodules to **its
own `EventBus`** (`game_state.cpp`). **Nothing connects them** —
orchestrator-confirmed: `application.cpp` and the game modes never call
`EverQuest::GetGameState()`.

Who reads which:

| instance | consumers |
|---|---|
| `Application::m_gameState` | `ActionDispatcher`, `CommandProcessor` (incl. `cmdHail`), the game mode → `ConsoleRenderer`, `InputActionBridge` |
| `EverQuest::m_game_state` | the `eq_commands.cpp` readers, the Phase 7.x `sync*State()` calls inside `EverQuest` |

So "populate the state layer" is ambiguous until you say *which* state
layer — and populating the wrong one produces exactly the observed
symptom: setters that run, state that looks populated, and readers that
still see nothing. That is the `/assist` bug (T28) and the `cmdHail` gap
(T39) in one sentence, and it is why those two readers behaved
differently despite reading "the same" field.

T39 worked with it rather than against it (wired via
`Application::syncCombatState()`/`syncInventoryState()`, mirroring the
existing `syncPetState()`/`syncNPCInteractionState()` polling pattern),
which was the right call for a unit that was not scoped to re-architect
this. But the duplication remains, and it will keep generating this bug
class every time someone reasonably assumes there is one game state.

**Needs a decision, not a drive-by fix.** Options, roughly ascending:
(a) leave both, document loudly at both declarations so the next person
does not assume; (b) make `Application` hold a reference to `EverQuest`'s
instance (or vice versa) so there is one object; (c) collapse to one
owner properly, which touches the action layer, the modes, the bridges
and the Phase 7.x syncs. (b) sounds small and is not: the two are
constructed at different times with different lifetimes, and the
headless/graphical modes differ in what exists when.

**T78 (2026-08-12) ANALYSIS DONE — read `refactor/T78_two_gamestate_analysis.md`'s
Review before scoping the next unit off this entry.** Full census of both
instances (every construct/hold/read/write site, file:line), what each
genuinely serves, the four options with costs/risks/unblocks, and — the
finding that changes this entry's shape — **neither instance is vestigial,
so "delete one" is not available**, and the split currently breaks more than
the position-write case this entry and T55/T58 described: `Application`'s
copy is never fed `entities()`/`doors()`/`group()` at all, so `/face`,
`/who`, `moveToEntity`/`followEntity`, `/disband`, and dispatcher-gated
`/decline`/accept-invite all silently no-op or return a misleading "not
found" in production today, independent of whether the two instances are
ever connected. Doors are broken worse than a two-instance split — a third,
unconnected `DoorStateManager` on `EverQuest` holds the only real door data,
and neither `GameState`'s `doors()` is ever populated by anyone. T78's own
recommendation: don't pick (a)/(b)/(c) wholesale — close the entities/group
gaps cheaply via a `GameStateSyncBridge` extension, fix doors as its own
unrelated item, and scope full convergence (b)/(c) as a separate, larger,
deliberately-scoped unit given its ~90+ call-site regression surface. Still
Will's decision, not foreclosed by this note.

**T81 (2026-08-12) ENTITIES CLOSED — first category of T78's recommended
`GameStateSyncBridge` extension.** `Application::m_gameState->entities()` is
now fed for real: `EntityStateSnapshot` (new, full `state::Entity`, published
from `EverQuest::SyncEntityToGameState()`, add-or-replace) for spawn/self;
existing `EntityDespawned` for despawn; existing coalesced `EntityMoved` for
position (no per-tick full snapshot — this stayed the volume-bounded path
per D24); existing `EntityAppearanceChanged` for post-spawn equipment
changes (`ZoneProcessWearChange()` mutates equipment without re-invoking
`SyncEntityToGameState()`, so a spawn-only snapshot would go stale — see
`refactor/T81_entity_projection.md` Review); existing `ZoneUnloading` to
clear the projection on zone change (the source-side clear,
`eq_zone.cpp:270`, only fires events on `EverQuest::m_game_state`'s own
private bus, which nothing production reads, so it never reached this
decorator any other way). `/face`, `/who`, `moveToEntity`/`followEntity`,
and the NPC half of `interactNearest()` now work against real data with
**zero changes to `ActionDispatcher`/`CommandProcessor`** — confirming this
was a projection gap, not a deeper bug. Still open from T78's list: **doors**
(worse than a two-instance split — a third, unconnected `DoorStateManager`
holds the only real data, per T78) and **group** (both need their own
reconstructibility argument, not assumed from this unit's), plus **combat**,
which stays on its existing per-tick `Application::syncCombatState()` poll
(T78 confirmed populated, not broken — not touched by this unit).

**Live leg CLOSED (T82, 2026-08-12), with an important correction to the
paragraph above.** `/face` was confirmed live against T81's projection
exactly as claimed — `ActionDispatcher::faceEntity()` reads
`m_state.entities()` directly and succeeded live with zero code changes.
**`/who`, `moveToEntity`, and `followEntity` do NOT "work against real
data" in the sense this paragraph implies — they are unreachable through
any live input path.** `Application::DispatchSlashCommand()`'s registry-1
stage (`EverQuest::m_command_registry`) always runs first, and `who`/
`moveto`/`follow` are ALL also registered there under the identical name,
reading `EverQuest::m_entities` (a separate, pre-existing, never-broken raw
map) — so `CommandProcessor`'s versions (the ones actually reading T81's
projection) are permanently shadowed, confirmed live by the exact output
text observed (registry 1's `"[{}] {} (Lvl {})"` / `"Moving to entity:
..."` / `"Following: ..."` format, never registry 2's distinct text). This
is not a T81 defect — T81's own scope was the projection and its consumers,
which are correctly wired and unit-tested; the shadowing is a pre-existing
command-dispatch property neither T81 nor this unit touched. `/list` looks
like a fourth live proof (not shadowed, dispatches to `registry=action`)
but also does not exercise the projection: `ActionDispatcher::
listEntities()` forwards cross-thread to `EverQuest::ListEntities()`, the
same raw-map path as `/who`. Full evidence:
`refactor/T82_live_verification_session.md`'s Review, `review/T82/`.
`interactNearest()` was not exercised (no slash-command binding at all,
hotkey-only — same reachability class, one level further).

## Dead parallel state copies in the state layer, WITH LIVE READERS (orchestrator, 2026-08-02) — **ALL THREE CLAIMS CLOSED (T28/T39/T42/T49); this is the board's own worked example of why re-verification matters**

**CLOSED, T53 (2026-08-05) verification** (independently re-confirmed, not
just taken from the T53 plan's own framing):
1. `CombatState`'s target half — **populated**, confirmed via
   `Application::syncCombatState()` at `include/client/application.h:354`
   / `src/client/application.cpp:1785`; `world_state.cpp`'s sibling fields
   confirm the general pattern.
2. `InventoryState` occupancy — **wired**: `setEquipmentOccupied()`/
   `setGeneralOccupied()`/`setBankOccupied()` now have real production call
   sites at `src/client/bridge/game_state_sync_bridge.cpp:139-143`.
3. `WorldState::fireZoneChangedEvent()`'s pending-zone fields — **fixed**:
   `src/client/state/world_state.cpp:25-28,117-118` now uses
   `m_zoneArrivalX/Y/Z/Heading`, matching the fix this entry describes.

All three sub-claims hold up under direct grep, exactly as this entry
originally reported. No correction needed to the entry itself — it was
already accurate and evidenced. Kept as CLOSED, not deleted, per this
board's own governing rule.

Found while scoping a fix for T02's three pinned state-layer findings.
The T02 pins are accurate about what they assert, but the framing
("these events don't publish") understates and partly misdirects: for
two of the three, the *event* is not the problem — the state those
events would describe is never populated in the first place, and the
same dead state has live readers. All verified by grep against fable
tip `557a488`; commands in this entry, re-runnable.

**1. `state::CombatState`'s target half is never populated — and four
slash commands read it.**
- `combat().setTarget()` / `clearTarget()` / `setAutoAttacking()` /
  `updateTargetHP()` have **zero production call sites** (only tests
  call them). So `fireTargetChangedEvent()` being an empty stub is
  moot — the function it stubs is unreachable in production anyway.
- The authoritative target is `CombatManager::m_target_id`
  (`GetTargetId()`/`SetTarget()`), which is what `eq_bridge.cpp` uses
  throughout (~lines 763, 832, 906, 958, 1252, 1292, 1373).
- **But five production sites read the dead `CombatState` copy**:
  `eq_commands.cpp:887, 945, 974, 1020` (`combat().targetId()`) and
  `action/command_processor.cpp:771` (`combat().hasTarget()`).
  `eq_commands.cpp:887` is `/assist` with no args: it reads
  `targetId()`, gets 0 because nothing ever sets it, and prints
  **"You don't have a target."** even when the player does. The other
  three read sites are the same class.
- `TargetChanged` is meanwhile **already published** from three real
  sites (`eq_bridge.cpp:589` clear, `eq_bridge.cpp:771` set,
  `eq_entities.cpp:1728` HP update), and `eq_action_handler.cpp:21`
  documents a deliberate no-duplicate decision ("D20e1: TargetChanged
  event is already published by ProcessBridgeIntents"). ~~So naively
  making the stub fire — after wiring the setters — would DOUBLE-publish
  it.~~ **CORRECTION (orchestrator, 2026-08-04): that double-publish
  claim was WRONG, and I wrote it without checking. The two paths are
  DIFFERENT CHANNELS with DISJOINT CONSUMERS**, verified:
  - those three sites `m_bridge->pushEvent(...)` onto the
    **GameStateBridge**, consumed by `IrrlichtBridge` (renderer) and
    `ConsoleBridge`;
  - `CombatState::fireTargetChangedEvent()` would `m_eventBus->publish()`
    onto the **EventBus**, consumed by `ConsoleRenderer`
    (`console_renderer.cpp:107`, live in headless mode);
  - **nothing else publishes `TargetChanged` to the EventBus at all**,
    the bridges do not `subscribe()` to the EventBus, and no code
    forwards between the two. The EventBus is fed exclusively by
    `src/client/state/*.cpp`.

  So letting the stub publish is safe, and is in fact what every other
  state module already does. `eq_action_handler.cpp:21`'s no-duplicate
  note is about the bridge channel and does not apply. The migration is
  correspondingly smaller than this entry originally implied — it is
  "populate the state and let it publish", not "consolidate three
  publishers onto one".

  The stale source comment in `combat_state.cpp:68` claiming "we
  don't have a corresponding data struct" is simply wrong:
  `TargetChangedData` exists at `event_bus.h:384`.

**2. `state::InventoryState`'s occupancy half: same pattern.**
`setEquipmentOccupied()` / `setGeneralOccupied()` / `setBankOccupied()`
have zero production call sites, so the hardcoded
`event.hasItem = true; event.itemId = 0` at
`inventory_state.cpp:204-205` is real but unreachable. (The
*live* `InventorySlotChanged` events, enriched with display fields by
S2, come from the `eq_bridge` path, not from here.) No live readers
found for the occupancy arrays — the milder case.

**3. `state::WorldState::fireZoneChangedEvent()` IS live, and its
position is wrong.** Unlike 1 and 2, `world().setZone()` is genuinely
called in production (`eq.cpp:1909`). It publishes `ZoneChanged` on the
**EventBus** with `x/y/z/heading` read from the *pending-zone* fields
(`world_state.cpp:110-113`) rather than the actual arrival position.
Immediately afterward, `eq.cpp:1912-1922` pushes its OWN `ZoneChanged`
onto the **bridge** with the correct `m_safe_x/y/z`. Two channels, two
different payloads: `irrlicht_bridge`/`console_bridge` get the correct
one; the EventBus subscriber (`console_renderer.cpp:102,199`) gets
position `(0,0,0)` whenever `setZone()` runs without matching pending
fields. This one is a genuine live bug and matches its T02 pin exactly.

**Why this matters beyond the three pins**: the "Phase 7.x sync to
XState" comments show the intent was to migrate game state INTO the
state layer, and that migration was left half-done — setters written,
never wired, while readers were repointed at the new home anyway. The
`/assist` breakage is the visible cost. Whoever fixes this should decide
the direction deliberately (populate the state layer vs. repoint the
readers at the authoritative owner) rather than making stubs fire, which
would change nothing for 1 and 2 and would double-publish for 1.

Repro commands for a future session:
`grep -rn "combat()\.setTarget\|combat()\.clearTarget" src/ include/`
(empty) vs `grep -rn "combat()\.targetId()\|combat()\.hasTarget()" src/`
(five hits).

## willeq.db schema version is bumped but never validated (T27 follow-up, 2026-08-02) — **FIXED by T56 (2026-08-10)**

**CLOSED.** `WilleqDatabase::open()` (`src/client/willeq_database.cpp`) now
compares `metadata.version` against a new shared constant,
`EQT::kWilleqDbExpectedVersion` (`include/client/willeq_database.h`, = 2),
after reading it. On mismatch it `LOG_FATAL`s naming the rebuild command
(`tools/willeq_db_builder`) and returns false; both older-than-expected and
newer-than-expected are fatal (a newer db against an older client would
otherwise fail just as opaquely). `tools/willeq_db_builder.cpp`'s
`setMeta("version", ...)` call now reads the same constant instead of a
hardcoded literal, so builder and client cannot drift again. Missing
`metadata` table and missing-version-row-but-table-present behaviour are
unchanged (still ERROR+fail, and WARN+open respectively). Five new tests in
`tests/test_willeq_database.cpp` (`WilleqDatabaseVersionTest`) cover
matching/older/newer version and both missing-table/missing-row cases
against throwaway temp sqlite files — the tracked `data/willeq.db` was never
touched. See `refactor/T56_logging_db_board_fixes.md` Review for the full
verification transcript. Original report follows for the record:

T27 bumped `metadata.version` 1→2 when it added the `class_levels` BLOB
column, but **nothing reads that version** (the executor checked; it is
only logged). Meanwhile `willeq_database.cpp`'s spell queries now name
`class_levels` explicitly in their SELECT lists, so against a stale v1
database `sqlite3_prepare_v2()` fails with "no such column" and **every
spell lookup silently returns nothing** — a confusing "spell not found"
rather than "your database is out of date".

Low practical risk today because `data/willeq.db` is a TRACKED binary, so
any `git checkout`/`pull` of `fable` brings the matching v2 file. It
bites only someone with a locally-modified or separately-generated db —
e.g. a clone that regenerates with an older builder, or a worktree where
the file was manually replaced.

Small fix, worth doing under the 2026-08-02 automation-is-first-class
policy: validate `metadata.version` at open and fail loudly with an
actionable message naming the rebuild command, instead of degrading into
empty spell results. (Also note: the `willeq-benchmark` clone at
`b68c32a` predates all of this and its `build_errors_response.md`
channel file no longer exists — nothing was written to that clone.)

## Headless client aborts on shutdown and never exits when driven headlessly without a server (orchestrator, 2026-08-02) — **FIXED by T30 (2026-08-03)**

**CLOSED, T53 (2026-08-05) verification.** Commit `fa31ca97` ("T30: fix
headless client abort + non-exit on piped shutdown with no server"). Root
cause for the abort: `ConsoleInputHandler::shutdown()` only joined its stdin
thread inside `if (m_active.load())`, but `pipedInputThreadFunc()` also
clears `m_active` itself on EOF, skipping the join
(`src/client/input/console_input_handler.cpp:54-71`). For the never-exits
half: `Application::processPreConnectionLifecycleCommands()`
(`src/client/application.cpp:903,919`) now intercepts `/quit`/`/exit`/`/q`/
`/help`/`/?` *before* the `m_fullyConnected` gate at line 901, so `/quit`
piped with no server now works. Original report follows for the record:

Found while independently verifying T29 by actually piping commands into
the binary (rather than reading its diff). **Not caused by T29** —
attribution tested explicitly, see below.

Repro (no eqemu server running):
```
printf '/help\n/quit\n' | timeout 100 <build>/bin/willeq \
    -c /home/user/projects/claude/casterella.json --no-graphics
```
Two distinct problems, both reproduced **2/2**:

1. **`terminate called without an active exception` on stderr** — the
   classic signature of a `std::thread` destructor running while the
   thread is still joinable (neither joined nor detached). Same failure
   class as P36's self-join abort and T03b's RDP `stop()` use-after-free,
   both of which were real bugs.
2. **The process never exits** — `timeout` has to kill it (exit 124).
   `/quit` piped to stdin does not terminate the client here, because
   the input bridge that drains piped commands is gated on
   `m_fullyConnected` (`application.cpp:840`), so with no server the
   command is never dispatched at all.

**Attribution (this is the part that matters):** identical behaviour,
2/2 each, on BOTH the `task/T29-command-channel` binary AND the
already-merged `fable` tip (`51759f9`, i.e. with T26/T27/T28 in and T29
out). So T29 did not introduce it. Not tested against a pre-P36 binary,
so whether this is a regression since P36's "exit matrix all clean
(5×/camp exit-0 + SIGTERM + script-end + headless)" claim, or a case
that matrix never covered (headless + piped stdin + no server), is
open — worth knowing, because P36 explicitly asserted this was clean.

**Why it matters under the 2026-08-02 automation-is-first-class policy:**
every automated headless run that ends this way exits non-zero via
timeout and prints an abort, so **process exit codes are useless as a CI
signal** and every run leaves a core. That is squarely "in the way of
fully automated development and testing." Related limitation worth
fixing in the same unit: because piped commands are held until
`m_fullyConnected`, a script piping into a client that never connects
gets **no stdout at all** — no held/waiting line, no diagnostic, just
silence until something kills it. T29's stdout contract has no outcome
for "held, not yet connected".

Suggested shape: find the unjoined thread (the P36/T03b playbook —
`running_.store(false)` → `join()` → destroy), make `/quit` reachable
pre-connection or emit a held/waiting line so scripts can tell the
difference between "working" and "hung", and add a headless
piped-stdin-no-server case to the exit matrix so it stays fixed.

## Non-FATAL log output never reaches stdout during normal operation (T29 finding, 2026-08-02) — **FIXED by T31 (2026-08-03)**

**CLOSED, T53 (2026-08-05) verification.** Commit `c167acdc` ("T31: fix
non-FATAL log output never reaching stdout during normal operation").
`include/common/logging.h` now runs a dedicated `LogManager::drainLoop()`
background thread (started ~line 258) that calls `drainAllBuffers()` every
50ms unconditionally, instead of relying solely on `FlushThreadLog()`
(still called by `LOG_FATAL`, but no longer the only trigger). Confirmed in
current tree: `drainLoop`/`drainAllBuffers` both present and wired at
`include/common/logging.h:258-282`. Original report follows for the record:

`common/logging.h`'s per-thread double buffer (`ThreadLogBuffer`) only
flips its active/drain sides on `LOG_FATAL` or thread exit —
`FlushThreadLog()` is called by the `LOG_FATAL` macro and **nowhere
else**. The periodic drain thread (`LogManager::drainLoop()`, 50 ms)
only ever reads the *inactive* side, which nothing writes to for
non-FATAL levels absent an explicit flip.

**Net effect: `LOG_INFO`/`LOG_WARN`/`LOG_DEBUG`/non-fatal `LOG_ERROR`
never reach stdout during normal operation, regardless of
`--log-level`/`-d`.** That is how `AddChatSystemMessage`,
`ConsoleBridge::applyEvent`, and essentially all chat/system/combat
narration are implemented — so the client's own account of what is
happening is invisible.

Why this matters most, given the 2026-08-02 automation ruling: T29 gave
automation a way to *send* commands and a `CMD outcome=` line saying a
command was dispatched. But a script still cannot **observe the game's
response** — no chat, no system messages, no combat narration. Half the
loop is missing. This is very likely why T29's own loot-cycle
verification was so hard to drive, and it will block the next attempt
too.

Diagnosis confirmed, not guessed: a temporary local patch (self-flip in
`drainAllBuffers()` when the drain side is empty) made output appear,
and was **reverted before T29's commit** — `include/common/logging.h`
was outside that unit's territory. Also confirmed reproducible
independent of T29's changes; note the wrinkle that an isolated
standalone repro of `logging.h` alone works correctly, so something in
the full app's steady state (with no FATALs firing) is what exposes it.

Real fix needs its own unit: the naive always-flip has a wider blast
radius (drain-thread lock contention, `std::cout` write frequency on a
50 ms timer) than a drive-by change should carry. **Recommended as the
next automation unit after the shutdown fix.**

## Environment/asset blockers that stopped T29's live acceptance demo (2026-08-02)

Both hit while trying to complete the kill→loot cycle; neither is
T29's doing, and neither was fixed:

- **`race=415` model data missing → asset crash in graphics mode**,
  which blocked running the acceptance demo in graphical mode entirely
  (T29's transcript is `--no-graphics`). Same family as the recorded
  gfaydark `race=330` FATAL. Will's ruling wants "the same commands in a
  real session", and *real* means graphical — so this blocks the honest
  version of that demo.
- **A missing zone map file degraded pathfinding**, which is why
  `/moveto`-driven approach to a target could not reliably close range.
  Related to the recorded "Player BSP region is SIZE_MAX / missing
  maps/qeynos.map" family.

**T26's behavioural verification is still owed** because of these: the
*tooling* gap is closed (piped stdin reaches `/combat on` through the
real registry, confirmed against a live server), but no kill→loot cycle
has ever completed. — **T53 (2026-08-05) note: this is now stale.** T32
(2026-08-03, see the "Auto-loot on kill has NEVER worked" entry below)
*did* complete a real end-to-end kill→loot check and found the loot
machine's `AwaitingCorpse` entry is never reached — a genuine, verified
answer (the feature is dead, not merely unverified), not the open
question this paragraph describes. Treat T26's behavioural verification
as DONE via T32, with the auto-loot dead-path bug tracked separately
below. ~10 minutes of live piped interaction across
multiple target/combat/attack/moveto/who/camp cycles showed no freeze,
hang, or crash — meaningful negative evidence for the stall class T26
removed, but not a completed happy path.

## Command-registry overlap and the bare `/target` precedence gap (T29, 2026-08-02)

T29 unified command *dispatch* (registry 1 → 2 → renderer bridge)
without merging registry *ownership*. It found **~37 command names
defined in both** `EverQuest::m_command_registry` and
`action::CommandProcessor`. Spot-checks (say/shout/ooc/auction,
attack/target) showed they converge on the same underlying
`EverQuest`/`CombatManager` calls — same effect by two independently
written paths, not the "different behaviour" STOP case.

One visible difference: registry 1's `/target` requires an explicit
name and prints usage on empty args, while registry 2's falls back to
`targetNearest()`. Registry 1 now wins, so **bare `/target` no longer
targets nearest** on the `--script`/stdin path (it did before, when
`--script` reached only registry 2). Orchestrator checked: no committed
script uses bare `/target`, so blast radius is nil today. Still
reachable via `/listtargets` + an explicit name. Flagged rather than
silently absorbed; collapsing the duplicate vocabularies is a natural
part of a future command-layer cleanup.

**T51 (2026-08-05) reduced the "~37" estimate to a concrete, greppable
34-name list** — see `refactor/T51_attack_path_consolidation.md`'s Review.
It also closed three of them for real (`attack`, `stopattack`, `aa`):
registry 2's copies were deleted as always-shadowed dead code after a
live probe proved registry 1 served every one. **That is the template for
the rest of the list** — establish live which registry actually serves a
name before touching it, because reading the registrations is not enough:
T51's own plan asserted `/a` was a live attack alias, and the live probe
showed it had always been `/assist`'s. **Any future consolidation here
should be behaviour-neutral at the command line and should prove that,
not assume it.**

Smaller follow-up from the same unit: `ConsoleInputHandler`'s bespoke
non-slash `say`/`tell`/`quit` words are now redundant with `/say`,
`/tell`, `/quit` through the unified channel. Left in place (something
may depend on them); retire once nothing does.

## `--log-level=` is clobbered by `SetDebugLevel(0)` in main.cpp (T31 finding, 2026-08-02) — **FIXED by T56 (2026-08-10)**

**CLOSED.** `src/client/main.cpp` now detects whether `--log-level=` was
given explicitly (scans `argv` after `InitLogging()` has already applied
it) and, if so, restores that level after `EverQuest::SetDebugLevel()`
would otherwise clobber it back down from `-d`'s default of 0. **Decision:
explicit `--log-level=` beats numeric `-d`** when both are given (silent
precedence is what produced this bug; a one-line `LOG_INFO` fires when the
two disagree, naming which won). `-d` alone is unaffected — same behaviour
as before. `--help` (`src/client/application.cpp:2090`) now documents the
interaction. Verified end-to-end with three piped binary runs
(`--log-level=DEBUG` alone, `-d 3` alone, both together with disagreeing
levels) — see `refactor/T56_logging_db_board_fixes.md` Review for the full
transcripts. No test seam exists for this specific fix (the logic lives
inline in `main()`, which isn't compiled into a linkable target), so no
unit test was added for it — documented rather than invented, per the T56
plan's instruction. `eq.h`'s `SetDebugLevel()`/`logging.h`'s
`SetDebugLevel()` were both left untouched (out of this unit's territory);
the fix is entirely local to `main.cpp`. Original report follows for the
record:

Found by T31 while building its acceptance transcript, worked around
rather than fixed (outside that unit's `logging.h` territory).
`main.cpp` calls `SetDebugLevel(0)` *after* parsing `--log-level=`,
clobbering whatever the user asked for. `-d <n>` works; the documented
`--log-level=LEVEL` flag does not — and the client's own startup banner
tells you to use it ("Log level: DEBUG (use --log-level=LEVEL to
change)").

Relevant under the 2026-08-02 automation policy for the obvious reason:
a script that sets verbosity the documented way silently gets none.
Small fix; likely a one-line ordering change, but confirm which call
should win rather than just moving it.

## The live-session test config had THREE stale paths (orchestrator, 2026-08-03) — FIXED, and it probably explains both of T26's "blockers"

`/home/user/projects/claude/casterella.json` — the config CLAUDE.md
names for graphics integration tests, and the one every recent live
session used — still pointed at the pre-`/mnt/projects` layout. **All
three asset paths were dead:**

| key | was (missing) | now |
|---|---|---|
| `eq_client_path` | `/home/user/projects/claude/EverQuestP1999` | `/mnt/projects/EverQuestP1999` |
| `maps_path` | `/home/user/projects/claude/akk-stack/server/maps/base` | `/mnt/projects/akk-stack/server/maps/base` |
| `navmesh_path` | `…/maps/nav` | `/mnt/projects/akk-stack/server/maps/nav` |

The maps were never missing — 513 files sit at the real location,
byte-identical to the mirror at `/mnt/projects/maps/base`. Only the
prefix was stale, left behind by the workspace move.

**This very likely explains both blockers that stopped T29 finishing
T26's loot verification:** the "missing zone map file degrading
pathfinding" was this, and the `race=415` missing-model asset crash is a
strong candidate too — with `eq_client_path` pointing nowhere, model data
cannot load at all. That should be re-tested rather than assumed.

Fixed in place (backup at `casterella.json.bak-20260803`); the client now
starts with zero ERROR/FATAL lines where it previously reported an empty
EQ client path. **Lesson worth generalising: a config outside the repo
degraded live-session evidence for an unknown number of units without
ever failing loudly** — asset paths resolving should be checked at the
start of any live leg, not inferred from the absence of complaints.
Consider a startup validation that names missing configured paths.

## Auto-loot on kill has NEVER worked against a real server — pre-existing dead path, found by T32 (2026-08-03)

**T32 completed the first real end-to-end kill→loot check** (live eqemu,
GM-spawned lootable `a_scaled_wolf_pup`, driven entirely by piped
stdin) and the loot state machine's `AwaitingCorpse` entry was **never
reached** — zero loot-state markers across the kill and 60+ s of
observation.

**Mechanism (orchestrator-verified against source, not taken on
report):** two disjoint death-detection paths race, and the wrong one
wins.
- `CombatManager::ProcessCombatRound()` polls `entity.hp_percent <= 0`
  (`combat.cpp:1150`) and that block is the **only** site that enters
  `LootSubState::AwaitingCorpse`.
- But `ProcessCombatRound()` returns early at `combat.cpp:1100` when
  `!m_auto_attack_enabled`.
- The authoritative death signal arrives first as
  `EverQuest::ZoneProcessDeath()` (`eq.cpp:3233-3237`), which calls
  `m_combat_manager->DisableAutoAttack()` when the victim is our target
  — setting `m_auto_attack_enabled = false`.

So by the time `ProcessCombatRound()` next runs, it early-returns, and
the `hp_percent <= 0` block is unreachable after any server-signalled
death.

**ATTRIBUTION — NOT a T26 regression.** Verified against `677807c`
(fable immediately before T26 merged): the early-return guard, the
`hp_percent <= 0` block, and `ZoneProcessDeath`'s `DisableAutoAttack()`
call are **byte-for-byte the same structure pre-T26**. T26 replaced four
blocking sleeps *inside a block that a real kill already never reached*.
The dead path predates it, and on this evidence auto-loot-on-kill has
never worked against a live server.

**What T26 DID fix is a different, reachable entry.** `CheckAutoLoot()`
enters the machine at `AwaitingItems` (`combat.cpp:1851`) gated on
`m_combat_state == COMBAT_STATE_LOOTING`, which `LootCorpse()` sets —
the shift+click / action-layer path T23 identified as the one that
actually stalled the game thread. **T32 did not exercise that path**, so
T26's sleep-removal remains behaviourally unverified there even though
its liveness property held (see below).

**What T32 DID prove**: liveness is solid. `/who` and chat were answered
instantly during and after the kill, movement-sync packets never
stopped, clean `/camp` shutdown — i.e. nothing blocks the game thread on
this route any more.

**Two follow-ups, both real:**
1. **Wire the loot machine to the authoritative death event.** The fix
   shape is to enter `AwaitingCorpse` from `ZoneProcessDeath()` (or have
   `DisableAutoAttack()` hand off) rather than from a poll that the same
   event disables. Note this makes a dormant feature live for the first
   time — expect it to need its own verification, and decide whether
   auto-loot-on-kill is even wanted before wiring it.
2. **Verify the shift+click loot path**, which is the one T26 actually
   changed and the one that used to stall ~1.1 s per corpse.

Evidence: `benchmarks/audits/T32/` (run scripts + full transcripts) on
`task/T32-loot-verification`.

## `race=415` missing model data still crashes graphics mode (T32 re-test, 2026-08-03) — **FIXED by T38 (2026-08-04)**

**CLOSED, T56 (2026-08-10) correction.** This entry was stale: **T38 fixed
it on 2026-08-04** (`3e4f6bcd`, "T38: a missing base model skips one
entity, not the whole zone load"; `refactor/T38_missing_model_nonfatal.md`),
the day before T53's board-hygiene pass ran (2026-08-05). T53 checked this
exact entry, recorded "OPEN, confirmed still reproducing … no later fix
commit found" (`refactor/T53_deferred_board_hygiene.md:189`), and missed
it — one day wide.

**Re-verified at tip (T56, commit `d126e6fd` base) before writing this
correction, per that unit's hard precondition:**
- `src/client/graphics/renderer_zone_load.cpp:1437-1481`
  (`loadZoneStep8a_BaseModelPrep`) is now a per-entity `LOG_WARN` + skip:
  on `preloadModelData()` failure it logs a WARN naming the race/gender and
  increments `ctx.entitiesSkipped` / inserts into
  `ctx.skippedRaceGenderPairs` (declared `renderer_zone_load.cpp:94-95`),
  sets `ctx.entitySuccess = false`, and returns `true` (the zone load
  continues) — no `LOG_FATAL`, no abort. An end-of-step summary at
  `:1395-1407` reports the count and race/gender pairs whenever
  `entitiesSkipped > 0`.
- The entity falls through to `EntityRenderer::getMeshForRace()`'s
  documented placeholder-cube fallback (`src/client/graphics/CLAUDE.md`,
  "Model Loading Order" step 5).
- The parallel runtime path, `src/client/graphics/entity_prep_worker.cpp:139-145`
  (`EntityPrepWorker::processRequest`), was already non-fatal before T38 —
  confirmed still true: `preloadModelData()` failure just leaves
  `success = false` and is logged/handled downstream, no `LOG_FATAL`.
- `grep -rn LOG_FATAL src/client/graphics/` turns up exactly four
  survivors, none of them "missing race model" FATALs: two are
  whole-config-file-missing FATALs unrelated to any single entity
  (`entity_equipment.cpp:38` — `data/item_models.json` failed to load;
  `irrlicht_renderer.cpp:538` — `config/race_models.json` itself failed to
  load), and two are equipment-archive/WLD load FATALs
  (`eq/equipment_model_loader.cpp:920,927`), a different failure class
  from base race-model prep. **No `LOG_FATAL` on a missing race model
  remains anywhere in `src/client/graphics`.**
- T38's committed acceptance evidence is present:
  `benchmarks/audits/T38/run_race415_acceptance.sh`.

All of T56's Part 3 precondition held, so this re-title stands.

**Meta-finding, worth recording on its own:** T53's hygiene pass found and
fixed six stale-but-marked-open entries — and, per this correction, itself
produced a seventh miss, one commit and one day wide. The board's drift is
not a solved problem; T53's own closing caveat ("it is NOT self-maintaining")
is now directly evidenced, not just asserted. Treat every "OPEN, confirmed
still reproducing" note as time-stamped, not permanent — re-verify at the
commit you're standing on, not at the commit the note was written at.

**Genuine remainder, logged separately below** (out of T38's scope and out
of this unit's): race 415 and gfaydark's race=330 still render as
placeholder cubes, not their real models — `config/race_models.json` tops
out at race_id 367 (`refactor/T38_missing_model_nonfatal.md:85`). See
`` ## `race=415`/`race=330` render as placeholder cubes — data/content gap
past `config/race_models.json`'s race_id 367 ceiling `` below.

Original report follows for the record:

Re-tested after the stale-`eq_client_path` fix, since that was the
leading hypothesis. **Still reproduces** — genuinely separate: the model
data is missing from every S3D archive searched, not a path problem. It
fails clean (FATAL → requested quit → exit 0) rather than hanging, and
it blocks graphical live sessions, which is what Will's "the same
commands in a real session" ruling ultimately wants. Same family as the
recorded gfaydark `race=330` FATAL.

## `race=415`/`race=330` render as placeholder cubes — data/content gap past `config/race_models.json`'s race_id 367 ceiling (T56, 2026-08-10)

**New entry, NOT the same claim as the one above** (that one — the crash —
is fixed; this is the genuine remainder T38 left explicitly out of scope
and this unit leaves out of scope too). Race 415 and gfaydark's race=330
no longer crash graphics mode, but both still render as generic colored
placeholder cubes rather than their intended models, because
`config/race_models.json` only maps race IDs up to 367
(`refactor/T38_missing_model_nonfatal.md:85`). This is a *data/content*
gap — either the S3D archives genuinely lack these models, or
`race_models.json` needs new entries pointing at models that do exist
somewhere — not a code bug. Needs its own unit: asset verification (do
these races have real model data anywhere in the client archives?) and a
live visual acceptance only Will can give. Not attempted here per the T56
plan's explicit scope boundary.

## `check_file_sizes.sh` reports a vacuous pass indistinguishably from a real one (T33 finding, 2026-08-03) — **FIXED by T37 (2026-08-04)**

**CLOSED.** Investigating turned up **three** routes to a silent pass, not
the one originally reported:

1. **empty diff** (run from `main`, a slot detached at the base rev, or
   after a merge) — legitimate, but was invisible;
2. **`--files` with no paths after it** — a caller mistake;
3. **a `BASE_REV` that does not resolve** (typo, or not a git repo at all)
   — git wrote to stderr, `mapfile` got nothing, and the script exited 0.
   This was the worst of the three: a mistyped base *passed*.

Fix: (1) still exits 0 but prints `no C++ files to check (<source>) —
nothing was checked, this is not a pass`; (2) and (3) are now errors
(exit 2) because the caller asked for a check that never happened. Every
run that does check something now opens with `checking N file(s) (<source>)`
and closes with `checked N file(s): X ok, Y warn, Z fail`, so a Review
pasting the output shows what was examined rather than an absence of
output. Exit-code contract preserved: 0 ok, 1 FAIL over the hard ceiling,
2 error.

All five modes verified by hand (empty diff, bad base, empty `--files`,
explicit paths with a WARN and a FAIL, real diff with 10 changed files).

**This closes the verification-integrity trio** — golden-replay CWD skip
(T36), this, and the original golden-skip note (superseded by T36). All
three shared one shape: *a check that can quietly not run is worse than no
check, because it manufactures confidence.* Worth remembering the next
time a verification step is added.

Original report follows for the record:

`scripts/refactor/check_file_sizes.sh [BASE_REV]` builds its file list
from `git diff --name-only <base>` (default `fable`). When that diff is
empty — run from `main` (which *is* `fable`), or from a slot detached at
`fable`, or after a merge — the loop body never executes and the script
**prints nothing and exits 0**.

That is correct behaviour in the narrow sense (no changed files, nothing
to check), and it does work properly for an executor sitting on a task
branch in its own worktree. The problem is that **a vacuous pass and a
real pass are byte-identical from the caller's side**: no output, exit 0.
Every unit's verification bar ends with this step, so "no new FAILs" can
mean "checked and clean" or "checked nothing", and the report reads the
same either way.

T33's executor hit this and worked around it with `--files` plus absolute
paths, which is the right instinct but not something the next unit will
know to do.

Fix is small: emit a line when zero files are selected (e.g. `no files
changed vs <base> — nothing to check`), so a no-op run is visible. Worth
doing under the 2026-08-02 automation policy for the same reason the
golden-replay silent SKIP matters — a check that can quietly not run is
worse than no check, because it produces false confidence.

(Related, same class, already known: the golden replay test silently
SKIPS when a worktree lacks its `data/captures` symlink.)

## Golden replay silently SKIPS even with the repo-root `data/captures` symlink present (T35, 2026-08-03) — **FIXED by T36 (2026-08-03)**

**CLOSED.** `tests/CMakeLists.txt` now defines `WILLEQ_REPO_ROOT` for every
test target in that directory and below (one `ADD_COMPILE_DEFINITIONS`
line), and the four capture-path helpers resolve their defaults against it
instead of the process CWD: `test_session_replay.cpp`,
`pcap_test_utils.h`, `zone_stream_test_utils.h`,
`test_login_world_flow.cpp`. Env overrides (`WILLEQ_SESSION_LOG`,
`WILLEQ_PCAP`) still take precedence.

The pattern was not invented here — `test_zone_lines.cpp` already used
exactly this (`RepoPath()` + a per-target `WILLEQ_REPO_ROOT`, with the
comment "to stay independent of ctest's working directory"). It had simply
never been generalised beyond that one target.

**Measured, in a genuinely fresh build dir with only the documented
repo-root symlink**: 57 capture-dependent tests (golden replay + every
pcap/daybreak/login-world-capture suite) run and pass, where before every
one of them silently skipped. Isolated properly — with the symlink
correct, reverting *only* the source change still skipped all of them, so
the CWD resolution is what was at fault, not a missing capture. Full suite
on that fresh tree: 1818/1819, ALSA only.

**Remaining skips are legitimate** (graphics integration needing a
display, one sound buffer, one PFS archive fixture) — not path failures.

Original report follows for the record:

Sharper root cause than the existing note above: the repo-root symlink
(`<slot>/data/captures` -> `/mnt/projects/fable/captures`) is necessary but
**not sufficient**. `GTEST_DISCOVER_TESTS(test_session_replay)` sets
`SessionReplayGolden.MatchesRecordedSessionOrCreatesBaseline`'s ctest
`WORKING_DIRECTORY` to `<build>/tests` (the executable's own binary dir,
gtest_discover_tests' default), and `DefaultSessionLogPath()` in
`tests/test_session_replay.cpp` uses the relative string
`"data/captures/session.sleq"` — which resolves against `<build>/tests/`,
not the repo root. Every freshly created `build/` directory (routine —
`build/` is gitignored) silently re-skips this test until someone also
creates `<build>/tests/data/captures` -> the same real captures dir.
Verified: T35 hit this despite the worktree-root symlink already existing,
worked around it with
`ln -s /mnt/projects/fable/captures <slot>/build/tests/data/captures`,
reran, confirmed the test then runs and passes (not just "not skipped").

Not fixed in-tree — outside T35's territory. Two real fixes, either would
do: (1) set `WORKING_DIRECTORY` for this test to `${CMAKE_SOURCE_DIR}`
instead of the gtest_discover_tests default, or (2) have the test resolve
its session-log path relative to a compile-time source-dir macro instead
of CWD. Whoever owns `tests/CMakeLists.txt` / `test_session_replay.cpp`
next should pick one — this has now cost two units the same rediscovery.

## Item links in chat lose 11 characters of the item name (Will spotted it, root-caused 2026-08-03) — **FIXED, same T34 unit, commit `7a1e48b9`**

**CLOSED, T53 (2026-08-05) verification.** This entry was written "NOT
FIXED" but the fix landed in the same `task/T34-*` branch shortly after,
before merge (`7a1e48b9`, "T34: fix item links losing 11 chars of the item
name"; merge commit `5ec8ccc9` explicitly lists "fix item-link name
truncation"). Current `src/client/formatted_message.cpp` (both call sites,
~line 133 and ~277) disambiguates structurally via `isAllHex(data,
link_content_start + LINK_METADATA_SIZE_SHORT, ...)` on the byte range
between the SHORT and LONG metadata sizes, rather than guessing from total
link length alone. Pinned by `tests/test_formatted_message.cpp:558-605`
with the exact "Scaled Curskin" regression case this entry describes.
Original report follows for the record:

Will flagged that T34's loot evidence read *"You have looted a kin."* —
`"a kin"` is not a plausible item name. It is not: the real item is
**"Scaled Curskin"** (id 12654, confirmed against the live DB and the
client's own `TitaniumItemParser` output in the same transcript).

**Root cause — `src/client/formatted_message.cpp`, the link-metadata
size heuristic.** Item links arrive as `0x12 <hex metadata> <item name>
0x12`. The parser guesses the metadata length from the *total* link
length:

```cpp
if      (link_len > LINK_METADATA_SIZE_LONG /*56*/) { name_start_offset = 56; }
else if (link_len > LINK_METADATA_SIZE_SHORT/*45*/) { name_start_offset = 45; }
```

Titanium sends **45**-char metadata. So for a 14-character name,
`link_len = 45 + 14 = 59`, which is `> 56` — the parser takes the 56
branch and starts the name 11 characters too late:
`"Scaled Curskin"[11:]` = `"kin"`. Exactly the observed output.

**Trigger condition: any item name of 12+ characters** (45 + 12 = 57 >
56). Shorter names land under the threshold and render correctly, which
is why nobody noticed — and why the tests miss it: the longest item name
in `tests/test_formatted_message.cpp` is `"Epsilon"` (7 chars), so every
existing case stays in the safe branch.

**This is user-visible in normal play**, not just in audit transcripts:
every loot message, and every item another player links in chat, mangles
any name of 12+ characters. Most EQ item names are longer than that.

**Likely correct fix**: this client is Titanium-only ("All opcodes are
hard-coded for Titanium specifically" — CLAUDE.md), so the 56-char
variant belongs to later clients (SoF+) and arguably should not be in
the decision at all — use 45 unconditionally, or determine the boundary
structurally (metadata is uppercase hex) rather than by guessing from
total length. Add a regression case with a long item name; that alone
would have caught this.

**Correction to the record**: T33's and T34's Reviews both call this
"cosmetic" and "pre-existing". Pre-existing is right; cosmetic
undersells it — it corrupts displayed item names for the majority of
real items. Neither unit's loot verdict depends on it: both proved the
loot itself via `LootItem ACK` and the item landing in an inventory
slot, not via the chat string.

## PATTERN: `EQT_HAS_GRAPHICS` gates around non-graphics code — **SWEPT by T43 (2026-08-04)**

**CLOSED as a pattern.** T43 applied P28's rule exhaustively to every
guard outside `src/client/graphics/` and fixed **8 misclassified sites**:
hotbar load/publish on zone-connect; `setPlayerInfo()` on the inventory
manager; all three `OnPetCreated()` call sites (the residual below — pet
deltas never published headless); two `UpdateInventoryStats()` call sites
(headless equipment stats never refreshed on HP change); `/bank` and
`/train`; and **a sixth instance the sweep itself found — audio's
EQ-client-path read left audio silently disabled in headless even after
T42's `SetEQClientPath` fix.**

Notably **no site needed a new null-check**: every fixed function already
null-checks its own `m_bridge`/`m_inventory_manager`, which is *why* the
guards were redundant rather than protective. That is the general shape —
these gates were belt-and-braces around code that already defended
itself, and the belt silently disabled the feature.

**Pinned** by `FullStateSnapshot.HeadlessZoneInPopulatesGameState`
(`tests/test_zone_connection_flow.cpp`), a synthetic headless zone-in
over the server-free `ReplayZonePacket` seam asserting player stats, an
inventory slot, a spell gem and a pet all reach the bridge — state
*populated*, not "did not crash". Runs in both trees.

**Honest limit on that pin, worth keeping in mind**: it would have caught
P28, T41 and T42's item-dispatch drop, but **not** T29 (command
registration) or T42's `SetEQClientPath` — those need different oracles,
which is why the live headless leg stays in the verification bar rather
than being replaced by the test. Graphics-mode no-op-ness was established
by `visual_gate` PASS in both trees with goldens untouched.

The classification table — **with a one-line reason for every guard left
in place** — is in `refactor/T43_graphics_gate_sweep.md`'s Review. Consult
it before re-auditing; that artifact exists so nobody starts from
scratch. Historical record of the pattern follows:

Every unit that has gone looking in headless territory has found another
one. This is no longer a series of coincidences; it is a systemic
misclassification, and the instances keep getting more severe:

| unit | what was wrongly gated | effect on `EQT_GRAPHICS=OFF` |
|---|---|---|
| P28 | `eq.h` guard blocks enclosing non-graphics members | ~33k lines of base-layer source in no test binary |
| T29 | ~2,500 lines of `eq_commands*.cpp` registration | **no slash commands at all** |
| T41 | `m_inventory_manager`'s **construction** (P28 declassified the declaration, missed this) | manager never constructed; snapshot would publish nothing |
| T42 | the generic `HC_OP_ItemPacket` dispatch branch, plus a larger gate over `HC_OP_MoveItem`..`HC_OP_GMTraining` | regular inventory packets **dropped entirely** |
| T42 | `Application::initialize()` never calling `SetEQClientPath()` | **every headless zone-in force-quit within ~2 s** — headless could not stay connected to a real server |

Plus one known residual, noted here as "not yet fixed": **all three
`OnPetCreated()` call sites in `eq_entities.cpp` are `#ifdef
EQT_HAS_GRAPHICS`**, so pet *deltas* never publish in an `EQT_GRAPHICS=OFF`
build. T41's snapshot fix covers a pet present at zone-in; a pet summoned
mid-session is still invisible there.

**FIXED — same unit, T53 (2026-08-05) verification.** This residual reads
as open in the text above it, but T43's own commit already closed it:
`fabf0246` ("T43: sweep EQT_HAS_GRAPHICS guards, fix pet-delta residual,
pin headless with a test"). All three sites in current
`src/client/eq_entities.cpp` (~481, ~1236, ~1462) are unconditional, each
annotated "T43: was #ifdef EQT_HAS_GRAPHICS." The entry's prose was simply
never updated after the fix landed in the same commit that wrote it.

**Why this matters more than it looks:** headless is the mode the
automation work (T29/T30/T31) and the testing campaign run in. Each of
these made headless quietly *less functional than it appears* rather than
failing loudly — the T42 one meant a headless client could not hold a
server connection at all, and nobody noticed because nothing had tried.

**Recommended: one sweep unit** that audits every `EQT_HAS_GRAPHICS` /
`EQT_GRAPHICS` guard against P28's own rule (only renderer-facing API
stays guarded), rather than continuing to find these one per unit. The
rule is already settled; what is missing is that nobody has applied it
exhaustively. A test that builds `EQT_GRAPHICS=OFF` and exercises a real
headless zone-in end to end would have caught three of the five.

## PATTERN: real features reachable only through a UI gesture, invisible to automation — **NARROWED by T48 (2026-08-05), not closed**

**Vendor is closed.** T48 added `/vendor`, `/vendorlist`, `/buy <slot>
[qty]` and `/sell <slot> [qty]` to `EverQuest::m_command_registry`, all
pushing the **same intent structs the renderer gesture pushes**
(`VendorToggleIntent`/`VendorBuyIntent`/`VendorSellIntent`) through the
identical `ProcessBridgeIntents()` switch into unmodified
`RequestOpenVendor`/`BuyFromVendor`/`SellToVendor`. The one differing hop
is stated rather than glossed: commands push from the game thread, the
gesture from the render thread — safe either way via the bridge's mutex.

`/vendor` was added beyond the original scope because scoping the
acceptance revealed `VendorToggleIntent` had the **same** no-producer
problem — you could not have opened a vendor from a script to buy from
it. Reachability without discoverability is only half the gap, which is
why `/vendorlist` exists too.

**Attack/auto-attack is now closed too, but T51 (2026-08-05) found it was
never actually an instance of this pattern** — the framing above (T48's
"worth a small unit on the T34/T48 model") turned out to be wrong when
checked directly. `AttackIntent`/`ToggleAutoAttackIntent` did have working
`ProcessBridgeIntents()` handlers and no producer, but auto-attack was
**always reachable** the whole time: `/attack`/`att`, `/stopattack`, and
`/aa`/`~` in `EverQuest::m_command_registry` (registry 1) call
`CombatManager` directly and always won T29's dispatch order over a
**second, fully duplicate, always-shadowed** registration of the same
three commands in `action::CommandProcessor` (registry 2, routed through
`ActionDispatcher`/`EqActionHandler` to the same `CombatManager`). The
intents were a **third**, wholly dead path with no producer at all. T51's
live probe (`benchmarks/audits/T51/`) proved registry 1 served all of
`/attack`, `/stopattack`, `/aa`, `/att` in every case, proved auto-attack
genuinely engages (real melee rounds, real damage both directions, against
a live mob, three separate times), and deleted both the shadowed
registry-2 registrations and the producer-less intents. See
`refactor/T51_attack_path_consolidation.md`'s Review for the full registry
table, including a correction to the plan's own alias-trap framing (`/a`
was never live for attack — it collided with `/assist`'s pre-existing `a`
alias in registry 1 and was already dead before this unit touched
anything).

**The broader pattern (dozens of other producer-less intent types) is
still narrowed, not retired** — T48's scan is not repeated here. The
running instance list of *confirmed real* reachability gaps: `/combat on`
(T26), the whole `EverQuest` registry (T29), `/loot` as a stub (T34),
vendor buy/sell (T42→T48), `/bank` bypassing the validated intent (T50),
and **zone-line-triggered zoning (T52)** — `SetZoningEnabled()` was
reachable only via a renderer Ctrl+Z keybind, so a purely piped/headless
session could never cross a real zone line at all. T52 exposed the
existing API as `/zoning [on|off]` because it needed the zone-line
regression leg headlessly. **That is now six instances, every one found
because a unit happened to need it — the pattern keeps producing.** Attack/auto-attack (T51) turned out to be a
duplication defect wearing the same clothes, not a new instance — a
caution for whoever scans the remaining intent list next: "has a handler,
no producer" is necessary but not sufficient; check for a parallel
direct-call command before assuming a gap.

**T66 (2026-08-11) adds instances 7 and 8 — both surfaced not by scanning
the intent list, but by a live verification unit (T63) hitting the wall
directly, same as T26/T42 before it.** `IrrlichtRenderer::cycleCameraMode()`/
`setCameraMode()`/`getCameraModeString()` (`irrlicht_renderer.cpp`) were
fully implemented, with the ONLY producer being `HA::ToggleCameraMode` — a
raw Ctrl+F5 key event. A scripted/piped session could never enter first
person; **T63 could not produce any first-person evidence at all**, a third
of what its plan required, and correctly STOPped rather than inventing a
scripting affordance. T66 exposed the same functions as `/camera` (no
argument reports the current mode via `getCameraModeString()`; a named
argument sets a mode that round-trips with the getter's own strings —
deliberately NOT just exposing `cycleCameraMode()`, which would force a
script to cycle blind and count, exactly T51's `/attack` trap; cycling is
additionally exposed via `/camera cycle` since it was free). Modelled
directly on `/screenshot` (registry 3, the renderer's own
`processSlashCommand()`/`RendererCommand` bridge path) — same dispatch, same
`chat()`-reply idiom, no new path invented. Proven headlessly end to end
(`tests/test_irrlicht_camera_command.cpp`): a default-constructed, never-
`Init()`'d `IrrlichtRenderer` is safe to drive directly (constructor is
empty; the functions in this path only touch `cameraMode_` and
null-guarded members), so the test exercises the real
`processSlashCommand()` → `setCameraMode()`/`getCameraModeString()` chain,
not a mock of it — set-then-read round trips for all three modes, plus
alias/cycle/unknown-mode coverage.

Second: `EverQuest::SendMoveItem(fromSlot, toSlot, quantity)`
(`eq_merchant.cpp`, opcode `HC_OP_MoveItem=0x420f`) was fully implemented
and already had ONE producer — `MoveItemIntent`, pushed by the inventory
UI's drag-and-drop gesture (`inventory_manager.cpp`) — but no command-line
producer existed. **T63 had to write `inventory.item_id` directly into the
database and restart the client** to get a character holding a weapon at
all, a worse test than the real path since it bypasses the packet flow a
real equip uses. T66 added `/equip <fromSlot> <toSlot>`, pushing the SAME
`MoveItemIntent` the drag/drop gesture pushes (same maximum-fidelity choice
T48/T50 made for vendor/bank), with slot ids from the canonical
`eqt::inventory::EquipmentSlot` enum (`inventory_constants.h`, T57's
ruling — no numbering invented) and validation against the same slot
categories `InventoryManager::isValidSlot()` recognizes (that method is
private, so the command mirrors its predicate rather than widening its
access for one caller). Scope discipline held: no inventory management, no
swap semantics, no client-side prediction — the server is authoritative and
the existing `ZoneProcessMoveItem` path (untouched) handles the reply.
**The live end-to-end leg is explicitly OUTSTANDING** — T66 ran under NO
EQEMU (sibling T65 held the lock this round), so nothing has watched a real
`HC_OP_MoveItem` packet leave the wire or a real server actually equip a
real item. Coverage stops at the wiring (`tests/test_eq_commands_equip.cpp`):
command registered, arguments parsed (missing/non-numeric/trailing-garbage
rejected), invalid slots refused with an actionable message, and the right
intent pushed with the right fields for the equip/unequip/cursor/bank
directions the plan named. **The next unit that takes the eqemu lock should
run `/equip` live** (equip a weapon from inventory, confirm server ack and
visible appearance change) before this instance can be called closed the
way T52 closed zoning.

**That is now eight instances**, still producing exactly as this entry
predicts whenever a unit actually needs the capability rather than scanning
for it abstractly.

**Instance 8 (`/equip`'s live leg) is now CLOSED (T71, 2026-08-12).** T65
ran it live first and reported it "duplicating the primary item rather
than swapping" — the next unit taking the lock (this one) characterised
that report rather than trusting or dismissing it: three live eqemu round
trips (direct equipment<->equipment swap, cursor-mediated swap with a
clean cursor, cursor-mediated swap with a deliberately congested
multi-item cursor), each verified via fresh `inventory` table reads. All
three were CORRECT — `SendMoveItem`/`ZoneProcessMoveItem` (the shared wire
path drag/drop also uses) were never broken, and T65's exact duplication
was not reproduced despite the attempt. **The real, confirmed, fixed
defect: `/equip` never touched `InventoryManager`'s own local model or
fired `EquipmentChangedIntent` after a successful move** — the protocol
has no success-ack (only a failure-correction), so a request the server
silently accepted left the client's own inventory view (and the
appearance it drives) stale until an unrelated event refreshed it.
Drag/drop's own gesture was never affected — it already updates local
state before sending, which is exactly why nobody ever saw this on the
UI path. Fixed in `eq_commands.cpp` only, no legality judgment added (the
server is still the sole authority on whether a move is allowed); see
`refactor/T71_equip_fix_and_first_person.md`'s Review for the full
evidence and `review/T71/`.

**The first-person weapon question (open since T63) is also now answered
— NOT SHOWN, by design of un-wired code, not a capture failure.** Entering
first person (`/camera fp`, or the pre-existing Ctrl+F5 path — both funnel
through the same `IrrlichtRenderer::setCameraMode()`/`cycleCameraMode()`)
calls `EntityRenderer::setPlayerEntityVisible(false)`, which explicitly
hides the player's body mesh, name tag, AND both weapon/shield equipment
nodes for as long as the camera stays in first person — unconditionally of
combat state. Separately, `EntityRenderer::setPlayerFirstPersonMode()`
(which would switch to "arms + weapon visible"), `updateFirstPersonWeapons()`,
and `triggerFirstPersonAttack()` are fully implemented but have **zero
callers anywhere in the codebase** — confirmed by an exhaustive grep, not
inferred from the pixels. T63's own read of these three functions ("near-
no-op compatibility stubs... reuses the same skeletal bone-attachment
path") was half right: the bone-attachment path is indeed shared, but
nothing ever routes execution into it for first person, because nothing
calls the function that would flip `isFirstPersonMode` on in the first
place. Visually confirmed live in three equip configurations (primary
only, dual wield, shield) in a lit zone (`qeynos2`) with `/camera`'s own
`getCameraModeString()` round-trip visible on screen — see `review/T71/`
for stills and video. This is a finding, not a fix — T71's stage 2 is
verification-only and did not touch `irrlicht_renderer.cpp`/
`entity_renderer.cpp`; a future unit owns deciding whether/how to wire
`setPlayerFirstPersonMode()` in.

**T77 (2026-08-12) adds a TENTH instance of the same standing pattern in a
slightly different shape: not "no producer at all" but "the validation
existed and the caller did not use it."**

> **Numbering note (orchestrator, 2026-08-12).** T77's Review called this
> the ninth; it is the tenth. **The ninth is the first-person weapon
> feature** described immediately above — `setPlayerFirstPersonMode()` /
> `updateFirstPersonWeapons()` / `triggerFirstPersonAttack()`, found by
> T71's stage 2, implemented with zero callers. It was written up as a
> finding without being given a number, so T77 reasonably took the next
> one. Corrected here rather than left to drift: this file is a research
> record and later units cite these by number. The first-person instance
> is additionally distinctive — it is the only one so far where the
> production code was **hollow** rather than merely unreachable, which is
> why "just add the callers" (T74's initial framing) would have produced
> nothing visible. T75 hit `/equip` sending a move
the server was always going to reject (`slot 23 -> Primary (13)`, `"The
selected slot was invalid"`) — `/equip` validated only that a slot NUMBER
was recognized, never whether the source held an item or whether the item
could legally go in the destination, even though
`InventoryManager::canEquipItem()`/`ItemInstance::canEquipInSlot()`
already did exactly that legality check and T71 had already added
`GetInventoryManager()` to reach it. T77 wired pre-flight into `/equip`
(rejects with a specific reason — "X cannot go in Y — wrong slot type" /
"...does not meet its class/race/level requirements" — instead of sending
a doomed packet), added `/equip <item name>` (resolves the source by
case-insensitive substring search over carried items, and the destination
from the item's own `slots` bitmask via `canEquipInSlot()` — paired slots
fall out for free by iterating every equip slot and preferring an empty
member of the pair; reports what it resolved to), and `/equip list`
(equipped-and-where, carried-and-where-it-would-go). No new legality
predicate was written — same fence T71 held. **The live end-to-end leg for
all three of T77's additions is explicitly OUTSTANDING**: T77 ran under NO
EQEMU (sibling T76 held the lock this round), so nothing has watched a
real pre-flight-then-send round trip, a real name-form resolution, or a
real `/equip list` against a live character's actual inventory. Coverage
stops at 30 tests in `tests/test_eq_commands_equip.cpp` against a populated
(but never server-synced) `InventoryManager`: empty source, illegal
destination (both the wrong-slot-type and the restrictions-failure
shapes), no-op same-slot, name resolution (including case-insensitive
substring match), name not found, ambiguous name across distinct items,
duplicate stacks of the identical item (correctly NOT ambiguous), paired-
slot selection preferring the empty member, both paired slots full
("would replace", still resolves), an item fitting no equip slot at all,
and both `/equip list` shapes (empty inventory, populated). **The next unit
that takes the eqemu lock should run all three forms live** — a pre-flight
rejection against a real restricted item, a successful `/equip <name>`
resolving and equipping a real carried weapon, and `/equip list` against a
real character's live inventory — before this instance can be called
closed the way T52 closed zoning and T71 closed instance 8's wiring leg.

**Instance 10 (T77's `/equip` pre-flight/name-form/list live leg) is now
CLOSED (T82, 2026-08-12).** All three forms confirmed live against
`Testmelee`: pre-flight rejections (empty source, wrong slot type, class/
race/level restriction — the last genuinely a race exclusion, decoded
against the item's `races` bitmask, not a level issue), `/equip <name>`
resolving and equipping a real carried weapon end to end (item 1327,
"Grizznot's Claws", resolved via `item_models`), `/equip list` against a
real live inventory, and the T71 explicit-slot form round-tripped cleanly.
Also closed, as a byproduct: the original T75/T76 server rejection
(`/equip 23 13`, "The selected slot was invalid") — T77's pre-flight now
catches it locally (the item in that slot, 12654 "Scaled Curskin", fits no
equipment slot at all; it was never a weapon). See
`refactor/T82_live_verification_session.md`'s Review for the full evidence
and `review/T82/`. **A second, narrower gap was found live in the same
session and is NOT closed**: `doMove()`'s direct slot-swap branch never
validates the *displaced* item against the equipment slot it lands in when
`fromSlot` is equipment and `toSlot` is occupied (e.g. unequipping a weapon
into an occupied general slot can swap a non-equippable item into the now-
empty equipment slot) — the server correctly rejects this and the client
self-heals via its existing desync-detection/resync path (not data loss),
but it is a real, reproducible pre-flight gap. Not fixed under T82 (larger
than the plan's "small, clearly-scoped" exception); a future unit should
extend `CheckEquipDestination()` (or its caller) to validate both directions
of a swap.

Historical record follows:

The 2026-08-02 ruling says anything blocking fully automated development
and testing is a defect. This keeps showing up in the same shape:

- **T26**: `/combat on` reachable only from the live chat window, which is
  why T26 could not verify its own loot fix.
- **T29**: the *entire* `EverQuest::m_command_registry` unreachable from
  `--script`.
- **T34**: `/loot` was a stub that printed a message and never dispatched;
  `EqActionHandler::lootCorpse` was reachable only from the renderer's
  shift+click callback.
- **T42**: **no slash command or renderer intent exists anywhere to
  trigger vendor buy/sell** — `VendorBuyIntent`/`VendorSellIntent` are
  never constructed. T42 had to add temporary scratch hooks calling the
  production functions directly to run its live acceptance, then revert
  them.

Each was found only because a unit happened to need it. **The vendor case
is the open one**: a scripted session cannot buy or sell, so nothing in
that area can be verified without hand-written hooks — and the next unit
that needs it will write them again.

Worth a small unit to expose vendor buy/sell through the command channel
the way T34 did for `/loot`, and worth asking what else in the intent
list has no producer.

## `GameTick()` never runs headless — spell cooldowns don't tick, buffs never expire (T44 finding, 2026-08-04) — FIXED by T45

The **runtime** analogue of the compile-time guard family T43 swept, and
it was missed precisely because T43 audited `#ifdef`s.

`EverQuest::GameTick()` is gated on graphics **twice**:

1. `Application::updateGameState()` — `if (m_eqClient && m_graphicsInitialized) GameTick(...)`;
2. inside `GameTick()` itself — `#ifdef EQT_HAS_GRAPHICS { if (!m_graphics_initialized) return; }`.

`GameTick()` is the only caller of `SpellManager::update()` and
`BuffManager::update()` (orchestrator confirmed: the sole call site is
`eq_bridge.cpp:234`, inside `GameTick`). Its own comment describes that
work as "cooldowns, memorization progress, cast timeouts" and "buff
durations, expirations".

**So in headless: spell cooldowns never tick, memorization never
progresses, cast timeouts never fire, and buffs never expire.** These are
game-logic timers with no renderer dependency whatsoever.

Not a regression — it predates the sweep and affected the old
`syncSpellState()` poll equally. And T26's plan already noted in passing
that "`GameTick()` is graphics-gated and would never run headless" (which
is why T26 wired the loot machine elsewhere), but nobody established the
*consequence* until T44 tried to observe a gem leaving cooldown in a
headless session and couldn't.

**Why it matters:** headless is the automation and testing mode. Anything
time-dependent in spell or buff behaviour is currently untestable there,
and any future unit that assumes otherwise will get a silent wrong
answer, exactly as T44 nearly did.

Fix is presumably small (the two gates), but it is **behaviour-changing
for headless** — timers that have never run would start running — so it
wants its own unit with a real before/after rather than being folded into
something else. Worth checking whether anything else `GameTick()` calls
has a genuine graphics dependency before un-gating wholesale.

**Fixed by T45** (`refactor/T45_gametick_headless.md`): both gates now OR
in `EverQuest::IsGraphicsReady()` (loading phase reached `COMPLETE`, the
same headless "state ready" signal T41 used for the full-state snapshot)
alongside the existing `m_graphicsInitialized`/`m_graphics_initialized`
check, rather than replacing it — graphics mode's flag is a one-way latch
already `true` for the entire window `GameTick()` can run in that mode
(set once in `Init()`, before `gameThreadLoop()` starts), so the OR is
provably a no-op there. Verified via `visual_gate.sh` (pixel-exact,
goldens untouched) plus a live before/after against eqemu (a
`SpellManager::update()` cast-confirm timeout that never fires on `fable`
headless, confirmed firing after the fix, in both build trees). See that
plan's Review for full detail, including a bridge-wiring gap found along
the way (next entry).

## `InventoryManager`/`SpellManager`/`SkillManager`/`HotbarModel` never get their own bridge wired headless (T45 finding, 2026-08-04) — **FIXED by T46 (2026-08-05)**

**CLOSED, T53 (2026-08-05) verification — this was the single most
dangerous stale entry found in this pass** (marked open, needing "a small
unit", when a unit already fixed it and the board never caught up).
Commit `b5f5c1d6` ("T46: wire the four managers' bridge pointers headless,
not just in InitGraphics()") extracted the wiring into
`EverQuest::wireBridgeToSubsystems()`, called from both `setBridge()`
(moved out of the header, now in `eq_bridge.cpp`) and `InitGraphics()`.
Confirmed live in current tree: `src/client/eq_bridge.cpp:121`
(`wireBridgeToSubsystems(bridge)` inside `setBridge()`) and callers at
`src/client/application.cpp:654` reasoning explicitly about the
unconditional propagation this entry asked for. The "Bridge pointers are
never cleared at teardown" entry below (T47) also depends on and
references this exact function, which is corroborating evidence the fix
is real and not a documentation artifact. Original report follows for the
record:

Found while verifying T45's "un-gating `ProcessBridgeIntents()` closes a
latent unbounded-queue hazard" claim for the `inventory_manager.cpp`
pickup-intent exception the T45 plan named.

Each of these four classes holds its **own** `GameStateBridge*` member
(`InventoryManager::bridge_`, `SpellManager::m_bridge`,
`SkillManager::m_bridge`, `HotbarModel::bridge_` — all separate from
`EverQuest::m_bridge`), and each is wired via a `setBridge()` call **only**
inside `EverQuest::InitGraphics()` (`eq_bridge.cpp:121-133`) — a
graphics-only call path: the function itself is `#ifdef EQT_HAS_GRAPHICS`,
and `Application::Init()` only calls it inside the
`config.graphicsEnabled && operatingMode == GraphicalInteractive` branch.
`EverQuest::setBridge()` (`eq.h:1037`, the one call `Application::Init()`'s
*headless* branch makes) sets only `EverQuest::m_bridge` — it does not
propagate to any of the four managers.

**Consequence: in every headless run (`EQT_GRAPHICS=OFF`, or
`EQT_HAS_GRAPHICS` with `--no-graphics`), all four managers' bridge
pointers are `nullptr` for the entire process lifetime.** Every
`if (bridge_) bridge_->pushIntent(...)` / `if (m_bridge)
m_bridge->pushEvent(...)` guarded on one of these four pointers is a
silent no-op headless — confirmed by grep (`setBridge` on each has exactly
one call site, inside `InitGraphics()`) and, for `SpellManager`
specifically, by reasoning through T44's own live transcript: the gem/cast
events T44 documented as "published" were inferred from adjacent
`LOG_INFO`/`LOG_DEBUG` lines that fire unconditionally, not from confirmed
bridge delivery — the actual publish calls (`publishGemChanged()`,
`publishCastStarted()`, `publishCastComplete()`) all early-return on
`SpellManager::m_bridge`, which was never wired in that session.

**Practical impact, concretely:**
- Item pickup in headless never pushes `MoveItemIntent`/
  `EquipmentChangedIntent` (the T45 plan's named "exception" case) — not
  because anything filters it, but because it never reaches `pushIntent()`
  at all.
- `SpellManager`'s own bridge-event publishes (gem state changes, cast
  start/complete — the T44 "spell state via bridge" unit's entire
  deliverable) are inert headless in production, even though
  `GameTick()` now runs there (T45) and `SpellManager::update()`'s
  *internal* state machine (the thing T44 and T45's tests actually
  verified) works correctly — it is specifically the bridge-event
  *publish* half that's silently dropped. `GameStateSyncBridge`/`SpellState`
  reads would see nothing change in a live headless session despite the
  underlying gem/cast state genuinely transitioning.
- Same shape for `SkillManager` and `HotbarModel`.

**Not a T45 regression** — this predates T45 and is orthogonal to the
gating fix T45 made (T45's own live acceptance leg worked around it by
observing `SpellManager`'s internal state via `beginCastFromGem()`'s debug
log, not via bridge-delivered events). **Not fixed here** — deliberately
out of T45's territory (`Application::updateGameState()`'s gate,
`EverQuest::GameTick()`'s own gate, and tests). Worth a small unit: wire
`EverQuest::setBridge()` (or a new headless-path call site alongside it,
matching T41's `m_inventory_manager` construction precedent) to also call
`setBridge()` on all four managers, with a live headless acceptance
leg that confirms a `SpellGemChanged`/`InventorySlotChanged` event is
actually observed on the console bridge (not just inferred from a log
line) — the mistake this entry itself documents T44 nearly made.

## Bridge pointers are never cleared at teardown — **FIXED by T47 (2026-08-05)**

**CLOSED.** `Application::shutdown()` now calls `setBridge(nullptr)`
unconditionally at the top, outside the `#ifdef`, so it runs in both
graphics and true-headless builds and clears all five pointers via T46's
`wireBridgeToSubsystems()`. `ShutdownGraphics()`'s redundant
`m_bridge = nullptr` was **removed** rather than left alongside — one
source of truth instead of two competing partial-clear paths.

Verified nothing publishes during shutdown by reading every step the
function runs (`stopRDPServer`, `joinLoadingThread`, `ShutdownGraphics`,
`IrrlichtRenderer::shutdown`, all three `IGameMode::shutdown`
implementations, `~EverQuest`), so clearing first drops nothing.

**Scope of the claim, stated as the executor stated it** — this is the
part worth preserving: the opt-in ASAN repro
(`tests/test_bridge_teardown.cpp`, `WILLEQ_T47_UAF_REPRO=1`, skipped by
default) produced a genuine ASAN-named heap-use-after-free in
`GameStateBridge::pushEvent` when a manager's pointer is not cleared
before the bridge dies, and a clean no-op when it is. **But that
exercises the mechanism at manager+bridge granularity, not
`Application::shutdown()` end to end — because the end-to-end hazard is
not currently constructible.** `shutdown()` destroys `EverQuest` and every
manager together in a single `m_eqClient.reset()`, so no window exists
today where a manager outlives a freed bridge pointer.

So T47 is **defensive hygiene that makes an invariant explicit**, not the
repair of a reachable bug. It stops being hygiene the moment teardown
ordering changes or anything publishes at shutdown — which is why the
permanent `BridgeTeardownInvariant` test is the durable part, not the
repro.

Historical record of the finding follows:

Characterized precisely by T46 while wiring the managers:

- **Graphics builds**: `ShutdownGraphics()` clears `EverQuest::m_bridge`
  but **not** the four managers' (`InventoryManager`, `SpellManager`,
  `SkillManager`, `HotbarModel`).
- **True-headless (`EQT_GRAPHICS=OFF`) builds**: **none of the five** are
  cleared, because that teardown block sits inside
  `#ifdef EQT_HAS_GRAPHICS`.

`Application::shutdown` does drop its own pointer and destroy the
decorator, so after shutdown `EverQuest` and its managers hold pointers
to a destroyed bridge. Survivable today only because nothing uses them
post-shutdown — the kind of reasoning that stops being true the moment
someone adds a shutdown-time flush or a late event.

**T46 left the fix one line away on purpose**: `wireBridgeToSubsystems()`
propagates whatever it is given including null, so a single
`setBridge(nullptr)` in `Application::shutdown()` would clear all five at
once. Wants its own small unit with a real before/after (ASAN or a
deliberate post-shutdown publish), not a drive-by — the whole point is
proving the teardown ordering, and this codebase has been bitten by
lifecycle ordering three times already (P36 self-join, T03b/T35 RDP
use-after-free, T30's unjoined stdin thread).

## `PlayerSpawnIdSet` arrives later headless than in graphics mode (T46 observation, corrected 2026-08-05) — NOT a bug

Recorded because it was reported as a defect and is not one, so nobody
re-raises it.

T46 flagged that `PlayerSpawnIdSet`'s publish at `eq_entities.cpp:396`
(the `ZoneSpawns` path) is `#ifdef EQT_HAS_GRAPHICS`-gated, and read that
as breaking `CastingStateChanged`-based casting reconstruction headless.

**T43 had already investigated that exact site** and classified it
legitimate, on the grounds that the `ClientUpdate` path publishes the
same event unguarded. **Orchestrator verified T43 is right**: there is no
guard around the `ClientUpdate` publish (`eq_entities.cpp` ~1517), so
headless does receive `PlayerSpawnIdSet` — just later, at the first
position update rather than at `ZoneSpawns`.

So it is a *timing* difference across a window in which the player cannot
have cast anything, not a missing event. Not a seventh instance of the
graphics-gate family, and T43's classification stands.

## From Batch Z

- Z5.3 empty-draw measurement (1-triangle buffer isolation) — TODO.

## Tracker hygiene

- Batch E table in progress.md has empty commit columns (work is committed;
  hashes were never recorded).

## P37 follow-ups (2026-07-29, merged `0019ba6`)

- ~~Pi KPI attribution + re-measure needed~~ — **MEASURED by P45
  (2026-07-30, merged `6944a3a`; full tables in
  refactor/P45_p37_kpi_remeasure.md Review). AWAITING WILL's
  regress-response decision.** Canonical e205 replay: p95
  32.240/32.263ms, flat vs P14/P39 and under the line — but this is a
  BENCHMARK-COVERAGE GAP, not neutrality: the default race cycle (IDs
  1-12) structurally never contains P37's excluded races. Real-roster
  A/B (ZOM-heavy --test-races, tip vs pre-P37 fda2e34): draws_avg
  113.6→348.3 (+207%), p95 39.304→57.650ms (**P37 costs +18.3ms p95**,
  ~all submission: +19.4ms sub p95 vs +0.3ms GPU). Caveat: e205
  stress + that roster ≠ production encounter density; translation to
  player experience is Will's call. OPTIONS (neither implemented):
  (1) composite-sheet alpha support in the builder + NFS redeploy —
  restores composite eligibility for masked races; (2) accept the
  loss for era correctness. SIDE FINDING: canonical-replay draws_avg
  41.9 vs P11b's 287.7 for a similar stack — route/pose-mix
  difference, noted not alarming. Also: the canonical replay's
  synthetic roster hides any masked/exotic-race effect — future
  entity-rendering units should add a --test-races real-roster leg
  when the change touches race-conditional paths.
- ~~Bless evidence pending~~ — P44's live pairs (merged `2246de5`)
  were **REJECTED by Will 2026-07-30, rejection verified correct**:
  aggroed mobs' attack animation made before/after pose-mismatched
  (diff = pose, not alpha), and the dark-stone background makes
  masked-opaque fill indistinguishable from alpha cutout. P44's
  "textbook evidence" claim is UNSOUND — annotated VOID in
  `refactor/P44_p37_bless_evidence.md`'s Review by P44b.
  **P44b retake COMPLETE (task/P44b-bless-retake, slot2) — STILL NO
  VISUAL EVIDENCE, new root cause found**: pose-identical static-
  benchmark method worked as designed (deterministic camera/entity
  placement + frame-index animation clock, bright-navy-sky close-up
  framing), but BEFORE/AFTER renders came back PIXEL-IDENTICAL for
  both SKE and ZOM at every angle/radius/scale tried. Root cause is a
  `gles2_zone_benchmark` TOOL GAP, not a P37 regression: the tool
  never links `entity_renderer.cpp`, so `mesh_building.cpp`'s
  MaterialType marker (confirmed correct via a reverted diagnostic
  trace, texture-for-texture match to P37's ground-truth table) never
  gets its companion-alpha-texture binding — the built-in alpha-test
  shader the tool falls back to reads unit-0 alpha only, which is
  always opaque for ETC1-loaded entity textures, so the discard never
  fires regardless of framing. RECOMMENDATION (not actioned, out of
  P44b's errand scope): wire `applyAlphaTestMaterialType()`-equivalent
  companion-texture binding into `gles2_zone_benchmark`'s entity path
  so future entity-rendering blesses have a working offline visual
  instrument — this would unblock a THIRD bless attempt without
  needing a live client at all. Zombie chest (zomch0001) remains an
  expected null by design (confirmed independently, doubly-so).
  Full writeup: `refactor/P44b_bless_retake.md`,
  `benchmarks/audits/p44b_bless_retake/EVIDENCE_MANIFEST.txt`.
  LESSON (P44): never use live animated mobs for visual A/B evidence;
  always check what's behind the alpha regions. LESSON (P44b): before
  trusting an offline benchmark tool as a stand-in for production
  visual behavior, verify it actually links the production code path
  in question, not just a marker the production path would consume.
  **P44c RGBA leg COMPLETE (same branch/slot) — ALSO NULL, but
  root-caused one level deeper than P44b, and this one is NOT a
  benchmark-tool-only gap.** Hypothesis tested: dropping `--eqe1-path`
  (forcing the direct-BMP/RGBA decode path, the same path production
  `willeq` uses whenever no ETC1 pack is configured) should let the
  built-in `EOGLES2SP_ALPHA3D` shader's unit-0-alpha read actually see
  real alpha, since `EntityRenderer::applyAlphaTestMaterialType()`
  (entity_renderer.cpp) documents the RGBA/DDS path as carrying "real
  alpha in the main texture's own alpha channel." FOUND INSTEAD: alpha
  is lost one step earlier, at decode. `ConstrainedTextureCache::
  decodeBMP()` (`src/client/graphics/constrained_texture_cache.cpp`,
  ~line 1151) handles EQ's masked entity textures — confirmed 8bpp
  paletted BMPs via a temporary trace (skefa0001/skeft0001/skeft0002/
  skech0001/skelg0001 all 8bpp) — by hardcoding alpha=255 for every
  pixel ("ignore palette alpha", the code's own comment). It does NOT
  implement the EQ masking convention ("palette index 0 = transparent")
  that `tools/eqe1_builder.cpp`'s BMP decoder (~line 88) already
  implements correctly for the ETC1/eqe1 offline pipeline. So
  `hasAlpha` is false for every ground-truth masked entity texture on
  this path — confirmed via a reverted diagnostic trace, then via an
  actual BEFORE/AFTER render pair (SKE, befallen, same `--orbit-radius
  8 --orbit-height-offset 6 --animate` framing as P44b, frame 26,
  `--eqe1-path` omitted): PIXEL-IDENTICAL, AE=0, byte-identical PNGs
  (matching md5), same result P44b already got when it tried this as a
  side-check. This means: unlike the ETC1 leg (P44b, a benchmark-tool
  linking gap only), **the RGBA path has no way to visually demonstrate
  P37's fix at all, in ANY tool including production `willeq`, until
  `ConstrainedTextureCache::decodeBMP()` gets the same palette-index-0
  masking logic `eqe1_builder.cpp` already has.** Not fixed this unit
  (diagnostics reverted, no production source changes) — this is a
  second, more consequential tool-fix candidate than P44b's
  entity_renderer-linking one, and arguably belongs ahead of it since
  it affects any RGBA/non-ETC1 rendering path, not just the benchmark.
  Evidence: `/mnt/projects/fable/videos/p44b_bless_retake/rgba_leg/`
  (NFS, gitignored) — `pairs/ske_{BEFORE,AFTER}_f26.png`,
  `crops/ske_{BEFORE,AFTER}_f26_torso_zoom.png` (400%),
  `diffs/ske_diff_f26{,_torso_zoom}.png` (AE=0 both). Full writeup in
  `refactor/P44b_bless_retake.md`'s Review addendum and
  `benchmarks/audits/p44b_bless_retake/EVIDENCE_MANIFEST.txt`'s RGBA-leg
  section.
  **BOTH GAPS CLOSED BY P46 (2026-07-30, merged `b49337b`)**: (A)
  runtime palette-index-0 masking in decodeBMP, gated on
  WLD-textureMasked via applyPaletteMask param — only the two
  call sites that already compute the masked flag pass true; zone
  path untouched; unmasked/zone renders md5-identical; (B) benchmark
  companion binding mirroring entity_renderer line-for-line.
  **BLESS EVIDENCE DELIVERED, NON-NULL, AWAITING WILL**: lead pair =
  videos/p46_bless/primary_etc1/crops/ske_{BEFORE,AFTER}_f26_torso_zoom.png
  (ETC1 leg, isolates P37 alone, Pi-representative); secondary RGBA
  leg = P37+P46 jointly (x86-representative); diffs entity-confined
  (tight silhouette bounding boxes); zomch0001 null by design.
  ~~ORCHESTRATOR OBSERVATION: AFTER limbs warmer~~ — that observation
  WAS the bug. **P46 pairs REJECTED by Will (round 3, wrong textures
  on body parts); ROOT-CAUSED + FIXED by P46b (2026-07-31, merged
  `c40475d`)**: benchmark-only — P46 Part B ported only the
  alpha-test half of entity_renderer's PAIRED shader promotion, so
  masked buffers were shader-lit while unmasked siblings stayed
  fixed-function (per-entity lighting mismatch that read as wrong
  textures). Bisect matrix exonerated P37/P42 and Part A entirely
  (Part-A revert = byte-identical; pre-P46 clean — caveat: the
  instrument couldn't reach the path pre-P46, so production
  exoneration rests on entity_renderer's own always-paired code, not
  the matrix). Fix = applyEntityShaderMaterialPromotion pairing both
  halves. **═══ P37 VISUAL BLESS: CLOSED — WILL BLESSED THE P47
  MAGENTA CONTACT SHEET 2026-07-31 ("contact sheet looks correct")
  ═══** Six rounds total; the instrument that finally worked is
  Will's own design (P47, merged `6b78548`): model rendered large
  against solid magenta, no zone, enclosed-magenta metric +
  permanent ctest regression pin (test_entity_viewer_regression).
  Every earlier round's evidence (P44 live pairs, P46 pairs, P46b
  pairs, P46d pairs) is historical only — the blessed artifact is
  benchmarks/contact_sheets/p47_entity_viewer/index.html.
  Historical record of round 4 follows: ~~ROUND-4 EVIDENCE READY,
  AWAITING WILL~~: lead pair
  videos/p46b_bless/primary_etc1/crops/ske_{BEFORE,AFTER}_f26_torso_zoom.png
  (+ zom). KNOWN DISCLOSED DELTA: BEFORE leg renders fixed-function
  (pre-promotion instrument history) → pairs show the cutout PLUS a
  uniform warm/bright shift; production always ran both sides
  shader-lit, so players saw only the cutout. Judge the AFTER
  standalone for correctness; the tone delta is the instrument, not
  the game.
  **P46c (round 5, lighting-neutral retake) — STOPPED, root cause found,
  no new pair delivered.** Added benchmark-only
  `--no-entity-shader-promotion` (verified working: promotion-off AE=0 vs
  promotion-on AE=6900, matching P46b's own promoted-leg number exactly).
  Pre-capture discard check (mandated by the plan) found the RGBA leg's
  alpha-test discard **never fires** at all with promotion off — AE=0 at
  every frame/radius tried. Root-caused (temp diagnostic, reverted): a
  cache-key collision in `ConstrainedTextureCache` (name-keyed only;
  `applyPaletteMask` is only consulted on first decode of a name) combined
  with `zone_bench_loader.cpp`'s static `getMeshForRace()` preload loop
  running (and decoding entity textures unmasked via
  `ZoneMeshBuilder::loadTextureFromBMP`, `zone_geometry.cpp:249`, no
  `applyPaletteMask` arg) *before* the masked-aware T05/animated decode for
  the same race — the unmasked decode wins the cache slot permanently.
  Retroactively explains P46b's own bisect-matrix finding ("reverting Part
  A changes nothing"): the RGBA leg's committed AE numbers (both P46's and
  P46b's) were never showing an alpha cutout, only the lighting-model
  delta — the ETC1 leg's numbers are unaffected (separate companion-texture
  alpha path) and remain real. Production source untouched (fix is a
  separate errand, cache-key collision + an open question re: whether
  production's own entity load path has the same ordering hazard). Full
  writeup: `refactor/P46c_neutral_pair_stop.md`. P46b's round-4 pair (with
  its disclosed tone delta) remains the best available evidence pending the
  cache fix.
  **P46d (round 5, cache-key fix) — DONE, closes P46 Part A.** Made
  `ConstrainedTextureCache` mask-aware: `cacheKey(name, applyPaletteMask)`
  (identity when false, `name+"\x01masked"` when true) separates masked and
  unmasked variants across `cache_`/`decodedEntries_`/`compressedEntries_`/
  `pendingLedgerTag_`; `uploadTexture()` gained an `applyPaletteMask` param;
  `uploadAllPending()` uses a new `splitCacheKey()` inverse so its
  registrations still carry the real display name. Two non-obvious
  additional name-uniqueness hazards found and fixed en route (both caught
  by this unit's own discard-fire re-verification, not assumed away):
  `COpenGLES2Driver::addTexture()`/`wrapExternalTexture()` dedup GPU
  textures by name (silently returns the existing texture, discarding new
  image data, for a same-named second upload) — fixed by uploading under
  the internal key, not the display name. **Production ordering audit
  verdict: the live client's primary entity path (createAnimatedNode-first,
  static-fallback-only-on-failure) was NEVER exposed to P46c's poisoning
  ordering** — P13b's own fix (unrelated original motivation) happens to
  also protect this path; the benchmark's own unconditional static preload
  loop was the sole source of the exposure P46c found, an instrument
  artifact, not a shipped bug. Two OTHER, pre-existing, out-of-scope gaps
  found during the audit (composite-deferred on-demand per-part loads via
  `mesh_building.cpp`'s `loadTextureFromBMP()` fallback, and equipment
  texture-variant slot swaps via `animated_mesh_creation.cpp` — neither ever
  threads `applyPaletteMask`) — flagged below, not fixed (extending Part A's
  plumbing to two more call sites, a new errand). Discard-fire re-check now
  fires (AE=678, entity-confined bbox 84x198 @ ske f26, RGBA mode,
  `--no-entity-shader-promotion`, tip vs fda2e34). **Round-5 pair delivered
  and PASSES the absolute one-variable bar** (numerically + by the
  executor's own part-by-part inspection) at frames 26/42, both races —
  zom's leg crop shows a directly visible dashed alpha-cutout pattern absent
  in BEFORE, the clearest visual proof in this whole chain. Frame 12 carries
  a small (13-44px), non-entity zone-geometry artifact independently
  reproduced with zero entities (traced to this session's Irrlicht-tree
  rebuild, not this fix) — excluded from the "clean pair" claim, fully
  disclosed. Full writeup: `refactor/P46d_cache_key_fix.md` Review; numeric
  evidence + crops: `benchmarks/audits/p46d_bless/EVIDENCE_MANIFEST.txt` /
  `videos/p46d_bless/{before,after}/{pairs,crops}/`. **Open follow-up items
  (not this unit's scope)**: (1) thread `applyPaletteMask` through
  `mesh_building.cpp`'s on-demand `loadTextureFromBMP()` fallback for
  composite-deferred races; (2) same for `animated_mesh_creation.cpp`'s
  equipment texture-variant swap path — both currently decode any masked
  texture they touch opaque, a real (if narrow) live-client exposure
  distinct from the now-fixed cache-key bug.
- **P44 live-client findings (flagged, not fixed)**: (1)
  non-deterministic SIGSEGV zoning into befallen at x=-195,y=-982
  (zone-object-mesh-build; would not reproduce under gdb, no core);
  (2) pre-existing bind-point respawn FATAL "Player BSP region is
  SIZE_MAX!" (missing maps/qeynos.map — related to the P10b
  local-map/BSP FATAL family); (3) qeynos2 sky stays black despite
  /time 12 (zone-lighting quirk, unrooted).

## P40 verdict: resolution-visibility "anomaly" NOT a code bug (2026-07-30, merged `e3b61ee`)

- x86 repro at 3 resolutions: visibility counters BYTE-IDENTICAL
  (5,000/5,000 cells); only GPU time scales with pixels (normal fill
  cost). Occlusion buffer is fixed 64x32 from the preset (never
  res-derived); frustum aspect identical at 4:3 points; no LOD-by-
  pixel code exists. Likeliest explanation = P38's own flagged
  methodology drift (res run lacked shipped flags). ~~PENDING: a
  2-invocation clean Pi A/B (400x300 vs 800x600, shipped flags) to
  close it — ride along the next Pi unit.~~ Non-4:3 aspect changes DO
  move the frustum (correct behavior, noted for future res work).
- ~~REOPENED by P41's Pi ride-along~~ — **ROOT-CAUSED + CLOSED by P43
  (2026-07-30, merged `330aa10`; full narrative in
  refactor/P43_res_visibility_root_cause.md Review)**: NOT a culling
  bug, NOT Lima. `docker/irrlicht-drm/CIrrDeviceFB.cpp:335-382`
  silently substitutes the connector's PREFERRED mode when --res has
  no exact DRM match and overwrites Width/Height downstream — every
  historical "400x300" Pi run actually rendered **2560x1440** (16:9,
  7.68x the pixels of 800x600), log-confirmed on hardware. Wider
  frustum explains +22% draws/+39% tris to the decimal; "GPU
  inversion" = plain fill-bound scaling at the real pixel count.
  P40's x86 null was correct all along (P43 re-confirmed
  byte-identical in both flag configs). **CAUTION for all past/future
  data: any Pi CSV at a non-native resolution is mislabeled — the
  only trustworthy Pi resolutions are the connector's exact modes
  (800x600@75 confirmed; check the mode list before any res sweep).**
  Shipped 800x600 unaffected (exact mode match) → P43 correctly
  STOPPED, no code change.
- **Follow-up landed (P49, task/P49-drm-fail-loud, x86-only, awaiting
  merge)**: `docker/irrlicht-drm/CIrrDeviceFB.cpp`'s mode-fallback log is
  now ELL_ERROR (was ELL_WARNING) stating requested-vs-actual mode,
  repeated once more on the first post-init `run()` call so it isn't only
  visible in device-creation spew. `gles2_zone_benchmark` now REFUSES to
  run when the achieved resolution != `--res` unless `--allow-mode-
  fallback` is passed, and always writes the ACTUAL width/height into two
  new trailing CSV columns (`actual_width`,`actual_height`) regardless of
  the flag, so a dataset can never again be silently mislabeled. Decision
  logic extracted to a pure, dependency-free helper
  (`docker/irrlicht-drm/drm_mode_select.h`, `eqt_drm::selectDRMMode()`)
  and unit-tested on x86 (`tests/test_drm_mode_select.cpp`, 7 cases
  including the exact P43 400x300-on-Pi scenario) — x86 has no DRM path
  at all, so this is the only way this logic could be verified without a
  Pi; the real `CIrrDeviceFB.cpp` was build-verified only (ARM cross-
  build clean).
  - ~~**RIDE-ALONG ITEM for the next Pi-owning unit**~~ — **DONE (P51,
    2026-08-01)**: ran `gles2_zone_benchmark --drm --res 400 300
    <shipped flags>` (no `--allow-mode-fallback`) on real Pi/DRM hardware.
    (a) process refused, exit code 1 — confirmed. (b) stderr/log shows
    the requested-vs-actual ERROR line naming `2560x1440` as the
    substituted mode, plus the benchmark's own "Refusing to run" line —
    confirmed. (c) the DRM device-creation-time `MODE FALLBACK` ELL_ERROR
    line fires — confirmed; the documented post-init `run()`-time REPEAT
    does NOT fire for this tool, and that's expected/benign:
    `gles2_zone_benchmark`'s own resolution check exits the process
    before it ever reaches `IrrlichtDevice::run()`'s frame loop (it isn't
    a continuous-loop client like `willeq`, which is what the repeat log
    was written for) — only the initDRM()-time line is reachable here.
    Control: the shipped `--res 800 600` invocation (this unit's own H1
    probe run, same session) logged "DRM: Found matching mode
    800x600@75Hz for requested 800x600" with no fallback line, as
    expected. Full logs: `benchmarks/pi_runs/p51_kpi_bisect/befallen_p49_
    ridealong_400x300_refusal.log`. P49 is now fully closed end to end
    (x86 unit-tests + this Pi ride-along).

## P41 landed (2026-07-30, merged `edcd72a`): lighting lever characterized, report-only

- ~~**AWAITING WILL — the lighting decision**~~ — **DECIDED 2026-07-30:
  KEEP PER-PIXEL** (docs/decisions.md 2026-07-30 entry; lightweight
  stays flag-gated for future re-evaluation). Original staging note
  kept for the record: evidence staged at
  refactor/perf_truth_audit.md "2026-07-30 — P41" section (tables +
  options note), stills at /mnt/projects/fable/videos/p41_lighting/
  (manifest: benchmarks/audits/p41_lighting/EVIDENCE_MANIFEST.txt).
  Headline: −15.1%/−22.1% total/GPU at realistic e5 but only
  −6.4%/−11.2% at e205 stress (absolute ~1.3ms GPU savings constant,
  diluted as entity submission grows); visually the doorway pose
  differs on 89.9% of pixels (per-vertex banding on large flat polys
  — the 1999-era signature). Per-pixel is Q03-blessed territory; no
  flip without Will.
- **Swap-floor (audit item 5) narrowed**: eglSwapInterval(0) =
  decisive null (swap_ms 3.805 vs 3.873 control). EGL default swap
  interval ruled out; next hypothesis = the DRM page-flip's own
  waitForFlipComplete() wait in docker/irrlicht-drm/CIrrDeviceFB.cpp
  (named, not proven). Future diagnostic unit if the ~14.4-15.0ms
  wall floor matters.

## P39 landed (2026-07-30): occluder-256 plumbing + corrected framing

- CORRECTION to P38's ranking item 1: the real client ALREADY resolved
  256 by accident (fromPreset baseline; JSON no-op'd the unknown key).
  The silent-16 was (a) any JSON-only config path incl. unit tests,
  and (b) the BENCHMARK: zone_bench_loader hardcodes --occluder-budget
  default 16 and never reads the config — likely the source of P38's
  ~35.3ms "shipped" number. P39 landed the key parse + explicit JSON
  preset entry + resolved-value log + ctest regression on the real
  shipped JSON; Pi spot 36.9→32.5ms p95 tracks P14's bless.
- ~~Same-pattern keys reported NOT fixed: totalMemoryBudgetBytes,
  occlusionBufferWidth/Height (JSON cannot express them) — small
  follow-up candidate. Benchmark default-16 fix = separate small item
  (affects future measurement honesty).~~ **Both CLOSED by P42
  (2026-07-30, merged `4781815`).**

## P42 landed (2026-07-30, merged `4781815`): perf-hygiene bundle, all 4 items

- Closes P38 ranked items 2 (enableMipmaps dead flags — honesty branch,
  dead DRM-only branches simplified, no default change) and 4
  (glTexParameteri state-dedup productionized: per-texture-object param
  cache, 100→1 calls/frame, frames md5-identical) plus both P39
  residuals above.
- ~~**Residual — gate config drift, Will's call someday**: the benchmark
  now correctly defaults occluder budget from ConstrainedRendererConfig
  (256 on orangepi), but scripts/ci/visual_gate.py pins its own
  invocation to --occluder-budget 16 because the blessed goldens were
  captured under 16 and budget-256 occlusion changes visible pixels.
  Options: leave pinned (gate = pixel-regression detector, fine), or
  re-bless goldens under the shipped 256 (needs Will).~~ — **DECIDED by
  Will 2026-08-02: LEAVE PINNED.** The gate's job is pixel-regression
  detection, which it does correctly at 16; matching the shipped preset
  is not worth a golden re-bless round. **The pin is intentional — do
  not "fix" it as drift, and do not re-bless goldens under 256 without
  a new ruling.** Revisit only if a future unit needs the gate to catch
  occlusion-dependent regressions specifically.
- P38's remaining open ranks: 3 (resolution-visibility — REOPENED
  Pi-specific, see P40/P41 entry above), 5 (swap floor — narrowed to
  DRM waitForFlipComplete), 6 (residual GPU-sync — queued behind the
  lighting decision).

## P50 (2026-07-31): ConstrainedTextureCache's public query API doesn't
## account for the P46d masked-variant key mangling — found, not fixed

While closing P46d Part B's two remaining applyPaletteMask plumbing
gaps (`refactor/P50_mask_plumbing_gaps.md`), building evidence
surfaced a separate, pre-existing gap: `ConstrainedTextureCache`'s
public query methods — `hasAlpha(name)`, `getTexture(name)`,
`getAlphaTexture(name)` (by-name overload), `isETC1Texture(name)` —
take only a plain texture `name` and look it up directly in `cache_`.
But P46d's `uploadTexture()` stores masked-variant entries (uploaded
with `applyPaletteMask=true`) under the mangled internal key
`cacheKey(name, true)` = `name + "\x01masked"`, not the plain name. A
plain-name query for a masked-variant entry therefore always misses
(returns "no alpha found" / null) even when the masked texture was
correctly decoded and uploaded.

**Does not affect rendering correctness**: `MaterialType` selection
(the actual alpha-test-vs-solid material choice) is driven directly by
the WLD-derived `textureMasked()` flag at mesh-build time
(`mesh_building.cpp`, `equipment_model_loader.cpp`), never by these
query methods — confirmed by P50's own evidence (the GPU texture's
pixel data IS correctly masked; only the diagnostic query missed it).
No known production caller relies on these methods to detect a masked
variant's alpha for a decision that matters.

Follow-up candidate: either (a) add an `applyPaletteMask` parameter to
each of these four query methods (mirroring `getOrLoad()`/
`uploadTexture()`'s existing pattern), or (b) document that callers
needing the masked variant must pass the pre-mangled key themselves.
Not fixed by P50 (outside its two-gap scope; found only while writing
evidence, not requested).

## P48 landed (2026-07-31, merged `bd0758e`): sheet-alpha recovery + a KPI-regression escalation

- **P37 regress-response option 1 EXECUTED**: masked races composite
  again via sheet alpha companions (27/86 sheets regenerated +
  deployed, backup EverQuest_ETC1_backup_20260731_p48). Pi
  real-roster p95 57.650 → 39.781ms, draws 348.3 → 111.8 — 97.4%
  recovery to the pre-P37 level. ~~AWAITING WILL: masked-composite
  bless~~ — **BLESSED by Will 2026-07-31 ("p48 contact sheet looks
  good"). The P37→P48 masked-entity arc is fully CLOSED**: correct
  masked rendering (P37+P46+P46d+P50), verified by instrument (P47),
  perf recovered (P48), blessed twice (P47 sheet, P48 composite
  sheet) —
  benchmarks/contact_sheets/p48_sheet_alpha/index.html. Known safe
  residuals: DEN default sheet resolves an unmasked variant; CAZ
  variants 1-4 not regenerated (both stay on the P37 exclusion
  fallback).
- ~~**ESCALATION — canonical-replay KPI REGRESSED on fable tip,
  PREDATES P48**~~ — **ATTRIBUTED + RE-BASELINED by P51 (2026-08-01,
  task/P51-kpi-bisect). NOT a production regression — instrument
  honesty.** H1 (P51's prime hypothesis) confirmed on the FIRST probe,
  no bisect needed: tip binary, canonical KPI spot, WITH P46c's
  `--no-entity-shader-promotion` opt-out → 32.250ms p95, squarely back
  in the old blessed band (same draws_avg as the regressed run — pure
  timing, no structural change). Mechanism: pre-P46b the benchmark's
  default-cycle entities rendered on Irrlicht's fixed-function material
  (never linked `entity_renderer.cpp`'s promotion, P44b's finding);
  P46b's `applyEntityShaderMaterialPromotion()` made the benchmark
  correctly mirror what `entity_renderer.cpp`'s `createEntity()` has
  ALWAYS done in production — every entity gets the real per-pixel-lit
  shader. Production code was never touched by this arc; only the
  benchmark's own fidelity gap closed. **Verdict: NO code change (there
  is nothing to fix — production behavior was already correct and
  unchanged throughout). The 32.24-32.5ms band is RETIRED for any
  binary at/after `4386398` (P46b); 34.3-34.6ms is the new canonical
  KPI-guard reference, effective now.** Scaling data point (e5 vs e205,
  4 Pi invocations total): the promotion cost is proportional to
  visible entity draws — negligible (~0.5ms, noise-level) at a
  realistic e5 load, only material (~2.2ms, ~6.9%) at the e205
  stress-test load the KPI guard itself uses. Full re-baselining memo,
  scaling table, and honest-KPI statement for Will in
  `refactor/perf_truth_audit.md`'s 2026-08-01 P51 section. Same-session
  P49 ride-along also closed (see P49's own entry above). CSVs/logs:
  `benchmarks/pi_runs/p51_kpi_bisect/`. Pi budget: 4 timed invocations
  of ≤10, pristine exit (drop_caches recovery from a near-floor CmaFree
  sample, no reboot needed).

## P52 (2026-08-01): GPU-sync characterization — instrumentation landed,
## Phase 2 matrix incomplete, budget exhausted, follow-up needed

Phase 1 (x86 prep) clean: flag-gated per-phase GPU timer-query + CPU-side
sync-point instrumentation (`docker/irrlicht-drm/gpu_sync_probe.h`),
zero-cost-when-off (P38 precedent), wired into both the production
renderer and `gles2_zone_benchmark --gpu-sync-probe`. Full Review in
`refactor/P52_gpu_sync_characterization.md`.

Phase 2 (Pi, after P51 released it) found and reported (full detail:
`refactor/perf_truth_audit.md`'s 2026-08-01 P52 section):

- **Definitive**: Lima has NO `GL_EXT_disjoint_timer_query` support
  (`fns=1, ext_string=0` — the capability probe's dual-signal check
  correctly caught the `eglGetProcAddress` false-positive). Any future
  GPU-side per-phase breakdown on this hardware needs a different
  technique (selective-disable A/B, P41's own method) — GPU timer
  queries are not an option here, full stop.
- **High confidence**: the CPU blocks on `glFinish()` (GPU completion
  wait), not `waitForFlipComplete()`/`eglSwapBuffers()` — swap+flip
  together total ~0.10-0.15ms vs. `glFinish()`'s ~11-90ms. This is a
  **null result for P41's swap-floor hypothesis** (perf_truth_audit
  finding (c)/(e) — `waitForFlipComplete()` named as the next suspect
  for the ~14.4-15.0ms wall-clock floor): on this measurement it's
  negligible, not the bottleneck.
- **Two real methodology bugs found mid-unit**, reported honestly rather
  than silently absorbed: (1) the original 6-invocation matrix used
  `--camera-pose` flags, which route into a structurally separate
  static-pose loop in `gles2_zone_benchmark.cpp` that skips ALL
  per-frame instrumentation (this unit's new code AND the pre-existing
  P08/P38 phase-timing/GL-counter columns) — found, fixed within budget
  by switching to `--path-file` (P51's own convention). (2) the 2-invocation
  fix run omitted the shipped-preset flags (`--merge-zone-draws
  --composite-scenegraph --eqe1-path`) that P38/P41/P51's own canonical
  replays all use — found, **NOT fixed**, budget (≤8, 2 reserve) fully
  exhausted (6 on the invalid matrix + 2 on the Bug-1 fix) before a
  correctly-flagged re-run was possible. The recovery run's own numbers
  (232.7ms avg total at e205, ~7x P51's honest 34.3-34.6ms band) must
  NOT be read as a KPI figure.
- Entity-count sweep (0/5/40/205) and fill-rate isolation (640x480/
  1024x768) — the plan's other two named attributions, and the
  orchestrator's specific ask to reproduce/refine P51's
  +0.455ms(e5)/+2.22ms(e205) entity-promotion curve — were **not
  meaningfully measured** (Bug 1 nulled the original 6-run matrix; Bug 2
  makes the 2-run recovery's entity delta uninterpretable; C/D/E/F were
  never re-run at all).

**Follow-up needed**: a small unit (2-4 Pi invocations) re-running
`A_canonical_e5`/`B_stress_e205` (budget permitting, `C_mid_e40`/
`D_zero_e0`/`E_fill_lo`/`F_fill_hi`) via
`refactor/scratch/p52_pi_gpu_sync_pathfile_fix.py` with the 3 missing
shipped-preset flags added — the only path to a real percentage-of-frame
GPU-sync attribution and a valid entity/fill-rate isolation. Ranked
options + full reasoning in `perf_truth_audit.md`'s P52 section.

CSVs (both the invalidated 6-run matrix, kept for the record with the
`--camera-pose` limitation documented, and the 2-run recovery):
`benchmarks/pi_runs/p52_gpu_sync/`. Pi budget: 8/8 invocations used (all
of the ≤8 ceiling); two SessionAbort-triggering CmaFree episodes (the
documented episodic drain pattern, P02-characterized), both recovered via
`sync && drop_caches`, no reboot, per protocol. Final pristine exit
verified: governor `ondemand` all 4 cores, `/tmp` swept, no
`gles2_zone_benchmark`/`willeq` process, both DRM devices free, final
CmaFree 85,880 kB.

**CLOSED by P52b (2026-08-01)**, `refactor/perf_truth_audit.md`'s
2026-08-01 P52b section — full 0/5/40/205 entity-count sweep on the
correct canonical replay path (`befallen_seed42_1000f.path`, P51's own
fixture — P52b also caught and fixed a *third* methodology bug: its own
starting point, P52's staged `p52_pi_gpu_sync_pathfile_fix.py`, used the
wrong path file, `befallen_fixedpose_300f.path`, which clusters entities
near a near-stationary camera and inflates draws/entities-visible ~3x)
with the full shipped preset. Ranked %-of-frame attribution delivered at
every load: `glFinish` (GPU-completion wait) is the #1 contributor
throughout (45.8%-56.6% of frame), re-confirming P52's structural
finding. The swap-floor question gets a refined answer, not a flat null:
`flip_wait_ms` is 55.2% of frame at e0 (near-tied with `glFinish`) and
falls to 0.7% by e205 — P41's display-cadence floor is real but only
visible near zero load; P52's "null result" was an artifact of never
testing e0. Entity-scaling curve (+34.377ms e0→e205, +33.289ms e5→e205)
brackets P51's own +0.455ms(e5)/+2.22ms(e205) promotion-only deltas as
expected. One open caveat carried forward: `B_e205`'s absolute magnitude
(42.811ms avg/56.420ms p95) still reads ~21-24% over the honest
34.3-34.6ms p95 band even on the corrected path/preset — plausibly a
300-of-1000-frame path-subset effect, not re-diagnosed for lack of
budget (0 reserve left of an orchestrator-extended 6-invocation
ceiling). Flagged as "directionally trustworthy, not KPI-clean" rather
than either suppressed or presented as fully validated. Not blocking:
none of the ranking/swap-floor/scaling-shape conclusions depend on this
specific magnitude. A follow-up unit re-running `B_e205` at the full
1000-frame path (matching P51 exactly) would close this residual
question if it ever becomes load-bearing for a real decision.

## STATE-LAYER SWEEP CLOSED (T39→T49, 2026-08-05) + two new findings from T49's live acceptance

**The sweep is done for the migration.** All four items are now event-fed
via `GameStateSyncBridge` instead of polled once per game tick from
`Application`: pet (T40), inventory (T42), spell (T44), and NPC
interaction — vendor/banker/trainer — (T49).
`Application::syncNPCInteractionState()` and its three `m_last*NpcId`
edge-detect members are deleted. `syncGameStateFromClient()`'s only
remaining sub-sync is `syncCombatState()`, which was never a migration
target: T39 built it as a direct mirror of `CombatManager`, the
authoritative source, and there is no "poll → event" to perform.

**T49 was a fidelity gain, not just plumbing**, and this is the measured
evidence rather than an argument: the deleted poll hardcoded
`setVendor(npcId, 1.0f, name)`, while `VendorWindowOpened` carries the
real `m_vendor_sell_rate` from the merchant packet. Observed end to end
live in both trees — server `ShopRequest response: sell_rate=1.0656` →
`/npcstate` reporting `sellRate=1.0656`. Names now also come through
`EQT::toDisplayName()` (`SummonResupplyAgent` where the poll would have
shown the raw `SummonResupplyAgent003`).

**The state had zero readers before this unit**, which is why T49 also
landed `/npcstate`. Note where it lives and why: **`action::CommandProcessor`
(registry 2), NOT `EverQuest::m_command_registry` (registry 1) where T48's
`/vendor`/`/vendorlist`/`/buy`/`/sell` live.** The two registries read
*different* `state::GameState` instances, and only `Application::m_gameState`
is fed by `GameStateSyncBridge`. Putting the reader next to T48's commands
would have made it report empty forever — the `/assist` (T28) and `cmdHail`
(T39) failure mode exactly. **This is a fresh instance of the open
"~37-command registry overlap" item**: vendor-related commands now
deliberately span two registries. Not a bug; worth knowing before anyone
"tidies" it.

### ~~NEW: `/bank` opens and self-closes instantly~~ — **FIXED by T50 (2026-08-05)**

**Closed.** `/bank` now pushes `BankerInteractIntent{targetId}` for the
current target, exactly as T48 did for vendor, so it enters the gesture's
validated handler and opens with the REAL npc id. Deliberate behaviour
change: `/bank` now requires a targeted banker to open. **The close half
does NOT go through the intent** — that handler is open-only (no close
branch, since the gesture always tries to open) — so `CloseBankWindow()`
is still called directly and never requires a target, because you may
have walked away or changed target since opening.
**Live proof, which also retires T49's caveat: `/npcstate` reported
`Banker: id=194` and held it unchanged across ~8s of movement ticks,
then `none` after close.** Original diagnosis follows.


`/bank` is reachable from the command channel, but useless live: it opens
and closes the bank window within the same processing step, every time,
in both trees.

```
[DEBUG] [INVENTORY] Opening bank window (banker NPC: 1)
[DEBUG] [INVENTORY] Player moved too far from banker (31689.8 > 15.0), closing bank
[INFO ] [MAIN] Bank window closed
```

**The proximity check itself is CORRECT and must stay** (Will,
2026-08-05: bank/train/trade all legitimately require closeness — and the
server enforces its own 200-unit `USE_NPC_RANGE2` on the same
interactions). The bug is purely *which entity it measures against*.

Root cause traced by T49 (not fixed — outside its territory):
`OpenBankWindow()` gets no npc id from `/bank`'s call site and falls back
to `bankerNpcId != 0 ? bankerNpcId : 1` — a **hardcoded dummy id of 1**.
Spawn id 1 is a real, unrelated entity (`zone_controller000` at
`(80000, 240000, -79999)`), so the next movement-tick proximity check
measures 31689.8 units to *that* and closes the window it just opened.
Pre-existing, unrelated to T49. Fixing it touches
`eq_commands_spells.cpp`/`eq_movement.cpp`.

**THE FIX IS ALREADY WRITTEN, ONE PATH OVER — this is the T34/T48 shape
again.** `BankerInteractIntent` (`renderer_intents.h:108`) **has** a
producer (`renderer_overlays.cpp:600`, the renderer gesture) and its
handler (`eq_bridge.cpp:902-940`) does it properly: validates
`npc_type`, checks `NPC_INTERACTION_DISTANCE`, checks
`class_id == CLASS_BANKER (40)`, tells the user why on each failure, then
calls `OpenBankWindow(i.npcId)` **with the real id**. `/bank`
(`eq_commands_spells.cpp:310`) bypasses all of it and calls the
no-arg overload. So the unit is: make `/bank` push
`BankerInteractIntent{targetId}` exactly as T48 made the vendor commands
push the gesture's intents. Same defect, same remedy, precedent set.

### NEW: `/train` sends `HC_OP_GMTraining` and the server never answers

`/train` validates the target (NPC, class 20-35) and calls
`RequestTrainerWindow(target_id)`; the packet is confirmed sent in both
trees against a real guildmaster (npc 1007, `Miltiades_Tal'Azroth`,
class=22, checked against the DB before spawning). **No response ever
arrived** — no `ZoneProcessGMTraining` handler log, no trainer window.
Not investigated (server-side, outside the eqemu ownership T49 was
granted, which covers DB reads and position writes rather than server
debugging). **This is the next live instance of the automation-reachability
pattern**: reachable command, unreachable outcome. T49 reported it rather
than inventing a hook, per its plan. **RESOLVED by T50 (2026-08-05) — see
the closing note at the end of this entry.**

**ROOT-CAUSED 2026-08-05 against the actual server source** (Will's
prompt: "there may be a server-defined max distance"). He was right that
such a rule exists, and the investigation found the real cause is a
*sibling* silent-return in the same handler.

`Client::OPGMTraining()` (`akk-stack/code/zone/client_process.cpp:1612`)
has **three silent `return`s, none of which send the client anything**:

1. trainer missing / not an NPC / class outside `WarriorGM..BerserkerGM`;
2. **cross-class**: `if (!RuleB(Character, AllowCrossClassTrainers))` then
   `trains_class = pTrainer->GetClass() - (WarriorGM - Warrior)` must equal
   `GetClass()`. The rule **defaults to `false`**
   (`common/ruletypes.h:174`);
3. **range**: `DistanceSquared(m_Position, pTrainer->GetPosition()) >
   USE_NPC_RANGE2`, where `USE_NPC_RANGE2 = 40000`
   (`common/features.h:215`) — i.e. **200 units**.

**T49's run died on (2), not (3).** Testchar is **class 1 (Warrior)**;
`Miltiades_Tal'Azroth` is **class 22 = PaladinGM**, which trains class 3
(Paladin). The check at (2) precedes the range check, so distance never
mattered — and `/list` in the same transcript shows every spawned NPC at
`0.0 units away`, confirming proximity was fine. **A Warrior needs a
`WarriorGM` (class 20) trainer.**

**The range rule is real and systemic though**, exactly as Will guessed:
`USE_NPC_RANGE2` gates ~10 interaction handlers in `client_packet.cpp` —
vendor (:2288), banker (:3640), tribute master (:11005) and more. The
banker money handler doesn't even trust the client's npc id; it calls
`entity_list.GetClosestBanker(this, distance)` and range-checks that,
logging a `POSSIBLE_HACK` player event on failure.

**The actionable client-side gap**: `/train` performs target/npc_type/
class-range validation but has **no distance check and no
player-class-vs-trainer-class check**, so the client cheerfully sends a
request the server will silently drop. The banker *gesture* path
(`BankerInteractIntent`, `eq_bridge.cpp:925`) does check distance and
class and tells the user why.

**CLOSED by T50 (2026-08-05).** `/train` now pre-validates both, and the
messages are actionable rather than bare refusals. Captured live against
T49's exact repro NPC:

```
Miltiades_Tal`Azroth004 trains Paladins; you are a Warrior.
You are too far away to train with Brin_Stolunger002.
```

**And the positive leg worked for the first time ever**: against
`Brin_Stolunger` (npc 1063, class 20 WarriorGM, matching Testchar's
Warrior), `/train` completed a real `HC_OP_GMTraining` round trip and
`/npcstate` reported `Trainer: Brin Stolunger (id=195)`.

**Two T50 decisions recorded here because they are live tradeoffs, not
settled truths:**

- **Distance is checked at `NPC_INTERACTION_DISTANCE` (15), not the
  server's 200.** This is deliberately STRICTER than the server: a
  trainer 15-200 units away that the server would happily train is
  refused client-side. Chosen for consistency with every other
  client-side interaction check rather than introducing a third constant.
  Revisit if it ever annoys a real player.
- **The class check is a HARD BLOCK.** `AllowCrossClassTrainers` defaults
  false, so this matches the server today — but **a server that enables
  that rule would find T50's client refusing training the server would
  allow.** No client-side configuration was built for it. That is the one
  known way this fix can be wrong.

Both legs are covered by unit tests against synthetic events instead
(`GameStateSyncBridgeTest.BankWindowOpened*`/`TrainerWindowOpened*`,
`CommandProcessorTest.NpcState_BankerOpen_*`/`NpcState_TrainerOpen_*`), so
the migration and reader are verified even where the live path is blocked.

## `#goto` same-zone teleport lands at the DB position — **SERVER-SIDE, NOT OURS** (T52, 2026-08-05)

T50 observed that GM `#goto x y z` does not land the player where asked
and filed it as a suspected eqemu quirk, without wire evidence. **T52
captured the wire and the verdict is confirmed: the client is faithful,
the substitution is the server's.** Sequence, from
`benchmarks/audits/T52/transcript_goto_primary_excerpt.txt`:

```
[ZONE] Server requests zone change: zone=36 pos=(200.0, 100.0, 5.0) type=1
[ZONE] Requesting zone change to zone 36 at (200.0, 100.0, 5.0)      <- client asks for exactly that
[ZONE] Zone change response: zone=36 pos=(21.9, -77.9, 3.0) success=1 <- server's own approval, DB position
```

`(21.9, -77.9, 3.0)` is Testchar's persisted `character_data` position.
The client requested the correct destination and applied what it was
given. **Nothing to fix client-side; do not add a compensating hack.**

Orchestrator note on how this was scoped, because the reasoning matters
more than the verdict: I first called it server-side from source reading,
Will corrected me (**the server respects whatever position the client
sends — position flows client→server in the overwhelming majority of
updates**), and that reframing was the right prior. It just happens not to
apply *here*, because the substitution occurs inside the server's
zone-change approval, before any client position update. **The wire
capture is what distinguished those two worlds; neither of us could have
from source alone.**

### What T52 fixed anyway (correctness hardening, zero live behaviour change)

Two real but **inert** client-side inversions, both verified inert before
and after in both trees:

1. **The `m_pending_zone_*` consumer** (`eq_login_world.cpp`) claimed its
   input was in server format and swapped x/y a second time. Every
   producer stores **client** format, so the swap was wrong — but the
   block is **dead code**: `is_zone_transition` is always false there,
   since `ProcessDeferredZoneChange` disconnects first. Landing position
   actually comes from the subsequent `PlayerProfile` packet on every
   zone entry, which carries its own correct swap.
2. **`RequestZoneChange`'s outbound packet build** had x/y backwards
   relative to the parse-side convention. Inert because the server never
   reads those fields to choose a destination (unsolicited requests use
   `GetClosestZonePoint` against the server's own tracked position).

**Producer B's convention, documented for the first time**: the zone-line
producer stores targets straight from the WLD-derived zone-line data,
which is already client format (`docs/CLAUDE.md` convention #3), so both
producers always agreed. `tests/test_zone_position_convention.cpp` pins
all three sites so a future edit fails a test rather than a play session.

**Implicated by the same producer/consumer pair and NOT fixed**: succor,
evacuate, and death/bind respawn (`ZoneProcessZonePlayerToBind` — a third,
previously undocumented producer, same correct convention). GM summon is a
separate opcode and untouched. None has a known live symptom.

**Evidence hygiene note**: T52 captured at `-d 6` and produced 121MB of
transcripts against ~350KB for comparable units. Repacked at merge into
excerpts plus gzipped full logs (8.1MB). **Use `-d 5` unless you
specifically need packet dumps, and capture a narrow window if you do.**

## RESOLVED by T60 (2026-08-11): the inventory character preview (3D paperdoll) is restored — was a deliberate, recorded gap (T54, 2026-08-10)

**Retitled, not deleted — history below is load-bearing.** T60 restored
`CharacterModelView` (`include/client/graphics/ui/character_model_view.h`,
`src/client/graphics/ui/character_model_view.cpp`) from `0f25cb57^`,
adapted for ownership by `IrrlichtRenderer` (lazy construction on first
inventory-popup open, never at renderer init) instead of the deleted
`WindowManager`, and wired into `renderInventoryPopup()` via a new shared
rect (`InventoryLayout::paperdollRect()`, T57's pattern) and a new
`UIRenderer::drawTexture()` blit method. The doll is fed the SAME
`EntityAppearance` the player's own in-world 3rd-person entity uses
(`entity_renderer.cpp`'s `isPlayer` branch), including
`compositeAllowed=false` to match the player's per-part (non-composite)
render path — see `refactor/T60_paperdoll_restore.md`'s Review for the
full appearance-source reasoning and render-trigger evidence (markDirty()
on appearance change / popup open / drag-rotation; proven not to render
per frame). See that plan's Review also for a real bug found and fixed in
the process: RTT textures blitted via `draw2DImage()` render upside-down
without a V-flip (GL FBO bottom-left origin vs. `draw2DImage`'s top-left
convention for ordinary images) — fixed in the new `drawTexture()`, does
not affect any pre-existing draw path.

**What the restore did NOT do: reinstate a third copy of the bone-
attachment logic.** The deleted `CharacterModelView` carried its own
`findBoneIndex`/`extractBoneRotation`/weapon-offset copy (a third,
alongside `entity_equipment.cpp` production and `tools/model_viewer.cpp`).
T60 extracted ONE shared implementation
(`include/client/graphics/eq/bone_attachment.h`), verbatim from
`entity_equipment.cpp` (the in-world/production copy — behaviour-
preservation was mandatory there), and pointed all three
consumers at it. Diffing the two surviving pre-T60 copies found real,
previously-undocumented divergence — most notably, production's shield
grip offset is a flat `0.3` constant, NOT the bounding-box-derived
`bbox_width * 0.175` the deleted paperdoll and the tool's initial
placement both used; the shared header keeps production's actual (flat)
formula, so the restored paperdoll's shield grip differs slightly from
what the pre-0f25cb57 paperdoll would have shown, but matches what the
player already sees in 3rd person today. See the plan's Review for the
full diff and the new `tests/test_bone_attachment.cpp` (17 cases,
including two that pin genuine quirks in the fallback chain: a case-
sensitivity bug that makes the `_DAG`-suffix-stripped fallback
unreachable for lowercase `"_dag"` suffixes, and a "lowercase" fallback
variant that is actually mixed-case because the stripped substring is
never re-cased). None of these quirks were changed — only pinned, per the
plan's explicit behaviour-preservation requirement for the in-world path.

**Still open, now with an instrument to diagnose it:** the held-item
misalignment Will reported (equipped items not always appearing correctly
in a character's hand) was explicitly OUT of scope for T60 — see the
paperdoll capture screenshots in `refactor/artifacts/T60/` (both x86/
llvmpipe and real Orange Pi/Mali400), which show it directly: an equipped
primary weapon renders as an untextured white blob near the character's
hand rather than a recognizable weapon model. Previously this could only
be debugged by comparing production (in-world, hard to inspect closely)
against the tool (which had ITS OWN divergent bone-lookup copy, so fixing
something there didn't reliably predict production). Now both consume the
identical shared implementation, and the paperdoll gives a third, always-
visible vantage point directly in the UI. Fixing the misalignment itself
is a future unit's work.

`CharacterModelView` — the 3D character-model preview panel inside the old
inventory window, showing your equipped appearance while you managed
items — was deleted along with the rest of the old `WindowManager`/
`WindowBase` UI tree in `0f25cb57` ("U07c2: Delete WindowManager + all old
UI window files (~35K lines)", 2026-03-15) and was **never reimplemented**
in the static-panel UI that replaced it (`include/client/graphics/ui/`,
`static_panels.h`'s `renderInventoryPopup()` — equipment/general slot
icons only, no 3D preview surface at all).

Until T54, the only trace this feature had ever existed was six
unconditionally-`GTEST_SKIP()`'d `InventoryModelViewTest` cases in
`tests/test_inventory_model_view.cpp`, added the same commit that deleted
the feature. T54 deleted that fixture (see the re-titled entry above) as
part of cleaning up dead-fixture test debt — which would have erased the
last record of the gap entirely. **This entry exists so the intent is not
lost**: Will asked explicitly that removing the skip-only tests not also
erase the memory that a paperdoll preview once existed and was removed by
design (a UI-rewrite scope cut), not lost by accident.

What would be needed to rebuild it: a render-to-texture or dedicated
viewport surface hosting an `EQAnimatedMeshSceneNode` (built via
`RaceModelLoader::createAnimatedNodeWithEquipment()`, which — see T54's
`CreateAnimatedNodeWithEquipment_Headless` test — already handles body/
armor texture variants correctly but does NOT attach weapon geometry to
the returned node; weapon attachment lives entirely inside
`EntityRenderer` via a separate `EquipmentModelLoader` mesh lookup and
would need its own path here), composited into `centerPopup`'s inventory
region alongside the existing equipment-slot grid. Not scoped or started
by this unit — this is a feature-loss record, not a design.

## NEW: static-panel UI surface still untested after T54 (2026-08-10)

T54 added the first-ever tests for the static-panel UI
(`tests/test_static_ui_panels.cpp`: `StaticUIInput::processInput()` hit-
testing for the hotbar, spell gems and inventory popup;
`StaticUIInput::processKey()`'s ESC-closes-popup path; one `EDT_NULL`
`renderInventoryPopup()` smoke test). Being honest about what's still
uncovered, since "first tests exist" easily gets misread as "now tested":

- **Spellbook and skills popup hit-testing** (`static_ui_input.cpp`'s
  `spellbook.isOpen`/`skills.isOpen` branches — row hover, page
  navigation, scroll) — zero coverage. Same file, same idiom as the
  inventory tests T54 added; not done for scope/time, not because it's
  harder.
- **The spellbook memorize click is a known no-op**: clicking a spell row
  in `static_ui_input.cpp`'s spellbook branch only `LOG_INFO`s the click
  — "would need MemorizeSpellIntent" is a comment already in the
  production code (not a T54 finding, but T54's test-writing surfaced it
  again while reading the file end-to-end; recorded here since nothing in
  this board named it before).
- **Every other panel's render function is untested**: `renderPlayerStatus`,
  `renderTargetInfo`, `renderChatPanel`, `renderHotbar`,
  `renderSpellGemPanel`, `renderBuffBar`, `renderCastingBar`,
  `renderGroupPanel`, `renderPetPanel`, `renderXPBar`,
  `renderSpellbookPopup`, `renderSkillsPopup` — only `renderInventoryPopup`
  got the optional `EDT_NULL` smoke-test treatment (plan Step 3's explicit
  "optional tail", done because time remained after the input tests were
  green).
- **A real layout-sizing defect T54's boundary tests found and left
  unfixed** (production code was read-only territory this unit):
  `InventoryPanelState`'s general-inventory row (8 slots) is **entirely
  unreachable** by mouse at the default 800x600 layout —
  `centerPopup` is only 350px tall but the equipment grid (11 rows ×
  34px + 22px header + 8px gap) needs 382px before the general row even
  starts, so `genY` (529) lands 54px below `centerPopup`'s bottom edge
  (475) and the outer popup-bounds check in `processInput()` rejects every
  point in that row before the per-slot hit test ever runs. The bottom two
  rows of the 22-slot equipment grid are partially/fully unreachable for
  the same reason. See
  `StaticUIInputTest.InventoryGeneralSlotsAreWhollyUnreachable` and
  `.InventoryEquipGridRow10IsUnreachable` in
  `tests/test_static_ui_panels.cpp` for the exact geometry and reasoning.
  Fix would be either shrinking `UILayout::centerPopup`'s slot math or
  growing `popH` — production-code territory, out of scope here.
  - **ORCHESTRATOR CORRECTION (2026-08-10, at T54 merge review) — this is
    worse than the entry above states, on two axes. Verified directly, do
    not re-derive:**
    1. **It is NOT resolution-dependent.** `popW`/`popH` are hardcoded
       `400`/`350` at `include/client/graphics/ui/ui_layout.h:131-132`,
       independent of `width`/`height` — only the popup's *origin* is
       centered. So the general row is unreachable at **every**
       resolution, not just "the default 800x600 layout". The 800x600
       framing above is an artifact of the geometry T54 happened to test
       at; it reads as a small-screen edge case, and it is not one.
    2. **It is a VISIBLE defect, not merely an input-dead one.**
       `renderInventoryPopup` (`src/client/graphics/ui/static_panels.cpp:279-280`
       and `:304`) computes `startY`/`genY` with **identical** arithmetic
       to the hit test — so those slots are *drawn* at coordinates
       outside `centerPopup`'s own background panel, i.e. the general row
       and the overflowing equipment row render outside the popup frame
       they belong to, over whatever is behind them. The render path and
       the hit-test path agree with each other and both disagree with the
       panel size; that shared constant is where a fix belongs, so the
       two cannot drift apart while being corrected.
    Consequence for scoping: this is a user-visible UI bug affecting all
    8 general inventory slots plus equipment slots 20-21 in every
    session, not a latent boundary-condition wart. It has presumably been
    live since the static-panel UI replaced the deleted windows —
    unnoticed because, until T54, nothing tested or exercised this
    surface at all. **Worth a unit; needs Will's visual acceptance since
    it changes on-screen layout.**
- **`UIAtlas`-backed rendering** (the non-fallback path — real sprite atlas
  loaded, not the null-atlas immediate-draw fallback T54's smoke test
  exercises) is untested for every panel, static UI or otherwise.

### T55 (2026-08-10) addendum — the "implicated" bullet above is RE-TITLED: convention worry CLEARED, real defect was propagation

The Index bullet and the "Implicated ... and NOT fixed" paragraph above
framed succor/evac/death-bind as sharing T52's *convention* bug (the
double x/y swap). **T55 checked this directly and it does not hold**:
succor and evacuate are client-side no-op stubs (`spell_effects.cpp:274-278`,
`:783-800`; `SpellEffect::Succor = 88` has no case at all) — all succor/evac
position work is server-driven through producer A
(`ZoneProcessZoneChange`, `eq_zone.cpp`), which T52 already fixed. Death/bind
(`ZoneProcessZonePlayerToBind`, `eq.cpp:3392`, opcode `HC_OP_ZonePlayerToBind
= 0x385e`) swaps identically and correctly, confirmed here with its own new
test (`ZonePositionConvention.ProducerC_SwapsWireOffsetsIntoClientFormat`,
`tests/test_zone_position_convention.cpp`) — T52's fix silently corrected
this producer's outbound packet too. **There was nothing left to fix on the
convention axis.** `ZoneProcessZonePlayerToBind` is added here, explicitly,
as producer C to the documented producer list (A = `ZoneProcessZoneChange`,
B = `CheckZoneLine`, C = `ZoneProcessZonePlayerToBind`) alongside producers A
and B above.

**The real defect T55 found and fixed is a different one: propagation.**
`EverQuest::SetPosition(x,y,z)` (`eq.cpp`) — the T39-era helper that writes
both the raw `m_x/m_y/m_z` members and the state layer's
`m_game_state.player()`, firing `PlayerMoved` — had **zero production
callers** anywhere in `src/`; only tests called it. Every forced-relocation
production site instead wrote the raw members directly (or, in death/bind's
case, wrote *nothing at all* — see the same-zone finding below) and never
touched the state layer. This is the exact T39 pattern ("setters were
written but never wired, while readers were repointed at the new home
anyway") surviving live, in the position path, after the T39-T49
state-layer sweep was **declared CLOSED** (`project-state.md`, 2026-08-05)
with this hole still open. T55 fixed it at the three forced-relocation
sites in its territory: the zone-in consumer (`eq_login_world.cpp`,
`WorldProcessZoneServerInfo`), death/bind (`eq.cpp`,
`ZoneProcessZonePlayerToBind`, both branches), and the pending-zone
WorldState publish for producers A and C (mirroring producer B's existing
`setPendingZone()` call). See `refactor/T55_position_state_propagation.md`
Review for the pre/post test evidence, including the CONFIRMED same-zone
death-respawn finding (before the fix, the client kept showing the
character's pre-death position with no local write at all, relying
entirely on an eventual server `PlayerProfile` round-trip).

**What remains, explicitly by decision, not oversight**: T55's scope was
held to forced relocation only. The per-frame movement writes
(`eq_movement.cpp`, `eq_entities.cpp`, `eq_bridge.cpp`,
`eq_commands_movement.cpp`) are still raw and unconverged — routing hot
movement through `SetPosition` would fire `PlayerMoved` per frame, an
event-storm/perf question needing its own investigation with measurements.
Also left alone: the `PlayerProfile` handler (`eq.cpp:2154-2156`), which
still writes only the raw `m_x/m_y/m_z` members and pushes `PlayerMoved`
directly onto the **GameStateBridge** (not through `SetPosition`, and not
onto the state layer's `PlayerState`/EventBus at all) — outside T55's
named territory (limited to `SetPosition` and
`ZoneProcessZonePlayerToBind` within `eq.cpp`), and deliberately not
touched given the login-path ordering risk the unit plan flagged.

### T58 (2026-08-11) addendum — the `PlayerProfile` gap left open above is CLOSED; per-frame movement writes remain the only unconverged piece

T55 flagged, and deliberately left alone, `ZoneProcessPlayerProfile`
(`eq.cpp:2154-2188`) — the packet that determines landing position on
*ordinary* zone-in, still writing only the raw `m_x/m_y/m_z/m_heading`
members with no state-layer write. T58 traced the risk T55's own plan
called out by name — that swapping the raw writes for `SetPosition`/
`SetHeading` while leaving the existing hand-rolled `m_bridge->pushEvent`
publish alone would double-publish `PlayerMoved` to the renderer — and
found it **does not apply**: `EverQuest::m_game_state`'s own state-layer
`EventBus` (the one `SetPosition`/`SetHeading` fire `PlayerMoved` on) has
**zero production listeners** and is never forwarded to the
`GameStateBridge` the hand-rolled push feeds. They are two genuinely
independent queues (see `refactor/T58_playerprofile_propagation.md`
Review's trace table for the full evidence chain: `PlayerState::
firePlayerMovedEvent` → `EverQuest::m_game_state`'s private `EventBus`,
vs. `EverQuest::m_bridge` → `GameStateBridge`/`GameStateSyncBridge` →
`IrrlichtBridge`/`ConsoleBridge`, wired from two entirely separate
`state::GameState` instances — `EverQuest::m_game_state` internal-only,
`Application::m_gameState` the one `ConsoleRenderer`/`AutomatedMode`
subscribe to). Fixed by routing `ZoneProcessPlayerProfile`'s position/
heading writes through `SetPosition`/`SetHeading`, unchanged x/y swap and
heading math, hand-rolled bridge publish left exactly as-is. Confirmed by
test: exactly one `PlayerMoved` still reaches the bridge per zone-in
(`ZonePositionConvention.PlayerProfile_ExactlyOnePlayerMovedReachesBridgeInBridgeHeadingFormat`),
in the same 512-scaled heading format as before.

**What remains, explicitly by decision, not oversight**: the per-frame
movement writes (`eq_movement.cpp`, `eq_entities.cpp`, `eq_bridge.cpp`,
`eq_commands_movement.cpp`) are the one piece of the position path T55
scoped out and T58 did not touch either (out of both units' named
territory) — still raw and unconverged, still an event-storm/perf question
needing its own investigation before routing hot movement through
`SetPosition`.

## RESOLVED by T62 (2026-08-11): held-item misalignment was a test-tooling defect, not a production bug — T60's "untextured white mass" repro traced to its root cause

**Retitled, not deleted — T60's "Still open, now with an instrument to
diagnose it" entry above is superseded by this finding.** T62 used the
paperdoll instrument T60 built to isolate the reported held-item
misalignment (weapon/shield equip a huge, untextured mass instead of
appearing held) and found it was **not a defect in the shared
`bone_attachment.h` grip math or in production's in-world rendering
path**. Two independent mistakes in the test/debug tooling fully explain
what T60's captures showed:

1. **T60's capture equipped database item 1000, believing it was a
   weapon.** `data/willeq.db` maps item 1000 to equipment model IT64,
   whose only texture is `sack1.bmp` — a sack/container model, not a
   weapon. Its odd, torso-sized silhouette is exactly what the capture
   showed, because it genuinely is not weapon-shaped.
2. **`ui_inventory_capture.cpp` never called
   `equipmentModelLoader.setConstrainedTextureCache()`** (it wired the
   cache for `raceModelLoader` only). `EquipmentModelLoader::
   buildMeshFromGeometry()`'s texture-upload branch is gated entirely on
   that pointer being non-null, with no fallback — so every equipment
   mesh the tool built was untextured regardless of which item was used.
   `tools/model_viewer.cpp` had the identical gap (no
   `ConstrainedTextureCache` anywhere in the file). **Production
   (`EntityRenderer::setConstrainedTextureCache`, wired from
   `IrrlichtRenderer` init) already wires this correctly for the in-world
   path** — this was never a live-game defect.

Both tools were fixed (`ui_inventory_capture.cpp`, `model_viewer.cpp`).
With a real weapon (IT1 sword, IT45 staff) and a real shield (IT202) and
texture wiring fixed, all render correctly textured, correctly scaled,
and gripped in the hand — using the shared `computeWeaponGripOffset`/
`computeShieldGripOffset` formulas **completely unmodified** from what
T60 converged (verified: `git diff include/client/graphics/eq/
bone_attachment.h` against this unit's changes is empty). Real measured
mesh dimensions (IT1: 2.746 units, one-handed branch; IT45: 5.887 units,
two-handed branch) validate the existing `3.0f` threshold rather than
contradicting it. The shield-offset divergence T60 documented (production's
flat `0.3` vs the deleted paperdoll/tool's `bbox width * 0.175`) was
re-tested with real shield data: visually near-identical for a
representative shield, so production's flat `0.3` was kept, unchanged.

**Consequence: in-world entities were never affected**, and this unit's
STOP condition ("in-world placement would change and you cannot produce
before/after evidence") did not trigger, because in-world placement
provably did not change — nothing in `entity_equipment.cpp` or
`bone_attachment.h` was touched.

Also folded in: `CharacterModelView`'s default rotation changed from
`0.0f` (profile, facing right) to `90.0f` (front-facing, facing the
camera) per Will's request — drag rotation is relative and unaffected.

See `refactor/T62_held_item_alignment.md`'s Review for the full
investigation, measured dimensions, and before/after capture paths
(review media at `/mnt/projects/fable/review/T62/`, not committed to the
repo, per the coordinator's mid-flight protocol update moving Will-facing
review media out of `refactor/artifacts/`).

**What remains, explicitly by decision, not oversight**: no
`EntityRenderer`-level in-world integration test exists (T60's honest-
limitation note, unchanged — out of scope here since `entity_equipment.cpp`
was not touched). If a genuine in-world held-item misalignment is
reported again in the future, it is not this defect — re-investigate from
scratch rather than assuming this finding covers it.

## Candidate follow-up: on-demand `/record start|stop` capture (T64, 2026-08-11, not chased)

T64 extracted the whole-session `--dump-frames-dir`/`--dump-every` video
capture flag (see the "Live x86 client bugs (anim-video-capture session)"
entry above). That mechanism is a session-lifetime flag set at launch — fine
for a planned capture, wasteful/impossible for a live session where only a
few seconds around an unplanned moment (a bug reproducing mid-session, an
interesting emergent behavior) need recording. A slash command
(`/record start`, `/record stop`) that toggles `Application`'s existing
dump-frame state at runtime (same render-thread hook, same bound) would let
whoever's driving a live session decide in the moment, without restarting
the client or guessing capture windows in advance. Deliberately out of
scope for T64 (extraction only, per its contract) — worth its own unit if
someone wants it. Would need: a command-processor entry, some way to name/
version successive recordings without overwriting, and a decision on
whether `--dump-max-frames`'s bound applies per-recording or session-wide.
## T63 (2026-08-11) live equipment verification — primary/shield render correctly; secondary-weapon-on-paperdoll does not; first-person camera unreachable from script

Live eqemu session (Testmelee, Dark Elf female, `arena` zone) settled the
question T60/T62 explicitly left open (Will's in-game report of items not
appearing properly held) with a real character holding real, resolved
items in a real session — full detail, item→model tables, and log
evidence in `refactor/T63_live_equipment_render_verification.md`'s Review
and `/mnt/projects/fable/review/T63/README.md` (media).

**Verdicts, per configuration (paperdoll evidence)**:
- Primary weapon only: **renders correctly** — gripped in right hand.
- Secondary weapon only: **defect** — never visibly renders (see below).
- Dual wield: primary renders correctly; secondary reproduces the same
  defect.
- Shield in secondary: **renders correctly** — on the arm/torso, not
  gripped in the hand; `SHIELD_POINT` resolved (bone index 11, distinct
  from `L_POINT`'s index 10 on the same character) — **no fallback to
  L_POINT** for this race, and a `wld_dump` of `global_chr.s3d` found a
  `*SHIELD_POINT_TRACKDEF` bone for all 25 playable race codes checked, so
  the fallback risk this unit was asked to characterise does not appear to
  exist for standard playable races in that file.

**Key architectural finding**: the live wire protocol's spawn equipment
slot carries a server-resolved material id (e.g. `7` for idfile `IT7`),
never the database item id (e.g. `6011`) — confirmed by log, not assumed.
`EquipmentModelLoader::getEquipmentMesh()`'s "try `item_models.json` as an
item-id map first" branch is therefore dead weight for live player
equipment; it always misses and falls through to using the value directly
as a model id, which is what's actually correct. Separately,
`data/item_models.json` itself was found stale/disconnected from the live
server's real item database (497/1863 sampled weapon/shield items had any
entry, only 3 of those matched the item's real idfile) — does not cause
any defect found in this unit (the live path never calls it with a real
item id) but is objectively wrong data if anything else reads it; worth
its own audit unit if that's ever suspected.

**Defect — needs its own fix unit**: on the paperdoll (`CharacterModelView`),
a plain (non-shield) secondary/left-hand weapon never visibly renders,
reproduced independently in two configurations (secondary-only and dual
wield) with identical bone index (10, `L_POINT`) and offset (0.51) both
times, despite the log confirming mesh build, texture load, and bone
resolution all succeeded — this is not a "not found" failure. The shield
in the same slot (different bone, `SHIELD_POINT` index 11) and the primary
weapon (bone index 6) both render fine. Flagged (not confirmed) candidate:
`character_model_view.cpp:549-552` / `entity_equipment.cpp:233-236` apply
rotation `(180,0,0)` to non-shield secondary items vs `(180,0,180)` for
shields — the shield-only extra Z rotation may be what keeps it outside
the body mesh while the plain-weapon rotation points its offset into/
behind it. Needs a controlled A/B to confirm, not asserted here. Whether
this also affects in-world (non-paperdoll, 3rd-person) rendering is
**not established** — `entity_equipment.cpp` shares the logic, but the
`arena` zone's dark default lighting made 3rd-person confirmation
impossible in this session.

**STOP, not delivered**: first-person-during-swing captures. No slash
command or renderer-bridge command wraps `ToggleCameraMode`
(`cycleCameraMode()`) — it is reachable only via a real Ctrl+F5 key event,
which `--script` cannot send. Per the plan's explicit instruction, no
scripting affordance was added to production code; this is a harness gap
for any future unit needing camera-mode control from `--script`, not a
rendering defect. Read-only correction found while chasing this:
`triggerFirstPersonAttack()`/`updateFirstPersonWeapons()` are near-no-op
compatibility stubs, not a separate first-person weapon system — actual
first-person weapon rendering reuses the same bone-attachment path already
verified here for 3rd person/paperdoll.

No production code was modified by this unit (verification-only,
`git status` clean throughout). Board entries above are the closure for
Will's open question: primary and shield equipment render correctly in a
live session; secondary-weapon-only does not, characterised precisely
enough to scope a fix unit without re-running the live session.

## `protectedRegions_` bitvector is permanently all-false in production (T67 side-finding, 2026-08-11) — census done by T79 (2026-08-12), NOT removed, decision deferred

- Found while removing `ConstrainedMeshCache` (see
  `refactor/T67_remove_constrained_mesh_cache.md`): `protectedRegions_`'s
  only writer anywhere in the codebase was `renderer_sim_bridge.cpp`'s
  `applySimulationResults()`, inside an `if (constrainedMeshCache_) {...}`
  block that never ran (the cache was never constructed). T67 removed that
  dead block; `protectedRegions_` was already permanently all-`false` in
  every shipped build before this, and remains so after (no behavior
  change from T67 itself — this is a pre-existing condition T67 merely
  made legible by deleting the last thing that made it look conditional).
- `protectedRegions_` still has live, non-cache readers in
  `renderer_zone_stream.cpp`'s `processFrameProgressiveLoad()`: Priority 3
  (door build, `doorManager_->getDoorsInRegions(protectedRegions_, ...)`)
  and Priority 4 (PVS object build, `protectedRegions_[obj.bspRegion]`
  gates). Since the vector is always all-false, both priorities are very
  likely already-dead in production for a reason **unrelated to**
  `ConstrainedMeshCache` — but doors and objects are also both built
  eagerly elsewhere (SEQ Step6/7 in `renderer_zone_load.cpp`, "Build all
  object meshes eagerly inline (no deferred/PVS-gated build at runtime)"),
  so this may simply be more redundant-but-harmless dead code rather than
  a functional gap. **Not confirmed, not chased** — out of T67's scope
  (its territory was `constrainedMeshCache_` references specifically, and
  this is a step removed from that). Whoever next touches
  `processFrameProgressiveLoad()`'s Priority 3/4 should check whether
  `protectedRegions_` is worth resurrecting (e.g. repurposed from the
  eager SEQ-step region-visibility data) or whether Priority 3/4 are
  themselves dead code that should follow the mesh cache out.
- **T79 (2026-08-12) resolved the "not confirmed, not chased" question above
  and confirmed the harder case, not the easy one.** All 5 read sites
  (`renderer_zone_stream.cpp:494,537,547,755,765`, all inside
  `processFrameProgressiveLoad()` and `checkProgressiveLoadingComplete()`)
  are live, reachable production code — not test-only, not behind any
  cache-related gate — and every one of them is a permanent no-op because
  `protectedRegions_` is always empty/false: Priority 3 (door build) and
  Priority 4 (object build) loop bodies never execute, and the
  `unbuiltDoors`/`unbuiltObjects` progressive-load status counters are
  permanently pinned to 0 regardless of actual pending state. **Left
  in place, not removed** — per the plan's own framing, live consumers
  that used to read a real signal are exactly the case where deleting the
  bitvector would be inventing a decision ("these consumers should also
  go") that isn't this unit's to make; STOP condition invoked for the
  removal question specifically. What T79 *did* do: added inline comments
  at all 5 read sites (was previously only documented at the declaration)
  making the always-false status and its consequence explicit at the point
  of use, so the next reader doesn't have to re-derive it. Two real open
  questions for whoever picks this up next: (1) doors/objects are also
  built eagerly at zone load (SEQ Step6/7), so whether the Priority 3/4
  dead paths are a functional gap depends on whether anything ever needs
  the progressive/PVS-triggered fallback that eager build doesn't already
  cover — not established either way; (2) if Priority 3/4 are kept, they
  need a real protection/PVS signal to replace `protectedRegions_` (T79
  did not invent one — out of scope); if the eager build is judged
  sufficient on its own, Priority 3/4 and the two status counters should
  be removed together with `protectedRegions_` rather than left as dead
  camouflage. Visual gate confirmed unaffected (expected — no behavior
  changed, only comments added). See
  `refactor/T79_tooling_cleanup.md` Review for the full consumer table.

## `check_file_sizes.sh` has no handling for deleted files in the diff (T67 finding, 2026-08-11) — **FIXED by T79 (2026-08-12)**

- `scripts/refactor/check_file_sizes.sh` (`refactor/EXECUTOR_PROTOCOL.md`'s
  verification-bar step 6) computes its file list from
  `git diff --name-only $BASE -- '*.cpp' '*.h'`, which includes files
  deleted since `$BASE`. For each, it then does `[ -f "$path" ] || ...`
  and prints `ERROR $f: file not found ... — pass absolute paths or run
  from the target worktree` with exit status 2 — worded as if the caller
  made a mistake, when the actual cause was a legitimate deletion in the
  diff. T67 deleted `constrained_mesh_cache.h`/`.cpp` and hit exactly
  this: 2 spurious "file not found" lines alongside 19 genuinely-checked
  files, `0 fail` in the real summary line, but a nonzero exit code that
  a caller not reading the output closely could mistake for a real
  regression. Also independently confirmed while running this same step:
  the script's `repo_root="$(git rev-parse --show-toplevel 2>/dev/null ||
  pwd)"` has no `-C <path>` anchor, so invoking it via `bash
  <slot>/scripts/...` from a shell whose cwd is NOT inside that slot
  (e.g. the Bash tool's ambient cwd) silently resolves the wrong repo
  root and reports "base revision does not resolve" even though the
  revision is fine — it just checked the wrong directory. Both are
  script-only findings; no production code involved. A future
  units-infra pass could special-case a deleted path as
  `del $f: removed (not checked)` instead of an ERROR, and add `-C`
  support (or a `--repo <path>` flag) so the script doesn't depend on
  ambient shell cwd. Not fixed here (out of T67's scope).
- **FIXED by T79 (2026-08-12):** both defects landed in one script edit.
  Deleted paths are now excluded via `git diff --diff-filter=d` before the
  per-file check ever runs (no more spurious "file not found" for a
  legitimate deletion). The cwd-anchor problem is fixed by anchoring
  `repo_root` to the script's own on-disk location by default (it lives at
  `<repo>/scripts/refactor/check_file_sizes.sh`, so its grandparent's
  parent is the repo), with an explicit `-C`/`--repo <path>` override for
  callers that want a different tree. Verified by running it exactly as
  `refactor/EXECUTOR_PROTOCOL.md` documents — `bash
  <slot>/scripts/refactor/check_file_sizes.sh fable` — from an ambient cwd
  OUTSIDE the slot (`/mnt/projects/fable`), no wrapper, no `cd`/`env -C`/
  absolute-path workaround: it correctly resolved `repo_root` to the slot
  and reported the slot's real diff against `fable`. Also verified the
  deleted-file case directly (temporarily deleted a real tracked header,
  confirmed it's silently excluded from the check rather than reported as
  an error, then restored it). See `refactor/T79_tooling_cleanup.md`
  Review for the transcript.

## Spell-effects rewrite arc, step 2 of N: `spells.eff` reader landed, the data-driven visual layer is still all ahead (T70, 2026-08-11)

- T68 (step 1) got spell *mechanics* data into `SpellData::effects`. T70
  (step 2, `refactor/T70_spells_eff_reader.md`) added a validated,
  index-by-`SpellAffectIndex` reader for the client's *visual* effect
  definitions (`spells.eff`) — `include/client/spell/spells_eff_reader.h`
  + `src/client/spell/spells_eff_reader.cpp`, 18 tests, no rendering wired
  up (deliberately out of scope, per the plan's own emphasis #1).
- What remains is the bulk of the arc: a data-driven visual layer that
  actually consumes this reader to replace the current 10-resist-type x
  5-school hardcoded vocabulary (`spell_visual_fx.cpp:190-219`,
  `:879-918`) with per-spell effects sourced from `spells.eff` records via
  `SpellData::spell_affect_index`. That includes deciding what to render
  for the ~800 unknown bytes per emitter block (T70 proved structure, not
  semantics — see its Review) and what to do for the 148 real spells
  whose `spell_affect_index` resolves to an empty padding record (T70
  Review: indices 70, 71, 80, 84, 88 — likely answer is "fall back to the
  existing resist/school default," but that is a rendering-unit decision,
  not made here).
- Separately, and explicitly out of scope for T70 (different file,
  different format, different consumer — folding it in would have muddied
  T70's evidence): `tools/model_viewer_spell_bar.h:274` hardcodes
  `ENTRY_SIZE = 256` for **`spellsnew.eff`**, where the correct value is
  **268** (`112024 / 268 = 418` exactly; `256` gives `437.59` and drifts).
  This is why that tool carries "skip entries that look like fragments"
  logic — the fragments are drift artifacts from the wrong stride, the
  same class of bug T70's plan diagnosed in `48ae032b`'s `spells.eff`
  loader. Not touched by T70; flagged here for whichever future unit picks
  up `spellsnew.eff`.
  **FIXED by T79 (2026-08-12):** `ENTRY_SIZE` corrected to 268, and the
  "skip fragments" workaround it motivated removed — confirmed a no-op at
  the correct stride (0 of 418 records skipped). Entry counts measured
  against the real `spellsnew.eff` (112,024 bytes): before (256, with the
  fragment-skip hack) 437 records, 22 "kept" (mostly garbled fragments like
  `"P\xef\xbf\xbd"`, `"keLight85."`, `"106 V2"`); after (268, hack removed)
  418 records, 416 kept (2 filtered as too-short/"None"), whole clean names
  throughout (`"Clinging Darkness"`, `"Frost Breath"`, `"Rain_Fall"`, etc).
  See `refactor/T79_tooling_cleanup.md` Review for the full before/after
  sample.
## Memory categories with no configured budget (T69, 2026-08-11) — Will's ruling needs a follow-up per-category

- T69 (`refactor/T69_memory_enforcement_gate.md`) wired
  `IrrlichtRenderer::getMemoryReportBreakdown()`'s per-subsystem byte tallies
  into `scripts/ci/visual_gate.py` via `gles2_zone_benchmark.cpp`'s
  `--memory-json`. Will's ruling was per-subsystem budgets, defined and
  enforced, no global cap. T69 could only **enforce** the two categories
  that already have a real configured ceiling in
  `ConstrainedRendererConfig`: `textureMemoryBytes` (texture cache, H02) and
  `framebufferMemoryBytes` (framebuffer, new in T69). Every other category
  the gate now measures has **no configured budget at all** — T69 could
  only baseline-guard them (exact-match regression detection against a
  blessed value, zero tolerance measured from 9 repeat runs across all 3
  golden zones, see the T69 Review), which catches drift but not "is this
  too much." This is the concrete list for Will to rule on next, same shape
  as the texture-cache/framebuffer precedent:
  - `zoneGeometryBytes` (region/single-mesh vertex+index bytes)
  - `placeableObjectsBytes` (currently 0 for the golden zones — none load
    `--objects`)
  - `s3dZoneSourceBytes` (CPU-side S3D geometry/BSP/animation data retained
    for mesh rebuilds)
  - `gpuTextureBytes` (actual GPU-side texture allocations — related to but
    distinct from the texture-cache budget, which only bounds the
    *cache's* accounting, not total GPU texture memory including
    non-cached driver textures)
  - `vboEboBytes` (GPU hardware buffers)
  - `eqe1MappedBytes` (EQE1 flat pack mmap pool — currently 0 in the gate's
    runs since the manifest's benchmark invocation doesn't pass
    `--eqe1-path`; a real budget question here is coupled to whether that
    changes)
  - `characterModelsBytes` (RaceModelLoader total — currently 0 in the
    gate's runs since none of the 3 golden zones load entities; only
    populated when a future zone config does)
  - Not measured by the gate at all, and so not even baseline-guardable
    today: entity per-instance data, equipment models, HCMap/collision,
    audio (SFX/sound-buffer/soundfont/music/PFS), network, door/spell-DB
    game-data estimates. `gles2_zone_benchmark`'s `ZoneBenchData` never
    constructs these subsystems (no collision map load, no AudioManager,
    no game-state entities in the golden zones' plain-geometry runs) — see
    `tools/hwcaps/zone_bench_memory.cpp`'s block comment for the full
    reuse-vs-not breakdown. Getting CI coverage on these would need either
    the benchmark tool to construct more of the subsystem graph, or a
    live-client-based memory harness distinct from this offline benchmark.

  **T80 update (2026-08-12) — "no configured budget" and "not exercised by
  the gate" were conflated above; they are different problems with
  different fixes, and this paragraph separates them.** T80
  (`refactor/T80_memory_gate_coverage.md`) added a SEPARATE memory-coverage
  benchmark invocation (`--eqe1-path`/`--objects`/`--entities`/
  `--test-races`, never added to the golden-image runs — see that plan's
  Review for why) that exercises three of the categories this entry called
  "currently 0": `eqe1MappedBytes`/`eqe1ArchiveCount`, `placeableObjectsBytes`,
  `characterModelsBytes` all now measure real, nonzero, bit-identical-across-
  repeat-runs values (proposed baseline table in the T80 Review, pending
  Will's sign-off same as everything else here). **That fixes "not exercised
  by the gate."** It does **not** fix "no configured budget" — none of these
  three categories, nor any other bullet in this list, gained a
  `ConstrainedRendererConfig` ceiling; they remain bucket-(b)
  baseline-guarded (regression detection against a blessed value) exactly
  like `zoneGeometryBytes`/`s3dZoneSourceBytes`/`gpuTextureBytes`/
  `vboEboBytes` above. So: two axes, not one --
    - *exercised?* eqe1/placeableObjects/characterModels: now YES (T80).
      Everything else in this list: still NO except zoneGeometryBytes/
      s3dZoneSourceBytes/gpuTextureBytes/vboEboBytes, which were already
      exercised by the plain golden-image runs (T69) — those never needed
      T80's separate invocation.
    - *budgeted?* Every category in this list, including the three T80 just
      turned on: still NO. This list is exactly as long as it was after
      T69 for the budget question — T80 changed nothing here. The budget
      ruling remains open and is Will's call, per T69's original framing;
      T80 did not attempt it (out of that unit's Territory too).
    - T80 also fixed one bug while extending coverage: `eqe1MappedBytes`'s
      T69-era `computeMemorySnapshot()` counted only `eqe1ArchiveCount` and
      left `eqe1MappedBytes` hardcoded at 0 with a comment claiming
      `EQE1Loader` exposed no per-loader byte accessor -- that accessor
      (`EQE1Loader::getMappedBytes()`, `eqe1_loader.h`) has existed since
      P04, well before T69; the comment was simply wrong. Fixed in
      `tools/hwcaps/zone_bench_memory.cpp` (now sums `getMappedBytes()`
      across `ZoneBenchData::eqe1Loaders`, the same accessor and the same
      summation `IrrlichtRenderer::getEQE1MappedBytesTotal()` performs in
      production).

## Pi per-device memory budgets (T69, 2026-08-11) — natural follow-up, explicitly out of scope

- T69's memory-ceiling gate runs entirely on x86/llvmpipe (per its plan,
  Pi was explicitly out of scope: "ceilings measured on x86/llvmpipe do not
  transfer to Mali400, and the Pi runs through a separate tier-2 harness").
  The `ConstrainedRendererConfig` budgets the gate now enforces
  (`textureMemoryBytes`, `framebufferMemoryBytes`) are preset-derived
  (OrangePi preset, same one production ships) and therefore ARE the real
  Pi numbers in config terms — but nothing in this unit measures Pi's
  *actual* usage against them; `scripts/pi/pi_common.py`'s tier-2 harness
  is the natural home for that, mirroring what T69 did here for x86.

## Untracked-RSS gating recommendation (T69, 2026-08-11) — reported, not gated

- `totalEstimate` vs process RSS ("untracked" = RSS minus every tallied
  category) is now emitted, baselined, and printed prominently by
  `scripts/ci/visual_gate.py`'s verify() for every golden zone, but
  **deliberately not asserted** (per the T69 plan's explicit instruction —
  see its Step 4 and the T69 Review for the full repeat-run numbers).
  Measured data (9 runs across 3 zones, this x86/llvmpipe dev VM): every
  byte-tally category (texture cache, zone geometry, S3D source, GPU
  textures, VBO/EBO, framebuffer, `totalEstimate`) was bit-identical run to
  run with zero variance. Only `processRssBytes`/`untrackedBytes` varied,
  and by a small amount relative to RSS (roughly 0.02%-0.2% across the 5
  qeynos2 repeats, similar for gfaydark/befallen) — RSS itself ranged
  ~168-198MB depending on zone, untracked was consistently ~75-80% of RSS
  (~140-153MB) across all three zones. That untracked fraction being large
  AND stable is itself the interesting finding for a future unit to chase
  (named suspects per `renderer_diagnostics.cpp`'s existing P04 comment:
  glibc allocator arena/heap fragmentation, decode/staging buffers not yet
  named as their own line, and — new since this file's last audit — the
  benchmark process's own non-renderer overhead, e.g. SQLite/willeq_database
  init, Irrlicht device/scene-manager bookkeeping, which
  `getMemoryReportBreakdown()` was never meant to capture since it's a
  renderer-only tally). A future unit could plausibly gate `untrackedBytes`
  with a generous ceiling (e.g. RSS-relative, not absolute, given the
  ~170-200MB scale varies by zone) given how tight the measured spread was
  here — but T69 was explicitly instructed not to enable this on a hunch
  from one VM's data, so it is reported only. Whoever picks this up should
  gather more repeat-run data (more zones, ideally a second machine) before
  proposing a number.

## Unmerged-branch survey (orchestrator, 2026-08-11) — 16 branches audited, ONE contained work worth having

Prompted by discovering that the `--dump-frames-dir` video-capture flag had
sat unmerged on a parked branch for two weeks while T63 was reduced to
burst-capturing an attack swing and hoping a frame landed. That raised the
obvious question — **what else has been missed?** — and nobody had ever
enumerated the branches.

Method: every branch with commits not in `fable`
(`git rev-list --count fable..<branch>`), then per-branch audit of whether
the content is genuinely absent from `fable` **by comparing code, not
ancestry** — old branches are frequently superseded by an independent
reimplementation, which `git branch --merged` cannot see.

### Verdicts

- **`task/anim-video-capture`** — the one real find. Carried
  `--dump-frames-dir`/`--dump-every`, parked in July because the branch
  *also* bundled three workarounds rather than fixes (the right call at the
  time). All three were later fixed properly and independently — P23
  (`e702341`) root-caused the `saveScreenshot` guard and the
  `ParticleManager::renderUnified` race, and the missing-equipment FATAL
  became a WARN — but nobody went back for the 55 lines that were worth
  having. **Extracted by T64.** Branch content now superseded.
- **`mem`** (2 commits, 2026-01-19) — **DELETED**. Its "eviction crash fix"
  was a self-labelled `WORKAROUND` that commented out
  `driver_->removeTexture()`, leaking GPU memory on the 512MB target to
  hide a dangling pointer in Burning Video — a renderer since deleted
  entirely. `fable` fixed the same problem properly via
  `TextureEvictionListener`, which frees the memory *and* covers the
  detached-node case `mem` never found. Applying it today would reintroduce
  an unbounded texture leak on the Pi. Tip archived as tag
  **`archive/mem-20260111`** (`cd6a7712`) because the branch was unmerged
  and its commits would otherwise be garbage-collected; the tag message
  carries the full verdict. One idea there has no counterpart in `fable`
  and may be worth reviving as a fresh design: `ConstrainedMemoryBudget` +
  `IAssetCache`, a cross-asset budget coordinator. **Will has since ruled
  that per-subsystem budgets, defined and enforced, are the model and no
  global coordinator is wanted** (see T69) — so that idea is closed unless
  the ruling changes.
- **`harness/qwen-t2` / `-t3` / `-t4`** (2026-07-25) — **DELETED**, no
  archive tag; nothing salvageable in the code. These were
  model-evaluation runs, and their commit messages were the actual
  artifact. Preserved here since the branches are gone: **T2**
  (`Strings::Truncate`, 2m05s, 11 tool calls) — "27/27 tests pass BUT model
  edited tests 3x to match impl, never fixed impl; Truncate_ExactLength now
  contains a duplicated assertion, boundary untested". **T3** (`/uptime`
  slash command, 9m55s, 68 tool calls) — "chose CommandProcessor correctly
  among 3 competing command systems; diff is idiomatic; most of the run
  wasted on a /tmp mock harness that never compiled; LATENT: `game_state.h`
  uses `std::chrono` but `<chrono>` was added to the .cpp". **T4** (a
  3-line comment in `eq.cpp`, 2m23s, 9 tool calls) — "never attempted a
  whole-file read of the 165K-token file; edit landed correctly inside
  `RegisterCommands()`; did NOT verify its own work, build was run by hand
  afterward". Shas, if ever wanted from reflog: `b3e30807`, `6b80ccc0`,
  `42f1cf68`.
- **`fix-spell-effects`** (1 commit, 2026-01-08) — **mislabelled, and now
  fully superseded.** Despite the name it contributes nothing to
  `SpellData::effects` (spell *mechanics*); it parses `spells.eff`, the
  client's *visual* effect file. Its parser was heuristic — a 32-byte
  sliding scan for "Source"/"Target" markers because it missed the 8-byte
  record header and the grouping-of-three, and it **synthesised effect ids
  from an incrementing counter** rather than using the record index, which
  is the one thing the file is indexed by. Kept at survey time only because
  its parser might seed the spell-effects rewrite. **T70 has since written
  a correct reader from scratch**, so that reason is gone. Safe to delete;
  a worktree at `/home/user/projects/claude/willeq-fix-spell-effects` holds
  it open and must go first.
- **`viewer`** (2 commits, 2026-01-22) — superseded. Its
  `getBoneWorldPosition()` Y-rotation fix, which looked alarmingly relevant
  to the held-item work, is **already in `fable`** (verified: `nodeRot` /
  `cosY` present). Only its model_viewer multi-view and hand-marker changes
  may remain.
- **`task/P07c-default-flip`** — deliberately parked as rejection evidence
  for the P07 lighting arc. Accounted for; leave it.
- **`renderer`** (documented reference-only, never merge), **`codex`**,
  **`poc`**, **`metalpoc`**, **`storypoc`**, and the two ~300-commit
  `task/eqemu-*` / `task/eqcpp-*` branches — parallel or historical lines,
  not pending work.

### The systemic point

**There is no process for parked branches.** Things get parked for good
reasons, the reasons expire, and nobody revisits — the same drift class as
this file, but with no index and no hygiene pass. A survey like this had
apparently never been run.

Note the asymmetry with T53's `deferred.md` pass, which found six items
marked open that were already fixed. The branch survey skewed the other
way: things that *looked* valuable and were superseded. **Code rots faster
than notes do**, so branch audits must compare code rather than trusting a
title or a commit message.

Conventions established, for whoever runs this next:
- Audit by comparing code against `fable`, never by ancestry.
- Before deleting an **unmerged** branch, tag it `archive/<name>-<date>`
  with the verdict in the tag message — otherwise the commits become
  unreachable and are eventually GC'd. A branch whose content is already in
  `fable`'s history (like T67's `ConstrainedMeshCache` removal) needs only
  a docs pointer, not a tag.
- A worktree pinned to a branch holds it open; remove the worktree first.
## Spell-effects arc step 3 landed (T72, 2026-08-11) — texture identity and captures are what's left

- T72 built the selection layer (`spell_visual_selection.h/.cpp`:
  spell_id -> `spell_affect_index` -> spells.eff record -> per-stage
  Source/middle/Target `hasContent()` + resolved texture filenames, 12
  tests) and wired staging presence/absence into `SpellVisualFX`'s GLES2
  particle creation (`createCastGlow`/`createProjectile`/`createImpact`/
  `createImpactAtPosition` each gate on the matching stage, but only for
  `Resolved` records — the ~961-spell fallback population, measured exactly
  by the new census test, is untouched). See `refactor/T72_spell_visual_layer.md`'s
  Review for the full writeup, including a genuine upward correction of
  T70's empty-padding count (163 measured vs. 148 in T70's five-index
  table — both correct, T72's is a fuller scan).
- **Two things explicitly NOT done, both natural next steps:**
  1. **Texture identity is resolved but not rendered.** The GLES2 particle
     path (`ParticleManager`/`SpellEffectsConfig`) draws from a fixed
     15-shape procedural atlas (`spell_effects_config.cpp::buildTextureNameMap()`),
     not the real per-spell `.tga` files spells.eff names
     (`spelc1.tga` etc., verified present in `SpellEffects/` by T70). Making
     a fire nuke's particles actually *look* different from an ice nuke's
     needs either loading spells.eff's real sprites into the atlas or
     extending `ParticleManager` to bind an ad-hoc per-effect texture —
     `particle_manager.cpp`/atlas-format work, out of T72's territory
     (shared with the weather system).
  2. **`unknown_bytes` (800 bytes/emitter) is still fully unread** — T70's
     original find, untouched by T72 on purpose (STOP condition in both
     units' plans). Decoding it for timing/scale/colour/particle-rate is
     the last piece standing between this and a fully data-driven spell
     visual system; it needs real leverage (e.g. correlating against a
     captured live cast), not another blind hypothesis test.
- **No `review/T72/` media exists** — T72 STOPped on captures rather than
  taking eqemu (owned by sibling T71) or building an out-of-territory
  scripted-cast harness on a rushed budget. The staging-gate code itself
  is verified by build+test, not by pixels; a future unit with eqemu
  access (or a proper headless spell-cast capture harness, itself a
  worthwhile small unit) should capture the before/after the T72 plan
  originally asked for once texture identity is also wired.

## `spells.eff` parameter table (T73, 2026-08-11) — MOSTLY REFUTED, remainder UNDETERMINED

The recovered `archive/fix-spell-effects-20260811:.agent/spells_eff_format.md`
document claimed to decode T70's 800-byte `unknown_bytes` region as 8
fields (duration, fade-in/out, min/max scale, particles/sec, particle
lifetime, 12-float colour keyframes). T73 tested every claim directly
against the real file and `data/willeq.db`, with no clamping, per
`refactor/T73_spells_eff_parameter_decode.md`'s Review (full distributions,
semantic-correlation numbers, and pinning-test names there). Summary:

- **REFUTED**: `min_scale`/`max_scale` (0x280/0x288) — 68% of populated
  emitters have `min_scale > max_scale`, the opposite of what a min/max
  bound pair should show; negatives present, outside the document's own
  claimed range.
- **REFUTED as "colour"**: the 0x370 float[12] array — real values reach
  32.0, 12x the document's own claimed [0.0, 2.0] bound. The array's
  *location/size* (ends exactly at the 928-byte block boundary; padding
  reads a flat `(1,1,...,1)` template; populated records show a
  decay-to-1.0 pattern) is genuinely corroborated — only "colour" is wrong,
  not the field's existence. Left unnamed; likelier a scale/intensity
  keyframe curve, not proven.
- **UNDETERMINED, real, unnamed**: `duration_ms`/`fade_in_ms`/`fade_out_ms`/
  `particles_per_second`/`particle_lifetime` (0x274-0x298). No internal
  contradiction (non-negative, clusters at round values, reads exactly zero
  in both confirmed padding records 88/255). But the semantic-correlation
  test — the plan's own "strongest test" — found nothing: records
  referenced by long-lived buffs vs instant nukes overlap 33/38 with no
  systematic separation, and `duration_ms` has ~zero correlation (Pearson
  r=-0.088) with the referencing spells' `cast_time_ms`. Real structured
  data lives at these offsets; what it means is not established.
- The 500-byte gap the document ignored (block offset 0x80-0x274) is not
  simply zero (0/768 emitters all-zero) — it opens with a `0xFF`-filled run
  (matching T70's padding-template filler finding, now seen extended into
  populated records too) followed by small structured int32-looking values.
  Not decoded. Two more undocumented internal gaps were found and are
  equally unclaimed (0x284-0x288, 4 bytes; 0x28C-0x294, 8 bytes;
  0x29C-0x370, 212 bytes) — mostly zero, nonzero in a minority of records.

**Nothing was adopted.** `SpellsEffEmitter::unknown_bytes` is unchanged;
the reader gained only a doc-comment addendum and 4 new pinning tests (22
total) that read the claimed offsets directly out of the existing raw
array — no new accessors, zero behaviour change (T72, live in slot2,
unaffected). Fully data-driven spell visuals via this parameter table are
**not** unblocked by this unit. What remains for a future attempt: the
500-byte gap's small int32-looking values right after the `0xFF` run are
the most promising unexplored lead; a real packet capture of the client
triggering known spell effects (rather than static file analysis) is
probably required to close the "duration_ms means what" question, since
static analysis found no correlate to lean on.

## `gtest_discover_tests` timeout under parallel build load — FIXED by T79 (2026-08-12)

- `cmake --build` failed 3x on 2026-08-12 with `GoogleTestAddTests.cmake:
  Error running test executable` while discovering tests for
  `test_spell_manager` and `test_graphics` (both heavy binaries — the
  former links `willeq_core`, the latter also links Irrlicht) — each time
  under `-j24` parallel compile load, each time clean on an immediate
  retry with no source change. This is a **discovery-time timeout, not a
  compile error**: `gtest_discover_tests()`'s default `DISCOVERY_TIMEOUT`
  is 5 seconds, and under `-j24` CPU contention a heavy binary's
  `--gtest_list_tests` subprocess can legitimately take longer than that
  to even start, let alone finish. The failure makes `cmake --build` exit
  nonzero — the whole verification-bar step 1 reads as red — even though
  nothing is actually broken.
- Two fixes considered: raise `DISCOVERY_TIMEOUT` on the affected targets,
  or move them to `DISCOVERY_MODE PRE_TEST` (enumeration deferred to
  `ctest` invocation instead of happening as a build-time/post-build
  step). **Chose raising `DISCOVERY_TIMEOUT` to 60s** on `test_spell_manager`
  and `test_graphics` (`tests/CMakeLists.txt`), not `PRE_TEST`, because:
  `DISCOVERY_MODE` is a CMake 3.20+ keyword and this project's
  `CMAKE_MINIMUM_REQUIRED(VERSION 3.14)` (top-level `CMakeLists.txt`)
  would make that combination version-fragile for anyone building with an
  older-but-still-declared-supported CMake, where raising
  `DISCOVERY_TIMEOUT` has no such floor. **Trade-off, stated honestly**:
  raising the timeout doesn't remove the underlying contention — discovery
  still runs concurrently with `-j24` compilation, competing for the same
  CPUs — it only gives more slack before that contention manifests as a
  failure. A sufficiently loaded host (more parallel jobs, a slower CI
  runner) could still exceed 60s. `PRE_TEST` would remove the mechanism
  structurally (discovery happens after the parallel build is already
  done), at the cost of the CMake-version floor above and of a real
  discovery crash surfacing one step later (at `ctest` time instead of
  `cmake --build` time).
- **Could not provoke on demand.** A full `cmake --build -j24` from a
  fresh configure in this unit's session completed cleanly with no
  discovery failure — the timeout is load-dependent and this run's host
  conditions didn't reproduce the 3 failures from earlier the same day.
  The fix is applied based on the documented mechanism (CMake's own
  `DISCOVERY_TIMEOUT` semantics match the exact error text seen), not on
  a captured repro in this session — stated honestly per the plan's
  instruction, not claimed as proven. See
  `refactor/T79_tooling_cleanup.md` Review.

## Befallen's empty frame and stacked entities — T83 (2026-08-12): TWO UNRELATED CAUSES, one fixed, three reported open

T83 was dispatched because T82's README described Befallen screenshots as
"the fully-loaded indoor zone" when they showed a flat blue field, a sky
dome indoors, one grey placeholder cube, and a pile of overlapping NPCs —
Will caught the mismatch by opening the images. Full investigation,
before/after evidence and screenshots: `refactor/T83_entity_positions_and_missing_geometry.md`
Review, `/mnt/projects/fable/review/T83/`.

- **FIXED**: `eq_entities.cpp`'s `ZoneProcessNewSpawn` (single-spawn
  packets) was missing the `/8.0f` fixed-point scale-down that the bulk
  `ZoneProcessZoneSpawns` path already applies to the identical encoding —
  a real, independent bug. Entities delivered via individual `NewSpawn`
  packets (the majority of a populated zone, since the initial bulk burst
  only covers what's already spawned) landed 8x too far from the player.
  Verified against the server's own `spawn2` table post-fix (`Blugurg`,
  `Guard_V\`Lex` in `nektulos` now match exactly after the documented X/Y
  swap). `tests/test_zone_entities.cpp` updated to match.
- **NOT a client bug, reported only**: Befallen's 40 stacked "entities"
  (`SummonResupplyAgent`, `SummonClockworkBanker`, `Miltiades_Tal\`Azroth`,
  `Brin_Stolunger`, `Chomper`, `a_scaled_wolf_pup`, `a_rat`) are NOT
  static `spawn2` mobs (T82's README claimed this — wrong, corrected here)
  and are NOT a decode error. The server's own `zone_state_spawns` table
  (persisted ephemeral-NPC state) has 40 rows for zone 36 with
  `spawn2_id=0`, all created at the identical timestamp, all positioned at
  the player's exact spawn coordinates/heading. This is leftover test
  debris from some prior GM-summon session that got persisted and now
  respawns every zone boot. A `DELETE` to clean it up was attempted this
  unit and **blocked by the session's own permission classifier**
  (outside the "documented character-position writes" allowance) —
  left in place, reported. The `nektulos` `/list` dump has the identical
  pattern (`a_rat000`-`a_rat009` stacked on `Testmelee`), same debris
  shape, different zone — not cleaned either.
- **NOT touched, reported only (Part 4 STOP — architecture-scale)**: no
  ground/wall geometry renders near the camera in Befallen despite the
  camera measuring *inside* the loaded zone geometry's X/Y bounding box
  (camera EQ~(7.39,-64.08,13.00) vs bounds X=[-1007.6,28.0]
  Y=[-281.9,195.9]) and despite 32995-38175 polys/363 regions/8 draw calls
  genuinely submitting every frame. A distant piece of real zone geometry
  (an entrance bridge/walkway) renders correctly in the same frame,
  ruling out a global coordinate-space or camera-placement bug — this
  looks localized to region/PVS visibility selection for the player's own
  BSP region, not measured further (out of this unit's fence).
- **NOT touched, reported only**: sky dome renders in indoor zones because
  `renderer_zone_load.cpp`'s `isDungeon = (zoneType == 2)` check uses the
  wrong signal — the server already sends (and the client already stores)
  a `skyType` field that correctly says "no sky" for Befallen
  (`skyType=0`, matching the DB's own `zone.sky=0`), but sky-dome gating
  keys off `zoneType` instead, which this DB doesn't populate
  meaningfully (0/255 with no indoor/outdoor correlation across a dozen
  sampled zones). Right shape of a fix: gate on `skyType` (or whatever
  field the DB's `sky` column round-trips to), not `zoneType`. Not
  implemented — outside Part 4's fence for this unit.
- **NOT a bug, a missing test-session config field**: "Failed to load map
  for zone: befallen" (T82) was T82's own client config
  (`data/captures/t82_testchar_willeq.json`) omitting `maps_path`
  entirely, falling back to a nonexistent relative `./maps`. The real
  `.map` file exists and is valid (`/mnt/projects/akk-stack/server/maps/base/befallen.map`,
  148KB) — confirmed by re-running with `maps_path` set: "Successfully
  loaded map for zone: befallen". Every future live-testing config should
  set `maps_path`/`navmesh_path` (see `casterella.json` for the known-good
  example) or this will silently recur for every zone, every session.
- **Confirmed, not a bug**: race 457 (`SummonClockworkBanker`,
  `SummonResupplyAgent`) exceeds `willeq.db`'s `race_models` table ceiling
  (`MAX(race_id)=367`), hence the placeholder cube. Right shape of a fix
  (not implemented, Will's call per the plan): either extend the race
  table for out-of-era summoned-NPC races or leave as-is — an era-filtering
  policy question, not a bug fix.
- **Two defects looked like one shared cause and are not.** Will's working
  hypothesis (one coordinate-space/origin error explaining both the empty
  frame and the collapsed entities) does not hold: the entity stacking is
  DB-side test debris (server data, not a client decode issue at all), and
  the empty-frame issue is a region/PVS-visibility question with the
  camera already inside the correct bounding box. They happened to be
  visible in the same screenshot; they are unrelated.
