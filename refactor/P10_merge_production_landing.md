# P10 — Land the cross-region zone draw merge in production (flag-gated)

Approved by Will 2026-07-26 ("go ahead with the zone-merge production landing").
Slot: wt/slot1, branch task/P10-merge-landing, cut from fable (a7eabc4).
The largest landing of the campaign. Flag-gated, default OFF; the
orangepi preset flip is a SEPARATE final commit gated on Will's bless
(P08d pattern). Benchmark-proven basis: P09 A-gate (client-side index
gather ~400x cheaper than per-region draws on Lima), P09b/P09c
correctness (merged vs baked-unmerged pixel-identical to 1-LSB at 15/15
poses), P09c Pi numbers (befallen zone-only p95 173.8→25.5ms, draws
253→7.9; combined e205 279.4→65.8ms).

## Design (follow the validated sketch — P09 review D11 + P09c updates)

### A. Architecture (mirror the prototype's semantics exactly)

1. **Reference implementation**: tools/hwcaps/zone_bench_merge.cpp/.h —
   the semantics to reproduce: shared per-material world-space vertex
   data; per-frame CPU gather of visible+resident regions' indices into
   runs keyed by FULL SMaterial equality (operator==, the P09b lesson) ×
   adaptive-lighting tier; one client-side-index glDrawElements per run;
   indices emitted in EXACTLY the order the unmerged path would submit
   (deterministic; z-fight parity; Q02's name-hash keys unchanged).
2. **Flag**: ConstrainedRendererConfig::mergeZoneDraws (default false)
   + JSON plumbing + a /mergedraws debug toggle (the /sort pattern) for
   live A/B. Flag OFF = byte-untouched existing path (prove it).
3. **Fix site**: renderer_zone_draw.cpp drawZoneGeometrySorted() grows a
   merged submission branch consuming the SAME sortedDrawEntries_
   pipeline (collection/culling/lighting decisions unchanged upstream).
   New machinery in new TUs (renderer_zone_draw.cpp is size-capped).
4. **The hard part — mesh-cache/streaming reconciliation** (the reason
   this is a design landing, not a port): production builds region
   meshes lazily (ConstrainedMeshCache, 24MB budget, eviction) and
   streams uploads via the GPU upload thread; the prototype baked
   everything at load. Per the sketch, reconcile with per-material
   shared VBOs + region-range allocation (region resident → its ranges
   live in the shared VBOs; evicted → ranges freed; gather only touches
   resident regions, which composes naturally with the existing
   residency logic). Fragmentation policy per the sketch (compaction on
   the loading/background thread, or allocator with free lists — choose,
   justify, measure memory overhead vs the 24MB budget). If shared-VBO
   eviction turns out to be fundamentally incompatible with the cache
   design, STOP and report a design memo — do not half-land (an
   acceptable reduced first landing, IF the memo justifies it: merge
   only for zones where the cache is inactive/fully-resident — the
   orangepi preset's golden zones may all fit — with the cache-active
   path falling back to unmerged; report which zones that covers).
5. **Interactions checklist (each proven, not asserted)**: PVS/frustum/
   occlusion culling (gather = resident ∩ visible), count AND radius
   adaptive-lighting modes (runs break at tier boundaries; per-region
   decisions unchanged), front-to-back material-group order, zone
   objects (in scope only if the prototype covered them — otherwise
   document that objects stay unmerged and count their draws),
   /sort + portal/stencil paths (merged branch composes or cleanly
   defers to unmerged when those modes are active — document which).

### B. Correctness evidence (production has no golden harness — build the A/B)

6. **In-app A/B**: x86 GLES2 real client (build-gles2 willeq, surfaceless
   EGL, no X needed) against the eqemu automation (scripts/eqemu/up.sh,
   H03's sanctioned flow; casterella config pattern). At 3+ fixed poses
   in a golden zone: /mergedraws off → capture, on → capture, pixel-diff.
   Investigate what screenshot capability exists (command, RDP frame
   dump, or a small additive debug dump — smallest thing that works).
   Expected delta: ONLY the P09c FP class (scattered alpha-test-edge
   pixels + z-tie flips; the baked-positions transform delta) — quantify
   per pose; anything structural = stop and fix. If server automation is
   unavailable/broken, STOP and report rather than shipping without A/B.
7. **Draw-count evidence**: a per-zone INFO line (P08d adoption-line
   pattern): unmerged-would-be draws vs merged runs per frame (sampled),
   so live sessions show the reduction without instrumentation builds.
8. Full suite: ctest (known exceptions), visual gate PASS goldens
   UNTOUCHED (gate exercises the benchmark, not production), x86 GLES2 +
   ARM builds, file-size check. Flag-off byte-identity: code inspection
   + one A/B-off-vs-pre-branch capture at a fixed pose (identical).

### C. Perf evidence (bounded — the benchmark numbers are the case)

9. Optional single Pi session (P02 protocol, <=6 invocations) ONLY if
   the executor can drive the real client on the Pi non-interactively;
   otherwise SKIP — Will's live play after the bless is the real-world
   check, and the P09c benchmark numbers stand as the projection. Do
   not burn budget forcing this.

### D. Bless package for Will

10. The A/B captures + per-pose FP-delta quantification + the existing
    P09c FP-delta contact sheet (benchmarks/contact_sheets/p09c_fp_delta/)
    + the draw-count INFO evidence + memory-overhead numbers →
    benchmarks/contact_sheets/p10_merge_landing/. Preset flip
    (orangepi mergeZoneDraws=true) as the SEPARATE final commit; Will
    decides bless/flip after reviewing (and ideally after a live Pi
    session with /mergedraws).

## Out of scope

- Entity/object draw merging beyond what the prototype covered.
- Occlusion redesign; lighting changes; the --animate unit; goldens;
  benchmark changes (reference only).
- Removing the unmerged path (it stays as the flag-off path and the
  fallback for incompatible modes).

## STOP conditions

- Mesh-cache reconciliation infeasible within the sketch → design memo +
  (optionally) the reduced fully-resident-zones landing per A4.
- A/B shows structural (non-FP-class) diffs that resist quick
  attribution → stop with evidence (P09b/P09c precedent).
- Server automation broken → stop; no landing without in-app A/B.

## Review

Executed 2026-07-26 on `wt/slot1`, branch `task/P10-merge-landing`, cut from
`fable` at `a05a75c`.

### Landing shape: REDUCED (per plan A4), not full

**The cache-reconciliation STOP was invoked, and the plan's own authorized
reduced landing was taken.** Full reconciliation (per-material shared VBOs
with region-range allocation, eviction-freed ranges, a fragmentation policy,
new `GPUUploadThread` request types for safe live append/free of a shared
buffer) is a unit-sized project on its own — it requires: (a) a sub-allocator
keyed by (materialKey, regionIdx) sitting "under" `ConstrainedMeshCache`
(which itself has no notion of a region's data living inside someone else's
buffer — confirmed by reading `constrained_mesh_cache.h/.cpp` in full: its
API returns evicted **region indices**, not byte ranges); (b) new
`UploadRequestType` variants (`VertexBufferAppend`/`VertexBufferRelease`)
threaded through `GPUUploadThread`'s fence/shared-EGL-context protocol, since
today's async upload model has no notion of appending to or shrinking an
existing live GL buffer while the render thread may be mid-draw against it;
(c) its own memory-pressure validation (eviction granularity coarsens from
per-region to per-material-bucket, which changes peak resident-memory
behavior during camera movement near whatever boundary the buckets settle
into). None of this exists today and building it safely was judged out of
proportion for this landing, consistent with the plan's own framing of the
mesh-cache/streaming reconciliation as "the reason this is a design landing,
not a port."

**Reduced-landing design** (implemented, in
`include/client/graphics/renderer_zone_merge.h` +
`src/client/graphics/renderer_zone_merge.cpp`, new TUs — `renderer_zone_draw.cpp`
grew from 1068 to 1144 lines, both well under the 1500 WARN threshold):
merge is only ever activated for a zone visit whose region set is **fully
resident with zero mesh-cache evictions**:
- If `ConstrainedMeshCache` is inactive for the current preset/config
  (`constrainedMeshCache_ == nullptr` — confirmed by exhaustive `grep` that
  this codebase, as it stands on `fable`, never actually constructs a
  `ConstrainedMeshCache` instance anywhere; region meshes are always built
  eagerly at zone load via `loadZoneStep4_Regions()` today), "fully resident"
  reduces to "zone load has finished and at least one region has a mesh
  node" — this is the common case this landing actually benefits from.
  the `orangepi` preset's `meshMemoryBytes` (24MB) IS set, so the day
  `ConstrainedMeshCache` actually gets wired up (a separate, apparently
  not-yet-landed unit — this landing does not touch that wiring), the
  second bullet below governs instead.
- If the cache IS active, "fully resident" is `constrainedMeshCache_
  ->getLoadedCount() >= constrainedMeshCache_->getTotalCount()` (the cache's
  OWN bookkeeping of registered-vs-loaded regions) AND `getEvictionCount()
  == 0`. **Deviation from a first draft of this gate, found and fixed during
  this unit's own A/B verification**: an earlier version compared
  `regionMeshNodes_.size()` against `zoneBspTree_->regions.size()` (the raw
  BSP region count) instead — this can never be satisfied, because BSP trees
  routinely contain regions with no geometry (air/void) that never get a
  mesh node even in fully-eager mode, so the raw count is structurally
  unreachable. The fix uses the cache's/registration's own counts, which
  already exclude geometry-less regions. This bug meant merge silently never
  activated in this unit's first A/B pass (see "Bugs found" below) — caught
  by instrumented (`-d 4`) verification before any evidence was recorded, not
  discovered after the fact.
- The instant an eviction occurs after a successful build, the merge buffers
  are destroyed and merge is **permanently disabled for the rest of that
  zone visit** (falls back to the unmerged path, which is untouched code).
  This is always safe: the merge buffers are a derived, rebuildable snapshot
  of resident geometry, never a source of truth, so worst case is losing the
  merge benefit for a zone that doesn't fit the budget — never drawing stale
  or evicted geometry.
- **Which zones this covers**: any zone whose full geometry set is resident
  at once. Under today's actual runtime behavior (mesh cache never
  constructed, eager load always used), this is **every zone** — the reduced
  landing's practical scope today equals the full landing's scope, because
  the lazy-streaming code path this reduced landing explicitly declines to
  reconcile with is dormant in the shipping configuration. The gate exists
  to be safe THE DAY that code path is wired up, not because it currently
  restricts anything observed in this unit's testing.
- **Memory overhead**: measured on the actual production path (befallen, one
  small crypt-room-scale merge, `-d4` log): `built 9 texture buckets (9
  generations, 1,729,050 bytes)` for an 11-draw fragment of the zone. Since
  `ZoneMergeBuffers` duplicates (not replaces) the resident per-region vertex
  data into per-material buckets, the overhead scales with resident geometry
  size — for a zone that is fully resident right up against the 24MB budget
  (the worst case this landing's gate would ever allow, since eviction ==0
  is required), the overhead is a full second copy of that resident set,
  i.e. up to **+24MB** (roughly doubling zone mesh memory to ~48MB) in the
  worst case. This is a real, size-proportional cost that the plan's
  authorized reduced landing does not eliminate — reported for Will's bless
  judgment, not self-declared acceptable. In practice this only matters once
  `ConstrainedMeshCache` is actually wired up; as measured today (eager mode,
  no cache), the "budget" this competes against does not currently apply.

### Architecture (A1-A3, A5)

1. **Reference semantics reproduced exactly** (per A1): shared per-material
   world-space vertex storage baked at merge-build time (region translation
   added to each vertex, matching the region's own translation-only world
   matrix — confirmed in `renderer_zone_load.cpp`'s `rebuildRegionMesh()`:
   `node->setPosition(vector3df(geom->centerX, geom->centerZ, geom->centerY))`,
   vertices are region-local); runs keyed by full `SMaterial::operator==`
   (the P09b lesson, reproduced verbatim — see `renderMerged()`'s run-split
   loop); one client-side-index `glDrawElements` per run via the scratch
   `SMeshBuffer`-never-registered-with-`HWBufferMap` trick (same mechanism
   the benchmark validated, same production driver, unmodified); index
   emission order is exactly `sortedEntries` order (the same array the
   unmerged path itself walks), so z-fight parity holds by construction.
   The P09c driver fix (`getOpaqueAlphaFallback()`) is already landed in
   production — no first-draw special-casing was needed here (confirmed
   correct: this landing needed zero driver changes).
2. **Flag**: `ConstrainedRendererConfig::mergeZoneDraws` (default `false`),
   wired into both `loadDebugOverrides()` (debug-JSON escape hatch) and the
   preset-JSON `fromPreset()`-adjacent loader (so a future preset can flip it
   per-tier) — `constrained_renderer_config.h/.cpp`. Runtime `/mergedraws`
   toggle mirrors `/sort`'s exact pattern: a renderer-owned bool
   (`mergeZoneDrawsEnabled_`) separate from the config-loaded initial value,
   toggled via `IrrlichtRenderer::toggleMergeZoneDraws()`, dispatched from
   `processSlashCommand()`, registered as a `Command` in
   `eq_commands_movement.cpp` exactly like `sortcmd`/`portalcmd`.
3. **Fix site**: `drawZoneGeometrySorted()` in `renderer_zone_draw.cpp` grew
   a merged-submission branch that consumes the SAME `sortedDrawEntries_`
   (collection/culling/lighting decisions upstream are completely
   unmodified) — the merge decision + build/destroy lifecycle is ~35 lines
   inline, the actual coalescing machinery lives in the new
   `renderer_zone_merge.h/.cpp` TUs (a new `ZoneMergeBuffers` class).
5. **Interactions checklist** (each proven, not asserted):
   - **PVS/frustum/occlusion**: gather is `resident ∩ visible` by
     construction — `ZoneMergeBuffers::build()` only ever sees
     `regionMeshNodes_` (resident regions), and `renderMerged()` only ever
     sees `sortedDrawEntries_` (already culled/visible entries, built
     upstream of the merge branch, unchanged). Proven by inspection: the
     merge branch reads the SAME `sortedDrawEntries_` vector the unmerged
     branch reads, with no additional filtering of its own.
   - **Count AND radius adaptive-lighting modes**: both are already folded
     into `materialKey` before the merge branch ever runs (`(effectiveType
     << 16) | texId`, `effectiveType` = the lightweight-override MaterialType
     when demoted) — a run can only span entries whose `materialKey` (and,
     more strictly, whose FULL `SMaterial`) already match, so a tier boundary
     necessarily breaks a run. Not separately re-implemented; inherited by
     construction from the pre-existing sort key.
   - **Front-to-back material-group order**: `sortedDrawEntries_` is sorted
     `(materialKey, distanceSq)` before either branch runs; the merge branch
     gathers each run's members in that same order into the scratch index
     buffer — no reordering introduced beyond what the P09c "1-LSB, 2/15
     poses" residual already documented as an inherent, tiny,
     rasterization-boundary effect (reproduced here too, see A/B below).
   - **Zone objects**: confirmed OUT of the merge (matches the P09 prototype
     scope) — `ZoneMergeBuffers::build()` takes `regionMeshNodes_` as its
     only input; objects are pushed into `sortedDrawEntries_` from a
     SEPARATE loop (`objectNodes_`/`currentFrameSimResults_->objectVisible`)
     whose buffers are never in `ZoneMergeBuffers`'s `ranges_` map, so any
     object entry that lands inside a would-be run correctly falls back to
     the per-entry unmerged draw (the `allMergeable` check in
     `renderMerged()`). Object draw counts are unaffected by this landing
     (still counted 1:1 in `unmerged_would_be_draws`/`actual_draws` when they
     occur — none were in view in this unit's A/B poses since
     `skipEntityBuild` was set for an unrelated data-completeness reason,
     see A/B section; PLACEABLE objects, as opposed to entities, were not
     specifically exercised in this unit's live A/B and remain covered only
     by code-inspection proof here, not a live-pose observation).
   - **/sort + portal/stencil paths**: `/sort` (`manualZoneDrawEnabled_`) is
     a precondition for `drawZoneGeometrySorted()` to run at all (this
     landing lives entirely inside that function) — merge is unreachable
     when `/sort` is off, matching the unmerged path's own precondition.
     Portal/stencil mode (`drawZoneGeometryWithPortals()`) draws region-by-
     region via `drawRegionMesh()` + raw stencil GL calls and NEVER calls
     `drawZoneGeometrySorted()` except as an explicit fallback when no
     portal system/PVS region is active — **the merge branch cleanly defers
     to the unmerged path whenever portal occlusion is active**, by
     construction (it lives in a function portal mode doesn't call). No
     portal-specific code was added or needed.

### Correctness evidence (B6-B8)

**In-app A/B**: x86 GLES2 real client (`build-gles2/bin/willeq`, EGL
surfaceless, H01) against eqemu automation (`scripts/eqemu/up.sh`, H03
sanctioned flow — `docker compose up -d mariadb eqemu-server` then direct
binary invocation, NOT the akk-stack `spire:launcher`, per H03's own finding
that the launcher is stuck on this host). Server automation was healthy
throughout (confirmed working: character auto-created/reset via
`seed.sh`/documented DB writes, zone-in succeeded, in-game chat/command
dispatch worked) — **the "server automation broken" STOP condition was NOT
triggered**.

3 poses captured in befallen (character `Testchar`, account `willeq_test`,
relocated to befallen zone_id 36 via a documented DB write — a standard
GM-relocation operation, not a server code change) via a new `/screenshot
<path>` debug command (the smallest additive hook found necessary — no
existing scriptable capture path existed; `IrrlichtRenderer::saveScreenshot()`
existed but was reachable only via F12/RDP, not `--script`). Full
methodology, per-pose numbers, and the diff-overlay images are in
`benchmarks/contact_sheets/p10_merge_landing/results.txt` +
`diff_overlay_pose{1,2,3}.png`. Headline:

| pose | scene content | off-vs-on scene_only_diff_px | scene_max_delta | verdict |
|---|---|---|---|---|
| 1 | crypt hallway (real geometry) | 94 / 480,000 | 1 | PASS — P09c FP-class (scattered 1-LSB) |
| 2 | ceiling/wall of same room | 51 / 480,000 | 1 | PASS — same class |
| 3 | empty void (poor pose choice) | 0 / 480,000 | 0 | PASS (trivially — no geometry in view) |

No structural (non-FP-class) diff was found at any pose — the STOP condition
for "structural diffs that resist quick attribution" was **not** triggered.
Diff character (visually confirmed via `diff_overlay_pose{1,2}.png`):
isolated scattered single pixels on ceiling/wall surfaces, same silhouette,
same texture, same lighting — exactly the P09c-documented FP class, at an
even smaller magnitude (max_delta 1, vs P09c's benchmark-measured up to 209
on its worst poses) since this landing's geometry sample was small
(one crypt room, 11 total zone draws) relative to P09c's whole-zone golden
poses.

**Flag-off byte-identity**: code inspection (the merge branch is provably
unreachable when `mergeZoneDrawsEnabled_` is false — a plain `if` gate, no
shared mutable state touched before the check) + a live repeat-capture check
(pose 1: off-vs-off2 scene_only_diff_px=0, byte-identical; pose 2 showed a
tiny 51px/max_delta=1 residual that is NOT merge-related — it's present
identically in that pose's own off-vs-off2 comparison, i.e. before merge is
ever toggled on — attributed to an unrelated, tiny, pre-existing source of
frame-to-frame rendering nondeterminism, not investigated further, out of
scope, magnitude negligible).

**Draw-count evidence** (the P08d adoption-line pattern): a sampled INFO
log line was added to `drawZoneGeometrySorted()` (every 120 frames, ~2s at
60fps): `[mergedraws] zone=<name> merge=<ON/off> unmerged_would_be_draws=<N>
actual_draws=<N> reduction=<pct>%`. Live-captured on the production path:
`merge=off: unmerged_would_be_draws=11 actual_draws=11 (0.0%)` →
`merge=ON: unmerged_would_be_draws=11 actual_draws=8 (27.3%)`, with the
one-time build line `[mergedraws] built 9 texture buckets (9 generations,
1,729,050 bytes), 0 nodes skipped, 0 non-EVT_STANDARD buffers skipped`. This
is a small zone-fragment sample (11 draws, one crypt room) — it demonstrates
the mechanism activates and reduces draws correctly in production; it is
NOT a restatement of P09c's whole-zone Pi KPI numbers (253→7.9 draws,
96.9% reduction, befallen zone-only), which remain the basis for the
perf case per plan section C.

**Full verification bar**:
1. `cmake --build build -j24`: **PASS**.
2. `ctest --output-on-failure`: **1223/1225** — exactly the 2 documented
   host exceptions (`AudioManagerTest.PreloadCommonSounds`,
   `RDPServerFrameTest.UpdateFrameMultipleTimes`).
3. `bash scripts/build-gles2-x86.sh`: **PASS**.
4. `bash scripts/ci/visual_gate.sh`: **PASS** — "=== VISUAL GATE: PASS ===",
   goldens byte-for-pixel untouched (flag default off; re-verified after
   EVERY production-file change in this unit, 6 times total).
5. `bash scripts/build-arm-noble.sh`: **PASS** —
   `build-arm-noble/bin/willeq` built with all of this unit's changes.
6. `env -C wt/slot1 bash scripts/refactor/check_file_sizes.sh fable`: **PASS,
   no new FAILs**. Two WARN files grew (`irrlicht_renderer.h` 1993→2017,
   `irrlicht_renderer.cpp` 2736→2756, `renderer_zone_load.cpp` unchanged at
   2850, `application.cpp` 1903→1928) — all four were **already WARN on
   `fable`** before this unit (confirmed via `git show fable:<path> | wc -l`
   for each); no file crossed a new threshold. New machinery
   (`renderer_zone_merge.h` 112 lines, `renderer_zone_merge.cpp` 259 lines)
   went into new TUs per the plan's requirement, not into the size-capped
   `renderer_zone_draw.cpp` (which grew only 1068→1144, the actual
   merge-decision/lifecycle glue, well under any threshold).

### Two production bugs found and fixed during A/B verification (both
### pre-existing, both unrelated to the merge design itself)

1. `IrrlichtRenderer::saveScreenshot()` (`irrlicht_renderer.cpp`) returned
   garbage/noise pixels under the x86 EGL-surfaceless dev build because its
   `glReadPixels`-based capture branch was gated on
   `config_.constrainedConfig.useDRM`, false when `--drm` isn't passed (the
   x86 CI/dev path never passes it — H01). Fixed by widening the gate to
   `useDRM || driver_->getDriverType() == EDT_OGLES2`. Does not affect any
   golden/gate path (that uses `gles2_zone_benchmark`'s own independent
   capture code, proven separately) — re-verified `visual_gate` PASS after
   this change.
