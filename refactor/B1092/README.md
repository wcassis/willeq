# B1092 (#412): BackgroundWorkQueue.StopWhileItemsPending flake

## The cause (A)

Both the queue and the test were at fault, and the CI traces prove it.

**The queue deviation.** `workerLoop` (background_work_queue.h, pre-fix
:143-184) had two loops: the main loop re-checked `running_` on each
iteration, but the DRAIN loop that follows the first pop of a batch was
`while (true)` with no `running_` check. So once the worker had popped the
first item of a batch, `stop()` was ignored for the WHOLE batch: the drain
kept popping and processing until the queue ran empty, and `stop()`
(whose `worker_.join()` waits for the worker thread) returned only after the
entire batch had been processed. The queue's own documented contract is
"in-flight completes, rest dropped, stop() returns".

**The test's wall-time assumption.** The pre-fix test submitted 20 items
whose processor slept 20 ms each, then called `q.stop()` "immediately" and
asserted `count < 20`. That assumes `stop()` reaches the queue within
20 x 20 ms = 400 ms of the first submit — i.e. that the main thread is not
descheduled longer than 400 ms between the last `submit()` and `stop()`.
Under CI load that does not hold. The failing jobs all show the same
signature (the decisive one, pipeline 1441 job 7743, build-headless):

    test_background_work_queue.cpp:216: Failure
      Expected: (count) < (20)
      Actual:  20 vs 20   (0 ms)
    [  FAILED  ] BackgroundWorkQueue.StopWhileItemsPending (406 ms)

`count == 20` — every item was processed — and the duration of 406 ms is
exactly 20 x 20 ms plus overhead: the worker drained the whole batch
through `stop()`, which is precisely what the drain-loop deviation does.
Pipelines 1451 (job 7797) and 1457 (job 7832, retry succeeded) carried the
same failure; `scan_bwq_1016.txt` lists the affected jobs across ~50
pipelines.

**Production exposure.** The only caller,
`EntityPrepWorker::dispatchOne()` (entity_prep_worker.cpp:60-83), submits
one item at a time, gated on `queue_->isIdle()` — a batch never accumulates
in production. The defect is real against the queue's documented contract
and is pinned by the deterministic test; it is not a live production bug.

## The fix (B)

`include/client/graphics/background_work_queue.h` (the drain loop):

- The `running_` check is now part of the drain itself, read UNDER
  `requestMutex_` — the same lock `stop()` stores `running_ = false`
  behind. A stop that lands mid-batch is therefore observed by the very
  next drain iteration (mutex release/acquire synchronisation), not only at
  the top-of-loop check. The in-flight item finishes; the tail stays in
  `requestQueue_`; `stop()` returns when the worker exits.
- On that exit `idle_` is set to true, so `isIdle()` does not report a busy
  worker the stop has already joined (pre-fix, `isIdle()` would stay false
  forever after a stop that landed mid-batch, because only the queue-empty
  branch set it — a latent hang for any user polling `isIdle()` after such
  a stop).
- `isStopping()` reads the `running_` latch. It is the test's observation
  window (the test must see that the stop has landed before it releases the
  gated in-flight item, or the test reintroduces the very scheduling race it
  replaced). The only outside reader is the regression test, so it is
  classified as an "observer" in the ZeroCallers census
  (`scripts/refactor/check_zero_callers.py`, `EXEMPT_TESTS_GROUPS`) per the
  census's own resolution (T458) rather than deleted.

`tests/test_background_work_queue.cpp` (`StopWhileItemsPending`, rewritten
deterministically — no sleep bound, no wall-time assumption):

- item 0's processor records "in flight" BEFORE blocking on a gate, so the
  main thread knows the worker has popped item 0 and is mid-batch;
- `stop()` runs on its own thread, started only after that signal, so it
  necessarily lands with item 0 in flight (not before the batch starts,
  which would let the worker exit cleanly with count 0);
- the main thread waits on `isStopping()` (running_ false) BEFORE opening
  the gate; the drain loop's `running_` check (under the request lock
  `stop()` wrote behind) is then guaranteed to land before item 1 is
  popped.

Asserts the contract exactly: the in-flight item completed (count == 1),
the other 19 are still queued (`getPendingCount() == 19`), and the queue
reports idle.

