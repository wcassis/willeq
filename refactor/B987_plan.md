# B987 — bounded crowd observation controls

Issue #306; task `t_80bc1578`; branch `task/B987-crowd-readiness`.
Base: B975 merged as `ee538d2c14849ac309dd6aa0d7dbef429b884498`.

## Scope and territory

Capture harness only: `scripts/b964/{pi_crowd.sh,crowd_run.sh,make_tour.py}`,
`scripts/b975/{frametiming_table.py,test_frametiming_table.py}`, focused harness
tests and their hygiene registration. Documentation: this plan, B987 evidence
summary, B975 current status, capture usage and relevant board references.
No renderer, GPU policy, workload, spawn/layout, movement or renderer-selection
changes. No budget, threshold, golden or baseline changes.

Expose the generator's existing idle/hold durations on both wrappers. Preserve
legacy defaults; document explicit B975 acceptance controls `--idle 180
--hold-after 75`, with a finite overall cap. Validate final-visit ready/steady
and (when requested) complete post-ready idle timing blocks. Never borrow
warmup, earlier visits, or partially overlapping timing windows. Missing ready
or exhausted observation remains failure, not a retry or acceptance waiver.

## Stop conditions

Stop for unavailable credentials/services, foreign locks, lease loss, failed
cleanup, unresolved acceptance failures or required changes outside territory.
In-scope diagnosed defects may be fixed and retested with original failures
retained. Never merge; downstream QA/review are pre-created.

## Verification

Regression tests: late readiness, no readiness, no final steady, earlier visits,
missing idle boundaries, partial timing windows; both wrappers' defaults and
option forwarding; invalid/nonfinite durations fail before resource acquisition.
Run existing final-visit regressions and appropriate source hygiene. CI must
pass hygiene/build/test/gate/build-arm at final HEAD.

Fresh real-Pi Qeynos batched/per-node pair using immutable changed harness,
explicit CI binary staging, production Mesa and existing memory/CPU/pixel guards.
Record source/binary/Mesa/harness hashes, spawn acknowledgements, ready/steady
timestamps and idle blocks. Use guardian jobs, checkpoints and atomic locks.
Prove snapshot byte identity with world down, stopped client/stack, and release
only owned locks. Evidence: `/home/user/kb-evidence/b987-crowd-readiness/dev/run-305/`.
Run283 failure and run284 controlled recovery are diagnosis, not fresh tests.

## Review

Implemented on MR !82 (`Closes #306`), not merged. Both wrappers expose idle,
hold and finite cap controls; defaults and the generated workload are preserved.
The shared observer fails closed on absent final ready/steady, wrong final zone,
or missing complete post-ready idle blocks when timing is requested. It discards
boundary-crossing frame/name-tag windows rather than counting their warmup.

### Fresh Pi pair — measured 2026-10-05, run 305

Immutable harness and ARM publication:
`d79f43394f19d5f741ff9f2812fc89ff2fad4903`. Later changes are documentation only,
verified by the terminal handoff's source-equivalence check. CI owns the build;
the publication was explicitly copied from the Pi-visible CI store to the
worker, then the sweep staged and hash-verified it on the Pi. Production Mesa,
800x600, Qeynos, `--idle 180 --hold-after 75 --cap 720 --frametiming`.

| renderer | final ready UTC | final steady UTC | complete idle blocks | transparent median ms |
|---|---|---|---|---|
| batched | 03:23:16.891700 | 03:24:17.047000 | 66 | 1.3195 |
| per-node | 03:32:29.983100 | 03:33:30.157500 | 36 | 32.7035 |

Timestamps above are the runner's read epochs, not inferred from script waits.
Both final faces occurred after readiness in this fresh pair; late readiness
is exercised by the regression, not claimed as artificially induced on the Pi.
Both have an actual post-ready idle interval exceeding the requested 180 seconds,
all 200 spawn acknowledgements, final-visit steady, clean exit, unchanged memory
and GPU guards, and the original nonblank-frame check. Both generated tours are
byte-identical to the controlled run284 recipe. The batch is faster in this
matched crowd pair; this is not a new standalone name-tag microbenchmark claim.

Binary SHA-256:
`f1435b28ebc2c42ba27b113c6d368df0ffb447fa4d92382cd0c0d4cbac5ea058`.
Mesa pin:
`cc4d9beb38dcdd1345c719a5f2223e802c51af1b1acaf49091c24188fc3d14bf`.
Complete source/binary/harness identities, raw log paths, block line numbers,
guard values and snapshot hashes: external run305 `capture-identity.json`,
`capture-verification.json` and `verified-{imp200far,tagnode200far}.json`.

Each snapshot restored byte-identically with the world down. Each sweep stopped
the stack and released its owned locks before the next leg acquired them.
The guardian finished with exit 0 and cleanup exit 0; `crowd/cleanup-0.log`
independently proves the final stopped client/stack and absent locks.

