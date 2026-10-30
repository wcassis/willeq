# CI — the GitLab pipeline

**What it is for:** builds and the full ctest suite run on the project's own
GitLab runner, not inside a hermes kanban worker. The workers are capped at
4 GiB and were being OOM-killed while linking (board `B847`), which is not a
failure any amount of retrying fixes.
Compiling is the same story: `scripts/build-gles2-x86.sh` defaults to 24 jobs
and a worker that ran it was OOM-killed at 04:40Z on 2026-10-13 (`cc1plus`,
`CONSTRAINT_MEMCG`); since B1046 the script reads its cgroup's `memory.max`
and clamps `JOBS` to 1 GiB per compiler (4 under the worker cap, 24 elsewhere).
Since B1087 (#407) a worker does not compile on hermes-ext at all: every
configure, build and ctest goes through `scripts/hermes/rbuild`, which runs it
on the ThinkPad (`docs/thinkpad.md`, "Worker builds").

## What runs where

Official-client oracle sessions are separate from CI. The one-command lane
driver, plan vocabulary, evidence layout and exit codes are in
[`oracle_driver.md`](oracle_driver.md). Its template library is checked by the
hygiene test `OracleLocateTemplates` (`scripts/eqemu/oracle/tests/oracle_locate_test.py`,
B1058), which needs NumPy + Pillow and the archived corpus under
`/mnt/projects/fable/review/B1058/corpus`; the runners have neither, so it
SKIPs there and runs on dev-u24 / hermes-ext. Its text reader is checked the same way by
`OracleGlyphReader` (`scripts/eqemu/oracle/tests/oracle_glyphs_test.py`, B1059: pinned labelled
crops under `/mnt/projects/fable/review`, read exactly or UNKNOWN, never wrong; same SKIP rule).

Live scenarios are separate from CI too: `scripts/scenario.sh <name>` on an eqemu lane (CLAUDE.md,
"The scenario lane"); their state machines run offline in the suite. `zone_connect_timeout` (B1072,
#392) is the zone-IN bound's live leg: `/camp` to character select, a dead zone port for two
connects, Enter World -- back at character select after the attempt plus ONE retry
(`ZoneComponent::kZoneConnectTimeoutMs` each), then Enter World again and zone in
(`EQEMU_LANE=2 bash scripts/scenario.sh zone_connect_timeout --unit B1072`; offline net
`test_zone_connect_timeout`).

### Lane leases (B1089, #409)

Cross-session use of an eqemu lane (and the XU4) is a **lease, not a lock**: a record with a TTL
that is renewed by activity. The old file-`touch` lock (`/mnt/projects/.eqemu-lock`, noclobber over
ssh) held forever when its holder died, and lane 5's copy lived on the NFS export shared with
dev-u24's local lane -- two hosts, one file. The lease fixes both:

- **The record lives ON the lane's own host** -- one key=value file per lane (`card`, `run`,
  `host`, `pid`, `acquired_at`, `last_touched`, `purpose`), every `take` / `renew` / `release` /
  `reap` one `flock` critical section, written by atomic rename. Lanes 1-4: the eqemu VM
  (`/mnt/projects/.eqemu-lease/laneN`, local disk there); **lane 5: the ThinkPad's LOCAL disk**
  (`/opt/akk-stack/.eqemu-lease/lane5` -- `/mnt/projects` is the NFS export on that box); the XU4:
  on the box (`/home/user/.willeq-lease/xu4`).
- **TTL from one env var**: `LANE_LEASE_TTL_S` (default 900 s). Expiry **releases the lane only;
  nothing is ever killed by expiry**. A record whose `last_touched` is >= TTL old is free,
  whatever pid it names (the holder's pid lives on the dev box, so liveness cannot be tested on
  the lane host; a live holder renews before its TTL).
- **Renew-on-use + the renewer**: `eq_remote` renews the lease before every lane touch when this
  shell holds it (`LANE_LEASE_HELD`, set by `eqemu_lock_take`), so a long wait loop keeps the
  lane without extra plumbing; long scripts ALSO start one background renewer
  (`eqemu_lease_start_renewer`, interval TTL/3, floor `LANE_LEASE_RENEW_FLOOR_S` = 60 s, dies
  with the script, `eqemu_lease_stop_renewer` in the cleanup trap). **A 30-min scenario is never
  stolen.**
- **A stolen lease fails loudly, with the lease's own code: 63** (`LEASE_EXPIRED`, beside
  scenario.sh's 64-69 operator codes). `renew`/`release` by a holder whose lease expired or was
  taken over prints the holder and a re-acquire one-liner:
  `lease <card>: EXPIRED (held by <card>/<run> on <host>)` +
  `re-lease: bash scripts/eqemu/lease take --lane <N> --card '<card>' --purpose '<why>'`.
  `take` on a free lane after expiry **takes over and names the former holder**.
- **The helper is the only door**: `scripts/eqemu/lease <take|renew|release|show|reap|track>
  --lane <1-5|xu4> [--card <c> --run <r> --pid <p>] [--purpose <why>]`;
  `scripts/eqemu/leases` is the status view (one line per lane, lanes 1-5 + XU4, parallel ssh,
  `UNREACHABLE` instead of a hang). **SCRIPTS ARE MANDATORY: a hand-rolled ssh gets no renewal
  and loses the lane at TTL.** The old `eqemu_lock_take/_show/_release`
  (`scripts/eqemu/env.sh`, sourced from `lease_lib.sh`) are thin wrappers over the lease, so every
  caller keeps working; `release` is now owner-checked (a release of someone else's lease is
  refused with 63 -- the eqemu half of B922 #241).
- **`reap`**: `scripts/eqemu/lease reap --lane N --dead-runs <file> [--dry]` kills exactly the
  pids a run recorded on the lane host (`lease track --run <r> --pid <p>`, the file
  `runs/<run>.pids` beside the record) and releases that run's lease when it holds it; a live
  run's record names the live run, so it is never touched. The dry flag reports what it would do
  and does nothing.
- **The net**: `scripts/eqemu/lease_selftest.sh` (ctest `LeaseCoreSelfTest`, label `hygiene`),
  hermetic -- the core against a temp dir, no ssh, no network: TTL expiry, renew extends,
  takeover after expiry, 63 + the re-lease one-liner, re-acquire, reap dead-vs-live, dry-run,
  five-way take race (exactly one winner), corrupt record refused. Red-before is N/A (new helper).

### The process-watcher zone exemption (B940, #258)

The `eqemu-server` image's `process-watcher.pl` (bind-mounted at
`assets/scripts/`) `kill -9`s any process whose
lifetime-average CPU exceeds 30 % after 60 s unless its ps line matches an allowlist
regex -- and the allowlist omits `zone`, so a freshly-respawned zone busy loading a
client was killed mid-load (iceclad ~65-75 s, overthere ~38 s on the Pi; the client's
`ECONNREFUSED` was the kill of its own zone pid; `refactor/B925/README.md`). The lanes'
stacks carry the exemption in the stack's own configuration, provisioned persistently by
`scripts/eqemu/watcher_provision.sh --lane N` (under the lease). A provision run with
the container up kills every live watcher form and starts the replacement itself,
with `PROCESS_WATCHER_EXCLUDE` passed explicitly (docker exec's env is not the
entrypoint's) and re-reads its `/proc/<pid>/environ` as proof.

- **the file**: `AKK_DIR/assets/scripts/process-watcher.pl`, patched by
  `scripts/eqemu/b940_patch_watcher.pl` -- the allowlist regex becomes
  `PROCESS_WATCHER_EXCLUDE`-driven, and the **upstream regex stays the default**, so a
  stack without the env var behaves exactly as before (the patch is a literal splice,
  `perl -c`s the patched text before it replaces the file, and refuses when the B940
  marker is already present).
- **the value**: the `eqemu-server` service's `environment:` gains one line
  (`scripts/eqemu/b940_patch_compose.pl`):
  `PROCESS_WATCHER_EXCLUDE=(bin|/bin)/zone`. The value is a bare regex body compiled
  with `qr//` by the watcher -- no delimiters and no backslashes, because both are
  eaten by the compose env -> shell -> `qr//` chain. It exempts exactly the zone
  binary's argv wherever it sits on the ps line (`./bin/zone iceclad`,
  `bin/zone overthere`, `/bin/zone`, `/home/eqemu/server/bin/zone qeynos2`); no
  non-zone eqemu argv carries `bin/zone`, so the exemption cannot leak.
- **verify**: `scripts/eqemu/watcher_selftest.sh --lane N` (read-only) fails if the
  watcher lacks the B940 env-read, if the upstream allowlist is no longer the DEFAULT,
  or if the compose file lacks the exact `PROCESS_WATCHER_EXCLUDE` line. The provision
  run itself re-proves the surviving value on the lane host through the provisioned
  code path (both directions: every zone argv exempt, every non-zone argv not) and,
  with `--busy-zone` (~3 min), proves the exemption live both ways: a CPU burner
  carrying the REAL `./bin/zone` argv (a `yes` copy installed as `bin/zone` in a
  scratch dir) survives past the 60 s window under the provisioned watcher, and the
  same burner is killed by a second watcher run with the env removed (the upstream
  default) -- the kill line quoted from the watcher's own `process-kill.log`.
- **proof it fixed the kill**: with the lane provisioned, iceclad's `zone_link_hold`
  (330 s hold) exits 0 on lane 2 -- the B925 R1 case that dropped ~65-75 s in before.

| | |
| --- | --- |
| GitLab | `https://gitlab.goldfarm.local`, project `willeq/willeq` (id 1) |
| Primary runner | **`thinkpad`** (runner id 7, 172.16.200.205, B1087, Will 2026-10-16): runs EVERY job, x86 and ARM — tags `x86-build`, `arm-nfs`, `goldfarm`, `x86-gpu`, `x86-build-fast`, runs untagged, `concurrent = 1` (3 until 2026-10-18), in `ci-build.slice` beside the lane-5 oracle ("The ThinkPad primary runner" below) |
| x86 runners | **gitlab-runner-1** and **gitlab-runner-2** (10.0.30.81/.82, B1008): shell executor (bash), `concurrent = 1` each, **both tagged `x86-build`** since 2026-10-11 — one pipeline's stages may run on different hosts (B1026, "Two x86 runners" below). Paused by B1087; **un-paused 2026-10-18 (Will) with the ThinkPad's full tag set and run-untagged**: docker + the NAS share on `/mnt/projects` + `/var/cache/ccache-ci-arm` provisioned, so they take ARM jobs too (one job at a time on each of the three runners) |
| ARM runner | `dev-u24-arm-nfs` on dev-u24, tag `arm-nfs` (T503, below). **Paused since B1087**, not unregistered |
| Former host | **hermes-ext** (172.16.200.100): runner id 1, untagged; it ran every job until B1008 (2026-10-08) and no longer builds |
| Config | `.gitlab-ci.yml` at the repo root |

The rest of this section describes the original hermes-ext runner (T502). Its
*description* in the GitLab UI reads "inspur-shell". That label is stale — it
was registered from hermes-ext on 2026-09-12 and has always executed there
(`Running on hermes-ext...` in every job log; GitLab's own runner record
reports `ipAddress` 172.16.200.100).

**The Inspur must never build.** Will's ruling (2026-09-17): the inference box's
CPUs belong to the GPUs. Every job's first line is therefore a host
allow-list (`.goldfarm`'s `before_script`; it was `hermes-ext` alone before
B1008)

    case "$(hostname)" in hermes-ext|gitlab-runner-1|gitlab-runner-2|thinkpad) ;; *) echo "refusing to build on $(hostname)"; exit 1 ;; esac

so if the project is ever reassigned to another runner the pipeline fails loudly
instead of quietly moving builds onto the Inspur. hermes-ext also hosts the
hermes gateway and its kanban workers, so every heavy command is bounded to
`JOBS` (8 of the 16 cores) and runs under `nice -n 10`: a pipeline must not
starve a live worker.

## The ThinkPad primary runner (B1087)

Will (2026-10-16): the ThinkPad (lane 5, `docs/thinkpad.md`) runs **every** CI job, x86 and ARM; runners 1
(hermes-ext), 5, 6 (gitlab-runner-1/2) and 2 (dev-u24-arm-nfs) were **paused, not unregistered** (un-pause
with `PUT /api/v4/runners/<id>` `paused=false`). **2026-10-18 (Will): runners 5 and 6 are un-paused again
with the same tag set as the ThinkPad and run untagged**, so every job has three candidate hosts and each
runner takes ONE job at a time (`concurrent = 1` on all three). For the ARM jobs each of the two VMs got
`docker.io`, the `willeq-arm-noble` / `willeq-arm64-noble` images (loaded from the ThinkPad), the NAS share
on `/mnt/projects` (so `scripts/ci/vendor_fetch.sh` and `scripts/pi/publish_arm*_build.sh` see the same
store and publish dirs) and an empty `/var/cache/ccache-ci-arm`; the job allow-lists in `.gitlab-ci.yml`
name them. The box also hosts the lane-5 oracle (headless Xorg :99 + the
Wine official client + the `/opt/akk-stack` eqemu containers), so the builds run under resource control:

