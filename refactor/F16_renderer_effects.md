# F16 — Split fire-glow effects and EQE1 loading out of irrlicht_renderer.cpp

Lift the last two EOF-region clusters (both flagged left-behind in F15's
review) out of `src/client/graphics/irrlicht_renderer.cpp` (13,679 lines).
Pure move — no behavior change, no interface change, no reformatting.

## Scope (enumerate by NAME; both clusters sit near end of file)

Move to new `src/client/graphics/renderer_effects.cpp`:
- **Fire-glow icosphere cluster** (~330 lines): `buildIcosphereMesh`,
  `compileIcosphereShader`, `updateFireGlowLighting`,
  `renderFireGlowIcospheres` (plus any contiguous fire-glow helpers; F15's
  review notes the cluster runs from `setEntityWeaponSkills`-adjacent code —
  combat-anim queuing methods just before it are NOT in scope).
- **EQE1 archive loading cluster** (~130 lines): `getOrOpenEQE1`,
  `loadEQE1ArchiveConfig`, `resolveEQE1Archives`. (Naming note: EQE1 loading
  is asset plumbing, not an effect — it rides along in this file per the
  batch table to avoid a 130-line TU; a future re-home is fine.)

Combat-animation queuing methods (setEntityWeaponSkills, queueCombatAnimation
family) between/around these clusters stay in irrlicht_renderer.cpp.

## Steps

1. Locate boundaries by grep of `IrrlichtRenderer::` definitions near EOF;
   watch for `#ifdef` guards (EQT_HAS_DRM / EQT_HAS_GLES2 likely around
   icosphere shader code) — preserve exactly.
2. New file needs the `namespace EQT { namespace Graphics { ... } }` wrapper
   and full-type includes for anything irrlicht_renderer.h only
   forward-declares (see F14/F15 reviews for the pattern).
3. File-local static helpers: usual rule.
4. Add to `GRAPHICS_SOURCES` in CMakeLists.txt.

## Verification

- `cmake --build build -j24` clean.
- `ctest --test-dir build` — green modulo the documented exceptions in
  refactor/test_baseline.txt (ALSA expected-fail; RDP flake → re-run isolated).
- `python3 scripts/refactor/verify_move_only.py BASE..HEAD` — PASS or
  documented de-static-ing (BASE = merge-base with fable if it advanced).
- `bash scripts/refactor/check_file_sizes.sh --files <abs>/renderer_effects.cpp` — ok.
- irrlicht_renderer.cpp shrinks by ~450+ lines.

## Review

(executor fills in)
