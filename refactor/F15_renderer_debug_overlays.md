# F15 — Split debug toggles and overlays out of irrlicht_renderer.cpp

Lift the debug-toggle cluster and the overlay/targeting cluster out of
`src/client/graphics/irrlicht_renderer.cpp` (15,243 lines after F14).
Pure move — no behavior change, no interface change, no reformatting.

## Scope (original refs @ a0c02cd; F14 removed 1,151 lines so positions have
shifted. Enumerate by NAME.)

Two new files under `src/client/graphics/`:

**renderer_debug.cpp** — the debug toggle cluster (orig ~10901–11533):
every `toggle*` / `cycle*` debug method (~30 of them) plus any contiguous
small debug setters in that block. **Explicitly NOT in scope:** `run()` and
screenshot capture — they are main-loop/user functionality living in the same
region; leave them in irrlicht_renderer.cpp.

**renderer_overlays.cpp** — the overlay + targeting cluster (orig
~14054–15295): map overlay, navmesh overlay, target outline/selection
rendering, zone-line overlay, FPS display, and the targeting methods
(`setCurrentTarget`, `handleMouseTargeting`, `getEntityAtScreenPos`,
`checkEntityLOS`). Enumerate every `IrrlichtRenderer::` definition in the
contiguous range by grep and list them in the review; anything in-range that
is clearly NOT overlay/targeting (judgment call) stays put, documented.

## Steps

1. Locate boundaries by grep of `IrrlichtRenderer::` definitions; watch for
   return types that break simple regexes (F14 hit this with `int64_t`).
2. Preserve any `#ifdef` feature guards exactly (EQT_HAS_GLES2 / EQT_HAS_DRM
   nesting occurs in this file).
3. Both files need the `namespace EQT { namespace Graphics { ... } }` wrapper
   (the original TU opens it once at file scope) and full-type includes for
   classes that irrlicht_renderer.h only forward-declares — F14's review
   documents this pattern.
4. File-local `static` helpers: move if exclusively used by moved code, else
   leave and document.
5. Add both files to `GRAPHICS_SOURCES` in CMakeLists.txt.

## Verification

- `cmake --build build -j24` clean.
- `ctest --test-dir build` — green modulo the two documented exceptions in
  refactor/test_baseline.txt (RDP flake: re-run isolated; ALSA
  AudioManagerTest.PreloadCommonSounds: expected-fail on this host).
- `python3 scripts/refactor/verify_move_only.py fable..HEAD` — PASS or
  documented de-static-ing only.
- `bash scripts/refactor/check_file_sizes.sh --files <abs>/renderer_debug.cpp <abs>/renderer_overlays.cpp` — ok (overlays ~1,300 expected, may WARN if larger; must not FAIL).
- irrlicht_renderer.cpp shrinks by ~1,800+ lines.

## Review

(executor fills in)
