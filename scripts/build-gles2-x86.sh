#!/bin/bash
# H01: Build the patched Irrlicht GLES2 backend for x86_64, targeting the
# same COpenGLES2Driver/CIrrDeviceFB code the ARM Docker build uses, so we can
# run the real production GLES2 rendering path on this dev VM under Xvfb with
# mesa/llvmpipe providing ES 2.0.
#
# This adapts docker/irrlicht-drm-patch.py (reused as-is — it is already
# architecture-neutral) plus the driver sources under docker/irrlicht-drm/,
# with one addition: CIrrDeviceFB is forked (docker/irrlicht-drm-x86/) to add
# a Mesa "surfaceless" EGL fallback for when there is no accessible/connected
# DRM display (the normal case for a headless x86 dev VM) — see the comment
# block at the top of docker/irrlicht-drm-x86/CIrrDeviceFB.h for why this is
# a fork rather than a flag on the shared ARM files.
#
# Steps (see refactor/H01_gles2_x86.md for the full design):
#   1. Copy the pristine Irrlicht 1.8.5 source (read-only reference at
#      /mnt/projects/irrlicht-1.8.5) into a local, gitignored build area.
#   2. Apply irrlicht-drm-patch.py (unmodified — shared with ARM).
#   3. Drop in the x86 driver sources (COpenGLES2Driver/COGLES2* shared with
#      ARM as-is; CIrrDeviceFB.* from the x86 fork).
#   4. Edit the Makefile the same way the ARM Dockerfile does (add
#      IRRMESHOBJ, disable desktop GL compilation) — SKIPPING the ARM-only
#      libpng NEON object list and the armv7/neon CFLAGS/CXXFLAGS.
#   5. `make staticlib` with the host gcc/g++ toolchain to produce a native
#      x86_64 libIrrlicht.a + headers.
#
# Idempotent: safe to re-run — always wipes and rebuilds the local Irrlicht
# copy from the pristine source and patch scripts.
#
# Usage:
#   ./scripts/build-gles2-x86.sh            # build patched Irrlicht + build-gles2/ tree
#   ./scripts/build-gles2-x86.sh --irrlicht-only   # only (re)build libIrrlicht.a

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

PRISTINE_IRRLICHT="/mnt/projects/irrlicht-1.8.5"
IRRLICHT_BUILD_DIR="$PROJECT_DIR/third_party/irrlicht-gles2-x86"
JOBS="${JOBS:-24}"
# B1046: hermes kanban workers run in a transient systemd scope with MemoryMax=4 GiB
# (hermes tools/process_registry.py, hard cap). 24 compilers there = cc1plus OOM-killed
# (2026-10-13, worker run 631). Clamp to the enclosing cgroup's memory.max at 1 GiB per job.
cg_mem_max() {
  local rel; rel="$(sed -n 's/^0:://p' /proc/self/cgroup 2>/dev/null)"
  [ -n "$rel" ] && cat "/sys/fs/cgroup$rel/memory.max" 2>/dev/null
}
CG_MAX="$(cg_mem_max)"
if [ -n "$CG_MAX" ] && [ "$CG_MAX" != "max" ]; then
  CG_JOBS=$(( CG_MAX / (1024 * 1024 * 1024) ))
  [ "$CG_JOBS" -lt 1 ] && CG_JOBS=1
  if [ "$JOBS" -gt "$CG_JOBS" ]; then
    echo "B1046: cgroup memory.max=$CG_MAX bytes -> clamping JOBS $JOBS -> $CG_JOBS"
    JOBS=$CG_JOBS
  fi
fi

IRRLICHT_ONLY=0
T122_TEXPROBE=0
T130_AUDIO_TIDPROBE=0
for arg in "$@"; do
    case "$arg" in
        --irrlicht-only) IRRLICHT_ONLY=1 ;;
        --t122-texprobe)
            # T122: compile the lock-free texture-lifecycle ring probe into
            # the driver (WILLEQ_T122_TEXPROBE) and keep -g so a resulting
            # core dump can be symbolized. OFF unless explicitly requested —
            # a plain ./build-gles2-x86.sh run never defines this macro.
            T122_TEXPROBE=1
            ;;
        --t130-audio-tidprobe)
            # T130: compile in a one-shot write of the audio callback
            # thread's tid to a file (WILLEQ_T130_AUDIO_TID_PROBE), for use
            # with an out-of-process LD_PRELOAD allocator interposer
            # (review/T130/audio_alloc_interposer.c) that attributes
            # malloc/calloc/realloc/free calls to that specific thread.
            # Diagnostic only, allocation-free, OFF unless explicitly
            # requested — a plain ./build-gles2-x86.sh run never defines
            # this macro. Distinct from the always-on
            # EQT::Audio::AudioCallbackProbe callback-timing counters
            # (audio_callback_probe.h), which need no flag.
            T130_AUDIO_TIDPROBE=1
            ;;
        --help|-h)
            echo "Usage: $0 [--irrlicht-only] [--t122-texprobe] [--t130-audio-tidprobe]"
            exit 0
            ;;
    esac
