# F18 — Split zone draw, lazy load, and progressive/PVS clusters out of irrlicht_renderer.cpp

Move the zone rendering and streaming clusters out of
`src/client/graphics/irrlicht_renderer.cpp` (10,790 lines). Pure move — no
behavior change, no interface change, no reformatting.

## Scope (orig refs @ a0c02cd; heavily shifted. Enumerate by NAME.)

Move to new `src/client/graphics/renderer_zone_draw.cpp`:
1. **Zone mesh / region / portal rendering** (orig 5822–6624): `createZoneMesh`,
   `drawZoneGeometrySorted`, `drawRegionMesh`, the `drawPortal*` family,
   `rebuildRegionMesh`, `buildRegionNeighborMap`, and contiguous region-draw
   helpers.
2. **Lazy/background mesh load pipeline** (orig 6625–7329):
   `processFrameLazyLoad`, `drainMeshResults`, `finalizeOneMeshResult`,
   material-swap helpers, `submitOneBgMeshBuild`, `indexObjectMeshes`,
   `buildDeferredObject`.
3. **Progressive load / PVS** (orig 12992–13446; F19's review lists these as
   left behind): `processFrameProgressiveLoad`, `getRegionPvsDepth`,
   `rebuildPvsNeighborhood`, `isInPvsNeighborhood`, `onPvsRegionChanged`,
   `queueEntityPrepRequests`, `logAssetBuildTime`, `sendLoadProgress`,
   `checkProgressiveLoadingComplete`.

In-range non-zone-draw methods (judgment): leave + document. If the single
file would exceed ~2,400 lines, split into renderer_zone_draw.cpp (clusters
1) and renderer_zone_stream.cpp (clusters 2+3) and document.

## Steps

1. Locate clusters by grep; produce the mapping in the review BEFORE moving.
2. Replicate namespace wrapper + include patterns (F14–F21 reviews).
3. Move verbatim; preserve #ifdef guards exactly.
4. File-local statics: usual rule.
5. Add file(s) to `GRAPHICS_SOURCES` in CMakeLists.txt.

## Verification

- `cmake --build build -j24` clean.
- `ctest --test-dir build` — green modulo documented exceptions in
  refactor/test_baseline.txt.
- `python3 scripts/refactor/verify_move_only.py BASE..HEAD` — PASS or
  documented deviations only.
- `bash scripts/refactor/check_file_sizes.sh --files <abs paths>` — WARN
  accepted, must not FAIL.
- irrlicht_renderer.cpp shrinks by ~1,900+ lines.

## Review

**Cluster boundaries (verified by grep of `IrrlichtRenderer::` definitions,
irrlicht_renderer.cpp @ branch-point commit `443fea3` (merge-base with
`fable`), 10,790 lines before this move):**

- **Cluster 1 — Zone mesh/region/portal rendering** (pre-move lines
  5401–6202, contiguous, no gap before or after). 11 methods in file order,
  moved into `renderer_zone_draw.cpp`:
  - `createZoneMesh()`
  - `RenderPassTimer::OnRenderPassPreRender(...)` — not named in the plan's
    prose, but it's the ILightManager render-pass hook that calls
    `drawZoneGeometryWithPortals()`/`drawZoneGeometrySorted()`; contiguous
    with the cluster and clearly "region-draw helper" per the plan's
    catch-all phrase.
  - `uploadMeshHardwareBuffers(irr::scene::IMeshSceneNode*)`,
    `deleteMeshHardwareBuffers(irr::scene::IMeshSceneNode*)` — GLES2 VBO
    helpers used by `rebuildRegionMesh` (this cluster) and by
    `finalizeOneMeshResult`/`processFrameLazyLoad` (cluster 2, moved to the
    sibling file — called across the two new TUs same as any other member
    function, no linkage issue).
  - `drawZoneGeometrySorted()`, `drawRegionMesh(size_t)`,
    `drawPortalQuad(const Portal&)`, `buildRegionNeighborMap()`,
    `drawZoneGeometryWithPortals()`, `drawPortalRecursive(...)` — the named
    portal-draw family plus their two contiguous helpers
    (`drawZoneGeometryWithPortals` wasn't named explicitly in the plan but
    sits directly between `buildRegionNeighborMap` and `drawPortalRecursive`,
    inseparable from the family it dispatches to).
  - `rebuildRegionMesh(size_t)` (the named end of the range).

- **Cluster 2 — Lazy/background mesh load pipeline** (pre-move lines
  6204–6907, contiguous, immediately after cluster 1). 8 methods, moved into
  `renderer_zone_stream.cpp`:
  - `processFrameLazyLoad()`, `drainMeshResults()`, `finalizeOneMeshResult()`
  - `updateRegionMaterials(...)`, `processOneMaterialSwap()` — the "material-
    swap helpers" the plan calls out by description rather than name.
  - `submitOneBgMeshBuild()`, `indexObjectMeshes()`, `buildDeferredObject(size_t)`

