# Orange Pi Zero as SBC lane 5 (B1060, #380)

Will brought the Zero online on 2026-10-14 as an Orange Pi One alternative. It is **`PI_LANE=5`** in
`scripts/pi/env.sh`: lane 1's sibling (same armhf publish, same shipped Mesa prefix, same `orangepi`
preset, same 256 MB bound and 64 MB target), with one structural difference -- it has **no display
output**, so every DRM run goes through a virtual KMS card (vkms). Every figure below was measured on
2026-10-14 against the fable publish `ci-arm/a3229e633c0412e5c62cdecc88b55cac2edddef4/`; the evidence is
under `/mnt/projects/fable/review/B1060/` (README there).

## The device

| | |
|---|---|
| board | Xunlong Orange Pi Zero, `172.16.200.207` (`orangepizero.goldfarm.local`), user `user`, key auth from dev-u24 and hermes-ext, passwordless sudo |
| SoC | Allwinner H2+, 4x Cortex-A7, cpufreq 480-1296 MHz, governor `ondemand` (sits at 1296 MHz under the rigs) |
| RAM | **484 MB** total in `free -m` (MemTotal 496128 kB, CMA 104 MB) -- not the 256 MB of the issue's first message; 242 MB zram swap (Armbian default) |
| GPU | Mali-400 MP2 (gp + pp0 + pp1, 64 KB L2), **Lima**, kernel `6.18.55-current-sunxi`; devfreq `simple_ondemand` over 120 / 312 / 432 / 576 MHz -- the **same OPP table as lane 1** |
| OS | Armbian_community 26.11.0-trunk.83 **trixie** (Debian 13), glibc 2.41, system Mesa 25.0.7 |
| display | **none wired**: no HDMI on the board; the device tree has `display-engine`, `tv-encoder@1e00000` and `hdmi@1ee0000` `disabled`, so sun4i-drm never binds and `/dev/dri/card0` is Lima, render-only (`drmModeGetResources` fails: not a KMS device) |
| storage | 29 GB SD root, `/tmp` tmpfs 243 MB `nosuid` (setcap copies go to the ext4 home, as on lane 1) |
| NFS | `/mnt/projects` pinned-port NFSv3, the ROCK 4D / ThinkPad line (fstab backed up to `/etc/fstab.pre-willeq`); the NAS accepted 172.16.200.207 with no export change |
| input | no `/dev/input` (nothing attached) |

## What was installed (2026-10-14)

`nfs-common`, then the GL/DRM runtime lane 1 has, from trixie: `libdrm2 libegl1 libegl-mesa0 libgbm1 libgles2
libgl1-mesa-dri` (the Lima, `sun4i-drm` and **`vkms`** DRI drivers), `kmscube`, `mesa-utils`, `mesa-utils-bin`,
`libdrm-tests` (`modetest`), `evtest` -- with `--no-install-recommends`; apt pulled the X/Wayland client libs,
`libllvm19` and `mesa-libgallium` as dependencies. `libcap2-bin` and `procps` were already present. The full
dpkg list and the lane-1 diff are in `review/B1060/deps/`. Nothing else on the box changed: no kernel, module,
device-tree or boot-config edit; vkms is loaded only for a run (below).

## The binary

The armhf noble publish starts on trixie unmodified once the GL runtime is installed (glibc is forward
compatible): before the install `ldd` lacked exactly `libdrm.so.2 libEGL.so.1 libgbm.so.1 libGLESv2.so.2`,
after it nothing (`review/B1060/binary/`). `willeq` has no `--version` flag (it falls through to the config
load); `--help` runs. No native build was needed.

## The display: vkms

The client's ARM DRM device has no surfaceless path: on the bare box `gles2_zone_benchmark --drm` logs
`DRM: no usable /dev/dri/card* (no card with a usable display)` and exits 1 (`review/B1060/drm/bench_card0_no_kms.txt`),
and kmscube refuses `card0` the same way. The kernel ships `CONFIG_DRM_VKMS=m`, and trixie's Mesa has a
kmsro `vkms_dri.so`: with `modprobe vkms`, `card1` appears with a connected `Virtual-1` (1024x768 preferred,
800x600 and 1280x720 offered), GBM on it renders through the Lima render node, and kmscube runs at 60 fps on
`renderer: "Mali400"` (`review/B1060/drm/kmscube_vkms_renderer.txt`). The client's own card scan (B1054)
skips `card0` and takes `card1` by itself; the shipped LLVM-less prefix (Mesa 25.2.8) drives it too
(`mesa_driver=Mali400:/home/user/mesa-nollvm-25.2.8/...` in every ledger row).

