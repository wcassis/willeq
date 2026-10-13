# B494 + B491 (cards of issue #169 + #166): retire the unreached texture-atlas runtime

Work item: kanban `t_dc3c4c83` (B494 + B491 + B326, #169 #166 #121). Executed
2026-10-14 on hermes-ext (worktree `kb-b494-b491-zone-atlas-zero-callers`,
branch `task/kb-b494-b491-zone-atlas-zero-callers`), base `origin/fable`
d2f958c5 (after fast-forward merge; the HARD blockers B606/B295+B293/B1018+B1019
were all on `origin/fable`, verified by `git merge-base --is-ancestor 76569602`
-- the MR !164 merge commit -- against HEAD).

This is the LAST of the four B494 cards and the second B491 card. It deletes
28 zero-caller rows (B491: 9, B494: 19), their supporting members, two cascade
functions, and the three orphaned files (`zone_geometry_atlased.cpp`,
`texture_atlas.h`, `texture_atlas.cpp`).

## Row table

All 28 rows: DELETED (declaration + definition + doc comments). None had a
real caller: step 1's tree-wide `rg -n -w <name> src include tests tools
benchmarks docker scripts` (script: `/home/user/kb-evidence/
b494-b491-zone-atlas-zero-callers/grep_rows.sh`, per-name output in
`grep_rows_output.txt`) found, for every row, only the declaration, the
definition, comment/log-string mentions, and the ratchet's own EXEMPT line.

| owner::name | issue | decision | state |
|---|---|---|---|
| ZoneMeshBuilder::buildAtlasedMesh | B491 | DELETE-WITH-MEMBER | deleted (def in zone_geometry_atlased.cpp, file gone) |
| ZoneMeshBuilder::getFallbackBufferMap | B491 | DELETE-WITH-MEMBER | deleted |
| TextureAtlas::finalizePreload | B494 | DELETE-WITH-MEMBER | deleted (file gone) |
| TextureAtlas::getAtlasHeight | B494 | DELETE-WITH-MEMBER | deleted (file gone) |
| TextureAtlas::getAtlasWidth | B494 | DELETE-WITH-MEMBER | deleted (file gone) |
| TextureAtlas::getGPUMemoryUsage | B494 | DELETE-WITH-MEMBER | deleted (file gone) |
| TextureAtlas::getPageCount | B494 | DELETE | deleted (file gone) |
| TextureAtlas::getTileCount | B494 | DELETE | deleted (file gone) |
| TextureAtlas::preloadFromFile | B494 | DELETE-WITH-MEMBER | deleted (file gone) |
| TextureAtlas::uploadPreloadedPage | B494 | DELETE-WITH-MEMBER | deleted (file gone) |
| ZoneShaderManager::appendAtlasPageTextures | B494 | DELETE | deleted |
| ZoneShaderManager::atlasTileScale | B494 | DELETE-WITH-MEMBER | deleted |
| ZoneShaderManager::setAtlasPageTextures | B494 | DELETE | deleted |
| ZoneShaderManager::setAtlasTileScale | B494 | DELETE-WITH-MEMBER | deleted |
| (free)::getAnimationSourceFromConfig | B491 | DELETE-WITH-MEMBER | deleted |
| (free)::getAnimationSourceS3DFileByCode | B491 | DELETE | deleted |
| (free)::normalToColor | B491 | DELETE | deleted |
| ZoneMeshBuilder::buildCompositeMesh | B491 | DELETE | deleted |
| ZoneMeshBuilder::buildTexturedMeshFromUploaded | B491 | DELETE | deleted |
| ZoneMeshBuilder::getConstrainedTextureCache | B491 | DELETE | deleted |
| ZoneMeshBuilder::registerLazyTexture | B491 | DELETE | deleted |
| GraphicsArchiveIndex::getCachedArchive | B494 | DELETE | deleted |
| RDPServer::isAudioReady | B494 | DELETE | deleted |
| ZoneShaderManager::clearPointLights | B494 | DELETE | deleted |
| ZoneShaderManager::getMaterialTypeLWWindAlphaTestRGBA | B494 | DELETE | deleted (getter only; shader programs kept) |
| ZoneShaderManager::getMaterialTypeLWWindSolid | B494 | DELETE | deleted (getter only) |
| ZoneShaderManager::getMaterialTypeWindAlphaTestRGBA | B494 | DELETE | deleted (getter only) |
| ZoneShaderManager::getMaterialTypeWindSolid | B494 | DELETE | deleted (getter only) |

## Members deleted (with their writes)

- `ZoneMeshBuilder::fallbackBufferMap_` (+ the `FallbackBufferInfo` struct).
- `ZoneMeshBuilder::shaderMaterialAtlasSolid_` / `shaderMaterialAtlasAlpha_`
  (both lived in the `#ifdef EQT_HAS_TEXTURE_ATLAS` block, which is gone) +
  `setAtlasShaderMaterialTypes()` + its two callers
  (`renderer_zone_load_objects.cpp`, `renderer_zone_draw.cpp`).
- `ZoneShaderManager::atlasTileScale_` (+ `setAtlasTileScale`/`atlasTileScale`).
- `RaceModelInfo::animationSource` + its two
  `info.animationSource = colText(stmt, 8);` writes (`willeq_database.cpp`).
  The SELECT column list is UNCHANGED, so `animationSourceS3d` stays column 9.
- The whole `TextureAtlas` class: `texture_atlas.h` + `texture_atlas.cpp`
  (`git rm`), including `PreloadData::tileLookup`, `gpuMemoryUsage_`,
  `atlasWidth_`/`atlasHeight_`.
- `GraphicsArchiveIndex::cachedArchives_` (+ `#include pfs.h` + the
  `PfsArchive` fwd decl): its only write (`.clear()`) went with the getter.

## Cascade (became zero-caller with their files, deleted with them)

- `(free)::splitTriangleToUVCells` (`zone_geometry_atlased.cpp`) -- went with
  `git rm` of the file.
- `TextureAtlas::getPageTexture` (`texture_atlas.h`) -- went with `git rm` of
  the file.

## Files and build targets

- `git rm src/client/graphics/eq/zone_geometry_atlased.cpp` (697 lines;
  empty after buildAtlasedMesh + splitTriangleToUVCells).
- `git rm include/client/graphics/texture_atlas.h` + `src/client/graphics/
  texture_atlas.cpp` (314 lines; the class was never constructed).
- `CMakeLists.txt`: all `zone_geometry_atlased.cpp` and `texture_atlas.cpp`
  source entries removed (willeq, willeq_core, gles2_zone_benchmark,
  gles2_composite_audit, the test lists); the "needed by texture_atlas.cpp"
  comment now says "legacy per-region VBO upload path"; the EQT_HAS_TEXTURE_
  ATLAS comment (lines 978-995) is now stale (no `#ifdef` blocks remain in
  zone_geometry.h -- verified by grep; the define and its three gles2-tool
  PRIVATE copies are KEPT per the card: FOLLOW-UP).
- `tests/cmake/graphics.cmake` (2 entries) and `tests/cmake/zone_integration.
  cmake` (1 entry): `zone_geometry_atlased.cpp` removed.
- `#include "client/graphics/texture_atlas.h"` dropped from `irrlicht_renderer.
  h`, `renderer_types.h`, `renderer_event_receiver.h`, `renderer_draw_types.h`
  (all four only used the name in comments/enums; the build found no type
  dependency).

## Kept (live, per the card's Traps)

- `ZoneShaderManager::getActiveAtlasSolid()` / `getActiveAtlasAlpha()`:
  callers in `src/client/graphics/diagnostics/diagnostics_debug.cpp`.
- `ZoneShaderManager::getAtlasPageTexture()` + `atlasPageTextures_`: the
  card's explicit decision -- never written now (its setters went) but still
  READ by the atlas shader callback (`zone_shader_callbacks.h`); a header
  comment marks it FOLLOW-UP.
- The four wind shader PROGRAMS (only the material-type GETTERS went).
- `ZoneShaderManager::isWindAlphaTestRGBAAvailable` / `isWindSolidAvailable` /
  `isLightweightAvailable` / `getMaterialTypeLWWindAlphaTest` (available; the
  getters removed here are a different set).
- `ZoneMeshBuilder::constrainedCache_`: live readers in
  `loadTextureFromBMP()` and `buildTexturedMesh()` (`zone_geometry.cpp`);
  only the `getConstrainedTextureCache()` accessor went.
- `RDPServer::audioReady_`: live reader in `sendAudioSamples()`
  (`rdp_server.cpp:985`); only the `isAudioReady()` accessor went.
- `ZoneMeshBuilder::isAtlasAvailable()`: live callers (diagnostics); only
  `setAtlasShaderMaterialTypes()` (its companion) went.
- `buildCompositeMesh`'s LIVE twin: the log-line emitter is
  `mesh_building.cpp` (line 670, untouched -- not the deleted
  `zone_geometry.cpp` twin). `test_eqe1_composite_open.cpp:177/:197`
  grep that one.
- The B950/B1019 layout net (`tests/test_zone_geometry_atlas_layout.cpp` +
  `CMakeLists.txt:995` PUBLIC define): the guarded members are gone, both
  sides now agree; the net is vacuous but KEPT per the card (retirement is a
  FOLLOW-UP).

## Ratchet RED -> GREEN

RED (`check_zero_callers.py --repo . --check` immediately after the deletion
commit, EXEMPT tables untouched): FAIL "stale" naming exactly the 28 rows of
this card and no others (paste: `/home/user/kb-evidence/b494-b491-zone-atlas-
zero-callers/RED_zero_callers.txt`; full run: the step-2 commit `abcc0651`).

GREEN (after striking the 28 EXEMPT lines, commit `cb81b3c4`; re-verified at
the final head `ee9e3842`):

```
zero-caller census clean: 307 functions, all classified in EXEMPT (boarded 307).
tests-only census clean: 270 functions, all classified in EXEMPT_TESTS (boarded 53, observer 165, seam 52).
write-only census clean: 45 fields, all 45 classified in EXEMPT.
unused census clean: 89 fields, all 89 classified in EXEMPT_UNUSED.
```

(paste: `GREEN_zero_callers_final.txt` / `GREEN_write_only_final.txt` in the
evidence dir). The write-only census was 45/89 BEFORE the deletions
(`write_only_base.log`) and is still 45/89 after: no member this card made
write-only (B1018's multi-declarator split was already merged; it saw no new
row either). `check_write_only_fields.py --check` exit 0 at the final head.

## Issue closure state (measured at the final head)

`grep -c '"): "B494",' scripts/refactor/check_zero_callers.py` = 91
EXEMPT rows still name B494 (owned by the earlier three B494 cards, the B195
re-board, the Slash reclass, B1022's re-point and wave-8 B533);
`grep -c '"): "B491",'` = 37 (owned by the sibling `b491-asset-loader-zero-
callers` card). Both sibling cards are unmerged on `origin/fable`, so per the
card's Issue-handling section this MR is PARTIAL on both: `Part of #169` +
`Part of #166` (no relabel; the remaining-rows note is in the MR description).
B326 (#121) is NOT gated: its three builders (buildTexturedMeshFromUploaded,
buildCompositeMesh, buildAtlasedMesh) are all in this card, so `Closes #121`.

## module_map line counts (measured after the last edit)

```
$ wc -l src/client/graphics/zone_lifecycle/renderer_zone_draw.cpp
1266 src/client/graphics/zone_lifecycle/renderer_zone_draw.cpp
$ wc -l src/client/graphics/zone_load/renderer_zone_load_objects.cpp
686 src/client/graphics/zone_load/renderer_zone_load_objects.cpp
```

Both module_map rows updated (they were 1,141 and 566 -- stale from earlier
merges, so they moved UP). The other TUs this card shrank
(`zone_geometry.cpp` now 688, `race_codes.cpp`, `graphics_archive_index.cpp`,
`rdp_server.h` inline, `willeq_database.cpp`) have no count row in the map.
The deleted files were never named in the map.

## Not verified / left open

- Not verified on this host: the full CI suite and the visual gate (CI's job
  on push; this host is capped at 4 GiB). Locally verified: `willeq` built
  green (`build_willeq.log`), all eight named test binaries rebuilt and run
  green (`test_runs.log`), `ctest -L hygiene` 35/35 (`ctest_hygiene3.log`).
- Left open, see FOLLOW-UP.
