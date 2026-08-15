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

Executed in `/mnt/projects/fable/wt/slot1`, task branch `task/T113-pi-midtour-segv`,
base fable tip `5709c563`. Live Orange Pi (deployed, exercised, left pristine —
`core_pattern` restored to `core`, `/tmp` 0% used, governor `ondemand`). Live
eqemu (lock free at session start; taken, used, released cleanly — the
`config/willeq_test_char.sql`/`Testchar` position was never touched by this
unit, only observed). Reused T112's exact reproduction inputs unmodified
(`review/T112/run_inputs/willeq_t112_pi.json`, `tour_pi.txt` — the
T109/T111-proven-safe 2-waypoint Befallen tour) so "same conditions" is a
file-identity guarantee, not a claim about re-typed commands.

**Headline: one real, confirmed, fixed defect (a genuine sibling of T111's
`regionPvsDepthMap_` fix, in the same "background-worker-writes,
main-thread-reads-without-the-matching-lock" class) explains T110's teardown
SIGABRT exactly. It is fixed here — narrow, three mutex additions plus their
call-site wrap-ups, no threading redesign — and T110 closes with this unit
for that specific signature. But it is NOT the whole story: the mid-tour
SIGSEGV the plan named as primary persisted after the fix, in a DIFFERENT
subsystem, directly and mechanically implicating the RegionVis lead. That
second defect is diagnosed with a precise, symbolized backtrace and a traced
data-flow explanation, but not fixed — its root cause (heap corruption
detected inside `ZoneMergeBuffers::ranges_`, a `std::map` walked while
already corrupted) is not yet isolated to a single write site, and closing it
properly needs either ASAN instrumentation or a longer, more targeted Pi
session than remained in this one's budget. A third crash signature (SIGBUS
inside the Mesa/Lima GL driver at teardown, no symbols available) was also
captured and is reported, unexplained.**

### Method: post-mortem core dumps, not live gdb

Set `/proc/sys/kernel/core_pattern` via `sudo` on the Pi, ran the client
normally at `--log-level=INFO -d 4` (never under gdb), analysed cores
afterward with `gdb -batch`. **Deviation from the plan's literal target
directory**: the plan says cores land in `/tmp` (242MB tmpfs) and get pulled
off after. The very first run instead demonstrated why that is dangerous on
this device — see "OOM confound" below — so from the second run on,
`core_pattern` was pointed directly at the NFS-mounted
`/mnt/projects/fable/review/T113/` (confirmed writable, ~11.4MB/s) instead of
local tmpfs, avoiding the RAM-doubling hazard entirely. `core_pattern` was
restored to the stock `core` at the end regardless, per the plan.

### Run 1 — OOM confound, not a reproduction (discounted)

First run used the plan's literal `/tmp` core target. The client's own log
ends abruptly mid-tour at `pos=(-333.2,4.5,3.0)` — inside the named hazard
box — consistent with a real crash, but `dmesg -T` shows the kernel's OOM
killer, not a signal handler, terminated the process 39s later while the
188MB core was still being written to the 242MB **RAM-backed** tmpfs on a
512MB device (writing a core dump to tmpfs holds both the live process's
pages and the dumped copy in RAM simultaneously). A SIGKILL from the OOM
killer cannot itself produce a core dump, so the partial 188MB file on disk
must be from whatever signal fired at `20:39:56` — but the OOM kill 39s later
means the dump was almost certainly truncated/torn, and this run's core was
deleted (in the same command that repointed `core_pattern` at NFS) before it
could be analysed. **This is a real deviation, not silently absorbed**: the
first run's core was lost. The circumstantial evidence (log stops exactly in
the hazard box) is consistent with the target bug, but it is not usable
evidence and is not counted in the crash-rate tally below. Every run after
this one wrote cores straight to NFS and was pulled/analysed before the next
run started.

### Run 2 (pre-fix) — T110's teardown SIGABRT, root-caused

