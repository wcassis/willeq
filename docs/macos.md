# macOS: a full-parity client platform (B1039 #359, B1043 #363)

**Target (Will, 2026-10-12):** "The mini is not meant to be headless only, it
is meant to have the same graphical capabilities as the x86 and orange pi
builds." A Mac runs the same GLES2 renderer, the rendered client,
`gles2_zone_benchmark`, the visual gate and the memory ledger as x86 and the
Orange Pi. GLES2 over EGL is the only backend (P30/P32), so on macOS the
provider is **ANGLE** (EGL + GLES2 over Metal) and the Irrlicht is the same
patched GLES2 build x86 uses, with a Darwin device.

Proven on the M4 mac mini (`m4mini.goldfarm.local`, Apple M4, macOS 15.7.4,
Xcode 26.2 / Apple clang 17, Homebrew under `/opt/homebrew`). History: B1031
(#351) census, B1038 (#358) + B1039 (#359) headless fix, B1043 (#363) the
graphics PROVE card (`review/B1043/README.md` has every command and log).

## Status per piece

| piece | status on the mini | where |
|---|---|---|
| headless client + suite (`EQT_GRAPHICS=OFF`) | **works**: build exit 0, `ctest -LE hygiene` + `-L hygiene` green | B1039, below |
| ANGLE (EGL 1.5 + GLES 3.0 over Metal) | **works**: built from source at a pinned commit; pbuffer GLES2 clear + triangle + `glReadPixels` PASS, shared context OK, ETC1 exposed | "ANGLE" below |
| ANGLE window surface | **partial**: `eglCreateWindowSurface` on an NSView's `CALayer` + 120 swaps + readback PASS from an ssh session; no on-screen window there (no WindowServer session for the ssh user) | `review/B1043/egl_probe/` |
| patched GLES2 Irrlicht | **works, headless**: `scripts/build-gles2-darwin.sh`, the x86 patch set + device with a Darwin pbuffer path; 0 errors | "Build (GLES2)" below |
| Irrlicht Darwin window + input (`--egl-window`, the `--egl-x11` parallel) | **works**: `CIrrDeviceFB_Cocoa.mm` (NSWindow + CAMetalLayer, ANGLE window surface, NSEvent keys/modifiers/mouse/wheel, resize, teardown). Proven at the console as `will` (B1044 run 4): a 1280x720 window, keys with Shift/Control, left and right (two-finger) clicks, wheel both ways, reaching the client's hotkeys (W = MoveForward); resize, Option and Command-Q are coded but were not exercised. From ssh it refuses cleanly ("no WindowServer session"). The window becomes key only on the first click (macOS 14+ cooperative activation from a Terminal launch) | "Run" below, `refactor/B1044/README.md` |
| willeq GLES2 build (`willeq`, `gles2_zone_benchmark`) | **works**: both link against ANGLE | "Build (GLES2)" below |
| `gles2_zone_benchmark` (qeynos2, headless) | **runs**: draws/state changes/polys equal the x86 manifest on all 3 frames; frames deterministic run to run | B1043 README |
| visual gate | `--platform mac` is the third golden set (Will's 2026-10-14 ruling): `--capture DIR` stages the five zones + memory-coverage legs and `DIR/manifest_mac.json` (the Mac's own metrics and memory rows); x86 metric differences are reported, not gated. B1044's capture is staged in the evidence directory (goldens_mac/) for Will's bless; it is red on one ledger ratchet (qeynos2 unowned 1.04 % of the ledger, bound 1 %) | B1044 README |
| memory ledger | malloc-zone backend ON; the Darwin reading is `phys_footprint` (Will's 2026-10-14 ruling, option 1: platform-scoped figures; "Memory on macOS" below). Every snapshot carries it (`[memledger] ... phys_footprint= owned_phys_footprint=`, JSON `physFootprintBytes`); the ledger is far below footprint, and reconciling the two is open | B1044 README |
| the rest of the GLES2 tree (`model_viewer`, tools, graphics tests) | **builds; suite 5,631 of 5,633 pass** (168 skipped): `NameTagBatchTest.BatchIsPixelIdenticalToPerNodeTextSceneNodes` differs by 615 px in its densest pose, and `visual_gate` needs Pillow or ImageMagick, neither installed for the build user | B1044 README |

## One-time setup

    xcode-select --install          # or a full Xcode
    brew install cmake ninja fmt glm jsoncpp cereal bash openssl@3

**Homebrew's `bash` must come first on `PATH`** when running ctest: the rig
wrappers that `CrowdObservationControls` drives need bash >= 4, and macOS's
`/bin/bash` is 3.2 (that test says so by name if it finds 3.2).
`FileSizeCeiling`'s script runs under either.

**The NAS.** The assets live on the `projects` SMB share. On the mini,
`~/bin/mount_projects.sh` (idempotent) mounts it at `~/projects`, so the
trees are `~/projects/EverQuestP1999` and `~/projects/EverQuest_ETC1`, and
the pristine Irrlicht is `~/projects/irrlicht-1.8.5`.
**Turn off the SMB directory cache before mounting**, or large directories
list wrong: with the default, `ls ~/projects/EverQuestP1999` returned the
first 1,000 names eight times over and no `snd*.pfs`, so every PFS-sound
test failed.

    printf '[default]\ndir_cache_off=yes\n' > ~/Library/Preferences/nsmb.conf
    umount ~/projects; ~/bin/mount_projects.sh
    ls ~/projects/EverQuestP1999 | sort | uniq -d | head   # must print nothing

## ANGLE (once per machine)

Built from source with depot_tools; needs only git, python3 and Xcode (no
Homebrew package). Pinned commit:
`25078a7740fdebf86b5396ec971aaebf418ce01d` (2026-10-12, "ANGLE 2.1.29094").

    mkdir -p ~/willeq-angle && cd ~/willeq-angle
    git clone --depth 1 https://chromium.googlesource.com/chromium/tools/depot_tools.git
    export PATH=$HOME/willeq-angle/depot_tools:$PATH
    git clone https://chromium.googlesource.com/angle/angle && cd angle
    git checkout 25078a7740fdebf86b5396ec971aaebf418ce01d
    python3 ~/willeq-angle/angle/scripts/bootstrap.py && gclient sync --no-history -j8
    gn gen out/Release --args='is_debug=false is_component_build=false angle_enable_metal=true angle_enable_gl=false angle_enable_vulkan=false angle_enable_swiftshader=false angle_build_tests=false angle_has_histograms=false target_cpu="arm64"'
    nice -n 10 autoninja -j 8 -C out/Release libEGL libGLESv2

Then stage a prefix. ANGLE names its dylibs `./libEGL.dylib` (Chrome ships
them next to the app binary); a dev prefix needs `@rpath/` names:

    P=~/willeq-angle/prefix; mkdir -p $P/lib && cp -R include $P/include
    for l in libEGL libGLESv2; do cp out/Release/$l.dylib $P/lib/
        install_name_tool -id @rpath/$l.dylib $P/lib/$l.dylib
        codesign --force -s - $P/lib/$l.dylib; done

On the M4: checkout + sync about 6 min (17 GB tree), build 8 min 16 s at
`-j8` (3,772 steps; the targets pull in SwiftShader and the Vulkan validation
layers as dependencies even with those backends off), prefix 18 MB. Renderer
string: `ANGLE (Apple, ANGLE Metal Renderer: Apple M4, ...)`,
`OpenGL ES 3.0`, EGL 1.5.

## Build (headless)

From a checkout (or an rsynced tree) on the Mac:

    ln -sfn ~/projects/EverQuestP1999 data/eq_client
    ln -sfn ~/projects/EverQuest_ETC1 data/eqe1_client
    PATH=/opt/homebrew/bin:$PATH cmake -S . -B build -G Ninja \
        -DCMAKE_BUILD_TYPE=Release -DEQT_GRAPHICS=OFF -DEQT_BUILD_TESTS=ON \
        -DOPENSSL_ROOT_DIR=/opt/homebrew/opt/openssl@3
    PATH=/opt/homebrew/bin:$PATH cmake --build build -j8

The memory ledger option stays at its default (ON); the configure prints
`Memory ledger: ON requested; macOS has no glibc allocator entry points --
compiled in its OFF shape`, plus one `NOT FOUND`/`not built` STATUS line each
for EGL/GLESv2 and the Lima shim.

## Build (GLES2)

    PATH=/opt/homebrew/bin:$PATH ANGLE_DIR=~/willeq-angle/prefix \
        ANGLE_OUT=~/willeq-angle/prefix/lib ./scripts/build-gles2-darwin.sh

It builds the patched Irrlicht into `third_party/irrlicht-gles2-darwin/`, then
configures `build-gles2/` with `-DEQT_GRAPHICS=ON -DEQT_GLES2=ON -DEQT_DRM=OFF
-DEQT_GLES2_IRRLICHT_DIR=... -DEQT_ANGLE_DIR=... -DEQT_ANGLE_LIB_DIR=...`
(the fetched etc2comp minimum is patched locally for CMake 4, with no global
policy override) and builds `willeq` and `gles2_zone_benchmark`, with graphics
tests configured ON. B1043's clean baseline on the M4 was 236 s
(`--irrlicht-only` for the library alone, about 1 min).

## Run

    ./build/bin/willeq --no-graphics --help
    ./build/bin/willeq --no-graphics -c <config.json>

Without a flag the GLES2 device takes the ANGLE/Metal pbuffer (headless; the
benchmark, the visual gate and CI). `--egl-window` opens a Cocoa window
instead (B1044, `docker/irrlicht-drm-x86/CIrrDeviceFB_Cocoa.mm`): an NSWindow
whose content view hosts a `CAMetalLayer` with `contentsScale` 1, so the EGL
window surface is exactly `-r W H` pixels (the compositor scales it on a
Retina display), ANGLE's Metal display, and NSEvent input mapped to the same
Irrlicht events the X11 device sends (`cocoa_keycode_map.h` is the key map,
from the SDK's `kVK_*` codes; Option is Alt; Command-Q or the close button
quits; the window is resizable and the driver gets `OnResize`). `--egl-x11`
on macOS prints a one-line hint and means `--egl-window`.
`gles2_zone_benchmark --egl-window` renders its frames into the same window.

    ./build-gles2/bin/willeq --egl-window -r 1280 720 -c <config.json> \
        --eqe1-path ~/projects/EverQuest_ETC1

Started from Terminal, the window comes up behind it and takes keys only after
the first click inside it (macOS 14+ cooperative activation; a bundled `.app`
would activate on launch). A window needs the run user's WindowServer (Aqua) session: from ssh
`launchctl managername` says `Background`, the device logs `EGL (Cocoa): no
WindowServer session: run this at the console` and falls back to the pbuffer.
Input tracing: `WILLEQ_INPUT_TRACE=1` prints one `[cocoa-input]` line per
event the device delivers plus a `[cocoa-input] summary` line at teardown;
`--log-module=INPUT:TRACE` prints the client's `[INPUT-EVENT]` line per event.

For another user (the console user is not the build user on the mini),
`scripts/macos/stage_window_bundle.sh` stages `bin/`, ANGLE's `lib/` (rpath
`@executable_path/../lib`, ad-hoc re-signed), `config/`, `data/` and
`scripts/macos/run_window_proof.sh` into `/Users/Shared/willeq-b1044`; that
script runs the windowed benchmark and the windowed client, screenshots both
and writes `out/summary.txt`.

## Benchmark

The visual gate's invocation (`scripts/ci/visual_gate.py` `run_benchmark`:
the manifest's benchmark block, `productionConfig.args`, the zone's
`extraArgs`), qeynos2:

    ./build-gles2/bin/gles2_zone_benchmark --zone qeynos2 --eq-path $PWD/data/eq_client \
        --frames 60 --warmup 5 --res 800 600 --dump-frames 0,1,2 \
        --dump-png-prefix out/qeynos2 --csv out/qeynos2.csv --memory-json out/qeynos2_mem.json \
        --occluder-budget 16 --adaptive-lighting --zone-pp-budget 20 --pp-budget-mode count \
        --player-light-level 9 --camera-pose 201.9,393.4,5.0,180.0,-5.0 \
        --camera-pose -134.9,-238.9,6.0,0.0,-5.0 --camera-pose -202.9,-706.7,20.0,90.0,-5.0