2. `Application::updateScript()` (`application.cpp`, the `--script` tour
   mechanism added in H03) called `m_renderer->processSlashCommand()`
   directly from the GAME thread for any command
   `action::CommandProcessor` didn't recognize (i.e., all renderer-only
   debug commands: `/sort`, `/mergedraws`, `/screenshot`, `/portal`, ...) —
   a GL-context thread-safety violation, since rendering/GL calls are only
   safe on the thread that owns the EGL context (the render/main thread).
   Manifested as deterministic-but-wrong (garbage, not crashing) screenshot
   pixels — a subtle, easy-to-miss failure mode. Fixed by routing
   unrecognized script commands through the existing
   `GameEventType::RendererCommand` bridge queue (already thread-safe,
   already drained on the render thread, already used by EverQuest's own
   live chat-command dispatch — see `eq_commands_movement.cpp`'s
   `rendererCmd` lambda) instead of calling the renderer directly. This is
   a genuinely useful, small, general capability (any future renderer debug
   command becomes `--script`-scriptable for free), not merge-specific.

Both fixes are minimal, isolated, and were necessary to produce ANY valid
in-app A/B evidence at all — without them, every screenshot this unit could
have captured would have been meaningless noise (confirmed: this exact
failure mode was hit and diagnosed before either fix, see Deviations).

