# F14 — Split RDP and diagnostics out of irrlicht_renderer.cpp

First renderer-side unit: lift the two cleanest clusters out of
`src/client/graphics/irrlicht_renderer.cpp` (16,394 lines). Pure move — no
behavior change, no interface change, no reformatting of moved code.

## Scope (line refs @ commit a0c02cd — irrlicht_renderer.cpp untouched since;
verify by method name)

Two new files under `src/client/graphics/`:

**renderer_rdp.cpp** — the RDP remote-desktop cluster (~15726–15936, ~210
lines): `initRDP`, `startRDPServer`, `stopRDPServer`, `captureFrameForRDP`,
`handleRDPKeyboard`, `handleRDPMouse` (confirm exact set by grep — every
`IrrlichtRenderer::` method in that range).

**renderer_diagnostics.cpp** — the diagnostics clusters:
- dumpScene / runSceneProfile / profileSceneBreakdown / measureSection
  (~9055–9341, ~285 lines)
- frame-timing profiling: logFrameTimings and neighbors (~11533–11834, ~300)
- `getMemoryReport` (~15365–15725, ~360)

## Steps

1. Locate exact method boundaries by grep (`IrrlichtRenderer::` definitions
   in the listed ranges). List the full method set in the review.
2. Watch for `#ifdef` guards spanning the clusters (RDP code is likely inside
   an RDP feature guard) — preserve guards exactly in the new files.