The device logs `DRM: not available on Darwin (B1043) - using the pbuffer EGL
path`, then the ANGLE/Metal pbuffer fallback line (`EGL (pbuffer): Context created and made current (ANGLE/Metal)`), then `GLES2 Driver: ANGLE (Apple, ANGLE Metal
Renderer: Apple M4, ...)`. `visual_gate.py --platform mac --capture <dir>`
stages a Mac set (`manifest_mac.json` + frames) without using x86 pixels or
x86 metrics as a threshold; it still fails on live memory budgets and the
ledger invariants. Draw/state/poly counts equal x86's on 14 of the 15 frames;
timorous frame 0 draws 7 fewer (579 vs 586): with `--no-occlusion-cull` both
platforms give 766/14,875, so the difference is the CPU software occlusion
culler hiding three more regions on arm64 (B1044 README). No Mac goldens are
blessed yet.

## Test

    export PATH=/opt/homebrew/bin:$PATH
    export EQ_CLIENT_PATH=$HOME/projects/EverQuestP1999
    export WILLEQ_EQE1_PATH=$HOME/projects/EverQuest_ETC1
    ctest --test-dir build -LE hygiene -j8 --output-on-failure
    ctest --test-dir build -L hygiene

`EQ_CLIENT_PATH` and `WILLEQ_EQE1_PATH` point the asset tests at the NAS
mount (their default is the Linux `/mnt/projects/...` path); without them
more asset tests SKIP, nothing fails. A green run on the Mac still skips
more than Linux's headless tree. B1039's headless run had the ledger OFF and
its ledger-dependent audio cases skipped, plus tests hard-coding
`/mnt/projects` and ordinary absent-capture/skip-by-design cases;
`refactor/B1039/README.md` names them. B1044's full GLES2 suite on the M4 (`ctest -LE hygiene`): 2 failed of 5,633,
168 skipped; the classification of every case that failed on the way is in
`refactor/B1044/README.md`. `zone_content_sweep_smoke` SKIPs on macOS (a
Linux rig: GNU `stdbuf`, `/proc` RssAnon).