`pi_hdmi_force_on` / `pi_hdmi_restore` (env.sh) do this on lane 5 (`PI_DRM_VIRTUAL=1`): load vkms only when
it is absent, open the card to `video`, unload only what the rig loaded. Frames are flipped on a virtual CRTC
and go nowhere: lane 5 measures memory, GPU cost and correctness of the GL path, never a picture on a screen.

## The numbers

GL floor (`lane_smoke.sh`, kmscube on vkms, system Mesa 25.0.7, 3 runs at 12 s): VmRSS 55.4 MB, **RssAnon
13.1 MB**, RssFile 42.4 MB, libLLVM mapped -- in the 55-57 MB VmRSS band lane 1's stock kmscube measured.

Offline benchmark (`lane_smoke.sh`, qeynos2, 800x600, 300 frames, system Mesa): `BENCH_EXIT=0`, frame p50
8.07 ms / p95 9.36 ms, steady `rss_anon=44556288 owned=53158144 over_target=0`.

Ledger (`pi_rendered_zone_sweep.sh`, live zone-in against the local eqemu lane, own cgroup, nollvm prefix,
`--constrained orangepi`, 1280x720, setcap'd copy with a 16 MiB `SO_RCVBUFFORCE` buffer):

| zone | verdict | RssAnon ready | RssAnon peak | owned_rss_anon peak | gpu_buffers steady | unnamed GEM steady | fps median |
|---|---|---|---|---|---|---|---|
| qeynos2 | fail: unnamed GEM 10.27 MiB > 10 MiB | 72.4 MB | 83.5 MB | 61.6 MB | 46.1 MB | 10.8 MB | 53 |
| timorous | fail: unnamed GEM 35.65 MiB | 119.1 MB | 126.5 MB | 99.6 MB | 68.2 MB | 37.4 MB | 39 |
| iceclad | fail: unnamed GEM 39.12 MiB | 98.3 MB | 105.7 MB | 79.2 MB | 70.8 MB | 41.0 MB | 44 |
| growthplane | pass | 86.7 MB | 94.1 MB | 75.2 MB | 41.6 MB | 9.1 MB | 30 |
| emeraldjungle | pass | 91.3 MB | 98.7 MB | 79.7 MB | 38.2 MB | 7.8 MB | 53 |

Every peak is under the 256 MB ceiling (`over_256MB_peak=0`), none OOMed, the box swapped ~3 MB. Lane 1's
newest comparable plain rows (`review/B962/pi_final/`, 2026-10-01, build 11f1f431, the same rig) are
qeynos2 RssAnon ready 59.5 / peak 70.6 MB with gpu_buffers 23.6 MB, and growthplane 87.5 / 94.9 MB with
19.8 MB. **These are not same-build comparisons**: growthplane's RssAnon matches lane 1 to within 1 MB, but
lane 5's `gpu_buffers` row is about twice lane 1's on both zones, and lane 5 names one scanout buffer
(3.7 MB) where lane 1 names two. That is a MEASUREMENT, not a finding: the mechanism (the vkms/kmsro scanout
path, the 6.18 Lima kernel driver, or the build) is unattributed. The B962 owner-census shim is armhf and
runs here (`--bo-census`); attributing the gap is a follow-up, and a same-build lane-1 row is its first step.

Clocks and thermals: the GPU idles at 120 MHz and rises under load (the smoke spent 92.7 s at 312 MHz, 3.4 s
at 576 MHz, `review/B1060/lane5/thermal_after.txt`), so the XU4-style stuck-clock defect is not present;
CPU 1296 MHz `ondemand`; SoC 47.8 °C before the smoke, 45.8 °C after, 59.0 °C after the heavy sweep, no
throttling seen. Lima logs `Failed to register cooling device` at probe (no GPU thermal zone in the DT).

## Traps

- **No display at all** -- not even CVBS while the TV encoder is disabled in the DT. Enabling it is a
  device-tree overlay and a reboot (Will's call, not done); vkms needs neither.
- **The vkms card node is `root:root 0600`** (no udev seat tag); `pi_vkms_on` opens it to `video`. kmscube's
  own device scan does not find vkms (libdrm cannot enumerate its faux bus: `MESA-LOADER: failed to retrieve
  device information`, harmless), so the floor pins it with `-D` (`PI_KMSCUBE_ARGS`).
- **Debian's ssh PATH has no `/usr/sbin`** (`/usr/local/bin:/usr/bin:/bin:/usr/games`), so an unprivileged
  `getcap`/`sysctl` over ssh is "not found" where Ubuntu lanes find it; `sudo -n` is unaffected.
- **kmscube's stdout is block-buffered** when redirected and lost on the `timeout` kill: run it under
  `stdbuf -oL` to see its renderer line.
- `free` reports 484 MB, not 256 MB: record the measured figure.
- The board's clock zone is `America/New_York`; the rigs' lock lines carry local time with an offset.
