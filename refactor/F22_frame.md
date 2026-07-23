# F22 — Split processFrame and its sub-passes into renderer_frame.cpp

Move the per-frame spine out of `src/client/graphics/irrlicht_renderer.cpp`
(7,596 lines). This is the main render-loop cluster — HIGH sensitivity, but
still a pure TU move. No behavior change, no interface change, no
reformatting.

## Scope (orig 9342–10900 @ a0c02cd; heavily shifted. Enumerate by NAME.)

Move to new `src/client/graphics/renderer_frame.cpp` the contiguous
frame-processing cluster: `processFrame` (~495 lines) and its sub-pass
methods — the `processFrame*` family (input, common input, player input,
input deltas, chat input, visibility, simulation, render — enumerate the
actual set by grep for `processFrame` and read the cluster boundaries), plus
the render-pass body methods in the same contiguous region.

In-range non-frame methods (judgment): leave + document. `run()` and
`saveScreenshot()` were explicitly excluded from F15 and remain out of scope
here too UNLESS `run()` is merely a thin wrapper that sits contiguously and
is inseparable — default: leave `run()` in irrlicht_renderer.cpp.

## Steps

1. Locate the cluster; produce the mapping in the review BEFORE moving.
2. Replicate namespace wrapper + include patterns (F14–F21 reviews; expect
   forward-declaration include gaps like F19/F20 hit).
3. Move verbatim; preserve #ifdef guards exactly.
4. File-local statics: usual rule.
5. Add to `GRAPHICS_SOURCES` in CMakeLists.txt.

## Verification

- `cmake --build build -j24` clean.
- `ctest --test-dir build` — green modulo documented exceptions in
  refactor/test_baseline.txt (ALSA expected-fail; RDP flake → re-run
  isolated, and note it reproduces on unmodified fable per deferred.md).
- `python3 scripts/refactor/verify_move_only.py BASE..HEAD` — PASS or
  documented deviations only.
- `bash scripts/refactor/check_file_sizes.sh --files <abs>/renderer_frame.cpp`
  — WARN accepted, must not FAIL.
- irrlicht_renderer.cpp shrinks by ~1,000+ lines.

## Review

