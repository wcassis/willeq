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

## Review

Executed at tip `1ea4a59c` (branch `task/K16-initgraphics-interface`, from
`fable`). No STOP condition hit.

### The measured body

Re-read `BridgeComponent::InitGraphics` in
`src/client/bridge/bridge_component.cpp` at this tip. The `renderer` parameter
appears exactly three times in the body and nowhere else:

- `if (!renderer->initLoadingScreen(config))` — on failure it logs, clears
  `ctx_.bridge` and returns false;
- `if (renderer)` — a null test, no dereference;
- `renderer->initDeferredGraphics()` inside that test.

It is not stored in a member, not passed to any callee, not compared against
another pointer, and not cast. K15's measurement held; the narrowing is exact
and there is no third call.

### The diff

- **new** `include/client/graphics/zone_load_surface.h` (37 lines) —
  `EQT::Graphics::IZoneLoadSurface`: virtual dtor,
  `virtual bool initLoadingScreen(const RendererConfig&) = 0`,
  `virtual void initDeferredGraphics() = 0`. It includes
  `client/graphics/renderer_types.h` for `RendererConfig` (J17's small
  free-standing types header), NOT the umbrella renderer header. A forward
  declaration of `RendererConfig` would have compiled, but implementers and
  the test stub both need the complete type, and the plan named
  `renderer_types.h` as the source.
- `include/client/graphics/irrlicht_renderer.h` — includes the new header;
  `class IrrlichtRenderer : public IZoneLoadSurface`. **The class had no base
  class before, and this is now its only one.** `initLoadingScreen` and
  `initDeferredGraphics` gained `override`; both signatures already matched
  exactly, so no body, no call site and no member changed. The class does gain
  a vtable and an implicitly-virtual destructor.
- `include/client/eq.h`, `include/client/bridge/bridge_component.h` — the
  `InitGraphics` parameter is `EQT::Graphics::IZoneLoadSurface*`.
- `src/client/bridge/bridge_component.cpp` — signature, plus an explicit
  `#include "client/graphics/zone_load_surface.h"` so the narrowed parameter
  does not depend on the umbrella header being included for other reasons
  (it still is, for the rest of the TU).
- `src/client/core/everquest.cpp` — the forwarder's signature. Its body was
  already a pure forward and needed no include.
- `src/client/application.cpp` — **unchanged**. `m_renderer.get()` converts
  derived-to-base implicitly, as the plan predicted; no cast was needed.
- **new** `tests/test_zone_load_surface.cpp` + its target in
  `tests/cmake/packets_skills.cmake` (next to `test_bridge_component`,
  graphics-gated).
- `scripts/refactor/file_size_allowlist.txt` — `irrlicht_renderer.h` ratcheted
  1659 → 1661.

### Did the forward declarations leave?

**Yes, from both headers, and the compiler is the witness.** `include/client/eq.h`
and `include/client/bridge/bridge_component.h` each declared
`class IrrlichtRenderer;` solely for this parameter; both now declare
`class IZoneLoadSurface;` instead, and the full build is green. The token
`IrrlichtRenderer` survives in `eq.h` only inside a comment. K15's warning —
that a name-token grep cannot see a use through a typed member pointer — did
not bite here, because neither header holds a renderer member.

### Header counts

| file | before | after |
| --- | --- | --- |
| `include/client/eq.h` | 899 | 899 |
| `include/client/graphics/irrlicht_renderer.h` | 1,659 | 1,661 |

`eq.h` did not shrink: the forward declaration was one line and its
replacement is one line. It did not grow either, so its allowlist entry (899,
"may not grow") is untouched. `irrlicht_renderer.h` grew by exactly two — the
new include and one comment line — and its entry was ratcheted to 1,661.

### Test

`tests/test_zone_load_surface.cpp`, 3 tests, all pass. A
`StubZoneLoadSurface` implements only the two pure virtuals and records the
call ORDER (a pair of booleans would not catch a swap). The tests drive the
REAL `EverQuest::InitGraphics` — the production body is the code under test,
which is what makes a test-only stub load-bearing here:

- `InitGraphicsCallsBothSurfaceMethodsInOrder` — exactly two calls,
  `initLoadingScreen` then `initDeferredGraphics`, and InitGraphics returns
  true. That the body compiles and completes against a stub with no other
  members is the proof the narrowing is complete.
- `InitGraphicsPassesTheConfigItBuilt` — width/height/eqClientPath arrive in
  the `RendererConfig` the game side built.
- `LoadingScreenFailureStopsBeforeDeferredInit` — a false return stops the
  sequence: one call only, and InitGraphics fails.

Audio is disabled in the fixture via `eq.Bridge().SetAudioEnabled(false)`;
InitGraphics ends with `InitializeAudio()` and this test is about the renderer
surface, not ALSA.

### Steps

| step | result |
| --- | --- |
| `cmake --build build -j24` | **exit 0** |
| `ctest -j6` | **3655 / 3656**, sole failure `AudioManagerTest.PreloadCommonSounds` (the sanctioned ALSA one). 3 new tests included; `visual_gate` passed inside the run (26.11 s) |
| `scripts/build-gles2-x86.sh` | **exit 0** |
| `scripts/ci/visual_gate.sh` standalone | **PASS**, no golden in `git status --porcelain` |
| `scripts/build-arm-noble.sh` | **exit 0** |
| `scripts/refactor/check_file_sizes.sh --all` | **exit 0** — 846 files, 0 warn, **0 FAIL** |

### The trap this unit hit — a timed-out build leaves TRUNCATED binaries

The first `cmake --build -j24` was killed by the tool timeout (exit 143). The
immediate re-run reported **exit 0 in seconds** and looked clean. The first
full `ctest` then failed `HeadlessShutdownTest.PipedQuitWithNoServerExitsClean`
and `...NonLifecycleCommandIsHeldNotDroppedOrApplied` with *empty process
output*, which reads exactly like a client regression.

It was not. `build/bin/willeq` was **0 bytes**, and so were 21 other binaries —
links interrupted mid-write. Their mtimes were newer than their inputs, so
make considered every one of them up to date and the "successful" rebuild
relinked nothing. That also silently shrank the suite: 3,252 tests discovered
instead of 3,656, because 22 test binaries could list nothing.

`find build/bin build/lib -size 0 -type f -delete` followed by a rebuild fixed
it; the headless tests then passed and the count came back to 3,656. This is
the concrete failure mode behind the protocol's "after any exit-143, assert
expected artifacts exist, then validate `build/bin` and `build/lib`" rule —
the zero-byte check is the assertion that matters, and a green exit code from
the follow-up build does **not** substitute for it.

### Board

B006 struck on `refactor/BOARD.md` — closed. Both halves recorded: (1) the
`InitGraphics` narrowing, done here; (2) the asset loaders in the
`EQT::Graphics` namespace, marked **naming only and left deliberately** — the
namespace is a label, not a dependency, and renaming every use site buys
nothing structural. `CLAUDE.md`'s "remaining known seams" paragraph, which
named this seam, was rewritten to match. `docs/module_map.md` gained the
`IZoneLoadSurface` note under the renderer section and
`bridge_component.cpp`'s row went 842 → 846.
