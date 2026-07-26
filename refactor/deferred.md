# Deferred / Known-Open Items

Live list of open work items extracted from "done" batches and audits
(2026-07-22 doc reconciliation). When an item is fixed, delete it here and note
the commit. Sources are historical docs — do not edit those.

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
       (b) **Z35 seam diff under animation**: composite vs multi-buffer
       animated = matching pose/silhouette but 17-29% px at LOW
       magnitude (mean ~0.1-2/255) localized to a texture-atlas seam —
       needs its own small unit (likely same gutter/padding class as
       the P08d static bleed suspicion); (c) **P11b**: true composite
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
     **CRASH FIXED by P10b (2026-07-27) — flip UNBLOCKED, pending
     Will's bless.** Real root cause (deferred-loading hypothesis
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

## Radius mode as production default — OPEN flip candidate (Q03, 2026-07-25)

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
- saveScreenshot()'s safe glReadPixels path compiled out on x86 GLES2
  dev builds (EQT_HAS_DRM guard); fallback createScreenShot() segfaults
  nondeterministically in llvmpipe — widen the guard properly.
- ParticleManager::renderUnified nondeterministic crash
  (dangling-pointer-shaped race vs SimulationWorker result buffer) —
  gated on isEnabled() as a workaround; real race needs fixing
  (compare RDP frame race in this file's history).
- Even with workarounds, sessions end in crashes rather than clean
  /camp — x86 llvmpipe client stability needs a pass before it can be
  a routine evidence harness.

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
