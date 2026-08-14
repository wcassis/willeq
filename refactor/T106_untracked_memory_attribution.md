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

Executed in `/mnt/projects/fable/wt/slot1`, task branch
`task/T106-untracked-memory`, base fable tip `df63f315`. Live eqemu
measurement performed (lock free at session start; taken, used, released
cleanly; Testchar restored to its documented resting position and confirmed
by `SELECT` after logout). **Read-only: no counters added, no allocation/
caching/loading behaviour changed** — `git status --short` at completion
shows only new files under `benchmarks/audits/T106/` and
`refactor/T106_untracked_memory_attribution.md` itself; zero `.cpp`/`.h`
diffs. Full raw evidence, the analysis script, and cross-checks are in
`/mnt/projects/fable/review/T106/` (README there indexes every file).

### The llvmpipe hypothesis — confirmed, refined

**llvmpipe (Mesa's software rasterizer) plus its LLVM JIT shader
compiler**, measured together as a single unit because they're always
loaded/used together on this dev VM, cost a **roughly fixed ~69-73 MB
resident per process, independent of zone or scene complexity**:

| Context | libgallium (llvmpipe) Rss | libLLVM (JIT) Rss | Combined | % of that run's RSS |
|---|---|---|---|---|
| Offline benchmark, qeynos2 (RSS 257,996 KB) | 11,264 KB | 61,796 KB | 73,060 KB (71.3 MB) | 28.3% |
| Offline benchmark, befallen (T105 near-final smaps, RSS 243,264 KB) | 11,264 KB | 61,796 KB | 73,060 KB (71.3 MB) | 30.0% |
| Live client, befallen, real populated zone (RSS 592,596 KB) | 11,644 KB | 58,964 KB | 70,608 KB (69.0 MB) | 11.9% |

The absolute MB is nearly identical across all three (69-71 MB) regardless
of whether the process is a 5-entity synthetic benchmark or a real 178-entity
populated zone with full audio/UI/network — confirming it's driver/JIT
overhead tied to EGL/GLES2 context creation, not scene content. Its share
of RSS swings from 12% to 30% purely because the *denominator* (total RSS)
differs so much between contexts, not because llvmpipe itself grows.

**This entire category does not exist on the Orange Pi.** Per
`src/client/graphics/CLAUDE.md`, the Pi uses the native `COpenGLES2Driver`
talking directly to the Mali400 kernel driver — no software rasterizer, no
JIT shader compilation, no `libLLVM`/`libgallium` anywhere in that code
path. **100% of this ~69-73 MB is dev-VM-only measurement noise.**

This does *not* fully explain "the untracked third," but it is the single
largest individually-identifiable dev-only chunk, and confirming its size
was the plan's explicit prerequisite before attributing anything else.

### The bigger correction: the untracked share isn't 34-36%, it's 75-90%

`totalEstimate` (and therefore `untrackedBytes`) credits EQE1 archives at
their **mapped** size (`eqe1MappedBytes`, ~110-120 MB) rather than their
**resident** size, which T105's Part 2 already measured at 1.6-10.6% of
that (reused verbatim here, not re-derived — see `review/T105/`).
Recomputing the untracked share using EQE1's *resident* bytes instead of
its *mapped* bytes (arithmetic only, on T105's own published numbers) gives
a very different headline:

| Zone | as-coded untracked % (T105) | corrected untracked % (this unit) |
|---|---|---|
| Qeynos2  | 34.6% | **75.3%** |
| Befallen | 36.0% | **79.1%** |
| Nektulos | 34.2% | **81.7%** |

Full arithmetic in `review/T106/corrected_untracked_percentages.md`.
**Independently cross-checked, same day, different harness**: this unit's
own mandatory `scripts/ci/visual_gate.sh` run (unmodified script, part of
the verification pass below) prints its own tracked/RSS/untracked line per
zone from a different code path with no `--eqe1-path` at all — qeynos2
75.6%, gfaydark 82.5%, befallen 90.2% untracked. Same 75-90% range, fully
independent measurement. **The as-coded 34-36% figure is real arithmetic on
`totalEstimate` as currently defined, but is itself an artifact of crediting
EQE1's mapped-not-resident size — the true gap this project needs to close
is three-quarters to nine-tenths of RSS, not a third.**

### `smaps` attribution table (Part 1)

Reused T105's `smaps` tooling approach (`/mnt/projects/fable/review/T105/bench/`)
and extended it to categorize the **entire** smaps dump (T105's own capture
already contained full smaps, not just the EQE1 subset — confirmed by
inspection), not just EQE1 mappings: `review/T106/scripts/analyze_smaps.py`
buckets every mapping by backing path (self-binary / each shared library,
with Mesa-llvmpipe/LLVM-JIT/EGL-glue split out separately / EQE1 archives /
`[heap]` / `[stack]` / anonymous-other) and sums `Size`/`Rss`/`Pss`.

**Offline benchmark, qeynos2, near-peak (fresh capture, ptrace-stopped via
`gdb`, self-consistent with the `mallinfo2()` reading below; RSS 257,996 KB
≈ T105's own qeynos2 reading of 257,048 KB — cross-validates both this
unit's capture method and T105's):**

| Category | Rss (KB) | % of RSS |
|---|---|---|
| `[heap]` | 151,020 | 58.5% |
| llvm-jit (`libLLVM.so.20.1`) | 61,796 | 24.0% |
| EQE1 archives (28 mappings) | 12,316 | 4.8% |
| mesa-llvmpipe (`libgallium*.so`) | 11,264 | 4.4% |
| other shared libs (190 mappings) | 7,364 | 2.9% |
| anonymous-other (303 mappings) | 7,020 | 2.7% |
| self-binary (`gles2_zone_benchmark`) | 5,876 | 2.3% |
| mesa-egl-gles-glue | 1,036 | 0.4% |
| everything else (stack, `[vdso]`, etc.) | ~300 | 0.1% |
| **TOTAL** | **257,996** | **100.0%** |

**Live client, befallen, real populated zone (178 entities, ptrace-stopped
at the same instant the in-game `/pmem` command printed "Tracked total:
62.7 MB" — matching T105's own befallen figure exactly; RSS 592,596 KB):**

| Category | Rss (KB) | % of RSS |
|---|---|---|
| anonymous-other (363 mappings) | 332,040 | 56.0% |
| `[heap]` | 160,908 | 27.2% |
| llvm-jit (`libLLVM.so.20.1`) | 58,964 | 10.0% |
| self-binary (`willeq`) | 15,516 | 2.6% |
| other shared libs (285 mappings, incl. full audio stack) | 11,992 | 2.0% |
| mesa-llvmpipe (`libgallium*.so`) | 11,644 | 2.0% |
| mesa-egl-gles-glue | 1,060 | 0.2% |
| everything else | ~200 | 0.0% |
| **TOTAL** | **592,596** | **100.0%** |

No EQE1 row for the live client — this session used the real S3D-decode
path (no `--eqe1-path`), matching T105's own live-session methodology; `/pmem`
confirms `[EQE1 Flat Packs] 0 B mmap'd (0 archives open)`.

**`willeq`'s binary size vs residency**: the ARM cross-build is 366,901,548
bytes (~350 MB) unstripped, matching the plan's "~340 MB" almost exactly;
the x86 dev build used for the live-client captures above is 82,765,264
bytes (~79 MB) unstripped — different toolchains, different debug-info
volume, same qualitative point. **Measured resident self-binary text+data**
(all `PT_LOAD`-mapped ELF segments, three independent live-client captures):
consistently **15,516-15,596 KB (~15.2-15.6 MB)** — almost none of the file
size is ever resident, because DWARF debug sections carry no `PT_LOAD`
program header and are never faulted into RSS regardless of file size. The
Pi's deployed binary is stripped (~10 MB per `CLAUDE.md`'s own build docs);
expect a smaller but nonzero resident figure there, plausibly single-digit
MB given ARM code density and no C++ exception-table/debug bloat — not
measured (Pi measurement is out of this unit's scope), reasoned only.

### The anonymous remainder (Part 2) — method, readings, and its limit

**Method chosen: `mallinfo2()` + `malloc_stats()`, called via `gdb` while
the target is ptrace-stopped, from outside the process, zero code
changes.** Chosen over the plan's other options because:
- `valgrind`/massif is **not installed in this environment** (`which
  valgrind` → not found, confirmed before choosing rather than assumed)
- `LD_PRELOAD` counting needs either a custom-allocator code change
  (out of this read-only unit's scope) or backtrace capture (effectively
  reimplementing a profiler) to attribute bytes to callers
- glibc's own `mallinfo2()`/`malloc_stats()` need no rebuild, no code
  change, and add zero steady-state distortion — the process runs at full
  speed until the single interrupt-and-query instant

**Mechanics**: this host's `yama.ptrace_scope=1` blocks `gdb -p <pid>`
attaching to an unrelated process, so the target is launched **as gdb's own
child** (`gdb --args <binary> ...`), which satisfies restricted ptrace. A
Python thread inside the gdb batch script sleeps N seconds then posts an
interrupt event, stopping the otherwise-normally-running inferior; at that
stop, `/proc/<pid>/status` and `/proc/<pid>/smaps` are read directly
(self-consistent with each other and with the `mallinfo2()`/`malloc_stats()`
calls that follow, all within the same paused instant), then execution
resumes to natural completion. Scripts in `review/T106/scripts/`.

**Stated limitation** (per the plan's requirement): `mallinfo2()` gives
*aggregate* allocator bookkeeping only — arena size, in-use, free, mmap'd-block
count. It cannot attribute bytes to a subsystem or call site. That
attribution gap is exactly what the "genuine remainder" below still is.

**Readings** (full detail, `review/T106/allocator_overhead.md`):

| | Offline benchmark, qeynos2 | Live client, befallen |
|---|---|---|
| VmRSS at capture instant | 257,996 KB | 592,596 KB |
| arena (brk-committed) | 186.3 MB | 478.3 MB |
| hblkhd (malloc's own big mmap'd blocks) | 5.1 MB (6 blocks) | 50.8 MB (8 blocks) |
| uordblks (in-use) | 160.1 MB | 330.3 MB |
| **fordblks (free, not released to OS — fragmentation)** | **26.2 MB (10.6% of RSS)** | **148.0 MB (25.6% of RSS)** |

`malloc_stats()`'s per-arena breakdown (live client) shows **66 arenas
total but all real allocation activity sits in Arena 0** — the other 65
(one per Mesa/llvmpipe worker thread, matching the ~65-70 thread-exit lines
logged at shutdown) are each ~2.9 KB in-use, essentially empty. So the large
`fordblks` figure is ordinary single-arena allocate/free churn (the live
client does far more of it — zone entities, UI, network, audio — than the
5-entity offline benchmark), **not** an artifact of per-thread-arena
fragmentation.

**Verdict: ~25.6% of the live client's total RSS is glibc allocator
overhead/fragmentation — real, large, previously unmeasured, and distinct
from both llvmpipe and genuinely-held-but-uncounted objects.** It exists on
the Pi via the same mechanism (glibc malloc fragments on ARM too) but
almost certainly at a much smaller absolute scale, since this reading
reflects a desktop-scale batch load (24.5 MB texture cache, 320 entries)
that the Pi's 128 MB-budgeted `orangepi` preset would never reach —
reasoned, not measured, per the plan's Pi-measurement prohibition.

### mmap trace — what the large anonymous blocks actually are

`strace -f -tt -e trace=mmap` over one full live session
(`review/T106/mmap_trace_notes.md`) found 160 `mmap` calls ≥8 MB, of which
149 are `PROT_NONE` per-thread malloc-arena *reservations* (64/128 MB of
address space each, near-zero `Rss` — already visible as the "many large
reservations, tiny Rss" pattern in the plain smaps categorization; harmless,
matches thread-arena-count). The remaining 11 are the real content: 5 are
already-counted shared-library segments (confirms `libLLVM`/`libgallium`
map their full segments in the first ~150ms of process start, before any
zone data loads); 3 are genuinely new information — two 22.8 MB anonymous
R/W blocks and one 64 MB `MAP_SHARED`+real-fd block (the fd+`MAP_SHARED`
combination is the signature of a memfd/shm-backed buffer — timing and the
loaded-library set (`libpulse`, `libpulsecommon`) make PulseAudio's shared
ring buffer the leading candidate, **not confirmed** — `strace` gives
syscall arguments, not call stacks); one is a 10 MB RWX block (the
permission bits are the standard signature of a JIT code buffer, plausibly
LLVM's shader compiler emitting native code). **None of these three
(~110 MB) are definitively attributed** — stated as informed hypothesis,
not measurement, per the plan's "do not report a residual as an
attribution" rule.

### Part 3 — the verdict, chunk by chunk

Live client, befallen, real populated zone — total RSS 592,596 KB (578.7 MB),
the fullest and most representative reading taken this unit (real spawns,
real audio, real UI, matching T105's live-session methodology):

| Chunk | Bytes | % of RSS | Owner | Exists on Pi? | Counter? |
|---|---|---|---|---|---|
| llvmpipe + LLVM JIT | 70.6 MB | 11.9% | Mesa software rasterizer + shader JIT compiler (file-backed, precisely measured) | **No** — native Mali400/GLES2 driver has neither | **No** — would bake dev-VM noise into a Pi-targeted gate |
| Tracked (graphics, via `/pmem`) | 62.7 MB | 10.8% | The 8 existing non-EQE1 counters (texture cache, zone geometry, placeable objects, entities, character models, equipment models, S3D source, GPU textures/framebuffer/VBO-EBO) | **Yes** — same subsystems, different absolute byte counts (fewer entities in range at 300u per T105 Part 1, native ETC1 on Pi vs driver-expanded here) | Already have counters |
| Self-binary resident text/data | 15.2 MB | 2.6% | `willeq`'s own `PT_LOAD` segments | **Yes, smaller** — stripped ~10 MB ARM binary, likely single-digit MB resident | No — small, stable, not scene-dependent |
| Other shared libs + EGL/GLES glue | 12.7 MB | 2.2% | libc/libstdc++/audio codecs/X11-xcb-wayland glue | **Partially** — libc-class libs yes; the X11/xcb/wayland-glue subset (a real fraction of the 285 mapped libs here) does not exist on the DRM/KMS Mali path | No |
| Allocator overhead/fragmentation | 148.0 MB | 25.6% | glibc malloc, unreleased free chunks (measured via `mallinfo2()`) | **Mechanism yes, scale almost certainly far smaller** — this reading reflects a batch load beyond the Pi's 128 MB preset budget | No dedicated counter — the lever is `malloc_trim()` at quiet points, not a report line item, and that's a behaviour change out of this unit's scope |
| **Genuine unattributed remainder** | **271.1 MB** | **46.9%** | Audio (SoundFont/MIDI/possible PulseAudio shm — invisible through the same `/pmem` reachability bug T105 found for RSS), UI/game-state, network buffers, Irrlicht internal scene-graph bookkeeping beyond what's counted, and whatever else lives in heap/anon space | Exists in some form (platform-independent C++ code), absolute scale unmeasured and likely very different (no PulseAudio-equivalent shm pattern assumed on the Pi's audio path, far less RAM to allocate against) | **Cannot recommend without further attribution** — see below |

(70.6+62.7+15.2+12.7+148.0+271.1 = 580.3 MB ≈ the measured 578.7 MB; the ~1.6 MB
gap is inter-category rounding across independently-sourced readings, not a
new unaccounted chunk.)

**Audio is the one exception to "do not instrument yet."** The plan's own
carve-out — "if a chunk is both large and trivially attributable to an
existing subsystem that simply lacks a counter, say so and recommend it" —
applies precisely here: `MemoryReportBreakdown` **already has** an Audio
section with real counters (`audioPfsArchiveBytes` etc.), gated on
`ext.audioAvailable`. The live `/pmem` command path passes an empty
`MemoryReportInput`, so `audioAvailable` is never true and the entire
`--- Audio ---` block is silently skipped even though it's live code —
confirmed directly: this unit's own live `/pmem` capture prints no Audio
section at all. **This is the same root-cause bug T105 already found for
`processRssBytes`/`untrackedBytes`** (`irrlicht_renderer.cpp:1219`'s
`/pmem` handler constructing an empty `MemoryReportInput{}`), not a new
one. **Recommended, not fixed here** (production code, out of this
read-only unit's scope): wire real `audioAvailable`/`audioPfsArchiveBytes`
into the same path T105 already flagged for RSS — one fix closes both gaps.

**`eqe1MappedBytes` should eventually report resident bytes, not mapped
bytes** — already recommended qualitatively by T105's Review, reaffirmed
here with the concrete headline-percentage consequence (34-36% vs 75-90%)
of not doing so. A counter *correction*, not a new counter; a production
change, not done here.

**What remains genuinely unattributable, and why**: the 271.1 MB "genuine
remainder" cannot be broken down further with the tools available in this
environment. `mallinfo2()` only gives aggregate totals, not call-site
attribution; `valgrind`/massif is not installed; adding an `LD_PRELOAD`
allocation hook with backtrace capture would itself be close to building a
profiler from scratch, and doing so *and* running it against a live,
networked eqemu session is a substantially larger undertaking than this
measurement unit's scope. The `strace` mmap trace narrowed ~110 MB of it to
plausible-but-unconfirmed candidates (PulseAudio shm, LLVM JIT code buffer)
by timing correlation alone — explicitly not proof. **Recommended follow-up
(separate unit, not this one)**: install a heap profiler (massif or
heaptrack) and run it against a real populated zone-in, accepting the
timing distortion the plan already flagged as this method's known cost.

### Deviations from the plan

- **Scope grew beyond the plan's own EQE1/willeq-binary/anonymous-remainder
  framing** to include the corrected-untracked-percentage finding (the
  34-36% figure being itself an artifact) and the audio-reachability
  finding. Both were surfaced by following the data, not planned in
  advance — flagged here explicitly rather than silently expanded scope.
  Neither required any code change; both are analysis findings plus one
  recommendation each (EQE1 basis correction, audio reachability fix),
  matching the plan's "do not instrument yet" / read-only constraints.
- **`strace` was used in addition to the plan's named tool list** (`smaps`,
  `mallinfo2`/heap-profiler/`LD_PRELOAD`) to investigate the anonymous
  remainder's largest blocks. It is external, read-only, and adds no code
  — treated as in-spirit with "pick one [Part 2] method and state its
  limitations" since it doesn't compete with the chosen `mallinfo2()`
  method, it supplements the mmap-trace angle the plan's Part 1 also asked
  about ("stacks, and anonymous regions").
- Three live-client sessions were run (one plain, two `gdb`-driven) plus
  one `strace`-driven session, all against the same befallen eqemu boot,
  rather than one — needed because `mallinfo2()` requires `gdb`-attach
  (ptrace-restricted, must launch as `gdb`'s child) while the mmap timing
  trace requires `strace`-attach, and the first plain run was needed to
  confirm steady-state RSS before adding either tool's overhead. Not a
  deviation from the plan's intent (still "at least one" real zone,
  befallen, matching T105 for comparability), just more session churn than
  a single run would have been.
- The `/pmem` RSS/untracked reachability bug (T105's finding #1 in
  `deferred.md`) was **not fixed** — confirmed still present, reused T105's
  benchmark-tool workaround for the numeric readings, per the plan's "known
  obstacle" guidance.
- No memory counters added, no allocation/caching/loading behaviour
  changed — confirmed via `git status --short` (only new files under
  `benchmarks/audits/T106/` and this plan file).

### Verification

1. `cmake --build build -j24` — PASS (no source changes; rebuild reflects
   `df63f315`, which only adds this plan document).
2. `ctest --test-dir build --output-on-failure` — **2246/2247 passed**,
   sole failure the documented pre-existing ALSA
   `AudioManagerTest.PreloadCommonSounds`, matching baseline exactly.
3. `bash scripts/build-gles2-x86.sh` — PASS, `build-gles2/bin/{willeq,gles2_zone_benchmark}`
   rebuilt cleanly.
4. Visual gate (`scripts/ci/visual_gate.sh`) — **PASS**, goldens untouched
   (`=== VISUAL GATE: PASS ===`); its own per-zone memory line is also the
   independent cross-check cited above.
5. `bash scripts/build-arm-noble.sh` — PASS, ARM cross-build succeeded
   (`build-arm-noble/bin/willeq`, 366,901,548 bytes unstripped).
6. `scripts/refactor/check_file_sizes.sh fable` — "no C++ files to check
   ... nothing was checked" — correct and expected, since no `.cpp`/`.h`
   files changed vs `fable` in this slot.

### Teardown

`down.sh` run, `status.sh` confirms no containers/ports remain, lock file
deleted. Testchar restored to `zone_id=36, x=-77.9232, y=21.9086, z=3,
h=190` and confirmed by `SELECT` after logout (heading had drifted to 128
from a prior session, not this one; restored to the documented value
regardless).

## Board

Record the attribution. Whether the memory-gate budgets can serve as a runtime
control surface for Will's load gate depends on this answer.
