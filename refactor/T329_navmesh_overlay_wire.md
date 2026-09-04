# T329 — Wire the `NavmeshChanged` bridge arm so the navmesh overlay can draw (B041)

Class A, found by L03. Small. Runs in slot2 alongside L04 (slot3) and T328
(slot1); L04 edits `irrlicht_bridge.cpp` at other lines (`dumpScene`,
`getMemoryReport` call sites) — touch only the `NavmeshChanged` arm.

## Measured at `7d2efe6d`

- `src/client/bridge/irrlicht_bridge.cpp` `IrrlichtBridge::applyEvent`,
  `case state::GameEventType::NavmeshChanged:` — body is one `LOG_TRACE`.
- Payload: `state::NavmeshChangedData` (`include/client/state/event_bus.h`)
  carries the navmesh pointer (`PathfinderNavmesh*`; `{nullptr}` on zone
  leave, the real pointer on load — `src/client/movement/movement_state.cpp`
  publishes both; `src/client/zone/zone_component.cpp` publishes one).
- Sink: `IrrlichtRenderer::setNavmesh(PathfinderNavmesh*)` (inline
  forwarder to `OverlaysComponent::setNavmesh`), zero callers today.
- Consumer: `OverlaysComponent::updateNavmeshOverlay()` returns
  immediately when the pointer is null; Ctrl+N toggles `toggleNavmeshOverlay`.

## Steps

1. The arm: read the payload the way the neighbouring arms read theirs
   (`std::get<…>` / the variant accessor the file uses) and call
   `renderer_->setNavmesh(data.navmesh)`; keep the trace line. Nullptr on
   zone leave is correct and must pass through.
2. Test: `tests/test_navmesh_overlay_wire.cpp` (register next to the
   existing bridge tests — grep `tests/cmake/` for `test_irrlicht_bridge`
   or the nearest `IrrlichtBridge` test and copy its shape): push a
   `NavmeshChanged` event with a non-null `PathfinderNavmesh*` (a stub or a
   default-constructed one if constructible) through `applyEvent`, assert
   the overlays component's navmesh pointer is that value (add a
   `const PathfinderNavmesh* navmesh() const` getter on `OverlaysComponent`
   if none exists — one line); then push `{nullptr}` and assert it clears.
   Also extend `tests/test_reachability_sweep.cpp`'s consumer-arm scan if
   it classifies `LOG_TRACE`-only arms — read it; if it already lists this
   arm as a known no-op, remove it from that list (the assertion gets
   stricter, not weaker).
3. Live proof (owed, not blocking merge): a screenshot of Ctrl+N in a
   navmesh-enabled zone goes to `review/T329/` with a README — ONLY if the
   eqemu lock is free (T328 Leg A holds it now; if held, say so and leave
   the live leg on the board as owed).
4. BOARD: strike B041 with the commit; if the live leg is owed, say so in
   the strike.

## Territory

`src/client/bridge/irrlicht_bridge.cpp` (one arm), `include/client/graphics/overlays/overlays_component.h`
(+ one getter if needed), `tests/test_navmesh_overlay_wire.cpp` + its cmake,
`tests/test_reachability_sweep.cpp` (list edit only, if applicable),
`refactor/BOARD.md`, `review/T329/`. Nothing under `src/client/graphics` other
than the header getter — if that header changes, run the visual gate and ARM
standalone.

## Verification

Build, full ctest -j6 (ALSA quirk only), `check_file_sizes.sh --all`; visual
gate + ARM only if a graphics header changed.

## Review

Measured against `7e929876`.

### Diff

- `src/client/bridge/irrlicht_bridge.cpp`, `IrrlichtBridge::applyEvent`,
  `case state::GameEventType::NavmeshChanged:` — now, guarded on
  `renderer_`, reads `d.navmesh` off `state::NavmeshChangedData` via
  `std::get<...>(event.data)` (same pattern as the neighbouring
  `SpellCastVisualInterrupted` arm) and calls
  `renderer_->setNavmesh(d.navmesh)`. The `LOG_TRACE` line is kept. A
  null payload (zone leave) passes straight through — `setNavmesh` is a
  plain store, only guarded on `IrrlichtRenderer::isLoading()`.
- `include/client/graphics/overlays/overlays_component.h` — added
  `const PathfinderNavmesh* navmesh() const { return navmesh_; }` next to
  `setNavmesh`, the one-line getter the contract allowed since nothing
  else could observe the stored pointer.
