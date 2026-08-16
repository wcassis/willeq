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

### Headline: probe-build N of 10 = **10 of 10 crashed**

Ran the probe-enabled build (`scripts/build-gles2-x86.sh --t122-texprobe`)
over T120's exact recipe (`review/T120/tour_zone_cross_x86_amplified.txt`,
same flags) **10 times**, fresh eqemu (`befallen` + `qeynos2` zone
processes, matching T82's precedent for a second zone beyond `up.sh`'s
default). All 10 crashed:

| run | signal | exit | crash signature |
|---|---|---|---|
| 1 | SIGSEGV | — (killed, log truncated Phase 9/15) | (no bt pulled, core kept) |
| 2 | SIGABRT | 134 | `malloc_printerr("double free or corruption (out)")` |
| 3 | SIGSEGV | 139 | `std::_Rb_tree<...triangle3d...>::_S_right()` |
| 4 | SIGSEGV | 139 | (core kept, bt not pulled — pattern already established) |
| 5 | SIGABRT | 134 | (core kept, bt not pulled) |
| 6 | SIGABRT | 134 | (core kept, bt not pulled) |
| 7 | SIGSEGV | 139 | (core kept, bt not pulled) |
| 8 | SIGSEGV | 139 | (core kept, bt not pulled) |
| 9 | SIGSEGV | 139 | (core kept, bt not pulled) |
| 10 | SIGSEGV | 139 | (core kept, bt not pulled) |

**T120's uninstrumented baseline was also 10 of 10.** The probe build's rate
matches it exactly — the probe is not perturbing the defect. This is the
opposite outcome from T119's mutex-taking diagnostic (10/10 → 2 clean); the
acceptance gate is satisfied and everything below is licensed for
interpretation, per the plan's own gate.

All 10 crashes reproduce the exact SIGSEGV/`std::_Rb_tree`/`std::map<size_t,
vector<triangle3d>>` and SIGABRT/`double free or corruption (out)` families
T117/T119/T120 already catalogued — this run adds no new crash shape, it
adds an explanation for the existing ones.

### Probe design (lock/allocation/IO-free)

`docker/irrlicht-drm/t122_texprobe.h` (new file, ~100 lines):

- A fixed `Entry g_t122Ring[1<<18]` (262,144 entries, ~6.3MB) in BSS —
  zero-initialized by the loader, no constructor, no dynamic allocation ever.
- A single `std::atomic<uint32_t> g_t122RingIndex` claims a slot via
  `fetch_add(1, memory_order_relaxed)` — no mutex, no CAS loop, no ordering
  requirement beyond "this entry is internally well-formed and its `seq`
  recovers relative order."
- `Entry` is POD: `const void* ptr; uint32_t seq, tid, value; uint8_t kind,
  pad[3];` — 24 bytes on x86_64. No strings, no containers, no formatting.
- Thread id via `pthread_self()` (a plain TLS read on glibc/NPTL, not a
  syscall) truncated to 32 bits — recorded for offline analysis only, never
  used for synchronization.
- No logging, no file I/O, no signal handler. The ring is **write-only**
  from the process's point of view; nothing is ever read back at runtime.
- Compile-time gated on `WILLEQ_T122_TEXPROBE`. When undefined,
  `T122_RECORD_CREATE/DESTROY/TEXPARAMS(...)` expand to `((void)0)` — zero
  codegen, verified below.

Three call sites, all load-bearing per the plan's explicit "record CREATE
too" instruction:

- `COGLES2Texture::COGLES2Texture(...)` (all three constructors,
  `COGLES2Texture.cpp`) — `T122_RECORD_CREATE(this, TextureName)` after the
  GL texture name is known (post-`uploadImage()`/`createRenderTarget()` for
  the two constructors that upload; immediately for the wrap-existing-name
  constructor).
