# T502 — a GitLab CI pipeline on the project's own runner

Branch `task/T502-gitlab-ci`, cut from `fable` at `90352a41`, worked in
`/mnt/projects/fable/wt/t6`. OFFLINE unit: no eqemu lane, no Orange Pi, no
client launch. The only remote systems touched are the project's GitLab and the
runner host that GitLab drives.

## Why

Builds and the ~4,900-case ctest suite were being run wherever an agent happened
to sit, and when that was a hermes kanban worker the worker's 4 GiB cap
OOM-killed it while linking. So on the path a worker takes, the suite was not
run at all, and a red tip could reach `fable` with nobody having compiled it
(board `B847`). The project already had a registered runner that had never run a
job for it, and `ciConfigPathOrDefault` = `.gitlab-ci.yml` with no such file.

## Where it runs, and where it must not

Will's ruling (2026-09-17): the Inspur inference box must never build — its CPUs
belong to the GPUs doing inference. The project's one runner is *described* in
GitLab as "inspur-shell", which is exactly the kind of label that gets believed,
so the host was established before any build was allowed near it:

1. **A one-job pipeline first** (pipeline **#1**, job 118, 6 s): its whole
   script was `hostname; cat /etc/hostname; nproc; free -g` plus a tool/asset
   probe. It answered **hermes-ext**, 16 cores, 31 GB, and printed
   `Running on hermes-ext...` in the runner's own preparation section.
2. GitLab's runner manager record for `Ci::Runner/1` reports `ipAddress`
   172.16.200.100, and that host's `~/.config/gitlab-runner/config.toml` carries
   `id = 1` under the name `hermes-ext`. The "inspur-shell" description is a
   stale label.

Belt and braces, every job's first line is a hostname guard that refuses
anything but hermes-ext, so a future runner reassignment fails loudly rather
than quietly moving builds onto the Inspur. hermes-ext also hosts the hermes
gateway and its kanban workers, so `JOBS` is 8 of its 16 cores and every heavy
command runs `nice -n 10`.

## The pipeline

`.gitlab-ci.yml`, four stages, `timeout: 3h` and `interruptible: true` on each,
`dependencies: []` everywhere (the jobs share one warm tree on a `concurrent=1`
runner; nothing is passed as an artifact):

| Stage | Command shape |
| --- | --- |
| `hygiene` | configure `build/hygiene` with `EQT_GRAPHICS=OFF -DEQT_BUILD_TESTS=ON`, `ctest -L hygiene -j6` |
| `build` | bootstrap the patched x86 GLES2 Irrlicht if absent, then the `CLAUDE.md` Release recipe, `cmake --build -j8` |
| `test` | `ctest -j6 -E '(^visual_gate$\|^RDPServerFrameTest\.UpdateFrameMultipleTimes$)'`, then that RDP case alone with `--repeat until-pass:3` |
| `gate` | wire the asset symlinks + Pillow, build `build-gles2`, run `visual_gate.sh` up to 3 times |

Scope is set pipeline-wide in `workflow.rules`: `task/*`, `plans/*`, `fable`,
merge requests; never tags; and a branch with an open MR builds once, not twice.
Artifacts (7 days): `hygiene-ctest.log`, `ctest.log` +
`build/Testing/Temporary/LastTest.log`, `gate.log` + `build-gles2/ci_artifacts/`.

`docs/ci.md` is the operator's half of this: how to read a pipeline from a
shell, how to retry the gate, how to force a cold build.

## The one code change: `review/` citations on a foreign host

The first real pipeline failed `hygiene` with 19 `DocCitations` FAILs, every one
a `review/...` path. `review/` is the sanctioned outside-repo media directory at
`/mnt/projects/fable/review`, and the checker resolves citations against it.

The first fix — SKIP when that directory does not exist — was wrong, and the
next pipeline proved it by failing identically: hermes-ext **does** have the
directory, as a stale rsync (275 entries, last synced 2026-09-12) missing
exactly the recent units T459–T496 the failing citations name. Resolving against
a stale copy reports a host's sync lag as stale citations.

The rule is now structural: `review/` resolves only when the checkout being
scanned is itself under the workspace root that holds the media, which every
canonical checkout and `wt/*` worktree is and the runner's
`/home/user/builds/…` checkout is not. From anywhere else those citations SKIP —
a category the checker already had, and which never affects the exit code. In-repo
citations are checked on CI exactly as they are locally.

## Pipeline log

| iid | What changed | Outcome |
| --- | --- | --- |
| 1 | one-job host probe | SUCCESS — `hermes-ext`, 16 cores, 31 GB |
| 2 | the four-stage pipeline | FAILED in `hygiene`: `DocCitations`, 19 `review/` FAILs |
| 3 | SKIP `review/` when the directory is absent | FAILED identically — the directory is present but stale |
| 4 | (superseded; auto-cancelled by `interruptible: true`) | CANCELED |
| 5 | `review/` judged only from a checkout beside the media | `hygiene` 45 s GREEN, cold `build` 1,237 s GREEN, then FAILED in `test`: 3 of 4,908 |
| 6 | asset symlinks wired in `before_script` for every job | **SUCCESS — green end to end** (first green) |
| 7 | docs + board + plan (no CI or code change) | **SUCCESS — green end to end**, and the warm-tree measurement |

## Review

Claims, each marked [measured] or [assumed].

- [measured] The runner executes on **hermes-ext**: `Running on hermes-ext...`
  and `hostname` = `hermes-ext` in job 118's log; `nproc` = 16; `free -g` total
  = 31 GB (8–9 GB free, ~19 GB in buff/cache). Corroborated by GitLab's
  `ipAddress` 172.16.200.100 for `Ci::Runner/1` and by `id = 1` in that host's
  runner config.
- [measured] The runner has cmake and g++ and has **no ninja and no ccache**
  (`which` printed only `/usr/bin/cmake` and `/usr/bin/g++`), so the build uses
  Makefiles and there is no compiler cache — the warm tree is the only
  incrementality there is.
- [measured] Both asset trees and the pristine Irrlicht source are on the runner
  host (`/mnt/projects/EverQuestP1999`, `/mnt/projects/EverQuest_ETC1`,
  `/mnt/projects/irrlicht-1.8.5`), and GitHub is reachable from it
  (`git ls-remote` for googletest `release-1.12.1` resolved), which the
  `FetchContent` of googletest and etc2comp needs.
- [measured] hermes-ext's `python3` is 3.12.3 with **no PIL** and no working
  `ensurepip` (`python3 -m venv` + `pip` fails: "You may need to use sudo…
  python3-venv"). Pillow 12.3.0 installs for it via the pip in the host's
  existing `~/.venvs/ci` (3.11) with `pip --python /usr/bin/python3 install
  --target`; verified by importing PIL under 3.12 with `PYTHONPATH` set. The
  gate job does this idempotently.
- [measured] `data/eq_client` / `data/eqe1_client` are gitignored and absent
  from a fresh checkout, so the gate job creates them with `ln -sfn`.
- [measured] The `review/` failure was a stale rsync, not stale citations:
  `/mnt/projects/fable/review` on hermes-ext has 275 entries dated 2026-09-12,
  and the 19 failing citations name T459–T496.
- [measured] The citation-checker change is behaviour-preserving where the media
  is authoritative: from this worktree, 1452 ok / 0 fail / 61 skip before and
  after. From a copy of the tree outside `/mnt/projects/fable`, zero `review/`
  FAILs remain.
- [measured] The config is accepted by this GitLab: `POST /ci/lint` returned
  `"valid": true`, no errors, no warnings.
- [measured] `interruptible: true` works as intended — pipeline #4 was
  auto-cancelled when #5 superseded it.
- [measured] `CiJob` has no `webUrl` field on GitLab 19.3 (the GraphQL query in
  the unit brief was rejected with `undefinedField`); job logs come from
  `/api/v4/projects/1/jobs/<id>/trace`.
- [measured] Per-stage wall time on hermes-ext, cold / warm:
  `hygiene` 45 s / 44 s (the tree is always warm — it is `build/hygiene`);
  `build` **1,237 s (20.6 min) / 6 s**; `test` 116 s either way (it is the same
  work), covering **4,908** registered tests plus the RDP case's own re-run;
  `gate` 363 s cold (that includes building the patched Irrlicht and the whole
  `build-gles2` tree from nothing) / **18 s** warm.
- [measured] The green run is real, not a skip: job 138 reports
  `100% tests passed, 0 tests failed out of 4908` and the RDP re-run
  `100% tests passed ... out of 1`; job 139 logs
  `=== visual_gate attempt 1/3 ===` followed by per-zone `[cpu]`, `[budget]` and
  `[ledger]` assertions for qeynos2/gfaydark/befallen and `=== VISUAL GATE: PASS ===`.
  Its qeynos2 CPU p95 was 2.495 ms against a 10.108 ms ceiling — the load
  headroom on this host is large.
- [measured] Three suite tests are asset-dependent and **fail rather than skip**
  without `data/eq_client`: `ComprehensiveZoneTest.ClassicZones`,
  `ComprehensiveZoneTest.OverallSummary`,
  `DoorLoadingUnitTest.MultipleZonesHaveObjectGeometries` (pipeline 5 job 134:
  every zone "SKIP (not found)", then `Expected: (loadedCount) > (0u)`). The
  symlinks are therefore made in every job's `before_script`, not just the
  gate's.
- [measured] "warm" is not one number: pipeline 6's build rebuilt nothing (6 s),
  pipeline 7's — a docs-only commit — recompiled 5 test TUs and relinked 34
  binaries for 403 s. With no ccache and no ninja, warm cost is dominated by
  linking. [assumed] why a docs-only commit invalidated those 5 TUs: not
  established. It is a cost, not a correctness problem, and it is bounded by the
  cold number.
- [measured] cmake 3.28.3 and g++ 13.3.0 on the runner; `ctest --repeat
  until-pass` is supported (3.17+).
- [measured] CLAUDE.md is 56,416 bytes after this unit's paragraph, under the
  60,000 ceiling `FileSizeCeiling` enforces.
- [assumed] That `GIT_CLEAN_FLAGS: -ffdx -e …` keeps those paths across
  pipelines is taken from GitLab's documented behaviour (the flags are passed to
  `git clean`) plus the warm-run timings below; no pipeline in this unit
  deliberately verified that a *modified* file inside `build/` survives a job
  boundary.
- [assumed] That 8 of 16 cores and `nice -n 10` are enough to keep a kanban
  worker responsive during a pipeline. It is a bound, not a measurement — no
  worker was run against a pipeline to time it.
- [assumed] The ~30 min cold Release build figure quoted in the brief; this
  unit measured its own cold build instead (see the times above), which is the
  number to trust.
- [assumed] That the 3-attempt gate retry is enough for the CPU-p95 assertion on
  a shared host. T291's history says the metric is now process CPU time and far
  less load-sensitive, but the first pipeline to run the gate under real load is
  the only sample here.

## Not done / deliberately out of scope

- No merge. The branch is pushed to `gitlab` and left for the orchestrator.
- No MR-only jobs, no scheduled nightly pipeline, no cache/artifact server
  (`concurrent = 1` and one host make both pointless for now).
- The eqemu lane and the Orange Pi deploy are not in CI and should not be:
  neither is reachable from the runner host on demand.
