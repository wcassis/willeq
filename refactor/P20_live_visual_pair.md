# P20 — Live-client visual pair: foliage alpha + geometry pop-in

**Slot: slot1. OWNS EQEMU (sole owner — no sibling agents this dispatch).
x86 only; do NOT touch the Pi.**

Two production-visible defects Will identified with his own eyes in the
live-client gameplay videos (llvmpipe capture sessions, qeynos2/qeynos
gates area, 2026-07-27 video review):

(a) **Tree foliage alpha is OPAQUE in the live client** — solid dark
    quads where masked leaf textures should be transparent. NOTE: the
    H02c/H02d arc already fixed this class in the GLES2 *benchmark/golden*
    path (alpha-source-aware shader selection, Option A) and gfaydark
    goldens render foliage correctly today — so this is a divergence
    between the live-client zone path and the (fixed) golden path.

(b) **World geometry sections missing, then appearing as the camera
    moves.** Standing hypotheses (from the review, in order):
    (1) `deferredAssetLoading` progressive build working as designed —
    i.e. cosmetic-by-design, verdict is a report, or (2) a real
    culling/BSP/PVS/occlusion bug. Distinguish them with evidence.

Both share one reproduction harness (a live session against eqemu), which
is why they are one unit. They are otherwise independent investigations —
do not force a shared root cause.

## Background (trusted prior findings — do not re-derive)

- H02d landed alpha-source-aware alpha-test shaders: RGBA/DDS-pipeline
  textures read their OWN alpha (`uTexture.a`); the companion
  `uAlphaTexture` (unit 1) variant is for ETC1 only. The GLES2 driver has
  an opaque-white 1×1 unit-1 fallback (COpenGLES2Driver ~1090-1101) —
  masked materials that end up on the companion variant WITHOUT a real
  companion render solid. Three zone_geometry.cpp sites were fixed to
  select the correct variant. Shader debug mode 12 (`companionAlpha`)
  exists; `/shaderdebug` in the live client can visualize alpha sources.
- `citywlivy.bmp` (qeynos ivy) is WLD-flagged masked but painted opaque —
  renders solid BY DESIGN under Option A. Not a bug; don't chase it.
- The video capture sessions' tooling and workarounds live UNMERGED on
  branch `task/anim-video-capture` (consult READ-ONLY for session
  scripting, camera placement at the qeynos gates, and the known
  live-client crash workarounds: befallen first-frame SIGSEGV,
  item_models FATAL, screenshot guard, ParticleManager race). Videos +
  README: `/mnt/projects/fable/videos/p12_anim_check/`. Do NOT merge or
  cherry-pick that branch's workarounds into your commits; if one of
  those crashes blocks you, apply the minimal workaround LOCALLY
  (uncommitted) and say so in the Review.
- P17 (already on fable) made zone unload synchronous and fixed the
  snapshot race — the live client you build from fable tip includes it.
- First, determine WHICH renderer path the capture videos used (check
  the videos README / capture branch scripts: desktop-GL build under
  Xvfb vs GLES2-x86 surfaceless). Reproduce in THAT path first; then
  check whether the other path shares the defect. Fixes must be correct
  for the path(s) actually shipped: GLES2 is the Pi production path;
  desktop-GL x86 is the dev/live-review path — a divergence between
  them is itself a finding to document.

## Part A — foliage alpha (root-cause and fix)