- `COGLES2Texture::~COGLES2Texture()` (`COGLES2Texture.cpp`) —
  `T122_RECORD_DESTROY(this, TextureName)` as the **first** statement,
  before any teardown, so the entry reflects the object's state at the exact
  moment it stops being live.
- `COGLES2Texture::cacheTexParams(...)` (`COGLES2Texture.h:79`, inline) —
  `T122_RECORD_TEXPARAMS(this, magFilter)` as the **first** statement,
  before the mutable-field writes, so a crash mid-write still leaves the
  entry recorded. `magFilter` is recorded because it is the exact literal
  T118 identified (`0x2601` = `GL_LINEAR`).

### Gating flag and build-script plumbing

`-DWILLEQ_T122_TEXPROBE` — off by default. `scripts/build-gles2-x86.sh` gets
a new `--t122-texprobe` flag that passes `CXXFLAGS="-g -DWILLEQ_T122_TEXPROBE"`
into the `make -C .../source/Irrlicht staticlib` invocation (GNU Make's `+=`
in the Makefile appends onto a command-line-set `CXXFLAGS`, so this doesn't
disturb `NDEBUG=1`'s `-O3`; `-g` is added only so a resulting core can be
symbolized, it does not change codegen). A plain
`scripts/build-gles2-x86.sh` (no flag) passes `CXXFLAGS=""`, which is a
no-op vs. the pre-T122 behavior.

**Verified zero footprint when disabled**: built once with the flag, once
without, from the identical source tree:

```
$ nm build-gles2/bin/willeq | grep -i t122   # --t122-texprobe build
0000000001541740 B _ZN3irr5video4t12210g_t122RingE
0000000000cd26e0 W _ZN3irr5video4t12210t122RecordENS1_9EventKindEPKvj
0000000001b41740 B _ZN3irr5video4t12215g_t122RingIndexE
...

$ nm build-gles2/bin/willeq | grep -i t122   # plain build, same source
(no output, grep exit 1)
```

### Build footgun (plan's warning, hit for real)

Hit the plan's named footgun in a new place: `docker/Dockerfile.arm-noble`
has its **own** independent copy-list (`COPY docker/irrlicht-drm/*.h
/tmp/irrlicht-drm/...` + matching `cp` lines inside the `RUN` block) that
`scripts/build-gles2-x86.sh`'s copy step does not share. Adding
`t122_texprobe.h` only to the x86 script's copy list left the ARM
cross-compile failing at `COGLES2Texture.h:12:10: fatal error:
t122_texprobe.h: No such file or directory` — even with the probe
*disabled*, because `#include "t122_texprobe.h"` is unconditional (only the
macro bodies are gated, per the header's own design note, so the file must
exist either way). Fixed by adding the same two lines
(`COPY`+`cp`) the Dockerfile already had for `gpu_sync_probe.h` right next
to it. Re-ran the full ARM cross-compile after the fix — clean.

### Ring extraction (`scripts/t122_extract_ring.py`)

Entirely offline, per the plan's constraint: opens the probe-build binary
and a core file on disk via `gdb -batch -core-file` (no live process
attach, no runtime dump). Resolves `irr::video::t122::g_t122Ring` and
`g_t122RingIndex`'s addresses from the ELF symbol table (gdb handles PIE
load-bias itself), `dump binary memory`s the raw bytes, and parses them in
pure Python against the `Entry` layout — no DWARF type info is needed since
the layout is documented in the script and asserted (`ENTRY_SIZE_64 == 24`).
Outputs a TSV: `seq  kind  ptr  tid  value`, one row per surviving ring
entry, `seq`-ordered (not ring-slot order, which wraps).

**Core capture**: this host's default `core_pattern` pipes to `apport`,
which (as T120 already found) produced no usable core for a plain segfault
in this environment. Repeated T120's sanctioned workaround: `sudo`
temporarily changed `/proc/sys/kernel/core_pattern` to `core.%e.%p`, exact
prior value captured first (`|/usr/share/apport/apport -p%p -s%s -c%c -d%d
-P%P -u%u -g%g -F%F -- %E`), **restored** at session end and verified via
`cat`.

