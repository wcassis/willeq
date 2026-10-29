# No-agent jobs -- the pure-script phases run outside worker runs

B1203 (#522), step 3 of the deterministic allocation stack (Will, 2026-10-29). The B1201
ledger measured 80 of 305 worker seat-hours spent inside commands longer than 60 s, 34 h of
them in sleep/poll loops on background renders and builds. A phase that needs no model
(build, render, measure, judge, evidence copy) therefore runs as a hermes `--no-agent` job:
the worker leg ENDS where the script phase begins, the job runs with no seat held, and the
next leg starts only when the job's done-file exists.

## The jobs (`scripts/hermes/jobs/`, all run on hermes-ext)

| script | does | done-file (`$JOBS_ROOT` = `/mnt/scratch/jobs`) |
|---|---|---|
| `gate_baseline.sh [<ref>]` | the per-fable-tip gate baseline: fetch + checkout `<ref>` (default `origin/fable`) in `/home/user/src/willeq-wt/gate-baseline`, `rbuild --target model_viewer`, `render_gate.sh all` (CONC 3) into `/mnt/scratch/gate-baseline/<sha8>-sl`, judge into `<sha8>-sl/tables/`. ONE AT A TIME: a second call queues behind the running one (lock dir + the legacy `pgrep -f "[g]ate-baseline.*render_gate"`). `--smoke [rows]` = 2 b1130 rows of the tree as it is, no checkout/build/lock | `gate-baseline/<sha8>.done` |
| `gate_after.sh <slug> [rows]` | a card's AFTER: `rbuild` its worktree, `render_gate.sh` (CONC 2, `SET` = all by default) into `/mnt/scratch/kb-evidence/<slug>/gate`, judge the AFTER and the matching `-sl` baseline (merge-base of the worktree HEAD and origin/fable) with the SAME judge, write `tables/after/`, `tables/base/`, `tables/delta.tsv`, `tables/flips.tsv`, `tables/pass_counts.txt`, and copy `tables/` to `~/kb-evidence/<slug>/gate_tables/` | `<slug>/gate_after.done` |
| `evidence_copy.sh <slug> <B####>` | the hand-back copy: `~/kb-evidence/<slug>/` and `/mnt/scratch/kb-evidence/<slug>/` to `/mnt/projects/fable/review/<B>/worker/` (frames and PNGs excluded), clips `~/kb-evidence/VISUAL-FX/<slug>/` to `/mnt/projects/fable/review/VISUAL-FX/<B>/` | `<slug>/evidence_copy.done` |
| `dispatch_jobs.py [--apply]` | finds `JOB:` lines in done cards' latest summaries (kanban.db read-only), starts the jobs that have no done/running file (`--apply`; default dry run), lists each JOB card's children with WAITS / READY-AFTER | -- |
| `gate_delta.py <base> <after> <out>` | the per-row delta + verdict flips of two judged gates (used by gate_after.sh) | -- |

Every job is idempotent: a done-file short-circuits it (`--force` re-runs), a live
`<job>.running` file makes a second start a no-op, and `render_gate.sh` itself skips rows
already measured, so a killed job resumes where it stopped. Renders YIELD while an `eqgame`
(the lane-5 oracle) runs on the ThinkPad or its load1 exceeds `LOADMAX` (12), exactly as
`render_gate.sh` does. A done-file is ONE line:

```
JOB gate_after kb-b1234-x status=ok head=1a2b3c4d base=cdf6fbb6 rows=87/87 judge_rc=0/0 delta_rc=0 wall_s=5400 tables=/mnt/scratch/kb-evidence/kb-b1234-x/gate/tables finished=2026-10-30T03:12:00Z
```

`status` is one of ok, incomplete (fewer rows measured than planned), no_baseline,
build_failed, judge_verify_errors (the B1188 judge's built-in re-derivation check failed),
judge_failed, checkout_failed, no_worktree, rsync_failed, no_evidence. A non-ok status is still
a finished job: the next leg (or the orchestrator) reads it and acts.

## The chaining convention

1. A leg whose next phase is a pure script ends with `kanban complete` and puts the job on
   its own line in the summary, e.g. `JOB: gate_after kb-b1234-x`, `JOB: evidence_copy
   kb-b1234-x B1234`, `JOB: gate_baseline <sha>`. (The card body says which; a dev leg that
   implements a fix ends at "pushed, model_viewer builds" + `JOB: gate_after <slug>`
   instead of rendering in its own run.)
2. The orchestrator cuts the chained next leg (`--parent <this card>`), e.g. the judging /
   README leg that READS `~/kb-evidence/<slug>/gate_tables/`.
3. `dispatch_jobs.py --apply` (no-agent cron, every 5 min) starts the job.
4. The kanban dependency promotes the child to `ready` as soon as the parent is done; the
   B1204 allocator keeps it blocked `awaiting job:<job>:<slug>` until the done-file exists,
   then unblocks it like any other admitted card. Until B1204 is live, the orchestrator does
   that step by hand (`dispatch_jobs.py` prints `CHILD <id> WAITS` / `READY-AFTER`).

## Cron specs (text only -- installing is the orchestrator's call)

```
ssh hermes-ext
mkdir -p ~/.hermes/scripts/jobs && git -C ~/src/willeq fetch -q origin
for f in jobs_lib.sh gate_baseline.sh gate_after.sh evidence_copy.sh gate_delta.py dispatch_jobs.py; do
  git -C ~/src/willeq show origin/fable:scripts/hermes/jobs/$f > ~/.hermes/scripts/jobs/$f; done
chmod +x ~/.hermes/scripts/jobs/*
cat > ~/.hermes/scripts/jobs/dispatch_cron.sh <<'SH'
#!/usr/bin/env bash
# silent unless a job was started (a --no-agent job delivers stdout verbatim; empty = silent)
out=$(python3 -I "$HOME/.hermes/scripts/jobs/dispatch_jobs.py" --apply --hours 48 2>&1) || { echo "$out" | tail -5; exit 1; }
echo "$out" | grep '^START' || true
SH
chmod +x ~/.hermes/scripts/jobs/dispatch_cron.sh
H="$HOME/.hermes/hermes-agent/venv/bin/python $HOME/.hermes/hermes-agent/hermes"
$H cron create 'every 5m' --name willeq-job-dispatch --no-agent --script jobs/dispatch_cron.sh --deliver local
```

The per-merge baseline (solo prompt rule 7) becomes one line in the orchestrator's merge step:
`nohup setsid bash ~/.hermes/scripts/jobs/gate_baseline.sh <merged sha> >/dev/null 2>&1 &`
(it queues itself behind a running baseline), or a `JOB: gate_baseline <sha>` line in the
merge comment's card summary for the dispatcher.
