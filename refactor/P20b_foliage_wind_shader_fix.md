# P20b — Foliage alpha: fix the wind-shader MaterialType clobber

**Slot: slot1. OWNS EQEMU (sole owner). x86 only; do NOT touch the Pi.**

Completes P20 Part A. P20's follow-up investigation (see
`refactor/P20_live_visual_pair.md` Review, correction block) established
the true root cause of Will's opaque-tree-foliage sighting with hard
evidence; this unit fixes it. Do not re-derive the diagnosis.

## Trusted findings (from P20 — do not re-derive)

- On the live x86 GLES2 client (EDT_OGLES2), `AnimatedTreeManager` is
  DEAD CODE: `loadZoneStep11_Env()` (`renderer_zone_load.cpp` ~1986-2003)
  only initializes it under `EDT_BURNINGSVIDEO`. Trees are built as
  ordinary placeables via `ZoneMeshBuilder::buildTexturedMesh()`, which
  classifies alpha correctly (H02d Option A).
- The bug: a wind-shader pass then UNCONDITIONALLY overwrites
  `MaterialType` on tree materials with no masked-flag or ETC1-vs-RGBA
  classification, at TWO sites:
  `renderer_zone_stream.cpp:559-576` and
  `renderer_zone_load.cpp:2288-2334`. Masked RGBA foliage lands on the
  ETC1-companion alpha-test variant with no companion bound → GLES2
  driver's opaque-white 1×1 unit-1 fallback (never discards) → solid
  opaque quads.
- Ground truth: `nekpine.bmp` (gfaydark pine, NEKPINE1/2_ACTORDEF) is
  WLD Fragment 0x30 TransparentMasked and its DXT5 texture carries a
  genuine hard-edged alpha cutout (47.1% near-zero / 52.9% near-full).
  This is NOT the citywlivy painted-opaque class.
- P20's `animated_tree_manager.cpp` change (already merged) is a correct
  H02d mirror for the software-renderer path; leave it alone.

## The fix

1. At both wind-shader sites, replace the unconditional MaterialType
   override with H02d Option-A classification, preserving the wind
   behavior: masked+ETC1 → wind/companion-alpha variant (companion bound
   on unit 1); masked+RGBA → a wind variant that reads `uTexture.a` (add
   it if the wind shader family lacks one, mirroring how
   `zone_shader.cpp` structures base vs RGBA alpha-test variants);
   non-masked → plain wind/solid variant. Follow the existing shader
   naming/wiring conventions.
2. Audit for OTHER unconditional MaterialType overrides of the same
   class applied after mesh build on the live path (grep the
   renderer_zone_* TUs); fix only tree/wind sites — the known
   door_manager/entity_renderer `setShaderMaterialTypes()` 2-arg gap is
   OUT OF SCOPE (tracked in deferred.md; needs its own evidence).

## Evidence bar (this failed once — be exact)

- Before/after stills MUST be same-pose by construction: identical
  `--script` move file, identical wait timings, screenshot at the same
  script step; state the move file's content in the Review. P20's
  round-1 "after" evidence was retracted because the move paths
  differed — do not repeat that.
- Two scenes minimum: gfaydark pine cluster (nekpine — the proven
  ground-truth case) and the qeynos2/qeynos gates trees from Will's
  original video complaint.
- Verify in the before stills that the opaque quads are present, and in
  the after stills that foliage shows the leaf-shaped cutout with
  background visible through negative space. If ANY tree in frame stays
  rectangular, identify why (painted-opaque texture = by-design; name
  the texture) in the Review — no unexplained rectangles.
- Stills to `/mnt/projects/fable/videos/p20_live_visual/stills/`
  (p20b_ prefix); update
  `benchmarks/audits/p20_live_visual/EVIDENCE_MANIFEST.txt`.
- You do NOT declare visual acceptability — Will judges the stills.

## Rules

- eqemu per EXECUTOR_PROTOCOL.md; restore Testchar (zone_id=36,
  x=-77.9232, y=21.9086, z=3, h=190) and shut down cleanly.
- Territory: `src/client/graphics/**` (renderer_zone_stream.cpp,
  renderer_zone_load.cpp, zone_shader.cpp if a wind-RGBA variant is
  needed, plus headers). No driver changes; no goldens; no server code.
- Goldens must stay untouched: the visual gate runs the benchmark
  loader, which never executes the wind pass — if the gate moves
  anyway, STOP and report.
