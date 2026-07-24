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
- **Pi A/B COMPLETE (P07b, merged 9ac4762): all four acceptance criteria
  PASS.** Anomaly poses tie count-mode (+0.0/+0.1%); city poses -19 to
  -24% vs count (78-103% of pure-area's win retained); whole-run
  -11.3% (76.85→68.19ms, 13.0→14.7fps); geometry byte-identical.
  Ready for the default flip: orangepi preset → area+floor1, golden
  re-bless with Will, Pi tier-2 baseline rerun — awaiting Will's go.
  (Path drift 5th occurrence — static-pose fallback used, see the
  record/replay unit below.)
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

## Benchmark camera path is not reproducible across builds (3rd occurrence, P06)

- The navmesh-driven moving camera path (`--entities 200 --seed 42`) has now
  failed to transfer three times: March logs → P05, and P05 → P06 (identical
  command, same worktree, different session — P05's frame 558/560-563 poses
  did not exist in P06's run; P06's whole-run avg 91ms vs P05's 30ms is a
  path difference, not a regression). Every perf unit re-finds poses from
  scratch and cross-unit frame-time comparisons are impossible. Fix: record/
  replay the camera path (dump per-frame pose to a path file, `--path-file`
  replay flag — the CSV already carries pos/heading/pitch, so replay is the
  only missing piece). Small unit; do it before the next perf campaign.
- Related doc gap: x86/llvmpipe runs silently fall back to an orbit camera
  unless `--navmesh-path /mnt/projects/maps/nav` is passed (not in help
  text) — P06 review documents it; fold the help-text fix into the same unit.
- Repo hygiene: `benchmarks/pi_runs/` is gitignored (H04-era), which is how
  P05's "committed" CSVs silently never landed (salvaged 3fb85fc). Unit
  deliverable CSVs must be `git add -f`'d, or unignore the directory.

## Pale fog-colored flat shapes in gfaydark/qeynos2 goldens (H02c/H02d observation)

- Distant geometry renders as flat fog-colored (pale) shapes against the
  darker sky — visually jarring; likely fog color/sky color mismatch or fog
  applied to geometry but not sky. Pre-existing (byte-identical across the
  alpha fixes); visible in gfaydark goldens. Investigate fog pipeline vs
  zone fog color data. Will's eye flagged the general area; needs its own
  unit.

## Production draw-order nondeterminism (found by H02b, 2026-07-23)

- **renderer_zone_draw.cpp:242,333 builds the material sort key from the low
  16 bits of the texture POINTER** — same bug H02b fixed in the benchmark
  loader (zone_bench_loader.cpp, FNV-1a name hash). Under ASLR the
  production draw order of equal-material geometry varies run-to-run:
  wobbling state-change counts, nondeterministic z-fighting on coplanar
  faces, and it would break any future full-app golden testing. Small fix
  (apply textureGroupId() pattern to both sites); do it before the full-app
  llvmpipe test tier.

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