### Regression, CI and boundaries

Focused timing, wrapper and existing final-visit regressions pass. Wrapper tests
execute both shell wrappers with inert services and verify unchanged workload,
renderer selection, defaults, duration forwarding and clean-exit failures.
A real bounded runner subprocess that never emits readiness reaches its cap;
the observation verifier rejects it. No live x86 matrix or unrelated renderer
optimization was repeated. Original renderer/policy/budget/golden sources are
unchanged. Automatic screenshots are early startup images, not final crowd
visual approval; the existing CI pixel checks are unchanged. The advisory
64 MiB owned-memory target remains unmet.

Pipeline 503 passed hygiene/build/test/gate/build-arm on the documentation
correction. Final-HEAD CI and rerun source/doc checks are recorded in run305
`result.json`, `ci-response.json` and `final-checks/commands.json`; CI is
incremental, not a cold-build claim. Optional skipped tests are not passes.

### QA finding F1 — duration validation agreed across all consumers

Independent QA (run 306, verdict FAIL) found that the Python validator
accepted spellings the shell consumers do not: `int()` takes `0_1`, `1_000`,
leading zeros, signs, whitespace and non-ASCII digits, while
`scripts/b964/crowd_run.sh` kept the RAW token for its deadline comparison
`[ $((now - start)) -ge "$CAP" ]`. Executed reproduction against the actual
wrapper with the repository's inert-service fixture: `--cap 1` stopped in
1.46 s; `--cap 0_1` ran until an external process-group timeout with 97
"integer expression expected" errors -- the overall deadline silently
DISABLED. Sibling, Pi side: `scripts/pi_rendered_zone_sweep.sh` took
`--cap`/`--stall` unvalidated into `timeout $((CAP_S + 120))` and
`rendered_zone_run.py` (0800/0900 invalid-octal errors; 0720 SILENTLY octal
-- the ssh timeout 584 while the runner is told 720), and never validated
`--stall`/`--hold`; the same class hit `scripts/b973`'s `--build`
(`$((BUILD_S + 360))`, `cap=$((BUILD_S + 420))`).

Fix (this MR, after run 306): ONE definition of a valid duration for `--idle`,
`--hold-after`, `--cap`, `--stall`, `--hold` and `--build` -- a plain ASCII
decimal `0` or `[1-9][0-9]*` in 0..86400 seconds (`scripts/b964/observation_controls.py`'s
`_seconds()`). Enforced in `scripts/b964/crowd_run.sh`, `scripts/b964/pi_crowd.sh`,
`scripts/pi_rendered_zone_sweep.sh` (which is called directly by other rigs)
and `scripts/b973/{crowd_run.sh,pi_crowd.sh}`, each BEFORE any lock, stack or
Pi resource, each re-checking the same regex with `LC_ALL=C grep -qE` (a
`[0-9]` range in `[[ =~ ]]` follows the locale and, in a UTF-8 locale,
matches non-ASCII digits), and the wrappers then CONSUME the validator's
printed canonical integers -- no path forwards a raw token. All defaults and
the tour workload are unchanged (x86 cap 420, Pi cap 720, idle 40, hold 15).

Regression: `CrowdObservationControls` gains the executed x86 deadline test
(a continuously logging child stopped at the cap, the test QA's probe would
have been), every rejected spelling (`0_1`, `1_000`, `0720`, `0800`, ` 5`,
`+5`, `-1`, `5.0`, `1e3`, a non-ASCII digit, empty, over range) on BOTH
wrappers and on the sweep with its REAL scripts in a sandbox, the good
spellings accepted, and the canonical integer reaching the sweep. RED on
`e3078ac5`: 3/8 of the new tests fail there (the rejected-spelling test on
`0_1` first -- the wrapper accepted it and acquired the lock; the sweep
sandbox reached the Pi probe with rc 68; `--env` unknown). GREEN on the head:
8/8.

`scripts/b964/pi_crowd.sh` also gains `--env K=V` (forwarded to the sweep),
so the B988 lane can run the pair with `WILLEQ_LIMA_NOBOCACHE=1` -- one-line
passthrough, in scope per the card.

Pipeline (on the head): see the MR description. The fresh confirming Pi pair
through the fixed wrappers is recorded under "Fresh Pi pair" below.

Preserved failures: initial pipelines 501/502 failed the newly changed hygiene
census and a relative documentation citation. Both documentation defects were
corrected without relaxing checks. A preliminary partial inspector ran before
the wrapper wrote its observation artifact; it failed on that missing file,
then the final audit waited for cleanup. No failed capture was retried.

B975 run283 remains FAILED (`no_steady_60s`, no post-ready idle blocks).
Run284's controlled recovery is historical diagnosis, not B987 verification;
the new parser still rejects run283 and accepts run284. Their original files
were not overwritten. Fresh B987 evidence is exclusively the run305 pair above.
