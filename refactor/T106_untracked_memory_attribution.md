# T106 — What is the untracked third of RSS?

**Slot: whichever is free. Live measurement — eqemu lock normally.**

**Territory: measurement and analysis only, `refactor/`,
`/mnt/projects/fable/review/T106/`. PRODUCTION BEHAVIOUR IS READ-ONLY.
Do not add memory counters — see "Do not instrument yet".**

Read T105's Review first, then T80's (the memory-gate categories) and T69's
(which first flagged untracked-RSS gating as deferred).

## The problem

`untrackedBytes` is computed as `processRssBytes - totalEstimate`
(`renderer_diagnostics.cpp:809`). It is a **residual, not a measurement** — it
proves a gap exists and says nothing about what is in it.

T105 measured that gap at **~34-36% of process RSS**, consistently across
Befallen, Nektulos and Qeynos2, matching T69's original ~100MB-of-273MB
qeynos2 finding. Nineteen counters already exist
(`characterModelsBytes`, `gpuTextureBytes`, `textureCacheBytes`,
`placeableObjectsBytes`, `hcmapBytes`, `entityInstanceBytes`,
`equipmentModelsBytes`, audio and framebuffer categories, and more) and a
third of memory still escapes them.

**This blocks the memory gate from becoming a runtime control surface.** A
budget cannot govern what it cannot see, and Will's load-gate design depends
on those budgets meaning something.

## The hypothesis to test first, because it could make this moot

**Much of the untracked third may be llvmpipe**, Mesa's software rasterizer,
which this dev VM uses in place of a GPU. It allocates substantially for
framebuffers and tile data.

**That would not exist on the Orange Pi**, which has a real Mali400 and the
native GLES2 driver.

So: if the untracked share is largely llvmpipe, chasing it is wasted effort
for the actual target, and the honest answer is "the gap is a dev-environment
artifact, the Pi's real untracked share is X." **Determine this before
attributing anything else.** It is the difference between a real problem and a
measurement artifact.

## Method

### Part 1 — attribute the file-backed share from `smaps`

`/proc/<pid>/smaps` gives per-mapping `Rss` with the backing path. That
attributes most of RSS mechanically:

- the `willeq` binary's own text and data (note it is ~340MB unstripped —
  establish how much is actually resident, which is a different number)
- each shared library, **with Mesa/llvmpipe/LLVM identified separately**
- the EQE1 mappings (T105 already measured these at 1.6-10.6% resident —
  reuse, do not re-derive)
- stacks, and anonymous regions

T105 already has working `smaps` tooling in `/mnt/projects/fable/review/T105/`.
**Reuse it.**

### Part 2 — the anonymous remainder

Whatever is anonymous/heap needs different treatment. Options in rough order
of cost: `malloc_info`/`mallinfo2` for allocator-level totals and arena
overhead; a heap profiler (massif or equivalent) on a headless run; or
`LD_PRELOAD` counting. **Pick one, say why, and state its limitations** — heap
profilers distort timing and may not survive the render thread.

Distinguish **allocator overhead and fragmentation** from genuinely-held
allocations. A third of RSS being malloc arena slack is a very different
finding from a third being live objects nobody counts.

### Part 3 — the verdict

For each attributed chunk, state:

- how many bytes
- what owns it
- **whether it exists on the Pi** (llvmpipe: no; Irrlicht scene graph: yes;
  binary text: yes but smaller for a stripped ARM build)
- whether it is worth a counter

## Do not instrument yet

**Do not add memory counters in this unit.** Adding a counter for something
that turns out to be llvmpipe, or allocator slack, would bake dev-environment
noise into the gate. Identify first; counters follow from the answer and are a
separate unit.

The one exception: if a chunk is both large and trivially attributable to an
existing subsystem that simply lacks a counter, **say so and recommend it** —
do not add it.

## A known obstacle

T105 found that **`/pmem`'s real RSS and untracked fields are unreachable in a
live client** — the code path that populates them has zero callers (recorded
in `deferred.md`). T105 worked around this via the standalone
`gles2_zone_benchmark`. Either use the same workaround or fix the reachability
if it is genuinely small — but if you fix it, say so, because that is a
production change and this unit is otherwise read-only.

## Live measurement

Measure a real populated zone-in, not just the benchmark — the benchmark's
synthetic population may not exercise the same allocations. T105 used
Befallen, Nektulos and Qeynos2 with real spawns; do at least one of those for
comparability, and reuse its numbers rather than re-measuring what it already
established.

Session discipline: binary freshness against HEAD first; force daylight
`/say #set time 11 0`; pad scripts long; `-d 4` plus `--log-level=DEBUG`;
restore DB state and confirm by `SELECT` **after logout**; kill clients by real
binary PID, not `nohup`'s `$!`; clean teardown, delete the lock.

## What you may not do

- **Do not add memory counters.**
- Do not change allocation, caching or loading behaviour.
- Do not attempt Pi measurements — reason about what would differ and say so.
- Do not report a residual as an attribution. "Unaccounted" is what we already
  have; the deliverable is names and byte counts.

## Verification

Full build and suite (current total 2247; sole pre-existing failure the
documented ALSA `AudioManagerTest.PreloadCommonSounds`). Visual gate — goldens
must not move, since behaviour is unchanged.

## Review

(executor fills: the llvmpipe verdict and its share; the full `smaps`
attribution table with per-mapping RSS; how the anonymous remainder was
attributed and that method's limitations; allocator overhead versus live
allocations; **per chunk, whether it exists on the Pi**; which chunks warrant
counters and which do not; what remains genuinely unattributable and why;
deviations)

## Board

Record the attribution. Whether the memory-gate budgets can serve as a runtime
control surface for Will's load gate depends on this answer.
