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
  session driver (MR !172) finds `:99` already up and skips its own Xvfb.
- **The NVIDIA GLX stack is the standard for :99** (B1073, #393). The 2026-10-13
  re-provision installed Mesa 25.2.8 beside NVIDIA 580.178.04, and Xorg loaded
  BOTH GLX providers — the generic `glx` module (`xserver-xorg-core`,
  `/usr/lib/xorg/modules/extensions/libglx.so`) and NVIDIA's
  `glxserver_nvidia` — leaving `GLX: Another vendor is already registered for
  screen 0` in the log and the client's 32-bit libglvnd dispatcher probing a
  dual-vendor GLX. The consequence showed up in the official client's Set Face
  picker (half-open modal, `FINDINGS-setface-regression.txt` in the CC-1
  evidence). The fix has two halves, both lane-5-only:
  - **Server side**: `/etc/X11/oracle/xorg.conf` carries a
    `Section "Module"` with `Disable "glx"`, so `glxserver_nvidia` is the ONLY
    GLX vendor on screen 0 (the canonical single-GPU NVIDIA setup; `wfb` and
    the nvidia driver load unchanged). Verification: a fresh `/var/log/Xorg.99.log`
    has no "Another vendor" line, and `DISPLAY=:99 glxinfo` reports NVIDIA
    vendor/renderer (Quadro RTX 3000, 580.178.04, GLX direct).
  - **Client side**: `oracle_remote.sh` exports
    `__GLX_VENDOR_LIBRARY_NAME=nvidia` (lane 5 only — lanes 1-4 have no NVIDIA
    vendor lib, and the pin would break their GL), so Wine's 32-bit
    `libGL.so.1` (libglvnd dispatcher) goes to `libnvidia-gl-580:i386`, never
    the Mesa i386 wrapper.
- **The rig has NO window manager, so input needs an explicit focus** (B1073,
  #393). After a client re-render the EverQuest window does not hold input
  focus, and on a no-WM display a motion click (the driver's `jclick` 1px-drag,
  the Set Face net-zero drag) lands on the root window and does nothing. The
  fix is the simpler durable option: `oracle_remote.sh` defines
  `client_focus` (`xdotool windowfocus --sync --name '^EverQuest$'`, a no-op on
  failure) and, on lane 5, redefines `jclick`/`jrclick` (from
  `/mnt/projects/eq-oracle/scripts/ui-lib.sh`, which stays untouched) and the
  raw-xdotool `hold` path to focus first; the Set Face open
  (`face_open_click`) uses the focus-wrapped `jclick` on lane 5 because
  matrix3's net-zero drag does not open the modal on this rig. No WM runs on
  :99; every motion click carries its own focus. A lane-5 capture run logs
  each Set Face open attempt in `timeline.tsv` (`face-open attempt=N`), so a
  run proves whether the FIRST click opened the modal.
- **The default oracle lane** (B1057, #377): oracle legs run
  `scripts/eqemu/oracle/oracle_session.sh 5 <plan> <out>` (`docs/oracle_driver.md`)
  unless lane 5's lock is held; lanes 1-4 are the fallback. The driver's stop
  never touches the real Xorg (no `xvfb.pid` is written when `:99` is up), and
  decoding/frame analysis run on the driver host because this box has no
  NumPy/Pillow/OpenCV. Measured per-step times: `review/B1057/README.md`.

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
