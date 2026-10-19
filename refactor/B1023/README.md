# B1023 (#343): the Pi renders 24-bit depth although Lima accepts Z16 — named step, measured, with the gate re-run

**Verdict: the step that never yields a 16-bit-depth EGL config is Mesa's
config GENERATION, not fable's chooser and not the hardware.**
Lima accepts `PIPE_FORMAT_Z16_UNORM` for depth, but Mesa 25.2.8's
`driCreateConfigs()` (in `dri_fill_in_modes()`) SKIPS the Z16 depth-stencil
format for every 32-bit color format unless the screen sets
`caps.mixed_color_depth_bits` — and Lima does not set it (Panfrost does;
that is why B1037's lane 3, the ROCK 4D, answers `depth=16`). **No fable-side
code change: the client's request is already correct** (it sends
`EGL_DEPTH_SIZE 16, EGL_STENCIL_SIZE 0` — proven below, not assumed), and
the 8 configs it is offered are the full set the driver generates. Class-C
ruling request at the bottom.

All numbers below are real runs on lane 1 (Orange Pi One, `Xunlong Orange
Pi One`, kernel 6.6.0, card0 = sun4i-drm display, card1 = lima GPU), under
the B1023 lock, out of the published fable tip build
`/mnt/projects/fable/ci-arm/e58a42e2735e4c048e2f5d3e67aa393d646de40a/`
(`sha=e58a42e2…`, built 2026-10-19) with the shipped Mesa prefix
`/home/user/mesa-nollvm-25.2.8` (`mesa_nollvm_version=25.2.8`, BUILD_INFO).
The raw evidence lives in `/home/user/kb-evidence/kb-b1023-pi-depth16/`.

## 1. What the Pi's display offers (step 1a: the EGL config dump)

Two independent dumps agree, so this is not a probe artifact:

- `eglinfo_dump.txt` — the full config table of the GBM platform
  (`EGL driver name: lima`, `EGL client APIs: OpenGL_ES`). For the 8-bit
  XRGB8888 visual (`0x34325258`, the gbm surface's own format) there are
  **eight** configs:
  depth/stencil `{0/0, 24/0, 24/8}` × `{1x, 4x MSAA}` — **no depth=16
  XRGB8888 config exists**. The only depth=16 configs in the whole table
  are on the 16-bit color visuals RGB565 (`0x36314752`) and its companions.
- `b1023_egl_probe2` (`egl_dump_card0.txt`, `egl_dump_card1.txt`) — a
  C probe over the same display; its standalone display path differs from
  the client's (card0 is GLX-flavoured for `eglGetDisplay`), so it is
  secondary; the LD_PRELOAD shim below is the authoritative client-side
  capture.

## 2. What the CLIENT actually sends and gets (step 1b + the chooser's fate)

`shim_log.txt` — an LD_PRELOAD shim (`b1023_eglshim.c`) intercepting the
client's own `eglChooseConfig`/`eglGetConfigAttrib` calls, run with the
published benchmark (`gles2_zone_benchmark-nollvm`, qeynos2, 800x600):

- the client's request list is
  `[SURFACE_TYPE=WINDOW, RED=8, GREEN=8, BLUE=8, ALPHA=0, DEPTH=16,
  STENCIL=0, RENDERABLE_TYPE=ES2]` — i.e. **fable already requests 16-bit
  depth, no stencil** (B971's surface_rule: the orangepi preset sets
  `enableStencilBuffer=false`, so `CIrrDeviceFB::initEGL()`'s
  `depthBits = wantStencil ? 24 : 16` = 16). A v4 shim re-run
  (`b1023_verify4.c`) confirms the request on the first call: `numConfigs=8`
  with DEPTH=16 in the attrib list, and `client_run.log` has NO
  "No config with depth buffer, retrying without" line — so the depth-16
  request SUCCEEDS; the no-depth fallback at CIrrDeviceFB.cpp:786 never
  runs.
- all 8 returned configs are `depth=24` (stencil 0 or 8); the client picks
  `depth=24 stencil=0` (log line `EGL: Config depth=24 stencil=0`) — the
  chooser (`chooseConfigForGbmFormat`, docker/irrlicht-drm/CIrrDeviceFB.cpp
  → `eqt_drm::selectEglConfigForGbmFormat` in gbm_config_select.h) correctly
  walks every match, finds the first XRGB8888 visual config, and there is
  nothing better to find. **The chooser is not at fault**: the depth=16
  config it would have to prefer does not exist in the list, and the
  fable-side request is already the 16-bit one. (Acceptance B is
  therefore not applicable; no fork is changed and
  scripts/ci/test_gbm_config_select_forks.py stays green.)

## 3. The step that drops the 16-bit config (step 1c, named with file:line)

Mesa 25.2.8, `src/gallium/frontends/dri/dri_screen.c` (fetched at tag
`mesa-25.2.8`; a copy is kept as
`/home/user/kb-evidence/kb-b1023-pi-depth16/mesa_2528_dri_screen.c`):

1. `dri_fill_in_modes()` builds the ZS format list from the driver's caps:
   `if (HAS_ZS(Z16_UNORM)) zs_formats[num_zs_formats++] =
   PIPE_FORMAT_Z16_UNORM;` — and `HAS_ZS` calls
   `p_screen->is_format_supported(…, PIPE_BIND_DEPTH_STENCIL)`.
2. `lima_screen_is_format_supported()` (Mesa 25.2.8
   `src/gallium/drivers/lima/lima_screen.c`, the ZS arm at lines 254-260)
   accepts exactly `PIPE_FORMAT_Z16_UNORM`, `PIPE_FORMAT_Z24_UNORM_S8_UINT`
   and `PIPE_FORMAT_Z24X8_UNORM` — so **the Z16 format IS in the list**
   (this is the issue's own measured premise).
3. But `driCreateConfigs()` (dri_screen.c, called from `dri_fill_in_modes`
   with `color_depth_match = !mixed_color_depth`, and
   `mixed_color_depth = p_screen->caps.mixed_color_depth_bits`) SKIPS the
   (color, ZS) combination when the depths do not match:
   ```
   if (color_depth_match && (depth_bits || stencil_bits)) {
       /* … A 32-bit color format still matches 24-bit depth …
          we just need to make sure that color/depth are both 16
          or both non-16. */
       if ((depth_bits + stencil_bits == 16) !=
           (color_bits[0] + color_bits[1] + color_bits[2] +
            color_bits[3] == 16))
           continue;
   }
   ```
   For the gbm surface's 32-bit XRGB8888 color format, `depth_bits=16`
   (Z16) fails the `!=` test and the config is never generated. Only Z24X8
   (24+0=24≠16, color 32→both non-16) and Z24S8 (24+8=32, same) survive —
   exactly the `{0, 24/0, 24/8}` set `eglinfo` shows.
4. `mixed_color_depth_bits` is set by **panfrost**
   (`pan_screen.c`: `caps->mixed_color_depth_bits = true;`) and NOT by
   **lima** (no `mixed_color_depth_bits` line in `lima_screen.c`; the base
   `u_init_pipe_screen_caps` in `u_screen.c` sets only
   `mixed_colorbuffer_formats`).
5. The GBM platform surfaces this exact list: `drm_add_configs_for_visuals`
   in `src/egl/drivers/dri2/platform_drm.c` walks
   `dri2_dpy->driver_configs` (the DRI2 connector's `driver_configs`,
   populated from the DRI driver's `ScreenCreate` — i.e. this
   `dri_fill_in_modes` output) into EGLConfigs. There is no second, GBM
   -specific config generator that could add configs back.

Cross-check: B1037's lane 3 (ROCK 4D, Mali-G52, **Panfrost**, Mesa 26)
answers `depth=16 stencil=0` for the same XRGB8888 surface — the same
client code, the same request, a driver that sets `mixed_color_depth_bits`
offers the 16-bit config and the chooser takes it. The mechanism is
confirmed by both lanes' opposite answers.

**The named step, one line:** `driCreateConfigs()` in Mesa 25.2.8
`src/gallium/frontends/dri/dri_screen.c` skips Z16 for 32-bit color because
`lima_screen.c` never sets `caps->mixed_color_depth_bits` — so the Pi's
EGL/GBM config list contains no depth-16 XRGB8888 config for fable's
chooser to pick.

## 4. The 15 Pi gate frames (step 4): pre/post table, no bless

Run recipe: `b1023_run.sh` (device copy in the evidence dir) — the gate's
OWN `--platform pi` benchmark invocation (`visual_gate.py run_benchmark()`
word for word: `--frames 60 --warmup 5 --res 800 600 --dump-frames 0,1,2
--dump-png-prefix <zone> --csv --memory-json --occluder-budget 16
--adaptive-lighting --zone-pp-budget 20 --pp-budget-mode count
--player-light-level 9`, the zone's `extraArgs` poses read from the
manifest's own `platforms.pi` section at runtime, then `--drm`), out of
`ci-arm/e58a42e2735e4c048e2f5d3e67aa393d646de40a`, 400 s bound per zone,
HDMI forced on, all five zones `exit=0`. Comparison: the gate's own
`compare_images_exact` rule, run locally (`compare_lane1.py`) against the
committed `tests/goldens/pi/<zone>/f<n>.png`. **No golden, manifest,
preset, budget, ratchet or third_party file was edited by this unit** —
this branch's diff is docs-only.

Per-frame diff counts, lane 1 (this run) vs the committed Pi goldens, next
to B1037's pre-fix lane 2 (Rock64/Mali-450/Lima) counts:

| zone | frame | lane 1 (this run) vs golden | lane 2 (B1037) vs golden |
|---|---|---|---|
| qeynos2 | f0 | differs (134 492 px) | differs (134 492 px) |
| qeynos2 | f1 | **IDENTICAL** | **IDENTICAL** |
| qeynos2 | f2 | differs (290 px) | differs (290 px) |
| gfaydark | f0 | differs (36 px) | differs (36 px) |
| gfaydark | f1 | differs (565 px) | differs (565 px) |
| gfaydark | f2 | differs (693 px) | differs (693 px) |
| befallen | f0 | differs (37 px) | differs (37 px) |
| befallen | f1 | **IDENTICAL** | **IDENTICAL** |
| befallen | f2 | **IDENTICAL** | **IDENTICAL** |
| iceclad | f0 | differs (1 231 px) | differs (1 231 px) |
| iceclad | f1 | differs (51 940 px) | differs (51 940 px) |
| iceclad | f2 | differs (13 619 px) | differs (13 619 px) |
| timorous | f0 | differs (114 732 px) | differs (114 732 px) |
| timorous | f1 | differs (1 218 px) | differs (1 218 px) |
| timorous | f2 | differs (143 514 px) | differs (143 514 px) |

3 of 15 frames byte-identical; the metrics (draws/stateChanges/polys) are
bit-exact against the `platforms.pi` blessed values in all 15 frames.
**No frame gets WORSE than the pre-fix lane 2 measurement**: all 15 match
B1037's lane 2 counts to the pixel — lane 1 (Mali-400) and lane 2
(Mali-450) are the same GPU class under the same Mesa 25.2.8, both at
`depth=24 stencil=0` GBM XRGB8888, and B1037's analysis attributes the
whole residual to Lima's FP16 fragment path (alpha-cutout coverage flips
at sub-LSB sampling differences, localized to the lower frame), which this
run reproduces frame for frame.

Driver side of the run: `[gldriver] renderer='Mali400'
gallium=/home/user/mesa-nollvm-25.2.8/lib/libgallium-25.2.8.so llvm=none`
in all five zone logs, `EGL: Config depth=24 stencil=0` in all five (the
depth the client will render with stays 24 — unchanged, as predicted for
the no-code-change path).

Ledger `owned` (the client's own census; step 3's ready/steady ask, qeynos2
only): the baseline client run (`client_run.log`, 60 frames) reports
`owned=54810136` at `point=ready` and `owned=56981856` at `point=steady`;
this gate run's qeynos2 ledger (`qeynos2_mem.json`, `point=benchmark_end`)
reports `owned=57541512`, `logical_depth_stencil=1920000` (800·600·4 — the
24-bit Z buffer, the predicted value: a depth-16 surface would have shown
960000). The Z buffer is a GPU buffer OUTSIDE RssAnon on the Pi (B955's
`gpu_buffers` row, 49.6 MB here) — it does not move `owned`, exactly as
B1037 measured for lane 2, so a future depth-16 surface would not change
these `owned` figures either.

## 5. What would be needed for depth=16 (class-C ruling request)

A **Mesa change, not a fable one**: set `caps->mixed_color_depth_bits =
true;` in `lima_screen.c` (one line, the same line panfrost has) so that
`driCreateConfigs()` emits the XRGB8888+Z16 config. What that would take:

- a Mesa bug report / patch against the Lima driver (the cap is
  deliberately opt-in because it is a hardware property — whether the
  Utgard-family blend unit can composite a Z16 depth attachment with a
  32-bit color buffer in one pass; the cap's semantics are the driver
  asserting that it can);
- if Will approves asking: the upstream Mesa PR, plus a rebuild of the
  shipped `mesa-nollvm` prefix (B935) at a Mesa that carries the fix;
- after the new prefix ships on lane 1: the client's EXISTING request
  (DEPTH=16, STENCIL=0, already proven above) makes `eglChooseConfig`
  return the Z16 config and the chooser takes it — **no fable code change
  at any point**; `logical_depth_stencil` would drop 1920000 → 960000 and
  the 15 gate frames would re-measure against the (24-bit-blessed) Pi
  goldens — which would then show a depth-resolution diff (the goldens are
  B971's 24-bit Pi set), and any Pi golden movement is a re-bless by Will.

**RULING REQUEST (for the orchestrator to file, class C):** "B1023 (#343):
the Pi stays at 24-bit depth because Mesa's `driCreateConfigs()` (25.2.8
`src/gallium/frontends/dri/dri_screen.c`) skips the Z16 format for 32-bit
color on Lima — `lima_screen.c` does not set `caps.mixed_color_depth_bits`
(panfrost does, which is why the ROCK 4D lane answers depth=16). Lima's
`is_format_supported` already accepts `PIPE_FORMAT_Z16_UNORM`. Request:
approve asking upstream Mesa (or the shipped-prefix rebuild) to set the
cap in the Lima driver — a one-line driver cap assertion that changes the
Pi surface from D24 to D16 (1.92 MB → 0.96 MB at 800x600, the goldens'
depth) — or close #343 as a known Mesa/Lima limitation now that the
mechanism is named and measured."

## 6. Hygiene

- No source, test, golden, manifest, preset, ratchet or third_party file
  changed (docs-only branch: this README + `docs/ci.md`).
- Lane 1 lock taken as `B1023`, released at the end; the box is left as
  found (no reboot, thermal state logged in `lane1/thermal_{before,after}.
  txt`).
