# B654 (issue #56): the --egl-x11 device drops every numpad digit key

Branch `task/kb-b654-x11-numpad`, kanban t_6f7a25d9.

## Root cause [measured]

`CIrrDeviceFB::pollX11Events()` (`docker/irrlicht-drm-x86/CIrrDeviceFB_X11.cpp`)
translates a KeyPress with `XLookupKeysym(&xev.xkey, 0)` — modifier index 0.
On Xvfb's default keymap (probed live with a tiny XLookupKeysym harness,
displays :96/:97/:99) a keypad-digit key's index-0 entry is its NAVIGATION
alias regardless of NumLock:

    keypad-7 keycode 79 -> XK_KP_Home   (0xff95)   [digit XK_KP_7 at index 1]
    keypad-8            -> XK_KP_Up
    keypad-9            -> XK_KP_Prior
    keypad-4            -> XK_KP_Left
    keypad-5            -> XK_KP_Begin
    keypad-6            -> XK_KP_Right
    keypad-1 keycode 87 -> XK_KP_End    (0xff9c)
    keypad-2            -> XK_KP_Down
    keypad-3            -> XK_KP_Next
    keypad-0            -> XK_KP_Insert

The old `x11KeysymToIrrlicht()` had cases only for the digit keysyms
(XK_KP_0..9), so all ten aliases fell to `default -> (EKEY_CODE)0` and every
numpad digit was dropped: the T471 camera bindings in config/hotkeys.json
(Numpad7 -> CameraZoomIn, Numpad1 -> CameraZoomOut, Alt+Numpad8/2/6/4/9/3 ->
raise/lower/orbit/pitch) never fired under `--egl-x11`. The DRM/evdev backend
maps KEY_KP0..KEY_KP9 to KEY_NUMPAD0..9 regardless of NumLock — that is the
reference behaviour.

## Fix

- `docker/irrlicht-drm-x86/x11_keysym_map.h` (new): the keysym -> EKEY_CODE
  decision factored into a pure, display-free header (includes only
  `<X11/X.h>`, `<X11/keysym.h>`, `<Keycodes.h>` — no Display*, no
  `<irrlicht.h>`), so the device TU and the display-less unit test share the
  exact same map and a regression cannot hide in a copy. The ten
  navigation-alias cases resolve to the SAME numpad-digit code as the digit
  keysym (both NumLock states, Alt+ variants included — the alias is the
  index-0 keysym in both states).
- `CIrrDeviceFB_X11.cpp` includes the header; the switch moved out of the TU.
  The TU shrank 129 -> ~128 lines of map (357 lines total, under the ceiling).
- `scripts/build-gles2-x86.sh` copies the header into the Irrlicht build area
  next to the TU, like the other driver headers.

## CI propagation (the warm-lib blind spot)

`.gitlab-ci.yml` keeps `third_party/irrlicht-gles2-x86/` warm between
pipelines (GIT_CLEAN_FLAGS) and the build job rebuilt the lib only when
`libIrrlicht.a` was ABSENT — so a committed device-source fix was invisible to
every later pipeline. Root fix: `scripts/ci/irrlicht_input_stamp.sh` — the
build script writes a content digest (relative path + sha256 of every file in
`docker/irrlicht-drm*/`, `docker/irrlicht-drm-patch.py` and the build script
itself, sorted) to `third_party/irrlicht-gles2-x86/irrlicht_input_stamp.txt`
next to the .a; the build job now rebuilds when the .a is absent OR when
`verify` reports the stored digest no longer matches the committed inputs.
The pristine Irrlicht reference is deliberately out of scope (fixed read-only
source; updating it is a manual cold-rebuild event). Proven live: this MR's
own pipeline must show the rebuild happening once (its first build runs on a
warm lib built from PRE-MR inputs, with no stamp file) and the Xvfb test
passing against the rebuilt lib; a second push with no input change must NOT
rebuild. docs/ci.md: "Device-source changes are caught by the input stamp".

## Tests

- `tests/test_x11_keysym_map.cpp` — display-less, compiles the shared header,
  pins XK_KP_0..9 AND the ten navigation aliases -> KEY_NUMPAD0..9, KP_Decimal
  unchanged, unmapped keysym still 0. RED before (RED_unit.txt: the ten alias
  cases resolved to 0), GREEN after (GREEN_unit.txt: 4/4).
- `tests/test_x11_numpad_e2e.cpp` — the real path: a private Xvfb (display
  :97, B654_XVFB_DISPLAY to override), a real CIrrDeviceFB via
  createDeviceEx(EDT_OGLES2, EIDT_BEST) under WILLEQ_EGL_X11 (the exact
  --egl-x11 opt-in), real `xdotool key --window <wid> KP_7`/`KP_1`, pumped
  through `run()` -> `pollX11Events()`, resolved through the shipped
  HotkeyManager. Asserts CameraZoomIn >= 1 and CameraZoomOut >= 1.
  RED before (RED_e2e.txt: both presses delivered key=0, action=(none)),
  GREEN after (GREEN_e2e.txt: key=103/KEY_NUMPAD7 -> CameraZoomIn,
  key=97/KEY_NUMPAD1 -> CameraZoomOut). SKIPs loudly only when Xvfb/xdotool
  are genuinely absent from the host; any device-path failure is a hard FAIL.
- Alt+ variants [measured, probe run against the rebuilt lib, display :96]:
  alt+KP_8 -> KEY_NUMPAD8 + CameraRaise, alt+KP_6 -> KEY_NUMPAD6 +
  CameraOrbitRight, alt+KP_9 -> KEY_NUMPAD9 + CameraPitchUp (altprobe.txt).

## Local verification

- `env JOBS=3 bash scripts/build-gles2-x86.sh --irrlicht-only` into the
  worktree's own `third_party/` (NOT the shared /home/user/src/willeq copy):
  exit 0; `nm libIrrlicht.a` contains the new map.
- File sizes: `scripts/refactor/check_file_sizes.sh -C . --all` -> 1254 ok,
  0 warn, 0 fail; CLAUDE.md 56,681 < 60,000.