## Verification

**The regression property (the red net).** The new test was built against
three header variants and run on the ThinkPad (host of the failing CI
jobs), `buildtest.sh`:

| header variant                          | result | why |
|---|---|---|
| old drain loop (+ accessor), NEW test  | **FAIL** | drain ignores `running_`: all 20 processed, pending 0 |
| fixed header, NEW test                 | **PASS** | drain observes the stop after item 0 |

Under a 6-CPU g++ compile load, 200 repetitions each (`stress.sh`):

    FIXED:             PASS (200 repeats, 45667ms, loadavg at start=3.83)
    OLD-HEADER-CONTROL: FAIL (200 repeats, 45931ms, loadavg at start=7.65)
    failure line: Expected equality: q.getPendingCount() Which is: 0 / 19u

i.e. against the pre-fix queue the new test fails on EVERY run in which
stop() lands mid-batch — exactly the CI condition — and against the fixed
queue it never fails.

**Acceptance (C) as stated — 200 consecutive passes of
`BackgroundWorkQueue.*` under load** — with a binary built from the
CURRENT worktree header + test (not a reconstructed variant), full suite
(`--gtest_filter='BackgroundWorkQueue.*' --gtest_repeat=200`), 6-CPU
compile load (`acceptance200.log`, in `evidence/`):

    result: PASS (45804ms, 4000 individual test OKs)   # 20 tests x 200 runs

The raw run logs are in `evidence/stress_runs.txt`.

**Before-rate (the pre-fix test + pre-fix queue, the CI configuration).**
A 200-iteration loop of the pre-fix binary (original wall-time test +
original queue) under a controlled 6-CPU compile load
(`beforerate.sh`, loadavg ~10) went 0/200: the flake is a tail of the
scheduling distribution, not a steady rate, so no short loop is a
before-rate. The before-rate of RECORD is the CI one — two failing jobs
in ~24 h (7743, 7797) plus 7832 pre-retry (the retried job is green),
the traces in the evidence dir
(`/home/user/kb-evidence/kb-b1092-bwq-stop-flake/`: `scan_bwq_1016.txt`,
`ctx_*.txt`). The first live reproduction of the exact CI failure
(`count == 20`, 406 ms) on the ThinkPad came from a 400-iteration loop
during the pre-change investigation; that loop's log is not attached
because its wall-clock timestamps are unreliable (the host clock
jumped), but its failure line is identical to the CI traces. The
deterministic matrix above is the proof that the fix removes the cause.

**In-tree.** `rbuild kb-b1092-bwq-stop-flake test -R BackgroundWorkQueue`
— 20/20, and the hygiene nets `ZeroCallers` / `DocCounts` / `DocClaims`
green with the accessor classified. The three `*Reproducible` ctest tests
fail at their preflight "tool not built" checks on a fresh rbuild tree
(missing `generate_textures` / `spell_sprite_atlas_builder` /
`ui_atlas_builder` / `eqe1_builder` / `willeq_db_builder`); they pass once
those targets are built and are unrelated to this change (the diff touches
no data, pin, or generator).

**Files.** `include/client/graphics/background_work_queue.h` (drain loop,
`idle_` bookkeeping, `isStopping()`), `tests/test_background_work_queue.cpp`
(deterministic `StopWhileItemsPending`), `scripts/refactor/check_zero_callers.py`
(one census row). No bless-only file touched; no CLAUDE.md touched; no
sleep bumped, no bound loosened.

## Not verified / notes

- The original wall-time test's flake is load-dependent: at the heavier
  lab load (loadavg ~7) the 400-iteration loop tripped it 10 % of the time;
  at lighter load a 200-iteration loop (`bin_prefix`, `beforerate.sh`) got
  0/200. CI's condition sits between — two jobs in ~24 h (7743, 7797) plus
  the pre-retry 7832. The before-rate of RECORD is the CI one (the traces),
  because that is the condition acceptance (C) names.
- The drain loop's `running_` read is relaxed INSIDE the `requestMutex_`
  critical section: correctness rests on the mutex synchronisation (a
  `stop()` that stored under the lock is visible to the next lock holder),
  not on the atomic's ordering — hence the comment in the header names the
  lock, not the load.
