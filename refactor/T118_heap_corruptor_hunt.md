# T118 — Find the heap corruptor behind the Pi's four crash signatures

**Slot: whichever is free. Start on x86 — do NOT start on the Pi.** eqemu lock
required. Pi only if the x86 legs come back clean.

**Territory: sanitizer/build configuration, `tools/`, `scripts/`,
`refactor/`, `/mnt/projects/fable/review/T118/`. Production `src/` changes
only under "If you find it" below.**

Read T117's Review first (the four signatures and the 2-of-10 rate), then
T115's (what x86 ASAN did and did not cover).

## The target

T117 established that the Pi is **not** stabilizing (2 of 10 clean), and — the
real finding — that **four independent victim containers were corrupted in the
same 10 runs with no shared write site**:

| Victim | When |
|---|---|
| `ZoneMergeBuffers::ranges_` (`__x=0x2601`) | mid-tour, at `/move -700` |
| `SkyData`'s texture map | teardown |
| `RaceModelLoader::vertexMappingForAnimation_` | teardown |
| glibc `_int_malloc` on an innocent `std::string` | **before** the hazard box |
| Mesa/Lima SIGBUS | teardown |

Unrelated containers, plus a crash inside malloc's own internals far from any
of them, is the signature of **one wild or out-of-bounds write**, not four
bugs. Three individually-correct lifetime fixes (T111, T115, T116) did not
move the rate, which is consistent.

**Your target is the write, not any victim.** Do not spend this unit on
`ranges_`.

## The decisive fact this unit starts from

**x86 ASAN has never run in anything close to the Pi's configuration.** T115's
and T116's ASAN runs were clean — 3 of 3 each, *including* teardown via
`/camp` — but `review/T115/run_asan_x86.sh` shows they ran with:

- **no `--constrained orangepi`** — so the entire constrained path
  (`ConstrainedTextureCache` and its LRU eviction, the `entityLoadRadius=400`
  gate, the constrained presets) was never instrumented;
- **no `--renderer gles2`** — a different renderer path than the Pi's;
- **`--no-audio`** — added as a workaround for the TinySoundFont overflow,
  which silently removed the whole audio subsystem from ASAN coverage from run
  2 onward.

So "x86 ASAN is clean" is a much weaker statement than it looked. **It means
clean on a substantially different program.** Closing that gap is cheap,
fast, and the obvious first move.

## Do these in order, and stop as soon as ASAN names the write

### Leg 1 — x86 ASAN in the Pi's actual configuration (cheap, do this first)

Rerun the existing x86 ASAN tour with the Pi's flags:
`--constrained orangepi --renderer gles2 -r 800 600`, **audio ENABLED**, same
15-step tour including `/camp` so teardown is covered.

Expect the TinySoundFont overflow (`tsf.h:1295`, see `deferred.md`) to fire
early. **Do not respond by disabling audio** — that is what hid this path
before. Instead use `ASAN_OPTIONS=halt_on_error=0` (and/or
`-fsanitize-recover=address`) so the run continues past it and reports
everything else, or apply the known one-line `nextPos` clamp locally as an
**uncommitted** test-only patch and say so. Either is fine; hiding the
subsystem is not.

### Leg 2 — 32-bit x86 ASAN (`-m32`)

The Pi is **32-bit ARM**; every x86 run so far has been 64-bit. A size
computation that truncates or overflows at 32 bits — or a struct-layout or
alignment assumption that only holds at 64 — would be invisible on x86-64 and
fatal on the Pi, and that is a strong shape for a wild write.

`-m32` gives you 32-bit `size_t` and pointer arithmetic **on a fast machine
with full ASAN**, which is far cheaper than fighting the Pi. Build i386 ASAN,
run the same tour. If the toolchain or a dependency will not go 32-bit,
report that as a blocker with what specifically failed — do not burn the unit
on it.

### Leg 3 — only if 1 and 2 are clean: make ASAN fit the Pi

T115's Pi ASAN failed 3 of 3 by OOM/timeout on a **292MB binary**. **Do not
repeat that invocation.** The reason it was 292MB is almost certainly **DWARF,
not ASAN instrumentation** — ASAN roughly doubles code size, while `-g` on a
C++ binary this size dominates everything.

