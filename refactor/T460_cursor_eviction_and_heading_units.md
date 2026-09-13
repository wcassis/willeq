# T460 — the software cursor dies at the first zone unload; a 0–360 heading on a 0–512 channel

Class A, **no lock, no Pi**, slot2. x86 unit; T459 (slot3) proves both on the
Pi afterwards.

## 1. Software cursor evicted and never reloaded (Will: no cursor on the Pi)

Lead (orchestrator triage, confirm before fixing):
`IrrlichtRenderer::createSoftwareCursor()` loads `software_cursor` through the
`ConstrainedTextureCache` only under `--drm`; `FrameComponent::renderFrameTail`
draws it while `owner_.softwareCursorTexture_` is non-null. At zone unload
`ConstrainedTextureCache::clear()` notifies the eviction listeners of every
texture that is not `protectTexture()`'d (today: the UI atlas and the item
icon sheets), the cursor's guard in `renderer_zone_merge.h` nulls
`softwareCursorTexture_`, and nothing ever reloads it. An LRU eviction under
the orangepi budget would do the same. BOARD notes the guard binds a
REFERENCE to `softwareCursorTexture_` (why the member stayed on the renderer —
L-batch census row 5) — respect that lifetime contract.

- Pre-fix failing test first: drive the real `ConstrainedTextureCache` (the
  `tests/test_software_cursor_pack_name.cpp` T413 fixture is the model) —
  load the cursor, `clear()`, and assert the renderer still has a drawable
  cursor texture; also an eviction-under-budget case.
- Fix: protect it like the UI atlas (preferred — it is a UI asset needed in
  every zone), or reload on demand; justify in the Review. Grep for OTHER
  renderer-lifetime textures loaded through the cache with a null-on-evict
  guard and no reload (loading screen, fallback textures) and board any that
  have the same shape (B520–B529 are yours).

## 2. `PlayerMoved` heading published in degrees where the consumer expects 0–512

Lead: `src/client/movement/movement_update.cpp` publishes `HeadingDeg()`
(0–360) while every other publisher of the same event converts to the EQ
0–512 unit that `IrrlichtRenderer::setPlayerPosition` expects. If true, any
game-thread-driven move (moveto, follow, auto-attack chase, a server
correction) sets the renderer's player heading to a wrong value that the next
render-thread integration overwrites — an alternation between two camera
headings, which matches Will's "jerks between two perspectives while moving"
(Follow camera, moving forward, flat ground). T459 is measuring the live
jerk; you establish the code truth.

- Census every publisher and consumer of the event/payload field (name them
  in the Review with the unit each uses). Confirm or refute the mismatch.
- If confirmed: pre-fix failing test at the publisher (payload unit), fix,
  and a test pinning the unit contract for all publishers (the census as a
  test, the T264 reachability idiom, if cheap). ⚠ The 90° player/NPC heading
  offset (`docs/CLAUDE.md`, memory) is load-bearing — do not touch it.
- Also check whether a keyboard forward run on the render thread publishes a
  `PlayerPositionChanged` intent whose game-side echo comes back as
  `PlayerMoved` into `setPlayerPosition` every tick (self-echo). If the echo
  exists, board it with the exact round trip even if the heading unit is
  right — it is the second-strongest lead for the jerk.

## Proof

Standard verification bar (EXECUTOR_PROTOCOL): background builds with exit
files, `ctest -j6`, G-batch goldens for every TU touched (CLAUDE.md merge
rule — `renderer_frame_render` and `renderer_player_physics` are pinned),
visual gate (no golden moves), ARM build, file sizes.

File territory: texture cache protect list, `renderer_init.cpp` /
`irrlicht_renderer.cpp` cursor creation, `renderer_zone_merge.h` guard,
`src/client/movement/`, `src/client/bridge/` publisher sites, new tests.
**Do NOT edit `src/client/graphics/player/renderer_camera.cpp`, the hotkey
manager or slash commands (T461 owns them), nor `refactor/BOARD.md` outside
B520–B529.**

STOP: the fix for either needs a change inside `renderer_camera.cpp` (report
the needed change instead — T461 is in it); an image golden moves.

## Review

Executor, slot2, branch `task/T460-cursor-eviction-heading-units` off `fable`
`9c164693`. No lock, no Pi, no server contact.

### 1. Software cursor -- lead CONFIRMED (board B520, closed here)

