# B1026 — CI host+sha build-tree stamp (issue #346)

**Class B (CI hygiene needing a code change). Will ruled fix option (a).**

## What was wrong

Will retagged gitlab-runner-2 from `x86-build-2` to `x86-build` on 2026-10-11
so two x86 pipelines run in parallel. `.gitlab-ci.yml`'s `.goldfarm` comment
still stated the premise that no longer held: the stages pass NO artifacts and
share one warm per-ref tree on ONE runner (B1012). With two tagged runners a
pipeline's `build` and `test` jobs can land on different hosts, and `test` then
runs whatever binary that host's `build/ref/<slug>` tree holds.

Measured (`review/AUDIT-WAVE8/sections/b603.md` finding 1,
`review/AUDIT-WAVE8/evidence/b603/pipeline_runners.txt`): fable pipeline 809 at
`0136137b` built on gitlab-runner-1 (job 4006) and tested on gitlab-runner-2
(job 4007). The `SpellLandHookIsGone` failure cites
`test_spell_manager_timing.cpp:793-796` `EXPECT_NE`, the pre-B603 source
(`0136137b` has `EXPECT_EQ` at 735-738): a false red. MR pipeline 805 was green
only because pipeline 804 had built the same sha on runner-2, so the same split
can also give a false green.

## What changed

- `scripts/ci/prune_ref_build_trees.py`: the existing `.willeq_ref` stamp gains
  an optional BUILD CLAIM. `--stamp <tree> --ref <r> --sha <c> [--host <h>]`
  adds `host=` and `sha=` lines (host defaults to this machine). The prune's
  parser ignores the extra lines, so there is one stamp format. A ref-only
  `--stamp` claims no build. New `--verify <tree> --host <h> --sha <c>`: exit 0
  only when the claim names that host and that sha. It exits 1 on a missing
  stamp, a ref-only stamp, another host or another sha, and prints the stored
  and expected host/sha either way.
- `.gitlab-ci.yml`:
  - The build steps exist once, as the shell function `willeq_build_tree`
    defined by the `.build_tree` anchor. It runs, in order: Irrlicht bootstrap
    if missing or stale, a ref-only stamp (which wipes any earlier claim),
    configure, B907 orphan prune, `cmake --build`, and finally the host+sha
    claim. It runs under `set -eo pipefail`, so a failed build never stamps.
    `build` = `!reference [.build_tree, script]` + `willeq_build_tree`.
  - `.verify_build_tree` (used by `test` and `client-ledger-live` before
    anything else): verify. On a mismatch it prints
    `!!! B1026 STAMP MISMATCH ... REBUILDING IN-JOB ...`, runs
    `willeq_build_tree`, and verifies again (a failure there fails the job).
  - `gate`: a comment explaining why it needs no verify (it builds its own
    `$GLES2_TREE` in-job via `.visual_gate_prepare`).
  - The `.goldfarm` comment now says what is true: both runners carry
    `x86-build`, stages may land on different hosts, and the stamp check is
    what makes that safe.
  - **Deviation from the brief, with reason:** the brief asked for
    `.build_tree:` as a script anchor used with `!reference`. GitLab's
    `!reference` only splices lines in unconditionally, but the consumers must
    rebuild ONLY on a mismatch. So the anchor defines a shell function, and
    `!reference` splices that definition into each job. There is still one
    copy of the steps.
- `scripts/ci/test_ci_build_tree_stamp.py` (ctest `CiBuildTreeStamp`, label
  `hygiene`, `tests/cmake/hygiene.cmake`): reads `.gitlab-ci.yml` as text (no
  PyYAML on the runners, and `!reference` is a GitLab tag). It asserts:
  - `test` and `client-ledger-live` verify before their first ctest or client
    line;
  - the verify block runs verify, rebuild, verify;
  - `.build_tree` writes exactly one host+sha claim, after `cmake --build`,
    with a ref-only stamp before the configure;
  - `build` has no private copy of `cmake --build`;
  - `.goldfarm` no longer says "ONE runner carries", `x86-build-2` or
    "idles until".

  It also self-tests: it checks that a copy of the file without the verify,
  and a copy with the claim moved before `cmake --build`, both fail. Against
  the pre-B1026 file it is red (no `.build_tree:`).
- `scripts/ci/test_prune_ref_build_trees.py` (`PruneRefBuildTrees`): new class
  `BuildClaimStamp` covers:
  - same host + same sha matches;
  - another host (the pl809 shape) is a mismatch naming both;
  - another sha is a mismatch;
  - a missing stamp and an empty tree are mismatches;
  - a ref-only stamp over an old claim is a mismatch;
  - the prune still reads and keeps a claim stamp;
  - `main()` exit codes 0/1 and the `--verify` argument check;
  - `--stamp` defaults host to this machine.
- Docs:
  - `docs/ci.md`: new subsection "Two x86 runners: the host+sha stamp (B1026,
    #346)". Also updated the "What runs where" table, the host-guard snippet
    and the cold-run recipe, which now says to clean EACH runner.
  - `docs/decisions.md` (B1008 entry): the "one runner carries the tag"
    sentence is annotated as superseded.
  - `refactor/B1008/README.md`: the single-runner open item is struck.
  - `refactor/BOARD.md`: new B1026 row. B1008's "fold build+test+gate" open
    item is struck.

No new count was stated in a doc (pipeline/job ids and line numbers are
historical citations, not tree measurements), so `check_doc_counts.py` needs no
row.

## Proof

Traces are saved in `/mnt/projects/fable/review/B1026/job<id>.trace`.

**Head `99a97cd8`: push pipeline 817 and MR pipeline 818 ran concurrently.
Both split, and both are green.**

| pipeline | hygiene | build | test | gate |
|---|---|---|---|---|
| 817 (push) | 4061 runner-1 ok | 4062 **runner-1** ok | 4063 **runner-2** ok (5735/5735 + 1) | 4065 runner-1 PASS |
| 818 (MR) | 4068 runner-2 ok | 4069 **runner-2** ok | 4070 **runner-1** ok (5735/5735 + 1) | 4072 runner-1 PASS |

These are exactly pl809's shape (build on one host, test on the other), but
the verify correctly said MATCH. The other pipeline had just built the SAME
sha on the test host, so that host's tree was a genuine build of this
commit:

    job 4062 (build, runner-1):
    stamped build/ref/task-b1026-ci-host-stamp ref=task/B1026-ci-host-stamp (ref only, no build claim)
    stamped build/ref/task-b1026-ci-host-stamp ref=task/B1026-ci-host-stamp host=gitlab-runner-1 sha=99a97cd8658bc6bfbd360ec31862c1b9fedbd521
    job 4063 (test, runner-2):
    stamp MATCH build/ref/task-b1026-ci-host-stamp (stamp read): stored host=gitlab-runner-2 sha=99a97cd8658bc6bfbd360ec31862c1b9fedbd521 ref=task/B1026-ci-host-stamp at=2026-10-12T02:02:09Z; expected host=gitlab-runner-2 sha=99a97cd8658bc6bfbd360ec31862c1b9fedbd521

A split onto a host whose tree is at ANOTHER sha is the mismatch case. It is
recorded below from the pipelines at the next head(s).

MISMATCH_PROOF_PENDING