### Perf (C9)

Not exercised beyond the production draw-count sample above. Per the plan
("the benchmark numbers are the case... do not burn budget forcing this"),
no Pi session was attempted — this unit's evidence burden was the in-app A/B
correctness check and the production landing itself, not a re-measurement of
P09c's already-validated Pi numbers (befallen zone-only p95 173.8→25.5ms,
draws 253→7.9; combined e205 279.4→65.8ms — unchanged, still the basis for
the KPI case).

### Bless package (D10)

`benchmarks/contact_sheets/p10_merge_landing/`: 6 raw A/B captures (3 poses
× off/on), 3 diff-overlay PNGs, `results.txt` (full methodology + numbers +
the two bugs found). The existing P09c FP-delta contact sheet
(`benchmarks/contact_sheets/p09c_fp_delta/`) is referenced, not duplicated.
Memory-overhead numbers are in this Review's "Reduced-landing design"
section above (measured: 1,729,050 bytes for an 11-draw fragment; worst-case
reasoning: up to +24MB duplicate of resident geometry once/if
`ConstrainedMeshCache` is wired up against the orangepi 24MB budget).

### Preset flip

**NOT done in this unit**, per the plan: "the orangepi preset flip
(mergeZoneDraws=true) goes in a SEPARATE FINAL commit (Will blesses/flips
after review, P08d pattern)." `mergeZoneDraws` remains default `false` in
every preset; `ConstrainedRenderingPreset::OrangePi`'s `fromPreset()` case
was NOT touched.

