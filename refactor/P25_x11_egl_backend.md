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

### Surface/context creation

`docker/irrlicht-drm-x86/CIrrDeviceFB.cpp` constructor: when the
`WILLEQ_EGL_X11` env var is set (see plumbing below), `initEGLX11()` (now in
the new `CIrrDeviceFB_X11.cpp` TU — see file-size deviation below) runs
*instead of* the DRM/GBM/surfaceless chain:

1. `XOpenDisplay(nullptr)` — real X11 connection (respects `$DISPLAY`; safe
   because this whole path is gated behind the explicit opt-in, never
   triggered by `$DISPLAY` alone).
2. `eglGetPlatformDisplay(EGL_PLATFORM_X11_KHR, dpy, nullptr)` via a
   dynamically-looked-up function pointer (`eglGetProcAddress`, trying
   `eglGetPlatformDisplay` then `eglGetPlatformDisplayEXT`), falling back to
   plain `eglGetDisplay((EGLNativeDisplayType)dpy)` — same defensive-lookup
   style already used by `initEGLSurfaceless()`.
3. `eglChooseConfig` with `EGL_SURFACE_TYPE=EGL_WINDOW_BIT`,
   RGBA8/D24/S8, `EGL_RENDERABLE_TYPE=EGL_OPENGL_ES2_BIT` (falls back to a
   no-stencil config if unavailable).
4. `eglGetConfigAttrib(..., EGL_NATIVE_VISUAL_ID, ...)` →
   `XGetVisualInfo(VisualIDMask)` to get a matching `Visual*`/depth (falls
   back to `DefaultVisual`/`DefaultDepth` if the lookup fails).
