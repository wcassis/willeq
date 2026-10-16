# Radxa Cubie A7S as willeq lane 4 (B1056, #376)

Will added the board on 2026-10-13 ("let's get this device set up for willeq work"). It is `PI_LANE=4` in
`scripts/pi/env.sh` and the first lane that is neither Ubuntu nor Mali: Debian 11 bullseye on Radxa's vendor
kernel, with an Imagination PowerVR GPU driven by the IMG DDK blob. This page records the device, what was set up on
it, the seven traps that make it different from lanes 1-3, and how to build and run willeq there. The build script
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

## The seven traps

1. **The `ci-arm64/` binary does not start.** It is linked against a noble sysroot and needs `GLIBC_2.38`
   (`objdump -T bin/willeq | grep GLIBC_`); the board has 2.31. Lane 4's binary is built on the device
   (below) and published under `ci-arm64-bullseye/<sha>/` -- `PI_LANE_PUBLISH` in `env.sh`, which
   `lane_smoke.sh` and `pi_rendered_zone_sweep.sh` read.
2. **An ssh session does not see the GPU driver.** The image puts the IMG libraries on the loader path through
   `/etc/environment`, but with an `export` keyword that pam_env ignores, so `eglinfo` over ssh finds Mesa and no
   driver. `env.sh`'s `pi_remote` exports the lane's `PI_RUN_ENV`
   (`LD_LIBRARY_PATH=/usr/local/lib LIBGL_DRIVERS_PATH=/usr/local/lib/dri WILLEQ_GPU_CENSUS_NO_FDINFO=1`) in
   front of EVERY remote command, so no rig prefixes it itself; `sudo` drops it, so `kmscube_floor.sh` re-applies
   it past sudo. A `setcap` binary (the sweep's staged `willeq`) loses `LD_LIBRARY_PATH` under `AT_SECURE`
   whatever the env says, so `build_native.sh` links with `DT_RPATH=/usr/local/lib` as well.
3. **The ssh user is `radxa`**, so the lane's lock is `/home/radxa/.pi-lock` and `env.sh` sets `PI_USER` per lane.
4. **The connector is `card0-DP-1`**, not `HDMI-A-1` (`PI_HDMI_CONN` per lane).
5. **kmscube's own device scan picks card1** (`pvrsrvkm`, render-only) and quits with "no connected
   connector!". The display is sunxi-drm card0 and GBM there reaches the IMG driver, so lane 4 sets
   `PI_KMSCUBE_ARGS=-D /dev/dri/card0` (`env.sh`; `kmscube_floor.sh` passes it). willeq's `CIrrDeviceFB`
   opens card0 first and needs nothing.
6. **Symlinks through the sshfs mount.** The share is mounted `follow_symlinks`, so `symlink(2)` lands on the
   server but returns EIO and `unlink(2)` refuses ("Is a directory"). `build_native.sh` makes the by-branch
   link over ssh on the share host (`PUBLISH_SSH`, default `user@10.0.30.23`).
7. **Reading a DRM fd's fdinfo panics the kernel.** (The guard fails closed: an fd whose driver cannot be resolved is
   not read either.) `pvr_show_fdinfo` in the IMG module (pvrsrvkm, DDK 24.2)
   dereferences NULL on any read of `/proc/<pid>/fdinfo/<drm fd>` -- from inside the process (willeq's GPU
   buffer census, the impostor memory probe) or from outside (`proc_mem_snapshot.py` on kmscube, and so any
   `nvtop`-style tool). Full trace: `review/B1056/lane4/bench60_local4.dmesg`. Every reader in the tree asks
   `eqt::memory::drmFdinfoIsSafe()` (mirrored in the Python tool) and skips `pvrsrvkm`, so the lane's
   `gpu_buffers` row comes from `drm_maps_lower_bound`, never `drm_fdinfo`. `kernel.panic_on_oops=1`,
   `kernel.panic=10` are set on the board (`/etc/sysctl.d/90-willeq-panic.conf`) so an Oops reboots it instead of
   wedging it; netconsole must NOT be loaded (its netpoll path freezes the box, trap recorded in memory).
   The Oops-capture rig that found it is `review/B1056/scripts/oops_capture_run.sh` (non-blocking `/dev/kmsg`
   reader on cpu7 + `kernel.printk_delay`).

## Building and running

`ARM_BUILD_BIN_DIR` (what `bench_pi.sh` scp's) is `ci-arm64-bullseye/by-branch/fable/bin` on this lane -- there is
no cross build for it; a `fable` build appears there once `build_native.sh` has run on a `fable` checkout.


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

**Thermal state as delivered (2026-10-14):** every SoC zone (cpul, cpub, gpu, npu, ddr) reads 60-62 °C within a
minute of a cold boot with `pwm-fan` already at its top state, and the passive trips for the CPU clusters and the
GPU are at exactly 60 °C: the little cluster idles throttled (`scaling_max_freq` 1196 of 1794 MHz, cooling state
5/8) and the GPU zone sits on its trip. `skin_zone` reads 36 °C against a 50 °C *critical* (power-off) trip. Nothing
in the sweeps below moved these numbers; a run on this board is a run on a throttled board until the cooling or
the trip table changes (Will's call).

**First-light smoke PASS, 2026-10-14 18:43Z (`review/B1056/lane4/`, build 945e73b2, `scripts/pi/lane_smoke.sh`):**

| figure | value |
|---|---|
| GL floor: kmscube under the IMG driver, 3 runs | VmRSS 22.5-24.2 MB, RssAnon 6.2-7.9 MB, RssFile 16.3 MB; no libgallium, no libLLVM |
| qeynos2 300 frames at 800x600 `--drm`: total frame avg / p95 / max | 3.54 / 4.38 / 4.82 ms (282 fps) |
| GPU sync avg / p95 | 1.30 / 1.44 ms |
| Draw calls avg / max; polygons avg / max | 10 / 14; 48 / 74 |
| VmRSS at steady | 58.9 MB (RssAnon 34.7 MB, RssFile 24.2 MB) |
| gpu_buffers (`drm_maps_lower_bound`, never `drm_fdinfo` here) | 15.8 MB; logical 7.2 MB |
| Texture cache | 126 textures, 5.3 MB |

The floor is a quarter of the Pi's (55-57 MB, `gl-floor-measured`): the IMG stack maps no LLVM and no gallium.
The first three smoke attempts (2026-10-13 19:07Z, 2026-10-14 15:37Z and 17:08Z) panicked the board from the
fdinfo read (trap 7); their logs and the bisect runs are in `review/B1056/lane4_attempts/`.