3. Create both files: brief header comment + include block (start from
   irrlicht_renderer.cpp's includes, prune compiler-driven).
4. File-local `static` helpers / anonymous-namespace symbols referenced by
   moved code: move if used only by moved code; otherwise leave and document
   (de-static via a shared internal header only if unavoidable).
5. Add both files to the same source list as irrlicht_renderer.cpp in
   CMakeLists.txt (respect any graphics/RDP conditional grouping).

## Verification

- `cmake --build build -j24` clean.
- `ctest --test-dir build` — fully green. NOTE: this unit touches RDP code and
  RDPServerFrameTest.UpdateFrameMultipleTimes is a known rare flake
  (pre-existing segfault under parallel load). If it fails, re-run it in
  isolation several times; it must pass consistently in isolation.
- `python3 scripts/refactor/verify_move_only.py fable..HEAD` — PASS, or
  substantive lines limited to documented de-static-ing.
- `bash scripts/refactor/check_file_sizes.sh --files src/client/graphics/renderer_rdp.cpp src/client/graphics/renderer_diagnostics.cpp` — ok.
- irrlicht_renderer.cpp shrinks by ~1,100+ lines.

## Review

**Cluster boundaries (verified by grep of `IrrlichtRenderer::` definitions,
irrlicht_renderer.cpp @ commit a0c02cd state, confirmed unchanged before this
branch):**

- `renderer_rdp.cpp`: lines 15720–15915 of the original file — comment header
  "RDP Server Integration", the whole block wrapped in a single
  `#ifdef WITH_RDP` / `#endif` guard (preserved verbatim). 8 methods, all
  `IrrlichtRenderer::`:
  - `initRDP(uint16_t)`
  - `startRDPServer()`
  - `stopRDPServer()`
  - `isRDPRunning() const`
  - `getRDPClientCount() const`
  - `captureFrameForRDP()`
  - `handleRDPKeyboard(uint16_t, uint8_t)`
  - `handleRDPMouse(uint16_t, uint16_t, uint16_t)`

  (`isRDPRunning`/`getRDPClientCount` weren't named in the plan's prose but
  are part of the same contiguous guarded cluster — included per the plan's
  "confirm exact set by grep" instruction.)

- `renderer_diagnostics.cpp`: three clusters, concatenated in file order:
  1. Scene dump (orig lines 9055–9340): `dumpScene() const` and
     `measureSection()`. `measureSection` wasn't caught by the first grep
     pass (its `int64_t` return type broke the regex character class) but
     was located by reading the surrounding lines and confirmed as part of
     the same contiguous block, immediately before `processFrame`.
  2. Frame-timing profiling (orig lines 11533–11831): `logFrameTimings()`,
     `runSceneProfile()`, `profileSceneBreakdown()`. `setFrameTimingEnabled`
     (just above, at 11510) was deliberately left in irrlicht_renderer.cpp —
     it's outside the plan's stated ~11533–11834 range and is a simple
     setter, not a diagnostics-cluster method.
  3. Memory report (orig lines 15353–15718): the `formatBytes(size_t)`
     file-local `static` helper (used exclusively by `getMemoryReport`, ~30
     call sites, all within the moved range) and
     `getMemoryReport(const MemoryReportInput&) const`.

**Guard handling:** the RDP file's entire body sits inside one
`#ifdef WITH_RDP` / `#endif` pair, moved intact. A nested `#ifdef
EQT_HAS_DRM` inside `dumpScene` (icosphereProgram_ diagnostic field) and
nested `#ifdef EQT_HAS_GLES2` blocks inside `getMemoryReport` were preserved
exactly, unmodified, in their original positions.

**Shared-helper analysis:**
- `formatBytes` (static, file-local) — used only by `getMemoryReport`; moved
  together with it into `renderer_diagnostics.cpp`. No de-static-ing needed.
- RDP helpers `rdpMouseFlagsToIrrlicht`, `rdpGetWheelDelta`,
  `rdpScancodeToIrrlicht`, `rdpScancodeToChar` are declared in
  `client/graphics/rdp/rdp_input_handler.h` (not file-local to
  irrlicht_renderer.cpp) — the new file includes that header under the same
  `#ifdef WITH_RDP` guard the original used; no code changes needed.
- Both `IrrlichtRenderer::` method sets needed to be wrapped in the
  `namespace EQT { namespace Graphics { ... } }` block that
  irrlicht_renderer.cpp opens once at file scope (line 103–104) and closes at
  EOF — this isn't a content change, just replicating the enclosing
  namespace the original TU already had. `renderer_diagnostics.cpp` also
  needed 3 additional includes (`constrained_mesh_cache.h`,
  `weather_effects_controller.h`, `sky_renderer.h`) for full type
  definitions — the original TU had these as full includes too (lines
  10–25), but `irrlicht_renderer.h` only forward-declares those classes, so
  the new TU needs them directly since it calls their methods.
- No other file-local statics/anonymous-namespace symbols were referenced by
  the moved clusters (`logFrameTimings`/`runSceneProfile`/
  `profileSceneBreakdown` use only local lambdas and class members).

**Line counts:**
- `irrlicht_renderer.cpp`: 16,394 → 15,243 lines (**-1,151**)
- `renderer_rdp.cpp`: 214 lines (new; 196 moved lines + includes/namespace
  wrapper)
- `renderer_diagnostics.cpp`: 972 lines (new; 951 moved lines +
  includes/namespace wrapper)
- Net change: +2,043 / -1,151 ≈ +892 lines total (headers, namespace
  open/close boilerplate duplicated in two new TUs)

**Build/test verification:**
- `cmake --build build -j24`: clean after fixing two build issues found
  during the first pass (both mechanical, not scope creep):
  1. Missing `namespace EQT { namespace Graphics { ... } }` wrapper in both
     new files (`IrrlichtRenderer` is defined inside that namespace; the
     extracted method bodies use unqualified `IrrlichtRenderer::`).
  2. Missing full-type includes in `renderer_diagnostics.cpp` for
     `SkyRenderer`, `WeatherEffectsController`, `ConstrainedMeshCache`
     (`irrlicht_renderer.h` only forward-declares them for the header;
     the original .cpp had the full includes).
- `ctest --test-dir build`: 1191/1192 run tests passed (9 pre-existing
  asset/graphics tests skipped, unrelated to this unit).
  - `RDPServerFrameTest.UpdateFrameMultipleTimes`: passed in the full run;
    additionally re-ran in isolation 5/5 times, passed every time.
  - `AudioManagerTest.PreloadCommonSounds`: failed (ALSA "cannot find card
    '0'" / no sound hardware in this sandbox). Verified pre-existing and
    unrelated: stashed this unit's changes, rebuilt `willeq_core` +
    `test_sound_assets` against unmodified `fable`, re-ran the same test —
    it failed identically. Restored the stash afterward. (This same failure
    was independently documented by the F02 unit's review, corroborating it
    as an environment gap, not a regression.)
- `python3 scripts/refactor/verify_move_only.py fable..HEAD`: PASS
  ("Boilerplate: +0 / -10 lines (allowed)"). Note: the verifier also listed
  `refactor/F02_strings.md` as a changed non-C++ file — this is drift from
  `fable` advancing (F02 was merged to `fable` by another concurrent
  executor) after this branch was cut from an earlier `fable` commit, not a
  change made by this unit. This unit did not touch that file.
- `bash scripts/refactor/check_file_sizes.sh --files
  <slot2-absolute-paths>/renderer_rdp.cpp
  <slot2-absolute-paths>/renderer_diagnostics.cpp`: both `ok` (214 and 972
  lines, both well under the 1,500-line ceiling). Note: the script resolves
  its repo root from the shell's cwd, not from the file arguments — running
  it with relative paths from a cwd outside slot2 silently no-ops (files not
  found, loop `continue`s, exits 0). Passing full absolute paths sidesteps
  that.

**Deviations from plan:** none in scope. Two clarifications beyond the
plan's line-range estimates (both required by the plan's own instruction to
"confirm exact set by grep"): `measureSection` included in the scene-dump
cluster (regex miss in the first grep pass, caught by manual inspection);
`isRDPRunning`/`getRDPClientCount` included in the RDP cluster (contiguous
with the named RDP methods, inside the same guard).
