#!/usr/bin/env bash
# B1056 (#376) -- build willeq ON lane 4 (the Radxa Cubie A7S) and publish it where
# the lane's rigs expect it.
#
# Why a native build: lane 4 is Debian 11 bullseye (glibc 2.31) and the aarch64
# lanes' cross build (scripts/build-arm64-noble.sh, a noble sysroot) links against
# GLIBC_2.38, so ci-arm64/<sha>/bin does not start there. Until a bullseye-sysroot
# cross image exists, the lane builds its own binary with the board's gcc 10 /
# cmake 3.18 and publishes it under $PI_LANE_PUBLISH (scripts/pi/env.sh, lane 4:
# /mnt/projects/fable/ci-arm64-bullseye/<sha>/) in the SAME shape as ci-arm64/:
# stripped bin/, that commit's config/ + data/, BUILD_INFO, by-branch/<branch>.
#
# Runs ON THE DEVICE (ssh a7s), which mounts the NFS share at /mnt/projects:
#   bash /mnt/projects/fable/wt/<wt>/scripts/sbc/a7s/build_native.sh [--no-publish] [--irrlicht-only]
#   JOBS=4 (default; 4 GB + 2 GB zram, B1046's 1 GiB-per-job rule)
#   BUILD_ROOT=~/build (eMMC; a debug tree does not fit the 1.5 G /mnt/ram tmpfs)
# The source tree is whatever checkout this script lives in; nothing is written
# into it (the patched Irrlicht and the build tree both live under BUILD_ROOT).
#
# Steps mirror docker/Dockerfile.arm64-noble's Irrlicht stage (ARM DRM-only
# CIrrDeviceFB from docker/irrlicht-drm/, libpng's arm/ NEON objects from the
# vendor store, GLES2-only compile config) and its project configure
# (EQT_DRM=ON, EQT_GLES2=ON, RelWithDebInfo, tests off, RDP off), minus the
# sysroot and the -static-libstdc++ (the binary runs on the box that built it).
# EGL_NO_X11 / MESA_EGL_NO_X11_HEADERS: bullseye's libglvnd 1.3 eglplatform.h
# includes <X11/Xlib.h> unless told not to (glvnd >= 1.4 needs USE_X11 instead),
# and Xlib's `None` macro breaks `CachedTexture::Source::None`. Noble never saw it.
# -ldl: glibc < 2.34 keeps dlsym in libdl (2.34 folded it into libc), and the tree's
# link lines do not name it; the cross Dockerfile passes it the same way.
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SRC="$(cd "$HERE/../../.." && pwd)"
JOBS="${JOBS:-4}"
BUILD_ROOT="${BUILD_ROOT:-$HOME/build}"
IRR_DIR="$BUILD_ROOT/irrlicht-gles2-a7s"
BUILD_DIR="$BUILD_ROOT/willeq-a7s"
VENDOR="${VENDOR_STORE:-/mnt/projects/fable/ci-arm/vendor}"
PRISTINE="${PRISTINE_IRRLICHT:-/mnt/projects/irrlicht-1.8.5}"
PUBLISH_ROOT="${PI_LANE_PUBLISH:-/mnt/projects/fable/ci-arm64-bullseye}"
KEEP="${PUBLISH_KEEP:-6}"
PUBLISH=1; IRR_ONLY=0
for a in "$@"; do case "$a" in
    --no-publish) PUBLISH=0 ;;
    --irrlicht-only) IRR_ONLY=1 ;;
    -h|--help) sed -n 2,24p "$0"; exit 0 ;;
    *) echo "unknown option $a"; exit 1 ;;
esac; done
[ "$(uname -m)" = aarch64 ] || { echo "build_native.sh runs on the lane-4 board (aarch64), not here"; exit 2; }
[ -f "$VENDOR/irrlicht-1.8.5.zip" ] || [ -d "$PRISTINE" ] || { echo "no Irrlicht source: neither $VENDOR/irrlicht-1.8.5.zip nor $PRISTINE (share mounted?)"; exit 2; }
export PATH="/usr/lib/ccache:$PATH"
SHA="$(git -C "$SRC" rev-parse HEAD)"; BRANCH="$(git -C "$SRC" rev-parse --abbrev-ref HEAD)"
echo "=== lane 4 native build: $SRC @ ${SHA:0:8} ($BRANCH), -j$JOBS, trees under $BUILD_ROOT ==="

