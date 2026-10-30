# B1208 — `rbuild --via ci`: worker builds through a GitLab CI `rbuild` job (#527)

The two x86 runner VMs answer no inbound port from hermes-ext or the ThinkPad
(`refactor/B1194/README.md`, "Why the inventory and the proof run as CI jobs"),
so `rbuild --host runner-N` cannot reach them. Every runner polls GitLab over
HTTPS, so GitLab itself is the middle point: `rbuild --via ci` pushes the
worktree and lets the runner fleet's own queue place the build. The ssh path
(the ThinkPad, `--host`) is unchanged and stays the default. All measurements
2026-10-30, branch head of `task/B1208-rbuild-via-ci` merged into a throwaway
branch on hermes-ext.

## What was built

- **`scripts/hermes/rbuild --via ci`** (or `RBUILD_VIA=ci`; header "--via ci",
  `ci_main`, `ci_status`, `ci_token_init` in the same file): for
  `<wt> --target <t...>`, `<wt> test -R <re> [--target <t...>]` and
  `<wt> hygiene` it WIP-commits the worktree (`git add -A`, `--allow-empty`,
  title `rbuild: ci build <what>`) on its branch, refuses a detached HEAD, the
  branch `fable`, any extra build/ctest argument, and a tree whose
  `.gitlab-ci.yml` has no `rbuild` job; pushes with
  `-o ci.variable=RBUILD_TARGET=<t...>` (+ `RBUILD_TEST=<re>`; `auto` for a
  `test` without `--target`, `hygiene` for hygiene); finds the push pipeline
  by sha; streams the job trace (whole-trace poll, printed past a byte
  offset, every `RBUILD_CI_POLL` s); downloads the artifacts to
  `<wt>/.rbuild-ci/<pipeline id>/` (gitignored); exits 0 for `success`, 1 for
  any other job status, 2 for a refusal. An interrupted caller cancels the job
  (EXIT trap). Token: `~/gitlab_token.txt`, else glab's token for the API host
  (hermes-ext has no token file; the glab fallback is what the runs below
  used), written to a 0600 header file for `curl -H @file`, never printed.
  `--status` lists pending/running rbuild jobs and the project runners.
- **`.gitlab-ci.yml`**: the `rbuild` job (stage `build`, `extends: .goldfarm`
  = tag `x86-build` + `interruptible: true` + the ccache/asset before_script),
  one rule: `$RBUILD_TARGET && $CI_PIPELINE_SOURCE == "push" &&
  $CI_COMMIT_BRANCH && $CI_COMMIT_BRANCH != "fable"`. It runs the SHARED
  `willeq_build_tree` (`.build_tree`, now taking `WILLEQ_BUILD_TARGETS`; a
  partial build never writes the B1026 host+sha claim), `ctest -R
  "$RBUILD_TEST" --no-tests=error` only when RBUILD_TEST is set, or the hygiene
  configure + `ctest -L hygiene` for `RBUILD_TARGET=hygiene`. Artifacts:
  `rbuild-ctest.log`, `rbuild-junit.xml` (also the junit report),
  `rbuild-LastTest.log`, `expire_in: 2 days`.
  **Exclusion**: `.not_rbuild` (`if: $RBUILD_TARGET` -> `when: never`) heads
  the rules of every other job — `.code_rules` (build-test, build-headless,
  gate, zone-budget-census), `hygiene` (rules = the reference + `when:
  on_success`), `prune-build-trees`, `build-arm`, `build-arm64`,
  `client-ledger-live` and the B1194 include's `.b1194_rules`. The workflow
  admits the RBUILD push ABOVE the `$CI_OPEN_MERGE_REQUESTS` suppression and
  drops the MR pipeline of a commit titled `rbuild: ci build ...`.
