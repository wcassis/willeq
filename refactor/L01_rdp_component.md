# L01 — Pilot: `RdpComponent` + `RendererContext`

Batch: `refactor/L_renderer_component_extraction.md`. Mechanism: Batch K's
§Mechanism (REVISED, every correction K01–K15) applied to `class
IrrlichtRenderer`. **Runs ALONE.** Measured at `6f696b25` (2026-09-03).

## The two deliverables

1. **`include/client/graphics/renderer_context.h` — `struct eqt::graphics::RendererContext`**
   with a reference member per core member of `IrrlichtRenderer`, named
   without the trailing underscore (`driver_` → `driver`), type copied
   VERBATIM from the header line named. The core is the **52 members** below
   (breadth ≥ 5 of 17 families, re-measured at this tip):

   | member | type | line |
   |---|---|---|
   | `config_` | `RendererConfig` | 1321 |
   | `entityRenderer_` | `std::unique_ptr<EntityRenderer>` | 862 |
   | `driver_` | `irr::video::IVideoDriver*` | 848 |
   | `constrainedTextureCache_` | `ConstrainedTextureCache*` | 902 |
   | `smgr_` | `irr::scene::ISceneManager*` | 849 |
   | `zoneShader_` | `std::unique_ptr<ZoneShaderManager>` | 913 |
   | `regionMeshNodes_` | `std::map<size_t, irr::scene::IMeshSceneNode*>` | 1026 |
   | `doorManager_` | `std::unique_ptr<DoorManager>` | 898 |
   | `zoneBspTree_` | `std::shared_ptr<EQT::Graphics::BspTree>` | 1028 |
   | `objectNodes_` | `std::vector<irr::scene::IMeshSceneNode*>` | 1257 |
   | `currentPvsRegion_` | `size_t` | 1029 |
   | `camera_` | `irr::scene::ICameraSceneNode*` | 851 |
   | `weatherEffects_` | `std::unique_ptr<WeatherEffectsController>` | 912 |
   | `device_` | `irr::IrrlichtDevice*` | 847 |
   | `currentZone_` | `std::shared_ptr<S3DZone>` | 1019 |
   | `skyRenderer_` | `std::unique_ptr<SkyRenderer>` | 901 |
   | `playerX_` (and its `playerY_`/`playerZ_` siblings — measure; take the trio if the siblings are ≥ 5 too) | `float` | 1402 |
   | `particleManager_` | `std::unique_ptr<Environment::ParticleManager>` | 911 |
   | `detailManager_` | `std::unique_ptr<Detail::DetailManager>` | 903 |
   | `currentZoneName_` | `std::string` | 1020 |
   | `boidsManager_` | `std::unique_ptr<Environment::BoidsManager>` | 1015 |
   | `regionBoundingBoxes_` | `std::map<size_t, irr::core::aabbox3df>` | 1027 |
   | `manualZoneDrawEnabled_`, `progressiveLoadingActive_`, `portalOcclusionEnabled_`, `fogEnabled_`, `usePvsCulling_`, `entityPrepReady_`, `wireframeMode_`, `portalOcclusionEligible_`, `lightingEnabled_`, `fireEffectsEnabled_` | `bool` | 1033, 1236, 1122, 1347, 1025, 864, 1343, 1123, 1348, 1356 |
   | `eventReceiver_` | `std::unique_ptr<RendererEventReceiver>` | 899 |
   | `cameraController_` | `std::unique_ptr<CameraController>` | 854 |
   | `objectLights_` | `std::vector<ObjectLight>` | 1289 |
   | `collisionMap_` | `std::shared_ptr<const HCMap>` | 1449 |
   | `animatedTextureManager_` | `std::unique_ptr<AnimatedTextureManager>` | 900 |
   | `objectInSceneGraph_` | `std::vector<bool>` | 1268 |
   | `portalSystem_` | `std::unique_ptr<PortalSystem>` | 1121 |
   | `renderDistance_` | `float` | 1275 |
   | `frustumCuller_` | `std::unique_ptr<FrustumCuller>` | 855 |
   | `governor_` | `std::unique_ptr<FrameBudgetGovernor>` | 1317 |
   | `bridge_` | `eqt::bridge::GameStateBridge*` | 1446 |
   | `gpuUploadThread_` | `std::unique_ptr<GPUUploadThread>` | 879 — **under `#ifdef EQT_HAS_GLES2`; the context reference carries the same guard** (K01 correction 1 shape) |
   | `tumbleweedManager_` | `std::unique_ptr<Environment::TumbleweedManager>` | 1016 |
   | `entityPrepWorker_` | `std::unique_ptr<class EntityPrepWorker>` (elaborated — copy verbatim) | 863 |
   | `occlusionCuller_` | `std::unique_ptr<SoftwareOcclusionCuller>` | 856 |
   | `indoorRegions_` | `std::unordered_set<size_t>` | 1126 |
   | `treeWindController_` | `TreeWindController` (by value) | 908 |
   | `currentHour_`, `currentMinute_` | `uint8_t` | 1297, 1298 |
   | `objectPositions_` | `std::vector<irr::core::vector3df>` | 1266 |

   Line numbers are leads at `6f696b25`; re-measure. None of the 52 is a
   nested type of `IrrlichtRenderer` (the nested-typed members are all below
   the threshold and bite L03/L04/L05/L11 instead).
   `IrrlichtRenderer` gains `RendererContext ctx_;` in the private tail
   (after `collisionMap_` at ~1449; the tail is already `private:` from
   ~1650, so no reopen) and a `Context()` accessor. **The ctor at
   `renderer_init.cpp` has NO initialiser list — create one to bind
   `ctx_`** (K01 correction). `config_` is assigned later in
   `initLoadingScreen()`; a reference binding is unaffected.