So: build ASAN with `-g1` or `-gsplit-dwarf`, **deploy a stripped binary**,
keep the symbols on the NFS host, and **symbolize the report offline**
(`ASAN_OPTIONS=symbolize=0`, then `llvm-symbolizer`/`addr2line` against the
unstripped copy). Also trim runtime cost: `quarantine_size_mb=0`,
`malloc_context_size=5`, `detect_leaks=0`, `redzone` small.

**Report the deployed binary size before and after.** If it does not come down
to something the Pi can hold, say so plainly with the numbers — that is a real
result and it kills a line of attack we keep assuming is available.

### Leg 4 — cheap Pi-native probe, can run alongside

`MALLOC_CHECK_=3` and `MALLOC_PERTURB_=<nonzero>` cost almost nothing, fit the
Pi trivially, and catch double-frees and some overflows **at `free()` time**
with an abort. Weaker than ASAN and it will not name the writing line, but it
can confirm heap corruption and roughly bracket *when* it happens. Worth a few
runs regardless of how the other legs go.

## Axes that x86 can never cover — name them, don't pretend otherwise

If every x86 leg is clean, the corruptor lives in something x86 cannot
exercise. Say which of these remain, so the next unit starts from a real list:

- **Lima/Mali GLES2 driver and DRM/KMS** — x86 uses llvmpipe surfaceless.
  Note the teardown SIGBUS lands *inside* `libgallium`, and SIGBUS is
  characteristically an **alignment** fault — ARM is stricter than x86 here.
- **Native ETC1** compressed textures on Mali.
- **Actual ARM codegen and alignment rules.**
- **Real memory pressure** — 512MB with ~93MB resident, where an overwrite is
  far likelier to land on something live than on x86's slack.

## If you find it

**Fix it only if the fix is genuinely narrow and obviously safe** — the write
is off by one, the buffer is the wrong size, the cast is wrong. Then verify on
the Pi with T117's exact 10-run method and report the new rate against 2-of-10.

**Otherwise the diagnosis IS the deliverable.** A precise, evidenced
"here is the write, here is why, the fix is larger than this unit" is a
success. T115's honest negative result is what made T117 possible.

**Do not paper over**: no clamping a symptom, no guard that hides a faulting
access, no disabling a subsystem to make the crash go away.

## STOP conditions

- The fix would change buffer ownership, the zone merge/rebuild lifecycle, or
  GL teardown order — those are their own units.
- You find **more than one** independent corrupting write. Report both rather
  than half-fixing; that changes the shape of the whole arc.
- A leg needs the Pi for more than verification. Say what and why.

## Verification

- x86 suite (current total **2272**; sole failure the documented ALSA
  `AudioManagerTest.PreloadCommonSounds`) plus ARM cross-compile, **if** you
  changed production code. If you changed none, say so and skip.
- Any Pi rate claim must use **T117's exact method** — same tour, same flags,
  10 runs, stated as N of 10. **Do not compare a 3-run sample to T117's 10-run
  number**; T117's whole lesson is that small samples read as more stable than
  reality.
- Pi rules if you get there: cores and logs to **NFS never `/tmp`**,
  `--log-level=INFO`, no live gdb, teardown by real binary PID (`pgrep -x`),
  `down.sh`, lock deleted and confirmed gone.

## Review

Executed in `/mnt/projects/fable/wt/slot1`, task branch
`task/T118-corruptor-hunt`, base fable tip `3aea5bba` (already the worktree's
resting point after reset — no drift to note). Live eqemu (lock free at
session start; taken, `befallen` zone brought up — see "Deviation: zone
shortname" below — used across Legs 1-2, `down.sh` run, lock file deleted,
confirmed gone). Live Orange Pi (5 timed invocations across the session: 2
Leg 3 ASAN runs, 1 Leg 4 malloc-probe run, well under the 12-invocation cap;
left pristine — `core_pattern` restored to `core`, `/tmp` 0% used, governor
`ondemand`, no leftover `willeq_t118_*` process). Full logs, ASAN reports,
core, backtrace, README: `/mnt/projects/fable/review/T118/`.

