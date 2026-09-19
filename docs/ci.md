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

## The stages

| Stage | What it does | Cold | Warm |
| --- | --- | --- | --- |
| `hygiene` | `ctest -L hygiene` in a no-C++ tree (`EQT_GRAPHICS=OFF`), configured into `build/hygiene` | 45 s | 44 s |
| `build` | the Release recipe from `CLAUDE.md`, `-j8` | 1,237 s (20.6 min) | 6 s |
| `test` | full `ctest -j6`, minus two cases, then one of them with retries | 116 s (4,908 tests) | 116 s |
| `gate` | `visual_gate` (T80/T291) over `build-gles2/bin/gles2_zone_benchmark` | 363 s | 18 s |

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
`ctest.log` + `build/Testing/Temporary/LastTest.log`, and `gate.log` +
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

## `review/` citations

The `DocCitations` hygiene test resolves a `review/...` citation against
`/mnt/projects/fable/review`, the sanctioned outside-repo media directory. That
check is only authoritative from a checkout that sits *beside* the media (`main`
and every `wt/*` worktree). The runner checks out under `/home/user/builds/…`
and carries only a stale rsync of the archive, so from there those citations are
reported SKIP rather than judged against out-of-date media. In-repo citations
are checked on CI exactly as they are locally.
