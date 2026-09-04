# L10 — `ZoneLoadComponent` (family: `renderer_zone_load.cpp` + `_entities` + `_env` + `_objects` — the loading thread's build of a zone)

Batch: `refactor/L_renderer_component_extraction.md` (corrections 1–32).
**Runs ALONE after L09 merges.** Survey at `cfadb78e`: 3,268 lines, 21
bodies, 3 single-family members; re-measure at your tip (L02/L08/L09 have
since retargeted call sites inside these TUs).

⚠ **The loading thread.** `Application` starts a zone-loading thread that
owns the GL context and calls into this family (`CLAUDE.md` §"Zone
loading": `ZoneLoadSnapshot` → loading thread → GPU resources →
completion signal). Read `application_zone_loading.cpp` and
`renderer_zone_load.cpp`'s entry (`loadZoneSequential`?) BEFORE moving:
which thread calls which body, and what the completion handshake touches.
No thread-ownership change (STOP); the entry point(s) `Application` calls
keep forwarders (named callers).

The zone blob (`currentZone_`, `regionMeshNodes_`, `objectNodes_`,
`objectInSceneGraph_`, `portalSystem_`, `currentPvsRegion_`) is on the
context — nothing here takes it. `zoneShader_`, `constrainedTextureCache_`,
`doorManager_` likewise. Siblings reached: `owner_.Eqe1()` (archive
plumbing, 6 methods), `owner_.ZoneStream()` (`indexObjectMeshes`,
`logAssetBuildTime`), `owner_.ZoneLifecycle()` (`buildRegionNeighborMap`,
`createZoneMesh`, `rebuildRegionMesh`, `uploadMeshHardwareBuffers`),
`owner_.PlayerCamera()` (`extractWorldTriangles`, `rayIntersectTriangles`,
`rayTestDoorsAndObjects`, `setupMinimalZoneCollision`), `owner_.Overlays()`
(`applyEnvironmentalDisplaySettings`, `setupFog`, `drawLoadingScreen`).

Text readers: `tests/cmake/*.cmake` may name these TUs for
`gles2_zone_benchmark` (J05 found nineteen add-sites for the WLD loader —
grep); `tools/hwcaps/zone_bench_loader.cpp` mirrors production zone-load
code and names it in comments (Q02) — do not diverge; `docs/`,
`refactor/BOARD.md`, every `CLAUDE.md`.

Predicted header delta: −21 declarations −3 members −banners
+(forwarders for the loading-thread entry points, ≈ 2) +(scaffolding
4..6) ≈ **−15 or better**.

Territory: `include/client/graphics/zone_load/zone_load_component.h`
(new), `src/client/graphics/zone_load/` (git mv of the four TUs),
`irrlicht_renderer.h`, `renderer_init.cpp`, root `CMakeLists.txt` (four
lines + benchmark add-sites), the new test + cmake, allowlist,
`docs/module_map.md`, CALL-SITE-ONLY edits in `application_zone_loading.cpp`,
`irrlicht_bridge.cpp`, the renderer TUs and components named. Visual gate
+ ARM NOT skippable (this IS the zone load path the gate exercises).

STOP: a thread-ownership change; the visual gate diffs any golden.
