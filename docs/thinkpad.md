# The ThinkPad: lane 5 and the x86 hardware-GL box (B1055)

Will, 2026-10-13: a ThinkPad with an Intel Core i7-9850H, 46 GB of RAM, an Intel
UHD 630 and an NVIDIA Quadro RTX 3000 Mobile, running Ubuntu Server 24.04 at
`172.16.200.205` (user `user`, key auth from dev-u24 and hermes-ext, passwordless
sudo), "for oracle sessions on a real video card and any other willeq work".
Provisioning evidence: `review/THINKPAD/README.md`.

## What it is

- **Lane 5** of the eqemu lanes (`scripts/eqemu/env.sh`): the eqemu stack AND the
  official-client oracle live on the same box. `EQEMU_LANE=5` resolves both hosts
  to `172.16.200.205`. The stack is at **`/opt/akk-stack`** (env.sh sets `AKK_DIR`
  for this lane) because `/mnt/projects` is the NFS mount here, not local disk.
  It is a clone of lane 1 (tree + the four docker images) with `localaddress`
  repointed; `scripts/eqemu/up.sh` / `down.sh` / `status.sh` work unchanged.
- **The x86 hardware-GL target**: the Intel iGPU (`/dev/dri/card1`, Mesa `iris`,
  the laptop panel `eDP-1`) is the hardware GLES2 implementation for willeq's
  `--drm` and EGL work on x86 -- the same Mesa/EGL/GBM path as the SBC lanes, with
  the full extension set the driver uses (ETC1, program binaries,
  `EXT_discard_framebuffer`). Until B1054 (#374) lands, `--drm` opens the
  connector-less NVIDIA `card0` first and falls back to surfaceless EGL; set
  `WILLEQ_DRM_DEVICE=/dev/dri/card1` once that override exists.
- **The oracle GPU**: the Quadro runs a headless Xorg on display `:99`
  (`oracle-xorg.service`, `/etc/X11/oracle/xorg.conf`: `BusID PCI:1:0:0`,
  `AllowEmptyInitialConfiguration`, virtual 1280x1024). The official Titanium
  client runs under `wine-9.0` (win32 prefix `~/eq-oracle/wineprefix`, client copy
  `~/eq-oracle/client`, `eqhost.txt` pointed at lane 5) and reaches the EULA in
  about 10 s, against roughly two minutes on the llvmpipe oracle VMs. The B1048 oracle
  session driver (MR !172, not yet merged) finds `:99` already up and skips its
  own Xvfb.

## Rules

- Both GPUs stay active (hybrid); nothing is blacklisted. Intel = willeq, NVIDIA =
  the oracle. A user needs the `video` and `render` groups or every DRM open fails
  ("failed to initialize legacy DRM", no Intel Vulkan device).
- Builds: `scripts/build-gles2-x86.sh` with `JOBS=12` takes about six minutes cold
  (the GitLab runner VMs take 25-30); no 4 GiB cgroup here, but a card that uses
  the box says so and leaves it as found.
- The lane lock discipline is the same as lanes 1-4 (`eqemu_lock_take`), and the
  oracle VM conventions apply with the box as its own oracle host.

## Done on the box (2026-10-13)

NFS `/mnt/projects` (pinned-port NFSv3, see `docs/ci.md`), build deps, Docker,
`nvidia-driver-580` with `nvidia-drm.modeset=1`, Xorg + NVIDIA DDX, Wine, the
client copy, the akk-stack clone, `zone_state_spawns` truncated and
`Zone:StateSavingOnShutdown=false` (B1047 #367: the timorous boot loop). Open:
the `x86-gpu` GitLab runner registration.