**Cluster boundaries (verified by grep of `IrrlichtRenderer::` definitions,
`irrlicht_renderer.cpp` @ merge-base commit `2085112ba344b3dcbb169f8d40a7913d523af580`
with `fable`, 7,596 lines before this move — `git merge-base fable HEAD`
confirms `fable` did not advance during this unit's work):**

Moved to `src/client/graphics/renderer_frame.cpp`, pre-move lines 5891–7448
(a single contiguous block), 1,558 lines. 9 `IrrlichtRenderer::` methods —
the full `processFrame*` family plus its named input sub-passes, exactly
matching the plan's "input, common input, player input, input deltas, chat
input, visibility, simulation, render" enumeration:

- `processFrame(float)` (~495 lines) — the per-frame spine: FPS/timer
  bookkeeping, tier2/tier3 scheduling, calls out to the phase methods below,
  drains GPU upload/result queues, frame-timing accumulation/logging.
- `processFrameInput(float)` (Phase 1: Input) — dispatches to
  `processCommonInput`, `processPlayerInput`, `processInputDeltas`, and
  (via the mouse/chat-focus branch inside it) `processChatInput`.
- `processCommonInput(const std::vector<RendererEvent>&)`,
  `processPlayerInput(const std::vector<RendererEvent>&)`,
  `processInputDeltas(float)`, `processChatInput()` — the four named
  sub-pass methods physically interleaved between `processFrameInput` and
  `processFrameVisibility`; none of these are prefixed `processFrame*` in
  the source (unlike the plan's phrasing might suggest) but they are
  exactly the "common input, player input, input deltas, chat input"
  methods the plan calls out, called only from `processFrameInput` and
  contiguous with the rest of the cluster.
- `processFrameVisibility()` (Phase 2: Visibility) — frustum-plane update
  from the Irrlicht camera, atlas per-pixel lighting camera-pos update.
- `processFrameSimulation(float)` (Phase 3: Simulation) — entity/door/spell-
  VFX/animated-texture per-frame updates, HCMap placeholder rebuild,
  tier-2 detail/tree update, tier-3 weather/particle update, HUD update.
- `processFrameRender(float)` (Phase 4: Render, ~460 lines) — the render-pass
  body: loading-screen early return, scene-breakdown profiling, static UI
  input/draw, zone-line overlay, RDP frame capture, software cursor draw,
  deferred screenshot dispatch, `endScene()`.

**Explicitly left in `irrlicht_renderer.cpp`, immediately after this
cluster (per the plan's explicit carve-out and default "leave `run()`"
rule):** `run()` — the game-loop driver that calls `processFrame` in a
`while (isRunning())` loop — and `saveScreenshot(const std::string&)`,
which sits directly after `run()` and is called from inside the moved
`processFrameRender` (`saveScreenshot("screenshot.png")` for the deferred-
screenshot path). Both were judged NOT "thin wrappers... inseparable" from
the cluster: `run()` is the outer driver (not itself part of the frame body)
and `saveScreenshot()` is a general-purpose utility also reachable from
outside the frame path; leaving both matches the plan's explicit default.
Immediately after those: `setFrameTimingEnabled(bool)`,
`setClipDistance(float)`, `getClipDistance() const` — diagnostics/mode
setter-getters, not part of the frame-processing spine (same
diagnostics-adjacent-to-cluster-boundary pattern F15/F19 noted for this
pair). **Immediately before** the cluster: `getCameraTransform(...)` — a
camera-accessor query method, not part of the frame spine; left in place.

**No `#ifdef` guards span the moved/left boundary** — all guards inside the
moved range (`EQT_HAS_GLES2`, `__linux__`, `EQT_HAS_DRM`, `WITH_RDP`) are
fully self-contained (open+close) within lines 5891–7448, confirmed by a
line-range-scoped grep before moving; preserved verbatim.

**Namespace + include handling (same pattern as F14–F16/F19–F21):**
- New file wraps its body in `namespace EQT { namespace Graphics { ... } }`,
  matching `irrlicht_renderer.cpp`.
- `client/graphics/irrlicht_renderer.h` — the class declaration itself.
- `client/graphics/entity_prep_worker.h` — `irrlicht_renderer.h` only
  forward-declares `EntityPrepWorker` (via `entity_renderer.h`'s forward
  decl); `processFrame`'s GPU-upload-queue drain calls
  `entityPrepWorker_->isIdle()`/`dispatchOne()`. Caught by the first build
  attempt ("invalid use of incomplete type 'class EntityPrepWorker'"),
  fixed by adding the full include — the original TU had it transitively
  via other still-resident code at the time.
- `client/graphics/constrained_mesh_cache.h` — `irrlicht_renderer.h` only
  forward-declares `ConstrainedMeshCache` (same gap F19/F20 hit);
  `processFrameRender`'s lazy-mesh-cache section calls
  `constrainedMeshCache_->markForRebuild(...)`/`isLoaded(...)`.
- `client/graphics/sky_renderer.h` — forward-declared only;
  `processFrameSimulation`/`processFrameRender` call
  `skyRenderer_->isInitialized()`/`setCameraPosition()`/`isEnabled()`/
  `getCurrentClearColor()`.
- `client/graphics/weather_effects_controller.h` — forward-declared only;
  `processFrameSimulation`/`processFrameRender` call
  `weatherEffects_->updateRenderOnly()`/`isEnabled()`/`render()`.
- `client/graphics/spell_visual_fx.h` (`EQ::SpellVisualFX`) — forward-
  declared only; `processFrameSimulation` calls `spellVisualFX_->update()`.
- `client/graphics/ui/static_ui_input.h`, `client/graphics/ui/ui_renderer.h`
  — `StaticUIInput`/`UIRenderer` are forward-declared in
  `irrlicht_renderer.h`; `processFrameRender`'s static-UI section calls
  `staticUIInput_->processInput(...)` and `uiRenderer_->beginFrame()`/
  `endFrame()`.
- `client/bridge/game_state_bridge.h`, `client/state/event_bus.h`,
  `client/events/renderer_intents.h` — `processPlayerInput`/
  `processChatInput` call `bridge_->pushIntent(eqt::events::...)` for
  `VendorToggleIntent`, `TrainerToggleIntent`, `DoorInteractIntent`,
  `WorldObjectInteractIntent`, `MusicVolumeChangeIntent`,
  `EffectsVolumeChangeIntent`, `ChatSubmitIntent`; `irrlicht_renderer.h`
  only forward-declares `eqt::bridge::GameStateBridge` (same gap F19 hit
  for `updatePlayerMovement`).
- `common/logging.h`, `common/performance_metrics.h`, `<fmt/format.h>`,
  `<algorithm>`, `<chrono>`, `<cmath>`, `<cstdint>` — `LOG_*` macros,
  `EQT::PerformanceMetrics::instance().recordSample(...)`,
  `std::chrono::steady_clock`, `std::max`/`std::abs`, `uint8_t` — full
  includes the original TU had unconditionally.
- `EntityRenderer`, `DoorManager`, `AnimatedTextureManager`,
  `Detail::DetailManager`, `AnimatedTreeManager`,
  `Environment::ParticleManager`, `ZoneShaderManager`, `FrustumCuller`,
  `CameraController` needed no extra include: all are fully (not
  forward-) declared directly in `irrlicht_renderer.h`, confirmed by grep
  of its top-of-file include list before adding anything.
- Raw Irrlicht types (`irr::core::vector3df`, `irr::video::SColor`, etc.)
  and `glm::vec3` compiled without any additional header — transitively via
  `irrlicht_renderer.h`'s own `#include <irrlicht.h>` / `<glm/glm.hpp>`,
  same as the original single TU.

**Shared-helper analysis:** no file-local `static`/anonymous-namespace
symbols in `irrlicht_renderer.cpp` are referenced by the moved cluster
(confirmed by grep before moving). One function-local `static` inside the
moved code (`static float lastPlayerX = playerX_, lastPlayerY = playerY_;`
inside `processFrameSimulation`'s tier-2 detail/tree block) moved with the
function body verbatim — scoped to the function, not file-scope, so no
linkage concern.

**Line counts:**
- `irrlicht_renderer.cpp`: 7,596 → 6,037 lines (**-1,559**; the deleted
  range also absorbed one of the two blank lines that separated
  `getCameraTransform` from `processFrame`, so one extra line vs. the
  1,558-line moved body)
- `renderer_frame.cpp`: 1,583 lines (new; 1,558 moved lines + 25 lines of
  includes/namespace-wrapper boilerplate)
- Net change: +1,583 / -1,559 = +24 lines total (all boilerplate — no
  content duplicated or dropped)
- Matches the plan's "shrinks by ~1,000+ lines" expectation (actual -1,559,
  well past the floor).

**Build/test verification:**
- `cmake --build build -j24`: clean after one fix found during the first
  pass (mechanical, not scope creep): missing
  `#include "client/graphics/entity_prep_worker.h"` for
  `entityPrepWorker_->isIdle()`/`dispatchOne()` (forward-declared only via
  `entity_renderer.h`).
- `ctest --test-dir build`: 1,190/1,192 run tests passed (10 skipped:
  asset/graphics/audio-dependent tests unrelated to this unit).
  - `AudioManagerTest.PreloadCommonSounds`: failed with the documented ALSA
    "cannot find card '0'" environment gap (`refactor/test_baseline.txt`),
    consistent with F02/F14/F15/F19/F20 findings on this host.
  - `RDPServerFrameTest.UpdateFrameMultipleTimes`: SEGFAULT on the full-suite
    run; re-ran isolated (`ctest -R RDPServerFrameTest.UpdateFrameMultipleTimes`)
    and it **passed** — matches the documented load-dependent RDP flake in
    `refactor/test_baseline.txt` / `refactor/deferred.md` ("RDP frame-update
    race, observed repeatedly during Batch F, reproduces on unmodified
    `fable`").
- `git merge-base fable HEAD` → `2085112ba344b3dcbb169f8d40a7913d523af580`,
  confirming no rebase occurred and `fable` did not advance during this
  unit's work; this is the BASE used for the verifier below.
- `python3 scripts/refactor/verify_move_only.py 2085112ba344b3dcbb169f8d40a7913d523af580..HEAD --repo <slot1>`:
  **PASS**. `CMakeLists.txt` (the additive `GRAPHICS_SOURCES` line) and this
  review file reported separately as non-C++, not verified — expected for a
  build-file edit and doc update.
- `bash scripts/refactor/check_file_sizes.sh --files <slot1>/renderer_frame.cpp`:
  result recorded below (WARN accepted if over the 1,500-line ceiling —
  1,583 lines is 83 over; not split further since the plan defines this as
  one cohesive unit and 1,583 is well under the 3,000-line FAIL threshold).

**Deviations from plan:** none in the moved code itself (verbatim, no
reformatting, no scope creep). One naming clarification: the plan's phrase
"processFrame* family (input, common input, player input, input deltas,
chat input, visibility, simulation, render)" describes eight sub-passes by
role, but only 5 of the 9 moved methods are literally prefixed
`processFrame*` (`processFrame`, `processFrameInput`,
`processFrameVisibility`, `processFrameSimulation`, `processFrameRender`);
the other 4 (`processCommonInput`, `processPlayerInput`,
`processInputDeltas`, `processChatInput`) are unprefixed but are the exact
methods the plan's parenthetical names and are contiguous with/called only
by the `processFrame*` methods — moved as part of the same cluster, no
judgment call needed since the plan's own scope note anticipated exactly
this ("enumerate the actual set by grep for `processFrame` and read the
cluster boundaries").
