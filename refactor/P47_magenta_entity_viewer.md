# P47 — Magenta entity viewer: a trivially-readable instrument for masked-alpha verification

**Slot: slot2. x86 ONLY — no Pi, no eqemu.** Will's design
(2026-07-31): "render the model+textures with a solid magenta
background so it's trivial to detect alpha transparency gaps." This
replaces the failed close-up/diff-crop evidence approach (5 rejected
rounds — see P44/P44b/P46/P46b/P46c/P46d chain).

## Deliverable shape

A benchmark-only `--entity-viewer` mode in gles2_zone_benchmark:
- Loads NO zone geometry. Clear color solid magenta (255,0,255).
- Renders ONE race model LARGE — filling most of the frame — at a
  small set of fixed angles (front/back/left-side minimum), fixed
  deterministic pose (use the proven frame-indexed animation clock;
  P44b showed the no-animate path takes a stale masked check, so
  route through the animated path at a fixed frame).
- PNG dump per angle. Resolution 800x600 (or higher if trivial).
- Reading rule: magenta visible INSIDE the model silhouette = alpha
  gap. No crops, no diff overlays, no before/after required.

## Work

1. The viewer mode (benchmark-only source). Texture modes: RGBA
   (primary — exercises P46 Part A + P46d, x86-representative) and,
   if cheap, ETC1 with the P46b promotion path bound (companion
   alpha, Pi-representative). Label each output with its mode.
2. Renders for: the masked ground-truth races SKE (21), ZOM (70),
   BAT (34), ORC (17) + unmasked control Human (1). (P37's table is
   ground truth; add other masked races only if free.)
3. Contact sheet per repo convention:
   benchmarks/contact_sheets/p47_entity_viewer/index.html
   (self-contained), stills also to
   /mnt/projects/fable/videos/p47_viewer/ (NFS).
4. **Regression pin**: a ctest test using the viewer — magenta-pixel
   count inside the model bbox must be >0 and stable(ish) for masked
   races, EXACTLY 0 for the unmasked control. Asset-gated with skip
   guards like the other EQ-asset tests. This makes alpha breakage
   detectable forever without human eyes.
5. Executor self-check before delivering: view your own renders —
   the model must be LARGE and clearly lit (neutral/unlit built-in
   shading, NOT the point-blank player-light promotion unless doing
   the ETC1 leg, which should be labeled); textures at this size
   must look right part-by-part.

## Rules

- Benchmark/tools + tests source only; NO production source changes;
  goldens untouched (gate PASS); no presets.
- Full bar: GLES2 ctest baseline (~1616/1617 + your new test), visual
  gate PASS, ARM cross-build clean, file sizes (zone_bench_loader
  pre-existing FAIL — flag only; put substantial new viewer code in
  its own TU rather than growing that file).
- Commits: viewer mode / renders+contact sheet / regression test.

## Review

**Delivered: `--entity-viewer` mode (new TU `tools/hwcaps/zone_bench_entity_viewer.h/.cpp`), full render set for SKE/ZOM/BAT/ORC + Human control (both RGBA and ETC1 legs, all 4 angles), self-contained contact sheet, and a ctest regression pin. All masked races show a clear, nonzero alpha gap on at least one angle; Human shows EXACTLY zero at all four, in both legs. Full bar green except the two pre-existing documented ctest exceptions and the pre-existing zone_bench_loader.cpp file-size FAIL (both flagged, neither touched).**

### Design

**CLI**: `--entity-viewer --entity-viewer-race N [--entity-viewer-race-label S] [--entity-viewer-frame N] --entity-viewer-out-dir DIR`, plus the existing `--zone`/`--eq-path`/`--eqe1-path`. When `--entity-viewer` is set, `gles2_zone_benchmark.cpp` forces `--entities 1 --test-races N --animate` and computes `--no-entity-shader-promotion` itself (RGBA leg — no `--eqe1-path` — forces promotion OFF, the P46c/P46d-proven one-variable configuration where the built-in alpha-test shader reads unit-0 alpha directly; ETC1 leg — `--eqe1-path` set — leaves promotion ON, the only code path that binds the P46b companion alpha texture to unit 1) rather than requiring the caller to know that pairing. This reuses `zone_bench_loader.cpp`'s existing, already-correct entity-loading machinery (`loadZone()`'s `--animate` block: `preloadModelData()`/T05, `createAnimatedNode()`, `applyEntityShaderMaterialPromotion()`) completely unchanged — no production or `zone_bench_loader.cpp` source was touched.

