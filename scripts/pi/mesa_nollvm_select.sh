#!/usr/bin/env bash
# B932: point ONE ARM executable at the LLVM-less Mesa prefix, without
# touching the Pi's system Mesa and without environment variables.
#
# It writes a COPY of the binary whose dynamic section carries
#   DT_RPATH = <prefix>/lib      (patchelf --force-rpath, NOT DT_RUNPATH)
# Why DT_RPATH and not LD_LIBRARY_PATH / DT_RUNPATH:
#   * willeq runs with file capabilities (cap_net_raw,cap_net_admin+ep, for
#     SO_RCVBUFFORCE -- B751). A capability binary executes in secure mode
#     (AT_SECURE), and ld.so then IGNORES LD_LIBRARY_PATH. An absolute
#     DT_RPATH without $ORIGIN is still honoured in secure mode.
#   * glibc searches the EXECUTABLE's DT_RPATH for every load in the process
#     whose requesting object has no DT_RUNPATH -- that covers glvnd's
#     dlopen("libEGL_mesa.so.0") from the system libEGL.so.1, libgbm.so.1
#     (a DT_NEEDED of willeq/kmscube), and libgallium-25.2.8.so (DT_NEEDED of
#     libEGL_mesa and of dri_gbm.so, which libgbm dlopens by the absolute
#     $libdir/gbm path compiled into it). A DT_RUNPATH would reach only the
#     executable's own DT_NEEDED entries.
# The glvnd vendor file is the system's (/usr/share/glvnd/egl_vendor.d/
# 50_mesa.json, library_path "libEGL_mesa.so.0"), so no EGL env var is
# needed either. The ONLY proof that this worked is /proc/<pid>/maps of the
# running process (scripts/pi/rendered_zone_run.py --maps-at, and the
# kmscube leg in refactor/B932/README.md): the prefix's libgallium-25.2.8.so
# must be mapped and libLLVM must be absent.
#
# usage: scripts/pi/mesa_nollvm_select.sh <in-binary> <out-binary> [prefix]
#        (prefix default /home/user/mesa-nollvm; runs patchelf in the
#        willeq-mesa-nollvm-armhf image, since neither the host nor the Pi
#        has patchelf installed)
set -euo pipefail
IN="$(readlink -f "$1")"
OUT="$2"
PREFIX="${3:-/home/user/mesa-nollvm}"
mkdir -p "$(dirname "$OUT")"
OUT="$(readlink -f "$(dirname "$OUT")")/$(basename "$OUT")"
cp "$IN" "$OUT"
docker run --rm -u "$(id -u):$(id -g)" -v "$(dirname "$OUT"):/w" willeq-mesa-nollvm-armhf \
    bash -euc "patchelf --force-rpath --set-rpath '$PREFIX/lib' '/w/$(basename "$OUT")'
               arm-linux-gnueabihf-readelf -d '/w/$(basename "$OUT")' | grep -E 'RPATH|RUNPATH|NEEDED'"