- `tests/test_navmesh_overlay_wire.cpp` (new) — `NavmeshOverlayWireTest.
  NavmeshChangedForwardsThePointerToTheOverlaysComponent`: a bare,
  never-`Init()`'d `IrrlichtRenderer` (same headless idiom as
  `test_overlays_component.cpp`) plus a real `eqt::bridge::IrrlichtBridge`
  wired to it via `setRenderer`. Pushes a `NavmeshChanged` event carrying a
  sentinel `PathfinderNavmesh*` (`reinterpret_cast<PathfinderNavmesh*>(0x1)`
  — never dereferenced along this path, same idiom as
  `test_rdp_component.cpp`'s `ConstrainedTextureCache*` sentinel, so no
  real Detour-backed navmesh needed) through `applyEvent`, asserts
  `renderer.Overlays().navmesh()` is that pointer, then pushes `{nullptr}`
  and asserts it clears.
- `tests/cmake/renderer_guards.cmake` — registered `test_navmesh_overlay_wire`
  next to `test_overlays_component` (same `IF(EQT_GRAPHICS AND
  IRRLICHT_LIBRARY)` guard, links `willeq_core` for `irrlicht_bridge.cpp` +
  `overlays_component.cpp`).
- `tests/test_reachability_sweep.cpp` — checked; it does not list
  `NavmeshChanged` (or any per-arm LOG_TRACE classification) at all, so
  there was nothing to strike there.

### Verification

- `cmake --build build -j24` — **exit 0** (one fix cycle: the first attempt
  used bare `state::` in the new test instead of `eqt::state::` — the
  namespace `IrrlichtBridge` itself sits inside, `eqt::bridge`, resolves
  `state::` implicitly via the enclosing `eqt` namespace, but a free
  function outside it needs the qualified `eqt::state::` name. Fixed and
  rebuilt clean.)
- `ctest --test-dir build -j6 --output-on-failure` — **3629/3630 passed.**
  First run also failed `HeadlessShutdownTest.
  NonLifecycleCommandIsHeldNotDroppedOrApplied`; re-ran it standalone
  (`--gtest_repeat=5`) and it passed all 5 times at a steady ~5.05s each —
  a `-j6` contention flake on a timing-sensitive test, not a regression
  from this change. A second full-suite re-run reproduced only the
  expected `AudioManagerTest.PreloadCommonSounds` (ALSA) failure.
  `test_navmesh_overlay_wire` itself passes standalone and in the suite.
- `check_file_sizes.sh --all` — **839 checked, 0 warn, 0 fail.**
- Header changed under `include/client/graphics`, so per the contract:
  - `scripts/build-gles2-x86.sh` — exit 0.
  - `scripts/ci/visual_gate.sh` — **PASS**, no goldens touched.
  - `scripts/build-arm-noble.sh` — exit 0.

### Live leg — OWED

The eqemu lock was held by T328 for this unit's entire run
(`/mnt/projects/fable/.eqemu-lock` → `T328 2026-09-03`, checked before and
after all other work). No live screenshot of Ctrl+N in a navmesh-enabled
zone was captured. `review/T329/` was not created. This leg is left open
on the board (see BOARD.md strike below) for a follow-up unit once the
server is free.

### Deviations

None from the plan besides the namespace fix above, which is a correction,
not a scope change.

---

## Addendum 2026-09-04 — the owed live leg was captured by T328 Leg B

The eqemu lock was free after T328 Leg A finished, so T328 Leg B took it and
ran the live proof this unit owed. Deliverable:
**`/mnt/projects/fable/review/T329/`** (two screenshots, both full client logs,
the capture driver, and the input probe).

**Result: this unit's wire is proven. The draw is not.**

Confirmed in the client's own log, in a `qeynos2` session with
`/mnt/projects/maps/nav/qeynos2.nav` loaded:

```
[INFO ] [MAP]      Loaded Navmesh V[2] file [/mnt/projects/maps/nav/qeynos2.nav]
[DEBUG] [INPUT]    Key pressed: N (ctrl=true, ...), action=ToggleNavmeshOverlay
[INFO ] [GRAPHICS] Navmesh overlay: ON
[DEBUG] [GRAPHICS] Navmesh overlay: Updated with 112 triangles
```

That last line is the one that closes this unit: `updateNavmeshOverlay()`
returns immediately when `navmesh_` is null — the exact null pointer B041 was
about — and instead it reached `GetTrianglesInRadius()` and got 112 triangles.

But the screenshot shows **no navmesh lines at all, and the entire 2D UI gone**
(health/mana/stamina bars, FPS, chat panel, hotbar), against an identical
overlay-OFF baseline in the same spot at the same forced daylight. Reproduced
in two grabs from the same session. That is `drawNavmeshOverlay()`, not this
unit's arm, and it is under `src/client/graphics` (L05's territory this wave),
so it was **boarded as B045 and not touched**. B041's live-proof note is struck
with that split recorded.

Two input findings from the capture, written down so they are not rediscovered:
`xdotool --window` (XSendEvent) delivers **nothing** to this client, and after
the in-game chat sequence **no further keystroke reaches it at all** — so a
capture needing a keypress must send it before `/say #set time 11 0`, and one
overlay state per session. Both are measured, with the probe, in
`review/T329/README.md`.