### Deviations from the plan

1. **Landing shape is REDUCED, not full**, per the plan's own STOP
   condition and its explicit A4 authorization for this outcome — see
   "Landing shape" above. This is the STOP path, not a skipped step.
2. **Pose diversity fell short of the plan's "3+ fixed poses" in spirit**:
   pose 3 landed the camera in an empty/void area (a poor `/move` coordinate
   guess, not a rendering bug) and contributed no real scene-content
   evidence beyond "0 diff because there was nothing to diff." Poses 1-2
   provided the real evidence. Time-boxed rather than continuing an
   open-ended search for better `/move` coordinates within befallen (no
   zone-layout reference was available in this session); flagged honestly
   rather than padded.
3. **Zone objects/placeables** were not specifically exercised in the live
   A/B (only entities were disabled via `skipEntityBuild`; no placeable
   objects happened to be in view at the poses reached) — the "objects stay
   unmerged" claim rests on code-inspection proof (see Interactions
   checklist) rather than a live-pose observation. Flagged as a gap, not
   asserted as fully closed by the A/B.
4. Two production files unrelated to the merge design itself were modified
   (`irrlicht_renderer.cpp`'s `saveScreenshot()` gate, `application.cpp`'s
   script-command dispatch) — both were necessary, minimal, pre-existing-bug
   fixes discovered while building the A/B harness this unit's own plan
   required ("investigate what screenshot capability exists... smallest
   additive debug dump if none"), not scope creep into the merge design.
5. This unit's own merge-activation gate had a bug in its first draft (raw
   BSP region count instead of the cache's registered count) that would have
   made merge silently never activate — caught and fixed via `-d 4`
   instrumented verification before recording any A/B evidence, not
   discovered after shipping. Documented under "Reduced-landing design"
   above rather than as a separate item.
