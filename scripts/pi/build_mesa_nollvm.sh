#!/usr/bin/env bash
# B932: build an LLVM-less Mesa for the Orange Pi (armhf, Ubuntu Noble ABI)
# on the dev host, in a container. NEVER on the Pi (483 MB) and never on the
# Inspur inference box.
#
# Source: Ubuntu's own source package for the version the Pi runs
# (mesa 25.2.8-0ubuntu0.24.04.1, see docker/Dockerfile.mesa-nollvm-armhf), so
# the A/B against the Pi's stock packages changes the BUILD OPTIONS only.
#
# What differs from Ubuntu's armhf build (debian/rules):
#   * -Dllvm=disabled                (Ubuntu: enabled -> libLLVM.so.20.1)
#   * -Dgallium-drivers=lima         (Ubuntu: softpipe nouveau r300 r600 virgl
#                                     freedreno etnaviv panfrost svga tegra vc4
#                                     v3d lima radeonsi zink llvmpipe). The
#                                     renderonly/kmsro display glue for
#                                     sun4i-drm is built automatically with
#                                     lima; 25.2 has no 'kmsro' option value.
#   * -Dplatforms=                   (Ubuntu: x11,wayland). EGL keeps its
#                                     always-on drm (GBM) and surfaceless
#                                     platforms, which is all --drm uses.
#   * -Dglx=disabled -Dopengl=false  GLES2 + EGL + GBM only (Irrlicht's
#                                     COpenGLES2Driver needs nothing else).
#   * no vulkan, va, vdpau, rusticl, teflon, valgrind, lmsensors, extra-hud,
#     tests, drm-shim.
#   * VERSION stays upstream's '25.2.8' (Ubuntu writes the full Debian
#     version into it), so the megadriver is libgallium-25.2.8.so and can
#     never be confused with the system's libgallium-25.2.8-0ubuntu0.24.04.1.so
#     in /proc/<pid>/maps.
# Kept like Ubuntu: -Dglvnd=enabled (the Pi's libEGL.so.1 / libGLESv2.so.2
# are glvnd; Mesa plugs in as libEGL_mesa.so.0), -Db_ndebug=true,
# buildtype=plain + dpkg-buildflags for armhf (-O2 ...), shader cache, zstd,
# expat/xmlconfig (drirc), and the result is stripped.
#
# Output: $OUT/mesa-nollvm/ -- a relocatable-by-rpath prefix meant to live at
# $PI_PREFIX on the Pi (default /home/user/mesa-nollvm), plus
# $OUT/meson-options.txt (the exact configured option set) and
# $OUT/BUILD_INFO. The Pi's system Mesa is never touched: a process is
# pointed at the prefix with scripts/pi/mesa_nollvm_select.sh (a DT_RPATH on
# a COPY of the binary), which survives setcap's secure-execution mode where
# LD_LIBRARY_PATH does not.
#
# usage: scripts/pi/build_mesa_nollvm.sh [--out DIR] [--pi-prefix PATH] [-j N]
set -euo pipefail
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT="${HOME}/b932-mesa-nollvm"
PI_PREFIX="/home/user/mesa-nollvm"
JOBS=32
IMAGE=willeq-mesa-nollvm-armhf
while [ $# -gt 0 ]; do
    case "$1" in
        --out) OUT="$2"; shift 2 ;;
        --pi-prefix) PI_PREFIX="$2"; shift 2 ;;
        -j) JOBS="$2"; shift 2 ;;
        -h|--help) sed -n '2,45p' "$0"; exit 0 ;;
        *) echo "unknown option: $1" >&2; exit 64 ;;
    esac
done
mkdir -p "$OUT"
docker build -f "$REPO/docker/Dockerfile.mesa-nollvm-armhf" -t "$IMAGE" "$REPO"

MESON_OPTS=(
    --cross-file /opt/mesa_nollvm_cross.ini
    --prefix "$PI_PREFIX" --libdir lib
    --buildtype plain -Db_ndebug=true
    -Dllvm=disabled
    -Dgallium-drivers=lima
    -Dvulkan-drivers= -Dvulkan-layers=
    -Dplatforms=
    -Degl=enabled -Dgbm=enabled -Dglvnd=enabled
    -Dgles1=disabled -Dgles2=enabled -Dopengl=false -Dglx=disabled
    -Dgallium-va=disabled -Dgallium-vdpau=disabled -Dgallium-rusticl=false
    -Dteflon=false -Dvalgrind=disabled -Dlmsensors=disabled -Dlibunwind=disabled
    -Dgallium-extra-hud=false -Dbuild-tests=false -Dtools= -Dvideo-codecs=
    -Dzstd=enabled -Dshader-cache=enabled -Dxmlconfig=enabled -Dexpat=enabled
)
printf '%s\n' "${MESON_OPTS[@]}" > "$OUT/meson-args.txt"

# One container run: configure, build, install into a DESTDIR, strip.
docker run --rm -u "$(id -u):$(id -g)" -v "$OUT:/out" -e HOME=/tmp "$IMAGE" bash -euc '
    cp -a /src/mesa /tmp/mesa
    cd /tmp/mesa
    echo 25.2.8 > VERSION
    eval "$(DEB_HOST_ARCH=armhf dpkg-buildflags --export=sh 2>/dev/null)"
    export CFLAGS CXXFLAGS LDFLAGS CPPFLAGS
    mapfile -t args < /out/meson-args.txt
    meson setup /tmp/build "${args[@]}"
    meson configure /tmp/build > /out/meson-options.txt
    ninja -C /tmp/build -j '"$JOBS"'
    rm -rf /out/stage
    DESTDIR=/out/stage meson install -C /tmp/build --strip
    echo "CFLAGS=$CFLAGS" > /out/buildflags.txt
    echo "LDFLAGS=$LDFLAGS" >> /out/buildflags.txt
'
rm -rf "$OUT/mesa-nollvm"
mv "$OUT/stage${PI_PREFIX}" "$OUT/mesa-nollvm"
rm -rf "$OUT/stage"
{
    echo "mesa_source=mesa_25.2.8-0ubuntu0.24.04.1 (Launchpad, sha256-checked in the Dockerfile)"
    echo "pi_prefix=$PI_PREFIX"
    echo "built=$(date -u +%FT%TZ) on $(hostname)"
    echo "repo_commit=$(git -C "$REPO" rev-parse HEAD)"
    echo "image_id=$(docker image inspect -f '{{.Id}}' "$IMAGE")"
} > "$OUT/BUILD_INFO"
echo "--- installed files ---"
( cd "$OUT/mesa-nollvm" && find . -type f -o -type l | sort )
echo "--- NEEDED of every installed .so (libLLVM must be absent) ---"
docker run --rm -v "$OUT:/out" "$IMAGE" bash -c \
    'for f in $(find /out/mesa-nollvm -name "*.so*" -type f); do echo "$f: $(arm-linux-gnueabihf-readelf -d "$f" | sed -n "s/.*NEEDED.*\[\(.*\)\]/\1/p" | tr "\n" " ")"; done'
if grep -rl libLLVM "$OUT/mesa-nollvm" >/dev/null 2>&1; then
    echo "FAIL: libLLVM referenced in the prefix" >&2; exit 1
fi
echo "OK: $OUT/mesa-nollvm (deploy to the Pi at $PI_PREFIX)"
