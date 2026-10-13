# Radxa Cubie A7S as willeq lane 4 (B1056, #376)

Will added the board on 2026-10-13 ("let's get this device set up for willeq work"). It is `PI_LANE=4` in
`scripts/pi/env.sh` and the first lane that is neither Ubuntu nor Mali: Debian 11 bullseye on Radxa's vendor
kernel, with an Imagination PowerVR GPU driven by the IMG DDK blob. This page records the device, what was set up on
it, the four traps that make it different from lanes 1-3, and how to build and run willeq there. The build script
lives in `scripts/sbc/a7s/`.

## The device

| | |
|---|---|
| board | Radxa Cubie A7S, `172.16.200.206`, ssh alias `a7s` (user **`radxa`**, passwordless sudo; keys from dev-u24 and hermes-ext) |
| SoC | Allwinner A733: 2x Cortex-A76 @ 2.0 GHz (cpu6-7) + 6x Cortex-A55 @ 1.8 GHz, 4 GB LPDDR, 1.9 GB zram swap |
| GPU | Imagination PowerVR B-Series **BXM-4-64**, vendor `pvrsrvkm` DKMS module (`img-bxm-dkms`), card1 / renderD128; firmware `rgx.fw.36.56.104.183` |
| GL stack | IMG DDK **24.2@6603887**: `/usr/local/lib/dri/pvr_dri.so` loaded by IMG's own Mesa-derived `libEGL`/`libgbm`/`libGLESv2` in `/usr/local/lib`; reports "OpenGL ES 3.2 build 24.2@6603887, Imagination Technologies" on the GBM platform. The Debian Mesa 20.3.5 packages are the glvnd loader only and know no PowerVR driver |
| OS | Debian 11 bullseye (EOL 2026-08-31 -- apt sources re-pointed at `archive.debian.org`), kernel `5.15.147-14-a733` (vendor; `-21` is in Radxa's repo), gcc 10.2, cmake 3.18, glibc **2.31** |
| display | `sunxi-drm` on card0, connector **`card0-DP-1`** (HDMI through a USB-C AV adapter, 1920x1080); USB keyboard + mouse on evdev |
| storage | 29 GB eMMC root (slow: ~12 MB/s direct writes), `/tmp` 1 G tmpfs, `/mnt/ram` 1.5 G tmpfs (both `nosuid`: `setcap` in the ext4 home) |

## What was set up (device only; 2026-10-13)

- **`sddm` disabled** (`systemctl disable --now sddm`): the image boots a desktop whose X server holds DRM master on
  card0, and a lane's `--drm` client needs it. `systemctl enable --now sddm` brings the desktop back.
- **apt**: sources moved to `archive.debian.org` (backup `/etc/apt/sources.list.d.pre-willeq`); build deps
  (build-essential, cmake, ninja, ccache, EGL/GLES/GBM/DRM/libinput/udev/OpenSSL/zlib/ALSA/glib dev, fmt 7.1, glm,
  jsoncpp 1.9.4, cereal, boost-graph 1.74, sqlite3, gtest, libpng), kmscube, mesa-utils(-extra), gdb/strace/evtest,
  sshfs. Two apt quirks: `apt-get update` prints `Error: Timeout was reached` from a desktop hook and still works,
  and a stale security index 404s until `/var/lib/apt/lists/*security*` is deleted.
- **`/mnt/projects` is sshfs, not NFS** (fstab `fuse.sshfs`, `user@10.0.30.23:/mnt/projects`, the board's
  `radxa@a7s` key in dev-u24's `authorized_keys`, `user_allow_other` in `/etc/fuse.conf`). The vendor kernel has no
  NFS client (`CONFIG_NFS_FS` unset; cifs and fuse are modules) and the NAS refuses SMB from `172.16.200.x`. Same
  paths as every other lane; reads go through dev-u24.
- `/tmp` and `/mnt/ram` tmpfs, root `noatime` (fstab backup `/etc/fstab.pre-willeq`); ccache 6 G at
  `~/.cache/ccache`, `/usr/lib/ccache` on PATH.

## The four traps

1. **The `ci-arm64/` binary does not start.** It is linked against a noble sysroot and needs `GLIBC_2.38`
   (`objdump -T bin/willeq | grep GLIBC_`); the board has 2.31. Lane 4's binary is built on the device
   (below) and published under `ci-arm64-bullseye/<sha>/` -- `PI_LANE_PUBLISH` in `env.sh`, which
   `lane_smoke.sh` and `pi_rendered_zone_sweep.sh` read.
2. **An ssh session does not see the GPU driver.** The image puts the IMG libraries on the loader path through
   `/etc/environment`, but with an `export` keyword that pam_env ignores, so `eglinfo` over ssh finds Mesa and no
   driver. Every GL run is prefixed with `env.sh`'s `PI_RUN_ENV`
   (`LD_LIBRARY_PATH=/usr/local/lib LIBGL_DRIVERS_PATH=/usr/local/lib/dri`); `sudo` drops it, so `sudo env
   $PI_RUN_ENV ...` (`kmscube_floor.sh` does this).
3. **The ssh user is `radxa`**, so the lane's lock is `/home/radxa/.pi-lock` and `env.sh` sets `PI_USER` per lane.
4. **The connector is `card0-DP-1`**, not `HDMI-A-1` (`PI_HDMI_CONN` per lane).

## Building and running

```bash
# on the board (ssh a7s); the checkout is a clone of the task branch on the eMMC
git clone --branch <branch> --single-branch ssh://user@10.0.30.23/mnt/projects/fable/main ~/src/willeq
bash ~/src/willeq/scripts/sbc/a7s/build_native.sh          # Irrlicht + willeq + tools, then publish
# from dev-u24
PI_LANE=4 scripts/pi/lane_smoke.sh <sha>                     # floor + offline qeynos2 on DRM
PI_LANE=4 EQEMU_HOST=local scripts/pi_rendered_zone_sweep.sh --zones qeynos2 --mesa system
```

`build_native.sh` mirrors `docker/Dockerfile.arm64-noble`: the same `docker/irrlicht-drm/` device and driver
sources, `irrlicht-drm-patch.py`, libpng's `arm/` NEON objects from the vendor store, GLES2-only compile config,
then the project with `EQT_DRM=ON EQT_GLES2=ON`, RelWithDebInfo (`-g1`, 4 GB), tests and RDP off, `-j4`. Trees live
under `~/build` (a debug tree does not fit the tmpfs); the publish has the `ci-arm64/` shape (stripped `bin/`,
`config/`, `data/`, `BUILD_INFO`, `by-branch/`). The `orangepi` preset applies, as on every lane (B1033).

## Measurements

Filled in by the first-light smoke (`review/B1056/`): GL floor, qeynos2 frame times, owned memory. Nothing from
the Lima cost sheet or the Panfrost findings transfers to this driver; re-measure before reasoning about it.
