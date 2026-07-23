# H02b — Re-frame the golden set with representative scenes

Orchestrator visual review rejected the first golden set: qeynos2 frames show
fog/ground/sky with no city geometry (9-10 draws), gfaydark clips through a
tree, befallen views the crypt from outside its shell. The gate machinery
(H02) is sound; the CAMERA PLACEMENT is not. Produce goldens that actually
guard the renderer.

## Design

1. **Prefer the navmesh camera path** (`zone_bench_camera`'s
   `generateFromNavmesh`) — it follows walkable ground through the zone,
   which is exactly "what a player sees." Check whether navmesh files exist
   for candidate zones (EQT_HAS_NAVMESH build + nav assets; investigate what
   the benchmark needs and what exists on disk). If navmesh paths work,
   designated frames come from points along that path.
2. **Fallback: automated interesting-view search.** If navmesh isn't
   available for a zone, add a small opt-in benchmark mode that samples many
   candidate camera poses (e.g., grid over walkable surface positions ×
   several headings), renders each once, scores by draw count × distinct
   colors × non-sky pixel fraction, and reports the top poses. Pick
   designated poses from the top scorers, then hard-code them in the
   manifest as explicit `--camera-pose x,y,z,heading,pitch` flags (add flag
   if needed) — deterministic and self-documenting. Scoring/search runs at
   bless time only, never in the gate.
3. **Acceptance bar per golden frame** (write these numbers in the review):
   ≥40 draws OR ≥2,000 polys; ≥1,500 distinct colors; ≤60% of pixels sky/void
   color; and a human-readable one-line description of what's visible
   ("qeynos2 gate plaza with buildings both sides"). All three zones, 3
   frames each. Keep qeynos2/gfaydark/befallen unless one simply cannot
   produce good frames — then substitute and document.
4. Rebless via `visual_gate.sh --bless`; regenerate manifest baselines and
   contact sheet; verify plain gate passes; keep the negative test evidence
   from H02 valid (re-run the perturbation check once against the new set).

## Verification

- All 9 frames meet the numeric acceptance bar; review lists per-frame
  stats + descriptions.
- `visual_gate.sh` passes; determinism re-confirmed on the new poses (two
  runs byte-identical).
- Default build + ctest unregressed.

## Review

### Navmesh path: not available — fallback used

The plan's preferred option (navmesh camera path) is a dead end on this
setup: `generateFromNavmesh` (tools/hwcaps/zone_bench_camera.cpp) requires
compiled `.nav` files and an `EQT_HAS_NAVMESH` build, and **zero `.nav`
files exist anywhere** — none under /mnt/projects/EverQuestP1999, none in
the repo. Went straight to the plan's fallback (automated interesting-view
search + hard-coded poses).

### New benchmark flags (all opt-in, default behavior unchanged)

`tools/hwcaps/gles2_zone_benchmark.cpp`:

- `--camera-pose X,Y,Z,HEADING,PITCH` (repeatable): replaces the moving
  navmesh/orbit path with static single-frame captures, one pose per frame.
  Pose i = frame i, which lines up 1:1 with the existing `--dump-frames` /
  manifest `designatedFrames` scheme — the gate script needed no changes to
  its capture flow, only manifest `extraArgs`. Heading/pitch convention is
  the exact inverse of the benchmark's existing metrics computation
  (0=North/+Y, CW; pitch positive=up), so CSV heading/pitch round-trips.
- `--pose-search N` (+ `--pose-search-headings/-pitch/-eye-height/-csv`):
  bless-time-only interesting-view search. Samples N anchor points spread
  across the zone's loaded **region bounding boxes** (guaranteed near real
  geometry — this is what fixed befallen, where surface-position heuristics
  kept landing outside the crypt shell), floor-anchored + eye height, at K
  headings each; renders each candidate once; scores
  `draws x distinctColors x nonSkyFraction`; prints the top 20 with ready-to-
  paste `--camera-pose` strings. Never used by the gate itself.

### The blessed set (all 9 frames, 800x600, llvmpipe)

Acceptance bar: >=40 draws OR >=2000 polys; >=1500 distinct colors; <=60%
sky/clear-color pixels. Every frame passes with huge margin:

| Frame | Pose (x,y,z,hdg,pitch) | draws | polys | colors | sky% | What's visible |
|---|---|---|---|---|---|---|
| qeynos2_f0 | 201.9,393.4,5.0,180,-5 | 236 | 885 | 53229 | 13.7% | Gate plaza, two-story buildings both sides, ivy wall |
| qeynos2_f1 | -134.9,-238.9,6.0,0,-5 | 119 | 542 | 109151 | 9.0% | Courtyard facing tiled-roof building entrance, ivy walls |
| qeynos2_f2 | -202.9,-706.7,20.0,90,-5 | 228 | 663 | 46140 | 0.0% | Temple interior: mosaic walls, pillars, vaulted ceiling |
| gfaydark_f0 | 820.0,84.9,110.0,0,-5 | 143 | 3338 | 24288 | 13.0% | Treehouse platform, big trunk, distant gazebo |
| gfaydark_f1 | 444.7,688.9,141.0,90,-5 | 134 | 3633 | 33850 | 33.5% | Gazebo rooftop close-up, connecting bridges |
| gfaydark_f2 | -440.9,72.3,110.0,180,-5 | 203 | 4421 | 23782 | 38.8% | Elevated bridge walkway between platforms |
| befallen_f0 | -554.8,-186.9,-50.1,270,-5 | 368 | 1241 | 19785 | 0.0% | Crypt altar room, checkerboard floor, staircase |
| befallen_f1 | -299.9,-181.9,-9.0,0,-5 | 75 | 384 | 18846 | 0.0% | Crypt doorway onto stone-railed hall |
| befallen_f2 | -149.9,-5.8,7.1,180,-5 | 170 | 526 | 36370 | 0.0% | Crypt balcony hall, balustrade, vaulted ceiling |