- STOP conditions: root cause turns out different from the trusted
  findings above (report, don't improvise); fix would require driver
  texture/state-cache changes (P18 territory); goldens move.

## Verification

- Full 6-step bar (EXECUTOR_PROTOCOL.md); known ctest exceptions:
  AudioManagerTest.PreloadCommonSounds,
  RDPServerFrameTest.UpdateFrameMultipleTimes.
- Same-pose before/after pairs per the evidence bar, both scenes.

## Review

### Fix, per site

Both sites previously did `MaterialType = zoneShader_->getActiveWindAlphaTest()`
unconditionally on every material of a tree object, with no check of the
WLD masked flag and no ETC1-vs-RGBA branching (the H02d Option-A gap named
in the trusted findings). Both now classify **per-material** (by the bound
texture's name, looked up against `geometry->textureNames()`/
`textureMasked()`, exactly mirroring `ZoneMeshBuilder::buildTexturedMesh`):

- **masked + ETC1** → `getActiveWindAlphaTest()` (existing ETC1-companion
  wind variant), companion texture bound on unit 1 via
  `constrainedTextureCache_->getAlphaTexture()` (unchanged binding logic,
  now gated correctly).
- **masked + RGBA/DDS** (the actual live-client bug case — `nekpine.bmp`
  and friends) → new `getActiveWindAlphaTestRGBA()` variant, which reads
  `uTexture.a` directly instead of an unbound companion unit 1.
- **non-masked** (e.g. tree trunk textures) → new `getActiveWindSolid()`
  variant — still gets wind vertex displacement, no alpha-test discard.

`src/client/graphics/renderer_zone_stream.cpp:560-624` (deferred/streamed
object build): rewrote the per-material loop; this site previously never
attempted to bind an ETC1 companion texture at all (a second, smaller gap
beyond the missing classification) — now does, matching the load.cpp site.

`src/client/graphics/renderer_zone_load.cpp:2289-2373` (Step11 fixup pass
for eagerly-built objects): added a local `classify` lambda (texture name
-> wind material type) shared by both the mesh-buffer loop and the node-
material loop, replacing the single unconditional `windMatType` constant
used by both.

### Shader variants added (`include/client/graphics/zone_shader.h`,
`src/client/graphics/zone_shader.cpp`)

New wind-family material types, mirroring the existing non-wind
`materialAlphaTestRGBA_`/`materialLWAlphaTestRGBA_` H02d pair:

- `materialWindAlphaTestRGBA_` / `materialLWWindAlphaTestRGBA_`:
  `WIND_VERTEX_SHADER_(LIGHTWEIGHT_)SRC` + the platform's RGBA alpha
  fragment shader (`FRAGMENT_SHADER_ALPHA_RGBA_SRC` / GLES2's base variant
  / `FRAGMENT_SHADER_LIGHTWEIGHT_ALPHA_RGBA_SRC`), `EMT_TRANSPARENT_ALPHA_CHANNEL_REF`
  base type. `getActiveWindAlphaTestRGBA()` resolves lightweight-vs-per-pixel
  the same way every other `getActive*` pair does.
- `materialWindSolid_` / `materialLWWindSolid_`: `WIND_VERTEX_SHADER_
  (LIGHTWEIGHT_)SRC` + `FRAGMENT_SHADER_(LIGHTWEIGHT_)SOLID_SRC`, `EMT_SOLID`
  base type — for non-masked tree materials, so they keep wind sway without
  picking up alpha-test discard semantics. `getActiveWindSolid()` added
  alongside.

All four are non-fatal to compile (LOG_WARN + `-1`, matching every other
optional shader in this file); call sites fall back to
`getActiveWindAlphaTest()` when a new variant's compile failed, so a
driver that can't compile the new programs degrades to the pre-fix
(but at least always-alpha-tested, never worse) behavior rather than
breaking.

### Audit for other unconditional MaterialType overrides (plan step 2)

