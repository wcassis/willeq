# T94 — Remember that a race has no model, instead of rediscovering it 130 times

**Slot: slot1. Headless characterization + fix. Live re-measure at the end —
eqemu lock normally if free; if slot3 still holds it, report the headless
numbers and flag the live leg as outstanding.**

**Territory: `src/client/graphics/eq/` (race model loading), `tests/`,
`refactor/`, `/mnt/projects/fable/review/T94/`.**

Read T91's Review in full first — especially its honesty caveat.

## The measurement this is built on

T91 measured Nektulos's `loadZoneStep8_Entities` at **246.065s**, of which
**230,749ms (93.8%)** went to **130 of 452 entities (28.8%)** whose race
matches no model anywhere. Befallen (171 entities, 5.85s) shows no such cost —
only 14 failures at 18%.

There is **no negative cache**: `notFound` in
`race_model_loader.cpp:984-1032` is a counter, not a memo. So "race R gender G
has no model" is rediscovered from scratch for every entity carrying that race.

Note what is *not* the cost: `loadNumberedGlobalModels()`
(`model_loading.cpp:52`) is guarded by `numberedGlobalsLoaded_`, so
`global2-7_chr.s3d` are read from disk once. The repeated expense is the
**per-race search across the loaded data**, plus the `otherChrCaches_` LRU
thrash T91 already fixed.

## Part 1 — Establish the post-T91 baseline FIRST

T91's LRU fix landed but **its magnitude was never confirmed** — the executor
could not reproduce the 230s headlessly, most likely warm page cache locally
versus T82's cold NFS capture. So:

**Measure Nektulos and Befallen with T91's fix in place, before adding
anything.** Without that number you cannot attribute any improvement to this
unit rather than to T91.

If the post-T91 cost is already small, **say so and stop** — the negative
cache may be unnecessary, and that is a perfectly good result. Do not
implement a fix for a cost that no longer exists.

## Part 2 — The negative cache

If Part 1 shows the per-race search is still expensive:

Cache the negative result — keyed on whatever actually determines the lookup
(race id and gender at minimum; check whether variant or texture index
participates). On a hit, skip the search entirely.

**Invalidation is a non-issue and should be stated as such**: the S3D archives
are static for the life of the process, so a race that has no model at startup
has no model at shutdown. A permanent negative cache is correct here. Say so
in a comment rather than leaving a future reader wondering.

**Thread safety**: `RaceModelLoader` already uses locking (see T91's
`storeOtherChrCacheLocked`). Follow the existing pattern; do not invent a
second locking discipline.

**Fix the logging while you are there.** A missing race currently produces
noise proportional to entity count. Log it **once per race**, with the count
of entities affected — that turns 130 lines of noise into one useful line, and
it is how the race-457 class of gap should have been visible all along.

## Part 3 — Measure again, honestly

Before/after on **both** zones. State the numbers. If the improvement is
smaller than expected, say so — T91's caveat exists because an unverified
magnitude claim is worse than none.

**Do not claim the zone-load problem is solved.** This removes one dominant
cost; whether what remains is acceptable is a separate question, and the
async/imposter work Will described (already partly built in `SimWorker`) is a
separate arc.

## What you may not do

- Do not implement distance gating, lazy building, or imposters. Will has
  ruled on the architecture separately and this unit is not it.
- Do not suppress the missing-race signal entirely — we still need to know
  which races are unmatched. Once per race, not once per entity.
- Do not change how models are found, only how a known-absent result is
  remembered. **STOP and report** if a fix seems to require changing the
  resolution order.

## Verification

Full build and suite (expect 2080/2081 plus any new tests; sole pre-existing
failure the documented ALSA `AudioManagerTest.PreloadCommonSounds`). Visual
gate — goldens should not move (an entity with no model still gets a
placeholder cube); **if they do, that is a signal**: say so and explain.

New tests must be **proven able to fail** — break the behaviour, show red,
restore, show green, and report that you did it.

Measurements to `/mnt/projects/fable/review/T94/` with a README.

## Review

(executor fills: the post-T91 baseline for both zones; whether the negative
cache was warranted; the cache key and why; before/after numbers; the logging
change; proof the tests can fail; what remains of the load cost and what it is
made of; deviations)
