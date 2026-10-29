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
- **The Set Face half-render is the CLASSIC-MODEL picker, not a rig defect**
  (B1086, #406, take 3). The bullet above attributes the *opening* half of the
  problem (the modal not opening at all) to the dual-vendor GLX, which is
  correct. The *half-render* (the modal opens, shows the FACE row and the
  Random/Accept/Cancel strip, and never the other six picker rows) is the
  official client's own behaviour with `UseLuclin<Race><Gender>=FALSE` in
  `eqclient.ini`: the classic model's picker exposes FACE only; the 26
  in-era per-race keys set TRUE select the Luclin models and their full
  seven-row list. Measured by a one-knob A/B on lane 5 (same lane, Testchar,
  Barbarian male pair and `facepick-rows` plan): FALSE -> 1 row, TRUE -> 7,
  FALSE restored -> 1, each a separate client start because the INI is read
  at startup. The good-era 10-13 captures rendered seven rows because their
  setup scripts ran `set_facepick_luclin.py` first and restored the INI
  afterwards; every lane's stock INI is all-FALSE. The take-2 suspects (wine
  prefix byte state, the GLX shim's dependencies, a KVM-host regression) are
  **withdrawn**; the harness now applies the toggle itself and restores it.
  See `refactor/B1086/README.md`. The `facepick-rows` row-count gate (>= 2)
  stays: it fails loudly on a stock INI instead of silently capturing one row.
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
- The lane discipline is the same as lanes 1-4 (B1089: the eqemu lock is now a
  **lease** — `eqemu_lock_take`, `docs/ci.md` "Lane leases"), and the
  oracle VM conventions apply with the box as its own oracle host.

## Done on the box (2026-10-13)

NFS `/mnt/projects` (pinned-port NFSv3, see `docs/ci.md`), build deps, Docker,
`nvidia-driver-580` with `nvidia-drm.modeset=1`, Xorg + NVIDIA DDX, Wine, the
client copy, the akk-stack clone, `zone_state_spawns` truncated and
`Zone:StateSavingOnShutdown=false` (B1047 #367: the timorous boot loop). Open:
the `x86-gpu` GitLab runner registration.

## Worker builds: `scripts/hermes/rbuild` (B1087)

hermes kanban workers never compile inside their 4 GiB scope on hermes-ext
(an OOM there took sibling workers down with it, `review/NIGHT-2026-10-16/`).
`scripts/hermes/rbuild <worktree> [configure | <cmake --build args> | test <ctest args> | hygiene]`
(installed for workers as `~/.local/bin/rbuild` on hermes-ext; usage in the
script header) runs the step here over ssh and passes the output and exit code
back.

- **Source over NFS, build tree local.** hermes-ext exports
  `/home/user/src/willeq-wt` to this box and (since B1194, 2026-10-29) the two
  gitlab-runner VMs (`/etc/exports.d/willeq-wt.exports`,
  `all_squash` to uid/gid 1000, which is `user` on each host); the fstab
  mount on this box uses the SAME path (NFSv4.2, `soft`, `actimeo=1`,
  systemd automount). The identical line for each runner VM is staged in
  `refactor/B1194/scripts/setup_rbuild_host.sh`, pending the one-time root
  run (the VMs answer no inbound ssh from either lab200 host and the CI
  user cannot `sudo -n`; B1194's README, RULING REQUEST). The
  attribute cache was `actimeo=0` until 2026-10-19 (Will): every stat was a
  GETATTR round trip to hermes-ext, and a worker build drove it to tens of
  thousands a second (the Build Workflow & Contention dashboard, NFS panels).
  With 1 s a stat can be up to a second stale; a compiler's open() still
  revalidates (close-to-open), so only make's up-to-date check is exposed, and
  the ssh round trip between a worker's edit and its rbuild already exceeds
  that. Measure, do not quote: `nfsstat -c -4` on this box before and after a
  no-op `cmake --build`. Changing it needs umount + automount restart: a
  `mount -o remount` refuses the attribute options. The build
  tree is `/home/user/willeq-builds/<slug>/build` on the NVMe, never in the
  worktree. The patched Irrlicht is built once per
  `irrlicht_input_stamp.sh` digest into `/home/user/willeq-builds/_irrlicht/`.
- **Host selection (B1194, #513).** The same script can target the two idle
  gitlab-runner VMs for hygiene and non-GL targets: `--host <name|auto>`
  (or `RBUILD_HOSTS` / `RBUILD_HOST`; default stays this box). `auto` picks
  the least-loaded REACHABLE host, probing `/proc/loadavg` over ssh; a
  filtered host is skipped, never waited on. GL/visual targets
  (`model_viewer`, `gles2_zone_benchmark`, `visual_gate`) and the oracle stay
  pinned to this box by name — the script refuses them elsewhere. The
  runners answer no inbound port from lab200 (measured), so until the
  firewall opens, steps that must land there run as self-selecting CI jobs
  (`refactor/B1194/ci/b1194.yml`, teed to the NAS share); the README under
  `refactor/B1194/` carries the inventory, the verbatim export/fstab lines
  and the proof timings.
- **Resource control.** Each step is a transient SYSTEM scope in
  `ci.slice/ci-worker.slice`, beside CI's `ci-build.slice` under the weight-30
  parent `ci.slice`, so worker builds yield to the lane-5 oracle exactly as CI
  builds do. The slice unit is `/etc/systemd/system/ci-worker.slice`
  (CPUWeight=30, IOWeight=30, MemoryHigh=28G); each scope gets
  CPUWeight=30, IOWeight=30, MemoryMax=16G. `ci.slice`'s MemoryHigh is 32G
  since the worker slice joined it (backup `ci.slice.pre-rbuild-slice`).
  rbuild starts the scope with
  `sudo -n systemd-run --scope --slice=ci-worker.slice ... -- setpriv --reuid=user --regid=user --clear-groups -- env <vars> <cmd>`.
  `/etc/sudoers.d/90-willeq-rbuild` allows exactly that form and nothing
  else. It is a sudo regex: an `rbuild-*` unit name, MemoryMax 1G..16G, and the
  fixed setpriv drop to `user`. Three notes on that form:
  - It uses `setpriv`, not `systemd-run --uid`, because a `--scope --uid` run
    kept root's supplementary group 0 (measured).
  - `--clear-groups` leaves the step with no supplementary groups, so no
    `video`/`render`. Worker tests therefore cannot open the Intel panel through
    DRM, the same reason gitlab-runner left those groups.
  - `user` also holds a general `NOPASSWD: ALL` here, so the drop-in only starts
    to matter if that rule is narrowed.

  When the sudo form is refused, or `RBUILD_MEM` is outside 1G..16G, rbuild
  says so and falls back to a user scope in the user slice `worker-build.slice`
  (MemoryMax 28G; `rbuild --setup-host` installs it). `-j6` is the default
  (`RBUILD_JOBS`).
- **Shared ccache across worktrees (B1090).** ccache lives in
  `/home/user/willeq-builds/_ccache`. Each tree is configured from the symlink
  `/home/user/willeq-builds/<slug>/src` (pointing at the worktree), not from the
  worktree path itself. ccache runs with `CCACHE_BASEDIR=/home/user/willeq-builds`
  and `CCACHE_NOHASHDIR=1`, so every compile line reads `../src/...` whatever the
  slug (measured on #407: 2485/2485 hits, 94 s against a 1929 s cold build).
  - Full cross-worktree hits need B1090 (MR !218), which takes the absolute
    paths out of the `-D` values.
  - rbuild re-configures a tree that was configured from the old `-S <worktree>`
    path, once.
  - Steps get the CI test environment: `WILLEQ_DATA_ROOT=<worktree>` and the
    Mesa llvmpipe pins (`__EGL_VENDOR_LIBRARY_FILENAMES`, `LIBGL_ALWAYS_SOFTWARE=1`).
- **Lifecycle.** One rbuild per worktree at a time (a second waits on a lock).
  If the caller dies, the ssh channel closes and the remote scope is stopped.
  `rbuild --status` lists running scopes, system and user. `rbuild --kill <slug>`
  stops one. A system scope's processes all run as `user`, so they are
  signalled directly (TERM, then KILL). Stopping the unit itself would need
  root, which the sudoers rule does not grant.
- **Shares the box with CI.** The GitLab runner here builds in `ci-build.slice`
  at the same time; measured times are on B1087.

