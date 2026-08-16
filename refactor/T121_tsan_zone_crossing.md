# T121 — TSAN over the zone-crossing tour, with a real acceptance test

**Slot: whichever is free. x86 only. eqemu lock required. No Pi.**

**Territory: whatever TSAN implicates on the crossing/teardown path,
sanitizer build config, `tests/`, `refactor/`,
`/mnt/projects/fable/review/T121/`.**

Read T120's Review first (the 10-of-10 reproducer and the ASAN lead), then
T114's (the TSAN harness and the 16-item inventory).

## Why now, when T114 already ran TSAN

Two things changed.

**1. There is a reproducer.** T120's amplified zone-crossing tour crashes
**10 of 10** on x86, no sanitizer, minutes per run
(`review/T120/run_baseline_x86.sh`, `tour_zone_cross_x86_amplified.txt`).
Every prior unit on this arc worked without one.

**2. T114's TSAN never crossed a zone.** It ran full zone-ins, movement tours
and clean shutdowns — but always within one zone, unloading only at process
exit. **`ConstrainedTextureCache::clear()` and the whole `unloadZone()`
teardown cascade therefore never ran with the renderer live under TSAN.**
That is the same blind spot that made T115/T116/T118's ASAN runs clean, and
it is exactly where T120's five baseline crashes land.

So this is not a repeat of T114. It is T114's instrument pointed at the code
T114 never reached.

## Start from what ASAN already named

T120's ASAN run (`quarantine_size_mb=256`) implicated an **unguarded
`std::map` move-assignment in `EquipmentModelLoader::adoptIndex()` on the
LoadingThread**, with `initMutex_` covering only the lazy-init path and
leaving readers — including probable render-thread equipment lookups —
unsynchronised.

Check that first. Then widen: T120's five baseline crashes each free or walk
a **different** container inside `unloadZone()`'s teardown (`SMesh`'s
mesh-buffer array, `regionWorldTriangles_`/`objectWorldTriangles_`, a
`ZoneGeometry` destructor chain, a `Portal` vector). If those share a
missing-lock shape, TSAN on this path should say so.

**Also cross-reference T114's 11 open inventory items** (in `deferred.md`) —
say which of them sit on the crossing path, because those just became
reachable evidence rather than a backlog.

## The harness is already built — reuse it

`review/T114/run_tsan_x86.sh` works and encodes two obstacles T114 solved:
**`setarch "$(uname -m)" -R`** (TSAN is incompatible with ASLR) and
config-relative-path resolution. `history_size=7` and `halt_on_error=0` are
sensible; keep them.

Run it over **T120's crossing tour** with **T120's flags**
(`--constrained orangepi --renderer gles2 -r 800 600`, audio enabled). Do not
revert to T114's single-zone tour — the crossing is the entire point.

### Suppression discipline is non-negotiable

T114 held to **exactly one** suppression entry — `race:libgallium*.so*`,
third-party, with no willeq frame in either stack — and **no willeq symbol
suppressed anywhere**. That is the condition that separates a real sanitizer
run from theatre. **Hold the same line.** If you find yourself wanting to
suppress a willeq symbol, that is a finding to report, not a line to add.

## The acceptance test — this is the part that is new

**A clean TSAN run proves nothing on its own.** TSAN perturbs timing far more
than T119's diagnostic probe did, and that probe was already enough to turn a
10-of-10 crash into two clean runs and produce a false negative (see
`deferred.md`, T120 orchestrator review).

So every fix must be validated against the **uninstrumented** reproducer:

- Re-run `review/T120/run_baseline_x86.sh` — clean non-sanitizer build, same
  tour, same flags — **at least 10 times**, and report **N of 10** against
  T120's baseline of **0 of 10 clean**.
- That number, not the TSAN output, is the result. State it prominently.

If TSAN goes clean but the baseline still crashes 10 of 10, say so plainly —
that is a real and important result meaning the corruption is not (only) a
data race.

## Fix scope

Fix races **on the crossing/teardown path**, using the same additive
per-container-mutex pattern T91/T111/T113/T114 established. Prefer several
narrow, obviously-correct locks over one clever restructuring.

**STOP and report** if a fix requires changing object ownership, the
zone load/unload lifecycle, or GL teardown order — those are their own units,
and a rushed lifetime change turns a crash into silent corruption.

**Watch for self-deadlock.** T114 correctly declined to lock
`ensureModelDataLoaded()`'s reads because a naive lock would self-deadlock
through nested `loadedModelsMutex_` acquisition. **A deadlock introduced
while fixing a race is worse than the race.** If the safe fix is not
obvious, report it rather than forcing it.

**Do not paper over**: no lock added merely to quiet a report you have not
understood, no subsystem disabled, no suppression standing in for a fix.

## What to establish

- The races TSAN finds **on the crossing path**, each with the two stacks and
  the container involved.
- Whether `EquipmentModelLoader::adoptIndex()` is confirmed.
- Which of T114's 11 open items are on this path.
- **N of 10 uninstrumented baseline runs after fixes**, against 0 of 10.
- If races are fixed and the baseline still crashes, what that rules out.

## Verification

- x86 suite (current total **2272**; sole failure the documented ALSA
  `AudioManagerTest.PreloadCommonSounds`) plus ARM cross-compile.
- **Tests for any fix.** This bug class has survived six units; assert on the
  real call path, not a fake.
- **Visual gate, goldens must not move.** If one moves, STOP — do not
  rebless.
- **No Pi run.** The reproducer is on x86 now; a Pi session would cost hours
  and add nothing this unit needs. If you think you need it, STOP and say why.

## Review

(executor fills: the TSAN reports on the crossing path, verbatim, with both
stacks; the suppression file contents and the justification for every entry;
whether `adoptIndex()` was confirmed; which T114 items are on this path; each
fix and why it is narrow; any race deliberately not fixed and the reasoning;
**N of 10 uninstrumented baseline runs**; deviations)

## Board

Lead with the uninstrumented number. If the baseline is now clean, say what
that means for the Pi arc and what a Pi re-measurement would be expected to
show — but do **not** claim the Pi is fixed without measuring it; that is the
next unit. If the baseline still crashes, say what TSAN ruled out, and what
instrument is left.
