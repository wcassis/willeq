# T113 — The Pi crashes mid-play; and it may be the same bug as T110

**Slot: whichever is free. Needs the Orange Pi. eqemu lock if a live session
is required to reproduce.**

**Territory: whatever the backtrace implicates. `tests/`, `refactor/`,
`/mnt/projects/fable/review/T113/`.**

Read T112's Review and its `deferred.md` entry first, then T110's plan and
T111's Review (which captured a teardown backtrace).

## The defect

T112 hit an **intermittent Pi-only mid-tour SIGSEGV** in **5 of 9** Pi
invocations. It reproduces under **both** gate states and **both** log levels,
so it is not the distance gate and not a logging artifact. It arrives as the
player nears Befallen's `x≈-333..-400, y≈5..20` — the `/move -400 20 3`
destination T109 and T111 both used — with **`RegionVis` jumping to
1,100+ of 3,596 just before the crash**.

It is **not deterministic**: the same tour completed cleanly twice.

This outranks T110's teardown SIGABRT. A crash at exit is untidy; **a crash
mid-play is a user-facing failure on the target platform.**

## The hypothesis to test first — they may be one bug

T110's crash is `corrupted size vs. prev_size` → SIGABRT during teardown.
**That message is glibc detecting heap corruption, not causing it** — the
corruption happens earlier and is only noticed when the allocator next walks
those structures.

So a mid-play SIGSEGV and a teardown heap-corruption abort in the same zone on
the same platform are plausibly **one defect with two manifestations**,
depending on whether the corrupted memory is dereferenced before shutdown or
merely detected at it.

**Test that before treating them as separate.** If confirmed, T110 closes with
this unit. If refuted, say so with evidence and T110 remains as scoped.

## Method — do not use live gdb

T112 measured that **DEBUG logging destabilises the Pi** (every DEBUG run
crashed mid-tour where INFO runs mostly did not), and T111 found gdb slows it
enough to distort behaviour. Both change the thing being observed.

**Use post-mortem core dumps instead.** T111 already established the
technique — set `core_pattern`, run normally at INFO, analyse the core
afterwards, and **restore the sysctl when done** (T111 did; do the same).

Given 5-of-9 reproduction, expect to run the tour several times to collect
cores. **Report how many runs produced how many crashes** — the rate is data,
particularly if it changes.

## What to establish

- The faulting frame and full call chain, **reported verbatim**.
- Which thread: render, game, loading, or the background prep worker T111
  wired up.
- What is being touched — freed memory, a null, a partially-destroyed object,
  or a corrupted heap chunk.
- **Whether the `RegionVis` jump to 1,100+/3,596 is implicated.** A visibility
  set expanding that sharply right before the crash is the strongest lead in
  the report — it suggests a buffer sized for a smaller region set, or a PVS
  lookup going out of bounds.
- Whether the crash and T110's teardown abort share a root cause.

## Consider what changed recently

T111 fixed two crashes of exactly this family and enabled sustained concurrent
background work for the first time — including a dangling `regionPvsDepthMap_`
pointer into a local `SimulationOutput`, and a SQLite handle opened
`NOMUTEX` while two threads used it.

**That is not an accusation — T112 confirmed this reproduces with the gate
inert.** But the prep worker now does real work it never did before, and
`regionPvsDepthMap_` is region-visibility data, which is exactly the area the
`RegionVis` lead points at. Check whether T111's fix was complete or whether a
sibling of it survives.

## Fix the narrowest thing

Once the cause is known, fix minimally and re-verify with **enough runs to
beat a 5-of-9 rate** — a nondeterministic crash that passes once has not been
fixed. State how many consecutive clean runs you observed.

**STOP and report** if the fix requires restructuring the visibility system,
the threading model, or zone teardown ordering. Those are their own units.

## Do not paper over it

- Do not suppress the signal, `_exit()` past teardown, or wrap it in a handler.
- Do not "fix" it by skipping the faulting access or clamping a bound without
  understanding why it is exceeded.
- Do not disable the code path that triggers it.

"Diagnosed, fix is larger than this unit" is a good outcome if the diagnosis is
precise enough that the next unit does not start over.

## Verification

Full build and suite on x86 (current total 2266; sole pre-existing failure the
documented ALSA `AudioManagerTest.PreloadCommonSounds`) plus ARM cross-compile.
If the cause admits a headless test, add one and **prove it fails in both
directions**; if it does not, say why.

Session discipline: binary freshness against HEAD on both machines; Pi `/tmp`
is a 242MB tmpfs — pull cores and captures off as you go, they are large;
**restore `core_pattern` when finished**; if a live session is needed take the
eqemu lock normally and tear down cleanly; kill clients by real binary PID;
leave the Pi pristine with the governor as found.

## Review

(executor fills: the verbatim backtrace; faulting thread and frame; crash rate
across runs; whether the `RegionVis` expansion is implicated; **whether this
and T110's teardown abort share a root cause**; whether T111's
`regionPvsDepthMap_` fix was complete; the fix and why it qualified as narrow,
or the diagnosis if it did not; consecutive clean runs observed; any test added
and its failability proof; deviations)

## Board

If this and T110 prove to be one defect, close T110 here and say so. If not,
record what distinguishes them so T110 starts from evidence rather than
assumption.
