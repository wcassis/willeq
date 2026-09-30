#!/usr/bin/env bash
# B932/B935: build the LLVM-less Mesa the Orange Pi ships with (armhf, Ubuntu
# Noble ABI) on the dev host, in a container. NEVER on the Pi (483 MB) and
# never on the Inspur inference box.
#
# Source: Ubuntu's own source package for the version the Pi runs
# (mesa 25.2.8-0ubuntu0.24.04.1, see docker/Dockerfile.mesa-nollvm-armhf), so
# against the Pi's stock packages only the BUILD OPTIONS differ.
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
# B935 (#253, Will 2026-09-30 "ship the prefix"): the result is a SHIPPED
# ARTIFACT with a pin, like data/willeq.db or the EQE1 pack.
#   * The prefix is built for ONE absolute path on the Pi,
#     $MESA_NOLLVM_PI_PREFIX (scripts/pi/mesa_nollvm.env): libgbm and
#     libgallium carry <prefix>/lib/gbm and <prefix>/share/drirc.d compiled
#     in, so it is not relocatable.
#   * It is packed into a DETERMINISTIC tarball ($MESA_NOLLVM_TARBALL: sorted
#     names, mtime 0, owner 0, single-thread zstd -19) whose SHA256 is pinned
#     in scripts/pi/mesa_nollvm.sha256.
#   * The store is OUTSIDE git, on NFS beside the CI ARM builds:
#     $MESA_NOLLVM_STORE/ = the tarball, its .sha256, BUILD_INFO,
#     meson-options.txt, and prefix/ (the tarball extracted, plus
#     prefix/.willeq-mesa-pin = the pinned sha) which the Pi copies from
#     (scripts/pi/deploy_mesa_prefix.sh).
#   * A build whose tarball does not match the pin is NEVER installed into the
#     store (FAIL, exit 1) unless --write-pin says the pin itself moves.
# The Pi's system Mesa is never touched. A process selects the prefix by a
# DT_RPATH on a COPY of the binary (scripts/pi/mesa_nollvm_select.sh; the CI
# publish step writes bin/willeq-nollvm), which survives setcap's
# secure-execution mode where LD_LIBRARY_PATH and $ORIGIN do not.
#
# usage: scripts/pi/build_mesa_nollvm.sh [--ensure] [--write-pin] [--out DIR] [-j N]
#   (no flag)    build into --out (scratch, default ~/b935-mesa-nollvm), pack,
#                compare with the pin, install into the store on a match
#   --ensure     the publish/CI entry point: if the store already holds the
#                pinned tarball, verify it, make sure prefix/ is extracted, and
#                exit 0 WITHOUT building; otherwise build as above
#   --write-pin  after the build, rewrite scripts/pi/mesa_nollvm.sha256 with
#                the new tarball's sha and install it (commit the pin file)
# Exit: 0 ok; 1 build failed, libLLVM referenced, or pin mismatch; 64 usage.
set -euo pipefail
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
# shellcheck source=mesa_nollvm.env
source "$REPO/scripts/pi/mesa_nollvm.env"
OUT="${HOME}/b935-mesa-nollvm"
JOBS=32
IMAGE=willeq-mesa-nollvm-armhf
ENSURE=0
WRITE_PIN=0
while [ $# -gt 0 ]; do
    case "$1" in
        --ensure) ENSURE=1; shift ;;
        --write-pin) WRITE_PIN=1; shift ;;
        --out) OUT="$2"; shift 2 ;;
        -j) JOBS="$2"; shift 2 ;;
        -h|--help) sed -n '2,70p' "$0"; exit 0 ;;
        *) echo "unknown option: $1" >&2; exit 64 ;;
    esac
done
PI_PREFIX="$MESA_NOLLVM_PI_PREFIX"
PIN="$(mesa_nollvm_pin)"
TB="$MESA_NOLLVM_STORE/$MESA_NOLLVM_TARBALL"
say() { printf '[build_mesa_nollvm] %s\n' "$*" >&2; }

# Verify the store's tarball against the pin and (re)extract prefix/ when its
# stamp is not the pin. Atomic: extract beside, then mv.
extract_prefix() {
    local got tmp
    got="$(sha256sum "$TB" | cut -d' ' -f1)"
    if [ "$got" != "$PIN" ]; then
        say "FAIL: $TB is sha256 $got, the pin ($MESA_NOLLVM_PIN_FILE) says ${PIN:-<none>}"
        exit 1
    fi
    if [ "$(cat "$MESA_NOLLVM_STORE/prefix/.willeq-mesa-pin" 2>/dev/null)" = "$PIN" ]; then
        return 0
    fi
    tmp="$MESA_NOLLVM_STORE/.prefix.tmp.$$"
    rm -rf "$tmp"
    mkdir -p "$tmp"
    zstd -dc -q "$TB" | tar -x -C "$tmp"
    echo "$PIN" > "$tmp/.willeq-mesa-pin"
    rm -rf "$MESA_NOLLVM_STORE/prefix"
    mv "$tmp" "$MESA_NOLLVM_STORE/prefix"
    say "extracted $MESA_NOLLVM_STORE/prefix (pin $PIN)"
}

