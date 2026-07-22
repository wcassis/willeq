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

**Cluster boundaries (verified by grep of `IrrlichtRenderer::` definitions,
irrlicht_renderer.cpp @ merge-base commit c3e926272ac9e401b474744156ca4eae74255ec7
with `fable`, 13,679 lines before this branch):**

- Fire-glow icosphere cluster (pre-move lines 13202–13545, 344 lines): entire
  block wrapped in a single `#ifdef EQT_HAS_DRM` / `#endif` guard (preserved
  verbatim), header comment "Fire Glow: icosphere mesh + shader + rendering".
  4 `IrrlichtRenderer::` methods:
  - `buildIcosphereMesh()`
  - `compileIcosphereShader()`
  - `updateFireGlowLighting()`
  - `renderFireGlowIcospheres()`

  The 4 fallback `#define` guards for older GLES2 headers
  (`GL_BLEND_SRC_RGB`, `GL_BLEND_DST_RGB`, `GL_DEPTH_WRITEMASK`,
  `GL_CURRENT_PROGRAM`) at the top of the cluster moved with it, unmodified.

- EQE1 archive loading cluster (pre-move lines 13547–13676, 130 lines):
  immediately follows the fire-glow cluster's `#endif // EQT_HAS_DRM`, no
  guard of its own. 3 `IrrlichtRenderer::` methods:
  - `getOrOpenEQE1(const std::string&)`
  - `loadEQE1ArchiveConfig(const std::string&)`
  - `resolveEQE1Archives(const std::string&) const`

**Left in place, confirmed out of scope:** `setEntityWeaponSkills`,
`getEntityPrimaryWeaponSkill`/`getEntitySecondaryWeaponSkill`,
`queueCombatAnimation`, `hasEntityPendingCombatAnims`,
`queueReceivedDamageAnimation`, `queueSkillAnimation`,
`triggerFirstPersonAttack` (pre-move lines 13145–13200) — the
combat-animation queuing family immediately preceding the fire-glow
cluster, explicitly excluded by the plan.

**Guard handling:** the fire-glow cluster's single `#ifdef EQT_HAS_DRM` /
`#endif` pair moved intact around all 4 methods, exactly as in the
original. No nested guards inside this cluster (unlike F14/F15's
diagnostics/memory clusters). The EQE1 cluster has no guards.

**Namespace wrapper:** both clusters needed the `namespace EQT { namespace
Graphics { ... } }` wrapper that irrlicht_renderer.cpp opens once at file
scope (was line 103–104) and closes at EOF — same pattern as F14/F15, not a
content change.

**Includes added to renderer_effects.cpp (new file, not in the moved
range):** `irrlicht_renderer.h`, `eqe1_loader.h` (redundant full include —
already pulled in transitively via `irrlicht_renderer.h`, kept explicit for
clarity per F14/F15 style), `common/logging.h`, `<json/json.h>`,
`<fstream>`, `<algorithm>`, `<cmath>`, `<mutex>`, plus the same conditional
GL header block the original TU carries at file scope for the
`EQT_HAS_DRM`/`EQT_HAS_GLES2` fire-glow GL calls:
```
#ifdef EQT_HAS_DRM
#ifdef EQT_HAS_GLES2
#include <GLES2/gl2.h>
#include <EGL/eglext.h>
#else
#include <GL/gl.h>
#endif
#include <EGL/egl.h>
#endif
```
All member fields used by the moved methods (`zoneShader_`, `camera_`,
`icosphereVBO_`/`icosphereIBO_`/`icosphereProgram_`, `icoLoc*_` uniform
caches, `fireGlowLights_`, `maxFireGlowLights_`, `fireGlowTime_`,
`captured3DView_`/`captured3DProj_`, `eqe1Mutex_`, `eqe1Loaders_`,
`eqe1ArchiveEntries_`, `constrainedTextureCache_`, `config_`) and the
`EQE1ArchiveEntry` nested type are declared directly in
`irrlicht_renderer.h` with full (non-forward-declared) types for
`ZoneShaderManager` and `EQE1Loader` — no additional forward-declaration
includes were required beyond the GL header block above.

**File-local static helpers:** none in either cluster (both `VS_SRC`/`FS_SRC`
shader-source strings inside `compileIcosphereShader` are function-local
`static const char*`, moved as-is with the function).

**CMakeLists.txt:** added `src/client/graphics/renderer_effects.cpp` to
`GRAPHICS_SOURCES`, immediately after `renderer_overlays.cpp`.

**Line counts:** `irrlicht_renderer.cpp` 13,679 → 13,203 lines (−476).
`renderer_effects.cpp`: 500 lines (new).

**Verification:**
- `cmake --build build -j24` — clean, no warnings/errors in either touched
  file.
- `ctest --test-dir build` — 1191/1192 passed; the one failure
  (`AudioManagerTest.PreloadCommonSounds`) is the documented pre-existing
  ALSA expected-fail in `refactor/test_baseline.txt`. 9 tests skipped
  (asset/graphics-dependent, pre-existing, unrelated).
- `python3 scripts/refactor/verify_move_only.py c3e926272ac9e401b474744156ca4eae74255ec7..HEAD`
  — reports 1 substantive added line: the `#else` in the conditional GL
  header block above (`CMakeLists.txt` change reported separately as
  "non-C++, not verified" — expected for a build-file edit). This is not a
  logic change: it's an unavoidable duplicate of the file-scope conditional
  include block irrlicht_renderer.cpp already carries once for the whole TU
  (shared by RDP/GLES2/etc. code outside this cluster), now needed locally
  in the new TU so the fire-glow cluster's raw GL calls (`glGenBuffers`,
  `glCreateShader`, etc.) resolve. Same category of necessary deviation as
  F14's "3 additional includes... for full type definitions" and F15's
  namespace-wrapper replication — documented here per the plan's "PASS or
  documented de-static-ing" allowance (this is a documented necessary-include
  deviation, not a de-static-ing, but the same sign-off principle applies).
- `bash scripts/refactor/check_file_sizes.sh --files renderer_effects.cpp`
  — `ok` (500 lines, under the 1,500-line ceiling).

**Deviations:** none in moved code itself (verbatim, no reformatting, no
scope creep). The only reported diff beyond a pure cut/paste is the
necessary duplicated `#ifdef EQT_HAS_DRM / EQT_HAS_GLES2 / #else / #endif`
GL header include block in the new file, explained above.