`scripts/t122_classify_ring.py` (new) answers the plan's three questions
programmatically: for every `TEXPARAMS` entry, walks the pointer's
CREATE/DESTROY history and classifies it `LEGIT` / `UAF` /
`TYPE_CONFUSE_NEVER` / `TYPE_CONFUSE_LATER`.

### Findings: the three questions, answered

Extracted and classified all 10 cores (`review/T122/probe_gate/run{1..10}_ring.tsv`,
`_ring.tsv` files are the raw extraction, classification re-run via
`t122_classify_ring.py`):

| run | TEXPARAMS total | LEGIT | UAF | TYPE_CONFUSE |
|---|---|---|---|---|
| 1 | 666 | 650 | 16 | 0 |
| 2 | 547 | 543 | 4 | 0 |
| 3 | 536 | 528 | 8 | 0 |
| 4 | 580 | 573 | 7 | 0 |
| 5 | 600 | 592 | 8 | 0 |
| 6 | 581 | 574 | 7 | 0 |
| 7 | 537 | 530 | 7 | 0 |
| 8 | 928 | 915 | 13 | 0 |
| 9 | 597 | 593 | 4 | 0 |
| 10 | 577 | 570 | 7 | 0 |
| **total** | **6149** | **6068** | **81** | **0** |

**Question 2 (type confusion) — answered NO, zero occurrences across all 10
runs and 6149 writes.** Every single `TEXPARAMS` pointer, in every run, has
a `CREATE` entry earlier in the ring. `material.getTexture(0)` is never
handing `applyMaterialRenderState()` a pointer that was never a
`COGLES2Texture` in the first place. This hypothesis, which the plan
explicitly said "fits the evidence as well as use-after-free and no unit
has tested it," is now tested and eliminated.

**Question 1 (use-after-free) — answered YES, confirmed, 81 of 6149 writes
(~1.3%) across all 10 runs, every single run showing at least 4.** Worked
example, `review/T122/probe_gate/run2_ring.tsv`, pointer `0x76ed52c9f3e0`:

```
seq   kind       ptr             tid          value
329   CREATE     0x76ed52c9f3e0  0x725fd6c0   0xeb   (LoadingThread: constructed, GL name 0xeb)
580   TEXPARAMS  0x76ed52c9f3e0  0xb9517040   0x2601 (render thread: legitimate write, texture live)
955   DESTROY    0x76ed52c9f3e0  0x71dfc6c0   0xeb   (a LATER LoadingThread: destructed)
2444  TEXPARAMS  0x76ed52c9f3e0  0xb9517040   0x2601 (render thread: UAF -- no CREATE between 955 and 2444)
```