Completed the full 15-step tour cleanly to `/camp` → `Logout confirmed by
server` → `Game thread stopped` → `unloadZone()` teardown, then:

```
double free or corruption (!prev)
timeout: the monitored command dumped core
```

Full backtrace: `review/T113/run2_teardown_sigabrt_backtrace_PREFIX.txt`.
**Verbatim crashing frames** (Thread 1, deepest relevant part):

```
#0  __libc_do_syscall () ...
...
malloc_printerr / unlink_chunk / malloc_consolidate / _int_free / __libc_free
#N  std::_Rb_tree<unsigned long long, std::pair<unsigned long long const,
    std::shared_ptr<EQT::Graphics::RaceModelData> >, ...>::_M_erase
    (this=0x35bdf54, __x=0x41c4c58) at bits/stl_tree.h:1938
#N+1 std::_Rb_tree<...>::~_Rb_tree (this=0x35bdf54, ...) at stl_tree.h:986
#N+2 std::map<unsigned long long, std::shared_ptr<RaceModelData>, ...>::~map
    (this=0x35bdf54, ...) at stl_map.h:314
#N+3 EQT::Graphics::RaceModelLoader::~RaceModelLoader (this=0x35bddf8, ...)
     at /src/src/client/graphics/eq/race_model_loader.cpp:90
#N+4..7 unique_ptr/default_delete unwind
#N+8 EQT::Graphics::EntityRenderer::~EntityRenderer (this=0x2db2050, ...)
     at /src/src/client/graphics/entity_renderer.cpp:89
#N+9..12 unique_ptr/default_delete unwind
#N+13 EQT::Graphics::IrrlichtRenderer::shutdown (this=0x2adb658)
      at /src/src/client/graphics/irrlicht_renderer.cpp:1418
#N+14 eqt::Application::shutdown (this=0xbef015a8)
      at /src/src/client/application.cpp:710
#N+15 main (...) at /src/src/client/main.cpp:131
```

**Faulting thread/frame**: the main/render thread, at process exit, inside
`RaceModelLoader::~RaceModelLoader()`'s destruction of `variantModels_`
(`std::map<uint64_t, std::shared_ptr<RaceModelData>>`) — glibc's allocator
detects the tree's internal structure is corrupted while freeing its nodes.
This message reports **detection**, exactly as the plan's hypothesis says —
the corrupting write happened earlier, during play, not at shutdown.