# --- 1. patched GLES2/DRM Irrlicht (static), rebuilt when its inputs changed ---
STAMP="$IRR_DIR/irrlicht_input_stamp.txt"   # scripts/ci/irrlicht_input_stamp.sh (B654): content digest of the driver inputs
if [ -f "$IRR_DIR/lib/libIrrlicht.a" ] && (cd "$SRC" && bash scripts/ci/irrlicht_input_stamp.sh verify "$STAMP" >/dev/null 2>&1); then
    echo "--- libIrrlicht.a is current (stamp matches); skipping"
else
    echo "--- building libIrrlicht.a from the vendor archive (B970) + docker/irrlicht-drm/ ---"
    rm -rf "$IRR_DIR"; mkdir -p "$IRR_DIR"
    if [ -f "$VENDOR/irrlicht-1.8.5.zip" ]; then
        # One 21 MB file over the share beats 20k small files (lane 4's share is sshfs).
        (cd "$VENDOR" && grep irrlicht-1.8.5.zip "$SRC/docker/vendor.sha256" | sha256sum -c - >/dev/null)
        python3 -c "import zipfile,sys; zipfile.ZipFile(sys.argv[1]).extractall(sys.argv[2])" "$VENDOR/irrlicht-1.8.5.zip" "$BUILD_ROOT/.irr-unzip.$$"
        mv "$BUILD_ROOT/.irr-unzip.$$/irrlicht-1.8.5"/* "$IRR_DIR"/; rm -rf "$BUILD_ROOT/.irr-unzip.$$"
    else
        cp -r "$PRISTINE"/. "$IRR_DIR"/
    fi
    tar xJf "$VENDOR/libpng-1.6.37.tar.xz" -C "$IRR_DIR" libpng-1.6.37/arm
    cp -r "$IRR_DIR/libpng-1.6.37/arm" "$IRR_DIR/source/Irrlicht/libpng/"; rm -rf "$IRR_DIR/libpng-1.6.37"
    D="$SRC/docker/irrlicht-drm"; S="$IRR_DIR/source/Irrlicht"
    for f in CIrrDeviceFB.h CIrrDeviceFB.cpp COpenGLES2Driver.h COpenGLES2Driver.cpp COGLES2Texture.h COGLES2Texture.cpp \
             COGLES2Shaders.h COGLES2Shaders.cpp COGLES2MaterialRenderer.h COGLES2MaterialRenderer.cpp gl_call_counters.h \
             gpu_sync_probe.h drm_key_translation.h t122_texprobe.h drm_mode_select.h; do cp "$D/$f" "$S/$f"; done
    python3 "$SRC/docker/irrlicht-drm-patch.py" "$IRR_DIR"
    sed -i 's|^#define _IRR_COMPILE_WITH_OPENGL_|//#define _IRR_COMPILE_WITH_OPENGL_|' "$IRR_DIR/include/IrrCompileConfig.h"
    sed -i 's|^LIBPNGOBJ = |LIBPNGOBJ = libpng/arm/arm_init.o libpng/arm/filter_neon_intrinsics.o libpng/arm/palette_neon_intrinsics.o |' "$S/Makefile"
    sed -i 's|^IRRMESHOBJ = |IRRMESHOBJ = COpenGLES2Driver.o COGLES2Texture.o COGLES2Shaders.o COGLES2MaterialRenderer.o |' "$S/Makefile"
    make -C "$S" NDEBUG=1 -j"$JOBS" CXX="ccache g++" CC="ccache gcc" AR=ar CXXFLAGS="" \
        CXXINCS="-I../../include -Izlib -Ijpeglib -Ilibpng -I/usr/include/libdrm" staticlib
    mkdir -p "$IRR_DIR/lib"; cp "$IRR_DIR/lib/Linux/libIrrlicht.a" "$IRR_DIR/lib/libIrrlicht.a"
    (cd "$SRC" && bash scripts/ci/irrlicht_input_stamp.sh write "$STAMP")
    echo "--- libIrrlicht.a: $(du -h "$IRR_DIR/lib/libIrrlicht.a" | cut -f1)"
fi
[ "$IRR_ONLY" -eq 1 ] && exit 0

# --- 2. the project ---
cmake -S "$SRC" -B "$BUILD_DIR" -G Ninja \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DCMAKE_CXX_FLAGS_RELWITHDEBINFO='-O2 -g1 -DNDEBUG' \
    -DCMAKE_C_FLAGS='-DEGL_NO_X11 -DMESA_EGL_NO_X11_HEADERS' \
    -DCMAKE_CXX_FLAGS='-DEGL_NO_X11 -DMESA_EGL_NO_X11_HEADERS' \
    -DCMAKE_CXX_STANDARD_LIBRARIES='-ldl -lpthread' \
    -DCMAKE_C_COMPILER_LAUNCHER=ccache -DCMAKE_CXX_COMPILER_LAUNCHER=ccache \
    -DEQT_GRAPHICS=ON -DEQT_DRM=ON -DEQT_GLES2=ON \
    -DIRRLICHT_INCLUDE_DIR="$IRR_DIR/include" -DIRRLICHT_LIBRARY="$IRR_DIR/lib/libIrrlicht.a" \
    -DWITH_RDP=OFF -DEQT_BUILD_TESTS=OFF
TARGETS=(willeq gles2_zone_benchmark gpu_texture_formats gles2_shader_diagnostic gles2_etc1_benchmark nametag_drawcost)
cmake --build "$BUILD_DIR" -j"$JOBS" --target "${TARGETS[@]}"
echo "--- built: $(ls "$BUILD_DIR/bin")"
[ "$PUBLISH" -eq 1 ] || exit 0

# --- 3. publish (same shape as scripts/pi/publish_arm64_build.sh) ---
DEST="$PUBLISH_ROOT/$SHA"; TMP="$PUBLISH_ROOT/.tmp.$SHA.$$"
rm -rf "$TMP"; mkdir -p "$TMP/bin" "$PUBLISH_ROOT/by-branch"
for t in "${TARGETS[@]}"; do cp "$BUILD_DIR/bin/$t" "$TMP/bin/"; strip "$TMP/bin/$t"; done
git -C "$SRC" archive "$SHA" config data | tar -x -C "$TMP"
{
    echo "sha=$SHA"; echo "branch=$BRANCH"; echo "built=$(date -Iseconds) on $(hostname) (native, scripts/sbc/a7s/build_native.sh)"
    echo "lanes=4 (a7s, pvr) -- scripts/pi/env.sh; NOT the noble ci-arm64 binary (glibc 2.31 here)"
    echo "gl=IMG DDK $(ls /usr/lib/libGLESv2_PVR_MESA.so.* | sed 's/.*so\.//') via /usr/local/lib/dri/pvr_dri.so (PI_RUN_ENV)"
    echo "toolchain=$(gcc -dumpfullversion) cmake $(cmake --version | head -1 | awk '{print $3}') $(ldd --version | head -1)"
    file -b "$TMP/bin/willeq"
    echo "tools=$(ls "$TMP/bin" | tr '\n' ' ')"
} > "$TMP/BUILD_INFO"
rm -rf "$DEST"; mv "$TMP" "$DEST"
[ -n "$BRANCH" ] && [ "$BRANCH" != HEAD ] && ln -sfn "../$SHA" "$PUBLISH_ROOT/by-branch/$(echo "$BRANCH" | tr / _)"
ls -1dt "$PUBLISH_ROOT"/*/ | grep -v by-branch | tail -n +"$((KEEP + 1))" | xargs -r rm -rf
echo "=== published $DEST ==="; cat "$DEST/BUILD_INFO"