- **`ci.slice` / `ci-build.slice`** (`/etc/systemd/system/`): `CPUWeight=30`, `IOWeight=30`,
  `MemoryHigh=24G`. The dash is hierarchy (`ci-build.slice` lives UNDER `ci.slice`), so the weights must be
  on `ci.slice` to compete at the root with `system.slice` and `user.slice` (default 100): with only
  `ci-build.slice` weighted, a lane-5 oracle login under a full pipeline took 273 s against 127 s idle;
  with `ci.slice` weighted, 173 s (#407). Idle, a build still has all 12 threads. `IOWeight` is inert on
  this box's NVMe (scheduler `none`); IO pressure stayed near zero under load, CPU is the contended
  resource.
- **The runner runs AS `gitlab-runner`, without `--user`** (drop-in
  `/etc/systemd/system/gitlab-runner.service.d/b1087.conf`: `Slice=ci-build.slice`, `User=gitlab-runner`,
  `UMask=0002`, `SHLVL=1`). With `--user` the root runner `su`s into each job, and PAM's `pam_systemd`
  moves the job shell into `user-996.slice/session-*.scope`, OUT of the slice (measured). The config is
  owned by gitlab-runner so the runner can rewrite its own token.
- **Docker jobs** (the two ARM cross-builds) start containers through dockerd, which would put them in
  `system.slice`. The runner's `environment` puts `/usr/local/lib/ci-build/bin` first on `PATH`; the
  `docker` wrapper there adds `--cgroup-parent=$CI_DOCKER_CGROUP_PARENT` (`ci-build.slice`) to
  `docker run` and passes everything else through. Image builds (`docker build`, BuildKit) stay in
  dockerd's cgroup; they are layer-cached after the first pipeline.
- **No GPU for jobs**: gitlab-runner is NOT in `video`/`render`. With them, the GL tests opened the
  Intel iGPU through DRM (a 3840x2160 modeset on the laptop panel) instead of surfaceless llvmpipe, and
  19 pixel tests failed (MR !211's first pipeline, 1420); without them the jobs see what the runner VMs
  saw. Without the groups, surfaceless EGL still found NVIDIA's EGL (its `/dev/nvidia*` nodes are world-
  readable; `EGL (surfaceless): ... vendor=NVIDIA`), which has no ETC1, so the census decoded every
  texture (`textures_decoded` 14 MB vs 0.34 MB on runner-1, 110 of 118 zones over budget). The runner's
  `environment` therefore pins Mesa: `__EGL_VENDOR_LIBRARY_FILENAMES=/usr/share/glvnd/egl_vendor.d/50_mesa.json`
  and `LIBGL_ALWAYS_SOFTWARE=1` (llvmpipe, as on the runner VMs; this also keeps CI off the oracle's
  Quadro). A future `x86-gpu` hardware-GL job needs its own grant and must unset both.
- **One runner, shared checkouts**: `concurrent = 3` gives each slot its own checkout
  (`builds/<runner>/0..2`), and an x86 job can land in a slot an ARM job used. The ARM cross-build's
  container writes `build-arm*-noble/cache` as root, which gitlab-runner's `git clean` cannot remove, so
  the global `GIT_CLEAN_FLAGS` keeps both ARM trees as well (pipeline 1427's hygiene died in the clean).
- **`CI_JOBS=8`** from the runner's `environment`: `.goldfarm`'s `before_script` exports it as `JOBS`, and
  `build-arm64` as `BUILD_JOBS`, so the host, not this file, sizes its builds.
- **ccache** at `CCACHE_DIR=/var/cache/ccache-ci` (NVMe root, `max_size = 30G` in its `ccache.conf`).
- **ARM ccache** (B1087 follow-up): a slot that never built ARM compiled both cross-builds from scratch
  (pipeline 1428: build-arm 1,451 s, build-arm64 1,719 s). The two ARM jobs set
  `ARM_CCACHE_DIR=/var/cache/ccache-ci-arm`; `scripts/ci/arm_ccache.sh` mounts
  `$ARM_CCACHE_DIR/<image>` into the build container as `CCACHE_DIR` (10G per image) and the Dockerfiles'
  entrypoint adds `ccache` as the compiler launcher only when `CCACHE_DIR` is set. The container sees
  every slot's checkout at `/src` and its tree at `/build`, so the compile lines match across slots
  (`CCACHE_BASEDIR=/src`, `CCACHE_NOHASHDIR=1`). The directory is provisioned on the host
  (`install -d -o gitlab-runner -g gitlab-runner`); where it is missing the build runs without ccache.
  Each ARM log ends with `=== ccache (this build) ===` (this compile's hits, from a per-build stats log)
  and the cache totals.
- **The ARM publish path is the same NFS share** (`/mnt/projects`, the NAS 10.0.30.10, already in the
  ThinkPad's fstab), so `ci-arm/<sha>/` and `ci-arm64/<sha>/` land exactly where the SBC lanes read them.
  gitlab-runner (uid 996) is in group `user` (gid 1000) so it can write the group-writable `ci-arm64/`;
  `ci-arm/` is open to all on the NAS. Host tools for the publish: `docker-buildx`,
  `binutils-arm-linux-gnueabihf` (the armhf strip runs on the host).

## The ARM runner and Pi runs (T503)

A second runner, **`dev-u24-arm-nfs`** (GitLab runner id 2, tag `arm-nfs`, does NOT run untagged), runs in user
mode on the dev host `dev-u24`: binary `~/.local/bin/gitlab-runner`, config `~/.gitlab-runner/config.toml`
(GitLab's self-signed cert pinned beside it), `systemctl --user status gitlab-runner` (linger on), builds under
`~/gitlab-runner/builds`. It runs two jobs, **`build-arm`** and B1030's **`build-arm64`** (the aarch64 lanes'
binary, "The aarch64 SBC lanes" below), one at a time in ONE shared checkout, so both jobs' `GIT_CLEAN_FLAGS`
keep both build trees (`-e build-arm-noble -e build-arm64-noble`). `build-arm` is `scripts/build-arm-noble.sh` (docker image
`willeq-arm-noble`) then `scripts/pi/publish_arm_build.sh`, which publishes the stripped client plus that
commit's tracked `config/` + `data/` to **`/mnt/projects/fable/ci-arm/<sha>/`** (`by-branch/<branch>` symlinks;
newest 12 kept). The job is automatic on `fable` and a manual, non-blocking job everywhere else
(`glab ci trigger build-arm`), and refuses any host but `dev-u24` and `thinkpad` (B1087). Both ARM jobs write the whole build to
`build-<arch>-noble.log`, kept as a job artifact (14 days); a failure prints the `error:` lines and the tail,
so an ARM-only compile error reaches the trace (B1054's did not through the old `| tail -n 40`).

Why there: hermes-ext has no docker and no NFS; the Orange Pi mounts the same NFS share as the dev host. A
hermes-ext worker therefore runs a Pi leg entirely over ssh — NFS paths exist **on the Pi**, not on the worker:

    source scripts/pi/env.sh; pi_lock_take kb-<slug>          # single-tenant Pi, see scripts/pi/lock_selftest.sh
    D=/mnt/projects/fable/ci-arm/$(git rev-parse HEAD)
    pi_remote "cd $D && WILLEQ_DATA_ROOT=$D WILLEQ_CACHE_DIR=/tmp/<slug>/cache $D/bin/willeq --drm --renderer gles2 --constrained orangepi -r 1280 720 -c <config> > /tmp/<slug>/willeq.log 2>&1"
    pi_remote cat /tmp/<slug>/willeq.log > evidence.log;  pi_lock_release kb-<slug>

That recipe runs WITHOUT `CAP_NET_ADMIN` (the NFS binary cannot carry it; B751/#73's zone-in burst needs the
forced receive buffer). For a live zone-in, copy the binary to the Pi's ext4 home (NOT `/tmp`: tmpfs mounted
`nosuid` ignores file capabilities) and `sudo setcap cap_net_raw,cap_net_admin+ep` it. The client logs the
buffer it actually got (`rcvbuf=... SO_RCVBUFFORCE`). `scripts/pi_rendered_zone_sweep.sh` does exactly this
for the rendered Kunark + Velious sweep (#102, `refactor/B854/RENDERED_SWEEP.md`).

### The aarch64 SBC lanes (B1030, #350)

The Orange Pi is **lane 1** of the lane table in `scripts/pi/env.sh` (lanes 1-3, B1030; 4, B1056; 5, B1060); `PI_LANE=N` picks the device the way
`EQEMU_LANE` picks an eqemu VM, and a card or a script names its LANE, never an address. Each lane is
single-tenant with its own lock ON the device (`pi_lock_take`/`pi_lock_release` are unchanged; `PI_LANE=2 bash
scripts/pi/lock_selftest.sh` is the per-lane net). A `PI_HOST` alone still picks its lane, so every pre-B1030
caller behaves as before, but a `PI_LANE` that disagrees with a listed `PI_HOST` is refused (exit 2), never
rewritten: `PI_HOST` is exported, so a shell that sourced `env.sh` for lane 1 must `unset PI_HOST` before
`PI_LANE=3 bash <script>` (`scripts/pi/env_selftest.sh`, ctest `PiEnvLaneResolution`). The table itself (host, arch, GPU driver) is in `scripts/pi/env.sh` -- read it there rather
than here.

| lane | device | arch | GPU / driver | binary |
|---|---|---|---|---|
| 1 | `orangepi` | armv7l | Mali-400, Lima | `ci-arm/<sha>/bin/willeq-nollvm` (the shipped prefix above) |
| 2 | `rock64` (RK3328) | aarch64 | Mali-450, Lima (system Mesa) | `ci-arm64/<sha>/bin/willeq` |
| 3 | `rock4d` (RK3576) | aarch64 | Mali-G52, Panfrost (system Mesa) | `ci-arm64/<sha>/bin/willeq` |
| 4 | `a7s` (Allwinner A733) | aarch64 | PowerVR BXM-4-64, IMG DDK 24.2 blob (`pvr_dri.so` behind IMG's Mesa-derived EGL/GBM) | `ci-arm64-bullseye/<sha>/bin/willeq`, built ON the board (B1056) |
| 5 | `orangepizero` (H2+) | armv7l | Mali-400, Lima, on a **vkms** card (no display output) | `ci-arm/<sha>/bin/willeq-nollvm` (as lane 1) |

Lanes 2 and 3 run ONE generic `-march=armv8-a` binary from **`scripts/build-arm64-noble.sh`**
(`docker/Dockerfile.arm64-noble`, a noble arm64 sysroot; lane 3's Armbian resolute runs it because glibc is
forward compatible), published to **`/mnt/projects/fable/ci-arm64/<sha>/`** by `scripts/pi/publish_arm64_build.sh`
-- the same shape as `ci-arm/` (stripped `bin/`, that commit's `config/` + `data/`, `BUILD_INFO`,
`by-branch/`, newest 12 kept) minus the Mesa prefix step. `build-arm` is NOT widened: the sibling job
**`build-arm64`** (same runner, same rules -- automatic on fable, manual elsewhere via `glab ci trigger
build-arm64`) runs it; the runner is `concurrent=1`, so a fable pipeline's two ARM builds run one after the
other, and a hand-run `scripts/build-arm64-noble.sh` on dev-u24 takes `BUILD_JOBS=4` while any other build is
alive. `docker/Dockerfile.arm64-noble` is the armhf Dockerfile with the architecture
swapped and nothing else; `scripts/ci/check_arm64_dockerfile.sh` (ctest `DockerfileArm64Derived`, label
`hygiene`) fails when they drift, and `--write` regenerates it.

Device facts that bound a run: both aarch64 lanes mount the NFS share at `/mnt/projects`, have `/tmp` and
`/mnt/ram` as `nosuid` tmpfs (copy a binary to the ext4 home for `setcap`, exactly as on lane 1), and run the
`orangepi` preset like lane 1 -- RULED (Will 2026-10-12, B1033 #353, `docs/decisions.md`): one preset on every
lane, the same 256 MB bound and 64 MB target, no goldens of their own until a lane runs the gate; a lane
preset is for an experiment (`pi_rendered_zone_sweep.sh --constrained <preset>`), never a default or a gate.
Lane 2's Mesa is the same
25.2.8 the Pi runs, so the Lima cost sheet applies; lane 3 is Bifrost under Panfrost and nothing Lima-specific
transfers to it.

**Lane 4 is the odd one out** (B1056, #376, `docs/reference/radxa_a7s.md`). The Radxa Cubie A7S runs Debian 11
bullseye (end-of-life since 2026-08-31; its apt sources point at `archive.debian.org`) on Radxa's vendor 5.15 kernel,
its GPU is Imagination's, not Mali, and three things the other lanes take for granted do not hold there, so
`scripts/pi/env.sh` carries them per lane instead of letting a rig assume them: the ssh user is `radxa`
(`PI_USER`); the noble-sysroot `ci-arm64/` binary needs `GLIBC_2.38` and the board has 2.31, so the lane's binary
is built on the device by `scripts/sbc/a7s/build_native.sh` (gcc 10, cmake 3.18, the same patched DRM/GLES2
Irrlicht and configure flags as the cross build) and published under **`ci-arm64-bullseye/<sha>/`**
(`PI_LANE_PUBLISH`, same shape as `ci-arm64/`); and the IMG libraries live in `/usr/local/lib` with the DRI driver
in `/usr/local/lib/dri`, which an ssh session does not see (`/etc/environment` there uses `export`, which pam_env
ignores), so every run on the lane is prefixed with `PI_RUN_ENV` (`LD_LIBRARY_PATH` + `LIBGL_DRIVERS_PATH`) --
`kmscube_floor.sh` and `lane_smoke.sh` do this; a hand run must too. The vendor kernel has no NFS client and the
NAS refuses SMB from `172.16.200.x`, so `/mnt/projects` is an **sshfs** mount of dev-u24's share (fstab,
`user@10.0.30.23`, the board's key in dev-u24's `authorized_keys`): the same paths, slower reads, and a run that
streams zone assets is reading them through dev-u24. Its display is `card0-DP-1` (`PI_HDMI_CONN`); the image's
`sddm` was disabled because the display manager held DRM master on `card0`. Nothing Lima- or Bifrost-specific
transfers to it; re-measure.

**Headless DRM.** The two boards share one KVM, so a lane's HDMI is usually `disconnected` and the DRM device
refuses to start. `pi_hdmi_force_on` (`scripts/pi/env.sh`) writes `on` to the connector's sysfs `status`, which
makes the kernel offer its default modes with no monitor, and `pi_hdmi_restore` puts `detect` back; both are
no-ops on a connected lane. `scripts/pi/lane_smoke.sh` (`PI_LANE=N lane_smoke.sh <sha>`: `kmscube_floor.sh`
then a 300-frame `gles2_zone_benchmark` on DRM) and `scripts/pi_rendered_zone_sweep.sh` use them. The sweep
is the ledger rig for every lane: on an aarch64 lane it defaults to `ci-arm64/by-branch/fable`, the system Mesa
(no LLVM-less prefix exists for aarch64) and the `orangepi` preset (`--constrained <preset>` overrides, for the
measurements a per-lane preset ruling needs); the client runs in its own cgroup and zones in live against the
eqemu lane, so the `[memgpu]` row and the rendered-zone TSV row are both device figures. Evidence from the
first runs: `review/B1030/README.md`.

**Lane 2 GEM census (B1036, #355).** The arm64 image now ships the B962
`lima_bo_census.so` owner shim. The rendered owner census in
`refactor/B1036/README.md` finds that the Rock64 charges its display scanout
to the client's `memcg_shmem` `gpu_buffers` row, unlike the Orange Pi, and
that its EGL surface has colour BOs beyond the logical depth/stencil tally.
The existing 10 MiB unnamed-GEM sweep still fails; B1036 records a class-C
ledger-decomposition ruling request, not a bound or preset change.

**Lane 5, the Orange Pi Zero** (B1060, #380; `docs/reference/orangepizero.md`) is lane 1's sibling: the same armhf
`ci-arm/` publish and shipped Mesa prefix, the same preset and ceilings, on Armbian **trixie** (glibc 2.41; the noble
build runs unmodified once the GL runtime packages are installed). The board has no HDMI and its device tree leaves
the display engine and TV encoder `disabled`, so `card0` is Lima, render-only, and the client's DRM device (there is
no surfaceless path on ARM) finds no connector. On lane 5 `pi_hdmi_force_on` loads the stock **vkms** module instead
(a connected `Virtual-1`; Mesa's kmsro pairs it with the Lima render node) and `pi_hdmi_restore` unloads it, so
`lane_smoke.sh` and `pi_rendered_zone_sweep.sh` run there unchanged. The scanout goes nowhere: frames are rendered
by Lima and flipped on a virtual CRTC, so lane 5 measures memory and GPU cost, never a picture on a screen.

**Not a lane: the ODROID XU4** (B1045, #365) is Will's interactive play-test box. It runs the same armhf
`ci-arm/by-branch/fable` publish on Panfrost, and needed a self-built Panfrost module whose power-on path
re-selects the GPU's PLL clock mux -- the Exynos sub-CMU otherwise leaves the GPU at 24 MHz under mainline
(`docs/reference/odroid_xu4.md`, `scripts/sbc/xu4/`).

### The shipped Mesa prefix (B935, #253)

Will, 2026-09-30: "ship the prefix" (`docs/decisions.md`). The Pi runs the client on an LLVM-less Mesa (B932's
build, `refactor/B932/README.md`) that ships beside it, selected by an absolute DT_RPATH. The system Mesa packages
on the Pi are never touched.

- **The artifact.** `scripts/pi/build_mesa_nollvm.sh` (docker image `willeq-mesa-nollvm-armhf`, dev-u24 only) packs
  the prefix into a deterministic `mesa-nollvm-<version>-armhf.tar.zst`, pinned by SHA256 in
  `scripts/pi/mesa_nollvm.sha256`. The store is `ci-arm/mesa/<version>/` on NFS (the tarball, its `.sha256`,
  `BUILD_INFO`, the meson option dump, and `prefix/` = the tarball extracted plus `.willeq-mesa-pin`). Never in git,
  never pruned. The version, the Pi install path (`MESA_NOLLVM_PI_PREFIX`) and the store path are named once, in
  `scripts/pi/mesa_nollvm.env`. The prefix is built for that one Pi path (libgbm and libgallium carry it compiled
  in), so a path or version change is a rebuild plus a re-pin (`--write-pin`) in one commit.
- **Publish.** `build-arm` runs `build_mesa_nollvm.sh --ensure`, which verifies the store against the pin and builds
  only when the pinned tarball is missing (a build whose bytes differ from the pin is never installed: the job
  fails). It then adds to `ci-arm/<sha>/`: `bin/willeq-nollvm` (the stripped `bin/willeq` with
  `DT_RPATH=<Pi prefix>/lib`, `scripts/pi/mesa_nollvm_select.sh`), a `mesa` symlink to the store dir, and the Mesa
  version / tarball sha / Pi prefix in `BUILD_INFO`. `bin/willeq` itself is unchanged and loads the system Mesa. If
  the Mesa step fails, everything else is still published, without `bin/willeq-nollvm`, and the job is red.
- **Why an absolute DT_RPATH, not `$ORIGIN` or `LD_LIBRARY_PATH`.** The setcap'd client runs in secure-execution
  mode (`AT_SECURE=1`). There `ld.so` ignores `LD_LIBRARY_PATH` and drops an `$ORIGIN` rpath entry unless it
  expands into a trusted system directory (B935 measured both, `refactor/B935/README.md`). It must be DT_RPATH, not
  DT_RUNPATH, so that glvnd's `dlopen("libEGL_mesa.so.0")` from the system `libEGL.so.1` also searches it (B932).
- **The rigs use the prefix by default.** `scripts/pi_rendered_zone_sweep.sh` (and so `scripts/pi/collision_ab.sh`
  and `scripts/pi/mesa_ab.sh`) stages `bin/willeq-nollvm` and installs the prefix on the Pi with
  `scripts/pi/deploy_mesa_prefix.sh` (rsync `--checksum --delete` from the NFS store to the Pi's ext4 home;
  the pin is checked on both ends). **`WILLEQ_MESA=system`** (or `--mesa system`) is the opt-out: it stages
  `bin/willeq`. A build dir published before B935 has no `bin/willeq-nollvm`, so the default refuses it (exit 68).
  Use a newer build, or the opt-out.
- **Every run proves it.** The sweep always snapshots `/proc/<pid>/maps` at `startup` (plus any `--snap-points`).
  Each row records the mapped libgallium and `llvm_mapped`. A run that loaded the other Mesa fails as
  `mesa_mismatch` and aborts the sweep with **exit 67**. A run with no snapshot fails as `mesa_unproven`.
  `scripts/pi/kmscube_floor.sh` prints the same two facts per run.
  **The client says so too (B946, #264):** at the first GL init every
  graphics run logs ONE
  `[gldriver] renderer='<GL_RENDERER>' gallium=<path|none> llvm=<path|none>`
  INFO line — the GL renderer string and the libgallium/libLLVM the process
  mapped from its own `/proc/self/maps` (format: `refactor/B946/README.md`).
  A `willeq-nollvm` whose baked-in prefix directory is missing logs an ERROR
  variant naming the missing directory and the fallback (the default search
  path, i.e. the system Mesa) before the INFO line, and continues — a hand
  run on a fresh Pi must still work. (B1016/#337: the check tests the
  prefix's `lib/` directory itself — the rpath entry the publish bakes in is
  `<prefix>/lib`, and it is returned as-is, not stripped — so a PARTIAL
  deploy that lost only `lib/` is caught instead of passing and silently
  falling back to system Mesa. The real-run net is
  `test_gl_driver_census`'s `RealRunPrintsTheGlDriverLine` (spawns `willeq`),
  the format contract is `GldriverLineFormatContract`
  (`scripts/ci/test_gldriver_line_format.py` round-tripping
  `tests/gldriver_line_emit` through `mesa_driver_field`), and the CI gate's
  own run proves it too: the `gate:` job's `gles2_zone_benchmark` now emits
  the same one-time line and `visual_gate.py`'s `assert_gldriver_line`
  requires it to be present and parseable in every zone's benchmark stdout.
  `src/client/core/gl_driver_census.cpp` is linked into the benchmark for
  this.) The sweep's summarizer reads the line
  back and reports `driver=` in the result row (`mesa_driver` TSV column,
  additive — the maps-snapshot proof above stays the primary evidence).
- **By hand** (Pi lock held): `bash scripts/pi/deploy_mesa_prefix.sh`, then run `$D/bin/willeq-nollvm`, or a setcap'd
  ext4 copy of it. For another binary (kmscube, `gles2_zone_benchmark`), use
  `scripts/pi/mesa_nollvm_select.sh <in> <out>`.

### The vendored ARM archives (B970, #288)

`docker/Dockerfile.arm-noble` builds Irrlicht 1.8.5 and takes the NEON sources from libpng 1.6.37. It no
longer downloads them: SourceForge returned HTTP 522 for ~3 h on 2026-10-01 and every `build-arm` whose
`docker/irrlicht-drm/*` change invalidated the layer failed.

- **The store** is `/mnt/projects/fable/ci-arm/vendor/` on NFS (`VENDOR_STORE` overrides), never in git:
  `irrlicht-1.8.5.zip`, `libpng-1.6.37.tar.xz`.
- **The pins** are `docker/vendor.sha256` (`sha256sum -c` format). The Dockerfile `COPY`s the pin file and
  re-checks the archives inside the image.
- **`scripts/ci/vendor_fetch.sh --ensure`** verifies the store against the pins, downloads only what is missing or
  wrong (SourceForge first, then `sources.buildroot.net`, then `ftp.osuosl.org` for libpng; every download must hash to
  the pin), and prints the store path. `--check` verifies only. If a pinned file is neither in the store nor
  obtainable it exits 1 naming the pin. `build-arm` runs it as its first script line, and
  `scripts/build-arm-noble.sh` runs it again and passes the result as `--build-context vendor=<store>`.
- **Layer cache.** The archives are `COPY --from=vendor` before the `docker/irrlicht-drm/*` copies, so editing
  those sources does not touch the archive layers, and no step before the Irrlicht compile needs SourceForge.
- **Refreshing a pin.** Put the new file in the store, change the name/hash in `docker/vendor.sha256` and the
  source URLs in `vendor_fetch.sh`, and commit the pin. Never commit the archive.
- The x86 bootstrap (`scripts/build-gles2-x86.sh`) never downloaded: it copies `/mnt/projects/irrlicht-1.8.5`.

### The ARM build container dies with its job (B1093, #413)

A cancelled or timed-out `build-arm`/`build-arm64` used to leave its cross-build container compiling: the
runner kills the job shell and the docker CLI, dockerd keeps the container until its command ends. On
2026-10-18 the ThinkPad carried four such orphans at `-j8`/`-j12` (load average 46) beside its real jobs.
`scripts/ci/arm_container.sh` is the fix, in two halves, pinned by the `ArmContainerGuard` hygiene test:

- **Sourced by both build scripts.** `arm_container_guard <prefix>` names and labels the container
  (`willeq.ci.arm=1`, `--init`) and traps EXIT/TERM/INT/HUP to `docker kill` it; `arm_container_run <args>`
  runs `docker run --rm` **in the background and `wait`s** -- bash defers a trap while a foreground child runs
  (measured: SIGTERM to the shell left a foreground container alive), but a signal interrupts `wait`, the
  trap fires, and the container's own exit status still comes back to the script (`rc=7` round-trips).
- **A detached watchdog** (`setsid`, stdio on `/dev/null`) started by the guard polls the script's PARENT and
  the script itself every few seconds and `docker kill`s the container when either is gone. This is the
  layer the runner's own cancel needs: measured on gitlab-runner-1 (job 8777, 2026-10-18 02:57Z), a cancel
  killed only the job shell -- the build script was reparented to PID 1 with no signal, its trap never ran,
  the container compiled on for 7 minutes until killed by hand, and the job sat in "canceling" the whole
  time because the orphan still held the runner's pipe. A SIGKILL of the parent shell now takes the container
  down in about 2 s (the script exits 137); a hand run's parent is the interactive shell, so a closed
  terminal kills the build too.
- **`bash scripts/ci/arm_container.sh --sweep`** is the first line of both CI ARM jobs: it kills every
  labelled container on the host and prints what it killed. The runners are `concurrent = 1`, so a labelled
  container alive at job start is always an orphan (a SIGKILL of the script itself skips every trap and the
  watchdog needs a few seconds). The sweep is deliberately not in the build scripts, where a dev box may
  legitimately run two.

## The stages

| Stage | What it does | Cold | Warm |
| --- | --- | --- | --- |
| `hygiene` | `ctest -L hygiene` in a no-C++ tree (`EQT_GRAPHICS=OFF`), configured into `build/hygiene`; on `fable` also `prune-build-trees` (B1012, the per-ref tree expiry + census) | 45 s | 44 s |
| `build` (`build-test`, B1042) | the Release recipe from `CLAUDE.md`, `-j8`, into `build/ref/<slug>` (B1012), then in the SAME job full `ctest -j6`, minus two cases, then one of them with retries | build 1,237 s (20.6 min) + suite 116 s (4,908 tests) | build 6 s + suite 116 s |
| `build` (`build-headless`, B1038) | the same Release build with `EQT_GRAPHICS=OFF` into `build-headless/ref/<slug>`, then `ctest -LE hygiene` in the same job ("The headless build" below) | not yet measured on a runner; dev-u24 `-j4`: ~1,920 s cold (B1038) | not measured |
| `gate` | `visual_gate` (T80/T291) over `build-gles2/ref/<slug>/bin/gles2_zone_benchmark` (B1012) | 363 s | 18 s |
| | B813 (2026-09-20): the manifest names FIVE zones (qeynos2, gfaydark, befallen + iceclad + timorous); `gate` verifies all five (`--zones`, B903/B904 2026-09-29) | — | 40 s for all five in one job (measured back-to-back on a quiet hermes-ext: 10.6 s wall for three zones, 19.6 s wall for five; +9.0 s wall) |
| | B929 (#247): `zone-budget-census`, the full 118-zone sweep + `zone_budget_census.py --check-budgets` ("The zone-content sweep" below) | not yet measured on a runner; hermes-ext: 4 m 46 s of zone time + benchmark build | 4 m 46 s of zone time (warm benchmark build) |

(T502's own measurements on hermes-ext, when build and suite were still two
jobs; the suite does the same work either way — only the two build trees are
warm or cold. B1042's ccache changes what "cold" costs: "ccache on the x86
runners" below. A cold `gate` includes building the
patched Irrlicht *and* the whole `build-gles2` tree.)

"Warm" is not one number. A pipeline whose commit touched no source rebuilt
nothing (6 s), while the next one — a **docs-only** commit — still recompiled 5
test TUs and relinked 34 binaries, costing 403 s. With no ninja on the runners
a warm build's cost is dominated by *linking* (ccache, B1042, saves compiles,
never links), so expect anywhere from seconds to ~7 minutes. Since B1042 a
docs-only diff does not build at all ("Docs-only pipelines run hygiene only"). What makes a docs-only commit relink was not run to
ground. A fully warm pipeline — nothing to rebuild — is about **3 minutes** end
to end.

Every job's `before_script` re-creates the `data/eq_client` and
`data/eqe1_client` symlinks. They are gitignored (`tests/test_data_paths.cpp`),
so `git clean -ffdx` removes them before every job. B815 (issue #83): without
them the asset suites now behave by decision, not by accident —
`tests/asset_requirement.h` (`requireEqClientAssets`) makes an absent asset
root a GTEST_SKIP locally, and a hard FAIL when `WILLEQ_REQUIRE_ASSETS=1` is
set, which the `build-test` job exports (a green pipeline proves the assets were
found, so CI can never skip them silently). `test_asset_requirement` pins
both outcomes. A task worktree without the symlink therefore runs green with
skipped asset suites instead of three fake reds.

`hygiene` is first and deliberately cheap: a bad doc count, an over-size file or
a stale citation fails in under a minute instead of after a 30-minute build.

`test` excludes exactly two cases and handles both explicitly:

- `visual_gate` — belongs to the `gate` stage (it needs the separate
  `build-gles2/` tree), and would otherwise just SKIP with exit 125.
- `RDPServerFrameTest.UpdateFrameMultipleTimes` — a documented intermittent
  race. It is re-run alone with `ctest --repeat until-pass:3`.

`gate` is **not** `allow_failure` — hermes-ext has both client asset trees
(`/mnt/projects/EverQuestP1999`, `/mnt/projects/EverQuest_ETC1`) and the
pristine Irrlicht source, so nothing in it is legitimately skippable. Pillow is the one thing
the job has to install for itself: hermes-ext's `python3` is 3.12 with no PIL and no `ensurepip`, so the
  job installs it once into `~/.cache/willeq-ci/pylibs` using the pip from the
  host's existing `~/.venvs/ci` (3.11) driving the 3.12 interpreter, and exports
  `PYTHONPATH`. It is a no-op on every later run.

**`build-gles2/` is not in scope for the B907 prune.** No orphan check is
registered against it: `scripts/build-gles2-x86.sh` configures that tree with
`-DEQT_BUILD_TESTS=OFF`, so `tests/CMakeLists.txt` (and with it
`check_build_tree_orphans`) is never included there at all, and the gate's own
prepare step only ever builds two named targets (`willeq`,
`gles2_zone_benchmark`) incrementally by name rather than the whole graph.
There is therefore nothing for a prune to be a regression net FOR in that
tree; not adding one there is a scope decision, not an oversight.

**The gate is one blocking job over all five manifest zones.** From
2026-09-28 to 2026-09-29 iceclad and timorous ran in a separate
`allow_failure` job, `gate-kunark-velious`, while they overran two ledger
budgets (iceclad `zone_parse`, timorous `zone_mesh`; `refactor/B813/README.md`).
B903/B904 fixed both in code and removed that job — the budgets were never
widened (Will, 2026-09-28; `docs/decisions.md`). `--zones` only selects zones —
there is no waiver list and no per-zone budget — and the hygiene test
`VisualGateLedgerAssertions` fails unless the job's one list equals the
manifest's zones exactly and the job blocks.

**The gate measures the client's post-load state, not the parse peak**
(B914, 2026-09-29). The benchmark's `loadZone()` ends with the same zone-source
release the client runs at Step 13, through the same function,
`S3DZone::releaseSourceAfterLoad()`. So the memory JSON's `s3dZoneSourceBytes`,
`totalEstimate` and the ledger's `zone_parse` live bytes no longer count region
source geometry and texture pixels the client has already freed. The
parse-time high-water mark is still in each category's `peak`, but **no budget
is asserted against `peak` or against any mid-load point**: every budget is
checked once, at the end of the run. B927 derived the `zone_parse` budget from
that point, and mid-load `zone_parse` is several times higher
(`refactor/B927/README.md`).

B943 (2026-09-30) ratcheted six more orangepi budgets to the same convention
(measured worst x 1.25, whole MiB): `archives` 3, `textures_decoded` 2,
`entity_models` 38, `gpuTextureBytes` 38, `textureCacheBytesUsed` 51 and
`characterModelsBytes` 2 MiB; `audio` was held at 32 (`refactor/B943/README.md`).
The table is in `config/constrained_presets.json` and pinned in
`tests/test_constrained_renderer.cpp`; do not copy the numbers here.

B955 (2026-10-01) added the `gpu_buffers` row to the memory JSON's `ledger`
object: `gpuBuffersBytes` and a `gpuBuffers` object with the row's source and
inputs (`include/client/core/gpu_buffer_census.h`), beside `ownedRssAnonBytes`
(RssAnon − gpu_driver) and `ownedBytes`. Since B978 (2026-10-01, Will "1) yes")
**`ownedBytes` is the 64 MB target figure**: RssAnon − gpu_driver with
`gpu_buffers` added back when the row is OUTSIDE RssAnon
(`gpu_buffers_outside_rss_anon`). On the gate's llvmpipe the source is
`logical_in_gpu_driver` -- the buffers are heap already inside `gpu_driver`, so
nothing is added again (no double count) and `ownedBytes` equals
`ownedRssAnonBytes`; on the Orange Pi the row is outside RssAnon and counts.
The `ownedPlusGpuBuffersBytes` field B955 shipped was removed by B978 (it equals
`ownedBytes` now). **Nothing asserts the target**: the 256 MB bound stays the
failing one, on whole RssAnon; the row exists for the Orange Pi, where it is
the only figure that sees Lima's GEM buffers. On a kernel+driver that publishes
`drm-*` memory keys (Panfrost, the ROCK 4D lane) the source is `drm_fdinfo` and
the row is the RESIDENT figure (B1032, #352): `drm-total-memory` includes
Panfrost's 128 MiB growable tiler heap before the tiler has faulted it in, and
the `[memgpu]` line carries `fdinfo_total=`/`fdinfo_resident=`/`fdinfo_purgeable=`
so the choice stays visible. The fdinfo read is never made against the IMG
PowerVR module (`pvrsrvkm`, lane 4): its `pvr_show_fdinfo` panics the kernel on
any read of a DRM fd's fdinfo (B1056, trace in `review/B1056/lane4_attempts/`).
The guard fails closed (an fd whose driver has no sysfs row is not read either),
and `WILLEQ_GPU_CENSUS_NO_FDINFO=1` (in lane 4's `PI_RUN_ENV`) skips the read for
every driver. With fdinfo skipped `decideGpuBuffers` falls through in order to
`memcg_shmem`, `drm_maps_lower_bound`, then `logical`: lane 4's smoke row is
`source=drm_maps_lower_bound`. `scripts/ci/owned_memory.py` is
the one Python definition of these figures, behind the `[rss-target]` line and
the Pi summarizers.

B1035 (2026-10-18, #354) names the lane-3 gap the issue's after-fix table left
unexplained: the render node's resident figure is below the exclusive cgroup's
shmem by the process's SCANOUT FRAMEBUFFERS -- shmem-backed DRM objects
charged to the cgroup, published by panfrost as `drm-shared-memory`, but
resident in NO node (the rockchip `card0` node's fdinfo carries no memory key
at all). `decideGpuBuffers` therefore takes the shmem over the resident figure
when the cgroup is exclusive and it is higher, and the `[memgpu]` line names
the delta `fdinfo_scanout_gap=<B>` (after `memcg_kernel_seen=`, before the
B978 `owned=` fields; `fdinfoScanoutGapBytes` in the JSON). The `scanout_fb=`
pair carries the device's own tally of the same bytes; the mechanism, the
four-source capture and the fixtures are in
`docs/reference/rock4d_fdinfo_gap.md` (the Lima lanes are unaffected: they
publish no `drm-*` key and take the shmem as the row already).

## Reading a pipeline (worker or human)

A human: the pipeline list is linked from the project page, and every job page
has the full log. From a shell — the helper handles the token and the
self-signed cert:

    scripts/gitlab/gql 'query{project(fullPath:"willeq/willeq"){pipelines(first:1){
      nodes{ iid status jobs{ nodes{ id name status duration } } } }}}'

`CiJob` has **no** `webUrl` field on this GitLab (19.3) — ask for `id` and read
the log over REST, which is also the only way to get a failing job's output:

    curl -sk -H "PRIVATE-TOKEN: $(cat ~/gitlab_token.txt)" \
      https://gitlab.goldfarm.local/api/v4/projects/1/jobs/<id>/trace

Each job also uploads its logs as artifacts, kept **7 days**: `hygiene-ctest.log`,
`ctest.log` + `build/Testing/Temporary/LastTest.log`, `gate.log` +
`build-gles2/ci_artifacts/` (the gate's diff images).

## Scope: which pushes build

`workflow.rules` — branches `task/*` and `plans/*`, merge requests, and `fable`.
**Never** on tags. A branch with an open MR builds once (as the MR pipeline), not
twice.

### Push with `-o ci.skip`, then open the MR (B1042, #362)

    git push -o ci.skip -u gitlab task/<UNIT>-<name>
    glab -R gitlab.goldfarm.local/willeq/willeq mr create --target-branch fable ...

**Why:** the workflow rule above suppresses a branch pipeline only once an MR
exists, so the FIRST push of a new branch runs a full branch pipeline, and the
MR opened a minute later runs the same commit again as its MR pipeline. Two
x86 builds of one sha, each ~30 minutes of a `concurrent = 1` runner. `-o
ci.skip` creates no pipeline for that push; the MR pipeline is the one that
counts (the merge gate reads the MR's head pipeline). Later pushes to a branch
with an open MR need no option: they only create the MR pipeline. A branch
that will never get an MR (an experiment you want CI on) is pushed without it.

### The rbuild job: worker builds through CI (B1208, #527)

`scripts/hermes/rbuild --via ci` (or `RBUILD_VIA=ci`) is the worker build path
that needs no ssh to any build host: the x86 runner VMs answer no inbound port
from hermes-ext or the ThinkPad (`refactor/B1194/README.md`, "Why the
inventory and the proof run as CI jobs"), but every runner polls GitLab, so a
pushed pipeline reaches them. The ssh path (the ThinkPad, `--host`) is
unchanged and stays the default.

    rbuild --via ci <worktree> --target <t> [<t>...]
    rbuild --via ci <worktree> test -R <regex> [--target <t>...]
    rbuild --via ci <worktree> hygiene
    rbuild --via ci --status

rbuild commits the worktree as a WIP commit `rbuild: ci build <what>` on its
branch (always a new sha; it stays on the branch; refused on a detached HEAD,
on `fable`, and on a tree whose `.gitlab-ci.yml` has no `rbuild` job), pushes
it with `-o ci.variable=RBUILD_TARGET=<t...>` (plus `RBUILD_TEST=<regex>` for
`test`), finds the pipeline by sha, streams the `rbuild` job's trace, downloads
its artifacts into `<worktree>/.rbuild-ci/<pipeline id>/` (gitignored) and
exits 0 on a `success` job, 1 otherwise, 2 when it refused or found no
pipeline. Ctrl-C (or a killed worker) cancels the job. The header of
`scripts/hermes/rbuild` lists every variable.

The pipeline such a push creates holds **the `rbuild` job alone**:

- `workflow:rules` admits a push carrying `RBUILD_TARGET` on any branch but
  `fable`, ABOVE the `$CI_OPEN_MERGE_REQUESTS` suppression (a branch with an
  open MR still gets its rbuild pipeline), and drops the MR pipeline of a
  commit titled `rbuild: ci build ...`, so a WIP push never runs the suite;
- every other job's rules start with `!reference [.not_rbuild, rules]`
  (`if: $RBUILD_TARGET` -> `when: never`), directly or through `.code_rules`
  -- `hygiene`, `prune-build-trees`, both ARM jobs, `client-ledger-live` and
  the B1194 include's jobs included.

The `rbuild` job (stage `build`, `extends: .goldfarm`: tag `x86-build`,
`interruptible`, the before_script's ccache + asset links) runs the shared
`willeq_build_tree` into this ref's warm tree (`$BUILD_TREE`, the build-test
configure: Release, GLES2, tests ON):

| `RBUILD_TARGET` | the job |
|---|---|
| `<t> [<t>...]` | `cmake --build --target <t...>`; then `ctest -R "$RBUILD_TEST" --no-tests=error` when `RBUILD_TEST` is set |
| `auto` (`test` without `--target`) | `scripts/ci/rbuild_test_targets.py` reads the CMake file-API codemodel and builds the executables whose sources declare a gtest case the regex matches (gtest discovery is PRE_TEST, so an unbuilt binary lists only `<target>_NOT_BUILT`); then the ctest |
| `hygiene` | the hygiene job's configure + `ctest -L hygiene` |

`WILLEQ_BUILD_TARGETS` (the `rbuild` job's targets) makes `willeq_build_tree`
build named targets only, and a partial build leaves only the ref-only stamp:
it never claims the tree for B1026's host+sha verify. Artifacts (kept **2
days**): `rbuild-ctest.log`, `rbuild-junit.xml` (also the job's junit report),
`rbuild-LastTest.log`. GL targets build on any x86 runner here (build-test
already builds `model_viewer` on all three); the ThinkPad pin is an ssh-path
rule. Measured runs, overhead against the ssh path and the runner census:
`refactor/B1208/README.md`. Pinned by `CiQueueRules` (group 5) and
`RbuildTestTargets`.

### Docs-only pipelines run hygiene only (B1042, #362)

`build-test`, `gate`, `zone-budget-census` (B929) and `client-ledger-live`
(and `build-headless`, B1038)
carry `rules:` (the `.code_rules` anchor in `.gitlab-ci.yml`) that add the job
only when **at least one changed path does not end in `.md`**. `hygiene` has no
code rule and always runs (its one rule is B1208's `.not_rbuild`, below), so a pipeline whose diff is only `*.md` files — anywhere,
`refactor/` and `review/` notes included — is the hygiene stage alone (plus
`prune-build-trees` on fable). Anything else in the diff (a `.tsv`, a `.png`, a
script, `.gitlab-ci.yml`) builds as before. No non-hygiene ctest test reads a
`.md` file, so nothing the skipped jobs would have run can change with one.

What "changed" is compared against (GitLab's `rules:changes` semantics, read
from the GitLab 19.x CI YAML reference, `rules:changes` / `compare_to` /
`regexp`; this instance is 19.3.1, `GET /api/v4/version`):

| pipeline | compared against |
|---|---|
| merge request | the MR's changes against its **target branch** (GitLab: "Merge request pipelines, `rules:changes` compares the changes with the target MR branch") |
| `fable` push (every merge) | the pushed range, i.e. the merge's diff |
| `fable` without a push (schedule, "Run pipeline") | nothing: GitLab evaluates `changes` as true, everything runs |
| `task/*`, `plans/*` push | `compare_to: refs/heads/fable` — without it GitLab treats a NEW branch's first push as "everything changed" |

The test is one Ruby regexp, `rules:changes:regexp: '\A(?!.*\.md\z)'` (a
path that does not end in `.md`), because glob `changes:` can only express
"any of these paths changed", never "a path outside these changed". GitLab adds
the job when ANY changed path matches. To force the full pipeline anyway, run
it with the variable `WILLEQ_FULL_PIPELINE=1`.

**A docs-only pipeline is green.** Jobs excluded by `rules` are never created
(GitLab: "If no listed files have changed, do not add either job to any
pipeline (same as `when: never`)"), so the pipeline holds only the hygiene
jobs and its status is theirs: `success` when hygiene passes. The solo
orchestrator's merge gate (`merge_ok.sh` on hermes-ext) reads only
`head_pipeline.status == success` and the pipeline sha, never job names, so it
passes such an MR exactly as it passes a full one. Proven on a probe MR:
`review/B1042/README.md`. The hygiene test `CiQueueRules`
(`scripts/ci/test_ci_queue_rules.py`) pins the rules, the regexp's meaning
over sample paths, and that `hygiene`'s rules are only the `.not_rbuild`
exclusion plus an unconditional `when: on_success`.

## The warm tree, and its trade-off

Each runner keeps one working directory per project, and the pipeline keeps four
paths in it across runs (`build-headless` since B1038):

    GIT_CLEAN_FLAGS: -ffdx -e build -e build-gles2 -e build-headless -e third_party/irrlicht-gles2-x86

GitLab passes these straight to `git clean` (its own default is a bare `-ffdx`),
so `-e <pattern>` excludes those paths from the wipe. Everything else is still
cleaned on every job. This is what makes a pipeline incremental — a from-scratch
Release build on this host is ~30 minutes, and the patched x86 GLES2 Irrlicht is
built, not committed (`scripts/build-gles2-x86.sh`), so rebuilding it every time
would be pure waste. `build/hygiene` is a subdirectory of the preserved `build`
tree for the same reason: its CMake cache and its googletest `FetchContent` clone
survive too.

### One warm tree per ref (B1012, issue #333)

Inside those two preserved roots, **each ref builds into its own subtree**:
`build/ref/$CI_COMMIT_REF_SLUG` (`$BUILD_TREE`) and
`build-gles2/ref/$CI_COMMIT_REF_SLUG` (`$GLES2_TREE`). The slug is the SOURCE
branch for a merge-request pipeline too, so a branch's push pipeline and its MR
pipeline share one tree and stay warm against each other. The cause: on the
`concurrent = 1` runner the stages of two pipelines interleave, and with one
shared `build/` MR !119's `test` stage (pipeline 697, 2026-10-08) ran over
binaries MR !118's `build` stage (698) had just written — it failed on
`JumpFallArc.*` and `BridgeIntentTable.*`, tests that did not exist in its own
source. `--prune` (B907, below) could not see that: those binaries DID have
targets in the other branch's graph. With a tree per ref the only leftovers a
pipeline can meet are its own ref's earlier commit's, which `--prune` handles.

The `build` and `gate` jobs stamp the tree they use (`.willeq_ref`: the full ref
name + the job's UTC time, `scripts/ci/prune_ref_build_trees.py --stamp`). The
expiry the shared layout never had is the **`prune-build-trees` job**
(hygiene stage, `needs: []`, automatic on every `fable` pipeline, manual and
non-blocking elsewhere): it deletes a tree whose stamped ref is no longer a
branch on the remote (`git ls-remote --heads`), whose stamp is older than 14
days, or that has no stamp; sweeps the pre-B1012 shared tree's entries at the
two roots (everything except `ref/` and `hygiene/`); and prints every decision
with the tree's size and age plus `du`/`df` for the roots. **`prune.log` on the
latest fable pipeline is the census** — read it to see what the runner holds.
The script refuses an empty live-ref list (a failed `ls-remote` must never
read as "every branch is gone") and any root that is not
`<build|build-gles2|build-headless>/ref` (the third since B1038); `scripts/ci/test_prune_ref_build_trees.py`
(ctest `PruneRefBuildTrees`, hygiene label) pins each rule. Measured
2026-10-09 on gitlab-runner-1: one ref's two trees are ~4.4 GB; the disk had
206 GB free, so ~40 live refs fit before the 14-day expiry matters.
`scripts/build-gles2-x86.sh` honours `GLES2_BUILD_DIR` and `visual_gate.py`
honours `WILLEQ_GLES2_BUILD_DIR` for this; a developer's checkout keeps the
default `build/` and `build-gles2/`.

**The trade-off is still real within a ref:** a stale object file, a stale
CMake cache or a half-written artifact can survive into the next pipeline OF
THE SAME REF, so a green pipeline is evidence about an *incremental* build, not
a clean one. To force a cold run:

- run the pipeline with `GIT_CLEAN_FLAGS` overridden to `-ffdx` (Run pipeline →
  add the variable), **or**
- on EACH x86 runner (gitlab-runner-1 and gitlab-runner-2, as root — each
  holds its own warm trees): `rm -rf /home/gitlab-runner/builds/*/0/willeq/willeq/{build/ref/<slug>,build-gles2/ref/<slug>,build-headless/ref/<slug>}`
  for one ref, or the whole `{build,build-gles2,build-headless,third_party/irrlicht-gles2-x86}`
  for everything.

Do the cold run before trusting a green pipeline on anything that changed the
build system itself. Since B1042 a wiped tree still compiles from the ccache
(it lives outside the checkout); add `CCACHE_DISABLE=1` to the pipeline
variables for a cold compile as well as a cold tree.

### ccache on the x86 runners (B1042, #362)

**Why:** each ref builds in its own tree (B1012), so every NEW branch was a cold
~30-minute Release build even though nearly all of its objects are byte-identical
to fable's. ccache 4.9.1 is installed on gitlab-runner-1 and gitlab-runner-2
(shell executor as user `gitlab-runner`, cache `/home/gitlab-runner/.cache/ccache`,
`max_size = 20.0 GB` in `~gitlab-runner/.config/ccache/ccache.conf`); hermes-ext
has none.

- **The launcher.** `.goldfarm`'s `before_script` checks `command -v ccache`.
  When it exists it sets `CCACHE_CMAKE_ARGS` to
  `-DCMAKE_C_COMPILER_LAUNCHER=ccache -DCMAKE_CXX_COMPILER_LAUNCHER=ccache` and
  exports the two `CMAKE_*_COMPILER_LAUNCHER` env vars; when it does not, the
  args are `-U...` so a warm tree drops a stale launcher. Every x86 configure
  passes `$CCACHE_CMAKE_ARGS`: `hygiene` (no compile, harmless), `.build_tree`
  (`build-test`, `client-ledger-live`'s rebuild, `build-headless`), and the
  gate's warm `$GLES2_TREE` (re-configured before its incremental build).
- **`scripts/build-gles2-x86.sh`** (the gate's cold bootstrap and the Irrlicht
  bootstrap) honours the same env vars: its configure passes them as `-D`, and
  the Irrlicht `make` runs `CC="ccache gcc" CXX="ccache g++"`. Cheap, so it is
  in: the bootstrap re-copies the pristine source each time, but ccache hashes
  content, so a stamp-triggered rebuild recompiles only the device TUs that
  changed. A developer's run without the env vars is unchanged.
- **Paths.** `CCACHE_BASEDIR: $CI_PROJECT_DIR` rewrites the checkout's absolute
  paths to relative ones, and every per-ref tree is the same depth
  (`build/ref/<slug>`), so a new branch's compile lines match fable's.
  `CCACHE_NOHASHDIR=1` keeps the working directory out of the hash should a
  `-g` build appear (Release has no `-g` today).
- **Reading the win.** After every `cmake --build` the job prints
  `ccache --show-log-stats` — THIS job's hits and misses, from a per-job stats
  log (`CCACHE_STATSLOG=$CI_PROJECT_DIR/ccache-stats.log`, reset by
  `git clean`) — and `ccache -s`, the host's running totals. Look for
  `=== ccache after build/ref/<slug>` in the `build-test` log. The host totals,
  any time:

      ssh 10.0.30.81 sudo -u gitlab-runner ccache -s    # gitlab-runner-1
      ssh 10.0.30.82 sudo -u gitlab-runner ccache -s    # gitlab-runner-2

  First cold and warm figures: `review/B1042/README.md`.
- **Linking is not cached.** A relink-heavy warm build costs what it did.

### Two x86 runners: the host+sha stamp (B1026, #346)

**Both x86 runners carry `x86-build`** (Will retagged gitlab-runner-2 from
`x86-build-2` on 2026-10-11 so two pipelines build in parallel). The stages
still pass no artifacts, so a pipeline's `build` job and its `test` job could
(until B1042 folded them into `build-test`) land on DIFFERENT hosts, and each host has its own warm `$BUILD_TREE`. Before
B1026 the `test` job then ran whatever binary the other host's tree held.
**Measured:** fable pipeline 809 (at `0136137b`) built on gitlab-runner-1
(job 4006) and tested on gitlab-runner-2 (job 4007). The failing
`SpellLandHookIsGone` assertions cited `test_spell_manager_timing.cpp:793-796`
`EXPECT_NE`, the PRE-B603 source; the commit under test had `EXPECT_EQ` at
735-738. That was a false red, and the same split produces a false green: MR
pipeline 805 was green only because pipeline 804 had happened to build the
same sha on runner-2 (`review/AUDIT-WAVE8/sections/b603.md` finding 1).

**The mechanism.** The build steps exist once, as the shell function
`willeq_build_tree` defined by the `.build_tree` anchor:

1. a **ref-only** stamp (`prune_ref_build_trees.py --stamp <tree> --ref <ref>`)
   before the configure. It is what the prune reads, and it wipes any earlier
   build claim, so a build that fails part-way leaves a tree no job trusts;
2. Irrlicht bootstrap if missing or stale, configure, the B907 orphan prune,
   `cmake --build`;
3. the **build claim** (`--stamp <tree> --ref <ref> --host "$(hostname)"
   --sha "$CI_COMMIT_SHA"`), written only after `cmake --build` succeeded
   (errexit + pipefail: a failed build never reaches it).

The claim is the same `.willeq_ref` file with two more lines, `host=` and
`sha=`; the prune's parser ignores them, so there is one stamp format.

`build-test` (and `build-headless`) run the function. `build-test` then, and
`client-ledger-live` first, run (via the `.verify_build_tree` anchor)
`prune_ref_build_trees.py --verify "$BUILD_TREE" --host "$(hostname)" --sha "$CI_COMMIT_SHA"`,
which exits 0 only when the claim names this host AND this commit. On a
mismatch they print a loud line, run `willeq_build_tree` in-job, then verify
again (a second failure fails the job), and only then run ctest or the client.
In `build-test` the verify follows the job's own build, so it MATCHES unless
the claim step itself broke.
`gate` needs no verify: `.visual_gate_prepare` already builds its own
`$GLES2_TREE` in the gate job, on whichever host it landed.

**Reading a job log.** Every verify prints one line:

    stamp MATCH build/ref/<slug> (stamp read): stored host=gitlab-runner-1 sha=<sha> ref=<ref> at=<utc>; expected host=gitlab-runner-1 sha=<sha>

On a split it says `stamp MISMATCH`, followed by
`!!! B1026 STAMP MISMATCH: ... REBUILDING IN-JOB ...`, the
`=== willeq_build_tree: ... ===` build output, a second `stamp MATCH` line and
`B1026: in-job rebuild done and verified on <host>`. `stored host=<none>`
means the tree was never claimed: no stamp, or a ref-only stamp left by a
build that failed or was cancelled. A stored host equal to the expected one
with a different sha means this host's tree is from an older commit of the
ref (the build job ran elsewhere this time).

**The cost, and why `build` and `test` are now one job (B1042, #362).** When
the stages split, the test job paid one build on its own host: incremental if
that host had built this ref before, a cold Release build if it had not.
**Measured:** over the 50 newest pipelines that ran both jobs (ids 820..909,
2026-10-12), `build` and `test` ran on different runners in **25 (50 %)**,
and in 15 of those the `test` job ran over 600 s, i.e. rebuilt
(`python3 scripts/ci/ci_host_split.py`; the table is
`review/B1042/host_split_build_test.tsv`). On top of the rebuild, every stage
boundary is one more wait in the runners' queue behind every other
pipeline's jobs. B1042 therefore folded the two into ONE job, `build-test`:
build, then the suite, on one host. The `gate` stage still follows it (it
builds its own tree and verifies nothing). The verify stays in `build-test`
after the build, where it always MATCHES — it is the net should the claim
step ever move — and in `client-ledger-live`, which can still land
elsewhere. Passing `bin/` as artifacts stays rejected (gigabytes per
pipeline). The regression net is the hygiene test
`CiBuildTreeStamp` (`scripts/ci/test_ci_build_tree_stamp.py`), which fails if
`test` reaches ctest without the verify, if the claim is written before
`cmake --build`, or if `.goldfarm` claims a single tagged runner again;
`PruneRefBuildTrees` pins `--verify`'s outcomes. Evidence:
`refactor/B1026/README.md`.

### The headless build (B1038, #358)

**What it catches.** The `--no-graphics` client is the `EQT_GRAPHICS=OFF`
configuration, and until B1038 no job compiled it: `hygiene` configures OFF
but builds no C++, and `build` (now `build-test`) configures ON. B1031's macOS probe (#351)
found, and its Linux cross-check (`review/B1031/linux_crosscheck_pass2.tsv`)
confirmed on dev-u24, that it no longer compiled on ANY host: 5 TUs
(`application.cpp`'s steady_60s census reading `m_renderer`, three tests
including `irrlicht.h`, and the T260 cases of
`test_zone_connection_flow_identity.cpp`). Behind those, 8 test executables
did not LINK (cases calling renderer TUs from `GRAPHICS_SOURCES`) and 5 cases
asserted graphics-only behaviour (`refactor/B1038/README.md`). The fix is
guards only: a case that exercises a renderer TU or a graphics-only path is
`#ifdef EQT_HAS_GRAPHICS`, a test whose every case does is registered only
under `EQT_GRAPHICS`; the headless cases of each file keep running.
`build-headless` makes that rot a red pipeline: it configures
`-DEQT_GRAPHICS=OFF -DEQT_BUILD_TESTS=ON`, builds everything, and runs
`ctest -LE hygiene` (the `hygiene` job already runs the other label).

**How it is built.** It runs the SAME `willeq_build_tree` as `build-test` (one
copy of the B1026 order: ref-only stamp, configure, B907 orphan prune,
`cmake --build`, host+sha claim). The job sets `WILLEQ_HEADLESS=1`, which
picks the OFF configure line and skips the Irrlicht bootstrap; nothing else
in the function differs. Its tree is its own, `build-headless/ref/<slug>`
(job-level `BUILD_TREE`), so the graphics tree is never reconfigured OFF and
each stamp describes one tree. `GIT_CLEAN_FLAGS` keeps `build-headless`, and
`prune-build-trees` expires it like the other two (`--root
build-headless/ref`; it is never a `--legacy-root`, it had no pre-B1012
layout). The suite runs in the build job itself, after the claim, so the
build and the suite can never land on different hosts and the job needs no
B1026 verify. `WILLEQ_REQUIRE_ASSETS=1` as in `build-test`: an absent asset root is
a FAIL, never a silent SKIP.

**Ordering.** It is in the `build` stage with no `needs:`, so it waits for
`hygiene` like `build-test`, the two run in parallel when both runners are
free, and `gate` waits for both (a red `build-headless` therefore also skips
`gate`, the same as a red `build-test`). Like `build-test` it is skipped on a
docs-only diff (B1042, `.code_rules`).

**The cost.** A second full Release build per runner per ref (it shares no
objects with `build/ref/<slug>`), warm after the first pipeline of a ref.
Measured on dev-u24 (40 cores, `-j4`, no ccache, 2026-10-12): a cold headless
tree took ~1,920 s (1,534 + 352 + 32 s over the three passes that built it)
and `ctest -LE hygiene` 48 s (3,533 tests); the runner's `-j8` number is the
first pipeline's job log.

**The cereal dependency** (same unit): `include/common/net/packet.h` and
`daybreak_structs.h` include `<cereal/cereal.hpp>`, which `CMakeLists.txt`
never looked for (a host without it got 217 opaque errors on macOS). It is
now `FIND_PATH(CEREAL_INCLUDE_DIR cereal/cereal.hpp)` with a `FATAL_ERROR`
naming `libcereal-dev` (Debian/Ubuntu) and `cereal` (Homebrew); it is not
vendored.

**Regression net.** `CiBuildTreeStamp` (`scripts/ci/test_ci_build_tree_stamp.py`)
fails if the job is gone, leaves the `build` stage, keeps its own copy of the
build steps, loses its own tree or `WILLEQ_HEADLESS`, stops running `ctest
-LE hygiene` after `willeq_build_tree`, or if `.build_tree` loses its OFF
configure, `GIT_CLEAN_FLAGS` its exclusion or `prune-build-trees` its root;
`PruneRefBuildTrees` pins that `build-headless/ref` is a known root.

**macOS** (B1039, #359): the same headless configuration builds and passes its
suite on the M4 mac mini; no CI job runs there. The recipe (Homebrew line,
configure, ctest environment, the NAS mount) and what is not supported on a
Mac (renderer, gate, memory-ledger numbers) are in `docs/macos.md`.

### A prior branch's leftover binary is pruned, not just detected (B907)

The warm tree has a second concrete failure mode beyond the input-stamp one
below: `cmake never prunes bin/` (`scripts/ci/check_build_tree_orphans.py`'s
own long-standing docstring, T175). Deleting a target from `CMakeLists.txt`
leaves its last-built binary sitting in `build/bin/` forever — a fresh
`cmake --build` does not touch it. On a per-branch build tree that binary is
merely embarrassing; on this runner's ONE shared warm tree it is toxic across
branches: MR !47 (pipeline 313, job 1443) failed on `test_region_world_triangles`,
a binary built by the unmerged branch `task/B909-client-spatial-ledger`'s
pipeline and still present when !47's own pipeline reconfigured next. The
`check_build_tree_orphans` ctest test (in the suite, `build-test` since B1042) was correctly
reporting that condition — it is the CI *feed* into that check that was
polluted, not the check.

The build step (`willeq_build_tree`, in `build-test` since B1042) now runs
`scripts/ci/check_build_tree_orphans.py --prune build`
immediately after that job's own `cmake -S . -B build …` reconfigure and
before `cmake --build`. `--prune` shares its target discovery with the
read-only check byte-for-byte (`_discover()` in the script — one function,
called by both `run_check()` and `run_prune()`, so the two can never disagree
about what an orphan is) and deletes every orphan it finds, printing each one
removed. It refuses outright (exit 2, deletes nothing) on a directory with no
`CMakeCache.txt`, or whose `bin/` resolves (`os.path.realpath`) outside the
build tree itself; a `bin/` that does not exist yet is a silent no-op success,
not a refusal. Every path it removes is `os.path.join(<build>/bin, <a bare
os.listdir() entry>)` — no recursion, no traversal, nothing outside
`<build>/bin/` is ever touched.

**The plain `check_build_tree_orphans` ctest test is UNCHANGED in meaning.**
It still runs in the suite exactly as before, over the SAME `build/`
tree the build step just pruned (one `build-test` job since B1042) — a developer's local warm tree still gets
the original warning with no `--prune` involved, and if an orphan somehow
survives the CI prune (meaning the prune itself broke, not that the allowlist
needs an entry), the suite still fails outright on it. The `ALLOWLIST`
in the script stays EMPTY; a worker "fix" that added a shim entry there was
reverted — an allowlist entry hides a real orphan instead of explaining why CI
produced one, and B907 is why one showed up at all.

`--prune`'s own regression net is `scripts/ci/test_check_build_tree_orphans.py`
(ctest `CheckBuildTreeOrphansPrune`, hygiene label): a hand-built fake CMake
file-API reply in a fresh temp directory per test, asserting an orphan is
removed, a claimed binary is kept, a non-CMake-tree and an escaping `bin/` are
both refused untouched, nothing outside `bin/` is ever touched, and the
read-only check still fails before the prune runs and passes after. RED is
proven with `run_prune(..., delete=False)`: discovery still finds the orphan,
but it survives because the delete step itself was skipped — isolating that
it is the `os.remove()` call, not the shared discovery, that makes the real
path an actual prune.

**Stale ctest registrations were considered and ruled out as a second
mechanism.** `CTestTestfile.cmake` is not incremental — CMake regenerates it
whole on every reconfigure from the CURRENT `add_test()`/`ADD_TEST()` calls in
the CMakeLists graph, so a test `add_test`-registered by a since-reverted
branch simply does not appear in it after the next branch's reconfigure; there
is no separate stale-registration failure mode alongside the stale-binary one.
Verified empirically (2026-09-29): a two-`add_test()` CMakeLists reconfigured,
then trimmed to one and reconfigured again in the SAME build directory —
`CTestTestfile.cmake` held only the one that survived, with no trace of the
dropped one. Binaries and CMake's own bookkeeping of what to run behave
differently precisely because cmake **regenerates a file** (the ctest driver)
but **never deletes a file it once wrote to bin/**.

**Why not the other fixes on the table:**

- *Allowlist the binary.* Hides the exact condition the check exists to catch
  (a binary from sources nobody's CMakeLists claims); already tried and
  reverted once (this unit).
- *Weaken or disable the check.* The check is right for what it catches — a
  stale binary that still runs and passes. B907 is a CI feed problem, not a
  check problem.
- *Cold builds by default (`GIT_CLEAN_FLAGS=-ffdx`).* Removes the pollution
  by removing the warm tree entirely, but a from-scratch Release build on
  hermes-ext is ~20-30 minutes (measured above) against a warm pipeline's
  ~3 minutes-to-seconds; every pipeline paying that cost defeats the reason
  the warm tree exists at all.
- *A build directory per branch.* Also removes cross-branch pollution, and
  keeps the warm-tree speed *within* a branch, but multiplies disk use by the
  number of branches with pipeline history on a `concurrent = 1` runner that
  already has no ccache and no ninja, with no expiry mechanism proposed. Not
  chosen at B907; **chosen at B1012 (2026-10-09, Will) once `--prune` proved
  insufficient against concurrent pipelines, with the expiry mechanism added**
  — see "One warm tree per ref" above.

### Device-source changes are caught by the input stamp, not by a cold run

The warm-tree trade-off has one concrete failure mode that a cold run does not
catch automatically: the patched Irrlicht is built from `docker/irrlicht-drm/`
and `docker/irrlicht-drm-x86/` (plus the build script and the shared patch), and
none of that is committed — only `libIrrlicht.a` survives the clean, and the
build job only rebuilt it when the `.a` was *absent*. So a merged fix to the
device sources (e.g. B654's X11 numpad mapping) left every later pipeline
testing against the pre-fix lib, invisibly.

`scripts/ci/irrlicht_input_stamp.sh` closes that: `build-gles2-x86.sh` writes a
content digest (relative path + sha256 of every in-scope input, sorted) to
`third_party/irrlicht-gles2-x86/irrlicht_input_stamp.txt` each time it produces
the lib, and the build job's bootstrap now rebuilds when the `.a` is absent **or
when `verify` reports the stored digest no longer matches the committed inputs**.
The digest is over file contents, so it is identical on every checkout of the
same commit — no mtime or git-state dependence. The pristine Irrlicht reference
(`/mnt/projects/irrlicht-1.8.5`) is deliberately out of scope: it is a fixed
read-only source, and updating it is a manual cold-rebuild event.
Both halves were proven in the B654 pipeline: a commit that changed the device
sources rebuilt the warm lib (`stamp MISMATCH (stored=none ...)`, then
`stamp written`), and the follow-up docs-only push took 7 s with
`stamp matches (warm lib is current)` and no rebuild (`refactor/B654/README.md`).

## Retrying the gate

The gate's CPU-p95 assertion is load-sensitive by nature — it is a real CPU-time
measurement and hermes-ext is a shared host (`tests/cmake/eqe1.cmake` has the
full T261/T291 history). The retry is **inside** the job: up to 3 attempts, and
the gate must pass one of them. Only if all three fail does the job fail, and
`gate.log` then holds all three attempts.

To retry by hand, use the job's **Retry** button, or from the shell:

    curl -sk -X POST -H "PRIVATE-TOKEN: $(cat ~/gitlab_token.txt)" \
      https://gitlab.goldfarm.local/api/v4/projects/1/jobs/<id>/retry

A gate that fails all three attempts twice in a row is a real regression, not
load — pull `build-gles2/ci_artifacts/` from the artifacts and compare the
frames. Never re-bless goldens to make CI green.

## Two golden sets: x86 and Pi (B1024)

**The Mac set (B1044; ruled 2026-10-14, not blessed yet).** Will ruled the
memory figures platform-scoped (#364, option 1), so the Mac is a third set:
`tests/goldens/mac/<zone>/f<n>.png` + manifest `platforms.mac`, with the
Mac's own metrics and memory rows (`textureCacheBytesLimit`,
`physFootprintBytes`). On the M4 mini, `scripts/build-gles2-darwin.sh` builds
the ANGLE/Metal client and benchmark (the macOS counterpart of
`scripts/build-gles2-x86.sh`; ANGLE setup in `docs/macos.md`). `python3
scripts/ci/visual_gate.py --platform mac --bench
build-gles2/bin/gles2_zone_benchmark --capture DIR` stages the five zones plus
the memory-coverage legs and writes `DIR/manifest_mac.json`; a draw/state/poly
difference from the x86 manifest is reported (`x86Divergence`), not gated;
budgets and ledger invariants still fail it. `--platform mac` without
`--capture` verifies against the blessed section and says NOT YET BLESSED
until there is one. The bless is the orchestrator's commit of a staged set
that Will approved; the README in the B1044 evidence directory's goldens_mac/ names the files.

The pixel gate has ONE compare rule — `compare_images_exact`
(`scripts/ci/visual_gate.py`) — and TWO golden sets, because the two
drivers can never render byte-identical frames:

- **x86** (the CI `gate` job): llvmpipe, 16-bit depth (B971), goldens at
  `tests/goldens/<zone>_f<n>.png`, section `manifest["zones"]`. The CI
  job passes no `--platform`; nothing about it changed. The runners take
  the x86 device fork's surfaceless-EGL path (no /dev/dri grant), so the
  goldens are driver-stable. On a multi-GPU x86 box (lane 5, the
  ThinkPad: a headless NVIDIA Quadro on card0, the Intel UHD 630 eDP
  panel on card1) `--drm` card selection tries every `/dev/dri/card*` in
  numeric order and skips a card that opens without a connected
  connector and a CRTC, landing on the card that actually has a display
  (B1054, #374); `WILLEQ_DRM_DEVICE=/dev/dri/cardN` is an explicit
  single-path override whose failure is an error, never a silent
  fallback. Since B1034 the same `--drm` KMS path also SUCCEEDS where it
  previously died: the GBM surface's EGL-config choice
  (`chooseConfigForGbmFormat`, the logic shared by both device forks in
  `docker/irrlicht-drm/gbm_config_select.h`) used to take the first of a
  fixed 64, and a driver whose `eglChooseConfig` lists every 10-bit config
  ahead of every 8-bit one (a laptop eDP or 10bpc-capable panel offering
  more than 64 configs) cut the XRGB8888 config off that fixed array, so
  the fallback was `configs[0]` (10-bit) and the failure moved to
  `eglCreateWindowSurface` -- the caller's no-depth retry never ran. The
  selection now queries the count first, allocates, and prefers an 8-bit
  ARGB/XRGB visual, so an x86 host whose `/dev/dri/card*` has a connected
  connector and 10-bit configs takes the display through KMS instead of
  erroring. The CI `gate` job is unaffected: it has no /dev/dri grant and
  stays on the surfaceless path above.
- **Pi** (by hand, never a CI job): Mali-400 under the Lima driver on the
  Orange Pi with the shipped LLVM-less Mesa prefix, goldens at
  `tests/goldens/pi/<zone>/f<n>.png`, section
  `manifest["platforms"]["pi"]` (its own `blessedUnder`, per-zone
  `metrics`/`memory`). Rendered under the Pi lock
  (`scripts/pi/env.sh`) with
  `visual_gate.py --platform pi --bench <published gles2_zone_benchmark-nollvm> --capture DIR`;
  the run carries the Mesa-prefix maps proof (libgallium under the
  shipped prefix, libLLVM never mapped) and never asserts the p95 CPU
  ceiling (Pi TIMING is H04's lane: `scripts/pi/compare_pi.sh`).

Why two sets: the Pi-vs-x86 residual (B980) is entirely coverage flips at
alpha-cutout edges (Lima's FS is FP16; Mali's hardware sampler vs
llvmpipe's software filter turns a sub-LSB sampling difference into a
whole-pixel flip) plus polygon/far-clip subpixel rules, and the x86
goldens are 16-bit depth while the Pi renders 24-bit — a cross-platform
compare can never be exact. (B1023, #343, named the Pi's 24-bit depth
measured: it is NOT the fable chooser and NOT the hardware — Mesa's
`driCreateConfigs()` (25.2.8 `dri_screen.c`) skips the Z16 format for
32-bit color because `lima_screen.c` does not set
`caps->mixed_color_depth_bits` (panfrost does — the ROCK 4D lane answers
depth=16 for the same request); `refactor/B1023/README.md`.) The Pi IS
deterministic (B971: 0 px across two binaries in 15 of 15 frames), so one
exact-match rule works per set.

**A Pi golden moves only on a Pi re-bless by Will; an x86 re-bless never
touches the Pi set, and vice versa.** The Pi verify fails `NOT YET
BLESSED` (exit 1) until the section exists — it never falls back to the
x86 set. The Pi goldens are post-B976 as of 2026-10-19 (#417, B1097:
re-blessed on lane 1 at fable tip `c03c44a0`; `refactor/B1097/README.md`);
a Panfrost lane, if gated, is judged with a frame-wide 1-LSB bound (Will's
2026-10-19 ruling — no per-lane golden sets, no code). The live-client sweep (`scripts/pi_rendered_zone_sweep.sh`) is
not a pixel channel: it drives the LIVE client, whose frames are not
frame-locked (B997 measured 0/18 identical across two baseline legs), so
it can never be an exact pixel compare.

## The zone-content sweep (B852, issue #98)

The blocking `gate` renders five zones: three classic ones plus iceclad and
timorous. Will's 2026-09-20 ruling
(`docs/decisions.md`): "velious and kunark zones are part of the willeq target,
so we need to verify that content works." The zone set behind that ruling is
the 118-zone T490 census (`refactor/T490_every_classic_zone_fits_the_pi.md`:
"all 118 original/Kunark/Velious archives"), and nothing but the sweep loads
them all:

```bash
# Full 118-zone sweep (human/orchestrator tool, not a pipeline stage):
bash scripts/ci/zone_content_sweep.sh

# The 3-zone smoke leg (one classic, one Kunark, one Velious):
bash scripts/ci/zone_content_sweep_smoke.sh
# or: ctest --test-dir build -R zone_content_sweep_smoke
```

The sweep runs `gles2_zone_benchmark` — the gate's memory-coverage invocation
(`--eqe1-path/--objects/--entities/--test-races`, tiny frame count) — ONE
process per zone, heaviest `.s3d` first, with a per-zone wall-clock timeout
(300 s) and a log-silence watchdog (120 s), and writes one TSV row per zone:
exit code, wall time, FATAL/ERROR counts, the first FATAL line, missing-texture
and missing-EQE1 counts, placeholder-model count, whether the loader reached
its success line, and peak RssAnon from the `[memledger]` lines. A row is
clean iff: exit 0, zero FATAL/ERROR, the loader's `Zone loaded in` line
present, no placeholder models, and peak RssAnon under the 256 MB ceiling
(`docs/decisions.md`, T293 envelope). A zone that dies on the 4 GiB worker
cap or times out is a RESULT (its row says so), not a reason to stop. Results
TSV + per-zone logs go to `build/b852_sweep/` by default
(`--out-dir` to move them; the committed 118-row TSV and the summary table
live under `refactor/B852/`).

The zone list is `scripts/ci/zone_content_sweep_zones.tsv` (zone, expansion
tag, s3d bytes, notes), generated by `scripts/ci/zone_content_sweep_zones.py`
from the census plus `docs/reference/pre_luclin_zones.json`'s expansion lists
(two census zones it omits are tagged from `tools/generate_zone_lines.py` and
cited in the notes column). The ctest leg (`zone_content_sweep_smoke`, label
`slow`, in the `build-test` job) runs the smoke list on every push and skips loudly
(exit 125) when the benchmark binary or the gitignored EQ asset symlinks are
absent; CI's `build-test` job re-makes the symlinks and sets `WILLEQ_REQUIRE_ASSETS=1`,
so a skip there is impossible. `--zones` names that are not in the list fail
loudly (exit 3) instead of sweeping zero zones.

**The sweep also asserts the budgets: the `zone-budget-census` gate-stage
job** (B929, #247). The sweep's own "clean" row is load-cleanness plus the
256 MB RssAnon ceiling; a zone that loads fine but exceeds a memory budget
was invisible to CI (B929's growthplane: 28,687,788 B of
`placeableObjectsBytes` against 25,165,824 — fixed in the loader by B936b,
asserted here). The job first runs
`scripts/ci/zone_census_pack_selfheal.sh` before the sweep: the runners read
the shared NFS pack tree, and a STALE pack (B930/#248's soldunga v1
signature) FATALs the benchmark's all-or-nothing texture load — the sweep
would then die on an asset, never asserting that zone's budget. The
runner cannot repair the NFS tree (read-only for it), so the script builds
a LOCAL pack tree the sweep reads with `--eqe1-path`: every swept zone's
three packs plus the loader's fixed pack list (re-read from
`zone_bench_loader.cpp` each run — the sweep's mandatory set) is
`verify_eqe1 --require-v3`-verified and hard-linked when clean, or
rebuilt with `eqe1_builder` from the `.s3d` when STALE (the client FATAL's
own named remedy); a mandatory pack that is missing or does not come back
clean fails the job. Then the full
118-zone sweep (4 m 46 s measured, refactor/B929/README.md; the benchmark
is the per-ref tree's binary, passed with `--bench-bin`) and then
`scripts/ci/zone_budget_census.py --check-budgets`, which fails on ANY zone
over ANY budget: every estimate pair the `gate` job asserts
(`visual_gate.py`'s `MEMORY_BUDGET_PAIRS` minus the two also-baselined,
against the budgets the benchmark read out of the shipped orangepi preset)
and every `memoryLedgerBudgets` category. Blocking, no retries (the figures
are load-invariant, unlike the gate's CPU-p95). The ctest legs of the same
net: `test_zone_budget_census` (pure python, every pipeline),
`zone_budget_census_smoke` (the 3-zone smoke, label `slow`) and
`test_zone_census_pack_selfheal` (the self-heal's exit-code contract on stub
verify/builder binaries, pure bash+python).

## The shared pack and pack-changing branches (B1094, issue #414)

The shared pack `/mnt/projects/EverQuest_ETC1/willeq.eqe1` is one mutable file
read live by every worktree, every runner and the Pi lanes. Since B1094 the
tree draws a hard line between the two properties that used to live in one
test verdict:

- **Reproducibility from the inputs** -- `PackReproducible` (ctest, labels
  `hygiene;slow`) runs `scripts/build_willeq_pack.sh --check`, which rebuilds
  into a temp file and compares it to the pinned `data/willeq.eqe1.sha256`
  ONLY. The installed pack's sha is printed as `installed (INFO only):`,
  never as a verdict. The contract is pinned by `PackCheckContract`
  (`scripts/ci/test_pack_check_contract.py`, stub builders, temp files only):
  a `--check` that still reads the installed pack into its exit code is a red
  hygiene test. This is what made B1076's card break every other pipeline at
  the fable tip (MR !230 pipeline 1626): the old `--check` compared the
  rebuild to the pin AND to the installed pack, so one card installing its
  branch pack over the shared file was a red for N-1 branches.
- **The installed pack matches the pin** -- `InstalledPackMatchesPin` (ctest,
  label `hygiene`) runs `scripts/ci/installed_pack_matches_pin.sh`, which is
  `build_willeq_pack.sh --check-installed` (no build, no builders) with the
  verdict converted by `CI_COMMIT_BRANCH`: on **fable** a mismatch is a hard
  FAIL (a merge changed the pin and the orchestrator has not installed yet --
  the pipeline says so loudly); anywhere else it prints-and-passes, so a
  pack-changing branch is green on its own pipeline and its MR pipeline.

**What a worker does on a pack-changing card:** change the inputs
(`data/textures/*.bmp`, `tools/ui_atlas_builder`), rebuild with
`scripts/build_willeq_pack.sh --output <tmpfile>` -- NEVER a bare install,
and NEVER `--output` at the shared path -- commit the inputs plus the
rewritten `data/willeq.eqe1.sha256` in ONE commit, and write
`PACK INSTALL OWED AT MERGE: <full 40-char sha>` in the completion summary.
**The orchestrator installs the shared pack at merge** (the script without
`--check`/`--check-installed`: temp file in the destination directory, `mv`
into place, same as before B1094). Until it does, fable's
`InstalledPackMatchesPin` is the loud red that names exactly what is owed.

## The walked zone-line crossing (B1068, issue #388)

A live lane scenario, not a pipeline stage: it needs an eqemu lane and a
display. `scripts/scenario.sh zone_line_walk` seeds Testchar in qeynos 20
units short of the qeynos -> qeynos2 gate's BSP zone-line region
(`scripts/scenario_configs/zone_line_walk_seed.sql`), starts a private Xvfb,
runs the client `--egl-x11`, and its sidecar holds the forward key with
xdotool -- the renderer's own keyboard movement, the path a player walks.
`--scenario` runs headless (no renderer), so this lane runs the ordinary
windowed client and `scenario.sh` reads the verdict from its log with the same
exit codes: 0 once the client is in qeynos2, 3 if qeynos never finished
loading, 2 if it never zoned within `--timeout`. The post-check
(`zone_line_walk.post.sh`) requires the client's `Zone line triggered!` line
and the source zone log's `zoning to [qeynos2]` line for the character
(eqemu's `Client::Handle_OP_ZoneChange`).

```bash
EQEMU_LANE=1 bash scripts/scenario.sh zone_line_walk --timeout 180 --unit B1068
```

Like every `scenario.sh` run it brings the lane's server up and stops its
container for the restore -- never point it at a stack someone is playing on.
The offline half is `test_zone_line_crossing` (every pipeline): the 0x29
body-string decode on the zone files' own DRNTP strings, the swept segment
test, and BSP-over-curated precedence. `scripts/tools/wld_zonelines.py
--table <eq_dir> data/zone_lines` prints the per-zone BSP-vs-curated census
(review/B1068/README.md).

## The keyboard step walk (B1067, #387)

A live lane scenario, not a pipeline stage: it needs an eqemu lane, an X
display and an x86 GLES2 build (`build-gles2/bin/willeq`,
`scripts/build-gles2-x86.sh`). Keyboard movement is the only route through the
renderer's region-triangle collision (`PlayerCameraComponent::
resolveIrrlichtCollision`); `/move`, `--script` and `--scenario` resolve on the
game thread against the zone map, so none of them can see a step defect there.

```bash
EQEMU_LANE=1 bash scripts/eqemu/step_walk.sh --leg both    # a VM lane: both legs
EQEMU_HOST=local bash scripts/eqemu/step_walk.sh --leg rise   # the local lane: one leg per run
```

It seeds Testchar in qeynos2 beside the street curb at renderer Y 391.75 (a
1.0 rise, coordinates and the map evidence in `review/B1067/README.md`), runs
the client on Xvfb with `--egl-x11` and `--pose-log`, holds W with `xdotool`
after the loader's `Game ready!` line, and `scripts/eqemu/step_walk_verdict.py`
decides from the pose rows alone: PASS when the player's Y got at least 2 units
past the edge and its Z changed by at least 0.75 of the rise in the right
direction. Exit 0 every leg PASS, 1 a leg FAILED, 2 no verdict, 64 lock held,
65 lane problem. **On the local lane it never stops the world or bounces a
zone** (the XU4 play rig logs into that stack): the pose is put back with a
plain `UPDATE` after logout. On a VM lane the seed is written with the world
down (a zone hands back its LINKDEAD copy of a character's pose otherwise) and
the standard snapshot restore runs at the end. The unit net for the same
decision is `test_bsp_ground_step`.

### Mouse press ownership: `scripts/eqemu/press_ownership_check.sh` (B1066)

A mouse press belongs to the static UI or the camera, never both
(`include/client/graphics/press_ownership.h`, #386). The rig drives REAL X
button events with `xdotool` into an x86 GLES2 client on Xvfb (`--egl-x11`,
`--pose-log`) on the local lane as Testbuddy, never touching the stack or the
database: it opens the inventory, right-drags 20 px on an item (the item
display opens), left-drags on the window's background, right-drags on the 3D
view, and right-drags from the view into the window.
`scripts/eqemu/press_ownership_verdict.py` reads the heading and the camera
pitch around each gesture from the pose rows: the two UI gestures must leave
both unchanged, the view drags must turn the camera (including the part after
the pointer entered the window). Exit 0 PASS, 1 FAIL, 2 no verdict.

```bash
EQEMU_HOST=local bash scripts/eqemu/press_ownership_check.sh --bin build/bin/willeq --explore   # find the pixels
bash scripts/eqemu/press_ownership_check.sh --bin build/bin/willeq --dismiss 754,460 \
    --item 280,456 --window-bg 300,250 --view 650,80 --cross 520,300           # 800x600 window
```

Each run uses a fresh `WILLEQ_CACHE_DIR` (the per-character UI ini would
otherwise remember an open inventory and the rig's `i` would close it). The
unit net is `test_press_ownership`; the runs are in `review/B1066/`.

## `review/` citations

The `DocCitations` hygiene test resolves a `review/...` citation against
`/mnt/projects/fable/review`, the sanctioned outside-repo media directory. That
check is only authoritative from a checkout that sits *beside* the media (`main`
and every `wt/*` worktree). The runner checks out under `/home/user/builds/…`
and carries only a stale rsync of the archive, so from there those citations are
reported SKIP rather than judged against out-of-date media. In-repo citations
are checked on CI exactly as they are locally.

## Anchored citations

A bare `path:LINE` citation only proves the file exists and has that many
lines; pipeline 767 was hygiene-green on four README line numbers that were
two lines short (`/mnt/projects/fable/review/RETRO-WAVE7/README.md`, ids 5 and
37). So cite a line WITH its anchor: `` `<file>:LINE` (`token`) `` or a range,
`` `<file>:LINE-END` (`token`) `` (B1020). `DocCitations` FAILS when the
token (one pair of surrounding backticks or quotes stripped) does not occur on
that line or range at the checked-out tree. Rules of the form:

- The parenthesis follows the citation (or its closing backtick) after exactly
  ONE space; that shape is always read as an anchor, so prose belongs after a
  comma instead (`` `<file>:12`, the clear path ``), not in that parenthesis.
- The anchor is one token that is literally on the line: a symbol, a quoted
  string fragment, a key. Re-measure after your LAST commit
  (`git grep -n '<token>' -- <path>`), since your own hunks move lines.
- The anchored rule runs over the live docs AND over every `*.md` under
  `refactor/B<NNN>*/` (the unit write-ups the kanban workers produce); in the
  unit directories ONLY anchored citations are checked.
- `python3 scripts/refactor/check_doc_citations.py --self-test`
  (`DocCitationsSelfTest`) pins the accepted and refused shapes.

**The `gate` job checks the stamp too (B959).** Its `.visual_gate_prepare` used to
build `build-gles2/` incrementally without looking at libIrrlicht, so a driver
change that adds a symbol the client calls (B959's `eqtSetShaderBinaryCacheDir`)
failed the gate at link time on a warm tree (pipeline 397, job 1865). It now runs
the same `verify`, and rebuilds the lib with `--irrlicht-only` on a mismatch,
before the incremental build.