done

PROBE_CXXFLAGS=""
if [ "$T122_TEXPROBE" -eq 1 ]; then
    # T126: -fno-omit-frame-pointer is load-bearing for the TEXPARAMS
    # return-address chain (__builtin_return_address(N>0) needs a real
    # frame-pointer chain to walk beyond frame 0 -- see t122_texprobe.h).
    echo "*** T122 probe build: WILLEQ_T122_TEXPROBE defined, -g + -fno-omit-frame-pointer kept for core symbolization / call-chain capture ***"
    PROBE_CXXFLAGS="-g -fno-omit-frame-pointer -DWILLEQ_T122_TEXPROBE"
fi

if [ ! -d "$PRISTINE_IRRLICHT" ]; then
    echo "ERROR: pristine Irrlicht source not found at $PRISTINE_IRRLICHT" >&2
    exit 1
fi

echo "=== H01: Building patched Irrlicht GLES2 static lib for x86_64 ==="

# ------------------------------------------------------------------
# Step 1: Fresh copy of the pristine source into the gitignored build area.
# Never touch $PRISTINE_IRRLICHT itself.
# ------------------------------------------------------------------
echo "--- Copying pristine Irrlicht source (read-only reference untouched) ---"
rm -rf "$IRRLICHT_BUILD_DIR"
mkdir -p "$IRRLICHT_BUILD_DIR"
cp -r "$PRISTINE_IRRLICHT"/. "$IRRLICHT_BUILD_DIR"/

# ------------------------------------------------------------------
# Step 2: Drop in the driver source files (shared with ARM, unmodified)
# and the x86-forked CIrrDeviceFB.
# ------------------------------------------------------------------
echo "--- Installing GLES2 driver sources ---"
DRM_SRC="$PROJECT_DIR/docker/irrlicht-drm"
DRM_X86_SRC="$PROJECT_DIR/docker/irrlicht-drm-x86"
DEST_SRC="$IRRLICHT_BUILD_DIR/source/Irrlicht"

cp "$DRM_SRC/COpenGLES2Driver.h"        "$DEST_SRC/COpenGLES2Driver.h"
cp "$DRM_SRC/COpenGLES2Driver.cpp"      "$DEST_SRC/COpenGLES2Driver.cpp"
cp "$DRM_SRC/COGLES2Texture.h"          "$DEST_SRC/COGLES2Texture.h"
cp "$DRM_SRC/COGLES2Texture.cpp"        "$DEST_SRC/COGLES2Texture.cpp"
cp "$DRM_SRC/COGLES2Shaders.h"          "$DEST_SRC/COGLES2Shaders.h"
cp "$DRM_SRC/COGLES2Shaders.cpp"        "$DEST_SRC/COGLES2Shaders.cpp"
cp "$DRM_SRC/COGLES2MaterialRenderer.h" "$DEST_SRC/COGLES2MaterialRenderer.h"
cp "$DRM_SRC/COGLES2MaterialRenderer.cpp" "$DEST_SRC/COGLES2MaterialRenderer.cpp"
cp "$DRM_SRC/gl_call_counters.h"        "$DEST_SRC/gl_call_counters.h"
cp "$DRM_SRC/gpu_sync_probe.h"          "$DEST_SRC/gpu_sync_probe.h"
cp "$DRM_SRC/drm_key_translation.h"     "$DEST_SRC/drm_key_translation.h"
cp "$DRM_SRC/drm_flip_tracker.h"         "$DEST_SRC/drm_flip_tracker.h"
# B961: the scanout-framebuffer tally (header-only; both device forks
# #include it relative to the TU, exactly like drm_flip_tracker.h).
cp "$DRM_SRC/scanout_buffer_tally.h"     "$DEST_SRC/scanout_buffer_tally.h"
cp "$DRM_SRC/t122_texprobe.h"           "$DEST_SRC/t122_texprobe.h"
# B1054: card-selection decision, shared by both device forks (the ARM fork
# gets it the same way as drm_mode_select.h: a plain cp next to the TU).
cp "$DRM_SRC/drm_card_select.h"         "$DEST_SRC/drm_card_select.h"
# x86 fork (surfaceless EGL fallback) instead of the ARM DRM-only version
cp "$DRM_X86_SRC/CIrrDeviceFB.h"        "$DEST_SRC/CIrrDeviceFB.h"
cp "$DRM_X86_SRC/CIrrDeviceFB.cpp"      "$DEST_SRC/CIrrDeviceFB.cpp"
# P25: X11-windowed EGL backend member functions, split into their own TU to
# keep CIrrDeviceFB.cpp under the file-size warning threshold (same class,
# not a separate device) - x86-only, no ARM equivalent.
cp "$DRM_X86_SRC/CIrrDeviceFB_X11.cpp"  "$DEST_SRC/CIrrDeviceFB_X11.cpp"
# B654: the pure X11-keysym -> EKEY_CODE map, shared by CIrrDeviceFB_X11.cpp
# and the display-less unit test. Copied into the build area next to the TU
# (like the other driver headers) so the TU's relative #include resolves.
cp "$DRM_X86_SRC/x11_keysym_map.h"      "$DEST_SRC/x11_keysym_map.h"