## Memory on macOS

Will ruled on 2026-10-14 (#364, option 1): memory figures are
platform-scoped. Each platform's manifest section carries its own rows
(`textureCacheBytesLimit`, the Mac's `physFootprintBytes`), and the visual
gate has a third golden set, `--platform mac` (`tests/goldens/mac/`,
manifest `platforms.mac`), which Will blesses from a `--capture` staging.

The Darwin figure that stands where Linux reads `RssAnon` is Mach's
`task_vm_info.phys_footprint`: `task_info(mach_task_self(), TASK_VM_INFO,
...)` (`src/client/core/memory_ledger_darwin.h`). It is the task's dirty,
compressed and swapped memory, including IOSurface/Metal memory charged to
the task (what Activity Monitor shows as "Memory"); `resident_size` from the
same call is the `VmRSS` counterpart. On Darwin `rssAnonBytes` stays 0, so
none of the Linux bounds (the 256 MiB ceiling, slack, `[heap]` coverage) read
it; `owned_phys_footprint = phys_footprint - gpu_driver` is the counterpart of
`owned_rss_anon`. The ledger (malloc-zone hooks, `memory_ledger_backend.h`)
charges only what reaches the executable's allocator wrappers, so it sits far
below the footprint (ANGLE/Metal and libSystem allocate around it); that gap
is measured per zone in the B1044 README, not yet reconciled.

## Float results are not x86's bit for bit

Apple clang on arm64 fuses `a*b+c` into one FMA unless told not to, so
`CMakeLists.txt` passes `-ffp-contract=off` on Darwin; with it,
`HCMapRaySetGolden` matches the x86 goldens in every zone (with the default
contraction 4 of 4 differed, one by a flipped line-of-sight verdict). Apple's
libm is a different matter: `GameStateIdentity`'s pinned collision-map
digests depend on `sin`/`cos` (`HCMap`'s placeable rotation) and do NOT match
on the Mac. Both suites look for the server maps at
`/mnt/projects/akk-stack/server/maps/base` and SKIP on the Mac; do not point
`WILLEQ_MAP_FIXTURES` at the NAS copy there and read the
`GameStateIdentity` failures as a defect -- they are the platform's libm.

## What differs on Darwin in the code

The B1043 differences were `__linux__`/`__APPLE__` guards or CMake conditions;
its changed Linux TUs preprocessed identically. B1044 also changes the shared
GLES2 header rule, so Linux CI must verify that regression surface.

- The memory ledger: malloc-zone allocation-class charges are ON (B1044);
  `[memarena]`'s mallinfo2 line remains Linux-only. The Darwin reading is
  `phys_footprint` ("Memory on macOS" above). The T504 `gpu_driver` caller
  classifier (a block whose malloc/new return address lies inside a
  GL/driver module -- Mesa on Linux, ANGLE/Apple GPU on Darwin) runs here
  too, off the dyld image table: `dl_iterate_phdr` does not exist on
  Darwin, so the classifier's table is filled from
  `_dyld_image_count()`/`_dyld_get_image_header()`: one range per image,
  its `__TEXT` segment (slide applied; `__PAGEZERO` is a virtual guard
  range with no code and is never recorded). Recording every non-`__PAGEZERO`
  segment overflows the 1,024-range table on a full dyld image set -- the
  benchmark loads ~380 images whose ~2,400 data segments (libswift*,
  libobjc, libSystem, ...) would push the last images' `__TEXT` past the
  end, which is exactly where the GPU stack's call sites land. The table
  is populated by the first unknown-address rebuild (it fires within the
  first few allocations: dyld has already loaded the initial image set,
  ANGLE's dylibs are link-time images of the client) and rebuilt,
  rate-limited, only when an unknown address appears later (a dlopen'd
  image). Image 0 (the main executable) is
  never a GPU image; the rest are tested against the ANGLE dylibs
  `libEGL` / `libGLESv2` and the frameworks Metal, IOSurface,
  CoreDisplay -- the same `gpu_driver` category, so the row means the
  same thing on both platforms). All of it lives in
  `src/client/core/memory_ledger_gpu_driver_classifier_darwin.cpp` beside
  the Linux classifier in `memory_ledger.cpp`, which keeps a one-line
  `#elif defined(__APPLE__)` arm; the Linux preprocessed output is
  unchanged. The hot path is the Linux arm's shape -- one atomic table load
  + binary search, no `dladdr`, no `backtrace`, no lock. B1062 (#382)
  measured the spec's cost A/B on the mini (per-call ns, the
  `[classifier-cost]` line of `tests/test_gpu_driver_classifier.cpp`;
  the measured figures are quoted from the mini run of that test
  below):
  - **The measured A/B (M4 mini, macOS 15.7.4, 10,000 calls each,
    `[classifier-cost]` line):** `dladdr(pc)` **1987.6 ns/call**
    (10,000/10,000 resolved); `backtrace(8)` + `dladdr` per frame
    **9275.4 ns/call** (80,000 frames resolved); the classifier's own
    table binary search **1.73 ns/call** (10,000/10,000 in a known
    image). The binary search is the hot path -- ~1,150x cheaper than
    dladdr and ~5,375x cheaper than the backtrace path; dladdr and
    backtrace are the alternatives it does not take (the spec's "no
    per-allocation backtrace" is met with the table, at ~2 ns per
    allocation).
  - **The effect on the gate's ratchet** (five-zone capture, the
    `[ledger]` lines of `scripts/ci/visual_gate.py --platform mac`):
    unowned falls from the B1044 red 1.038 % (qeynos2, 56,192 B) to
    **qeynos2 0.098 %, gfaydark 0.057 %, befallen 0.067 %, iceclad
    0.015 %, timorous 0.016 %** -- every zone under the 1 % bound, with
    `gpu_driver` charged 327,024 B (4,357 allocations) on qeynos2.
    `gpu_driver`'s own ledger-budget bound is still unconfigured
    (`[ledger-budget] ... NO BUDGET CONFIGURED for ... gpu_driver`), so
    the row is reported, not gated, until a budget exists (FOLLOW-UP).
- The window: `CIrrDeviceFB_Cocoa.mm` defines the three "X11" members
  (`initEGLX11`, `pollX11Events`, `closeX11Window`) on Darwin, so
  `CIrrDeviceFB.cpp`'s run()/present()/destructor are unchanged; its state
  object lives in `x11Display_`. The frameworks it needs (Cocoa, QuartzCore,
  CoreGraphics) ride on the Darwin `EGL` CMake target.
- `PlacedObjectRecord` is 160 bytes, not 168: libc++'s `std::string` is 24
  bytes (libstdc++'s is 32); `tests/test_placed_object_record.cpp` pins both.
- Thread names: the T504 names go through `eqt::setCurrentThreadName`
  (`include/common/thread_name.h`), the one-argument `pthread_setname_np`
  on macOS. The tree has the 6 thread names set this way.
- The UDP receive buffer: no `SO_RCVBUFFORCE`; the `SO_RCVBUF` fallback
  runs and the INFO line says `this platform has no SO_RCVBUFFORCE`.
- The shipped-data resolver finds the binary's directory with
  `_NSGetExecutablePath` (there is no `/proc/self/exe`).
- `test_headless_shutdown` uses `pipe()` + `FD_CLOEXEC` (no `pipe2`).
- GLES2 (B1043): on Darwin the literal link names `EGL GLESv2 drm gbm X11`
  are CMake targets -- ANGLE's two dylibs and three empty INTERFACE
  libraries -- so no GLES2 target's link line changes; `EQT_HAS_GLES2` is
  defined in the `IF(APPLE)` branch too. The patched Irrlicht's device
  (`docker/irrlicht-drm-x86/CIrrDeviceFB*`) compiles its DRM/GBM/evdev/VT/X11
  code only off Darwin; `CIrrDeviceFB_Darwin.cpp` supplies the Darwin
  stubs, and `initEGLSurfaceless()` asks ANGLE for its Metal display.
  `constrained_texture_cache*.cpp` include GLES2 headers (`gl2.h`,
  `gl2ext.h`) on every platform; `flock_controller.cpp` defines
  `GLM_ENABLE_EXPERIMENTAL` for Homebrew's glm 1.x.
