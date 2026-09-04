# L08 — `ZoneStreamComponent` (family: `renderer_zone_stream.cpp` + `renderer_zone_merge.cpp` — progressive load, PVS neighbourhood, deferred objects, texture guards)

Batch: `refactor/L_renderer_component_extraction.md` (corrections 1–28).
**Runs ALONE after L07 merges** (L07 retargets `applySimulationOutput`
call sites inside these two TUs). Measured at `8f77ef89`: 1,440 lines, 15
bodies, 2 single-family members (survey; re-measure — and collect inline
accessors, K04 f).

Callers (word grep; the compiler confirms; forwarders only for a NAMED
external caller — expect none): `renderer_frame.cpp` (`buildDeferredObject`,
`processFrameProgressiveLoad`, `queueEntityPrepRequests`),
`renderer_sim_bridge.cpp` → after L07, `sim/…` (`checkProgressiveLoadingComplete`,
`getRegionPvsDepth`, `isInPvsNeighborhood`, `onPvsRegionChanged`,
`rebuildPvsNeighborhood`, `queueEntityPrepRequests` — write
`owner_.ZoneStream().X()` there), `renderer_zone_draw.cpp`,
`renderer_zone_load*.cpp` (`indexObjectMeshes`, `logAssetBuildTime`),
`renderer_init.cpp` (`ensureBoidsTextureGuard`, `ensureDoorTextureGuard`),
`overlays/overlays_hud_env.cpp` (`ensureBoidsTextureGuard` → `owner_.ZoneStream()`),
`renderer_entity_load_gate.cpp`, `irrlicht_renderer.cpp`
(`unregisterRemainingT124Guards`), `simulation_worker.cpp` and
`constrained_renderer_config.h` / `eqe1…` (`queueEntityPrepRequests`,
`processFrameProgressiveLoad` — read: comments, callback types, or real
calls from the worker thread? If the WORKER calls a renderer method, that
is a thread boundary — STOP and report the shape). `isEntityBlockingProgressiveLoad`,
`sendLoadProgress`: in-family or dead (§5 + K12 ff).

⚠ `renderer_zone_merge.h` is a real header included by three T124/T126
texture-guard tests (`test_object_node_texture_guard.cpp`,
`test_t126_equip_texture_guard.cpp`, `test_t124_texture_holder_guards.cpp`)
— read it: it may declare free functions (`swapTextureInMeshSceneNode`) that
are NOT `IrrlichtRenderer` members. Keep the header at its path (or move
it with the TU and repoint the three includes — call-site edits only,
assertions untouched); record which.

Predicted header delta: −15 declarations −2 members −banners +(scaffolding
4..6) ≈ **−12 or better**.

Territory: `include/client/graphics/zone_stream/zone_stream_component.h`
(new), `src/client/graphics/zone_stream/` (git mv of the two TUs and, if
moved, `renderer_zone_merge.h`), `irrlicht_renderer.h`, `renderer_init.cpp`
(ctor line + 2 call sites), root `CMakeLists.txt`, the new test + cmake,
the three guard tests (include path only), allowlist, `docs/module_map.md`,
CALL-SITE-ONLY edits in the renderer TUs and components named. Visual gate
+ ARM NOT skippable.