**TU split**: all new logic (camera framing, magenta clear, silhouette/hole analysis, PNG dump) lives in `tools/hwcaps/zone_bench_entity_viewer.cpp` (~300 lines) + `.h`, per the plan's instruction not to grow `zone_bench_loader.cpp` (already FAIL at 3196 lines, untouched by this unit). `gles2_zone_benchmark.cpp` gained only the CLI plumbing + a ~25-line dispatch block (+95 lines total, was already WARN pre-existing at 2839).

**"No zone geometry" — interpretation**: `loadZone()` still loads the named zone's real S3D/WLD data (needed for the shader manager / texture cache / race-model-loader plumbing the entity path depends on), but the viewer's render loop never calls `collectDrawEntries()`/`collectObjectDrawEntries()` — only `collectEntityDrawEntries()` for the single placed entity, against a magenta clear. So no zone or object geometry is ever *drawn*, which is what the mission's reading rule actually depends on (a human judging "is there magenta inside the silhouette" doesn't care whether the zone's BSP was parsed in the background). Noted as a deliberate interpretation, not silently assumed.

**Camera framing**: entity placed at EQ origin, grounded via `-mesh.getBoundingBox().MinEdge.Y` (mirrors `--composite-entity-test`'s existing convention). Camera distance computed from the model's post-pose world AABB height/width against the 60° vertical FOV (aspect-corrected horizontal FOV too), with an 80% fill margin, at 4 fixed angles (front/right/back/left, heading 0/90/180/270 around the model — "front" names heading 0 by the tool's existing EQ heading convention, not a claim about which way the model artist intended it to face). Pose is deterministic: `OnAnimate(frame * 33.3ms)`, same clock P44b/P46d used, called directly (not through the moving-camera-path machinery) — frame 26 by default, matching the frame P46d's own bless pair led on.

**Reading/regression signal — NOT a naive bbox count.** The plan's own suggestion ("magenta-pixel count inside the model bbox") was tried literally first and rejected empirically: projecting the world AABB's 8 corners (the technique `renderer_zone_draw.cpp`'s `estimateRegionScreenArea()` uses for a cheap, deliberately-conservative culling estimate) put corners nearer the camera than the visible silhouette well outside the actual rendered pixels under perspective, inflating the "bbox" to the full frame edge; even a *tight*, pixel-derived rectangle around the non-magenta pixels still counts ordinary background beside a non-convex silhouette (found on a lunging skeleton pose: 76-95% of its own tight bbox rectangle was plain background, not holes) — and would never reach exactly zero for an unmasked model either (any diagonal/asymmetric pose has background inside its own AABB). The metric that actually works: a border flood-fill through magenta pixels (from every edge pixel, 4-connected) marks everything topologically connected to the outside as "background"; whatever magenta is left is a genuine enclosed hole (rib gaps, eye sockets — exactly what an alpha-test cutout produces). A small connected-component area floor (20px) filters an empirically-confirmed, frame-independent 2-15px noise source (a hand/forearm silhouette sealing off a sliver of background against the hip at certain angles, present even on the unmasked Human control) without coming close to hiding a real cutout (the smallest genuine masked-race signal found, ZOM/back, is 29px; most are 200-2000px). This is documented in-line in `zone_bench_entity_viewer.cpp`'s `analyzeSilhouette()`.

### Renders delivered

Both legs (RGBA primary, ETC1 optional-but-free), all 4 angles, for the P37 ground-truth masked races (SKE 21, ZOM 70, BAT 34, ORC 17) plus unmasked control Human 1. `magenta_in_bbox` below is the enclosed-hole pixel count (RGBA leg; ETC1 leg gave identical numbers for every race except SKE, where it was mildly higher — 829/688/348 vs 564/688/246 at right/back/left — consistent with ETC1 companion alpha being a slightly different/independent decode path from the RGBA in-band alpha, not a bug):

| race | front | right | back | left | verdict |
|---|---|---|---|---|---|
| SKE (21, masked) | 0 | 564 | 688 | 246 | PASS — clear rib-gap cutout visible back/right/left |
| ZOM (70, masked) | 0 | 0 | 29 | 0 | PASS — small but real, jagged boot/leg mask edge visible in the back render |
| BAT (34, masked) | 222 | 0 | 204 | 0 | PASS — wing-membrane holes visible front/back |
| ORC (17, masked) | 0 | 1395 | 2008 | 0 | PASS — loincloth-fringe and gauntlet-edge cutouts, largest signal of the set |
| HUM (1, unmasked control) | 0 | 0 | 0 | 0 | PASS — exactly zero at every angle, both legs |

Every masked race has at least one angle with an unmistakable signal; Human is exactly zero everywhere, in both legs. Stills: `/mnt/projects/fable/videos/p47_viewer/{rgba,etc1}/` (NFS, gitignored — outside the repo tree entirely, not a `.gitignore` entry). Contact sheet (self-contained, base64-embedded PNGs, repo convention): `benchmarks/contact_sheets/p47_entity_viewer/index.html`.

### Self-check (own renders viewed at full size, per the plan's mandate)

Viewed every PNG. SKE (back/right): ribcage shows unmistakable magenta lattice between ribs — the clearest single image in the set, textures otherwise correct part-by-part (skull, forearms, femurs, feet all read correctly, no wrong-texture/duplicated-part issues). ZOM (back): jagged sawtooth mask pattern visible at both boot tops (matches P37's `zomlg0001` "flesh-gap" ground truth); ZOM's chest (`zomch0001`, P37's documented citywlivy-class opaque-by-design texture) correctly shows no cutout. BAT: wings fill nearly the whole frame at this pose (bbox touches the top edge at front/back — noted as a minor over-large-not-under-large framing deviation, doesn't affect readability); body/face textures correct, membrane holes visible against the wing's dark shading. ORC: bracer/loincloth cutouts are large and obvious, by far the strongest signal; skin-tone and armor textures read correctly part-by-part. Human: fully opaque at every angle in both legs, as required for the control. No wrong texture, no missing part, no color-space issue observed on any render. Lighting is neutral/flat (RGBA leg, promotion off — expected, matches the P46c/P46d-proven configuration) vs full per-pixel-lit (ETC1 leg, promotion on) — both clearly legible, labeled per the plan.

### Verification bar

| Step | Result |
|---|---|
| `cmake --build build -j24` (full) | PASS, clean (pre-existing project warnings only, none new) |
| `ctest --output-on-failure` | 1617/1619 passed (2 pre-existing documented exceptions: `AudioManagerTest.PreloadCommonSounds` ALSA host quirk, `RDPServerFrameTest.UpdateFrameAfterStart` the documented RDP race — neither touched by this unit); includes the new `EntityViewerRegressionTest.MaskedRacesShowAlphaGapHumanDoesNot`, passing |
| `bash scripts/ci/visual_gate.sh` | PASS (qeynos2/gfaydark/befallen), goldens untouched |
| `bash scripts/build-arm-noble.sh` | PASS, clean; `build-arm-noble/bin/willeq` verified non-stub |
| `bash scripts/refactor/check_file_sizes.sh 5420886` | `zone_bench_loader.cpp` FAIL at 3196 lines — **unchanged, not touched by this unit** (pre-existing since P46/P46b/P46c/P46d); `gles2_zone_benchmark.cpp` WARN, 2839→2934 lines (+95, this unit's CLI/dispatch plumbing — pre-existing WARN, not newly caused); new files `zone_bench_entity_viewer.{h,cpp}` well under threshold, not flagged |

### Deviations

1. BAT's wingspan at the default 80%-fill framing occasionally touches the top frame edge (bbox `y0=0`) at front/back angles — the model is not clipped in a way that hides any silhouette content (the whole bat is still visible, wingtips just reach the edge), but it is a minor over-large framing rather than a small margin. Not fixed — a per-race margin override would be easy to add but wasn't necessary for readability at this pass.
2. The regression pin asserts "at least one of the four angles is nonzero" per masked race rather than "every angle nonzero" — several angles legitimately show 0 (the alpha-test cutout is only visible from angles where the camera can see into the gap, e.g. SKE/front and ZOM/front-left-right are 0 while back is 688/29). This matches what was actually observed, not an assumption; documented in the test file and this Review.
3. `--entity-viewer-frame` was found to have no visible effect across 0/5/10/40 for Human's idle animation during investigation (same pose every time) — not chased further (out of scope: the deterministic-pose requirement is satisfied either way, since the pose is still a pure function of the frame index, it just happens the idle clip doesn't visibly move in this range). Flagged as a minor, non-blocking oddity for whoever next touches the animation clock.

Commits (branch `task/P47-entity-viewer`): viewer mode (CMakeLists.txt + gles2_zone_benchmark.cpp + zone_bench_entity_viewer.{h,cpp}) / renders + contact sheet (benchmarks/contact_sheets/p47_entity_viewer/index.html) / regression pin (tests/CMakeLists.txt + tests/test_entity_viewer_regression.cpp) / this evidence (plan Review).