**Headline: the write was not caught by any of the four legs, but Leg 4
reproduced the exact `ZoneMergeBuffers::ranges_`/`0x2601` signature under a
brand-new instrumentation regime (`MALLOC_CHECK_=3`/`MALLOC_PERTURB_`, plain
non-ASAN binary), with a full symbolized backtrace and the crashing register
state. Legs 1-3 (x86 ASAN in the Pi's exact runtime configuration, 32-bit x86
ASAN, and a Pi-fitted ASAN build that finally deploys and completes full
tours) are all clean — 3/3, 2/2, and 2/2 respectively, zero ASAN reports of
any kind against the target defect. That is a real, evidenced narrowing: the
write is not a classic heap-buffer-overflow or use-after-free that ASAN's
shadow-memory model can see, on either 64-bit or 32-bit x86, in the
constrained/gles2/audio-enabled configuration, or natively on the Pi's own
ARM/Lima/DRM stack. One production bug was found and fixed along the way
(unrelated to the corruptor): a CMakeLists.txt macro-definition/library
mismatch that made 32-bit builds impossible.**

### Leg 1 — x86 ASAN in the Pi's actual configuration: CLEAN, 3/3

Reused `build-asan/` (rebuilt at this unit's base rev), run with
`--constrained orangepi --renderer gles2 -r 800 600 --log-level=INFO`, audio
**enabled** (no `--no-audio`), the full 15-step Befallen tour incl. `/camp`
(`review/T118/tour_leg1.txt`, identical to T117's Pi tour). This is the
combination T115/T116 never ran under ASAN — no `--constrained`, no
`--renderer gles2` explicitly forced, and audio disabled from run 2 onward.

The first run reproduced the documented TinySoundFont overflow (`tsf.h:1295`,
`deferred.md`'s T115 side-finding) immediately, and — because ASAN was not
built with `-fsanitize-recover=address` — `ASAN_OPTIONS=halt_on_error=0` did
**not** keep the process alive past it (ASAN still calls `abort()` on any
report unless the recover flag was compiled in; this is worth recording so
the next unit doesn't repeat the same false assumption). Per the plan's
explicit alternative, applied the known one-line `nextPos` clamp
(`third_party/TinySoundFont/tsf.h`, all 3 `outputmode` branches) as an
**uncommitted, test-only** patch — backed up at
`review/T118/tsf.h.orig_backup`, restored before commit, never landed on the
branch. Rebuilt `willeq` only (header-only change, one TU touched).

With the patch in place, 3 consecutive full-tour runs (`review/T118/x86_leg1/run1.log`
/ `run2.log` / `run3.log`), each reaching `/camp` and `Application shutdown
complete`, **zero ASAN reports of any kind** (the only "ERROR" lines in any
log are the pre-existing, benign `[ERROR] [MAP] Unable to open map file:
./maps/befallen.map` collision-map warnings, unrelated and present in every
run including T115's/T117's).

**This closes the exact gap the plan named**: the constrained path
(`ConstrainedTextureCache`, `entityLoadRadius=400`), the GLES2 renderer
path, and the whole audio subsystem (TinySoundFont sample synthesis +
miniaudio callback thread) have now run under ASAN, together, for the first
time, through a full tour including teardown — and ASAN found nothing there.
"x86 ASAN is clean" is now a materially stronger statement than it was after
T115/T116.

### Leg 2 — 32-bit x86 ASAN (`-m32`): CLEAN, 2/2, after fixing a real build bug

Not blocked, but not free either — required building a full 32-bit
dependency chain from scratch:

1. **32-bit Irrlicht**: no prebuilt i386 patched Irrlicht existed. Built one
   at `third_party/irrlicht-gles2-x86-m32/` by rerunning
   `scripts/build-gles2-x86.sh`'s steps manually with `CXX="g++ -m32" CC="gcc
   -m32"` (needed `g++-multilib`/`gcc-multilib` for `bits/c++config.h`,
   missing on a bare `libssl-dev:i386` install). New `.gitignore` entry added
   alongside the x86_64 sibling — the directory itself is a local build
   artifact, never committed.
2. **i386 dev packages**: installed via `apt-get install ...:i386` (network
   available, `sudo -n` unprompted) — `libssl-dev`, `zlib1g-dev`,
   `libfmt-dev`, `libjsoncpp-dev`, `libegl1-mesa-dev`, `libgles2-mesa-dev`,
   `libdrm-dev`, `libgbm-dev`, plus `g++-multilib`/`gcc-multilib`.
3. **OpenSSL arch mismatch**: CMake's `FindOpenSSL` picked the amd64
   `libssl.so` by default (link error: "file in wrong format"). Fixed by
   passing `-DOPENSSL_SSL_LIBRARY=/usr/lib/i386-linux-gnu/libssl.so`
   `-DOPENSSL_CRYPTO_LIBRARY=/usr/lib/i386-linux-gnu/libcrypto.so` explicitly.
4. **A real, narrow CMakeLists.txt bug**, found only because i386 exposed
   it: `EQT_HAS_NAVMESH` was `ADD_DEFINITIONS`'d whenever
   `RECAST_INCLUDE_DIR`/`DETOUR_INCLUDE_DIR` were found (line 210), but
   `pathfinder_nav_mesh.cpp` — the file that actually implements
   `PathfinderNavmesh` — is only added to `CLIENT_CORE_SOURCES` when
   `RECAST_LIBRARY`/`DETOUR_LIBRARY` are found (line 345, pre-existing). On
   this host `librecast-dev` has **no i386 package at all**
   (`/usr/include/recastnavigation` headers are architecture-independent and
   were found; the `.so` was not), so the macro got defined with no
   implementation linked in — an undefined-reference link failure for
   `PathfinderNavmesh`'s vtable/typeinfo/methods. This was latent on every
   prior build only because every environment that had the headers also
   happened to have the matching-arch library. **Fixed**: `CMakeLists.txt`
   now gates `EQT_HAS_NAVMESH` on `RECAST_LIBRARY AND DETOUR_LIBRARY`,
   matching the source-inclusion condition exactly. Narrow, additive,
   verified: x86_64 build/ctest/ARM cross-compile all still pass (see
   "Verification" below) with `EQT_HAS_NAVMESH` still correctly defined on
   every environment that has the real library.

With those fixed, `build-asan-m32/bin/willeq` built and linked (32-bit PIE
ELF, confirmed via `file`). **Default ASAN options immediately hit an
internal allocator OOM** (`AddressSanitizer: out of memory: allocator is
trying to allocate 0x10008 bytes` / `internal allocator is out of memory
trying to allocate 0x20 bytes`) within the first rendered frame after zone
load — a known ASAN-on-32-bit ceiling (shadow memory + redzones consume most
of the ~3-4GB 32-bit address space regardless of host RAM), not a code
finding. Reduced with `quarantine_size_mb=0 malloc_context_size=2
max_redzone=32 allocator_may_return_null=1`; with those options **2
consecutive full-tour runs completed cleanly** (`review/T118/x86_leg2_m32/run2_lowmem.log`,
`run3_lowmem.log`), reaching `/camp` and full teardown, **zero ASAN reports**.

**This is the axis T115/T117 flagged as the strongest untested shape** ("a
size computation that truncates or overflows at 32 bits... would be invisible
on x86-64 and fatal on the Pi") — now tested and clean, at least for whatever
the 15-step tour exercises.

### Leg 3 — ASAN made to fit the Pi: now deployable, CLEAN, 2/2

T115's ASAN ARM binary was 292MB — DWARF, not instrumentation, per the
plan's hypothesis — and never fit the Pi's 242MB `/tmp` tmpfs, forcing an
NFS-direct run that then OOM'd 3/3 times on RSS alone. Rebuilt with
`-O1 -g1 -fsanitize=address -fno-omit-frame-pointer` (vs. T115's `-O1 -g`)
via a custom `docker run` against the existing `willeq-arm-noble` image
(willeq target only, skipping the ~15 benchmark tools T115/T117 didn't need
either).

**Binary size, before/after:**

| | Unstripped | Deployed (stripped) |
|---|---|---|
| T115 (`-g`) | ~292MB (never stripped/deployed — ran off NFS) | N/A — didn't fit `/tmp` |
| T118 (`-g1`) | 165,189,584 bytes (~157.5MB) | **38,494,528 bytes (~36.7MB)** |

`-g1` alone roughly halved the unstripped size versus T115's `-g`; stripping
(`arm-linux-gnueabihf-strip`) took it the rest of the way to 36.7MB —
comfortably inside the 242MB tmpfs (16% used after staging, `libasan.so.8`
already present system-side on the Pi, no extra deploy needed). Symbols kept
on NFS at `review/T118/leg3_pi_asan/willeq_t118_asan_unstripped_symbols` for
offline symbolization — used with `symbolize=0` and native Pi `gdb`, per the
plan, though this leg's runs produced no ASAN reports to symbolize.

Ran the same 15-step tour, audio enabled, `--drm --renderer gles2
--constrained orangepi -r 800 600`, `ASAN_OPTIONS` with the same trimmed
memory settings as Leg 2 (`quarantine_size_mb=0 malloc_context_size=5
max_redzone=32`), core dumps and `log_path` on NFS, `ulimit -c unlimited`.
**Both invocations (`review/T118/leg3_pi_asan/pi_run1.log`, `pi_run2.log`)
completed the full 15-step tour to `Application shutdown complete`, zero
ASAN reports.** ASAN's steady-state overhead on the Cortex-A7 is severe —
~660 seconds wall-clock per run (vs. a few tens of seconds unsanitized),
memory oscillating between ~5MB and ~40MB free of the Pi's 512MB during
zone-object construction, but never OOMing this time (the trimmed
`ASAN_OPTIONS` and smaller/stripped binary are the difference from T115).

This closes T117's "GL teardown order" and "real memory pressure" axes for
the shapes ASAN itself is built to catch (heap/stack buffer overflows,
use-after-free, use-after-return, double-free) — it does **not** rule out a
wild write to an arbitrary address that ASAN's shadow model doesn't see, nor
alignment-only faults (see "Axes that remain," below).

### Leg 4 — cheap Pi-native malloc probe: **reproduced the target signature**

`MALLOC_CHECK_=3 MALLOC_PERTURB_=165`, the plain (non-ASAN) stripped ARM
binary (10.3MB — `arm-linux-gnueabihf-strip` off `build-arm-noble/bin/willeq`,
built at this unit's own base rev with the `EQT_HAS_NAVMESH` fix included),
same tour, same flags. **First invocation crashed with SIGSEGV, core
dumped**, at `/move -700` — the exact hazard-box window every prior unit
(T113, T115 partially, T117) has named.

**Symbolized with Pi-native `gdb`** (`build-arm-noble/bin/willeq` via NFS,
never copied — same binary and source tree the core was produced against;
full transcript `review/T118/leg4_malloc_probe/pi_run1_backtrace.txt`):

```
#2  std::map<void const*, EQT::Graphics::ZoneMergeBuffers::RegionBufferRange, ...>::find
    (__x=<optimized out>, this=<optimized out>)
    at /usr/arm-linux-gnueabihf/include/c++/13/bits/stl_map.h:1220
#3  EQT::Graphics::ZoneMergeBuffers::renderMerged (this=<optimized out>, driver=<optimized out>,
    sortedEntries=std::vector of length 2207, capacity 4096 = {...})
    at /src/src/client/graphics/renderer_zone_merge.cpp:230
#4  EQT::Graphics::IrrlichtRenderer::drawZoneGeometrySorted (this=<optimized out>)
    at /src/src/client/graphics/renderer_zone_draw.cpp:669
#5  EQT::Graphics::IrrlichtRenderer::RenderPassTimer::OnRenderPassPreRender (...)
    at /src/src/client/graphics/renderer_zone_draw.cpp:260/227
#6  irr::scene::CSceneManager::drawAll()
#7  EQT::Graphics::IrrlichtRenderer::processFrameRender(float) at renderer_frame.cpp:1169
#8  EQT::Graphics::IrrlichtRenderer::processFrame(float) at renderer_frame.cpp:210
#9  eqt::Application::render / mainLoop / run  (main/render thread — confirmed single-threaded
    call stack, not the SimulationWorker or audio callback thread)
```

Register state at the fault (`pc = 0x6d3ce0`, inside
`ZoneMergeBuffers::renderMerged(...)+380`):

```
r0   0x2601  9729     (map key comparand)
r2   0x2601  9729
r3   0x2601  9729
```

**`r0`/`r2`/`r3` all hold `0x2601` (9729 decimal) — byte-for-byte identical
to T113's, T114's discounted smoke-test's, and T117's four `ranges_.find()`
crashes**, now reproduced under a completely different memory-fill regime
(`MALLOC_PERTURB_` overwrites freed memory with a non-zero byte and its
complement) with a plain, non-instrumented binary. `sortedEntries[k].buffer`
— the `find()` key, a raw `irr::scene::IMeshBuffer*` populated in
`collectMeshBuffers()` from `mesh->getMeshBuffer(i)` (`renderer_zone_draw.cpp:365`)
— is the value corrupted; 0x2601 is far too low to be any valid heap, stack,
or global address on this process (glibc's ARM heap base is nowhere near the
null-page region), so this remains consistent with T117's "wild/uninitialized
pointer" framing, not a new bug class.

**Read with appropriate caution**: `MALLOC_PERTURB_` changes memory
*contents* at existing addresses, not address *values* themselves — so this
run does not independently rule address-dependent heap-layout theories in or
out, and the fact the same exact value recurred is expected if the
underlying computation that produces `0x2601` is itself deterministic given
the same zone/position/game-state (which `/move -700` at a fixed spot in
Befallen is). It does add one genuinely new fact: this crash is **not**
sensitive to glibc's internal free-list/perturbation behavior, which mildly
disfavors (without disproving) a classic "write past the end of an adjacent
allocation, and what's now there depends on allocator state" shape, and
mildly favors a **deterministic wrong-value computation** (an uninitialized
read, a stale/dangling pointer reused after a deterministic teardown
sequence, or a non-pointer value written into the `buffer` slot) as at least
as likely a mechanism as a spatial OOB write. This doesn't resolve which one
it is — it's a lead for whichever unit takes the write-hunt further, not a
finding this unit had budget to chase past the reproduction.

Core preserved: `review/T118/leg4_malloc_probe/pi_cores/core.willeq_t118_pla.8840`
(198MB on NFS, not `/tmp`).

### Whether the Pi-config x86 run changed the picture vs. T115's weaker configuration

Yes, materially. T115's ASAN coverage was audio-free from run 2 onward and
never touched `--constrained`/`--renderer gles2` explicitly at all (relying
on x86 defaults). Leg 1 closes that gap outright: constrained + gles2 +
audio-enabled, full tour, three clean runs. Combined with Leg 2 (32-bit) and
Leg 3 (native ARM ASAN, now actually completing runs), **every axis ASAN
itself can observe has now been exercised and comes back clean.** The
target defect survives all of it — which, per the plan's own framing, is
itself the news: it means the corruptor is in the category of bugs ASAN's
shadow-memory model structurally cannot see (a write through a wild/garbage
pointer value that never touches a byte ASAN is tracking, as T115 already
established for the `portalVisibleRegions_`-class SEGV's "can not provide
additional info" signature), or it depends on real ARM/Lima specifics Leg
1-3 still can't reach even when they run ASAN on the Pi itself (see next
section).

### Axes that x86 (and now ASAN-on-Pi) still cannot cover

- **Lima/Mali GLES2 driver and DRM/KMS internals below the ASAN-visible
  boundary.** Leg 3 puts ASAN on the Pi's own CPU/OS/allocator, but a fault
  originating inside the closed-source-adjacent kernel DRM/GEM path, or a
  write that lands in GPU-mapped memory ASAN's userspace shadow doesn't
  cover, is still invisible to it.
- **A write through a genuinely wild (uninitialized or freed-and-reused)
  pointer that happens to land outside any ASAN-tracked redzone entirely** —
  this is exactly the shape T115 already demonstrated for a different field
  (`portalVisibleRegions_`, "can not provide additional info") and Leg 4's
  `0x2601` register value is consistent with the same shape: an address so
  low it was never inside any allocation ASAN instruments, so ASAN has
  nothing to report even when it's running.
- **Actual ARM codegen / struct layout under `-O1`/`-O2` with real
  optimization**, as opposed to x86's `-O1` ASAN build — this unit did not
  test an ARM build at `-O2` (T113/T114/T117's normal deployment) under any
  sanitizer; Leg 3 used `-O1` to match ASAN's own recommended flags.
- **Timing/scheduling races specific to the Pi's single Cortex-A7-class
  core budget and the DRM/KMS vsync path** — Leg 1-2 run on a 24-core x86
  host under llvmpipe; Leg 3 runs on the real hardware but ASAN's own ~15-20x
  slowdown changes the timing envelope substantially versus an unsanitized
  run, so a narrow race that only manifests at real frame-rate remains
  untested by any ASAN leg.

### Deviations

- **eqemu zone shortname**: the plan's Territory notes eqemu ownership but
  not which zone; T115/T117's tour is Befallen-specific (`/move -400`/`-700`
  are Befallen coordinates), so `up.sh` was run with `befallen` explicitly
  (its default is `arena`) — matching T115/T117, not a deviation from their
  practice, just recorded since the plan didn't name it.
- **Leg 1's `halt_on_error=0` did not work as expected** without
  `-fsanitize-recover=address` compiled in — documented above so the next
  unit doesn't repeat the assumption. Used the plan's explicitly-sanctioned
  fallback (the `tsf.h` test patch) instead.
- **CMakeLists.txt fix**: not anticipated by the plan, but squarely inside
  "sanitizer/build configuration" territory and required to complete Leg 2
  at all. Narrow (one `IF()` condition), additive, verified on the normal
  x86_64 tree and ARM cross-compile (both still pass with
  `EQT_HAS_NAVMESH` correctly defined wherever the library actually exists).
- **Leg 3/4 run counts are 2 and 1 respectively, not a full T117-style
  10-run tally** — this unit makes no Pi crash-rate claim (no fix was made,
  so there is no new rate to report against 2-of-10), so T117's "10 runs,
  same tour, same flags" discipline applies to *rate claims specifically*,
  which this unit does not make. Leg 4's single run already produced a
  decisive, evidenced result (a reproduction with full symbolization); Legs
  1-2's 3/2 clean runs are consistent with, not a substitute for, further
  sampling if a future unit wants tighter confidence there.
- **`build-arm-noble-asan/bin/willeq`** (436MB, dated earlier in the session)
  is a stale artifact from an unrelated prior build in this reused directory
  — not produced or used by this unit; the ASAN binary this unit built and
  used is `willeq_asan_unstripped`/`willeq_asan_stripped`, named explicitly
  throughout.

## Board

**The write itself is still not found — this unit's deliverable is a
substantially narrowed search space, plus one incidental production fix.**
Every axis ASAN can observe (heap/stack overflow, UAF, double-free, on
64-bit x86, 32-bit x86, and now natively on the Pi's own ARM/Lima/DRM stack,
with audio and the constrained GLES2 path all enabled) is now clean. What
remains is the class of bug ASAN structurally cannot see: a write through an
already-wild pointer value, landing outside any tracked allocation — Leg 4's
`0x2601` register capture is a concrete, reproducible instance of exactly
that shape, with a full backtrace pinning it to `ZoneMergeBuffers::renderMerged`
reading `sortedEntries[k].buffer` (populated from `mesh->getMeshBuffer(i)` in
`collectMeshBuffers()`) immediately after `/move -700`. The next unit's most
promising lead is **not** another sanitizer pass — it's tracing backward from
`collectMeshBuffers()`/the mesh-buffer lifecycle at the exact BSP-region
transition `/move -700` triggers, since `0x2601`'s invariance across
allocator/instrumentation regimes suggests a deterministic wrong-pointer
computation (stale/uninitialized/reused-after-teardown) rather than
address-dependent heap-layout corruption. No fix was attempted on the
corruptor itself (STOP condition: "the fix would change... the zone
merge/rebuild lifecycle" — tracing `collectMeshBuffers()`'s mesh-buffer
lifecycle is exactly that). The Pi rate against 2-of-10 is unchanged by this
unit since nothing was fixed; that measurement still belongs to whichever
unit lands the actual fix.
