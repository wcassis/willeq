# T122 — A lock-free texture-lifecycle probe, resolved offline

**Slot: whichever is free. x86 only. eqemu lock required. No Pi.**

**Territory: `docker/irrlicht-drm/` (`COpenGLES2Driver.cpp`, `COGLES2Texture.h`),
a new probe header, `scripts/`, `refactor/`,
`/mnt/projects/fable/review/T122/`.**

Read T121's Review first (TSAN clean, and why `adoptIndex()` is a detector not
a cause), then T118's (`GL_LINEAR`) and the T120 orchestrator-review entry in
`deferred.md` (how a probe already faked a result on this arc).

## What is left after elimination

- **Not a data race** — T121's TSAN, run live over the crossing/teardown path
  for the first time, found zero races there.
- **Not a classic OOB or use-after-free** — ASAN with a real quarantine and
  zone crossings (T120) only ever catches it at **free** time, never at the
  write.

What fits all three negatives: **a write of a plausible value through a wrong
pointer into a live allocation.** No race for TSAN to see; a valid address and
a valid-looking value for ASAN to wave through; detectable only when the
victim is later freed or walked.

The only code that writes the observed value (`0x2601` = `GL_LINEAR`) is
`COpenGLES2Driver.cpp:~1355-1375`, which calls
`tex->cacheTexParams(...)` — a `const` method writing into `mutable` fields
(`COGLES2Texture.h:79`).

**Generic sanitizers are exhausted. This unit builds a targeted instrument.**

## The instrument

A **lock-free, allocation-free, I/O-free** ring buffer recording three event
kinds, so the texture lifecycle can be reconstructed after a crash:

1. **CREATE** — a `COGLES2Texture` is constructed (record `this`, and the GL
   texture name if cheaply available).
2. **DESTROY** — a `COGLES2Texture` is destructed (record `this`).
3. **TEXPARAMS** — `cacheTexParams()` is entered (record the `tex` pointer and
   the value being written).

Requirements, all of them load-bearing:

- **Fixed-size static array** in BSS, power-of-two capacity, index via a
  single `std::atomic<uint32_t>` `fetch_add(..., memory_order_relaxed)` and a
  mask. **No locks, no allocation, no logging, no syscalls on the hot path.**
- **POD entries only.** No strings, no containers. A pointer, a sequence
  number, a small enum, a thread id, a value.
- **Compile-time gated and OFF by default** (e.g. `-DWILLEQ_T122_TEXPROBE`).
  A normal build must contain none of it.
- **Resolved entirely offline.** Do not write the ring to disk at runtime and
  do not dump it from a signal handler. Capture a core and extract the ring
  post-mortem with a gdb/python script, which you check in. If cores prove
  unwieldy, an `MAP_SHARED` mmap-backed ring is an acceptable fallback —
  still no locks and no formatting on the hot path — but say which you used
  and why.

## The questions it answers

For each `TEXPARAMS` entry, resolve against the CREATE/DESTROY history for
that same pointer:

1. **Was the pointer destroyed before this write?** (with no intervening
   CREATE at the same address) → use-after-free through `cacheTexParams()`,
   which is T118's candidate, confirmed.
2. **Was the pointer never created as a texture at all?** → type confusion —
   `material.getTexture(0)` handing back something that was never a
   `COGLES2Texture`. **Do not skip this case.** It fits the evidence as well
   as the first and no unit has tested it.
3. If neither, the write is legitimate and `cacheTexParams()` is exonerated
   — which is itself a real result and redirects the arc.

Recording CREATE is what makes 1 and 2 separable from address reuse. Without
it you cannot distinguish "written after destruction" from "written to a new
object at a recycled address."

## The acceptance gate — do this BEFORE trusting any negative

**A probe that suppresses the crash is worthless, and that has already
happened on this arc.** T119's diagnostic took `mutex_` and linear-scanned the
texture cache on the renderer's hottest path; it turned T120's 10-of-10 crash
into two clean runs and produced a false negative that survived into a merged
conclusion.

So, before drawing any conclusion from probe data:

- Run the **probe-enabled** build over T120's tour
  (`review/T120/run_baseline_x86.sh`, `tour_zone_cross_x86_amplified.txt`)
  **at least 10 times** and report **N of 10 crashed**.
- T120's uninstrumented baseline is **10 of 10 crashed**. If the probe build
  drops materially below that, **the probe is perturbing the defect and its
  negatives cannot be trusted.** Say so, and make it lighter before
  continuing.

State this number before any finding. It is the licence to interpret
everything else.

## Build footgun — you will hit this

`docker/irrlicht-drm/` is the **shared** source. `scripts/build-gles2-x86.sh`
**copies** `COpenGLES2Driver.{h,cpp}` and the `COGLES2*` files into
`third_party/irrlicht-gles2-x86/` (gitignored) at build time.

**Editing `docker/irrlicht-drm/` does nothing until you re-run that script**
(`--irrlicht-only` suffices for the Irrlicht half). If your probe appears to
record nothing, check this first — do not conclude the code path is unreached.

## If it names the write

Fix only if genuinely narrow and obviously safe. **STOP and report** if the
fix requires changing texture ownership, refcounting, the zone load/unload
lifecycle, or GL teardown order.

Validate any fix against the **uninstrumented** baseline: 10 runs, **N of 10**
against T120's 0-of-10 clean. The probe build's number does not count.

**Do not paper over**: no null/liveness guard added inside `cacheTexParams()`
to skip the bad write without explaining it. That converts a diagnosable crash
into silent wrongness.

## What to establish regardless of outcome

- The probe-build crash rate, N of 10 (the acceptance gate).
- Which of the three questions above the data answers, with the specific
  entries quoted.
- If `cacheTexParams()` is exonerated, say so plainly and say what the ring
  shows instead — the texture lifecycle timeline across a crossing is useful
  to the next unit either way.

## Verification

- x86 suite (current total **2272**; sole failure the documented ALSA
  `AudioManagerTest.PreloadCommonSounds`) with the probe **disabled**, proving
  a normal build is unaffected.
- ARM cross-compile — the probe lives in shared driver source, so it must not
  break the ARM build even when disabled.
- **Visual gate goldens must not move.** If one does, STOP; do not rebless.
- **No Pi run.**

## Review

(executor fills: the probe design and why it is lock/allocation/IO-free; the
gating flag; how the ring was extracted, with the script; **probe-build N of
10**; the CREATE/DESTROY/TEXPARAMS findings with entries quoted; which of the
three questions is answered; the fix and its uninstrumented N of 10, or the
diagnosis; whether the probe stays in tree, default-off, and why; deviations)

## Board

If `cacheTexParams()` is confirmed as the write, say which of T117's five
signatures that explains. If it is exonerated, say what the arc's remaining
hypotheses are — the elimination is worth as much, provided it is real.
