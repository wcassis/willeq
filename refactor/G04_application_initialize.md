# G04 — `Application::initialize`: one sequence of subsystem steps becomes one `bool` step per subsystem

Batch: `refactor/G_oversize_methods.md` (G01 corrections 1–12). Two-wide
with G05 (disjoint: `src/client/application.cpp` vs `src/client/graphics/frame/`).
Measured at `810d699f`: `Application::initialize` in `src/client/application.cpp`
(~569 lines), sequential init with early `return false`s. Re-measure
mechanically: list every early return and the step boundaries the
existing banners/comments mark.

## Shape

1. Each subsystem block becomes `bool Application::initX()` (config,
   logging, database, audio, renderer/loading screen, game client, bridge
   wiring, RDP, threads…) — verbatim slice; its early `return false`
   stays inside the step; `initialize()` becomes the ordered list
   `if (!initX()) return false;`. Locals that cross steps become private
   members or are passed explicitly (record each; STOP if a lifetime
   changes).
2. Order is load-bearing (the bridge must exist before the game client;
   the renderer before `InitGraphics`) — same discipline as G03: no call
   crosses a cut; an order test pins the step sequence by parsing the TU
   (G03 correction 9: measure what the container preserves — here there
   is no container, the TU IS the order), golden extracted BEFORE the
   change, size asserted, faults planted in code (swap two step calls;
   drop one).
3. Proof: line multiset; the driving tests (`test_zoning_graphics_integration`,
   `test_headless_shutdown`, whatever names `Application::initialize` or
   constructs an `Application` — grep) pass with assertions untouched.

Predicted: `initialize` 569 → ≈ 20–30 lines; `application.h` gains ≈ 10
private declarations.

STOP: a cross-step local whose lifetime must change; a driving test's
assertion; an early-return whose semantics differ once wrapped (e.g. a
partial-init cleanup that ran after the return point — read the
shutdown path first).

Territory: `src/client/application.cpp`, `include/client/application.h`,
the new test + golden + cmake, `docs/module_map.md`. No graphics files.