- **Cluster 3 — Progressive load / PVS** (pre-move lines 10147–10786,
  contiguous, running to end of file before the closing
  `namespace Graphics`/`namespace EQT` braces). 8 methods, matching the
  plan's list and F19's review exactly, moved into `renderer_zone_stream.cpp`
  (appended after cluster 2):
  - `processFrameProgressiveLoad()`, `getRegionPvsDepth(size_t) const`,
    `rebuildPvsNeighborhood(size_t)`, `isInPvsNeighborhood(size_t) const`,
    `onPvsRegionChanged()`, `queueEntityPrepRequests()`,
    `logAssetBuildTime(...)`, `sendLoadProgress(const std::string&)`,
    `checkProgressiveLoadingComplete()`.

  **Explicitly left in `irrlicht_renderer.cpp`, immediately before cluster
  3** (per F19's review, confirmed unchanged): `setFrameTimingEnabled`,
  `setClipDistance`, `getClipDistance` — diagnostics/renderer-mode
  setter/getters, not zone-draw/streaming. **Immediately after cluster 1+2**
  (before this unit's move, now immediately after cluster 1 in the trimmed
  file): `setExpectedEntityCount`, `notifyEntityLoaded`, `setNetworkReady`,
  `setWeather` — entity-count/network/weather state setters, not zone-draw.

**Partition rationale (two-file split):** clusters 1+2+3 combined are 2,146
raw content lines — under the plan's ~2,400-line single-file threshold, so a
single-file layout wasn't mandatory. Split anyway into
`renderer_zone_draw.cpp` (cluster 1 only, 835 lines) and
`renderer_zone_stream.cpp` (clusters 2+3, 1,363 lines) because: (a) it
matches the plan's own suggested file names and cluster grouping exactly;
(b) a single combined file would land at ~2,170 lines (WARN on the 1,500-line
ceiling), while the two-file split keeps both files WARN-free-or-minimal —
`renderer_zone_draw.cpp` at 835 is comfortably under 1,500, and
`renderer_zone_stream.cpp` at 1,363 stays under it too (see file-size check
below); (c) "zone mesh/portal drawing" (per-frame render path) and
"background load streaming" (lazy load + progressive/PVS, both
asset-population state machines) are naturally distinct concerns, matching
how F14–F21 split diagnostics/RDP/debug/overlays/effects/player-physics by
topic rather than raw line count.

**Guard handling:** `drawZoneGeometryWithPortals()` and
`drawPortalRecursive(...)` (cluster 1) preserve their `#ifdef EQT_HAS_GLES2`
/ `#else` / `#endif` bodies exactly, unmodified, in `renderer_zone_draw.cpp`.
`uploadMeshHardwareBuffers`/`deleteMeshHardwareBuffers` (cluster 1) preserve
their `#ifdef EQT_HAS_GLES2` / `#endif` bodies. `rebuildRegionMesh` (cluster
1) preserves its nested `#ifdef EQT_HAS_GLES2` / `#else` / `#endif` VBO-
upload block. `finalizeOneMeshResult` (cluster 2) preserves the same nested
GLES2 VBO-upload guard. `onPvsRegionChanged` (cluster 3) preserves its
`#ifdef EQT_HAS_GLES2` / `#endif` GPU-upload-reprioritize block.
`processFrameProgressiveLoad`/`checkProgressiveLoadingComplete` (cluster 3)
preserve their internal `#ifdef EQT_HAS_GLES2` / `#endif` pendingVBOUploads_
blocks. No guard was split across the file boundary — every `#ifdef`/`#endif`
pair in the moved code opens and closes within the same new file.

**Namespace + include handling (same pattern as F14–F19/F21):**
- Both new files wrap their bodies in `namespace EQT { namespace Graphics {
  ... } }`, matching the original TU.
- `renderer_zone_draw.cpp` needed:
  - `client/graphics/constrained_texture_cache.h`,
    `client/graphics/constrained_mesh_cache.h` — `ConstrainedTextureCache`
    and `ConstrainedMeshCache` are only forward-declared in
    `irrlicht_renderer.h` (`class ConstrainedTextureCache;` /
    `class ConstrainedMeshCache;`); `createZoneMesh`/`rebuildRegionMesh` call
    their methods directly, same gap F19 hit for `ConstrainedMeshCache`.
  - The `#ifdef EQT_HAS_DRM` / GL header block (`GLES2/gl2.h`, `EGL/eglext.h`
    or `GL/gl.h`, `EGL/egl.h`) and the `extern` GLES2 static-HW-buffer bridge
    declarations (`gles2CreateStaticHWBuffer`, `gles2DeleteStaticHWBuffer`)
    — copied verbatim from the original TU's preamble since
    `drawZoneGeometryWithPortals`/`drawPortalRecursive` call raw
    `glEnable`/`glStencilFunc`/`glColorMask`/`glDepthMask`/`GL_STENCIL_TEST`
    under `#ifdef EQT_HAS_GLES2`, and `uploadMeshHardwareBuffers`/
    `deleteMeshHardwareBuffers` call the two `gles2*StaticHWBuffer` bridge
    functions.
  - `common/logging.h`, `<algorithm>`, `<chrono>`, `<cstring>`, `<limits>` —
    `LOG_*` macros, `std::sort`, `std::chrono::steady_clock`, `std::memcpy`,
    no direct `<limits>` use was found on a second pass but kept for
    belt-and-suspenders consistency with the numeric casts in
    `rebuildRegionMesh`'s VBO block (matches F19's documented style).
  - `ZoneMeshBuilder` (used by `createZoneMesh`/`rebuildRegionMesh`),
    `Portal`/`PortalSystem`, `WorkPriorityKey`/`AssetType`/`UploadRequest`,
    `AnimatedTextureManager`, `ZoneShaderManager` needed no extra include:
    all are fully defined via `irrlicht_renderer.h`'s own transitive
    includes (`eq/zone_geometry.h`, `portal_system.h`, `work_priority.h`,
    `gpu_upload_thread.h`, `animated_texture_manager.h`, `zone_shader.h`).
