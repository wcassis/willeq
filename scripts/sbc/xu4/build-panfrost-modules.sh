#!/usr/bin/env bash
# Build Panfrost (with the G3D clock-mux fix) and the DRM GPU scheduler as
# out-of-tree modules for the running Armbian kernel on the ODROID XU4, and
# install them under /lib/modules/$(uname -r)/extra.
#
# Why this exists: Armbian's odroidxu4 "current" kernel 26.8.3 (6.6.151) ships
# with CONFIG_DRM_PANFROST unset and no gpu-sched module, and even the earlier
# 26.05 kernel that had Panfrost ran the GPU at 24 MHz (docs/reference/odroid_xu4.md).
# The sources are the upstream stable tree at the running kernel's version, so
# the module matches the headers exactly; only panfrost_gpu.c carries a patch.
#
# Run ON the board, as the unprivileged user (sudo for the install step):
#   bash build-panfrost-modules.sh            # build + install + depmod
#   bash build-panfrost-modules.sh --no-install
# Needs: linux-headers-current-odroidxu4 matching `uname -r`, build-essential,
# flex, bison, libssl-dev, libelf-dev, curl. Re-run after every kernel upgrade
# (a new version drops /lib/modules/<old>/extra with the old tree).
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
KVER="$(uname -r)"                              # e.g. 6.6.151-current-odroidxu4
UPSTREAM="v${KVER%%-*}"                         # e.g. v6.6.151
KDIR="/lib/modules/${KVER}/build"
WORK="${XU4_MODULE_WORK:-$HOME/src/xu4-modules}"
MIRROR="${XU4_KERNEL_MIRROR:-https://raw.githubusercontent.com/gregkh/linux/${UPSTREAM}}"
INSTALL=1
[ "${1:-}" = "--no-install" ] && INSTALL=0

[ -e "$KDIR/Module.symvers" ] || { echo "no kernel headers at $KDIR (apt install linux-headers-current-odroidxu4)" >&2; exit 65; }

fetch() { # fetch <subdir> <dest> <files...>
    local sub=$1 dest=$2; shift 2
    mkdir -p "$dest"
    for f in "$@"; do
        [ -s "$dest/$f" ] || curl -sSf -m 120 -o "$dest/$f" "$MIRROR/$sub/$f"
    done
}

echo "== sources: $UPSTREAM -> $WORK"
fetch drivers/gpu/drm/scheduler "$WORK/gpu-sched" sched_main.c sched_entity.c sched_fence.c gpu_scheduler_trace.h
fetch drivers/gpu/drm/panfrost "$WORK/panfrost" panfrost_device.c panfrost_device.h panfrost_devfreq.c \
    panfrost_devfreq.h panfrost_drv.c panfrost_dump.c panfrost_dump.h panfrost_features.h panfrost_gem.c \
    panfrost_gem.h panfrost_gem_shrinker.c panfrost_gpu.c panfrost_gpu.h panfrost_issues.h panfrost_job.c \
    panfrost_job.h panfrost_mmu.c panfrost_mmu.h panfrost_perfcnt.c panfrost_perfcnt.h panfrost_regs.h

# The trace header is written for an in-tree build; point it at its own directory.
sed -i 's|#define TRACE_INCLUDE_PATH .*|#define TRACE_INCLUDE_PATH .|' "$WORK/gpu-sched/gpu_scheduler_trace.h"
cp "$HERE/Makefile.gpu-sched" "$WORK/gpu-sched/Makefile"
cp "$HERE/Makefile.panfrost" "$WORK/panfrost/Makefile"

echo "== patch: panfrost_gpu.c (G3D clock mux)"
if ! grep -q xu4_g3d_mux_fix "$WORK/panfrost/panfrost_gpu.c"; then
    patch -p1 -d "$WORK/panfrost" < "$HERE/panfrost-xu4-g3d-mux.patch"
fi

echo "== build: gpu-sched"
make -C "$WORK/gpu-sched" -j"$(nproc)"
echo "== build: panfrost"
# The Armbian headers' Module.symvers lacks gpu-sched; the freshly built one supplies it.
make -C "$WORK/panfrost" -j"$(nproc)" KBUILD_EXTRA_SYMBOLS="$WORK/gpu-sched/Module.symvers"
ls -la "$WORK/gpu-sched/gpu-sched.ko" "$WORK/panfrost/panfrost.ko"

[ "$INSTALL" = 1 ] || exit 0
echo "== install: /lib/modules/$KVER/extra + modules-load + boot service"
sudo install -d "/lib/modules/$KVER/extra"
sudo install -m 644 "$WORK/gpu-sched/gpu-sched.ko" "$WORK/panfrost/panfrost.ko" "/lib/modules/$KVER/extra/"
sudo depmod -a
sudo install -m 644 "$HERE/xu4-panfrost.conf" /etc/modules-load.d/xu4-panfrost.conf
sudo install -m 755 "$HERE/xu4-g3d-clock.sh" /usr/local/sbin/xu4-g3d-clock.sh
sudo install -m 644 "$HERE/xu4-g3d-clock.service" /etc/systemd/system/xu4-g3d-clock.service
sudo systemctl daemon-reload
sudo systemctl enable xu4-g3d-clock.service
echo "installed: $(modinfo -n panfrost) ; reload with: sudo rmmod panfrost; sudo modprobe panfrost"
