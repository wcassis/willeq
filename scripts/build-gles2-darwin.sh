#!/bin/bash
# B1043 (#363): build the patched GLES2 Irrlicht on macOS (Darwin, arm64)
# against ANGLE (EGL + GLES2 over Metal), then optionally configure + build the
# willeq GLES2 tree against it. The Darwin counterpart of
# scripts/build-gles2-x86.sh: the SAME driver sources, the SAME
# docker/irrlicht-drm-patch.py, the SAME x86 device fork
# (docker/irrlicht-drm-x86/), plus:
#   - CIrrDeviceFB_Darwin.cpp (the Darwin member stubs: no DRM/GBM/evdev/X11;
#     the device takes its surfaceless/pbuffer EGL path on ANGLE's display)
#   - three Darwin-only config edits the x86 script does not need:
#       * IrrCompileConfig.h selects the FB device + OGLES2 in the APPLE block
#         (the patch script's edits land in the Linux-only block, so without
#         this Darwin builds the Cocoa/desktop-GL OSX device and an EMPTY
#         COpenGLES2Driver.o -- B1043 census pass 1)
#       * bundled libpng/zlib test TARGET_OS_MAC for Mac OS Classic (fp.h,
#         fdopen=NULL); on a modern SDK it is always 1
#       * PNG_ARM_NEON_OPT=0: arm64 clang defines __ARM_NEON, so libpng
#         references its arm/ NEON filters, which the Irrlicht copy lacks (the
#         ARM Dockerfile adds them; here the portable C filters are used)
#   - BSD tools: perl -pi instead of sed -i; clang; lib/ is lib/Linux/ from the
#     Makefile's `staticlib` target (SYSTEM=Linux), copied to lib/.
# docs/macos.md has the ANGLE recipe this needs. Usage (on the Mac):
#   ANGLE_DIR=~/willeq-angle/angle ./scripts/build-gles2-darwin.sh [--irrlicht-only]
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
[ "$(uname -s)" = Darwin ] || { echo "ERROR: Darwin only (use build-gles2-x86.sh)" >&2; exit 1; }

PRISTINE_IRRLICHT="${PRISTINE_IRRLICHT:-$HOME/projects/irrlicht-1.8.5}"
ANGLE_DIR="${ANGLE_DIR:-$HOME/willeq-angle/angle}"
ANGLE_OUT="${ANGLE_OUT:-$ANGLE_DIR/out/Release}"
IRRLICHT_BUILD_DIR="${IRRLICHT_BUILD_DIR:-$PROJECT_DIR/third_party/irrlicht-gles2-darwin}"
JOBS="${JOBS:-8}"
IRRLICHT_ONLY=0
[ "${1:-}" = "--irrlicht-only" ] && IRRLICHT_ONLY=1

for f in "$PRISTINE_IRRLICHT/include/irrlicht.h" "$ANGLE_DIR/include/EGL/egl.h" "$ANGLE_OUT/libEGL.dylib" "$ANGLE_OUT/libGLESv2.dylib"; do
    [ -e "$f" ] || { echo "ERROR: missing $f" >&2; exit 1; }
done

echo "--- Copying pristine Irrlicht source ---"
rm -rf "$IRRLICHT_BUILD_DIR"; mkdir -p "$IRRLICHT_BUILD_DIR"
cp -R "$PRISTINE_IRRLICHT"/. "$IRRLICHT_BUILD_DIR"/
DRM_SRC="$PROJECT_DIR/docker/irrlicht-drm"
DRM_X86_SRC="$PROJECT_DIR/docker/irrlicht-drm-x86"
DEST_SRC="$IRRLICHT_BUILD_DIR/source/Irrlicht"
source "$SCRIPT_DIR/ci/irrlicht_gles2_files.sh"
copy_irrlicht_gles2_files