Code truth, every hop cited:
- `IrrlichtRenderer::createSoftwareCursor()` (irrlicht_renderer.cpp) loaded
  `software_cursor` via `getOrLoad` + `uploadTexture` and never called
  `protectTexture()`. The only protected names in the tree were `ui_atlas`
  (`UIAtlas::load`) and the item icon sheets (`ItemIconSheets`).
- `ConstrainedTextureCache::clear()` skips only protected names. For every
  other name it notifies the listeners and `removeTexture()`s it.
  `evictUntilAvailable()` takes the oldest unprotected LRU entry, which is
  always the startup-loaded cursor.
- `SingleTexturePointerGuard::onTextureEvicted` (renderer_zone_merge.h),
  registered in `renderer_init.cpp`, nulls `softwareCursorTexture_`.
  `FrameComponent::renderFrameTail` draws only while it is non-null, and
  nothing reloads it. So the cursor was gone for the whole process from the
  first zone unload (the guard's comment said "for the rest of this zone
  visit", which was wrong; corrected).

Fix: **protect, not reload**. The load moved into a new free function,
`EQT::Graphics::AcquireSoftwareCursorTexture()`
(`include/client/graphics/software_cursor_texture.h`,
`src/client/graphics/renderer/software_cursor_texture.cpp`). It calls
`protectTexture("software_cursor")` before loading, and `createSoftwareCursor()`
calls it. Why protect:
- The cursor is a process-lifetime UI asset of the same class as `ui_atlas`.
- It is 8x10 px, so pinning it costs nothing.
- A reload-on-demand would put a pack lookup, whose miss is `exit(1)`, on the
  frame path.

The guard and its reference-to-member lifetime (L13 census row 5) are
untouched; the guard stays registered as the use-after-free net. The member
did not move.

Tests (`tests/test_software_cursor_eviction.cpp`: real cache, EDT_NULL device,
real acquisition, real guard):

| test | pre-fix | post-fix |
|---|---|---|
| `AZoneUnloadLeavesTheRendererADrawableCursor` (`clear()` twice) | FAIL: pointer NULL, texture gone | PASS |
| `BudgetPressureNeverEvictsTheCursor` (16 x budget/4 fillers) | FAIL: "LRU eviction took the software cursor" | PASS |
| `CreateSoftwareCursorUsesTheProtectedAcquisition` (source) | FAIL (test bug: matched a comment; tightened to `->getOrLoad(`) | PASS |

`tests/test_software_cursor_pack_name.cpp` (T413) now reads the cache-name
literal from the new TU; its four cases pass.

Same-shape census: renderer-lifetime cache textures with an evict guard and
no reload. Every `getOrLoad` / `onTextureEvicted` site under
`src/client/graphics` was checked:
- `ParticleManager` (both atlases) and `DetailManager` reload in `onZoneEnter`,
  so they are not affected.
- `SpellVisualFX` textures are never loaded on GLES2 (T124's finding).
- Rain and snow overlays are compiled out on GLES2.
- Three have the shape and are boarded **unconfirmed**:
  - **B522** `StormCloudLayer::cloudFrames_`
  - **B523** `TumbleweedManager::tumbleweedTexture_` (no listener at all)
  - **B524** `BoidsManager` `creature_atlas`

### 2. PlayerMoved heading unit -- lead CONFIRMED (board B521, closed here)

Census of `GameEventType::PlayerMoved` (dirs grepped: `src/ include/ tests/
tools/ benchmarks/`):

| site | bus | `.heading` unit |
|---|---|---|
| `BridgeComponent::OnSpawnMovedGraphics` (bridge_component.cpp) | renderer bridge | `HeadingDeg()*512/360` -- 512 |
| `BridgeComponent::ConvertSelfZToRenderFrame` (bridge_component_intents.cpp) | bridge | 512 |
| `EntitiesComponent` ZoneSpawns self path (entities_component.cpp) | bridge | 512 |
| `EntitiesComponent` NewSpawn self path (entities_component_updates.cpp) | bridge | 512 |
| `ZoneComponent::ZoneProcessPlayerProfile` (zone_entry.cpp) | bridge | 512 |
| `ZoneComponent::RelocateWithinZone` (zone_component.cpp) | bridge | 512 |
| **`MovementComponent::UpdateMovement` (movement_update.cpp)** | bridge | **degrees -- WRONG** |
| **`WarpToPosition` (commands_movement.cpp, `/warp`)** | bridge | **degrees -- WRONG** |
| `PlayerState::firePlayerMovedEvent` (player_state.cpp) | STATE bus (`GameState::events()`; only `AutomatedMode`'s catch-all subscribes) | degrees, that bus's own unit -- not a renderer input |

Consumer: `IrrlichtBridge::applyEvent` hands `d.heading` to
`IrrlichtRenderer::setPlayerPosition`, which goes to
`PlayerCameraComponent::setPlayerPosition`. That stores it in
`ctx_.playerHeading` and passes it to `CameraController::setFollowPosition`,
logging "EQ heading 0-512". The renderer's own `PlayerPositionChanged`
intent carries the same unit back (`renderer_player_physics.cpp`:
"EQ format (0-512)"), and `OnGraphicsMovement` divides by 512/360.

So every tick of a game-driven move (`/moveto`, `/follow`, auto-attack chase:
`has_console_movement`) set the camera heading to deg/512*360 of the right
one. Due east (90 deg) the camera got 90 units = 63 deg.

Fix: both sites now publish `HeadingDeg() * 512.0f / 360.0f`. The 90-degree
player/NPC offset is untouched: both sites convert units only, after every
flip.

Tests (`tests/test_player_moved_heading_units.cpp`):

| test | pre-fix | post-fix |
|---|---|---|
| `TheConsoleMovementIntegratorPublishesThe512Unit` (`MoveTo` east + `UpdateMovement`) | FAIL: published 90, want 128 | PASS |
| `WarpPublishesThe512Unit` (`RequestWarp("10 20 30")`) | FAIL: published 90 | PASS |
| `EveryBridgePublisherConvertsTo512` (source census, count = 8) | FAIL: named exactly movement_update.cpp:503 and commands_movement.cpp:237 | PASS |
| `TheConsumerReadsThe512Unit` | PASS | PASS |

**Self-echo** (plan's second lead), code truth:
- A keyboard run publishes `PlayerPositionChanged` from
  `renderer_player_physics.cpp`, throttled to 250 ms and only when position or
  heading changed. `BridgeComponent::OnGraphicsMovement` stores it with
  `SetPosLocal`/`SetHeadingLocal` (no event) and then `SendPositionUpdate`.
  **No `PlayerMoved` is published on that path**, and `UpdateMovement` returns
  early without console movement. So there is no client-side per-tick echo.
- Two round trips remain OPEN and are boarded as **B525**:
  - The server returning our own `OP_ClientUpdate`. That would go through the
    `ZoneProcessClientUpdate` self branch, then `OnSpawnMovedGraphics`, then
    `PlayerMoved`, then `setPlayerPosition`, which also re-arms
    `playerPositionIsSeed_`. Whether eqemu sends it to the mover is not in
    this tree; T459 can count those lines live.
  - Keyboard input DURING console movement: both threads write the camera
    heading.

### Deviations
- `commands_movement.cpp` is outside this plan's listed territory. It is not
  T461's either: T461's plan lists `graphics/slash/` and `hotkey_manager.cpp`,
  and my edit is a one-statement body change in `WarpToPosition`, not command
  registration. It is the same defect as the lead, and the census test would
  otherwise have had to allowlist a known-wrong site. Flagged for the
  orchestrator.
- Doc edits outside territory, required by the `DocCounts` ctest (the one red
  in the first full run):
  - `docs/module_map.md`: new row; `irrlicht_renderer.cpp` 1073 -> 1077.
  - "four TUs" sentences in `CLAUDE.md` and `src/client/graphics/CLAUDE.md`.
- `include/client/graphics/renderer_zone_merge.h`: comment-only correction.
  The header is in territory, but the edit makes a wide rebuild.

### Verification

| step | result |
|---|---|
| 1 `cmake --build build -j24` | PASS (exit 0) |
| 2 `ctest -j6` | 4,702/4,703; the one red was `DocCounts` (module_map row), fixed, then 17/17 re-run green (DocCounts, FileSizeCeiling, all new + T413 + cursor census). Golden tests (G03 registration, G05 frame render, G06 movement, zone dispatch, app init) green in the full run; no G-pinned TU was touched |
| 3 `build-gles2-x86.sh` | PASS (exit 0) |
| 4 visual gate | PASS (`=== VISUAL GATE: PASS ===`, qeynos2/gfaydark/befallen); `git status` shows no file under `tests/goldens/` changed |
| 5 `build-arm-noble.sh` | PASS (exit 0, `build-arm-noble/bin/willeq` rebuilt) |
| 6 file sizes (`--files`, 9 touched) | 9 ok, 0 warn, 0 fail |