Grepped `renderer_zone_stream.cpp`/`renderer_zone_load.cpp` for
`MaterialType = `. Two other sites found in `renderer_zone_stream.cpp`
(~line 212, ~line 335) unconditionally set `MaterialType =
zoneShader_->getActiveAlphaTest()` — but both are late-arriving-texture
fallback-buffer paths for **ordinary (non-tree) region geometry**, already
gated on `hasAlpha` (a real classification check, not a blanket override),
and outside this unit's tree/wind territory. Not touched. The
`treeManager_->setShaderMaterialTypes()` 2-arg gap
(`irrlicht_renderer.cpp:822`, `renderer_debug.cpp:311`) is the already-known
`AnimatedTreeManager` (software-renderer, P20) call site — confirmed
out of scope per the plan and `refactor/deferred.md`, not touched.

### Same-pose evidence

Both scenes captured live against eqemu (`scripts/eqemu/up.sh`/`down.sh`),
real x86 GLES2 client (`build-gles2/bin/willeq --renderer gles2`), stills
at `/mnt/projects/fable/videos/p20_live_visual/stills/` (`p20b_` prefix,
manifest updated at
`benchmarks/audits/p20_live_visual/EVIDENCE_MANIFEST.txt`). BEFORE captures
used a temporarily-reverted copy of the two wind-clobber call sites
(original unconditional-clobber code restored in-place, `zone_shader.{h,cpp}`
additions left in place but inert/unreferenced) rebuilt into
`build-gles2/bin/willeq`, then the fix was restored and rebuilt for AFTER —
same commit-tree state (`fable` tip) as the actual "before" code, so this
is a faithful pre-fix/post-fix pair, not an approximation.

**gfaydark** — move-script (`--script`, identical between BEFORE/AFTER
runs, only the screenshot filename suffix differs):
```
wait 10
/say #zone gfaydark
wait 20
/say #time 12
wait 2
/screenshot .../p20b_gfay_00_BEFORE.png
/move 10 20 0
wait 6
/screenshot .../p20b_gfay_01_BEFORE.png
/move 10 60 0
wait 6
/screenshot .../p20b_gfay_02_BEFORE.png
/move 0 100 0
wait 6
/screenshot .../p20b_gfay_03_BEFORE.png
wait 1
/camp
```
`p20b_gfay_02_BEFORE.png`/`p20b_gfay_03_BEFORE.png` (and `_01_BEFORE.png`)
show the large center pine's canopy as a solid, hard-edged, opaque
dark-green rectangular panel with no needle silhouette — matches Will's
report and the trusted `nekpine.bmp` ground truth exactly.
`p20b_gfay_01_AFTER.png`/`p20b_gfay_02_AFTER.png` (same pose) show the same
pine canopy now rendering with a visible needle-cluster silhouette and sky
visible through the negative space around it — only the foliage material
changed, camera/pose identical by construction.

**qeynos2** — move-script (identical between BEFORE/AFTER):
```
wait 10
/say #zone qeynos2
wait 20
/say #time 12
wait 2
/turn 180
wait 2
/screenshot .../p20b_qeynos2_00_BEFORE.png
/turn 165
wait 2
/screenshot .../p20b_qeynos2_01_BEFORE.png
/turn 195
wait 2
/screenshot .../p20b_qeynos2_02_BEFORE.png
wait 1
/camp
```
(Deviation from the originally-planned `/move`-based approach — see
Deviations below.) `p20b_qeynos2_00/01/02_BEFORE.png` show a pine tree
(right of a willow) as a solid opaque rectangular panel.
`p20b_qeynos2_00/01/02_AFTER.png` (same pose — turn-only sequence, so
same-pose-by-construction is exact) show the same pine with a correct
needle-shaped canopy silhouette. A pale, fog-tinted tree-shaped silhouette
visible in the background behind the "Testchar" nametag in both BEFORE and
AFTER frames was checked by zoomed crop: it already has a correct tapered
pine outline (not a rectangle) in both frames — a distant, fog-attenuated
tree rendering correctly, not an instance of this bug. No unexplained
rectangles remain in any AFTER frame for either scene.

### Verification bar

