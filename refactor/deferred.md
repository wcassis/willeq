# Deferred / Known-Open Items

Live list of open work items extracted from "done" batches and audits
(2026-07-22 doc reconciliation). When an item is fixed, delete it here and note
the commit. Sources are historical docs — do not edit those.

## Tree wind update unreachable on live GLES2 (P32 finding, preserved not fixed)

- `TreeWindController::update()` — the only code advancing wind time
  and the weather-multiplier transition — was ALREADY unreachable on
  the live GLES2 path before P32 (gated on a CPU-instancing tree count
  only the deleted software path populated). The weather multiplier
  has always been effectively frozen at 1.0. P32 preserved that exact
  behavior. OPEN QUESTION for Will (era-authenticity call): should
  trees visibly sway at all? 1999-era clients did not animate
  foliage; if era-honest stillness is preferred this item closes as
  "keep frozen — document"; if sway is wanted, a small unit wires
  update() into the frame loop (then needs a fresh golden check since
  wind time becomes frame-dependent — likely keep goldens at t=0).

**STANDING CROSS-SESSION POINTER (2026-07-28): the testing campaign
tracks its work in `refactor/testing_progress.md` (charter:
`refactor/TESTING_CHARTER.md`). Perf session: read its "Handoffs"
section each batch. Handbacks land in `refactor/TESTING_HANDBACK.md`.**

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

## Handback items owned by the perf session (Will, 2026-07-28)

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
  **→ FOR THE TESTING SESSION (tests/ territory)**: the remaining 4
  FAILs + 8 skips share ONE root cause — `tests/pcap_test_utils.h` +
  three test files hardcode zone port 7000, but the akk-stack image's
  entrypoint auto-boots ~15 static zones claiming 7000-7011, so real
  captures land on 7012+ (befallen 7012, qeynos2 7026 in this
  fixture). Fix = WILLEQ_ZONE_PORT env override or port autodiscovery
  from the pcap (SessionRequest/Response scan). Every future capture
  against this image hits the same thing.
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
  4/5). REMAINING: Phase 4 (delete dead source paths: BURNINGSVIDEO /
  AnimatedTreeManager / RenderingBackend::Software+OpenGL / EDT_OPENGL
  loading-screen branch) + Phase 5 (full doc sweep + decisions.md
  completion marker) — await Will's go. The
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

Still open from that audit:
- `InitGraphics` keeps its `IrrlichtRenderer*` parameter (drives
  initLoadingScreen + initDeferredGraphics); full removal needs
  Application-side init restructuring.
- Asset-loader namespace relocation (`WldLoader`, `PfsArchive`, `BspTree`,
  … out of `EQT::Graphics` into a shared asset layer) — own unit.
- Renderer reads spell static data (spell_visual_fx.cpp) and item DB
  (equipment_model_loader.cpp) — acceptable (static data).
- `EverQuest::GetDebugLevel()` static call from door_manager.cpp.
- `output/graphical_renderer.h` legacy interface — dead-code deletion
  candidate (IrrlichtRenderer does not inherit from it).

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

- **`SpellData::effects` is never populated.** `EQT::SpellInfo`
  (include/client/willeq_database.h) has no effects field, so
  `SpellDatabase::queryAndCache()` leaves every spell's effects array empty,
  and `isDamageSpell()`/`isHealSpell()` can never return true via this path.
  The old file-based loader populated it; the SQLite migration dropped it.
  Whoever owns the spell-effects data path should restore or formally remove it.
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

## From Batch H (2026-07-23)

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

## Performance gap on real hardware (H04 baseline, 2026-07-23)

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

## Outskirts anomaly (P05→P07, 2026-07-24) — hybrid floor LANDED, Pi A/B + default flip pending

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
- **Harness alignment (salvaged from P07c, still worth doing):** visual
  gate + Pi tier-2 baseline currently measure FULL per-pixel, not the
  production count-mode adaptive config — add the production flags to
  both, regenerate goldens (count-mode look), re-bless with Will,
  re-capture tier-2. Small unit; fold into the next harness-touching
  batch (path drift is 6 occurrences counting P07d — do record/replay
  first or together).
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
- After the Pi numbers: default-flip decision (orangepi preset →
  area+floor), golden re-bless with Will, Pi tier-2 baseline rerun.

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

## KPI status + the composite measurement gap (P11 synthesis, 2026-07-26)

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
- **Occluder budget: DEAD lever on befallen** (P11: +0.9-2.7% at 2x and
  uncapped). Production knob exists
  (occlusionMaxOccluderRegions, default 16) — one-line flip if another
  zone ever shows headroom; do not spend more on this.
- **Residual to 30fps** (at merge + static-entity dispatch): ~57-60%
  submission (entity draws dominate), ~37-39% GPU sync. The entity
  lever through the REAL animated path is the critical path.
- **--all-zones sweep DONE** (P11 side deliverable): 120 zones, 191 new
  sheets (477 total on NFS), 184s, 97 collisions logged, 39 failures
  all sleeper (DR2_HS_DEF texture won't fit 2048) — audit clean.
- Gate arbitration: P11's slot1 visual gate PASSED → P12's slot2
  zero-source-diff foliage mismatch is environment-specific
  (Xvfb-session suspected), not a gate regression.
- **NEXT: P12b** — fix the animated-path crumple (bind-pose lead in the
  P12 review), un-skip ModelAnimatesProperly, THEN re-measure the
  stacked number through animated composite entities = the real KPI
  verdict. Will's live-animation answer is load-bearing twice over
  (crumple liveness + whether flipped composites render correctly in
  play).

## Live x86 client bugs (anim-video-capture session, 2026-07-26)

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
  WARN+skip.
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
- Post-camp shutdown-thread aborts remain (documented class, still
  open) — sessions still may not end in a fully clean /camp; the two
  fixes above remove the main mid-session crashers.

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

## P20 live-visual pair: pop-in RESOLVED (by design), foliage alpha real site found (2026-07-27)

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
  its own look (likely race_codes.cpp mapping gap class).

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
- NOTE: achieving these numbers in live play requires the
  mergeZoneDraws preset flip (composites already live).

## ConstrainedMeshCache is never constructed (P10 finding, 2026-07-26)

- Exhaustive grep (verified independently by orchestrator): no
  construction site exists for ConstrainedMeshCache anywhere in src/ —
  the lazy/progressive region-mesh streaming documented in
  src/client/graphics/CLAUDE.md ("mesh cache active → texture pixel
  data retained", 24MB budget, eviction) is NOT wired into production;
  regions are always built eagerly. Docs overstate reality; either wire
  it (with the P10 full shared-VBO reconciliation) or trim the docs and
  the dead conditional paths (renderer_zone_stream.cpp/renderer_zone_draw
  guard on a pointer that is never non-null).

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

## Entity mesh materials hold dangling ITexture* after eviction (found by P15, NOT fixed)

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

## RDP frame-update race (observed repeatedly during Batch F, 2026-07-22)

- **RDPServerFrameTest.UpdateFrameMultipleTimes segfaults intermittently**
  (~once per <=10 stress repetitions, confirmed on unmodified fable during
  F18 integration; also seen under parallel ctest load in F01/F09/F24 runs).
  This is a real race in the RDP frame-update path, not just test noise —
  worth an actual investigation/fix in renderer_rdp.cpp / rdp_server.cpp.

## From Batch Z

- Z5.3 empty-draw measurement (1-triangle buffer isolation) — TODO.

## Tracker hygiene

- Batch E table in progress.md has empty commit columns (work is committed;
  hashes were never recorded).