2. **`include/client/graphics/rdp/rdp_component.h` + `src/client/graphics/rdp/rdp_component.cpp`**
   (`git mv` of `renderer_rdp.cpp`, 230 lines, one `#ifdef WITH_RDP` region):
   `class eqt::graphics::RdpComponent`, ctor `(RendererContext&,
   IrrlichtRenderer&)`. Members that move: `rdpServer_`
   (`std::unique_ptr<RDPServer>`, L1877) and `rdpLockKeys_`
   (`RDPLockKeyState`, L1886). Core members reached as `ctx_.eventReceiver`,
   `ctx_.driver`, `ctx_.device`, `ctx_.config`. **No method that stays behind
   is called; no static, nested type or constant is named** — the only
   substitutions are the four `ctx_.` ones. The three `[this]` captures at
   L45/L49/L56 are lambda captures, NOT identity keys (K11 dd): they stay
   `this` and capture the component.

## Callers (receiver-filtered; the compiler confirms)

| method | callers | action |
|---|---|---|
| `initRDP`, `startRDPServer`, `getRDPServer`, `isRDPRunning`, `stopRDPServer` | `application.cpp` (5 sites) | **keep as one-line forwarders** — the K11 rule: `Application` must not learn a component exists. Count them: 5 |
| `captureFrameForRDP` | `renderer_frame_render.cpp` (1 site, in-class unqualified) | retarget to `rdp_->captureFrameForRDP()`; that one line of the frame TU is in territory |
| `getRDPClientCount` | none tree-wide | **delete** (§5; check it publishes no event — it does not) |
| `handleRDP*` | rdp-internal | private on the component |

## Header regions to delete / replace

Public API L711–L750 (6 declarations + comments) → replaced by the 5
forwarders inside the existing `#ifdef WITH_RDP`. Private state
L1875–L1896 (2 members + 4 declarations) → deleted. The include pair at
L53–L56 stays (the forwarder `getRDPServer` returns `RDPServer*`). Add the
component include and `renderer_context.h` include there.

## Predicted `irrlicht_renderer.h` delta

−40 + 7 − 22 + 1 + 1 + 2 + 1 + 1 + 1 = **−48** (1,902 → ~1,854); −46 if
the `rdp_`/`Rdp()` pair needs its own guard pair. Three of the +6 are
one-time scaffolding. Report the measured number against this.

## Tests

`tests/test_rdp_component.cpp` (register next to `test_rdp_input_handler`
in `tests/cmake/` — grep for it): construct an `IrrlichtRenderer` the way
`test_irrlicht_camera_command.cpp` does (read it; headless construction is
proven there), reach `Rdp()`, and assert `isRDPRunning()` is false before
`initRDP`, and that `Context()` binds to the same objects as the renderer's
members (address equality on `driver`, `device`, `config`). Build with
`WITH_RDP` as the tree does; if `WITH_RDP` is OFF in the slot's cache, the
test is `#ifdef`-guarded to a single pass and the Review says so.

Text readers: none for `renderer_rdp.cpp`; `docs/module_map.md` row
updates. `irrlicht_renderer.h` is read by no test as text.

## Territory

`include/client/graphics/renderer_context.h` (new),
`include/client/graphics/rdp/rdp_component.h` (new — the directory exists),
`src/client/graphics/rdp/rdp_component.cpp` (moved), `src/client/graphics/renderer_rdp.cpp`
(moves), `include/client/graphics/irrlicht_renderer.h`,
`src/client/graphics/renderer_init.cpp` (ctor initialiser list + `rdp_`
construction), `src/client/graphics/renderer_frame_render.cpp` (one line),
root `CMakeLists.txt` (one line), `tests/test_rdp_component.cpp` + cmake,
`scripts/refactor/file_size_allowlist.txt` (the `irrlicht_renderer.h`
number), `docs/module_map.md`. **Files under `src/client/graphics` change,
so the visual gate and ARM build run standalone — not skippable.**

## STOP conditions

- A core member's declared type cannot be bound by reference in the
  declaration order available → STOP with the member named.
- The visual gate diffs any golden → STOP; never re-bless.

## Review

(executor: measured vs lead; the 52-member table as bound with any
differences; predicted vs measured header delta; forwarder count; residual;
ctest + visual gate + ARM results; corrections to the mechanism specific to
the renderer — these become the Batch L template.)
