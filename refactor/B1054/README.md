# B1054 (#374): --drm skips an openable card with no connected connector; WILLEQ_DRM_DEVICE

Measured defect (issue #374, ThinkPad 172.16.200.205, 2026-10-13):
`gles2_zone_benchmark --drm` opened card0 (NVIDIA Quadro RTX 3000, no
connected output), found no connector, and fell back to surfaceless EGL
(NVIDIA's, mislabeled "llvmpipe" by the log text) instead of trying
card1 (Intel UHD 630, eDP-1 connected).

## The fix

`docker/irrlicht-drm/drm_card_select.h` (new): `eqt_drm::selectDRMCard()` —
first candidate that BOTH opens AND has a connected connector with a CRTC
wins; `/dev/dri/card*` enumerated in numeric order; `WILLEQ_DRM_DEVICE` is
a single-path override whose failure is an explicit error, never widened.
Both device forks (`docker/irrlicht-drm/CIrrDeviceFB.cpp` and
`docker/irrlicht-drm-x86/CIrrDeviceFB.cpp`) call it; all four file-by-file
build recipes copy the header next to the TU (B654's input stamp already
hashes the whole dirs, so a warm lib cannot outlive this change).

Logging: `DRM: Opened <path> (fd=N) driver=<drmGetVersion>`;
`DRM: using <path> connector=<type-id>`; a skipped card logs WHY; the
surfaceless fallback line no longer claims llvmpipe — the `EGL
(surfaceless): Version ... vendor=<actually got>` line is the source of
truth.

Pinned by `tests/test_drm_card_select.cpp` (12 cases, fake card list,
Linux-only, beside `test_drm_mode_select`).

## ThinkPad verification (this branch, built from the committed inputs)

Host: ThinkPad (`~/src/willeq`, branch checked out at this MR's head via
bundle; `third_party/irrlicht-gles2-x86` rebuilt with
`scripts/build-gles2-x86.sh --irrlicht-only`; input stamp `55b89c68cd5b81bb3b2ee41024a6b9be19d199761c75778a31ad45b4d6c9517f`
== the stamp computed on the MR head's tree, so the lib and this tree
agree byte-for-byte on inputs).

Command: `WILLEQ_DATA_ROOT=$PWD WILLEQ_CACHE_DIR=/tmp/b1054
build-gles2/bin/gles2_zone_benchmark --zone qeynos2 --res 3840 2160
--eq-path /mnt/projects/EverQuestP1999 --eqe1-path
/mnt/projects/EverQuest_ETC1 --drm --frames 300 --csv /tmp/b1054/bench.csv`

(Note: the card's only mode is 3840x2160 — the default 800x600 run exits
1 with P49's loud mode-fallback error, which is correct behaviour, not a
regression. The verification therefore pins the native resolution.)

Key log lines (full logs: `run_autoselect.log`, `run_override.log`):

    DRM: /dev/dri/card0 opens but has no usable display (connector: none, CRTC: no) - trying next card
    DRM: Opened /dev/dri/card1 (fd=5) driver=i915
    DRM: using /dev/dri/card1 connector=14
    DRM: Found matching mode 3840x2160@60Hz for requested 3840x2160
    ...
    GLES2 Driver: Mesa Intel(R) UHD Graphics 630 (CFL GT2) (Intel) [OpenGL ES 3.2 Mesa 25.2.8-0ubuntu0.24.04.4]

connector=14 = `DRM_MODE_CONNECTOR_eDP` (libdrm `drm_mode.h`:
eDP is 14, LVDS 18) — the Intel panel, as the issue predicted.

`[impostor-timing]` line: `frame_p50_ms=9.43 frame_p95_ms=9.83`
(`run_autoselect.log:490`).

`[memledger] point=steady` line: `rss_anon=55259136 ledger=34655520
gpu_driver=26590768 owned_rss_anon=28668368 gpu_buffers=68562944`
(`run_autoselect.log:485`).
The run was at 3840x2160 (the box's only mode), so the gpu_buffers row
is ~3x an 800x600 run's; the figures are the measured ones for this
resolution, quoted as-is.

Override run (`WILLEQ_DRM_DEVICE=/dev/dri/card0`, same command, 60
frames):

    DRM: /dev/dri/card0 opens but has no usable display (connector: none, CRTC: no) - trying next card
    DRM: WILLEQ_DRM_DEVICE=/dev/dri/card0: card has no usable display (explicit override, no fallback)
    DRM/GBM/EGL initialization failed - falling back to surfaceless EGL
    EGL (surfaceless): Version 1.5, vendor=NVIDIA
    GLES2 Driver: Quadro RTX 3000/PCIe/SSE2 (NVIDIA Corporation) [OpenGL ES 3.2 NVIDIA 580.178.04]

The override names the path AND the reason, and the surfaceless line now
names the vendor it actually got (NVIDIA, not "llvmpipe") — both halves
of the issue's logging ask, in one run. Exit code 0 (surfaceless is the
device's designed fallback once DRM init has failed loudly; the ERROR
line is what makes it non-silent).

The ThinkPad was left as found: no packages installed, `~/src/willeq`
restored to its fable checkout state after verification (branch
`task/b1054` left in place, harmless; /tmp/b1054 left in place).

## Not verified / left open

- **The ARM fork's initDRM path was build-verified only** (Dockerfile
  COPY lines + the header's own compile in the x86 tree and in
  `test_drm_card_select`); it has not been exercised on a Pi in this
  run (the card says "do not touch the Pi"). The ARM behaviour is the
  same shared decision function, and the Pi rigs' cards are
  single-display, so the selection is a no-op there (card0 has a
  connected connector on every lane — the loop just picks it first, as
  before).
- **`docker/Dockerfile` (desktop x86 image) and the ARM Dockerfiles**
  are not built by CI in this pipeline (the ARM image is T503's lane;
  the desktop image is not in CI) — the new `cp`/`COPY` lines are
  textual parity with `drm_mode_select.h`'s, verified by inspection.
- The `[memledger]`/`frame_p50_ms` figures above are at 3840x2160
  (the ThinkPad's only mode) and are cited as measured, not compared
  against any budget (this card is a selection fix, not a memory unit).