- **`scripts/ci/rbuild_test_targets.py`**: `RBUILD_TARGET=auto`. gtest
  discovery is PRE_TEST (`tests/CMakeLists.txt`), so an unbuilt executable
  lists only `<target>_NOT_BUILT` and `ctest -N -R` cannot name its target;
  the helper drops a CMake file-API codemodel query before the configure and
  picks the executables whose sources declare a matching `Suite.Name`.
- **Tests**: `CiQueueRules` group 5 (every job but `rbuild` starts with the
  exclusion, hygiene's exact rules, the rbuild rule, the workflow order; four
  new self-test breaks) and the new hygiene test `RbuildTestTargets`
  (`scripts/ci/test_rbuild_test_targets.py`). `CiBuildTreeStamp`'s self-test
  regex now tolerates the conditional claim line.
- **Docs**: `docs/ci.md` "The rbuild job: worker builds through CI (B1208,
  #527)"; the `scripts/hermes/rbuild` header; `docs/module_map.md`'s hygiene
  count.

## Measured runs (hermes-ext, as a worker)

Throwaway branch `task/B1208-probe` (this branch merged in) in
`~/src/willeq-wt/b1208-probe`, the script copied to `~/.local/bin/rbuild-ci`
(the installed `~/.local/bin/rbuild` untouched). Caller logs (`EXIT=` and
`WALL=` lines appended by the wrapping shell) and the job traces/API rows are
under `/mnt/projects/fable/review/B1208/hermes-ext/`; the job columns come
from `jobs_api.tsv` there (`GET /projects/1/jobs/:id`).

| command | caller exit | caller wall | pipeline / job | runner | queued | job duration | jobs in the pipeline |
|---|---|---|---|---|---|---|---|
| `--via ci <wt> --target model_viewer` | 0 | 49 s | 2619 / 13432 | thinkpad | 1.9 s | 35.8 s | `rbuild` only |
| `--via ci <wt> test -R SpellSlot` | 0 | 18 s | 2621 / 13433 | thinkpad | 0.3 s | 10.8 s | `rbuild` only |
| `--via ci <wt> hygiene` | 1 | 78 s | 2622 / 13434 | thinkpad | 1.8 s | 70.2 s | `rbuild` only |
| `--via ci <wt> test -R NoSuchSuiteB1208` | 1 | 27 s | 2623 / 13435 | gitlab-runner-1 | 1.7 s | 16.7 s | `rbuild` only |
| `--via ci <wt> test -R '^PacketTest\.'` (branch with an OPEN MR) | 0 | 22 s | 2624 / 13436 | gitlab-runner-1 | 1.3 s | 13.9 s | `rbuild` only |

Sources per row: `b1208_mv.log`; the SpellSlot caller log was overwritten by
the NoSuchSuite run (same file name — `b1208_test_nosuch.log` is the latter),
so its 18 s / exit 0 are the two summary lines quoted in the session and its
job facts are `job13433_trace.log` + `jobs_api.tsv`; `b1208_hygi.log`;
`b1208_test_nosuch.log`; `b1208_mrpkt.log`. Artifacts as downloaded:
`rbuild-ci-artifacts/<pipeline>/`.

- **model_viewer**: built in a NEW per-ref tree (`build/ref/task-b1208-probe`)
  on the ThinkPad runner: configure 11.6 s + generate 3.9 s
  (`job13432_trace.log`, "Configuring done"/"Generating done"), ccache 120/120
  hits ("Hits:" line of the same trace). A build-only job writes no ctest
  file; the `rbuild-LastTest.log` it uploaded was a warm tree's leftover,
  which is why the after_script now copies it only after the job's own
  ctest (second commit on this branch).
- **SpellSlot**: `auto` resolved `test_combat test_input_action_bridge
  test_skill_reachability` and ran 3 tests, all passed
  (`job13433_trace.log`, the `rbuild_test_targets:` and "tests passed" lines).