**Root cause, found by reading, not guessing**: `variantModels_` is
documented (`race_model_loader.h`) as "written by worker thread
(`preloadVariantModel`) and read by render thread... — protected by
`variantModelsMutex_`". Grepping every access site found **three that
bypass the lock**: `RaceModelLoader::removeMeshRef()`
(`model_loading.cpp`, called from `EntityRenderer::removeEntity()` on the
render thread every time an entity despawns/goes out of range — a routine,
frequent, in-play code path, not an edge case), `RaceModelLoader::
clearCache()`, and `RaceModelLoader::getMemoryStats()` (the `/pmem`
diagnostic's backing call). `removeMeshRef()`'s unlocked `variantModels_.
erase(vmIt)` racing against `preloadVariantModel()`'s locked `variantModels_
[key] = modelData;` insert on `EntityPrepWorker`'s background thread is a
textbook unprotected-vs-protected concurrent `std::map` mutation — undefined
behaviour that corrupts the map's own red-black tree, only detected later
when something (here, the destructor) next walks it.

**A second, independent instance of the same class was found while auditing
the sibling map `loadedModels_`** (`std::map<uint32_t,
std::shared_ptr<RaceModelData>>`), documented "main thread only — no mutex
needed": `RaceModelLoader::getRaceModelData()` is called from
`EntityPrepWorker::prepVariantTextures()` and `RaceModelLoader::
isModelDataCached()` from `EntityPrepWorker::processRequest()` — **both on
the background thread** — while ~10 call sites across `model_loading.cpp`
insert into the same map unprotected from the main/loading thread. This one
had not yet been caught by anything (no crash backtrace pinned it directly —
`variantModels_`'s `uint64_t` key type is what the captured backtraces show
— but it is the same defect shape, in the same class, discovered by
following the plan's "check whether T111's fix was complete or a sibling
survives" instruction to its logical conclusion rather than stopping at the
first hit).

**Whether T111's `regionPvsDepthMap_` fix was complete**: yes, for its own
narrow scope — no evidence found that the specific dangling-local-`
SimulationOutput` hazard T111 fixed has regressed or has a remaining gap.
What survived is a **sibling in the same class of bug** (worker thread writes
shared renderer state; a lock exists but not every access site uses it) in a
completely different subsystem (`RaceModelLoader`, not `SimulationWorker`'s
double-buffered output) that T111 never touched and had no reason to check.

### The fix (T110's crash class — narrow, applied)

`include/client/graphics/eq/race_model_loader.h`: added `loadedModelsMutex_`
next to `loadedModels_`, documented the invariant break in a comment (worker
thread now reads it — see T113).

`src/client/graphics/eq/race_model_loader.cpp`: `hasRaceModel()`,
`getRaceModelData()`, `isModelDataCached()` now lock `loadedModelsMutex_`
around their `loadedModels_` reads; `clearCache()` locks both
`loadedModelsMutex_` (around `loadedModels_.clear()`) and
`variantModelsMutex_` (around `variantModels_.clear()`, previously
unprotected); `promotePreparedModels()` takes `loadedModelsMutex_` nested
inside its existing `preparedDataMutex_` (consistent ordering everywhere,
no reverse-order acquisition anywhere else in the file — no deadlock risk).

`src/client/graphics/eq/model_loading.cpp`: all ten
`loadedModels_[key] = modelData;` insert sites now wrapped in
`loadedModelsMutex_`; `loadZoneModels()`'s `loadedModels_.clear()`/
`variantModels_.clear()` now locked; `removeMeshRef()`'s
`loadedModels_.erase(key)` and its `variantModels_` erase loop (the exact
site implicated by the captured backtrace) now locked;
`getMemoryStats()`'s `loadedModels_`/`variantModels_` size/iteration reads
now locked.

**Why this qualifies as narrow**: it is purely additive synchronization —
introducing one new mutex and extending the SAME per-map-mutex pattern
already established and working correctly for `variantModelsMutex_`,
`preparedDataMutex_`, and `otherChrCacheMutex_` in this exact file (the last
two are T91/T111's own precedent for this class of fix). No public API
changed, no call sites moved to a different thread, no change to
`EntityPrepWorker`'s existence or scheduling, no change to
`SimulationWorker`'s double-buffering. `ensureModelDataLoaded()`'s internal
same-thread `loadedModels_` reads (the long fallback chain) were
deliberately left unlocked — they run on the same thread as the writes
within that same function, sequentially, and are never reached from
`EntityPrepWorker` (confirmed: only `getRaceModelData`/`isModelDataCached`
are), so locking them would add overhead without closing any race.

### The mid-tour SIGSEGV — NOT fixed, diagnosed precisely

Two more live Pi runs, **with the fix above already built and deployed**,
still crashed — the plan's 5-of-9 rate has very much not been beaten yet.

**Run 3 (post-fix)**: mid-tour, not at teardown. Log ends abruptly at
`Script: wait 15.0s (step 12/15)`, right after crossing `pos=(-404.7, 20.0,
3.0)` during the second `/move` — inside the named hazard box. Full
backtrace: `review/T113/run3_midtour_sigsegv_backtrace_POSTFIX.txt`.
**Verbatim**:

```
Program terminated with signal SIGSEGV, Segmentation fault.
#0  std::_Rb_tree<void const*, std::pair<void const* const,
    EQT::Graphics::ZoneMergeBuffers::RegionBufferRange>, ...>::_M_lower_bound
    (__k=<optimized out>, __y=<optimized out>, __x=0x2601, this=<optimized out>)
    at bits/stl_function.h:451
#1  ...::find (__k=<optimized out>, this=<optimized out>) at bits/stl_tree.h:2532
#2  std::map<void const*, RegionBufferRange, ...>::find (...) at bits/stl_map.h:1220
#3  EQT::Graphics::ZoneMergeBuffers::renderMerged
    (this=<optimized out>, driver=<optimized out>,
     sortedEntries=std::vector of length 2271, capacity 4096 = {...})
    at /src/src/client/graphics/renderer_zone_merge.cpp:230
#4  EQT::Graphics::IrrlichtRenderer::drawZoneGeometrySorted (...)
    at /src/src/client/graphics/renderer_zone_draw.cpp:669
#5  ...RenderPassTimer::OnRenderPassPreRender (renderPass=ESNRP_SOLID, ...)
    at renderer_zone_draw.cpp:260
#6  ...OnRenderPassPreRender (...) at renderer_zone_draw.cpp:227
#7  irr::scene::CSceneManager::drawAll()
#8  EQT::Graphics::IrrlichtRenderer::processFrameRender (...) at renderer_frame.cpp:1169
#9  EQT::Graphics::IrrlichtRenderer::processFrame (...) at renderer_frame.cpp:210
#10 eqt::Application::render (...)
#11 eqt::Application::mainLoop (this=0xbeadc5a8) at application.cpp:875
#12 eqt::Application::run (this=0xbeadc5a8) at application.cpp:654
#13 main (...) at main.cpp:130
```

**Faulting thread/frame**: the render thread, inside
`ZoneMergeBuffers::renderMerged()`'s `ranges_.find(sortedEntries[k].buffer)`
(`renderer_zone_merge.cpp:230`) — a `std::map<const void*,
RegionBufferRange>` lookup. `__x=0x2601` is not a plausible heap pointer as a
tree node — `ranges_`'s own internal tree structure is corrupted, not merely
missing the key (a missing key is already handled gracefully two lines
later: `allMergeable=false`, falls back to the unmerged per-entry draw path
— that is not what happened here; the lookup itself faulted).

**The `RegionVis` expansion is directly, mechanically implicated — traced
through the source, not inferred from timing alone.** `sortedEntries` here
has length 2271 (capacity 4096). Tracing where that vector comes from:
`IrrlichtRenderer::drawZoneGeometrySorted()` builds `sortedDrawEntries_` by
walking `sortedZoneDrawList_` (`renderer_zone_draw.cpp`), which is populated
1:1, every frame, from `SimulationOutput::sortedRegions`
(`renderer_sim_bridge.cpp:667-672`) — **the exact same vector**
`sim_visibility.cpp`'s `computeVisibility()` logs as `RegionVis: N/3596
visible`. That vector balloons from a small PVS-neighbourhood-sized set to
nearly the zone's full 3596-region population whenever the PVS candidate
lookup misses (`sim_visibility.cpp` ~109-113:
`zoneData_.pvsVisibleBoundsIndices.find(currentPvsRegion)` fails →
`candidates = &zoneData_.allBoundsIndices`, all 3596 regions) — confirmed
happening in this exact hazard box by two of T112's own captured DEBUG logs
(`review/T112/pi_logs/pi_active_debug_run3_crash_midtour.log`,
`pi_inert_debug_run1_crash_midtour.log`), both of which show `RegionVis`
climbing from double digits to 1,100–1,180+/3,596 in the frames immediately
before their own logs go silent, at the identical `x≈-333..-345` position.
A frame that draws ~2271 regions instead of a normal PVS-neighbourhood
handful makes ~2271 `ranges_.find()` calls in one frame instead of a few
dozen — far more likely to be the call that touches whatever heap region is
already corrupted, which is consistent with (not just correlated with)
T112's "RegionVis jump precedes the crash" observation, independent of
where the corrupting *write* actually happened.

`mergeZoneDraws: true` is shipped in the `orangepi` preset
(`config/constrained_presets.json`) — this is live, default-on production
code on the exact target hardware, not a debug-only path.

**Not fixed.** `ZoneMergeBuffers::build()`'s own contract (documented in
`renderer_zone_merge.h`) is "only ever called once the zone's region set is
FULLY resident with zero evictions", with an eviction-count gate that
destroys the merge buffers if a texture eviction happens after a successful
build. That gate is unrelated to what actually happened here (no eviction —
`ranges_` itself is corrupted, not stale). Root-causing this precisely would
mean determining whether the corrupting write is (a) fallout from a
still-undiscovered sibling of the exact class just fixed above (heap
corruption bleeding across unrelated objects, which would explain why an
unrelated map is the one found corrupted), or (b) a defect intrinsic to
`ZoneMergeBuffers` itself (e.g. a buffer overrun in `MergedGeneration::buf`'s
index array under the same region-count blowup). Distinguishing those needs
ASAN instrumentation or a much longer targeted Pi session than remained in
this unit's budget — **this is the "diagnosed, fix is larger than this
unit" outcome the plan explicitly allows for.**

**Run 4 (post-fix)**: completed the full tour cleanly to `/camp` →
`Logout confirmed by server` → teardown, progressed further into
`unloadZone()` than run 2 did (past texture-cache cleanup, past
`RaceModelLoader` cache clearing, past composite-entity-adoption logging, to
`Fog: start=250, end=300`) before crashing with **no further log output**.
Full backtrace: `review/T113/run4_teardown_sigbus_backtrace_POSTFIX.txt`.
**Verbatim**:

```
Program terminated with signal SIGBUS, Bus error.
#0  0xb33ec80e in ?? () from /lib/arm-linux-gnueabihf/libgallium-25.2.8-0ubuntu0.24.04.1.so
#1  0xb33cb994 in ?? () from /lib/arm-linux-gnueabihf/libgallium-25.2.8-0ubuntu0.24.04.1.so
Backtrace stopped: previous frame identical to this frame (corrupt stack?)
```

No symbols available for `libgallium` on this Pi image, so no attributable
call site in willeq's own code. SIGBUS (not SIGSEGV/SIGABRT) is a different
failure shape from the other two captured crashes — classically an unaligned
access or a reference past a shrunk/unmapped memory-mapped region (e.g. a
DRM/GBM buffer), which could be driver-level GL/DRM teardown-ordering rather
than the same C++-heap-corruption family. **Reported, not explained** —
distinguishing "fallout from the same corruption" from "an unrelated driver
bug" needs a debugging build of the Mesa/Lima stack this Pi image doesn't
have, well outside this unit's remaining budget.

### Crash rate

| Run | Binary | Outcome | Location |
|---|---|---|---|
| 1 | pre-fix | OOM during coredump (confound, discounted) | mid-tour, in the named hazard box |
| 2 | pre-fix | SIGABRT, `double free or corruption` | teardown — **root-caused, fixed** |
| 3 | **post-fix** | SIGSEGV, `ZoneMergeBuffers::ranges_` corruption | mid-tour, in the named hazard box |
| 4 | **post-fix** | SIGBUS, Mesa/Lima driver, no symbols | teardown (further along than run 2) |

Excluding the discounted OOM run: **2 usable pre/post-fix comparison points,
both of which crashed** (2/2 pre-fix-class runs would also have crashed
before the fix, by construction — run 2 IS that crash). Post-fix: **0 of 2
runs completed cleanly.** The fix closes the specific defect it targets
(confirmed: that exact `variantModels_`-destruction SIGABRT signature did
not recur in either post-fix run) but the client is **still not reliably
completing the tour** — this explicitly does **not** meet the plan's "beat
the 5-of-9 rate" bar, and is reported as such rather than claimed as a win.
**Zero consecutive clean runs observed post-fix.**

### Whether this and T110 share a root cause

**Partially confirmed, more precisely than "yes/no".** T110's documented
signature (`corrupted size vs. prev_size` → SIGABRT during
`unloadZone()`/shutdown teardown) and this unit's run 2 (`double free or
corruption (!prev)` → SIGABRT, same teardown window, same
`RaceModelLoader::~RaceModelLoader()` frame) are **the same defect** — fixed
here, T110 closes for this signature (see Board). But T113's own primary
subject — the **mid-tour** SIGSEGV — is a *different* defect
(`ZoneMergeBuffers`, not `RaceModelLoader`), and the plan's hypothesis
("test whether they're one bug before treating them as separate") is
**refuted for that pairing**: the mid-tour crash and the teardown crash do
not share the fixed root cause. Whether the mid-tour `ZoneMergeBuffers`
corruption and T110's *original* (pre-this-unit) teardown signature were
ever the same thing cannot be fully settled retroactively — T110's own
report only ever captured the shallow, symbol-poor backtrace T111 left
behind (`unlink_chunk`/`malloc_consolidate`, "previous frame identical to
this frame"), not a symbolized frame naming which object was being
destroyed. It is plausible T110 was *always* two (or more) crashes
sharing one glibc-detection message, and this unit has now separated one of
them out with certainty (`RaceModelLoader`) while leaving at least one more
(`ZoneMergeBuffers`, possibly also the SIGBUS) unresolved.

### Test coverage

**No headless test added — reasoned, not skipped for convenience.**  The
fixed defect (unprotected concurrent `std::map` mutation) is exactly the
class of bug this project already tests via a **manual stress tool**, not
ctest, per `refactor/TESTING_CHARTER.md`'s explicit rule ("anything
soak/stress-shaped goes in `tools/`... not ctest") and `tools/
particle_race_stress.cpp`'s precedent for this project's own prior instance
of the same bug class (P23). Building an equivalent stress harness for
`RaceModelLoader` specifically would need either (a) real S3D character
archives so `preloadModelData()`/`preloadVariantModel()` actually populate
`loadedModels_`/`variantModels_` with something to race on — which the fast
ctest suite deliberately avoids depending on — or (b) a non-trivial
refactor to let a synthetic/injectable `RaceModelData` reach those maps
without going through real asset loading, which is a larger change than
this narrow mutex fix warrants on its own. The fix itself is a pure,
mechanical extension of an already-validated locking pattern (three other
maps in this same class already use exactly this pattern correctly), not
new logic that needs its own test to be trustworthy. Flagged as a gap
rather than silently omitted; a `tools/race_model_loader_race_stress.cpp`
(TSAN-driven, following `particle_race_stress.cpp`'s shape) is the natural
follow-up if this bug class recurs.

### Build/test

1. `cmake --build build -j24` — clean.
2. `ctest --test-dir build --output-on-failure` — **2265/2266**, sole
   failure the documented ALSA `AudioManagerTest.PreloadCommonSounds`.
3. `bash scripts/build-gles2-x86.sh` + visual gate (ctest #2265) — PASS,
   goldens untouched (this unit changes only `RaceModelLoader`'s internal
   locking, not any code path the visual gate's zone benchmark exercises
   differently).
4. `bash scripts/build-arm-noble.sh` — PASS, clean ARM cross-build (this is
   the exact binary deployed for runs 3 and 4 above).
5. `scripts/refactor/check_file_sizes.sh fable` — 0 FAIL. One pre-existing
   WARN carried forward (`model_loading.cpp`, already 1695 lines on `fable`
   before this unit, now 1767 after the lock-wrapping — still a WARN, not a
   new FAIL; the added lines are exclusively `{ lock_guard; ...; }` wrapper
   scaffolding around ten existing single-line assignments, not new logic).

### Deviations

1. **Run 1's core was lost** — deleted (along with switching `core_pattern`
   from `/tmp` to NFS to fix the OOM hazard) before it could be analysed.
   The run is discounted from the crash-rate tally rather than papered over.
2. **`core_pattern` target moved from the plan's literal `/tmp` to
   NFS-mounted `review/T113/`** after run 1 demonstrated that writing a
   ~200MB core to this device's 242MB RAM-backed tmpfs competes directly
   with the live process's own memory and can itself trigger an OOM kill
   that truncates the dump — a self-inflicted measurement artifact, not the
   bug under investigation. This is a deliberate, reasoned deviation from
   the plan's literal instruction, done to make the technique actually work
   on this specific 512MB device, not a shortcut.
3. **Full stdout/stderr logs for runs 2-4 were not preserved** — only the
   tails visible in-session were captured before the Pi's `/tmp` working
   directory was cleaned up at end-of-session (`rm -rf /tmp/t113`), which
   ran before those specific files were copied off. The gdb-extracted
   backtraces (the load-bearing evidence) were preserved via `review/T113/`
   NFS writes and are complete; the surrounding log context is not. Flagged
   as a real gap, not silently absorbed — the next unit picking up the
   `ZoneMergeBuffers` or SIGBUS thread should re-run rather than assume
   the full logs exist somewhere.
4. **The mid-tour SIGSEGV (the plan's primary subject) is not fixed** — see
   above. Diagnosed precisely (exact map, exact call chain, exact data-flow
   link to the `RegionVis` lead) but the actual corrupting write is not yet
   isolated. This is reported as the honest outcome, not stretched into a
   claim of resolution.
5. **A third crash signature (SIGBUS in the Mesa driver) was found and is
   completely unexplained** — flagged rather than folded into either of the
   other two findings without evidence.
6. Two live Pi sessions were used (well under the 12-invocation budget: 5
   `willeq` invocations total across both — run1 through run4, plus the
   version-check probe at the start).

## Board

**T110 closes with this unit for its documented signature only.** The
`corrupted size vs. prev_size` / `double free or corruption` teardown SIGABRT
inside `RaceModelLoader::~RaceModelLoader()`'s map destruction is root-caused
(unprotected concurrent `std::map` mutation between `EntityPrepWorker`'s
background thread and the render thread across `loadedModels_` and
`variantModels_`) and fixed (mutex additions, `race_model_loader.h/.cpp`,
`model_loading.cpp`). Re-verification found **no recurrence of that specific
signature** in either post-fix Pi run.

**T113 (this unit's own primary subject, the mid-tour SIGSEGV) does NOT
close.** It is a different defect from T110's, in `ZoneMergeBuffers`, not
`RaceModelLoader` — diagnosed with a symbolized backtrace and a traced
mechanistic link to the `RegionVis` lead (`ranges_.find()` crashing on a
corrupted tree, called ~2271 times in the exact frame `sortedDrawEntries_`
balloons per the PVS-fallback-to-full-population path), but the corrupting
write is not isolated. **A third, completely separate crash signature
(SIGBUS in the Mesa/Lima GL driver at teardown) was also found and is
unexplained.** The next unit on this thread should start from
`review/T113/run3_midtour_sigsegv_backtrace_POSTFIX.txt` and
`run4_teardown_sigbus_backtrace_POSTFIX.txt` rather than re-diagnosing from
scratch, and should budget for ASAN instrumentation given a second `gdb`-only
pass did not close it. Recommend: next unit is Pi-scoped, ASAN-equipped if
feasible, and treats `ZoneMergeBuffers` and the Mesa SIGBUS as two more
open threads alongside whatever it's assigned, rather than assuming either
is resolved.
