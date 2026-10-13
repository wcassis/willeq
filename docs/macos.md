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
| Irrlicht Darwin window + input (the `--egl-x11` parallel) | **not written** (FIX card): Cocoa window/CAMetalLayer surface, Cocoa key/mouse input | #363 |
| willeq GLES2 build (`willeq`, `gles2_zone_benchmark`) | **works**: both link against ANGLE | "Build (GLES2)" below |
| `gles2_zone_benchmark` (qeynos2, headless) | **runs**: draws/state changes/polys equal the x86 manifest on all 3 frames; frames deterministic run to run | B1043 README |
| visual gate | **not on the Mac yet**: the gate compares pixel-exact; Metal vs the x86 llvmpipe goldens differ in 14.7-40.8 % of pixels at MAE 0.07-0.18 (PSNR 50-55 dB). A `mac` platform with its own goldens is Will's bless | B1043 README |
| memory ledger | **OFF shape** on Darwin (no glibc allocator entry points; no `malloc_zone` backend). `RssAnon` has no Darwin reading; the candidate is `phys_footprint` (peak 162-171 MiB on the qeynos2 benchmark run, `/usr/bin/time -l`) | B1039, B1043 |
| the rest of the GLES2 tree (`model_viewer`, tools, graphics tests) | **not attempted** | FIX card |

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
-DEQT_GLES2_IRRLICHT_DIR=... -DEQT_ANGLE_DIR=... -DEQT_ANGLE_LIB_DIR=...
-DCMAKE_POLICY_VERSION_MINIMUM=3.5` (Homebrew's CMake 4 refuses the
FetchContent'd etc2comp's old `cmake_minimum_required`) and builds `willeq`
and `gles2_zone_benchmark`. Clean, on the M4: 236 s for all of it
(`--irrlicht-only` for the library alone, about 1 min).

## Run

    ./build/bin/willeq --no-graphics --help
    ./build/bin/willeq --no-graphics -c <config.json>

The GLES2 `willeq` binary starts and parses options; on the Mac it has no
window or input yet (the Darwin device is pbuffer-only), so a rendered
interactive session is the FIX card's.

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
path`, then the surfaceless fallback's lines (their "llvmpipe, x86/Xvfb"
wording is the x86 fork's), then `GLES2 Driver: ANGLE (Apple, ANGLE Metal
Renderer: Apple M4, ...)`. Do not run `visual_gate.py` itself on the Mac and
read its pixel failures as a regression: there are no Mac goldens.

## Test

    export PATH=/opt/homebrew/bin:$PATH
    export EQ_CLIENT_PATH=$HOME/projects/EverQuestP1999
    export WILLEQ_EQE1_PATH=$HOME/projects/EverQuest_ETC1
    ctest --test-dir build -LE hygiene -j8 --output-on-failure
    ctest --test-dir build -L hygiene

`EQ_CLIENT_PATH` and `WILLEQ_EQE1_PATH` point the asset tests at the NAS
mount (their default is the Linux `/mnt/projects/...` path); without them
more asset tests SKIP, nothing fails. A green run on the Mac still skips
more than Linux's headless tree: the ledger-dependent audio cases (the
ledger is off), and the tests that hard-code a `/mnt/projects` path with no
override, on top of Linux's own skips (absent `data/captures/` replays, the
no-renderer gate tests, skip-by-design cases). `refactor/B1039/README.md`
names every one. The GLES2 tree's tests are not built on the Mac yet.

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

Every difference is a `__linux__`/`__APPLE__` guard or a CMake condition;
the Linux build compiles the same code it did before (B1043 checked the
changed TUs' preprocessed output, base vs branch: identical).

- The memory ledger: the OFF shape (above); `[memarena]`'s mallinfo2 line
  is Linux-only, like its only caller.
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
  `constrained_texture_cache*.cpp` include ANGLE's GLES2 headers (`gl2.h`,
  `gl2ext.h`) on Darwin (Linux still includes Mesa's desktop `gl.h` there); `flock_controller.cpp` defines
  `GLM_ENABLE_EXPERIMENTAL` for Homebrew's glm 1.x.