echo "--- Applying irrlicht-drm-patch.py (shared with ARM/x86) ---"
python3 "$PROJECT_DIR/docker/irrlicht-drm-patch.py" "$IRRLICHT_BUILD_DIR"
CONFIG_H="$IRRLICHT_BUILD_DIR/include/IrrCompileConfig.h"
perl -pi -e 's|^#define _IRR_COMPILE_WITH_OPENGL_|//#define _IRR_COMPILE_WITH_OPENGL_|' "$CONFIG_H"

echo "--- Darwin config edits ---"
perl -0pi -e 's|#define _IRR_COMPILE_WITH_OSX_DEVICE_\n#endif|#define _IRR_POSIX_API_\n#define _IRR_COMPILE_WITH_FB_DEVICE_\n#define _IRR_COMPILE_WITH_OGLES2_\n#endif|' "$CONFIG_H"
grep -q '^#define _IRR_COMPILE_WITH_OGLES2_' "$CONFIG_H" || { echo "ERROR: Darwin device edit did not apply" >&2; exit 1; }
perl -pi -e 's|defined\(TARGET_OS_MAC\)|(defined(TARGET_OS_MAC) \&\& !defined(__APPLE__))|g' \
    "$DEST_SRC/libpng/pngpriv.h" "$DEST_SRC/zlib/zutil.h"

echo "--- Patching Makefile ---"
MAKEFILE="$DEST_SRC/Makefile"
perl -pi -e 's|^IRRMESHOBJ = |IRRMESHOBJ = COpenGLES2Driver.o COGLES2Texture.o COGLES2Shaders.o COGLES2MaterialRenderer.o |' "$MAKEFILE"
perl -pi -e 's|^IRROTHEROBJ = |IRROTHEROBJ = CIrrDeviceFB_X11.o CIrrDeviceFB_Darwin.o |' "$MAKEFILE"
perl -pi -e 's|^staticlib sharedlib: CXXINCS \+= -I/usr/X11R6/include||' "$MAKEFILE"

echo "--- Building libIrrlicht.a (clang, make -j$JOBS) ---"
nice -n 10 make -C "$DEST_SRC" NDEBUG=1 -j"$JOBS" CXX=clang++ CC=clang AR=ar \
    CXXINCS="-I../../include -Izlib -Ijpeglib -Ilibpng -I$ANGLE_DIR/include -DPNG_ARM_NEON_OPT=0" \
    staticlib
mkdir -p "$IRRLICHT_BUILD_DIR/lib"
cp "$IRRLICHT_BUILD_DIR/lib/Linux/libIrrlicht.a" "$IRRLICHT_BUILD_DIR/lib/libIrrlicht.a"
echo "=== libIrrlicht.a built: $IRRLICHT_BUILD_DIR/lib/libIrrlicht.a ==="
[ "$IRRLICHT_ONLY" -eq 1 ] && exit 0

BUILD_DIR="${GLES2_BUILD_DIR:-$PROJECT_DIR/build-gles2}"
echo "=== Configuring $BUILD_DIR (EQT_GLES2=ON, ANGLE) ==="
cmake -S "$PROJECT_DIR" -B "$BUILD_DIR" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DEQT_GRAPHICS=ON -DEQT_GLES2=ON -DEQT_DRM=OFF \
    -DEQT_GLES2_IRRLICHT_DIR="$IRRLICHT_BUILD_DIR" \
    -DEQT_ANGLE_DIR="$ANGLE_DIR" -DEQT_ANGLE_LIB_DIR="$ANGLE_OUT" \
    -DEQT_BUILD_TESTS=OFF -DWITH_RDP=OFF \
    -DOPENSSL_ROOT_DIR=/opt/homebrew/opt/openssl@3 \
    -DCMAKE_POLICY_VERSION_MINIMUM=3.5
# CMAKE_POLICY_VERSION_MINIMUM: Homebrew's CMake 4.x refuses the FetchContent'd
# etc2comp's cmake_minimum_required(VERSION 2.x); Linux hosts run CMake 3.28.
echo "=== Building willeq + gles2_zone_benchmark ==="
nice -n 10 cmake --build "$BUILD_DIR" --target willeq gles2_zone_benchmark -j"$JOBS"
echo "=== Done. Binaries in $BUILD_DIR/bin/ ==="