The render thread (`tid=0xb9517040`, constant across every `TEXPARAMS` in
this run) calls `cacheTexParams()` on this pointer **twice**: once
legitimately at seq 580 while the texture is alive, and once at seq 2444 —
1489 ring entries and (per the run's timeline) a full zone-crossing later —
**after** a different thread (`tid=0x71dfc6c0`, a subsequent zone's
`LoadingThread`) already destroyed it at seq 955, with **no intervening
CREATE at that address**. That is a textbook use-after-free through
`cacheTexParams()`'s `const`-but-`mutable`-writing design, and it is
single-threaded-at-the-write (the write itself is not concurrent with
anything — it happens strictly after the DESTROY completed on another
thread, at a later point in wall-clock/ring-seq time) — consistent with, not
contradicting, T121's TSAN-clean finding. **This is orchestration/sequencing
corruption, not a race**, exactly as T121 characterized the adjacent
`adoptIndex()` finding.

Every one of the 10 runs' UAF entries writes the identical value, `0x2601`
(`GL_LINEAR`) — matching T118's original candidate exactly. The UAF entries
are always clustered at the very tail of the ring (the last 4-16 entries
before the crash), i.e. they are the terminal event of each run, not
scattered noise.

**Direct register-level corroboration** (not required by the plan, but
available and conclusive): pulled `gdb` backtraces from two cores.

- `run3_core.willeq` (SIGSEGV): crashes in
  `std::_Rb_tree<...>::_S_right()`, called from
  `std::map<size_t, vector<triangle3d>>::clear()`, called from
  `IrrlichtRenderer::unloadZone()` — the exact container T120's baseline
  already named (`regionWorldTriangles_`/`objectWorldTriangles_`).
  `info registers` at the fault: **`rax = 0x260100002601`** — literally two
  copies of `0x2601` packed back-to-back (`CachedMagFilter` immediately
  followed by `CachedMinFilter`, both `GLint`, both written `0x2601` by the
  same `cacheTexParams()` call), being dereferenced as a red-black tree node
  pointer.
- `run2_core.willeq` (SIGABRT): glibc's `malloc_printerr` reports `"double
  free or corruption (out)"`, `_int_free_merge_chunk(p=0x76ed52c9f450,
  size=41785736832512)` — `size` in hex is `0x260100002600`, the same bit
  pattern, corrupting a heap chunk header 0x70 bytes from the confirmed UAF
  pointer `0x76ed52c9f3e0` above (same run, same neighborhood — consistent
  with the freed `COGLES2Texture`'s memory having been coalesced with or
  sitting adjacent to the chunk later reported corrupted).

Two independent, previously-unexplained crash signatures from this same
arc, explained by the same root cause, in the same run.

**Question 3 (exoneration)** does not apply globally — the write is
confirmed, not exonerated — but is worth stating precisely: **6068 of 6149
writes (98.7%) ARE legitimate.** `cacheTexParams()` itself is correct code
operating on a correct contract (write through the pointer you were given);
the defect is entirely in what hands it a dangling pointer, not in the
function.

### `cacheTexParams()`: CONFIRMED, not exonerated

Answers the plan's Board question directly: **T117's signature #1**
(`ZoneMergeBuffers::ranges_`/mid-tour SIGSEGV, `__x=0x2601`, the arc's
original and most-recurring signature) **is explained** — this unit's
`run3` backtrace lands in the same `std::map<size_t,vector<triangle3d>>`
family T120's baseline crashes already implicated, now with the corrupting
write traced to its source. **T119's "fifth distinct sub-signature"**
(`double free or corruption (out)`) **is explained** — `run2`'s backtrace,
above. The `SkyData`/"corrupted size vs. prev_size" and Mesa/Lima-teardown
SIGBUS signatures were not specifically re-encountered this unit (no crash
this run happened to hit those exact code paths), so they remain open,
undecided by this evidence rather than ruled out.

### What this does NOT close, and why no fix was attempted

The write site (`cacheTexParams()`) is confirmed, but the **owner of the
dangling pointer is not identified**. Something still holds (and later
renders with) a `SMaterial` whose texture pointer refers to a
`COGLES2Texture` that a zone-unload sequence already destroyed — the same
territory T119 partially closed (`ConstrainedTextureCache::clear()` missing
`clearTextureReferences()`) and explicitly flagged as *not* closing the arc
(T119's Pi re-measurement: 0 of 5 clean after that fix). This unit's finding
is consistent with T119's own suspicion — `SkyRenderer`'s dome/sun/moon/glow
nodes, disabled (`setVisible(false)`) rather than removed during
`unloadZone()` — but **that is a hypothesis, not verified against this
run's specific pointers** (the ring records the texture object's own
lifecycle, not which scene node/material held the reference that outlived
it; that would need a second instrument, e.g. recording
`SMaterial::setTexture()` call sites, which is a new unit's scope).

Per the plan's explicit STOP condition — **"STOP and report if the fix
requires changing texture ownership, refcounting, the zone load/unload
lifecycle, or GL teardown order"** — finding and clearing the specific
leftover owner is squarely that territory: it requires auditing every scene
node type's `unloadZone()` teardown for completeness (the same audit shape
T119 did once for `ConstrainedTextureCache`, now needed for whatever else
still points at texture objects post-teardown), and a wrong guess risks
turning a diagnosable crash into a silent one. **No fix was attempted in
this unit.** Per the plan's own permission ("Fix only if genuinely narrow
and obviously safe... otherwise STOP and report"), diagnosis is the
deliverable.

No liveness guard was added inside `cacheTexParams()` either, per the plan's
explicit prohibition on papering over.

### Should the probe stay in tree?

**Yes, default-off.** It is:
- Proven not to perturb the defect (10/10 = 10/10, unlike T119's probe).
- Proven zero-cost when disabled (`nm` diff above — no symbols at all).
- Directly reusable by whatever unit next hunts the leftover-reference
  owner: the same ring, re-armed, immediately shows which
  `COGLES2Texture*` values go dangling-then-rewritten, which narrows "where
  is this material coming from" without needing a new instrument.
- Cheap to keep: one new header, three call sites, a `--t122-texprobe`
  build flag, two extraction scripts. No production code path changed
  behaviorally with it off.

### Deviations

1. Only 3 of 10 crashes got a full `gdb` backtrace pulled (runs 2, 3) plus
   the ring analysis for all 10 — judged sufficient once the pattern (UAF in
   every run, 0 type confusion in every run, identical `0x2601` value, two
   independently-corroborating backtraces) was established from the first
   two; matches T120's own precedent of not backtracing all 10 baseline
   crashes.
2. `run10_ring.tsv`'s extraction (`gdb dump binary memory` against a
   ~1.4GB core) ran long enough to need a background/poll cycle rather than
   completing within a single foreground call — no correctness impact, just
   slower than the other 9 (all under ~5s).
3. The ARM `Dockerfile.arm-noble` footgun (separate copy-list from the x86
   script) was not named in the plan and was discovered, not anticipated —
   documented above, fixed, and the full ARM cross-compile re-run clean
   after the fix.
4. `sudo` used once to retarget `core_pattern` (T120's precedent exactly),
   restored and verified before session end.
5. Testchar's position drifted during the tour runs (server-side position
   updates from the scripted `/move` commands, zone `qeynos2`/`befallen`,
   never reaching the tour's terminal `/camp`+restore because every run
   crashed first) — restored via direct SQL
   (`UPDATE character_data SET zone_id=36, x=-77.9232, y=21.9086, z=3,
   heading=190 WHERE name='Testchar'`) rather than a live GM command, since
   no run survived to `/camp`. Verified via `SELECT` before bringing the DB
   container back down.
6. No Pi run, per the plan.

## Board

**`cacheTexParams()` is CONFIRMED as the corrupting write, not exonerated.**
It explains T117's signature #1 (the original `ranges_`/`0x2601` family,
now traced end-to-end from write to crash) and T119's "double free or
corruption (out)" fifth signature. It does NOT explain (not ruled out,
simply not re-encountered this unit) the `SkyData` teardown signature or
the Mesa/Lima SIGBUS signature — a future unit chasing those should not
assume they're the same root cause without separate evidence.

**Type confusion (question 2) is eliminated** — 0 of 6149 writes across 10
runs. Any future hypothesis on this arc can drop that branch.

**What's left, and where the next unit should point**: the write is
confirmed; the *owner of the dangling pointer* is not. The next unit's
job is a completeness audit of `unloadZone()`'s scene-node teardown (in
`SkyRenderer`'s spirit, per T119) — find whatever else still holds an
`SMaterial` pointing at a texture that teardown already destroyed, using
this unit's ring (still in tree, default-off, re-armable) as the
diagnostic. That unit inherits a hard STOP-condition warning: this is zone
load/unload lifecycle territory, exactly what T118/T119/T120/T121/T122 all
independently declined to touch without a dedicated unit.