# ------------------------------------------------------------------
# Step 3: Apply the shared DRM/GLES2 compile-config patch (identical to ARM;
# this script is architecture-neutral, no x86-specific fork needed).
# ------------------------------------------------------------------
echo "--- Applying irrlicht-drm-patch.py ---"
python3 "$PROJECT_DIR/docker/irrlicht-drm-patch.py" "$IRRLICHT_BUILD_DIR"

# GLES2-only build: disable desktop GL compilation (same as
# ENABLE_GLES2=ON in docker/Dockerfile.arm-noble).
sed -i 's|^#define _IRR_COMPILE_WITH_OPENGL_|//#define _IRR_COMPILE_WITH_OPENGL_|' \
    "$IRRLICHT_BUILD_DIR/include/IrrCompileConfig.h"

# ------------------------------------------------------------------
# Step 4: Makefile edits (subset of the ARM Dockerfile's sed commands).
# Skipped vs. ARM: the libpng NEON object list (arm/*.o) — x86 doesn't need
# or want NEON intrinsics, and we never copied in libpng's arm/ source dir.
# ------------------------------------------------------------------
echo "--- Patching Makefile (IRRMESHOBJ; no ARM NEON libpng objects) ---"
MAKEFILE="$DEST_SRC/Makefile"
sed -i 's|^IRRMESHOBJ = |IRRMESHOBJ = COpenGLES2Driver.o COGLES2Texture.o COGLES2Shaders.o COGLES2MaterialRenderer.o |' \
    "$MAKEFILE"
# P25: register the split-out X11 backend TU so `make staticlib` compiles and
# archives it (CIrrDeviceFB.o's IRROTHEROBJ entry doesn't cover new files).
sed -i 's|^IRROTHEROBJ = |IRROTHEROBJ = CIrrDeviceFB_X11.o |' \
    "$MAKEFILE"

# ------------------------------------------------------------------
# Step 5: Build static lib with the host x86_64 toolchain (no cross-compiler,
# no ARM sysroot, no NEON flags).
# ------------------------------------------------------------------
# B1042 (#362): honour the same CMAKE_{C,CXX}_COMPILER_LAUNCHER environment
# variables CMake reads (CI exports them as `ccache` on the x86 runners), so
# a stamp-triggered Irrlicht rebuild recompiles only the TUs whose input
# changed. Unset (a developer's host) means plain gcc/g++, exactly as before.
IRR_CC="${CMAKE_C_COMPILER_LAUNCHER:+$CMAKE_C_COMPILER_LAUNCHER }gcc"
IRR_CXX="${CMAKE_CXX_COMPILER_LAUNCHER:+$CMAKE_CXX_COMPILER_LAUNCHER }g++"
echo "--- Building libIrrlicht.a (native x86_64, make -j$JOBS, CXX='$IRR_CXX') ---"
make -C "$DEST_SRC" NDEBUG=1 -j"$JOBS" \
    CXX="$IRR_CXX" CC="$IRR_CC" AR=ar \
    CXXFLAGS="$PROBE_CXXFLAGS" \
    CXXINCS="-I../../include -Izlib -Ijpeglib -Ilibpng -I/usr/include/libdrm" \
    staticlib

