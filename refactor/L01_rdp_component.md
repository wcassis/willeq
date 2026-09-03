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

Executed in `wt/slot3` on `task/L01-rdp-component`, branched from `fable` at
`5f6d0328`. Every verification step passed; nothing STOPped.

### Headline

`include/client/graphics/irrlicht_renderer.h` **1,902 -> 1,871 (-31)**;
allowlist lowered to 1,871 in the same commit. `RendererContext` binds **54**
members (the plan's "52" collapses the `playerX_/Y_/Z_` trio into one row and
`currentHour_/currentMinute_` into another). The moved TU's residual is
**zero code lines added and zero modified**; the only code lines removed are
the sanctioned §5 deletion and the include block. New suite total 3,613, one
failure (the ALSA quirk).

### The 54-member table as bound

Every member in the plan's table exists at this tip at the type it names, and
every one was bound by reference. The two differences are both counting, not
substance:

| difference | detail |
|---|---|
| **54 members, not 52** | the plan's table has 52 ROWS; two of them name more than one member. `playerX_`'s row says "and its `playerY_`/`playerZ_` siblings -- measure; take the trio if the siblings are >= 5 too": all three are declared on ONE line (`float playerX_ = 0, playerY_ = 0, playerZ_ = 0, playerHeading_ = 0;`, header L1402), so the trio was taken and `playerHeading_` was NOT (it is not in the plan's core). `currentHour_`/`currentMinute_` is the other multi-member row. |
| every type copied verbatim | including `std::unique_ptr<class EntityPrepWorker>&` (the elaborated form), `std::shared_ptr<EQT::Graphics::BspTree>&` (the qualified one) and `TreeWindController&` (the one by-value member, bound as a plain reference). |

Confirmed conditional: `gpuUploadThread_` is under `#ifdef EQT_HAS_GLES2`
(header L879) and its context reference carries the same guard, in the struct
**and** in the ctor initialiser list. It is the renderer's analogue of K01
correction 1's `WITH_AUDIO` member; the shape transferred unchanged.

Declaration order held: the last-declared core member is `collisionMap_`
(L1449), and `ctx_` was placed immediately after it, so every reference binds
a member declared earlier. No STOP condition on binding was reached.

### Predicted vs measured header delta

| | lines |
|---|---|
| predicted (plan) | **-48** (-46 with an extra guard pair) |
| measured | **-31** (1,902 -> 1,871) |

The 17-line gap is entirely in the two blocks the plan costed optimistically:

| block | plan | actual |
|---|---|---|
| public API L711-750 deleted | -40 | -40 |
| public API replacement | +7 (5 forwarders + guard pair, which already existed) | **+12** -- 5 forwarders + `Rdp()` + a 5-line comment carrying the K11 rule and the `getRDPClientCount` deletion; the `#ifdef`/`#endif` pair was reused, as predicted |
| private state L1875-1896 deleted | -22 | -22 |
| private replacement | +1 (`rdp_`) | **+4** -- `rdp_` + a 3-line comment |
| `ctx_` member | +1 | **+6** -- member + a 4-line comment + a blank |
| `Context()` accessor | +1 | **+4** -- accessor + a 2-line comment + a blank |
| includes | +2 | +2 |

So the mechanism's arithmetic is right and the miss is **comment budget**: the
plan costed the scaffolding at one line per construct and the shipped
scaffolding carries the transition notes every K unit also wrote. **Batch L
plans should predict +3 to +6 per scaffolding construct, not +1.** The
one-time part of it (`ctx_`, `Context()`, the two includes = 12 lines) is paid
once for the whole batch; L02 onward start from a +4 baseline (the component
pointer and its accessor) and delete every declaration they move.

### Forwarders

**5**, exactly as the plan's caller table predicted, all inline one-liners
inside the existing `#ifdef WITH_RDP`: `initRDP`, `startRDPServer`,
`stopRDPServer`, `isRDPRunning`, `getRDPServer`. Plus `Rdp()`, the accessor,
which is scaffolding rather than a forwarder.

`getRDPClientCount()` -- **zero callers tree-wide confirmed** (`grep` over
every `.cpp`/`.h`/`.cc`: the only hits were its own declaration and its own
definition), body is `return rdpServer_ ? rdpServer_->getClientCount() : 0;`
with no `pushEvent`/`GameEventType::` (K12 correction ff checked) -- **deleted**.

`captureFrameForRDP()` retargeted at its single call site,
`src/client/graphics/renderer_frame_render.cpp:552`, to
`rdp_->captureFrameForRDP()`. The compiler confirmed the caller set: after the
declarations were removed the build named exactly the sites the table listed
and no others.

### Residual (§Mechanism 8)

Normalisation applied to the NEW side only, then both sides sorted:
`RdpComponent::` -> `IrrlichtRenderer::`, `ctx_.driver` -> `driver_`,
`ctx_.device` -> `device_`, `ctx_.config` -> `config_`,
`ctx_.eventReceiver` -> `eventReceiver_`. Comment and blank lines then
dropped, leaving 155 old code lines against 149 new:

```
removed (8):
  size_t IrrlichtRenderer::getRDPClientCount() const {      -- the §5 deletion
      return rdpServer_ ? rdpServer_->getClientCount() : 0;
  }
  #ifdef WITH_RDP                                           -- the old include guard
  #endif
  #include "client/graphics/irrlicht_renderer.h"
  #include "client/graphics/rdp/rdp_input_handler.h"
  #include "client/graphics/rdp/rdp_server.h"
added (2):
  #include "client/graphics/rdp/rdp_component.h"
  #include "client/graphics/renderer_event_receiver.h"
```

**Zero moved body lines added, zero modified.** The five removed non-§5 lines
are the include block, which the component header now owns (it includes
`rdp_server.h` and `rdp_input_handler.h` itself). The
`renderer_event_receiver.h` include is NEW and was found by the compiler: the
old TU got the complete type transitively through `irrlicht_renderer.h`, and
`ctx_.eventReceiver->isKeyDown()` needs it. **This is the renderer's version of
K03 correction (e): dropping `irrlicht_renderer.h` from a moved TU drops ~60
transitive includes with it, and the compiler is the only reliable way to find
which ones the bodies actually used.** One include, one build iteration.

### Verification

| step | result |
|---|---|
| `cmake --build build -j24` | **exit 0**. One intermediate failure, fixed: the 6 incomplete-type errors on `RendererEventReceiver` above. No timeout, no truncation scan needed |
| `ctest --test-dir build -j6 --output-on-failure` | **3,612 / 3,613 passed**, 17 skipped, **1 failure: `AudioManagerTest.PreloadCommonSounds`** (the known ALSA quirk) -- the only permitted one. `FileSizeCeiling` and `DocCitations` both PASS |
| `test_rdp_component` (new) | **8 tests, all pass**, `WITH_RDP=ON` in this slot's cache so the RDP arm is live (not the `#ifdef`-guarded single pass) |
| `scripts/build-gles2-x86.sh` | **exit 0** |
| visual gate standalone | **PASS** (`=== VISUAL GATE: PASS ===`). `git status --porcelain` shows **no golden path** -- goldens untouched, nothing re-blessed |
| `scripts/build-arm-noble.sh` | **exit 0** |
| `scripts/refactor/check_file_sizes.sh --all` | **834 checked, 834 ok, 0 warn, 0 FAIL**, with the `irrlicht_renderer.h` allowlist entry lowered 1,902 -> 1,871 |

### Line counts

| file | before | after |
|---|---|---|
| `include/client/graphics/irrlicht_renderer.h` | 1,902 | **1,871** (-31) |
| `src/client/graphics/renderer_rdp.cpp` -> `src/client/graphics/rdp/rdp_component.cpp` | 230 | 233 (+3: a longer file-header note, one more include, one fewer method) |
| `include/client/graphics/renderer_context.h` | -- | 139 (new) |
| `include/client/graphics/rdp/rdp_component.h` | -- | 76 (new) |
| `tests/test_rdp_component.cpp` | -- | 132 (new) |

### Corrections to the mechanism for the renderer (the Batch L template)

1. **The namespace is `EQT::Graphics`, NOT `eqt::graphics`.** The plan (and
   the batch doc) write `eqt::graphics::RdpComponent` and
   `eqt::graphics::RendererContext` by analogy with `eqt::guild`. There is no
   lowercase `graphics` namespace anywhere in the tree: `IrrlichtRenderer`,
   `RDPServer`, `RDPLockKeyState`, `rdpScancodeToIrrlicht` and every other
   graphics type live in `EQT::Graphics`. Putting the component in a new
   namespace would have forced a qualification on every one of those names
   inside the moved bodies -- a fifth substitution class the mechanism does not
   allow and the residual would have shown. **Both new types were put in
   `EQT::Graphics`, and every L unit should do the same.** This is the
   renderer's version of K01 correction 9, with the opposite answer: match the
   siblings, and the renderer's siblings are capitalised.

2. **The context is 54 references, and the plan's table rows are not
   members.** A row may name a trio declared on one line. Count members, not
   rows, before writing the initialiser list -- aggregate initialisation is
   positional and a miscount is a silent type mismatch at best.

3. **`RendererContext` needs almost no forward declarations of its own.**
   `include/client/graphics/renderer_types.h` already carries `RendererConfig`,
   `ObjectLight`, `S3DZone`, `CameraController`, `ConstrainedTextureCache`,
   `SkyRenderer`, `WeatherEffectsController`, `<irrlicht.h>` and the std
   containers. Including it plus 17 forward declarations (`EntityRenderer`,
   `ZoneShaderManager`, `DoorManager`, `BspTree`, `AnimatedTextureManager`,
   `RendererEventReceiver`, `PortalSystem`, `FrustumCuller`,
   `FrameBudgetGovernor`, `GPUUploadThread`, `SoftwareOcclusionCuller`,
   `TreeWindController`, `EntityPrepWorker`, the three `Environment::`
   managers, `Detail::DetailManager`) plus `HCMap` and
   `eqt::bridge::GameStateBridge` was enough. **Every core member is held by
   reference or by `unique_ptr`/`shared_ptr` reference, so an incomplete type
   suffices for all 54** -- including `treeWindController_`, which is by value
   on `IrrlichtRenderer` but a reference here. `renderer_context.h` therefore
   does NOT include `irrlicht_renderer.h` and no cycle exists.

4. **The ctor initialiser list is the whole risk surface, and it is
   positional.** `IrrlichtRenderer::IrrlichtRenderer()` had an empty body and
   no list (K01 correction 8's shape, confirmed). The list must repeat the
   struct's member order exactly, **including the `#ifdef EQT_HAS_GLES2`
   member in the same position in both files**. Write the struct and the list
   from one ordered list; do not hand-maintain two.

5. **The component pointer is constructed in the ctor BODY**, first statement,
   after `ctx_` is bound in the list -- `rdp_ = std::make_unique<RdpComponent>(ctx_, *this);`
   under `#ifdef WITH_RDP`. Same as K01.

6. **A whole-family `#ifdef` propagates to five places, not one.** `WITH_RDP`
   guards: the component header's entire body, the component pointer, the
   `Rdp()` accessor, the forwarder block, and the `make_unique` in the ctor.
   The moved TU keeps its own inner `#ifdef WITH_RDP` verbatim even though
   CMake already gates the file -- that is the verbatim rule, not an oversight.

7. **The `IrrlichtRenderer&` transition handle is currently UNUSED by
   `RdpComponent`** and was kept anyway, as the mechanism prescribes, for the
   families that follow. `RdpComponent` needs **no `friend` declaration**: it
   names no `IrrlichtRenderer` private static, nested type or constant, so it
   passes both axes of K13 correction (ii) on day one -- the second component
   in the codebase after `eqt::gm::GmComponent` to do so. **L12 can drop the
   handle from this component without a census.**

8. **`Application` never learned a component exists.** The K11 rule held with
   zero pressure: the five forwarders are one line each and `application.cpp`
   is untouched by this unit. The one in-class caller
   (`renderer_frame_render.cpp`) was retargeted to `rdp_->`, which is the
   in-family form K01's addendum prescribes.

9. **A test cannot reach the context's private-only members from outside.**
   The plan asked for address equality on `driver`, `device` and `config`.
   `getDriver()`/`getDevice()` return the pointer VALUE (asserted, both null on
   a never-Init()'d renderer) and there is no public accessor returning a
   reference to `config_` at all. Rather than add a `friend` line or a getter
   -- both of which would grow the header this unit exists to shrink --
   `tests/test_rdp_component.cpp` proves the binding by **mutation in both
   directions**, which is strictly stronger than address equality: writing
   `Context().config.constrainedConfig.deferredAssetLoading` and reading it
   back through `isDeferredAssetLoading()`; `Context().renderDistance` /
   `getRenderDistance()`; `Context().portalOcclusionEnabled` /
   `isPortalOcclusionEnabled()`; and the reverse for `setBridge()`,
   `setConstrainedTextureCache()`, `setRenderDistance()`. One true address
   equality is available and asserted: `getCurrentZoneName()` returns a const
   reference, so `&Context().currentZoneName == &getCurrentZoneName()`.
   **Value equality on two default-constructed objects proves nothing; every L
   context test should mutate.**

10. **The test was registered in `tests/cmake/renderer_guards.cmake`, not
    `tests/cmake/audio.cmake`** where `test_rdp_input_handler` lives (the plan
    said "next to `test_rdp_input_handler`"). It links `willeq_core` and
    constructs a real `IrrlichtRenderer`, which is `renderer_guards.cmake`'s
    subject and idiom (`IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)`); the RDP
    assertions are `#ifdef WITH_RDP` so the binary builds and passes in a
    configure without FreeRDP. Recorded as a deviation, not absorbed.

11. **The visual gate and the ARM build are NOT skippable for an L unit** and
    were both run standalone here -- every L unit changes files under
    `src/client/graphics` by construction. (Contrast K01's addendum, which
    correctly skipped them because no graphics file changed.)

12. **`docs/module_map.md`'s renderer section needs a components table**, not
    just a row edit: the flat "`irrlicht_renderer.cpp` + 24 `renderer_*.cpp`
    TUs" sentence goes stale on every L unit. L01 added the table and the
    `RendererContext` note; L02+ append a row and decrement the TU count.