- `renderer_zone_stream.cpp` needed:
  - `client/graphics/constrained_texture_cache.h`,
    `client/graphics/constrained_mesh_cache.h` — same forward-declaration
    gap as above; used extensively by `processFrameLazyLoad`,
    `finalizeOneMeshResult`, `submitOneBgMeshBuild`,
    `processFrameProgressiveLoad`, `checkProgressiveLoadingComplete`.
  - `client/graphics/entity_prep_worker.h` — `EntityPrepWorker` is only
    forward-declared in `irrlicht_renderer.h`
    (`std::unique_ptr<class EntityPrepWorker>`); `processFrameProgressiveLoad`
    calls `entityPrepWorker_->isIdle()`/`dispatchOne()` and
    `queueEntityPrepRequests` calls `isPendingForEntity`/`requestPrep`/
    constructs `EntityPrepWorker::PrepRequest`, and `onPvsRegionChanged`
    calls `updateDepths(...)`.
  - `common/logging.h`, `<fmt/format.h>` — `LOG_*` macros and
    `fmt::format(...)` (used by `sendLoadProgress` callers throughout both
    clusters).
  - `<algorithm>`, `<chrono>`, `<cmath>`, `<cstring>`, `<limits>` —
    `std::sort`/`std::remove_if`/`std::max`/`std::min`,
    `std::chrono::steady_clock`, `std::sqrt` (light-position distance calc in
    `buildDeferredObject`), `std::memcpy`, `std::numeric_limits<float>` (zone
    bounds calc in `checkProgressiveLoadingComplete`).
  - No `EQT_HAS_GLES2` raw-GL calls or `gles2*` bridge externs needed in this
    file — its GLES2-guarded sections only call `gpuUploadThread_->submit`/
    `reprioritize` and check `pendingVBOUploads_`, all through types already
    fully included via `irrlicht_renderer.h`.

**Shared-helper analysis:**
- `uploadMeshHardwareBuffers`/`deleteMeshHardwareBuffers` (member functions,
  defined in `renderer_zone_draw.cpp`) are called from `rebuildRegionMesh`
  (same file) and from `finalizeOneMeshResult`/`processFrameLazyLoad`
  (`renderer_zone_stream.cpp`) — ordinary cross-TU member-function calls via
  the class declaration in `irrlicht_renderer.h`, no linkage concern.
- File-local `static` variables inside the moved functions
  (`adaptiveLightLogOnce` in `drawZoneGeometrySorted`; `lazyLoadSkipLog`/
  `lazyLoadGovLog`/`lazyLoadQueueLog` in `processFrameLazyLoad`;
  `progGovLog`/`progCheckLog` in `processFrameProgressiveLoad`/
  `checkProgressiveLoadingComplete`) are function-local statics, not
  file-scope — moved with their enclosing function bodies verbatim, no
  de-static-ing needed.