- **hygiene**: exit 1 is a real red, not a tool fault: `DocCitations` failed
  because `docs/ci.md` already cited this README, which the probe branch did
  not have yet (`rbuild-ci-artifacts/2622/rbuild-ctest.log`). It shows the
  failure path end to end: job `failed` -> exit 1, artifacts still fetched.
- **NoSuchSuite**: the resolver's exit 3 failed the job before any build
  (`job13435_trace.log`, "no executable target declares a test matching");
  no artifacts (the job wrote none), exit 1.
- **open MR**: MR !358 (draft, opened on the probe branch for this check,
  then closed) — `GET /projects/1/pipelines?sha=1a756856f…` returned only
  push pipeline 2624, whose jobs are `[('rbuild', 'success')]`, and
  `refs/merge-requests/358/head` has NO pipeline: neither opening the MR (its
  head was an `rbuild: ci build` commit) nor the rbuild push started the MR
  suite. `auto` resolved `test_packet`, 24/24 passed (`b1208_mrpkt.log`).

**Refusals** (`refusals_and_status.txt`, same directory): on a worktree of
branch `fable` -> exit 2, `refusing to build from 'fable'`, HEAD unchanged
(1dc424875 before and after); on a detached HEAD -> exit 2; an extra `-j4`
-> exit 2. All three refuse before any `git add`/commit/push. The same file
holds one `--via ci --status` output.

Nothing compiled on hermes-ext or on this dev VM: every compile ran inside a
CI job on thinkpad / gitlab-runner-1 (the `runner` column) or, for the ssh
comparison below, in rbuild's scope on the ThinkPad.

## Overhead against the ssh path

The same worktree, `~/.local/bin/rbuild <wt> --target model_viewer` (ssh to
the ThinkPad, its own fresh builds-root tree `/home/user/willeq-builds/b1208-probe`):
exit 0 in **8 s** wall, rbuild's own line `build exit 0 in 7 s on thinkpad`
(configure + 118 steps, `b1208_ssh_mv.log`); an immediate no-op rerun 1 s
(`b1208_ssh_mv2.log`). B1194 measured no model_viewer number (its proof
target was `test_packet`), so this is the comparison.

| | ssh (ThinkPad) | `--via ci` |
|---|---|---|
| model_viewer, fresh tree, ccache warm | 8 s | 49 s (job 35.8 s) |

So **~40 s more per call** on a fresh tree: the CI job's configure is the
bulk (11.6 + 3.9 s on a new per-ref tree, every rbuild job re-runs it), plus
`get_sources` (2 s, the trace's `section_start/end:get_sources`), the queue
(0.3–1.9 s here, idle runners), the after_script/artifact upload (1 s), and
the caller's pipeline lookup + 5 s trace-poll granularity (wall minus queued
minus job: 49 − 1.9 − 35.8 ≈ 11 s). On a warm per-ref tree the whole call is
18–27 s (rows 2, 4, 5). The issue's expectation was 1–2 min; measured, it is
under one. The real cost is the queue: with every x86 runner busy on
pipelines (`concurrent = 1`, below) an rbuild job waits behind them, which
the ssh path does not.

## Runner census (project runners API, 2026-10-30)

`GET /projects/1/runners` then `GET /runners/:id` (`--via ci --status`,
`refusals_and_status.txt`, 01:25Z):