if [ "$ENSURE" -eq 1 ] && [ "$WRITE_PIN" -eq 0 ] && [ -n "$PIN" ] && [ -f "$TB" ]; then
    extract_prefix
    say "OK: the pinned $MESA_NOLLVM_TARBALL is in $MESA_NOLLVM_STORE (no build)"
    exit 0
fi
[ -n "$PIN" ] || [ "$WRITE_PIN" -eq 1 ] || { say "no pin for $MESA_NOLLVM_TARBALL in $MESA_NOLLVM_PIN_FILE (use --write-pin)"; exit 1; }

mkdir -p "$OUT"
docker build -q -f "$REPO/docker/Dockerfile.mesa-nollvm-armhf" -t "$IMAGE" "$REPO" >/dev/null

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
say "--- installed files ---"
find "$OUT/mesa-nollvm" \( -type f -o -type l \) -printf '%P\n' | sort >&2
say "--- NEEDED of every installed .so (libLLVM must be absent) ---"
docker run --rm -v "$OUT:/out" "$IMAGE" bash -c \
    'for f in $(find /out/mesa-nollvm -name "*.so*" -type f); do echo "$f: $(arm-linux-gnueabihf-readelf -d "$f" | sed -n "s/.*NEEDED.*\[\(.*\)\]/\1/p" | tr "\n" " ")"; done' >&2
if grep -rl libLLVM "$OUT/mesa-nollvm" >/dev/null 2>&1; then
    say "FAIL: libLLVM referenced in the prefix"; exit 1
fi

# The deterministic tarball: same files -> same bytes (sorted names, fixed
# mtime/owner/mode bits, single-threaded zstd).
tar --sort=name --mtime=@0 --owner=0 --group=0 --numeric-owner --mode=go-w --format=gnu \
    -C "$OUT/mesa-nollvm" -cf - . | zstd -19 -T1 -q -c > "$OUT/$MESA_NOLLVM_TARBALL"
GOT="$(sha256sum "$OUT/$MESA_NOLLVM_TARBALL" | cut -d' ' -f1)"
{
    echo "mesa_version=$MESA_NOLLVM_VERSION"
    echo "mesa_source=mesa_25.2.8-0ubuntu0.24.04.1 (Launchpad, sha256-checked in the Dockerfile)"
    echo "pi_prefix=$PI_PREFIX"
    echo "tarball=$MESA_NOLLVM_TARBALL sha256=$GOT"
    echo "built=$(date -u +%FT%TZ) on $(hostname)"
    echo "repo_commit=$(git -C "$REPO" rev-parse HEAD)"
    echo "image_id=$(docker image inspect -f '{{.Id}}' "$IMAGE")"
} > "$OUT/BUILD_INFO"
if [ "$WRITE_PIN" -eq 1 ]; then
    printf '%s  %s\n' "$GOT" "$MESA_NOLLVM_TARBALL" > "$MESA_NOLLVM_PIN_FILE"
    say "pin written: $MESA_NOLLVM_PIN_FILE ($GOT) -- commit it"
    PIN="$GOT"
elif [ "$GOT" != "$PIN" ]; then
    say "FAIL: the build packed $GOT but the pin says $PIN -- NOT installed ($OUT/$MESA_NOLLVM_TARBALL kept for inspection; --write-pin moves the pin)"
    exit 1
fi

# Install into the store (atomic per file: temp in the destination dir, mv).
mkdir -p "$MESA_NOLLVM_STORE"
if [ ! -f "$TB" ] || [ "$(sha256sum "$TB" | cut -d' ' -f1)" != "$GOT" ]; then
    cp "$OUT/$MESA_NOLLVM_TARBALL" "$TB.tmp.$$"
    mv "$TB.tmp.$$" "$TB"
fi
printf '%s  %s\n' "$GOT" "$MESA_NOLLVM_TARBALL" > "$TB.sha256"
cp "$OUT/BUILD_INFO" "$OUT/meson-options.txt" "$OUT/meson-args.txt" "$OUT/buildflags.txt" "$MESA_NOLLVM_STORE/"
extract_prefix
say "OK: $TB ($GOT); prefix for the Pi at $PI_PREFIX"
