# K16 — Narrow `InitGraphics` to a two-method renderer interface (B006, Will-approved 2026-09-03)

**Runs ALONE after L05 merges** (it edits the `class IrrlichtRenderer`
declaration line, which every L unit conflicts on).

## Measured at `d6cbf730`

`EverQuest::InitGraphics(int, int, IrrlichtRenderer*, GameStateBridge*)` is
a sanctioned forwarder to `BridgeComponent::InitGraphics`
(`src/client/bridge/bridge_component.cpp`). K15 read the body: the renderer
pointer reaches exactly `renderer->initLoadingScreen(config)` (returns
`bool`, takes `const RendererConfig&`) and `renderer->initDeferredGraphics()`
(void), is not stored, not passed on, not compared. Re-measure at your tip.

## Steps

1. `include/client/graphics/zone_load_surface.h` (new, ~25 lines):
   `namespace EQT::Graphics { class IZoneLoadSurface { public: virtual
   ~IZoneLoadSurface() = default; virtual bool initLoadingScreen(const
   RendererConfig&) = 0; virtual void initDeferredGraphics() = 0; }; }`.
   `RendererConfig` comes from `renderer_types.h` (not the 1,8xx-line
   renderer header).
2. `IrrlichtRenderer` inherits `IZoneLoadSurface` (in addition to what it
   inherits today — read the class line); its two existing public methods
   gain `override`. No body changes.
3. `EverQuest::InitGraphics` and `BridgeComponent::InitGraphics` take
   `EQT::Graphics::IZoneLoadSurface*`. `include/client/eq.h` and
   `include/client/bridge/bridge_component.h` drop the `IrrlichtRenderer`
   forward declaration if nothing else needs it (measure; K15 oo — the
   compiler decides). `application.cpp`'s call site passes the same
   pointer; it converts implicitly.
4. Test: `tests/test_zone_load_surface.cpp` — a stub `IZoneLoadSurface`
   whose `initLoadingScreen` records the config and returns true; construct
   `EverQuest` via `zonetest::MakeEq()`, call `InitGraphics(w, h, &stub,
   bridge)`, assert both methods were called in order. This proves the
   narrowing is complete: the body compiled against the interface alone.
5. Board: strike B006 (both halves — record the asset-loader namespace
   half as "naming only, left").

## Territory

`include/client/graphics/zone_load_surface.h` (new),
`include/client/graphics/irrlicht_renderer.h` (class line + two `override`s),
`include/client/eq.h`, `include/client/bridge/bridge_component.h`,
`src/client/bridge/bridge_component.cpp`, `src/client/core/everquest.cpp`
(the forwarder signature), `src/client/application.cpp` (if the call site
needs a cast — it should not), `tests/test_zone_load_surface.cpp` + cmake,
`refactor/BOARD.md`, `docs/module_map.md`. Visual gate + ARM NOT skippable
(`irrlicht_renderer.h` changes).

## STOP

- The body dereferences the renderer for anything beyond the two calls at
  your tip → STOP with the third call named.
