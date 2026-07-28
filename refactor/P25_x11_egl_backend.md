# P25 — X11-window EGL backend for the x86 GLES2 device

**Slot: slot2. x86 only — no Pi. OWNS EQEMU (optional use, for the
windowed live-session proof).** Sibling: P24 (slot1, survey — docs
only) — territories disjoint.

Standing decision (docs/decisions.md 2026-07-28): GLES2 is the only 3D
renderer. This unit adds the missing piece for interactive x86 dev use:
a real X11 window carrying the GLES2 context, so eliminating desktop GL
costs nothing interactively. Mesa's EGL supports the X11 platform with
GLES2 contexts natively (`eglGetPlatformDisplay(EGL_PLATFORM_X11_KHR)`
+ `eglCreateWindowSurface`); llvmpipe backs it on this VM.

## Design constraints

1. **Surfaceless stays the default.** CI, goldens, visual gate, and
   benchmarks all run the surfaceless path today — their behavior must
   be byte-identical after this unit. Windowed mode is explicit opt-in
   via a new CLI flag (suggest `--egl-x11`; wire it through the same
   config plumbing as `--drm`). Do NOT auto-detect DISPLAY — implicit
   mode switches are how goldens drift.
2. **Where it lives**: the x86 device fork `docker/irrlicht-drm-x86/
   CIrrDeviceFB.{h,cpp}` (the file that already holds the surfaceless
   fallback; H01). X11 code must not leak into the shared
   `docker/irrlicht-drm/` files the ARM build compiles — the ARM build
   must remain byte-identical (verify: build-arm-noble + confirm the
   shared-file diff is empty).
3. **Input**: in windowed mode, keyboard/mouse come from X11 events
   (XNextEvent → Irrlicht event translation), replacing the evdev path
   (which needs /dev/input access X11 sessions don't have). Scope:
   enough for real gameplay — WASD/arrows, modifiers, mouse move/
   buttons/wheel, window close. Keep the evdev path untouched for DRM.
4. **Swap/resize**: eglSwapBuffers on the window surface; handle
   Expose/ConfigureNotify at least to the degree of not crashing on
   resize (fixed-size window with resize declined is acceptable for
   round 1 — document the choice).
5. **Build**: link X11 only for the x86 GLES2 configuration
   (build-gles2-x86.sh / its CMake path). The default desktop build and
   ARM build link lists are untouched.

## Evidence bar

- **Headless CI invariance**: visual gate PASS, goldens untouched;
  gles2_zone_benchmark output unchanged (spot: one canonical replay
  frame-time CSV runs, no crash, screenshots byte-comparable if the
  harness supports it).
- **Windowed smoke under Xvfb**: run `willeq --renderer gles2
  --egl-x11` (with eqemu if you use the live proof, or the benchmark's
  zone-view mode if adequate) under Xvfb; capture the window via
  `xwd`/`import` — evidence that rendering lands IN THE WINDOW, not
  just that the process lives. Stills to
  `/mnt/projects/fable/videos/p25_x11_window/` + a committed manifest
  under `benchmarks/audits/p25_x11_window/`.
- **Input proof**: synthetic X11 input (xdotool) driving visible state
  change across captures — e.g. camera turn between two stills, or UI
  toggle. Name the exact xdotool sequence in the Review.
- **Live session (preferred if time allows)**: eqemu up, zone into
  qeynos2 windowed, move with injected input, capture. Restore
  Testchar (zone_id=36, x=-77.9232, y=21.9086, z=3, h=190), shut down
  cleanly.

## Rules

- Territory: `docker/irrlicht-drm-x86/`, the CLI-flag plumbing
  (application/config files, minimal lines), `scripts/build-gles2-x86.sh`
  + its CMake wiring if needed, audit/evidence dirs. Do NOT touch
  shared driver files (`docker/irrlicht-drm/`), zone loaders, or
  anything in P24's report.
- STOP conditions: (i) X11 support would require changes to shared
  `docker/irrlicht-drm/` files (report the required change instead);
  (ii) goldens move; (iii) input translation requires reworking the
  input-handler layer beyond event translation (report scope).

## Verification

- Full 6-step bar (known ctest exceptions per protocol) + the ARM
  shared-file byte-identical check + the three evidence items above.

## Review

(executor fills in: surface/context creation details, input coverage
table, evidence paths + xdotool sequences, resize decision, deviations)
