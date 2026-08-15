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

**Headline (revised after a coordinator review caught a real gap in the
first draft — see "Leg 4" below for the full correction): the write was not
caught by any of the four legs, but Leg 4 reproduced the exact
`ZoneMergeBuffers::ranges_` crash under a brand-new instrumentation regime
(`MALLOC_CHECK_=3`/`MALLOC_PERTURB_`, plain non-ASAN binary) with a full
symbolized backtrace, and this time the crashing register value —
`0x2601`, present in T113's, T117's, and this run's identical crash — was
correctly identified: it is `GL_LINEAR`, not an unidentified small integer.
Corrected disassembly (against the exact binary/core pair, not a
subsequently-rebuilt one — see the evidence-chain note in "Leg 4") further
shows the corrupted item is a tree-node child pointer inside `ranges_`'s own
rb-tree, not the external search-key argument as first drafted. Together
these point at a concrete, named write site — `COGLES2Texture::
cacheTexParams()` storing `GL_LINEAR`/`GL_NEAREST` into a texture object's
cache fields through a pointer whose validity depends on
`ConstrainedTextureCache` eviction timing — as the leading candidate
mechanism, not yet confirmed. Legs 1-3 (x86 ASAN in the Pi's exact runtime
configuration, 32-bit x86 ASAN, and a Pi-fitted ASAN build that finally
deploys and completes full tours) are all clean — 3/3, 2/2, and 2/2
respectively, zero ASAN reports of any kind against the target defect,
which is now well-explained rather than merely observed: a well-formed
write of a valid value through a stale pointer into a different live
allocation crosses no redzone and touches no freed-and-poisoned memory, so
ASAN has nothing to catch. One production bug was found and fixed along the
way (unrelated to the corruptor): a CMakeLists.txt macro-definition/library
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
r0   0x2601  9729
r2   0x2601  9729
r3   0x2601  9729
```

**CORRECTION (post-review, both to this Review and to the earlier drafting
of it): `0x2601` is not "far too low to be a valid address" — it is
`GL_LINEAR`** (`GL_NEAREST` is `0x2600`, `GLES2/gl2.h`). It is not arbitrary
low-address garbage; it is a named GLenum constant, in a codebase whose
entire renderer is GLES2. The line originally here calling it "far too low
to be any valid heap, stack, or global address" was true but incomplete — it
described what the value *isn't* without asking what it *is*, and a
Claude-side coordinator review caught that gap. Re-examined below with the
correct binary this time (see the caution paragraph after this one on why
that mattered) and the correct disassembly.

**Also corrected: which thing is actually corrupted.** The original draft of
this section labeled the crash "the map key comparand," extrapolating from
the earlier x86 read of the analogous T115 SEGV. Re-disassembling the exact
crashing instruction (`gdb disassemble` on the Pi, against the *exact*
stripped binary that produced this core plus the core itself — not the
freshly-rebuilt `build-arm-noble/bin/willeq`, which this unit's own later ARM
verification build had silently overwritten; using the wrong binary was a
real mistake in the first pass, caught and fixed before this correction)
shows the fault is:

```
=> 0x006d3ce0:  ldr  r2, [r3, #16]        ; SIGSEGV here: r3 = 0x2601, r3+16 unmapped
   0x006d3ce2:  ldrd r1, r0, [r3, #8]
   0x006d3ce6:  cmp  r2, r8
```

libstdc++'s `_Rb_tree_node_base` (`bits/stl_tree.h`) is `_M_color`(0)/
`_M_parent`(4)/`_M_left`(8)/`_M_right`(12) — 16 bytes total — so offset 16 is
exactly where the derived `_Rb_tree_node<pair<const Key,Val>>`'s payload
starts, i.e. this is `_S_key(__x)` inside `_M_lower_bound`'s tree-walk loop
(`while (__x) { if (!compare(_S_key(__x), __k)) __y=__x, __x=_S_left(__x);
else __x=_S_right(__x); }`, `bits/stl_tree.h:1338`). **`r3` is `__x` — the
tree-node cursor being walked, not the external search-key argument to
`find()`.** It got here by a *previous* iteration reading `_S_left`/
`_S_right` (offsets 8/12) off some earlier, presumably-valid node and getting
`0x2601` back. That means one of `ranges_`'s actual heap-allocated rb-tree
nodes has a `_M_left` or `_M_right` child-pointer slot **overwritten with the
literal bit pattern of `GL_LINEAR`.**

This is a correction to **this unit's own Leg 4 draft**, not to T113. Checked
directly against `deferred.md`'s T113 entry (the "Directly, mechanistically
tied to the `RegionVis` lead" passage): T113 already wrote, at the time,
*"`__x=0x2601` as a tree-node pointer means that map's own tree structure is
corrupted, not merely missing the key (a miss is already handled gracefully
two lines later)"* — which is exactly right, and exactly what this
disassembly confirms. **T113's original reading was correct all along; this
unit's own first-pass mislabeling of it as "the map key comparand" was the
error, now fixed.** The plan's prompt asked whether "every prior 'corrupted
tree' reading — T113's included — was wrong about the shape"; the answer,
checked from the disassembly rather than inferred, is no: T113 had it right,
this unit briefly had it wrong, and it's now corrected back to T113's
original, accurate characterization.

**Where a real `GL_LINEAR` write could originate**: `COpenGLES2Driver::
applyMaterialRenderState()` (`docker/irrlicht-drm/COpenGLES2Driver.cpp:1319`,
called from `renderMerged()`'s own draw path for both merged and per-buffer
unmerged draws) computes `GLint magFilter = ...BilinearFilter ? GL_LINEAR :
GL_NEAREST` and, when the value changed, calls `tex->cacheTexParams(magFilter,
minFilter, wrapS, wrapT)` on a `const COGLES2Texture* tex` obtained via
`static_cast<const COGLES2Texture*>(material.getTexture(0))`.
`cacheTexParams()` (`docker/irrlicht-drm/COGLES2Texture.h:79`) is a real,
deliberate write — **not just a GL API call** — into `mutable GLint
CachedMagFilter`/`CachedMinFilter`/`mutable GLenum CachedWrapS`/`CachedWrapT`
member fields on `*tex` (a P42 state-dedup cache, explicitly `mutable`
because it's written through a `const` pointer by design). **If `tex` is
ever stale** — e.g. a texture evicted by `ConstrainedTextureCache`'s LRU
(`constrained_texture_cache.cpp:599`, `driver_->removeTexture(...)`) while a
queued/sorted `ZoneDrawEntry`'s `SMaterial` still references the old
pointer — this write lands wherever that freed memory has since been
reused. Both `COGLES2Texture` objects and `ranges_`'s `std::map` nodes come
from the same global heap allocator, so a freed texture's memory being
reused for one of `ranges_`'s rb-tree nodes shortly afterward is entirely
plausible, and `CachedMagFilter = GL_LINEAR` landing at the reused node's
`_M_left`/`_M_right` offset would produce exactly this crash.

**I did not chase this into a fix** — tracing `applyMaterialRenderState()`'s
texture-pointer lifetime against `ConstrainedTextureCache`'s eviction timing
is squarely "the zone merge/rebuild lifecycle," the plan's own explicit STOP
condition, and its own unit. This is reported as the strongest concrete
*candidate write site* found this unit, not a confirmed root cause — I have
not proven `tex` is actually stale at the crash moment, only that (a) this
is a real code path that writes the exact corrupted value, through a pointer
whose validity depends on cache-eviction timing that this unit didn't
independently verify, and (b) the offset arithmetic is at least plausible.
A future unit should look here first, with instrumentation on texture
delete/reuse and `tex` pointer identity at the crash's exact tour step,
before assuming this is confirmed.

**This also newly explains why Legs 1-3's ASAN runs came back clean**: this
is not a bounds violation (the write, if this is the mechanism, lands
squarely inside a real, currently-allocated object — just the *wrong*
object) and not a classic use-after-free in the sense ASAN is built to catch
(the freed `COGLES2Texture` memory has already been *reallocated* to a new,
live owner by the time the stale write happens, so there's no poisoned
freed-region access for ASAN's quarantine to catch — only a well-formed
write to valid memory through the wrong pointer). This is precisely the
blind spot T115 already demonstrated for the unrelated `portalVisibleRegions_`
field ("can not provide additional info" — a wild-but-plausible pointer, not
an OOB access).

**Read with appropriate caution on the `MALLOC_PERTURB_` angle**:
`MALLOC_PERTURB_` changes memory *contents* at existing addresses, not
address *values* themselves — this run does not independently rule
address-dependent heap-layout theories in or out. The exact-value recurrence
across T113/T117/this run is now much better explained than in the earlier
draft: **`GL_LINEAR` recurs exactly because it is a constant, not because a
computation happens to be deterministic.** GL-filter-mode code writes the
same named enum value every time bilinear filtering is requested (the
overwhelmingly common case for zone textures) — there is no computation to
vary, so of course every sighting shows the identical bit pattern regardless
of allocator/instrumentation regime. That fact, once the value is identified,
argues strongly *against* the "deterministic wrong-pointer computation, e.g.
uninitialized index" framing the earlier draft of this Review proposed, and
toward type confusion / stale-texture-pointer-write instead.

Core and disassembly transcript preserved:
`review/T118/leg4_malloc_probe/pi_cores/core.willeq_t118_pla.8840` (198MB,
NFS) and `review/T118/leg4_malloc_probe/pi_run1_disassembly.txt`
(the corrected, matching-binary disassembly + register dump).

**Evidence-chain note, flagged rather than silently fixed**: the first pass
at re-examining this crash (prompted by the coordinator's GL_LINEAR
observation) ran `objdump`/`addr2line` against `build-arm-noble/bin/willeq`
as it stood *at that moment* — which by then was a **different binary** than
the one that actually produced this core, because this unit's own
verification step (`bash scripts/build-arm-noble.sh`, run to check the
`EQT_HAS_NAVMESH` fix) had rebuilt and overwritten that file in between. The
mismatch was caught because the resolved symbol (`CBurningVideoDriver::
VertexCache_get`) didn't match gdb's original DWARF-based symbolization
(`ZoneMergeBuffers::renderMerged+380`) — a PIE binary's raw addresses only
mean anything paired with the exact binary/load-bias they came from, and
`nm`/`objdump`/`addr2line` don't apply ASLR load-bias correction the way
`gdb` does against a matched core. Recovered by re-running against the
*exact* binary that ran (the already-preserved `willeq_t118_plain`, stripped
but byte-identical in `.text`, paired with the preserved core) via Pi-native
`gdb`, which resolves load bias automatically from the core's own memory
map. **Lesson for future units re-examining a core after any subsequent
rebuild in the same session: always re-verify against the exact preserved
binary, never the live build directory, once anything downstream has
touched it.**

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
- **A well-formed write of a valid value through a stale/wrong pointer,
  landing inside a different live allocation** — no redzone crossed, no
  freed-and-still-poisoned region touched, so ASAN has structurally nothing
  to flag. This is exactly the shape T115 already demonstrated for
  `portalVisibleRegions_` ("can not provide additional info"), and Leg 4's
  finding fits it precisely: a real, identified write site
  (`COGLES2Texture::cacheTexParams()`, see "Leg 4" above) storing a named GL
  constant into a `mutable` cache field, through a `const COGLES2Texture*`
  whose validity depends on texture-cache eviction timing this unit did not
  verify. If that pointer is stale, its target has already been reallocated
  to a new, valid owner — invisible to ASAN by construction, not merely by
  bad luck.
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

**The write itself is still not confirmed — but this unit narrows it to a
named mechanism and a named write site, not just a named search space, plus
one incidental production fix.** Every axis ASAN can observe (heap/stack
overflow, UAF, double-free, on 64-bit x86, 32-bit x86, and now natively on
the Pi's own ARM/Lima/DRM stack, with audio and the constrained GLES2 path
all enabled) is clean — and, now that the corrupted value is identified as
`GL_LINEAR` rather than unidentified garbage, that cleanliness is
*explained*, not just observed: a well-formed write of a valid GLenum
through a stale-but-plausible texture pointer into a different live
allocation crosses no ASAN redzone and touches no freed-and-poisoned region.

**The leading candidate**: `COGLES2Texture::cacheTexParams()`
(`docker/irrlicht-drm/COGLES2Texture.h:79`), called from `COpenGLES2Driver::
applyMaterialRenderState()` (`docker/irrlicht-drm/COpenGLES2Driver.cpp:1319`)
on every draw where the bilinear-filter state changed, writes `GL_LINEAR`/
`GL_NEAREST` into `mutable` fields on a `const COGLES2Texture*` obtained from
`material.getTexture(0)`. If that texture was evicted by
`ConstrainedTextureCache`'s LRU (`constrained_texture_cache.cpp:599`,
`driver_->removeTexture(...)`) while a queued `ZoneDrawEntry`'s material
still referenced it, the write lands in whatever now occupies that freed
slot — plausibly one of `ranges_`'s own rb-tree nodes, both being ordinary
heap allocations from the same allocator. Corrected disassembly (`gdb` on
the Pi against the exact matching binary+core, not a since-overwritten one —
see "Leg 4"'s evidence-chain note) shows the crash is a tree-node
child-pointer dereference inside `ranges_`'s own structure, consistent with
exactly this: a `_M_left`/`_M_right` slot holding `GL_LINEAR` instead of a
node pointer or null.

**Not confirmed**: this unit did not verify `tex`'s staleness at the crash
moment, did not instrument the eviction/reuse race, and did not check
`SkyData`'s texture map (T117's other texture-adjacent victim, and a
plausible fit for the identical story — `sky_renderer.cpp` issues its own
`glTexParameteri(..., GL_LINEAR)` calls) or `RaceModelLoader::
vertexMappingForAnimation_` (T117's fourth victim, a vertex-mapping vector
with no direct texture/GL-call adjacency found in a bounded search — a
weaker fit, reported honestly rather than forced into the same story)
beyond a quick read. The next unit's most promising lead is **not** another
sanitizer pass — it's confirming or refuting `cacheTexParams()`'s stale-`tex`
theory directly: instrument `applyMaterialRenderState()`'s texture pointer
identity against `ConstrainedTextureCache`'s eviction log at the exact
`/move -700` tour step, and check `SkyData`'s crash for the same write
signature. No fix was attempted (STOP condition: tracing
`ConstrainedTextureCache`/texture-material lifetime against the zone-merge
draw path is exactly "the zone merge/rebuild lifecycle," its own unit). The
Pi rate against 2-of-10 is unchanged by this unit since nothing was fixed;
that measurement still belongs to whichever unit lands the actual fix.
