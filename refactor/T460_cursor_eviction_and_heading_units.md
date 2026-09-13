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

(executor fills)
