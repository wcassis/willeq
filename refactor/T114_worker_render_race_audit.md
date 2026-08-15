# T114 — Find the whole class: races across the worker/render boundary

**Slot: whichever is free. x86 first — TSAN does not fit the Pi. Pi only if
Part 3 needs it. eqemu lock if a live session is required.**

**Territory: `tests/`, `tools/`, `refactor/`,
`/mnt/projects/fable/review/T114/`, plus whatever a confirmed race
implicates. Sanitizer build configuration is in scope.**

Read T113's Review first, then T111's and T112's.

## Why this unit exists — stop fixing them one at a time

Three crashes in three units, all the same shape:

- **T111**: dangling `regionPvsDepthMap_` pointer into a local
  `SimulationOutput` — fixed.
- **T113**: `RaceModelLoader::variantModels_`/`loadedModels_` accessed by the
  background prep worker and the main thread without the matching lock —
  fixed, and it explains T110's teardown abort.
- **T113, still open**: `ZoneMergeBuffers::ranges_` walked while already
  corrupted, crashing mid-play.

**All three are containers shared between the background prep worker and the
render thread without synchronisation.** T111 enabled sustained concurrent
background work for the first time and converted a latent class into live
crashes. At roughly one Pi session per crash, chasing them individually is the
slow way to find a class.

## Why TSAN on x86, not ASAN on the Pi

The defects are **data races**. ASAN finds memory corruption — the
*consequence*, at the point of damage. **TSAN finds the race itself**,
including instances that have not crashed yet, which is the point.

TSAN's memory overhead (roughly 5-10x) does not fit a 483MB Pi. x86 has the
headroom. **The races are in shared code** — they surface on the Pi because
four slow cores widen the windows, not because the code differs.

`libasan.so.8` is present on the Pi if Part 3 needs it, and P23 has precedent
for sanitizer work here (see `refactor/P23_particle_race_stability.md`'s
Review for the invocation it used — reuse anything applicable, including any
suppression file).

## Part 1 — Get TSAN running at all

This is the real risk. The races are between the prep worker and the **render**
thread, so graphics must be live — which means TSAN over llvmpipe, and
third-party noise from Irrlicht and Mesa.

- Build with `-fsanitize=thread` and get the client to run a zone-in.
- **Expect a suppression file** for third-party frames. Suppress by library,
  never by our own symbols — a suppression that hides our code defeats the
  unit.
- **State what you suppressed and why.** An unreviewable suppression list is
  how a sanitizer run becomes theatre.

**If TSAN cannot be made to run** — build failure, unusable noise, or it will
not complete a zone-in — say so precisely and fall back to Part 3. That is a
legitimate outcome, not a failure.

## Part 2 — The inventory is the deliverable

**Do not stop at the first race.** Produce a list of every unsynchronised
container or shared object crossing the worker/render boundary, each with:

- what it is, and which threads touch it
- the access pattern (write/read, read/read, iterate-while-mutated)
- whether TSAN flagged it or you found it by reading
- whether it has crashed in the wild yet

The known-open one — `ZoneMergeBuffers::ranges_` — should appear. If it does
not, that is important: it would mean TSAN is not reaching the path, and the
inventory is incomplete rather than clean.

Cross-check against the fixes T111 and T113 already made: **confirm they hold
under TSAN**, since both were reasoned rather than sanitizer-verified.

## Part 3 — Fix only what is unambiguous

Fix races that are clearly unsynchronised access to a shared container, using
the **existing per-container mutex pattern** T91, T111 and T113 all followed.
Additive locking only.

**STOP and report** if a fix requires changing the threading model, the
ownership of a subsystem, or lock ordering across subsystems. Deadlock
introduced while fixing a race is a worse outcome than the race.

If TSAN was unusable, use ASAN on the Pi to close `ZoneMergeBuffers::ranges_`
specifically — it fits (client 92.9MB, ~2x overhead, ~395MB headroom) and
`libasan.so.8` is installed.