(colors = exact full-image distinct RGB count; sky% = fraction of sampled
pixels equal to the clear color 40,40,60. Draw counts are with occlusion
culling ON, i.e. the production-representative path.) All 9 viewed by eye —
real geometry, correctly textured, nothing clipped inside a wall or viewing
a dungeon from outside. Zones kept: qeynos2/gfaydark/befallen as required.
Same-zone per-frame descriptions are in the manifest
(`frameDescriptions`) and on the contact sheet.

### Real bug found: address-dependent draw order broke exact-match gating

The first blessed static set FAILED its own verify rerun with 1-33
differing pixels on scattered frames — different frames each run. Chased
through three wrong theories (occlusion-culler tie-breaks, lazy first-touch
GL state, "llvmpipe address sensitivity" — `setarch -R` made same-shell
runs reproducible, which *looked* like confirmation until the gate failed
again under ctest, whose different environment block shifts the layout even
with ASLR pinned). Actual root cause, found in
`tools/hwcaps/zone_bench_loader.cpp`: the material sort key was built from
the **low 16 bits of the texture pointer** —
`(uintptr_t)mat.getTexture(0) & 0xFFFF` — in all four draw-collection paths
(zone regions, objects, entities, batched entities). Heap addresses differ
run-to-run under ASLR (or any env-block size change), so equal-material
draws sorted into different orders → z-fighting flips on coplanar geometry
(1-33 px) and a wobbling `state_changes` count. The moving-orbit H02 set
never tripped this only because its frames' materialKey/distance pairs
happened not to collide ambiguously.

Fix: `textureGroupId()` — FNV-1a hash of the **texture name** (16-bit),
address-independent, applied to all four sites. After the fix (and after
reverting the two wrong-theory band-aids so the static path runs with
occlusion culling ON and no warmup hack): gate passes 5x consecutively from
a shell, under ctest, and with a deliberately perturbed 500-byte
environment variable — and two independent benchmark processes produce
**byte-identical PNGs** (md5) and identical CSV geometry columns. No
`setarch`/ASLR workaround needed or shipped.

### Verification

1. `visual_gate.sh --bless` → 9 PNGs + manifest (new metrics/p95/memory
   baselines) + contact sheet regenerated. Plain `visual_gate.sh`:
   **PASS x5 consecutive** + PASS under ctest + PASS with perturbed env.
2. Determinism: byte-identical PNGs across independent runs (md5-verified),
   CSV draws/state_changes/polys/regions identical; only timing columns vary.
3. Negative test re-run against the NEW set: 20x20 magenta patch into
   `gfaydark_f1.png` → gate FAILED with `PIXEL MISMATCH (400 differing
   pixels)` + wrote actual/golden/diff artifacts to
   `build-gles2/ci_artifacts/`; restored from backup (md5 match confirmed)
   → PASS again.
4. ctest SKIP path: renamed `build-gles2/` away → `visual_gate ... Skipped`,
   restored → PASS.
5. Default build (`cmake --build build -j24`) clean; full ctest: 1223/1225,
   the only 2 failures being the pre-existing documented flakes
   (`AudioManagerTest.PreloadCommonSounds` ALSA env,
   `RDPServerFrameTest.UpdateFrameMultipleTimes` load-dependent segfault —
   both in `refactor/test_baseline.txt`). `visual_gate` ctest entry PASSED
   in the full run. `check_file_sizes.sh`: exit 0; pre-existing WARNs only
   (gles2_zone_benchmark.cpp 1649→1969 lines, zone_bench_loader.cpp
   2742→2746 — both already over the 1500 warn line before this unit,
   far from the 3000 fail line).

### Deviations from the plan

1. **Navmesh option skipped** — no `.nav` assets exist anywhere (documented
   above); fallback search used for all three zones, per the plan's own
   ordering.
2. **The p95 CPU baselines changed** (e.g. befallen 19.5→~4 ms) — static
   poses are cheaper than the old orbit sweep; ceilings re-baselined by the
   bless as designed.
3. **`textureGroupId` fix touches the shared draw-collection helper**
   (`zone_bench_loader.cpp`), i.e. slightly beyond pure camera-placement
   scope — but the pointer-keyed sort made the exact-match gate design
   unsound for ANY camera (H02's set included; it just hadn't tripped yet),
   so it is squarely in "produce goldens that actually guard the renderer."
   Production renderer code untouched.
4. Frame indices in the manifest changed from 0/30/59 to 0/1/2 (one frame
   per pose); the old `_f30`/`_f59` goldens are deleted.
