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

(executor fills in)
