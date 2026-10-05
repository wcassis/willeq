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

Pending implementation and measured verification. Draft MR targets `fable`,
`Closes #306`; readiness requires every acceptance check above.