| Step | Result |
|---|---|
| 1. `cmake --build build -j24` | PASS |
| 2. `ctest --test-dir build --output-on-failure` | 1233/1235 run passed; 2 failed — both documented known host exceptions (`AudioManagerTest.PreloadCommonSounds`, `RDPServerFrameTest.UpdateFrameMultipleTimes` SEGFAULT/intermittent) |
| 3. `bash scripts/build-gles2-x86.sh` | PASS |
| 4. Visual gate | `bash scripts/ci/visual_gate.sh` → PASS (qeynos2/gfaydark/befallen), goldens untouched, no re-bless; also included as ctest's own `visual_gate` case, PASS |
| 5. `bash scripts/build-arm-noble.sh` | PASS |
| 6. `bash scripts/refactor/check_file_sizes.sh fable` | No FAILs. 2 pre-existing WARNs, unchanged in kind from before this unit: `renderer_zone_load.cpp` (2913 lines, was 2873 on `fable`) and `zone_shader.cpp` (2561 lines, was 2484 on `fable`) — both already over the 1500-line warn threshold before this unit; this unit's additions (52 and 81 net lines respectively) did not newly cross any threshold. `renderer_zone_stream.cpp` (1408) and `zone_shader.h` (301) both `ok`. |

eqemu: brought up/down repeatedly across the session (gfaydark, qeynos2,
befallen), sanctioned `scripts/eqemu/up.sh`/`down.sh` only; a full
`down.sh` + `up.sh` cycle was needed a few times mid-session after the
client hung during EGL/connect startup under host CPU load (the
`akk-stack-eqemu-server-1` container auto-starts its own full static-zone
launcher fleet on top of `up.sh`'s single dedicated zone process,
contending for CPU on this shared host) — not a code issue, a host/
container timing flake; retrying after a clean restart, or with a longer
timeout, always resolved it. Testchar restored via a documented direct
DB write (`character_data` `UPDATE ... SET zone_id=36, x=-77.9232,
y=21.9086, z=3, heading=190 WHERE name='Testchar'`, verified by
re-`SELECT`) after the in-game `#goto`/`#setheading` GM commands were
found not to have taken effect (a client shutdown-thread abort —
`std::system_error: Resource deadlock avoided`, the same pre-existing
benign post-`/camp` teardown crash referenced in P10b — interrupted that
session before the position committed). Server shut down cleanly
(`down.sh`) at the end; confirmed via `status.sh` (no containers, ports
closed).

### Deviations

1. qeynos2 evidence used a `/turn`-only sequence (`/turn 180` ->
   `/turn 165` -> `/turn 195`) from the zone's default `#zone`-teleport
   spawn point, instead of the `/move`-based approach the plan's
   `p20_approach.txt` precedent used. Two things were tried first and
   rejected: (a) reusing `p20_approach.txt`'s exact `/move` targets from a
   fresh `#zone` teleport landed the camera facing a wall/void (no trees in
   frame) — that script assumed a starting position near the gates that a
   `#zone` teleport doesn't reproduce; (b) `#goto -74 428 3` (the
   coordinate P20's Part B section cites as "near the gates") also did not
   land near visible trees. A `/turn`-based scout at the default `#zone`
   spawn point (heading 180) found trees in frame directly, was adopted for
   both scenes, and is *more* rigorous for same-pose-by-construction than
   `/move` (no absolute-coordinate assumptions at all — camera position
   never changes, only heading).
2. Local, UNCOMMITTED workarounds applied during evidence capture, then
   reverted before the final commit (same pattern as P20's own follow-up
   pass, EXECUTOR_PROTOCOL.md's instruction for parked live-client
   issues): (a) widened `saveScreenshot()`'s safe-readback compile guard
   from `EQT_HAS_DRM` to `EQT_HAS_DRM || EQT_HAS_GLES2`
   (`irrlicht_renderer.cpp`) — the x86 GLES2 dev build compiles that guard
   out entirely, falling through to `driver_->createScreenShot()`, which
   segfaults on Mesa llvmpipe; (b) degraded the `LOG_FATAL`-and-abort-zone-
   load in `loadZoneStep8a_BaseModelPrep()` (missing race=330 model,
   `refactor/deferred.md`) to a `LOG_WARN`-and-skip-this-entity, otherwise
   making any live gfaydark session impossible. Both fully reverted before
   the final build/verification pass and the commit below; `git diff
   --stat` at commit time contains only the wind-shader-fix files plus the
   evidence-manifest update.
3. Added `/say #time 12` (GM daytime-set) as the first in-scene command in
   both tour scripts, applied identically to BEFORE and AFTER runs. Initial
   BEFORE captures at whatever in-game time-of-day the zone-in landed on
   were valid but visually harder to read (night lighting reduces
   sky-vs-canopy contrast); forcing daytime is a same-pose-by-construction-
   compatible addition (present in both halves of every pair) that makes
   the opaque-vs-cutout distinction unambiguous in the stills.
