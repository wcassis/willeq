# CI — the GitLab pipeline

**What it is for:** builds and the full ctest suite run on the project's own
GitLab runner, not inside a hermes kanban worker. The workers are capped at
4 GiB and were being OOM-killed while linking (board `B847`), which is not a
failure any amount of retrying fixes.
Compiling is the same story: `scripts/build-gles2-x86.sh` defaults to 24 jobs
and a worker that ran it was OOM-killed at 04:40Z on 2026-10-13 (`cc1plus`,
`CONSTRAINT_MEMCG`); since B1046 the script reads its cgroup's `memory.max`
and clamps `JOBS` to 1 GiB per compiler (4 under the worker cap, 24 elsewhere).
A card body still says `-j3` for any direct `cmake --build`.

## What runs where

Official-client oracle sessions are separate from CI. The one-command lane
driver, plan vocabulary, evidence layout and exit codes are in
[`oracle_driver.md`](oracle_driver.md).

| | |
| --- | --- |
| GitLab | `https://gitlab.goldfarm.local`, project `willeq/willeq` (id 1) |
| x86 runners | **gitlab-runner-1** and **gitlab-runner-2** (10.0.30.81/.82, B1008): shell executor (bash), `concurrent = 1` each, **both tagged `x86-build`** since 2026-10-11 — one pipeline's stages may run on different hosts (B1026, "Two x86 runners" below) |
| ARM runner | `dev-u24-arm-nfs` on dev-u24, tag `arm-nfs` (T503, below) |
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

    case "$(hostname)" in hermes-ext|gitlab-runner-1|gitlab-runner-2) ;; *) echo "refusing to build on $(hostname)"; exit 1 ;; esac

so if the project is ever reassigned to another runner the pipeline fails loudly
instead of quietly moving builds onto the Inspur. hermes-ext also hosts the
hermes gateway and its kanban workers, so every heavy command is bounded to
`JOBS` (8 of the 16 cores) and runs under `nice -n 10`: a pipeline must not
starve a live worker.

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
(`glab ci trigger build-arm`), and refuses any host but `dev-u24`.

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

The Orange Pi is **lane 1** of a three-lane table in `scripts/pi/env.sh`; `PI_LANE=N` picks the device the way
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
  run on a fresh Pi must still work. The sweep's summarizer reads the line
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

## The stages

| Stage | What it does | Cold | Warm |
| --- | --- | --- | --- |
| `hygiene` | `ctest -L hygiene` in a no-C++ tree (`EQT_GRAPHICS=OFF`), configured into `build/hygiene`; on `fable` also `prune-build-trees` (B1012, the per-ref tree expiry + census) | 45 s | 44 s |
| `build` (`build-test`, B1042) | the Release recipe from `CLAUDE.md`, `-j8`, into `build/ref/<slug>` (B1012), then in the SAME job full `ctest -j6`, minus two cases, then one of them with retries | build 1,237 s (20.6 min) + suite 116 s (4,908 tests) | build 6 s + suite 116 s |
| `build` (`build-headless`, B1038) | the same Release build with `EQT_GRAPHICS=OFF` into `build-headless/ref/<slug>`, then `ctest -LE hygiene` in the same job ("The headless build" below) | not yet measured on a runner; dev-u24 `-j4`: ~1,920 s cold (B1038) | not measured |
| `gate` | `visual_gate` (T80/T291) over `build-gles2/ref/<slug>/bin/gles2_zone_benchmark` (B1012) | 363 s | 18 s |
| | B813 (2026-09-20): the manifest names FIVE zones (qeynos2, gfaydark, befallen + iceclad + timorous); `gate` verifies all five (`--zones`, B903/B904 2026-09-29) | — | 40 s for all five in one job (measured back-to-back on a quiet hermes-ext: 10.6 s wall for three zones, 19.6 s wall for five; +9.0 s wall) |

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
so the choice stays visible. `scripts/ci/owned_memory.py` is
the one Python definition of these figures, behind the `[rss-target]` line and
the Pi summarizers.

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

### Docs-only pipelines run hygiene only (B1042, #362)

`build-test`, `gate` and `client-ledger-live` (and `build-headless`, B1038)
carry `rules:` (the `.code_rules` anchor in `.gitlab-ci.yml`) that add the job
only when **at least one changed path does not end in `.md`**. `hygiene` has no
rules and always runs, so a pipeline whose diff is only `*.md` files — anywhere,
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
over sample paths, and that `hygiene` carries no rules.

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

The pixel gate has ONE compare rule — `compare_images_exact`
(`scripts/ci/visual_gate.py`) — and TWO golden sets, because the two
drivers can never render byte-identical frames:

- **x86** (the CI `gate` job): llvmpipe, 16-bit depth (B971), goldens at
  `tests/goldens/<zone>_f<n>.png`, section `manifest["zones"]`. The CI
  job passes no `--platform`; nothing about it changed.
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
goldens are 16-bit depth while the Pi's EGL answers 24 — a
cross-platform compare can never be exact. The Pi IS deterministic
(B971: 0 px across two binaries in 15 of 15 frames), so one exact-match
rule works per set.

**A Pi golden moves only on a Pi re-bless by Will; an x86 re-bless never
touches the Pi set, and vice versa.** The Pi verify fails `NOT YET
BLESSED` (exit 1) until the section exists — it never falls back to the
x86 set. The live-client sweep (`scripts/pi_rendered_zone_sweep.sh`) is
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