5. `XCreateWindow` with that visual + a dedicated colormap, event mask
   `KeyPress|KeyRelease|ButtonPress|ButtonRelease|PointerMotion|
   StructureNotify|Exposure`; `XStoreName` ("WillEQ", later overwritten by
   `setWindowCaption()` with the app's real title); `XSetWMProtocols` with
   `WM_DELETE_WINDOW`; `XMapWindow` + `XFlush`.
6. `eglCreateContext` (ES 2.0) + `eglCreateWindowSurface` against the X11
   `Window` + `eglMakeCurrent`.

`present()` calls `eglSwapBuffers(eglDisplay_, eglSurface_)` when
`x11Active_` (checked before the existing `surfaceless_` branch).
`run()` calls `pollX11Events()` instead of `pollEvdev()` when `x11Active_`.
`initEvdev()` is skipped entirely in this mode (X11 events replace it, and
evdev needs `/dev/input` access X11 sessions don't grant). Destructor closes
the X11 window/display (`closeX11Window()`) alongside the existing EGL/GBM
teardown.

### Input coverage

| Input | X11 source | Irrlicht event | Notes |
|---|---|---|---|
| Letters, digits, F1-F12, Esc/Tab/Backspace/Return/Space/Delete/Insert/Home/End/PgUp/PgDn/CapsLock/NumLock/ScrollLock, arrows, Shift/Ctrl/Alt (L+R), punctuation/OEM keys, numpad | `KeyPress`/`KeyRelease` → `XLookupKeysym(&xev.xkey, 0)` | `EET_KEY_INPUT_EVENT` | New `x11KeysymToIrrlicht()` map (XK_-prefixed, no collision with Irrlicht's `EKEY_CODE` — only Linux `linux/input.h` `KEY_*` macros collide, and that header isn't included in the X11 TU) |
| Printable-char text (for chat etc.) | `XLookupString` on the same `KeyPress` | `KeyInput.Char` | ASCII only (matches the existing evdev path's scope) |
| Mouse move | `MotionNotify` | `EET_MOUSE_INPUT_EVENT` / `EMIE_MOUSE_MOVED` | |
| Left/Right/Middle click | `ButtonPress`/`ButtonRelease` (`Button1`/`Button3`/`Button2`) | `EMIE_{L,R,M}MOUSE_{PRESSED_DOWN,LEFT_UP}` | |
| Wheel | `ButtonPress` `Button4`/`Button5` | `EMIE_MOUSE_WHEEL` | X11 reports wheel as button clicks, not an axis |
| Window close | `ClientMessage` matching `WM_DELETE_WINDOW` | sets `Close=true` | verified: process exits cleanly on its own, not via signal (see evidence) |
| Resize/expose | `ConfigureNotify`/`Expose` | no-op | see Resize decision below |

Modifiers (Shift/Ctrl/Alt) are tracked from `xev.xkey.state` on every
key event and folded into `KeyInput.Shift`/`KeyInput.Control` (Alt tracked
internally, matching the scope of the existing evdev modifier tracking).

### Resize decision

Declined for round 1, as the plan allows. `initEGLX11()` sets
`WM_NORMAL_HINTS` with `min_width=max_width=Width`,
`min_height=max_height=Height` so a well-behaved interactive window manager
won't offer a resize handle. `ConfigureNotify`/`Expose` are still handled
(as explicit no-op cases, not falling through unnoticed) so an unexpected
resize notification doesn't crash. Verified directly: a raw
`XResizeWindow`-equivalent call (`xdotool windowsize <id> 1200 900`, issued
without any window manager mediating it, since Xvfb has none) DID resize
the X11 window to 1200x900 — WM hints are advisory to window managers only,
not enforced against a client's own direct resize request — and the app's
EGL surface/render viewport stayed fixed at the original 800x600 with no
crash. This is exactly the intended round-1 behavior (fixed render size,
survive unexpected resize), demonstrated via a path that happens to bypass
the interactive decline a real WM would otherwise enforce.

### CLI-flag plumbing

`--egl-x11` (`src/client/application.cpp` parseArguments, next to `--drm`)
→ `ApplicationConfig::useEglX11` (`include/client/application.h`) →
`ConstrainedRendererConfig::useEglX11` (`include/client/graphics/
constrained_renderer_config.h`) → `irrlicht_renderer.cpp`, guarded by
`#ifdef EQT_HAS_GLES2` (defined on both x86 and ARM GLES2 builds), sets the
`WILLEQ_EGL_X11` env var before `createDeviceEx()`.

**Deviation 1**: the plan suggested wiring "through the same config plumbing
as `--drm`" — literally that would mean `CIrrDeviceFB::requestX11Window()`
called directly from `irrlicht_renderer.cpp`, mirroring how `useDRM` sets
`params.DeviceType`. That doesn't work: `CIrrDeviceFB.h` is a private
Irrlicht-internal header (lives under `docker/irrlicht-drm-x86/`, copied
into `third_party/irrlicht-gles2-x86/source/Irrlicht/` at build time) that
is never on the application's include path — only the patched Irrlicht's
*public* API (`third_party/irrlicht-gles2-x86/include/`) is
(`CMakeLists.txt` `IRRLICHT_INCLUDE_DIR`). The existing codebase already
solves an identical problem this way: `src/client/graphics/
gpu_upload_thread.cpp` reads `eglGetCurrentDisplay()`/
`eglGetCurrentContext()` rather than reaching into `CIrrDeviceFB` directly,
for the same reason. Following that precedent, `--egl-x11` is plumbed via
the `WILLEQ_EGL_X11` env var (set by `irrlicht_renderer.cpp`, read directly
by the `CIrrDeviceFB` constructor) instead of a direct method call. This is
inert on ARM: ARM's `CIrrDeviceFB.cpp` is the untouched shared
`docker/irrlicht-drm/` file, which has no knowledge of this variable at all.

**Deviation 2**: `docker/irrlicht-drm-x86/CIrrDeviceFB.cpp` grew from 1305
to ~1790 lines with the X11 code inline, tripping the project's file-size
WARN threshold (docs/decisions.md Batch F ceilings, enforced by
`scripts/refactor/check_file_sizes.sh`, protocol: "put substantial new code
in new TUs"). Split `initEGLX11()`/`pollX11Events()`/`closeX11Window()`/the
X11 keysym map into a new `docker/irrlicht-drm-x86/CIrrDeviceFB_X11.cpp`
(member-function-definition overflow for the same `CIrrDeviceFB` class, not
a new class) — `CIrrDeviceFB.cpp` is now 1370 lines (`ok`).
`scripts/build-gles2-x86.sh` copies the new file alongside `CIrrDeviceFB.*`
and patches the Irrlicht Makefile's `IRROTHEROBJ` list (in addition to the
existing `IRRMESHOBJ` patch) so `make staticlib` compiles and archives it.
ARM is unaffected — this file isn't part of the ARM Dockerfile build at all.

**Deviation 3**: `CMakeLists.txt` needed X11 linked for *two* targets, not
just the main `willeq` executable — `gles2_zone_benchmark` also links the
patched static Irrlicht lib directly and failed with undefined X11 symbol
references until `X11` was added to its `ZONE_BENCH_LIBS` too (same
`EQT_GLES2 AND NOT EQT_DRM` condition). Audited all other
`IRRLICHT_LIBRARY`-linking targets (`model_viewer`, `generate_textures`,
etc.): they already link `X11` unconditionally whenever `EQT_DRM` is false
(their link-lib branching splits only on `EQT_DRM`, not
`EQT_DRM OR EQT_GLES2` — the x86 GLES2 build takes the same `ELSE()` branch
as a plain desktop-GL build), so no further changes were needed there.

### Evidence

**Headless CI invariance**: `ctest -R visual_gate` on the default desktop
`build/` — PASSED, goldens untouched. Full `ctest --test-dir build`
(1235 tests): 99% pass, only the pre-existing known-flaky
`AudioManagerTest.PreloadCommonSounds` failed (documented protocol
exception). `build-gles2/bin/gles2_zone_benchmark --zone qeynos2 --eq-path
/mnt/projects/EverQuestP1999 --eqe1-path data/eqe1 --frames 60 --warmup 5
--navmesh-path /mnt/projects/maps/nav` (default surfaceless path, no
`--egl-x11` concept in this tool) completed with no crash: Avg FPS 122.4,
Total frame avg 8.17ms, draw calls 25-111 across 60 frames — normal ranges,
no regression.

**Windowed smoke + input proof**: `benchmarks/audits/p25_x11_window/
EVIDENCE_MANIFEST.txt` (committed) + stills at
`/mnt/projects/fable/videos/p25_x11_window/stills/` (NFS, not committed —
repo-wide `.gitignore` excludes `*.png`, same convention as
`p20_live_visual`). Live eqemu session (qeynos2), Xvfb `:99`,
`DISPLAY=:99 build-gles2/bin/willeq -c data/captures/p25_willeq.json
--renderer gles2 --egl-x11`. Exact xdotool sequences:

```
# turn right (02_after_turn_right.png)
xdotool key --clearmodifiers --repeat 60 --delay 50 d

# mouselook right-drag (03/04_after_mouselook_*.png)
xdotool mousemove --window <id> 400 300
xdotool mousedown 3
xdotool mousemove_relative -- -200 0   # x2, 0.2s apart (03)
xdotool mouseup 3
# retry, slower (04): 5x `mousemove_relative -- -60 0`, 0.2s apart
```

`01_initial.png` → `02_after_turn_right.png` shows a fence/railing
structure and an NPC nameplate ("a plague rat") rotating into view that
weren't visible before — unambiguous camera-rotation proof from synthetic
keyboard input. `03` → `04` shows the same landmarks rotating further out
of view under mouse-only input (right-button drag). Window-close
(`WM_DELETE_WINDOW` ClientMessage) verified twice: process exits cleanly on
its own, confirmed via temporary debug instrumentation (added, verified,
then removed before commit) showing the `ClientMessage` event (type 33)
arriving and the process exiting without a signal.

**Live eqemu session**: used as the capture mechanism itself (not a
separate step) — see above. Testchar restored to zone_id=36, x=-77.9232,
y=21.9086, z=3, heading=190 via direct DB UPDATE (confirmed via SELECT
before/after) and `scripts/eqemu/down.sh` run for clean shutdown.

### Verification bar results

1. `cmake --build build -j24` — PASS.
2. `ctest --test-dir build --output-on-failure` — 1234/1235 PASS (only the
   documented `AudioManagerTest.PreloadCommonSounds` flake failed).
3. `bash scripts/build-gles2-x86.sh` — PASS (clean rebuild from the
   committed source, including the new `CIrrDeviceFB_X11.cpp` TU).
4. Visual gate PASS, goldens untouched.
5. `bash scripts/build-arm-noble.sh` — PASS (ran twice: once before and
   once after the file-size split, both clean).
6. `bash scripts/check_file_sizes.sh fable` — no FAILs. One pre-existing WARN
   pair (`application.cpp` 1971 lines, `irrlicht_renderer.cpp` 2835 lines)
   confirmed pre-existing at the `fable` tip (1963/2821 lines respectively)
   — not introduced by this unit, out of scope to fix here. The one WARN
   this unit's diff would otherwise have introduced (`CIrrDeviceFB.cpp`) was
   resolved by the file split (Deviation 2).
7. ARM shared-file byte-identical check: `git status --short
   docker/irrlicht-drm/ docker/irrlicht-drm-patch.py` — empty (untouched).

### Deviations summary

1. `WILLEQ_EGL_X11` env var instead of a direct `requestX11Window()` call
   (private-header visibility — see above).
2. New `CIrrDeviceFB_X11.cpp` TU (file-size threshold).
3. X11 also linked into `gles2_zone_benchmark`, not just `willeq`.

No STOP conditions were hit: X11 support did not require touching shared
`docker/irrlicht-drm/` files, no goldens moved, and the input translation
stayed within event-translation scope (no input-handler-layer rework).