mkdir -p "$IRRLICHT_BUILD_DIR/lib" "$IRRLICHT_BUILD_DIR/include"
cp "$IRRLICHT_BUILD_DIR/lib/Linux/libIrrlicht.a" "$IRRLICHT_BUILD_DIR/lib/libIrrlicht.a"
cp -r "$IRRLICHT_BUILD_DIR/include" "$IRRLICHT_BUILD_DIR/irrlicht-include-tmp"
rm -rf "$IRRLICHT_BUILD_DIR/include"
mv "$IRRLICHT_BUILD_DIR/irrlicht-include-tmp" "$IRRLICHT_BUILD_DIR/include"

echo "=== libIrrlicht.a built: $IRRLICHT_BUILD_DIR/lib/libIrrlicht.a ==="

# B654 (issue #56): stamp the inputs this lib was built from, so the CI build
# job can tell a current warm lib (GIT_CLEAN_FLAGS keeps it between pipelines)
# from a stale one -- a committed change to the device sources no longer leaves
# the whole pipeline running old code. The stamp is over content (relative path
# + sha256, sorted), stable across checkouts of the same commit.
bash "$PROJECT_DIR/scripts/ci/irrlicht_input_stamp.sh" write "$IRRLICHT_BUILD_DIR/irrlicht_input_stamp.txt"

if [ "$IRRLICHT_ONLY" -eq 1 ]; then
    exit 0
fi

# ------------------------------------------------------------------
# Configure + build the second build tree (build-gles2/), EQT_GLES2=ON,
# linking against the local patched Irrlicht instead of any system copy.
# There is no other kind of Irrlicht to link against any more (P30, desktop-GL
# system libirrlicht-dev/X11-GLX was removed) -- `<slot>/build` (the tree
# EXECUTOR_PROTOCOL.md's verification bar builds) is configured the same way
# (EQT_GLES2=ON, tests on); this script's `build-gles2/` is the smaller
# tests-off tree the visual gate (scripts/ci/visual_gate.sh) targets. This
# script's own job is only to (re)bootstrap the local patched Irrlicht static
# lib both trees link against (third_party/irrlicht-gles2-x86/), not to
# distinguish a "GLES2 tree" from some other still-supported desktop tree.
# ------------------------------------------------------------------
# B1012: CI builds one tree per ref (GLES2_BUILD_DIR=<checkout>/build-gles2/ref/<slug>);
# a developer's checkout keeps the default build-gles2/. B1042: the launcher
# env vars (above) are passed explicitly too, so a re-run over an existing
# tree picks them up (CMake reads the env vars only on a tree's FIRST configure).
BUILD_DIR="${GLES2_BUILD_DIR:-$PROJECT_DIR/build-gles2}"
echo "=== Configuring $BUILD_DIR (EQT_GLES2=ON) ==="
mkdir -p "$BUILD_DIR"
MAIN_CXXFLAGS=""
if [ "$T130_AUDIO_TIDPROBE" -eq 1 ]; then
    echo "*** T130 probe build: WILLEQ_T130_AUDIO_TID_PROBE defined ***"
    MAIN_CXXFLAGS="-DWILLEQ_T130_AUDIO_TID_PROBE"
fi

cmake -S "$PROJECT_DIR" -B "$BUILD_DIR" \
    -DEQT_GRAPHICS=ON \
    -DEQT_GLES2=ON \
    -DEQT_DRM=OFF \
    -DEQT_GLES2_IRRLICHT_DIR="$IRRLICHT_BUILD_DIR" \
    -DEQT_BUILD_TESTS=OFF \
    -DWITH_RDP=OFF \
    -DCMAKE_CXX_FLAGS="$MAIN_CXXFLAGS" \
    ${CMAKE_C_COMPILER_LAUNCHER:+-DCMAKE_C_COMPILER_LAUNCHER=$CMAKE_C_COMPILER_LAUNCHER} \
    ${CMAKE_CXX_COMPILER_LAUNCHER:+-DCMAKE_CXX_COMPILER_LAUNCHER=$CMAKE_CXX_COMPILER_LAUNCHER}

echo "=== Building willeq + gles2_zone_benchmark ($BUILD_DIR, -j$JOBS) ==="
cmake --build "$BUILD_DIR" --target willeq -j"$JOBS"
cmake --build "$BUILD_DIR" --target gles2_zone_benchmark -j"$JOBS"

echo "=== Done. Binaries in $BUILD_DIR/bin/ ==="
