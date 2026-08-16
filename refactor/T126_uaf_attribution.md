# T126 — Make attribution reliable, then name the dominant holder

**Slot: whichever is free. x86 only. eqemu lock required. No Pi.**

**Territory: `docker/irrlicht-drm/` (probe + driver call site), `scripts/`
(extraction/symbolisation), whatever the evidence implicates, `tests/`,
`refactor/`, `/mnt/projects/fable/review/T126/`.**

Read T124's Review and the T124 orchestrator-review entry in `deferred.md`
first (the flat rate, and the failed attribution attempt), then T123's (the
technique that worked once).

## The problem this unit solves

**The UAF rate has not moved**: 1.317% before any holder fix (T122, 81/6149),
1.334% after three units of them (T124, 22/1649). The guards closed real,
proven defects that together contribute a negligible share. **The dominant
holder is unidentified and is not among the seven enumerated.**

**Enumerating suspects is exhausted. Attribution is the bottleneck**, and
T124 showed the current technique is not reliable: one resolved `material_ptr`
did not contain the evicted `tex_ptr` nearby, another decoded to what looked
like a texture-name string buffer. T123's clean
`irrAllocator<SMaterial>`-vtable match did not reproduce.

**Why it is unreliable is the important part**: the core is taken at *crash*
time, potentially long after the offending write, so memory around
`material_ptr` may already have been reused. **Post-mortem structural
inference is fundamentally fragile here.** Do not just re-run the gdb recipe
and hope.

## Record the answer at write time instead of reconstructing it later

**Preferred: capture a short return-address chain at the write.**
`__builtin_return_address(0..3)` (with `__builtin_extract_return_addr`) is a
handful of loads, no allocation, no locks — fully compatible with the probe's
existing constraints. Store the addresses raw in the ring entry and
**symbolise offline** with `addr2line`/`llvm-symbolizer` against the unstripped
binary.

That names the **call path** into `cacheTexParams()` — which scene node or
subsystem was drawing — directly, without inferring anything from heap
structure. It is invariant to later memory reuse, which is exactly what broke
T124's approach.

Watch for inlining flattening the chain: the probe build already uses `-g`, so
build it `-fno-omit-frame-pointer` and, if frames are still missing, note it
rather than guessing at what the gap was.

**Fallback if return addresses prove unusable: a draw-phase breadcrumb.** A
thread-local `const char*` set at the top of each subsystem's draw
(zone regions, entities, doors, objects, boids, particles, sky, UI) and copied
into the ring entry — one store, no allocation. Coarser than a call path but
it names the holder category unambiguously. Use this only if the return-address
route fails, and say why.

## Guard against the trap T124 hit

T124's first probe design **wrapped the ring 3-4x per run**, corrupting the
CREATE/DESTROY history and producing spurious results. It caught this itself
and gated the records — but verify explicitly this time:

**Report whether the ring wrapped.** If it did, any lifecycle conclusion drawn
from it is unsound. Size the ring for the run, or record a wrap counter and
state it. This is a correctness precondition, not housekeeping.

## Re-run the acceptance gate

The probe must not perturb the defect. **Run the extended probe build over
T120's tour 10 times and report N of 10 crashed** against the standing
10-of-10 (T122's gate). If it drops materially, the instrument is changing what
it measures and its output cannot be trusted — that check is why T122's finding
stands and T119's did not.

## What to establish

- **The holder, named, with the evidence that names it** — a symbolised call
  path or an unambiguous breadcrumb, for one or more of the remaining UAF
  writes.
- Whether the remaining writes come from **one** dominant holder or several.
  The flat rate suggests one, but say what the data shows.
- Whether any of them originate in the **three guards added by T123/T124** —
  an ordering or registration bug there is a live possibility and would be
  embarrassing to miss. T124 flagged it as a candidate.
- The other candidates T124 named but did not investigate: `RaceModelLoader`
  equipment/mesh texture caches, and `AnimatedTextureManager` /
  `DetailManager` / `StormCloudLayer` (self-registered listeners, believed
  covered but not re-audited).

## Fixing

**Naming the holder is this unit's deliverable.** If the fix is then obvious
and narrow — another guard, a registration ordering correction — make it, and
report the UAF **rate** (not the raw count) before and after, with the total
write count alongside so it can be normalised.

**STOP and report** if the fix requires changing texture ownership or
refcounting, the zone load/unload lifecycle, or GL teardown order.

**Do not paper over**: no liveness guard inside `cacheTexParams()`.

**If attribution succeeds but the fix is larger than this unit, that is a
success** — a named holder with evidence is exactly what has been missing.

## Verification

- x86 suite (current total **2281**; sole failure the documented ALSA
  `AudioManagerTest.PreloadCommonSounds`) plus ARM cross-compile. The probe
  header is in **both** `scripts/build-gles2-x86.sh` and
  `docker/Dockerfile.arm-noble` copy-lists — keep both working.
- **Report rates, never raw counts.** UAF writes over total `TEXPARAMS`
  writes, with both numbers stated. T124's raw "81 → 22" looked like a 73%
  reduction and was zero change; do not repeat that.
- **Visual gate goldens must not move.** If one does, STOP and report.
- **No Pi run.** T125 (Pi re-measurement) is scoped and waiting on a result
  that plausibly moves the number.

## Review

(executor fills: the attribution mechanism and why it is reliable where
T124's was not; whether the ring wrapped; the acceptance-gate N of 10; the
**named holder with its symbolised evidence**; whether one holder or several;
whether any writes originate in T123/T124's own guards; the fix and the
before/after **rate**, or the diagnosis; deviations)

## Board

Name the holder. If fixed, give the UAF rate before and after with total write
counts, and say whether T125's Pi re-measurement is now worth dispatching. If
not fixed, say precisely what the evidence shows and what the fix would
require.