| id | runner | state | tags | untagged | concurrent | recommendation |
|---|---|---|---|---|---|---|
| 1 | hermes-ext-shell | PAUSED | goldfarm | yes | — | keep PAUSED: hermes-ext never builds (B1087; it hosts the gateway and the kanban workers) |
| 2 | dev-u24-arm-nfs | PAUSED | arm-nfs | no | — | keep PAUSED: Will, "no subagent builds on dev-u24" |
| 5 | gitlab-runner-1 | active, online | x86-build, x86-gpu, x86-build-fast, goldfarm, arm-nfs | yes | 1 (issue #527's census; the VM answers no ssh to re-read config.toml) | rbuild yes — non-GL and GL targets alike (llvmpipe; build-test builds model_viewer there already) |
| 6 | gitlab-runner-2 | active, online | same as runner-1 | yes | 1 (same source) | rbuild yes |
| 7 | thinkpad | active, online | same five tags | yes | 1 (`sudo -n grep concurrent /etc/gitlab-runner/config.toml` over ssh: `concurrent = 1`, `CI_JOBS=8`) | rbuild yes, but it is the saturated host (B1194); the ssh path stays the way to put a build HERE on purpose |

Which tag lands where: `x86-build` (every x86 job, `rbuild` included) -> runners
5, 6, 7; `arm-nfs` (build-arm, build-arm64) -> 5, 6, 7 (runner 2 is paused);
`x86-gpu`, `x86-build-fast`, `goldfarm` -> 5, 6, 7 (no job in this file asks
for them today); untagged -> 5, 6, 7 (runner 1 also takes untagged but is
paused). In the runs above `rbuild` landed on thinkpad three times and on
gitlab-runner-1 twice (`jobs_api.tsv`), i.e. GitLab places it on whichever
active runner is free — the load spreading B1194 wanted, without ssh.

**For the B1204 allocator** (`docs/hermes/allocator.md`, "Known limits":
runner-VM build slots have no live probe): `cpu-build` capacity for `--via ci`
cards = (active, online runners carrying `x86-build`) × each one's
`concurrent` — today 3 × 1 = 3 (runners 5, 6, 7). The live usage probe is
`GET /projects/1/jobs?scope[]=pending&scope[]=running` (URL-encoded
`scope%5B%5D=`): running x86 jobs occupy slots, and any `pending` job means
the slots are full. `rbuild --via ci --status` prints both (rbuild jobs,
the all-jobs pending/running count, and the runner table). `concurrent` is
in each runner's `config.toml`, not in the API, so capacity.yaml must carry
it as a declared number. Note the ThinkPad's slot is shared with the
ssh-path builds and the lane-5 oracle, so the allocator should not count it
as a free cpu-build slot whenever its ThinkPad census already closes renders.

## Acceptance

- (A) `--via ci` target / test / hygiene from hermes-ext, trace streamed,
  artifacts fetched, exit = job status: the table above (0, 0, 1, 1, 0).
- (B) the `rbuild` job only on RBUILD_TARGET pipelines, and those pipelines
  hold nothing else: every pipeline in the table has `rbuild` alone; branch
  pipelines without RBUILD_TARGET do not get it (its one rule); the MR check
  above. Pinned by `CiQueueRules`.
- (C) this README: the measured runs, the refusals, the census.
- (D) hygiene + the MR pipeline: see the MR.
- (E) first push `-o ci.skip`, MR "Closes #527".
- (F) no compile on hermes-ext or dev-u24 (the `runner` column; the ssh run
  compiled on the ThinkPad).

## Cleanup done

MR !358 closed; `task/B1208-probe` deleted on GitLab and in the hermes-ext
clone; worktrees `b1208-probe` and `b1208-fable` removed;
`~/.local/bin/rbuild-ci` removed. Left: the ThinkPad builds-root tree
`/home/user/willeq-builds/b1208-probe` (prunable) and the runners' per-ref
trees `build/ref/task-b1208-probe` (prune-build-trees expires them: the ref
is gone from the remote).

## CLAUDE.md EDITS

Suggested for the orchestrator: in "Process requirements", the CI bullet could
name `rbuild --via ci` as the worker build path that reaches the runner VMs.

## FOLLOW-UP

- The WIP commits stay on the task branch (the issue's design); a worker that
  wants a clean history squashes before the MR. A variant that pushes a
  side ref (`rbuild/<branch>/<sha>`) instead would leave the branch untouched
  and needs a workflow rule for that ref prefix.
- Wire `--via ci` into the worker cards' BUILD RULE and the B1204
  `capacity.yaml` cpu-build entry (3 × 1) once this merges.