## Verification — the honest bar

- x86 suite (current total 2266; sole pre-existing failure the documented ALSA
  `AudioManagerTest.PreloadCommonSounds`) plus ARM cross-compile.
- **Then run the Pi tour.** T113 got 0 of 2 clean. **State how many consecutive
  clean Pi runs you observe** — that number, not the sanitizer output, is
  whether the platform is fixed. A clean TSAN report with a still-crashing Pi
  means the inventory missed something.
- Visual gate — goldens must not move from added locking.

Session discipline: binary freshness on both machines; **Pi cores go to NFS,
never `/tmp`** — T113 OOM'd the Pi writing a core to its 242MB tmpfs; restore
any sysctl you change; `--log-level=INFO` on the Pi, since T112 measured DEBUG
as destabilising; kill clients by real binary PID; leave the Pi pristine.

## What you may not do

- Do not suppress our own symbols in TSAN.
- Do not fix a race by removing the concurrency (e.g. making the worker
  synchronous). That would undo T111 and hide the class rather than close it.
- Do not claim the platform is stable on sanitizer output alone.

## Review

Executed in `/mnt/projects/fable/wt/slot1`, task branch `task/T114-race-audit`,
base fable tip `6fba2451`. Live eqemu (lock free at session start; taken,
used, released cleanly) and live Orange Pi (deployed, exercised, left
pristine — `core_pattern` restored to `core`, `/tmp` 0% used, governor
`ondemand`) both used. Full logs, backtraces, TSAN reports, README:
`/mnt/projects/fable/review/T114/`.

**Headline: TSAN ran for real, over the actual GLES2/llvmpipe render path,
through a full zone-in and movement tour, and — after fixing a real,
project-code race in the logging subsystem that was drowning everything else
out — surfaced zero races in `RaceModelLoader`/`WilleqDatabase`/
`SimulationWorker`, confirming T111's and T113's earlier fixes hold and this
unit's own two new fixes are sound. Live Pi testing (the honest bar, not the
sanitizer) found a THIRD sibling of the T111/T113 class — a genuinely new,
previously-undocumented race in `RaceModelLoader`'s animated-mesh scratch
buffers — root-caused via a Pi core dump, fixed, and confirmed not to recur.
But T113's still-open `ZoneMergeBuffers::ranges_` mid-tour SIGSEGV persisted
even after every fix in this unit: 2 of 3 post-fix Pi runs hit it, byte-for-
byte identical to T113's signature. It remains open. The inventory is
therefore not clean, and this unit says so rather than rounding up.**

### Part 1 — TSAN: got running, at real cost

Configured a parallel `build-tsan/` tree (`-fsanitize=thread -g -O1
-fno-omit-frame-pointer`, reusing the already-bootstrapped
`third_party/irrlicht-gles2-x86/lib/libIrrlicht.a` unmodified — Irrlicht
itself was not recompiled under TSAN, which is normal: TSAN still tracks
pthread/mutex operations in uninstrumented code via libc interposition, it
just can't see *memory* races purely inside Irrlicht/Mesa, which is exactly
the "third-party noise" the plan anticipated). Two real obstacles, both
solved:

1. **ASLR incompatibility**: TSAN's shadow-memory layout doesn't tolerate
   this host's default ASLR (`FATAL: ThreadSanitizer: unexpected memory
   mapping`). Fixed per P23's documented precedent:
   `setarch $(uname -m) -R <binary>`.
2. **Relative-path config resolution**: the binary resolves
   `config/*.json`/`data/*` against CWD, not an installed prefix, so every
   invocation runs via a wrapper script that `cd`s into the worktree first
   (script contents, not a chained shell command — matches the executor
   protocol's `cd` exemption).

Cost: TSAN's own overhead made the client noticeably slower (average ~5-7fps
vs the normal dev-VM llvmpipe baseline; one frame during heavy scene-graph
work hit 81 seconds) but it completed full zone-ins, movement tours, and
clean shutdowns repeatedly — TSAN did not merely "start", it ran a real
session end to end multiple times. **Part 1's condition is met: TSAN runs.**

### Suppression list

`review/T114/tsan_suppressions.txt` — **exactly one entry**:
`race:libgallium*.so*`. Justification: 2 of the 633 warnings in the first
TSAN run were entirely inside Mesa's llvmpipe/gallium software rasterizer
(no willeq frame anywhere in either thread's stack — llvmpipe runs its own
internal tile-rendering worker pool with its own synchronization this
project has no visibility into or responsibility for). **No willeq symbol is
suppressed anywhere.** This is the only suppression in the file; it was not
needed to reach a usable report (the dominant noise source, covered next,
was fixed instead of suppressed).

### The real Part-1 risk: our own code drowned the signal, and had to be fixed to see anything

The first TSAN run produced **633 warnings**. Investigating the top of the
frequency table (not the bottom) found that **~90% of them (568) were one
single race**, all sharing the same two stack shapes: `ThreadLogBuffer::
append()` (`include/common/logging.h`) vs `LogManager::drainAllBuffers()`,
both touching the same `std::string` buffer without synchronization. This is
own project code, not noise to suppress (the plan explicitly forbids
suppressing our own symbols) — and it was a **genuine, confirmed bug**, not
a false positive: `ThreadLogBuffer::flush()`'s "is the drain side already
empty" peek at `buffers[1-old]`/`errBuffers[1-old]` ran with zero
synchronization against `LogManager::drainAllBuffers()`'s concurrent read
and `.clear()` of the exact same strings on the dedicated log-drain thread.
The class's own doc comments call this design "lock-free" and explain why
that's safe for the *active* side (only the owning thread ever touches it) —
but the peek at the *inactive* side, which the drain thread does touch, was
never covered by that reasoning.

**Fixed** (`include/common/logging.h`): added one `std::mutex sideMutex`
per `ThreadLogBuffer` (thread-local, so contention is only ever between that
one buffer's owning thread and the single shared drain thread, never across
unrelated threads). `flush()`'s peek-and-flip and `drainAllBuffers()`'s
read-index-and-touch-inactive-side are now both done under the *same*
per-buffer mutex; `append()`'s hot-path writes to the *active* side remain
completely lock-free, unchanged. This is technically outside the plan's
named territory (logging, not graphics), but it directly implicated itself
by making the rest of the audit impossible to read — same "required
infrastructure, landed regardless" reasoning T109/T111 used for their own
out-of-territory fixes.

Re-running after this fix cut the count from 633 to **32** warnings — real
signal at last. See `review/T114/x86_tsan_run1.log` (before) vs
`x86_tsan_run2.log` (after) and `tsan_run2_summary.txt` for exactly which 32
remained.

### Part 2 — the inventory (the deliverable)

| # | Container | Threads | Access pattern | Found by | Crashed in the wild? | Status |
|---|---|---|---|---|---|---|
| 1 | `RaceModelLoader::variantModels_` (T113) | worker (`preloadVariantModel`) / render (`removeMeshRef`, `clearCache`, `getMemoryStats`) | write/erase-while-inserting | T113, crash-driven | Yes — T110's SIGABRT | **T113 fix holds under TSAN** (0 races this session, x86 or Pi) |
| 2 | `RaceModelLoader::loadedModels_` (T113) | worker (reads via `getRaceModelData`/`isModelDataCached`) / main (inserts) | read/write | T113, reading | No (latent) | **T113 fix holds** |
| 3 | `WilleqDatabase`'s per-query cached `sqlite3_stmt*` | worker (`EntityPrepWorker`'s equipment/race-code queries) / main+render+audio threads (many query sites) | interleaved `reset+bind+step` on one shared handle, no lock at all | Reading (audit), not crash-driven | Not confirmed crashed (would silently cross-contaminate query results, not crash) | **FIXED this unit** — one mutex per connection, held per query |
| 4 | `RaceModelLoader::otherChrCaches_` | worker (`preloadModelData`/`preloadVariantModel`, via `storeOtherChrCacheLocked`) / main (10+ raw insert/read sites in `model_loading.cpp`, unlocked) | insert-while-reading, erase (LRU eviction)-while-reading | Reading (audit), sibling of #1/#2 | Not directly pinned to a backtrace, but the exact class T110's crash was in | **FIXED this unit** — every site now under `otherChrCacheMutex_` |
| 5 | `RaceModelLoader::getLoadedModelCount()` | worker / render (`/pmem`) | unlocked `size()` read while worker inserts | Reading (audit) | No | **FIXED this unit** — added the missing lock |
| 6 | `RaceModelLoader::originalVerticesForAnimation_` / `vertexMappingForAnimation_` | worker (`preloadVariantModel`, `variant_loading.cpp`) / render (`getAnimatedMeshForRace`, `animated_mesh_creation.cpp`) | write-then-immediately-read (build-then-consume), zero synchronization at all | **Live Pi crash** (teardown SIGABRT) | **Yes** — the crash that led directly to this fix | **FIXED this unit** — `animScratchMutex_` held across the whole build-then-consume sequence at both call sites |
| 7 | `RaceModelLoader::ensureModelDataLoaded()`'s ~20 unlocked `loadedModels_.find()` reads | main/loading thread only (confirmed: `Application::render()` early-returns during loading, so this and any worker access are mutually exclusive today) | reads bypass `loadedModelsMutex_` | Reading (audit) | No — currently unreachable concurrently | **NOT fixed** — see "What was deliberately not fixed" |
| 8 | `EquipmentModelLoader::archivesLoaded_`/`mappingLoaded_` | worker (`EntityPrepWorker::prepEquipmentModels`) / main (`loadEquipmentModelOnDemand`) | double-checked-locking fast path reads a plain, non-atomic `bool` outside the lock | Reading (audit) | No (unlikely to tear on this arch, but UB) | **FIXED this unit** — `std::atomic<bool>` |
| 9 | **`ZoneMergeBuffers::ranges_`/`buckets_`** (T113, named as the bar for this audit) | **render thread only** — confirmed by tracing every caller of `build()`/`renderMerged()`/`destroy()` (all inside `drawZoneGeometrySorted()`, `unloadZone()`, `/mergedraws`, all render-thread-only call sites) | N/A — not itself a race | Code audit + TSAN (see below) | **Yes — still crashing** | **STILL OPEN, NOT FIXED** |
| 10 | `RendererEventReceiver`'s ~15 "read-and-reset" fields (`enterKeyPressed_`, `slashKeyPressed_`, `escapeKeyPressed_`, `spellGemCastRequest_`, `hotbarActivationRequest_`, mouse-click flags, …) | render thread (writes in `processChatInput()`) / game thread (`GraphicsInputHandler::updateFromEventReceiver()`) | plain-bool/int8_t write-then-read-and-reset, zero synchronization | **TSAN**, this session | Not confirmed (subtle: a lost/duplicated keypress, not a crash) | **NOT fixed** — out of this unit's worker/render charter, see below |
| 11 | `AudioMixer`'s per-voice active flag | audio callback thread (`miniaudioDataCallback`) / game thread (`SfxManager::play`) | plain-bool write/read | **TSAN**, this session | Not confirmed | **NOT fixed** — different subsystem boundary (audio/game, not worker/render), see below |
| 12 | `GraphicalMode::requestQuit()`'s quit flag | main thread / game thread, at shutdown only | plain-bool write/read | **TSAN**, this session (1 occurrence) | No | **NOT fixed** — teardown-only, minor, reported |
| 13 | `SimulationWorker::input_`/`output_[2]` (P23) | worker / render | double-buffered, mutex + condvar gated | Not re-audited from scratch per the plan's instruction (already reviewed) | N/A | Header confirms the P23 fix is still in place; **0 TSAN races in `simulation_worker.cpp`/`sim_particles.cpp` this session** |
| 14 | `regionMeshNodes_` | render/loading thread only — confirmed `GPUUploadThread`'s own worker thread never touches it directly (only render-thread code drains its result queue and integrates into `regionMeshNodes_`) | N/A — not a race | Code audit | N/A | **Confirmed safe** |
| 15 | `background_work_queue.h`/`background_thread_pool.h`/`GPUUploadThread` request/result queues | correctly `std::mutex`-guarded at every access site checked | — | Code audit | — | **Confirmed safe** |
| 16 | `ConstrainedTextureCache` | `getOrLoad()` (CPU-side decode, safe from any thread, `mutex_`-guarded) vs `uploadTexture()` (GL calls, render/loading-thread only by contract) | correctly split by design | Code audit | — | **Confirmed safe** |

(#3–#8 found by a dedicated code-reading audit subagent tasked with
exhaustively tracing every worker-thread class in the codebase, not just the
ones named in prior reviews — full agent report available in this session's
transcript; #6, #10, #11, #12 additionally/independently confirmed or found
live by TSAN and the Pi.)

**`ZoneMergeBuffers::ranges_` DID appear** — as the plan required checking.
Per the plan's own framing ("If it does not, that is important: it would
mean TSAN is not reaching the path, and the inventory is incomplete rather
than clean"), the inverted case is also informative and is reported
precisely: **TSAN did reach `ZoneMergeBuffers::build()`/`renderMerged()`
repeatedly on x86** (confirmed via `[mergedraws]` log lines showing 9 texture
buckets built and multiple successful 80-90%-draw-call-reduction merged
frames during the TSAN run) **and found zero races there** — consistent
with the code-audit finding that every caller of this class is render-thread
only. The crash is real (confirmed live on the Pi, byte-identical to T113's
signature, `__x=0x2601`, `ranges_.find()` on a corrupted tree) but it is
**not itself a threading race inside `ZoneMergeBuffers`** — exactly the "(a)
fallout from a sibling of the class just fixed" hypothesis T113 raised as
the more likely of its two candidate explanations. This unit fixed two such
siblings (#4 and #6) and the crash still recurred (2 of 3 post-fix Pi
runs) — meaning either a third, still-undiscovered corrupting write exists
somewhere in the heap, or #9 is a defect intrinsic to `ZoneMergeBuffers`
itself (a buffer overrun, not a race) that TSAN cannot find because it isn't
a threading defect. **This is the honest, disappointing result the plan
explicitly asked to be stated rather than rounded up.**

### T111's and T113's fixes: hold under TSAN

- T111's `regionPvsDepthMap_` null-out fix: no TSAN warning anywhere near
  `getRegionPvsDepth()`/`SimulationOutput` in any of the three x86 TSAN
  runs, across sessions that ran the progressive-loading path repeatedly.
- T111's `SQLITE_OPEN_FULLMUTEX` fix: superseded in practical effect by this
  unit's own `WilleqDatabase` mutex (item #3 above), which subsumes it; no
  SQLite-related TSAN warning in any run.
- T113's `variantModels_`/`loadedModels_` mutex fix (items #1/#2 above):
  zero TSAN warnings against either map in any x86 run, and zero
  corresponding crash signature across 4 Pi runs (the `RaceModelLoader::
  ~RaceModelLoader()` crashes captured this session were in a *different*
  member — `vertexMappingForAnimation_`, item #6 — not a regression of
  T113's fix).

**All three prior fixes hold.**

### Part 3 — what was fixed and why each qualified

Items #3, #4, #5, #6, #8 above (`WilleqDatabase`, `otherChrCaches_`,
`getLoadedModelCount()`, `originalVerticesForAnimation_`/
`vertexMappingForAnimation_`, `EquipmentModelLoader`'s double-checked-locking
bools) — every one is **purely additive synchronization**, extending the
exact per-container-mutex pattern this codebase already established and
proved correct in T91/T111/T113 (one mutex per shared container/resource,
held for the container's own critical sections, never introducing a new
lock-ordering relationship with an existing mutex — checked explicitly for
each: `animScratchMutex_` is never held while any other RaceModelLoader
mutex is held and vice versa; `otherChrCacheMutex_`'s new sites never nest
inside `loadedModelsMutex_` in the wrong order; `WilleqDatabase::dbMutex_`
is a single new mutex with no pre-existing lock to conflict with). No public
API changed, no call site moved to a different thread, no change to
`EntityPrepWorker`'s or `SimulationWorker`'s scheduling.

**Item #6 is the standout finding**: unlike the others (found by reading),
this one was root-caused from a live Pi crash captured mid-session — the
clearest possible evidence a fix is real. `RaceModelLoader::
~RaceModelLoader()`'s teardown SIGABRT (`double free or corruption (!prev)`)
pointed straight at `vertexMappingForAnimation_`'s destructor; tracing every
write site to that member (and its `originalVerticesForAnimation_` sibling)
found exactly two, one on the render thread (`getAnimatedMeshForRace`) and
one on `EntityPrepWorker`'s background thread (`preloadVariantModel`), with
zero synchronization. Fixed with one new mutex held across the full
build-then-consume sequence (write, then immediate read) at both sites —
not just the individual field accesses, since a concurrent write landing
between the build and the read would still corrupt it.

### What was deliberately NOT fixed, and why (STOP conditions applied)

- **Item #7** (`ensureModelDataLoaded()`'s unlocked reads): a real bypass of
  the class's own documented invariant, but confirmed **not currently live**
  (the only concurrent writer, `EntityPrepWorker`, never writes
  `loadedModels_` directly — only the separately-locked `preparedModelData_`
  staging map, promoted on the same thread `ensureModelDataLoaded()` runs
  on). The narrow fix (wrap every read in `loadedModelsMutex_`) is not safe
  to apply naively: `ensureModelDataLoaded()` calls `loadModelFromZoneChr()`/
  `loadModelFromGlobalChr()`/etc., which **already acquire
  `loadedModelsMutex_` internally** — a blanket lock around the whole
  function would self-deadlock on re-entry through those calls (the same
  class of hazard the `RegionMeshTextureGuard` comment in
  `renderer_zone_merge.h` documents finding and fixing in a prior unit).
  Making `loadedModelsMutex_` recursive to route around this would be
  exactly the kind of "requires changing the threading model" the plan says
  to STOP on. Reported precisely, not fixed, so the next unit does not
  start over.
- **Items #10/#11** (`RendererEventReceiver`, `AudioMixer`): real,
  TSAN-confirmed races, but in different thread-pair boundaries than this
  unit's charter (game↔render input state, and audio-callback↔game audio
  state — neither is prep-worker↔render). Each recurs across ~10+
  similarly-shaped fields, meaning a correct fix is a wider design decision
  (converting a whole class to atomics, or introducing a mutex into a
  currently lock-free audio callback) rather than "add a mutex to one
  container." Recorded in `deferred.md` as follow-up units.
- **Item #12** (`GraphicalMode::requestQuit()`): one TSAN hit, teardown-only,
  minor. Reported, not chased further given the session's remaining budget.

### Verification

| Step | Result |
|---|---|
| 1. `cmake --build build -j24` | PASS |
| 2. `ctest --test-dir build --output-on-failure` | **2265/2266**, sole failure the documented ALSA `AudioManagerTest.PreloadCommonSounds`. `visual_gate` PASS within the suite. |
| 3. `bash scripts/build-gles2-x86.sh` + visual gate | PASS, goldens untouched (no rendering-visible change — all fixes are locking only) |
| 4. `bash scripts/build-arm-noble.sh` | PASS |
| 5. `scripts/refactor/check_file_sizes.sh fable` | 0 FAIL; one pre-existing WARN (`model_loading.cpp`, already large before this unit's additive locking added ~120 lines to it) |
| 6. x86 TSAN build (`build-tsan/`, separate tree, `-fsanitize=thread`) | Builds clean, runs a full zone-in + tour + shutdown repeatably |

### Pi tour — the honest bar

4 live Pi runs total, same script every time
(`review/T114/tour_pi.txt` — T109's own proven-safe 2-waypoint Befallen
tour, unmodified, matching T111/T112/T113's precedent of reusing exactly the
same inputs so "same conditions" is a file-identity guarantee):

| Run | Build | Outcome |
|---|---|---|
| 1 | pre-fix (this unit's build before the `animScratchMutex_` fix landed) | Teardown SIGABRT, `vertexMappingForAnimation_` — led to the fix |
| 2 | post-fix | **Clean** — full tour + camp + logout + shutdown, `EXIT_CODE=0` |
| 3 | post-fix | Mid-tour SIGSEGV, `ZoneMergeBuffers::ranges_`, step 11/15 (`/move -700 20 3`) |
| 4 | post-fix | Mid-tour SIGSEGV again, identical signature, same step |

**Consecutive clean Pi runs with every fix in this unit applied: 1 (out of
3 post-fix runs).** This does not meet a "fixed" bar. `ZoneMergeBuffers::
ranges_` remains open exactly as T113 left it, now with two more
data points (both crashes) and the two most likely sibling causes (#4, #6)
ruled out.

### Deviations

1. **The LogManager/`ThreadLogBuffer` fix is outside the plan's named
   territory** (logging, not graphics) — justified above: it was blocking
   the audit itself from producing readable signal, the same "required
   infrastructure, landed regardless" reasoning prior units used.
2. **`tsan_report.2347584` (the raw TSAN report file from run 2) was deleted**
   by this session's own `rm -f tsan_report.*` before the third run, before
   being archived to `review/T114/`. Not silently absorbed:
   `tsan_run2_summary.txt` preserves the verbatim SUMMARY lines and two full
   example reports captured in-session before the deletion.
3. **`ZoneMergeBuffers::ranges_` (T113's named target) is NOT closed by this
   unit.** Two real, additive fixes were found and landed that are the most
   likely sibling-corruption candidates T113 named, and the crash still
   recurred at the same rate order-of-magnitude as T112's 5-of-9. The next
   unit on this thread should budget for ASAN instrumentation specifically
   (per the plan's own Part 3 fallback, and per T113's prior recommendation)
   rather than another TSAN/gdb pass — a third pass with the same tools that
   already ruled out the two most likely causes is unlikely to find a third
   without different instrumentation.
4. **Items #10/#11/#12 (RendererEventReceiver, AudioMixer, GraphicalMode)
   found by TSAN but not fixed** — real races, outside this unit's
   worker/render charter, recorded in `deferred.md` as follow-up units
   rather than expanded into ad hoc here.

## Board

**The inventory (16 items above) is the map of this defect class and should
outlive the individual fixes made here** — recorded in full in
`refactor/deferred.md`. Five containers were fixed this unit (#3 `WilleqDatabase`
statement cache, #4 `otherChrCaches_`, #5 `getLoadedModelCount()`, #6
`originalVerticesForAnimation_`/`vertexMappingForAnimation_`, #8
`EquipmentModelLoader`'s double-checked-locking bools). One item (#7) was
found and deliberately left unfixed with a documented deadlock hazard.
Three items (#10, #11, #12) were found by TSAN, are real, and are follow-up
units. `ZoneMergeBuffers::ranges_` (#9) remains the one named target this
unit did not close — diagnosed further (two likely sibling causes
eliminated), not fixed.