- Two now-dead `extern` declarations in `irrlicht_renderer.cpp`
  (`gles2CreateStaticHWBuffer`, `gles2DeleteStaticHWBuffer`) were removed
  from the original TU's preamble: their only two call sites
  (`uploadMeshHardwareBuffers`/`deleteMeshHardwareBuffers`) moved to
  `renderer_zone_draw.cpp` in their entirety, so the declarations became
  unused in the old file. The other 6 `gles2*` extern declarations in the
  same block (`gles2DeleteAllStaticHWBuffers`,
  `gles2GetHWBufferMemoryUsage`, `gles2GetHWBufferCount`,
  `gles2GetGpuTextureMemoryUsage`, `gles2Draw3DLinesBatch`,
  `gles2WrapTexture`, `gles2RegisterExternalHWBuffer`) still have call sites
  elsewhere in `irrlicht_renderer.cpp` and were left untouched. See
  "Deviations" below — this is the only edit to `irrlicht_renderer.cpp`
  beyond deleting the three moved ranges.

**Line counts:**
- `irrlicht_renderer.cpp`: 10,790 → 8,642 lines (**-2,148**: 2,146 content
  lines from the three moved ranges + 2 lines removed for the now-dead
  `gles2*StaticHWBuffer` extern declarations)
- `renderer_zone_draw.cpp`: 835 lines (new; cluster 1 content + includes/
  namespace-wrapper boilerplate + the GL/GLES2 preamble copied from the
  original TU)
- `renderer_zone_stream.cpp`: 1,363 lines (new; clusters 2+3 content +
  includes/namespace-wrapper boilerplate)
- Net change: +2,198 / -2,148 = +50 lines total (boilerplate: namespace open/
  close pairs and include lines duplicated across two new TUs, offset by the
  2 dead-extern lines removed)

**Build/test verification:**
- `cmake --build build -j24`: clean on the first attempt (no missing-include
  fixes needed — the forward-declaration gaps for `ConstrainedMeshCache`/
  `ConstrainedTextureCache`/`EntityPrepWorker` were anticipated up front from
  reading F19's review before writing the new files, and the GL/GLES2
  preamble was copied verbatim from the original TU rather than reconstructed
  from scratch).
- `ctest --test-dir build`: 1191/1192 run tests passed (9 pre-existing
  asset/graphics/audio tests skipped, unrelated to this unit).
  - `AudioManagerTest.PreloadCommonSounds`: failed with the documented ALSA
    "cannot find card '0'" environment gap
    (`refactor/test_baseline.txt`), consistent with F02/F14/F15/F19 findings
    on this host. Ran twice (before and after removing the two dead
    `gles2*` externs) with identical results both times.
- `python3 scripts/refactor/verify_move_only.py 443fea3..HEAD --repo
  <slot1>`: **PASS** ("Boilerplate: +41 / -0 lines (allowed)"). First run
  flagged the two `gles2CreateStaticHWBuffer`/`gles2DeleteStaticHWBuffer`
  extern re-declarations in `renderer_zone_draw.cpp` as substantive added
  lines (they're needed there but weren't removed from the old file yet at
  that point); removing the now-dead duplicates from
  `irrlicht_renderer.cpp` turned the diff into a genuine move and the
  verifier passed clean on the second run. The verifier also listed
  `CMakeLists.txt` as a changed non-C++ file — this unit's own edit (adding
  the two new sources to `GRAPHICS_SOURCES`, additive/mechanical, 2 lines).
  `git merge-base fable HEAD` resolves to `443fea3`, confirming no rebase
  occurred and `fable` did not advance during this unit's work.
- `bash scripts/refactor/check_file_sizes.sh --files
  <slot1>/renderer_zone_draw.cpp <slot1>/renderer_zone_stream.cpp`: both
  **ok** (835 and 1,363 lines, both under the 1,500-line ceiling — no WARN).

**Deviations from plan:** one substantive deviation, both required and
disclosed above: removed 2 now-dead `extern` GLES2 bridge-function
declarations from `irrlicht_renderer.cpp` (their sole call sites moved out in
their entirety). This is not a behavior change — the declarations were
unused after the move — but it is an edit to the old file beyond a pure
deletion of the moved ranges, so it's called out explicitly per the
verifier's own flag. Two clarifications beyond the plan's named-method list
(both "contiguous region-draw helpers"/inseparable-helper judgment calls,
not scope creep): `RenderPassTimer::OnRenderPassPreRender` and
`drawZoneGeometryWithPortals` included in cluster 1. Two clarifications for
cluster 2 (named by description, not by name, in the plan): `updateRegionMaterials`
and `processOneMaterialSwap` as the "material-swap helpers". No methods were
left in-range beyond the boundary cases already documented by F19's review
(`setFrameTimingEnabled`/`setClipDistance`/`getClipDistance` before cluster
3; `setExpectedEntityCount`/`notifyEntityLoaded`/`setNetworkReady`/
`setWeather` after cluster 1+2) — both confirmed unchanged in the current
file during this unit's own grep pass.
