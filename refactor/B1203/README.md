# B1203 -- PURE-SCRIPT PHASES AS NO-AGENT JOBS: gate_baseline, gate_after, evidence_copy, the JOB: chain

Issue #522 (class B, owner session). Step 3 of 4 of Will's 2026-10-29 deterministic
allocation stack (step 1 = B1201 #520 MR !350, step 2 = B1202 #521 MR !351). Tool build: bash
+ python3 stdlib, no C++, no cron installed, no card touched. The spec and the cron text are in
`docs/hermes/noagent_jobs.md`.

## Why

The B1201 ledger (MR !350, `refactor/B1201/tables/headline.tsv` and `by_phase.tsv` on branch
`task/kb-b1201-inference-ledger`) measured 80 of 305 worker seat-hours inside commands longer
than 60 s, 34 h of them in `wait` (sleep / poll loops on the worker's own background renders
and builds), 13 h `build`, 13 h `measure`, 5.5 h `render`. These jobs run those phases with no
inference seat held.

## What landed (`scripts/hermes/jobs/`)

| file | role |
|---|---|
| `jobs_lib.sh` | job state (`job_begin()` `scripts/hermes/jobs/jobs_lib.sh:15`, `job_end()`; `<job>.running` / `<job>.done` / `<job>.log` under `/mnt/scratch/jobs/<slug>/`), the judge wrapper `judge()` (`scripts/hermes/jobs/jobs_lib.sh:45`: runs `refactor/B1188/scripts/build_tables_b1188.py` with its hard-coded `WT` replaced by the given checkout -- the same substitution as `/mnt/scratch/gate-baseline/8c2bfe7c-sl/tables/judge.py`, whose only diff from the committed script is that line) |
| `gate_baseline.sh` | the per-fable-tip baseline (solo prompt rule 7 as a script): checkout + rbuild + `render_gate.sh all` + judge; one at a time (lock dir `/mnt/scratch/jobs/gate-baseline.lock` + the legacy `pgrep` check, queued not refused); `--smoke` mode |
| `gate_after.sh` | a card's AFTER: rbuild + render + measure + judge AFTER and BASE with the same judge + `gate_delta.py`; tables copied to `~/kb-evidence/<slug>/gate_tables/` |
| `gate_delta.py` | per-row AFTER-vs-BASE delta and verdict flips of two judged dirs (generalises `refactor/B1188/scripts/delta_b1188_base.py`) |
| `evidence_copy.sh` | the hand-back rsync to `/mnt/projects/fable/review/<B>/worker/`, frames/PNGs excluded, VISUAL-FX clips |
| `dispatch_jobs.py` | reads `JOB:` lines from done cards (kanban.db `mode=ro`), starts missing jobs with `--apply` (default dry run), lists chained children WAITS / READY-AFTER; `--self-test` |

The render/measure itself is unchanged: both gate jobs call
`/mnt/projects/fable/review/GATE-BASELINE/render_gate.sh`, which keeps its yield rule (waits
while an `eqgame` runs on the ThinkPad or load1 > 12), its per-row skip of measured rows, and
its render-on-ThinkPad / measure-on-hermes-ext split.

## Smoke run (the proof; `refactor/B1203/smoke/`)

ThinkPad load before the run: `3.86 4.71 5.82` (load1 < 8, so the smoke ran; the 423ab3b6
baseline was rendering at CONC 3 at the same time, 45/87 rows measured).

1. `bash scripts/hermes/jobs/gate_baseline.sh --smoke r0 r1` (on hermes-ext, tree
   `/home/user/src/willeq-wt/gate-baseline` at 423ab3b6, CONC 2) ->
   `refactor/B1203/smoke/baseline.done`:
   `status=ok sha=423ab3b6 rows=2/2 judge_rc=0 wall_s=159`; per-row times
   `refactor/B1203/smoke/baseline_row_times.tsv` (render 38/40 s, measure 113/119 s).
2. `SET=b1130 WT=/home/user/src/willeq-wt/gate-baseline BIN=/home/user/willeq-builds/gate-baseline/build/bin/model_viewer NO_BUILD=1 BASE_SHA=423ab3b6 BASE_DIR=/mnt/scratch/jobs/smoke/423ab3b6-sl EV=/mnt/scratch/jobs/smoke/after-423ab3b6 bash scripts/hermes/jobs/gate_after.sh smoke-after r0 r1`
   -> `refactor/B1203/smoke/after.done`: `status=ok head=423ab3b6 base=423ab3b6 rows=2/2 judge_rc=0/0 delta_rc=0 wall_s=162`.
3. The AFTER is a second, independent render of the SAME tree and rows, so the delta is a
   determinism check of the whole chain: `refactor/B1203/smoke/delta.tsv` has 18 b1130 cells
   (2 rows x 9 metrics), every `delta` 0; `refactor/B1203/smoke/flips.tsv` is the header
   only; `cmp` of the two measured `b1130_rows.tsv` for r0
   (`refactor/B1203/smoke/baseline_b1130_r0_rows.tsv` vs `after_b1130_r0_rows.tsv`) is
   identical. `pass_counts.txt` = the PASS counts over 40 rows (38 unmeasured rows judge n-a,
   so the counts are the 2 smoke rows only).

Not exercised by the smoke (on purpose, it is 45 min of ThinkPad time or touches the shared
baseline tree): the checkout + rbuild + lock path of `gate_baseline.sh` (the running
423ab3b6 baseline and the queued cdf6fbb6 one use that tree), and `evidence_copy.sh` (a real
slug's review dir). `dispatch_jobs.py --self-test` passes (`SELF-TEST PASS`); its dry run over
the live board (`--hours 72`) printed `DISPATCH cards=87 started=0 mode=dry-run` (no card
carries a `JOB:` line yet).

Smoke evidence on scratch (raw): `/mnt/scratch/jobs/smoke/` (logs, measure, tables) and
`/mnt/scratch/jobs/smoke-after/gate_after.log`.

## For the orchestrator (NOT done by this MR)

- Install the scripts and the 5-minute dispatcher cron: `docs/hermes/noagent_jobs.md`
  "Cron specs".
- Card bodies: a fix/dev leg that would render ends with `JOB: gate_after <slug>` (+ the
  chained judging leg cut with `--parent`); the dev ENV block's "FINISH" bullet can name it.
- Solo prompt rule 7 becomes `gate_baseline.sh <merged sha>` (it queues itself).

## Checks

- `bash -n` on every `.sh`; `python3 -m py_compile` on both `.py`; `python3 -I scripts/hermes/jobs/dispatch_jobs.py --self-test` -> `SELF-TEST PASS`.
- `python3 -I scripts/refactor/cite_check.py refactor/B1203/README.md --tree . --extra /mnt/projects/fable` -> `checked 2 cites, 0 problems, 0 address warnings`
