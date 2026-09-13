# T465 — `HeadlessShutdownTest` fails under `ctest -j6` load in every full run

Class P (process: a red that every verifier has to re-run by hand), **no lock,
no Pi**, slot2.

## The evidence

On 2026-09-13 the full `ctest -j6` run of three separate merged tips
(`cb9c3f3c`, T461's rebased tip, `641a9402`) each had exactly one red:
`HeadlessShutdownTest.NonLifecycleCommandIsHeldNotDroppedOrApplied`, green
standalone every time. T461 saw "its client printed nothing within 5 s under
load". EXECUTOR_PROTOCOL records an earlier shape of the same test failing with
`failed to open data/willeq.db (sqlite rc=14)` (T360). A test every unit waves
through as a "load artefact" is a test that cannot catch the defect it was
written for.

## Tasks

1. Read the test and what it launches (`tests/`, grep the name). Establish
   the actual failure mechanism(s) under load, with evidence: reproduce by
   running it alongside synthetic load (e.g. `stress-ng`/a parallel ctest of
   heavy binaries, or `taskset` to one core) — measure how often it fails and
   which assertion/timeout fires; capture the child's output. Cover BOTH
   shapes (5 s no-output timeout; sqlite rc=14 = SQLITE_CANTOPEN — relative
   path/cwd? a shared db file opened concurrently by another test? a temp dir
   collision?).
2. Fix the root cause, not the timeout number alone: e.g. wait on a readiness
   signal instead of a fixed 5 s sleep; absolute/`ResolveDataPath` db path;
   unique temp dirs. If a deadline must remain, justify its size against the
   measured worst case and make the failure message say what was being waited
   on.
3. Grep for other tests with the same pattern (fixed wall-clock waits on a
   spawned client, shared relative `data/willeq.db` opens) and fix or board
   them (B570–B579).
4. Prove: the synthetic-load repro fails ≥N times pre-fix and 0 times in the
   same number of runs post-fix; then two full `ctest -j6` runs with zero reds.
   Update EXECUTOR_PROTOCOL.md's load-artefact paragraph (remove the waiver for
   this test if fixed).

## Proof

Standard verification bar (the visual gate and ARM build are required only if
non-test sources change). Board B570–B579.

File territory: the test and its fixture, test-only helpers, whatever
production seam the readiness signal needs (keep it minimal and say so),
`refactor/EXECUTOR_PROTOCOL.md`. **Not** `src/client/graphics/ui/`/frame timing
(T462), **not** camera/player/hotkeys/`application_zone_loading.cpp` (T464).

STOP: the root cause is a real production shutdown defect (report it with
evidence before fixing — it becomes its own class A item).

## Review

(executor fills)