1. Reproduce: live session, camera at trees (qeynos hills side of the
   gates has trees in frame per the gates videos; gfaydark is the
   richest foliage zone if reachable via GM #zone — Testchar is GM).
   Capture stills showing the opaque quads. If the live client renders
   foliage CORRECTLY in your session, STOP Part A and report — the
   defect may have been environment- or build-specific (cross-ref the
   P12-era zero-source-diff foliage gate mismatch on slot2, deferred.md
   ~line 201).
2. Diagnose which stage loses the transparency on the LIVE zone path
   (not the benchmark loader — that one is fixed). Suspects in order:
   (a) live zone-load material classification (mesh_building.cpp /
   zone_geometry.cpp routes the live path takes vs the benchmark
   loader's) — is the masked WLD flag mapped to the alpha-test material
   at all on this route; (b) alpha-source variant selection on the live
   route (an H02d-class site the fix didn't cover — e.g. the deferred /
   progressive asset path building materials before alpha data exists);
   (c) texture decode dropping alpha on the live route; (d) desktop-GL
   only: EMT_TRANSPARENT_ALPHA_CHANNEL_REF mapping on the live path.
   Use shader debug modes (7=alpha, 11=rawTexAlpha, 12=companionAlpha)
   before writing any fix.
3. Fix at the true root cause. Before/after stills from the SAME camera
   pose, saved under `benchmarks/audits/p20_live_visual/`.
4. The x86 GLES2 goldens must be UNTOUCHED (visual gate PASS,
   no re-bless). If your fix moves any golden pixel, STOP and report —
   golden changes require Will's re-bless, which is the orchestrator's
   to arrange, not yours to accept.

## Part B — geometry pop-in (diagnose; fix only if it's a real bug)

1. Reproduce: scripted camera movement through the area where the
   videos showed it (approach to the qeynos gates). Capture a frame
   sequence (every-frame stills or video per the capture branch's
   method) demonstrating the missing→appearing behavior.
2. Distinguish the hypotheses with a controlled experiment, e.g.:
   run the same camera path with `deferredAssetLoading` disabled (find
   the actual config/flag name — verify it exists on the live path) —
   if pop-in vanishes, it's the progressive build; if it persists, it's
   culling/BSP/PVS/occlusion. Also check whether the popped-in sections
   correspond to BSP regions (PVS culling granularity) vs individual
   meshes/assets (loader granularity) — the SHAPE of what's missing is
   diagnostic.
3. If progressive-build-by-design: REPORT-ONLY verdict — document the
   mechanism, measure how long sections stay missing at gameplay move
   speed, and propose (don't implement) mitigation options. If a real
   culling bug: root-cause, fix, before/after frame sequences, goldens
   untouched (same STOP rule as Part A step 4).
4. Note for your experiment design: the videos were captured at ~7fps
   under llvmpipe + recording overhead — some "pop-in" may be loading
   that would be invisible at real frame rates. Quantify in frames AND
   wall-clock time; call this out explicitly in the verdict.

## Rules

- eqemu per EXECUTOR_PROTOCOL.md: sanctioned start/stop only, never
  modify server code, restore Testchar to the documented befallen
  coordinates and shut the server down cleanly when finished.
- Territory: `src/client/graphics/**`, `docker/irrlicht-drm*/**` (driver,
  only if the root cause truly lives there), capture/audit scripts under
  `benchmarks/audits/p20_live_visual/`. Server code and `tools/hwcaps/`
  are off-limits. Do not touch goldens.
- Evidence discipline: every claim in the Review needs an artifact
  (still, frame sequence, log) committed under
  `benchmarks/audits/p20_live_visual/`. Keep videos OUT of git — write
  them to `/mnt/projects/fable/videos/p20_live_visual/` and reference
  paths. You do NOT declare visual acceptability — Will does; your job
  is faithful evidence.
- STOP conditions: (i) either defect does not reproduce (report what you
  observed instead); (ii) a fix would require touching goldens or server
  code; (iii) the parked live-client crashes block a session even with
  local workarounds; (iv) Part A root cause lands in the GLES2 driver's
  texture/state caching — that collides with P18's open wrap-param
  investigation; document the overlap and stop rather than half-fixing.

## Verification

- Full 6-step bar (EXECUTOR_PROTOCOL.md) for any production code change:
  build, ctest (known exceptions: AudioManagerTest.PreloadCommonSounds,
  RDPServerFrameTest.UpdateFrameMultipleTimes), gles2-x86 build, visual
  gate PASS with goldens untouched, arm-noble build, file sizes.
- Part A: before/after stills, same pose, foliage transparent after.
- Part B: verdict backed by the controlled experiment + frame sequences.

## Review

**Renderer path used**: the P12/P08d capture videos (README:
`/mnt/projects/fable/videos/p12_anim_check/README.txt`) were captured with
the real x86 GLES2 client (`build-gles2/bin/willeq`, EGL surfaceless
llvmpipe) — the SAME path as the fixed golden/benchmark tier, just via the
live zone-load route instead of the offline benchmark loader. Reproduced in
that exact path throughout (no need to also check desktop-GL — the videos
never used it).

### Part A — foliage alpha: PARTIALLY DIAGNOSED — committed fix is real but INERT on the live GLES2 client; the true live-client root cause was found in a follow-up pass and is a separate, still-unfixed site (STOP per plan, not expanded here)

**CORRECTION (follow-up pass, post-commit review)**: this section was rewritten
after orchestrator review found the original "ROOT-CAUSED AND FIXED" verdict
did not hold up under a crop-level before/after comparison (the large center
pine and an adjacent white-trunk tree render on unchanged, pixel-identical
opaque rectangles in the "AFTER" evidence). Investigating why surfaced a
larger problem than one unfixed tree: **the code fix this unit committed to
`AnimatedTreeManager::buildAnimatedMesh()` never executes on the live x86
GLES2 client that produced these screenshots.** Full corrected account below;
the original root-cause writeup for the *code bug itself* (paragraph below,
"Original root cause") is accurate as far as it goes — it is simply describing
a code path that is dead on this renderer backend.

**Why the fix is inert here**: `IrrlichtRenderer::loadZoneStep11_Env()`
(`src/client/graphics/renderer_zone_load.cpp:1986-2003`) only calls
`treeManager_->beginInitialize()` (which is what invokes
`AnimatedTreeManager::buildAnimatedMesh()`, the function this unit's fix
modified) when `driver_->getDriverType() == irr::video::EDT_BURNINGSVIDEO`
(the software renderer). The live x86 GLES2 client used for every capture in
this unit reports `EDT_OGLES2` (`docker/irrlicht-drm/COpenGLES2Driver.h:272`)
— it takes the explicit `else` branch, logged as `"GPU renderer path, trees
handled as normal objects with wind shaders"`, and never calls
`beginInitialize()` at all. `AnimatedTreeManager` is constructed but its tree
mesh-building code is simply never reached on this backend. The committed fix
is a legitimate correctness improvement for the software-renderer path (and
harmless — it doesn't fire there either, in the capture sessions), but it does
not touch anything the GLES2 screenshots exercise.

**What the GLES2/live path actually does for trees**: placeable tree objects
are built as ordinary zone geometry via `ZoneMeshBuilder::buildTexturedMesh()`
(`src/client/graphics/eq/zone_geometry.cpp:396-445` etc.) — which already has
correct masked/ETC1-vs-RGBA classification (the same H02d Option-A logic this
unit mirrored into `animated_tree_manager.cpp`). But immediately after,
**every mesh whose object name/texture matches `TreeIdentifier`'s tree
patterns has its `MaterialType` unconditionally overwritten** by a "wind
shader" pass, in two sites this unit did not touch:
- `src/client/graphics/renderer_zone_stream.cpp:559-576` (deferred/streamed
  object build)
- `src/client/graphics/renderer_zone_load.cpp:2288-2334` (Step11 "fixup pass"
  for eagerly-built objects)

Both force `MaterialType = zoneShader_->getActiveWindAlphaTest()` — a single
material-type constant — with **no check of the WLD masked flag and no
ETC1-vs-RGBA branching**. Both sites do attempt to bind a companion alpha
texture on unit 1 via `constrainedTextureCache_->getAlphaTexture(texName)`,
but that call only returns non-null for ETC1-sourced textures; for
RGBA/DDS-decoded textures (the live x86 GLES2 build's actual texture source,
per this unit's own root-cause narrative) it returns null, unit 1 stays
unbound, and the GLES2 driver's opaque-white 1×1 fallback for an unbound unit
1 never discards — **the exact same bug class this unit fixed in
`animated_tree_manager.cpp`, reproduced verbatim in a different file that
remains unfixed.** `treeManager_->isTreeObject()` (used by both clobber sites
to decide "is this a tree") delegates to `TreeIdentifier::isTreeMesh()`,
which matches on name/texture patterns including `*pine*` — `nekpine.bmp`
(gfaydark's primary conifer foliage texture, confirmed below) matches.

**Confirmed the texture itself is a genuine bug, not a citywlivy-style
by-design opaque case**: extracted `gfaydark_obj.s3d`/`gfaydark.wld` with the
repo's `wld_dump`/S3D tools (temporary, uncommitted local build of a small
WLD-flag dump using `WldLoader`, deleted after use — no repo files changed)
and confirmed `nekpine.bmp` (pine foliage — actor `NEKPINE1_ACTORDEF` /
`NEKPINE2_ACTORDEF`, both reference `TREECALLBACK`) has WLD materialType
TransparentMasked (Fragment 0x30, `masked=YES`), stored as DXT5 (256×256)
with a genuine hard binary alpha cutout in the decoded pixel data (47.1% of
texels near-zero alpha, 52.9% near-full alpha, ~0% mid-range — a real
leaf-silhouette mask, not a flat-opaque bitmap). This is unlike
`citywlivy.bmp`, which is masked-but-painted-opaque by design. `nekpine.bmp`
foliage genuinely should render as a leaf-shaped cutout; it does not, because
of the still-unfixed clobber sites above, not because of any by-design
opacity.

**STOP per plan condition (i)/(iv)-adjacent**: per this unit's own rule ("If
it's another unfixed rendering site, name the site precisely and STOP — do
not expand the fix"), `renderer_zone_stream.cpp:559-576` and
`renderer_zone_load.cpp:2288-2334` are named here as the real live-client
root cause and were NOT touched. Fixing them is a new, separate unit's work
(they are shared "wind shader" infrastructure touching all GPU-path tree
rendering, and P18 has an open wrap-param investigation in nearby GLES2
texture/state-caching territory worth checking for overlap first).

**Corrected evidence reading** (verified by direct crop/pixel-diff comparison
of the committed stills, not just visual inspection):
- `gfay_03.png` / `gfay_03_AFTER.png` used the **identical** `/move` target
  sequence (`data/captures/p20_gfay.txt` vs `p20_after.txt`, both
  `/move 10 20 0` → `/move 10 60 0` → `/move 0 100 0`; `/move` is an
  absolute-target command, "Moving to (x,y,z)", confirmed in
  `src/client/eq_commands_movement.cpp:130-145`) — this pair IS a valid
  same-pose comparison. `compare -metric AE` + visual diff confirms the large
  center pine's canopy (dark rectangle) and an adjacent white-trunk tree's
  panel are **pixel-identical, zero visible change**, between before and
  after. Only background grass detail and sky/lighting differ (expected
  session-to-session noise), not the trees. **This is the reliable,
  apples-to-apples evidence, and it shows no improvement.**
- `gfay_tree_normal.png` / `gfay_tree_normal_AFTER.png` — previously cited as
  showing a small pine cluster's "solid rectangle became a leaf-shaped
  cutout" — is **NOT a valid same-pose comparison and that claim is
  retracted**. Both reach the same absolute XY target (`/move 40 120 0`), but
  `gfay_tree_normal.png` (`data/captures/p20_shaderdebug.txt`) arrives there
  in a single hop directly from the zone-in spawn point, while
  `gfay_tree_normal_AFTER.png` (`data/captures/p20_after.txt`) arrives at the
  same coordinate only after three prior hops. Because `/move` walks the
  character to the target (not an instant teleport with a fixed facing), the
  two captures almost certainly arrive facing different headings — confirmed
  by a full-frame pixel diff between the two stills showing very large,
  scene-wide differences (mushroom structure, bridge, and tree trunk all in
  different screen positions), consistent with a rotated camera, not a fixed
  camera with only the tree's material changed. The apparent "leaf-shaped
  fix" in that pair is most likely a different tree/viewing angle, not
  evidence of the code change working.

**Original root cause** (accurate description of the code bug fixed, scope
now understood to be software-renderer-only —
`AnimatedTreeManager::createAnimatedTree()`
(`src/client/graphics/animated_tree_manager.cpp`, was lines 375-388)
unconditionally force-switched **every** tree material to
`shaderMaterialAlphaTest_` — the H02d ETC1-companion-alpha shader variant
(reads alpha from `uAlphaTexture` on texture unit 1) — regardless of (a)
whether the WLD material was actually masked at all, and (b) whether the
tree's texture was ETC1-sourced or direct RGBA/DDS-decoded.
`buildAnimatedMesh()` never bound a companion texture on unit 1 for tree
quads, so on the software-renderer (`EDT_BURNINGSVIDEO`) path, unit 1 always
resolved to the GLES2 driver's documented opaque-white 1x1 fallback
(background, `COpenGLES2Driver` ~1090-1101) — which never discards — so
masked tree foliage would have rendered fully opaque there too, had this fix
not landed. This is real, and is the same bug class as the live client's
actual problem — just in a file that isn't reached by the live client's
actual renderer backend.

**Reproduction**: gfaydark (richest foliage zone), live session against
eqemu. `gfay_02.png` / `gfay_03.png` (path note below) show tree canopies
as solid, hard-edged, opaque rectangular quads blocking the sky — matches
Will's description exactly.

**Evidence path note**: the project's `.gitignore` excludes `*.png`
repo-wide, and no prior `benchmarks/audits/*` unit has committed PNG
stills either (e.g. `benchmarks/audits/p17_eviction_hygiene/` is text/log
only) — following that convention, all still screenshots below live on NFS
at `/mnt/projects/fable/videos/p20_live_visual/stills/` rather than in
`benchmarks/audits/p20_live_visual/` (which contains a committed
`EVIDENCE_MANIFEST.txt` describing every still). Filenames below are
relative to that NFS directory.

**Root cause of the committed code change**: see "Original root cause" in the
correction block above — summary: `AnimatedTreeManager::createAnimatedTree()`
unconditionally force-switched every tree material to the ETC1-companion-alpha
variant with no companion ever bound, which would opaque-render masked
foliage on the software-renderer path. This is exactly the "H02d-class site
the fix didn't cover" the plan called out as suspect (b) — but, as the
correction above establishes, the *live GLES2 client's* actual H02d-class gap
is in `renderer_zone_stream.cpp` / `renderer_zone_load.cpp`'s wind-shader
clobber, not here. (Confirmed the same 2-argument-only gap also exists in
`door_manager.h`/`entity_renderer.h` `setShaderMaterialTypes()` — flagged here
as a related-but-out-of-scope finding, not fixed, since Part A's territory is
tree foliage.)

**Fix** (`src/client/graphics/animated_tree_manager.{h,cpp}`,
`src/client/graphics/irrlicht_renderer.cpp`,
`src/client/graphics/renderer_debug.cpp`): added the H02d Option-A
`isMaskedMaterial` classification (WLD Fragment 0x30 `materialType ==
TransparentMasked`, ground truth) and `isETC1Texture()` branch to
`buildAnimatedMesh()` (mirroring `ZoneMeshBuilder::buildTexturedMesh`),
binding the companion alpha texture on unit 1 only for the ETC1 case and
selecting the RGBA main-texture-alpha variant otherwise. Added the missing
`shaderMaterialAlphaTestRGBA_` member + 3rd `setShaderMaterialTypes()`
parameter to `AnimatedTreeManager` (matching `ZoneMeshBuilder`'s existing
signature) and wired it at both call sites
(`irrlicht_renderer.cpp:initLoadingScreen`,
`renderer_debug.cpp:swapZoneMeshMaterials`). Removed the blanket
MaterialType override in `createAnimatedTree()`'s per-material loop (now
only sets the universal Lighting/BackfaceCulling/FogEnable properties) so
`buildAnimatedMesh()`'s per-texture classification is no longer stomped.

**Evidence** (`/mnt/projects/fable/videos/p20_live_visual/stills/`, see
evidence path note above; manifest committed at
`benchmarks/audits/p20_live_visual/EVIDENCE_MANIFEST.txt`) — **reread per the
correction above; this is now a negative result on the live client**:
- Before: `gfay_02.png`, `gfay_03.png` — opaque rectangular tree quads,
  including the large center pine and an adjacent white-trunk tree.
- After (verified same pose — identical `/move` target sequence —
  post-fix rebuild): `gfay_03_AFTER.png` — the center pine and the
  white-trunk tree's panel are **pixel-identical to the before frame**; no
  visible change. `gfay_tree_normal_AFTER.png` is NOT a valid same-pose
  comparison against its nominal "before" (`gfay_tree_normal.png`) — see
  correction above — and is not usable as evidence either way.
- Shader-debug stills (`gfay_tree_normal.png`,
  `gfay_tree_alpha_mode7.png`, `gfay_tree_rawtexalpha_mode11.png`,
  `gfay_tree_companionalpha_mode12.png`) were captured pre-fix and showed no
  visible difference across modes 0/7/11/12 — inconclusive, as originally
  noted, and now understood to be additionally inconclusive because
  `AnimatedTreeManager` (what `/shaderdebug` modes 7/11/12 were built to
  inspect for this fix) never executes on this renderer backend in the first
  place.
- The committed code fix is real for the software-renderer path (no visual
  evidence exists either way for that path — it wasn't captured; the software
  renderer isn't the shipped/live path per the plan's own path note above).
  It does not explain, and does not fix, any pixel in the live GLES2
  screenshots above.
- Aside (not chased, out of scope): a separate pre-existing magenta/black
  checkerboard missing-texture placeholder appears on some qeynos2
  building rooftops/awnings (visible in `approach_01.png`, `approach_02.png`,
  `popin_auto_02.png` through `popin_auto_05.png`) — a genuine but
  unrelated missing-texture bug, not investigated further here.

**Verification**: goldens untouched — `bash scripts/ci/visual_gate.sh` PASS
(qeynos2/gfaydark/befallen) both before writing the Review and after the
final revert-cleanup rebuild. Full 6-step bar below.

### Part B — geometry pop-in: REPORT-ONLY — progressive-build-by-design, not a bug

**Reproduction**: qeynos2, teleported ~211 units from the gates
(`-74,428,3` → `101,315,3.75`, a fresh/never-visited area) in one jump
(simulating a fast camera approach), then captured the SAME camera pose at
increasing time offsets with no further movement:
`popin_timed_auto_t0.png` (+0.2s), `_t1.png` (+1.2s), `_t3.png` (+3.2s),
`_t6.png` (+6.2s).

**Controlled experiment**: repeated identically with `--zone-load manual`
(all region meshes built synchronously at zone load, confirmed to exist as
a real CLI override — `application.cpp` `--zone-load` →
`config.zoneLoadMode` → `constrainedConfig.deferredAssetLoading`):
`popin_timed_manual_t0.png` .. `_t6.png`.

**Finding**: under the default `automatic` mode, the destination view goes
from partially-built (`_t0`, some geometry present, one flat
placeholder-colored region) to almost ENTIRELY flat sky/ground clear-color
panels with barely any real geometry (`_t1`, +1.2s — the worst point) to
fully, correctly textured (`_t3`, +3.2s onward, stays complete through
`_t6`). Under `--zone-load manual`, the same jump shows geometry present
immediately (`_t0`) and remains present in every subsequent frame except
one single fully-blank gray frame at `_t1` (UI still rendered on top,
consistent with a one-frame camera-transition artifact from the
pathfinding turn, not a multi-second geometry fill — there is no
progressive accumulation pattern in the manual sequence the way there is
in the automatic sequence).

**Verdict**: hypothesis (1), `deferredAssetLoading`-by-design — CONFIRMED
by the controlled experiment (disabling it removes the multi-second
gradual-fill pattern). This is NOT a culling/BSP/PVS bug; not fixed, per
the plan's own instruction for this verdict.

**Quantification**: geometry that is visibly incomplete at +0.2s/+1.2s is
fully resolved by +3.2s — i.e., missing-then-appearing world geometry
persists for roughly **1-3 seconds of wall clock** after arriving in a
previously-unbuilt area, under `automatic` mode on this llvmpipe/x86 dev
build. This is a WALL-CLOCK measurement (screenshot timestamps, not frame
counts), so it is NOT an artifact of the ~7fps capture-recording overhead
the source videos were shot at (that caveat applies to how SMOOTH the pop-
in looks on video, not to whether it exists or how long it lasts) — at real
gameplay frame rates (30-60fps) the same 1-3 second gap would still be
directly visible to a player, just rendered at a higher frame rate. Orange
Pi hardware (the production target) is slower than this x86 llvmpipe dev
box for texture/mesh upload, so the real-hardware window is likely
longer, not shorter.

**Mitigation options (proposed, NOT implemented — report-only per the
plan)**: (a) widen the background-thread pool's `WorkPriorityKey`
prioritization for regions directly ahead of the camera's current heading/
velocity vector, not just PVS depth; (b) pre-warm a larger halo of regions
around teleport/zone-in points before the loading screen releases control
back to the player, trading a longer load screen for less mid-play pop-in;
(c) expose `deferredAssetLoading` as a per-hardware-tier preset knob (e.g.
`manual` on higher-memory tiers, `automatic` only where the memory budget
truly requires it) rather than the current single `"automatic"` default
baked into the `orangepi` preset.

### Deviations

1. **RETRACTED, follow-up pass**: `config/constrained_presets.json`'s
   `orangepi_p20` preset (exact copy of `orangepi` with
   `mergeZoneDraws: false`) has been REMOVED from this branch. It was
   originally added and justified by citing the P08d-retake-era qeynos2
   `mergeZoneDraws` first-frame SIGSEGV (`ZoneMergeBuffers::build()` /
   `textureGroupId()`) — but that crash was root-caused and fixed by **P10b**
   (already on `fable` before this unit started), whose own validation matrix
   already showed qeynos2 crash-free with `mergeZoneDraws: true` (automatic
   mode) through a full session. The original executor here cited the
   stale, pre-P10b finding without re-observing it. Re-verified by direct
   observation in this follow-up pass: brought up eqemu on qeynos2, ran
   `build-gles2/bin/willeq --renderer gles2 --constrained orangepi` (STOCK
   preset, `mergeZoneDraws: true`) through a first-frames session
   (`data/captures/p20_orangepi_stock_test.txt`: screenshots at +0.2s/+1.2s/
   +3.2s/+6.2s after zone-in, then `/camp`) — clean exit, no crash, 4 valid
   non-garbled screenshots saved
   (`/mnt/projects/fable/videos/p20_live_visual/stills/orangepi_stock_qeynos2_t0..t6.png`).
   Confirms P10b's fix holds and `orangepi_p20` was unnecessary baggage in a
   production config file; the capture sessions that used it are already
   done, so removing it doesn't affect any evidence already collected under
   the (identical apart from `mergeZoneDraws`) `orangepi` name. The shipped
   `orangepi` preset itself was never touched by either the original
   executor or this follow-up.
2. **Local, UNCOMMITTED workarounds applied during investigation, then
   reverted before the final commit** (per EXECUTOR_PROTOCOL.md's
   instruction for parked live-client crashes): (a) widened
   `saveScreenshot()`'s safe-readback compile guard from `EQT_HAS_DRM` to
   `EQT_HAS_DRM || EQT_HAS_GLES2` (same fix the unmerged
   `task/anim-video-capture` branch already documented and applied) — the
   x86 GLES2 dev build compiles that guard out entirely, silently falling
   through to `driver_->createScreenShot()`, which segfaults inside Mesa
   llvmpipe after a nondeterministic number of calls; (b) degraded a
   `LOG_FATAL`-and-abort-the-whole-zone-load in
   `loadZoneStep8a_BaseModelPrep()` (`renderer_zone_load.cpp`) to a
   `LOG_WARN`-and-skip-this-one-entity, for the pre-existing, already-
   tracked (`refactor/deferred.md`, "gfaydark zone-in FATAL... race=330")
   missing-race-model crash that otherwise makes ANY live session in
   gfaydark impossible — same pattern as the already-landed P12
   item_models.json degrade. Neither workaround is in the final diff;
   `git diff --stat` at commit time contains only the Part A fix files
   plus the `orangepi_p20` preset addition.
3. Part A's shader-debug-mode stills (7/11/12) were captured but turned
   out inconclusive (no visible difference from mode 0) — kept as evidence
   of what was tried, but the actual root-cause path was code inspection,
   not the debug shader.

### Verification bar (final tree, after reverting the two uncommitted
workarounds)

| Step | Result |
|---|---|
| 1. `cmake --build build -j24` | PASS |
| 2. `ctest --test-dir build --output-on-failure` | 1233/1235 run passed; 2 failed — both documented known host exceptions (`AudioManagerTest.PreloadCommonSounds`, `RDPServerFrameTest.UpdateFrameMultipleTimes` SEGFAULT/intermittent) |
| 3. `bash scripts/build-gles2-x86.sh` | PASS |
| 4. Visual gate | `bash scripts/ci/visual_gate.sh` → PASS (qeynos2/gfaydark/befallen), goldens untouched, no re-bless |
| 5. `bash scripts/build-arm-noble.sh` | PASS |
| 6. File-size check vs. `fable` | No FAILs. 2 pre-existing WARNs on files this unit only added a few lines to (`irrlicht_renderer.cpp` 2816 lines, `renderer_zone_load.cpp` — reverted to unchanged, not part of final diff); new/touched files (`animated_tree_manager.{h,cpp}`, `renderer_debug.cpp`) all well under 1500 |

eqemu: server brought up on qeynos2 then gfaydark then qeynos2 again
(sanctioned `scripts/eqemu/up.sh`/`down.sh` only) across the session;
Testchar restored to the documented befallen coordinates
(zone_id=36, x=-77.9232, y=21.9086, z=3, heading=190) and server shut down
cleanly (`down.sh`) at the end.
