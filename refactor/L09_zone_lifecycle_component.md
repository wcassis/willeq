# L09 — `ZoneLifecycleComponent` (family: `renderer_zone_lifecycle.cpp` + `renderer_zone_draw.cpp` + `renderer_gpu_upload.cpp` — unload/ready/environment, portal draw, GPU upload thread)

Batch: `refactor/L_renderer_component_extraction.md` (corrections 1–28;
26 is why `zone_draw` and `gpu_upload`, which own no state, ride with
`zone_lifecycle`). **Runs ALONE after L08 merges.** Measured at `8f77ef89`:
2,340 lines, 28 bodies, ~4 single-family members (survey; re-measure).

Callers with a REAL external receiver (forwarders, each named):
`irrlicht_bridge.cpp` (`setExpectedEntityCount`, `setNetworkReady`,
`setWeather`, `unloadZone`), `application_zone_loading.cpp`
(`setupInstantScene`, `storeZoneEnvironment`, `unloadZone`),
`bridge_component_ui_audio.cpp` / `zone_audio_manager.cpp` / `weather_audio.cpp`
(`unloadZone`, `setWeather` — read: real calls on an `IrrlichtRenderer`
receiver, or same-named methods of other classes? `weather_system.cpp`,
`tree_wind_controller.cpp`, `weather_effects_controller.cpp` almost
certainly have their OWN `setWeather` — K10 t). `notifyEntityLoaded`,
`setExpectedEntityCount`, `setNetworkReady` are also named in
`output_renderer.h` / `console_renderer.h` / `null_renderer.h` — same-named
methods on other classes, NOT an interface (correction 13).

In-renderer callers retarget: `renderer_frame.cpp` (`drainGPUResults`,
`processCompletedUploads`, `processOneGPUResult`), `renderer_frame_input.cpp`
(`buildZonePlaceholder`, `setupHCMapCollision`), `renderer_zone_load*.cpp`
(`buildRegionNeighborMap`, `createZoneMesh`, `rebuildRegionMesh`,
`uploadMeshHardwareBuffers`, `drawZoneGeometrySorted`), `zone_stream/…`
(`destroyZonePlaceholder`, `drawZoneGeometrySorted` → `owner_.ZoneLifecycle()`),
`sim/…` (`drawZoneGeometrySorted`), `sim_visibility.cpp`
(`drawZoneGeometryWithPortals` — the simulation worker's file: if the WORKER
THREAD calls it, STOP and report), `player/…` (`rebuildRegionMesh` →
`owner_.ZoneLifecycle()`; `player_camera_component.h` names it — a comment?),
`diagnostics/…` (`drawZoneGeometrySorted`), `slash/…` (`toggleGPUUploadThread`),
`renderer_init.cpp` (`initGPUUploadThread`, `setWeather`),
`renderer_entity_load_gate.cpp` (`unloadZone`), `texture_atlas.h`
(`processCompletedUploads` — read). `applyStoredZoneEnvironment`,
`checkAndSetZoneReady`, `deleteMeshHardwareBuffers`, `drawPortalQuad`,
`drawPortalRecursive`, `drawRegionMesh`, `setZoneEnvironment`: in-family or
dead (§5 + K12 ff; `unloadZone` and `setNetworkReady` may publish — check).

⚠ `gpuUploadThread_` is under `#ifdef EQT_HAS_GLES2` and is core (in the
context, guarded). The GPU upload thread is ANOTHER THREAD: no
ownership change (STOP). `tools/hwcaps/zone_bench_*.cpp` and
`gpu_sync_bench_hooks.h` name `renderer_zone_draw.cpp` in comments — verify;
`Q02` applied a fix in both places — do not diverge them.

Predicted header delta: −28 declarations −4 members −banners +(forwarders
≈ 6) +(scaffolding 4..6) ≈ **−20 or better**.

Territory: `include/client/graphics/zone_lifecycle/zone_lifecycle_component.h`
(new), `src/client/graphics/zone_lifecycle/` (git mv of the three TUs),
`irrlicht_renderer.h`, `renderer_init.cpp`, root `CMakeLists.txt`, the new
test + cmake, allowlist, `docs/module_map.md`, CALL-SITE-ONLY edits in the
renderer TUs, components, bridge and application files named. Visual gate
+ ARM NOT skippable (portal draw IS the zone render path).
