# CI — the GitLab pipeline

**What it is for:** builds and the full ctest suite run on the project's own
GitLab runner, not inside a hermes kanban worker. The workers are capped at
4 GiB and were being OOM-killed while linking (board `B847`), which is not a
failure any amount of retrying fixes.

## What runs where

| | |
| --- | --- |
| GitLab | `https://gitlab.goldfarm.local`, project `willeq/willeq` (id 1) |
| Runner | id 1, shell executor (bash), `concurrent = 1`, tag `goldfarm`, runs untagged |
| Host | **hermes-ext** (172.16.200.100): 16 cores, 31 GB RAM, cmake + g++, **no ninja, no ccache** |
| Config | `.gitlab-ci.yml` at the repo root |

The runner's *description* in the GitLab UI reads "inspur-shell". That label is
stale — it was registered from hermes-ext on 2026-09-12 and has always executed
there (`Running on hermes-ext...` in every job log; GitLab's own runner record
reports `ipAddress` 172.16.200.100).

**The Inspur must never build.** Will's ruling (2026-09-17): the inference box's
CPUs belong to the GPUs. Every job's first line is therefore

    [ "$(hostname)" = "hermes-ext" ] || { echo "refusing to build on $(hostname)"; exit 1; }

so if the project is ever reassigned to another runner the pipeline fails loudly
instead of quietly moving builds onto the Inspur. hermes-ext also hosts the
hermes gateway and its kanban workers, so every heavy command is bounded to
`JOBS` (8 of the 16 cores) and runs under `nice -n 10`: a pipeline must not
starve a live worker.

## The ARM runner and Pi runs (T503)

A second runner, **`dev-u24-arm-nfs`** (GitLab runner id 2, tag `arm-nfs`, does NOT run untagged), runs in user
mode on the dev host `dev-u24`: binary `~/.local/bin/gitlab-runner`, config `~/.gitlab-runner/config.toml`
(GitLab's self-signed cert pinned beside it), `systemctl --user status gitlab-runner` (linger on), builds under
`~/gitlab-runner/builds`. It exists for one job, **`build-arm`**: `scripts/build-arm-noble.sh` (docker image
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
| `hygiene` | `ctest -L hygiene` in a no-C++ tree (`EQT_GRAPHICS=OFF`), configured into `build/hygiene` | 45 s | 44 s |
| `build` | the Release recipe from `CLAUDE.md`, `-j8` | 1,237 s (20.6 min) | 6 s |
| `test` | full `ctest -j6`, minus two cases, then one of them with retries | 116 s (4,908 tests) | 116 s |
| `gate` | `visual_gate` (T80/T291) over `build-gles2/bin/gles2_zone_benchmark` | 363 s | 18 s |
| | B813 (2026-09-20): the manifest names FIVE zones (qeynos2, gfaydark, befallen + iceclad + timorous); `gate` verifies all five (`--zones`, B903/B904 2026-09-29) | — | 40 s for all five in one job (measured back-to-back on a quiet hermes-ext: 10.6 s wall for three zones, 19.6 s wall for five; +9.0 s wall) |

(T502's own measurements on hermes-ext; `test` does the same work either way —
only the two build trees are warm or cold. A cold `gate` includes building the
patched Irrlicht *and* the whole `build-gles2` tree.)

"Warm" is not one number. A pipeline whose commit touched no source rebuilt
nothing (6 s), while the next one — a **docs-only** commit — still recompiled 5
test TUs and relinked 34 binaries, costing 403 s. With no ccache and no ninja on
this host, a warm build's cost is dominated by *linking*, so expect anywhere from
seconds to ~7 minutes. What makes a docs-only commit relink was not run to
ground. A fully warm pipeline — nothing to rebuild — is about **3 minutes** end
to end.

Every job's `before_script` re-creates the `data/eq_client` and
`data/eqe1_client` symlinks. They are gitignored (`tests/test_data_paths.cpp`),
so `git clean -ffdx` removes them before every job. B815 (issue #83): without
them the asset suites now behave by decision, not by accident —
`tests/asset_requirement.h` (`requireEqClientAssets`) makes an absent asset
root a GTEST_SKIP locally, and a hard FAIL when `WILLEQ_REQUIRE_ASSETS=1` is
set, which the `test` job exports (a green pipeline proves the assets were
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
object: `gpuBuffersBytes`, `ownedPlusGpuBuffersBytes` and a `gpuBuffers`
object with the row's source and inputs (`include/client/core/gpu_buffer_census.h`).
On the gate's llvmpipe the source is `logical_in_gpu_driver` -- the buffers are
heap already inside `gpu_driver`, so `ownedPlusGpuBuffersBytes` equals
`ownedBytes`. **Nothing asserts any of it**: the row exists for the Orange Pi,
where it is the only figure that sees Lima's GEM buffers.

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

## The warm tree, and its trade-off

The runner keeps one working directory per project, and the pipeline keeps three
paths in it across runs:

    GIT_CLEAN_FLAGS: -ffdx -e build -e build-gles2 -e third_party/irrlicht-gles2-x86

GitLab passes these straight to `git clean` (its own default is a bare `-ffdx`),
so `-e <pattern>` excludes those paths from the wipe. Everything else is still
cleaned on every job. This is what makes a pipeline incremental — a from-scratch
Release build on this host is ~30 minutes, and the patched x86 GLES2 Irrlicht is
built, not committed (`scripts/build-gles2-x86.sh`), so rebuilding it every time
would be pure waste. `build/hygiene` is a subdirectory of the preserved `build`
tree for the same reason: its CMake cache and its googletest `FetchContent` clone
survive too.

**The trade-off is real:** a stale object file, a stale CMake cache or a
half-written artifact can survive into the next pipeline, so a green pipeline is
evidence about an *incremental* build, not a clean one. To force a cold run:

- run the pipeline with `GIT_CLEAN_FLAGS` overridden to `-ffdx` (Run pipeline →
  add the variable), **or**
- on hermes-ext: `rm -rf ~/builds/*/0/willeq/willeq/{build,build-gles2,third_party/irrlicht-gles2-x86}`.

Do the cold run before trusting a green pipeline on anything that changed the
build system itself.

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
`check_build_tree_orphans` ctest test (in the `test` stage) was correctly
reporting that condition — it is the CI *feed* into that check that was
polluted, not the check.

The `build` job now runs
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
It still runs in the `test` stage exactly as before, over the SAME `build/`
tree the `build` job just pruned — a developer's local warm tree still gets
the original warning with no `--prune` involved, and if an orphan somehow
survives the CI prune (meaning the prune itself broke, not that the allowlist
needs an entry), the `test` stage still fails outright on it. The `ALLOWLIST`
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
  chosen; revisit only if `--prune` itself proves insufficient.

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
`slow`, in the `test` job) runs the smoke list on every push and skips loudly
(exit 125) when the benchmark binary or the gitignored EQ asset symlinks are
absent; CI's `test` job re-makes the symlinks and sets `WILLEQ_REQUIRE_ASSETS=1`,
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

**The `gate` job checks the stamp too (B959).** Its `.visual_gate_prepare` used to
build `build-gles2/` incrementally without looking at libIrrlicht, so a driver
change that adds a symbol the client calls (B959's `eqtSetShaderBinaryCacheDir`)
failed the gate at link time on a warm tree (pipeline 397, job 1865). It now runs
the same `verify`, and rebuilds the lib with `--irrlicht-only` on a mismatch,
before the incremental build.
