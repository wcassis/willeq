# H01 — Build the GLES2 backend for x86 (llvmpipe target)

Make the REAL production rendering path (the patched-Irrlicht GLES2 driver)
build and run on this x86_64 VM under Xvfb, where mesa/llvmpipe provides
ES 2.0. Today that driver is only ever compiled inside the ARM Docker image
(docker/Dockerfile.arm-noble lines ~193–230).

## Constraints

- The pristine Irrlicht source lives at /mnt/projects/irrlicht-1.8.5/ (and
  the zip beside it). Do NOT modify that shared copy — copy it into a local
  gitignored build area (e.g. third_party/irrlicht-gles2-x86/, gitignore it)
  and patch the copy.
- Do NOT break the ARM build: docker/Dockerfile.arm-noble,
  docker/irrlicht-drm-patch.py, and docker/irrlicht-drm/* must keep working
  for ARM exactly as-is. If the patch script needs an x86 mode, add a
  parameter/flag rather than forking the logic where feasible; forking into
  a clearly-named x86 variant is acceptable if parameterizing gets ugly —
  document the choice.
- Do NOT break the default x86 desktop-GL build (build/ with EQT_GLES2=OFF)
  — the full existing test suite must stay green on it.

## Design

1. **Recipe adaptation** (`scripts/build-gles2-x86.sh`): copy Irrlicht
   source, apply irrlicht-drm-patch.py + the driver sources (COpenGLES2Driver,
   COGLES2Texture, COGLES2Shaders, COGLES2MaterialRenderer), apply the
   Makefile edits from the Dockerfile — SKIPPING the ARM-specific pieces
   (libpng NEON objects line; keep the IRRMESHOBJ addition and the desktop-GL
   compile-config disable). Investigate how the patched tree does windowing:
   the DRM device (CIrrDeviceFB) is ARM/DRM-specific, but the Pi also runs
   `--renderer gles2` under X11, so the patch evidently keeps an X11+EGL
   path — find it and make sure it compiles for x86 against mesa's
   EGL/GLES2. Build a local static or shared libIrrlicht for x86_64.
2. **CMake wiring**: when EQT_GLES2=ON on x86, link the locally built
   patched Irrlicht + mesa EGL/GLESv2 instead of system Irrlicht (a cache
   variable like EQT_GLES2_IRRLICHT_DIR pointing at the local build; the
   ARM Docker flow must be able to keep its existing behavior). Keep DRM
   optional (EQT_DRM stays OFF for the x86 GLES2 config).
3. **Second build tree**: configure `build-gles2/` with
   -DEQT_GLES2=ON; the default `build/` stays desktop-GL. Both must build
   clean.
4. **Runtime bring-up**: under Xvfb (see scripts/run-model-viewer.sh for the
   established Xvfb pattern), run `build-gles2/bin/gles2_zone_benchmark` on
   a real zone from /mnt/projects/EverQuestP1999 for a short camera path.
   Acceptance: benchmark completes, CSV metrics are sane (nonzero draws,
   plausible poly counts), and a dumped frame is not black/garbage (the
   benchmark or a small addition should dump a PNG frame — PNG, not BMP).
   Then run `build-gles2/bin/willeq --no-graphics` (headless sanity) and,
   if time permits, briefly with graphics under Xvfb to confirm the device
   initializes (no server needed — reaching the login screen / loading
   screen render is enough).

## Verification

- Default build: `cmake --build build -j24` clean + full ctest green modulo
  documented exceptions (nothing regressed).
- GLES2 build: `build-gles2` compiles clean; benchmark run under Xvfb
  produces metrics CSV + a non-trivial PNG frame (attach dimensions + a few
  metric numbers in the review).
- `scripts/build-gles2-x86.sh` is idempotent (safe to re-run).

## Review

(executor fills in: patch adaptation details, windowing findings, build
config, benchmark run results, deviations)
