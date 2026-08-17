# T127 — Memory growth across repeated zone crossings

**Slot: whichever is free. x86 only. eqemu lock required. No Pi.**

**Territory: zone unload/load teardown paths, the persistent caches,
memory diagnostics (`renderer_diagnostics.cpp`), `tests/`, `refactor/`,
`/mnt/projects/fable/review/T127/`.**

Read T125's Review first (Tour B's OOM kills), then T106's (the untracked-
memory attribution work) and T108's (mapped-vs-resident).

## It already reproduces on x86 — you do not need the Pi

T125 could not complete a Pi zone-crossing tour because **every attempt was
killed by the kernel OOM killer**, confirmed from `dmesg`. That was read as a
Pi resource limit. **It is not Pi-specific.** The same growth is plainly
visible in T120's existing x86 logs
(`review/T120/baseline_x86/run1.log`, 61 `Memory: RSS` samples):

```
RSS first sample : 549.6 MB
RSS last sample  : 856.5 MB
growth           : +306.9 MB (1.6x) over one crossing tour
```

and it is **stepwise at each zone transition**, not gradual:
`549.8 → 551.2 → 575.6 → 724.1 → … → 856.5`. Roughly **75–150 MB per
crossing**, which on a 512 MB machine is fatal after one or two — exactly what
T125 observed.

**So this is an x86-reproducible bug, cheap to iterate on.** Start from
T120's existing tour and runner; a Pi run adds nothing this unit needs.

## The decisive clue: the tracked categories do NOT move

In the same log, across a crossing that added ~175 MB of RSS, the tracked
accounting is **flat**:

```
before: [Zone Geometry] 1.2 MB (1898 regions, 32491V/49989I)   Geometries: 1915
after : [Zone Geometry] 1.2 MB (1898 regions, 32491V/49989I)   Geometries: 1915
```

**RSS grows by hundreds of megabytes while every tracked category stays
identical.** Whatever is growing is *not* in the memory accounting at all —
which is why the memory gate and the `/pmem` breakdown have never shown this.
This is the same blind spot T106 investigated for untracked memory; re-read it
before assuming the accounting is trustworthy here.

## Establish the shape before hunting

**Leak (unreachable) and unbounded retention (reachable but never released)
both cause this OOM, and they need different fixes.** Distinguish them
explicitly and early:

1. **LeakSanitizer first — cheapest.** An ASAN build with `detect_leaks=1`
   over the crossing tour, exiting cleanly via `/camp`. If it names
   unreachable allocations, you have a true leak and probably the answer.
2. **If LSAN is quiet, it is retention, not a leak** — memory still reachable
   from a live cache or container. Then use an allocation profiler
   (`heaptrack`, or massif) to attribute growth by call site regardless of
   reachability. Say which you used.

Do not skip step 1 because you suspect a cache; a clean LSAN result is itself
strong evidence and takes minutes.

## Prime suspects — verify, do not assume

- **`EquipmentModelLoader::meshCache_`** — T126 established this is a
  **persistent cross-zone** cache. Persistent by design; the question is
  whether it is *bounded*. An unbounded cross-zone mesh cache is exactly this
  bug's shape.
- **`RaceModelLoader`'s model/animation caches** — same question.
- **`ConstrainedTextureCache`** — bounded by budget (`Evictions: 26` appears in
  this very log), so probably not the growth, but confirm rather than assume.
- **BSP/PVS data** — the log shows a `BSP tree: 2.6 MB … PVS 2.0 MB` line
  appearing after a crossing. Small, but check whether prior zones' trees are
  released.
- **Anything allocated per zone load and not freed by `unloadZone()`.** The
  stepwise-at-transition profile points squarely here.

## Fixing

Fix the narrowest thing that stops the growth. Likely shapes: bound a cache
with an eviction policy, or release a per-zone structure in `unloadZone()`.

**STOP and report** if the fix requires changing texture or mesh ownership,
the zone load/unload lifecycle, or cache architecture beyond adding a bound.
Those are their own units.

**If the growth is a deliberate, load-bearing cache** (e.g. cross-zone
equipment meshes exist to avoid re-extraction cost), **do not silently delete
it.** Say what it buys, what bounding it would cost, and let that be a
decision rather than a side effect. Report it and stop if the tradeoff is not
obvious.

**Do not paper over**: no periodic "clear everything" hack, no lowering a
budget to mask growth.

## Verification — the number is RSS growth per crossing

- **Primary: RSS growth across N crossings on x86**, before and after, from
  the same `/pmem` sampling. State MB per crossing and the total over the
  tour, against the current **+306.9 MB / 1.6x** baseline. A flat or
  near-flat profile is the target.
- **If you fix it, also fix the accounting**: whatever was growing untracked
  should become visible in `/pmem`, so the next person sees it. That is part
  of the fix, not a nicety.
- x86 suite (current total **2284**; sole failure the documented ALSA
  `AudioManagerTest.PreloadCommonSounds`) plus ARM cross-compile.
- **A test is required** if you bound a cache — assert the bound holds across
  simulated repeated loads.
- **Visual gate goldens must not move.** Releasing something still needed
  would show up here; if a golden moves, **STOP and report**.
- **No Pi run.** A Pi confirmation (does Tour B now complete?) is the natural
  follow-up unit and closes T125's open leg.

## Review

Executed in `/mnt/projects/fable/wt/slot1`, task branch
`task/T127-crossing-leak`, base fable tip `bc9b6bbd`. eqemu lock taken twice
(once for the tour runs, released; re-taken briefly to restore Testchar's DB
position after the tour's `/move`s moved it, then released again — confirmed
free both times, confirmed gone both times). Full logs, ASAN/LSAN reports,
heaptrack capture, and RSS series are in `/mnt/projects/fable/review/T127/`.
**No Pi run**, per the plan.

**Headline: two independent, additive fixes cut a 6-crossing tour's RSS
growth from +306.8MB (1.61x) to +42.0MB (1.09x) — a 7.3x reduction — and
the +42.0MB that remains is now almost entirely a legitimate one-time cost
of the bigger of the two zones, not growth: crossings 2-6 each add
0.0-0.1MB, only crossing 1 (befallen, 1898 regions -> qeynos2, 3596
regions) adds anything (+36.1MB).**

### Step 1 — LSAN: NOT quiet, but the leak it found is a small fraction of the RSS growth

ASAN build (`build-asan/`, `-fsanitize=address -fsanitize-recover=address -g
-O1 -fno-omit-frame-pointer`, `EQT_GLES2_IRRLICHT_DIR` pointed at the
existing patched Irrlicht, `EQT_BUILD_TESTS=OFF`), `ASAN_OPTIONS=
"detect_leaks=1 halt_on_error=0 abort_on_error=0 fsanitize-recover=1"`, run
over T120's exact amplified tour (3 round trips, 6 crossings,
`review/T127/tour_zone_cross_x86_amplified.txt`, copied verbatim from
`review/T120/`), exiting cleanly via `/camp` (`Application shutdown
complete` both pre- and post-fix runs — confirmed, not assumed).

**Pre-fix: `SUMMARY: AddressSanitizer: 88870444 byte(s) leaked in 291067
allocation(s)`** (`review/T127/lsan/asan_report_run1.3202061`). Attributed
by call site (script in this Review's evidence, output in
`review/T127/lsan/`): the two largest categories by far were
`EQAnimatedMeshSceneNode::createInstanceMesh()` (16.89MB, 25,752 objects)
and `RaceModelLoader` node-factory lambdas (11.97MB, 80,961 objects) —
per-entity animated-mesh scene-node construction, exactly the code path
that runs once per entity per zone load.

This is real, unreachable, LeakSanitizer-confirmed memory — not a live
cache the sanitizer can't see through. Per the plan: LSAN was not quiet, so
this is leak territory, not retention territory, for this component.

### The root cause: `RaceModelLoader`'s node-factory functions never balanced Irrlicht's own auto-grab()

Traced (details in a companion investigation, corroborated by direct code
reading before the fix was applied): `RaceModelLoader::createAnimatedNode`,
`createAnimatedNodeWithAppearance`, and `createAnimatedNodeWithEquipment`
(`src/client/graphics/eq/animated_mesh_creation.cpp`) each do
`new EQAnimatedMeshSceneNode(mesh, parentNode, ...)` and return the raw
pointer. `EQAnimatedMeshSceneNode`'s constructor forwards `parentNode` into
Irrlicht's `ISceneNode` base constructor, which auto-registers with the
parent via `addChild()` — and `addChild()` itself calls `grab()` on the new
node. Stock Irrlicht convention (every `addXSceneNode()` factory in the
engine) is: construct, configure, then `drop()` the factory's own `new`-
acquired reference before returning, because the parent's `grab()` is meant
to be the *sole* owning reference. `animated_mesh_creation.cpp` never did
this, so every constructed node sat at refcount 2 from birth. `EntityRenderer`
(`entity_renderer.cpp`) already does the textbook-correct
`sceneNode->remove(); sceneNode->drop();` teardown on entity removal and zone
unload (`clearEntities()`, called from `unloadZone()` before the next
zone's entities are built) — but that correct two-step teardown can only
bring refcount 2 back to 1, never to 0, so `~EQAnimatedMeshSceneNode()` (and
the `instanceMesh_`/`eqMesh_` it owns/`drop()`s) never ran. The node's
`entities_` map entry was erased regardless, so nothing in the process
still held the raw pointer: alive, refcount-pinned, and completely
unreachable — precisely LeakSanitizer's definition of a leak, once per
entity, every zone load.

### The fix: one `drop()` per factory, matching Irrlicht's own convention

Added `node->drop()` immediately after construction in all three
`RaceModelLoader` factory functions (`animated_mesh_creation.cpp`) and in
`EQAnimatedMeshSceneNode::clone()` (`animated_mesh_scene_node.cpp`; not
currently called from any code path, but the identical bug, fixed alongside
rather than left for the next caller to rediscover). Four lines total, each
commented in place with the refcount trace. Nothing about texture/mesh
ownership, zone load/unload lifecycle, or cache architecture changed —
`EntityRenderer`'s existing teardown was already correct and needed no
changes; it just never had a reachable refcount-0 to complete.

**Post-fix LSAN: `33426490 byte(s) leaked in 34420 allocation(s)`** — an
**89 -> 33MB (62%) reduction in leaked bytes and a 291,067 -> 34,420 (88%)
reduction in leaked allocation count.** The dominant remaining leak,
**22.97MB in only 4 allocation-site blocks, is `EverQuest::InitializeAudio()`
(`eq_bridge.cpp:1780`), identical byte-for-byte and block-for-block in both
the pre- and post-fix reports** — a flat, one-time startup leak (TinySoundFont
init, called once, not per zone), unrelated to zone crossing and out of this
unit's territory. Net of that, the zone-crossing-attributable leak fell from
~66MB to ~9MB (entity/mesh call sites: `loadZoneStep8f_BuildEntityMesh`,
`RaceModelLoader::loadGlobalModels`, `EQAnimatedMesh::copyBaseMesh`,
`SkeletalAnimator::computeBoneTransforms` — smaller, scattered residuals,
not chased further this unit; see Deviations and `deferred.md`.

### Step 2 — heaptrack, because RSS barely moved despite the leak fix

**The leak fix alone barely changed RSS**: production (non-ASAN)
`build-gles2-x86.sh` binary, same 6-crossing tour,
`review/T127/rss_before/run1.log` (pre-fix) vs `rss_after/run1.log`
(node-drop fix only): **+306.8MB (499.4->806.2MB) before, +297.2MB
(516.0->813.2MB) after — only a ~3% improvement**, even though LSAN showed a
56MB/88% cut in truly-leaked bytes/allocations. **The dominant RSS driver
was somewhere else.** This is exactly the plan's warning about not trusting
first appearances: a real leak existed and was fixed, but it was not the
main story.

Escalated to **heaptrack** (`review/T127/heaptrack/run1.zst`, `apt-get
install heaptrack`, not previously installed in this environment) over the
same tour on the post-node-fix production binary — chosen over massif
because it was directly installable and gives call-site attribution
regardless of reachability, matching the plan's fallback instruction.
**`heaptrack_print`: peak heap 421.45MB, but total memory leaked at exit
only 32.79MB** — closely matching the post-fix LSAN figure (33.43MB),
cross-validating both tools. **Most allocated memory genuinely is freed by
the time the process exits.** That rules out an unbounded *reachable* cache
too (the plan's "retention" branch) — if `EquipmentModelLoader::meshCache_`,
`RaceModelLoader`'s caches, or anything else were growing unboundedly and
staying reachable, heaptrack's "leaked" figure (which counts anything still
allocated at exit, reachable or not) would show it. It doesn't. **Neither
leak nor a live unbounded cache explains the ~300MB mid-tour RSS growth.**

Backtraces in `review/T127/heaptrack/run1_peaks.txt` pointed at the real
mechanism: **every zone crossing spawns a brand-new OS thread**
(`EQT::Graphics::LoadingThread::threadFunc`, via
`Application::startLoadingThread()`, confirmed directly in
`std::thread::_State_impl<>::_M_run()` backtraces) to do the bulk of the
zone-build allocation (WLD/S3D parsing, mesh/texture build — hundreds of MB
of churn per crossing). glibc gives a thread its own malloc arena the first
time it contends on the default arena, and **does not return that arena's
pages to the OS when the thread exits** — they sit in the arena's own free
lists, unreclaimed, while the *next* crossing's LoadingThread gets *another*
fresh arena. T106 (`refactor/T106_untracked_memory_attribution.md`) had
already identified glibc allocator fragmentation as a large (25.6% of RSS),
previously-uncounted category and explicitly named `malloc_trim()`/arena
tuning as "the lever... out of this unit's scope" — but T106's own
measurement was a single static zone visit (66 arenas existed, but only
Arena 0 held real content, so T106 concluded the fragmentation there was
"ordinary single-arena churn, not per-thread-arena"). **This unit's
measurement is the first to test the actual repeated-zone-crossing
scenario T106 didn't cover, and there the per-thread-arena mechanism is
real and dominant.**

**Confirmed empirically before committing to it as the explanation**: same
post-node-fix production binary, same tour, `MALLOC_ARENA_MAX=1` env var
forcing a single shared arena — **RSS growth dropped from +297.2MB to
+37.5MB (468.4->505.9MB), an 8x reduction**
(`review/T127/rss_arena1/run1.log`). This is decisive, load-bearing
evidence, not a hunch.

### The fix: `mallopt(M_ARENA_MAX, 1)` at process start

Added to `src/client/main.cpp`, first statement in `main()`, before any
other thread is created (audio, game thread, worker pool, LoadingThread all
come later in `Application::run()`). Baked into the binary via `mallopt()`
rather than left as an env var so a Pi run — and any future run — inherits
it automatically with no operator action required. Documented in place with
the measured before/after numbers and the tradeoff: a single shared arena
serializes cross-thread allocation on one lock instead of running lock-free
per-arena, an acceptable cost here (this is not a many-core
allocation-throughput-bound app, and the constrained-device target is a
single-core-class Orange Pi) against an 8x cut in RSS growth on a 512MB
machine where that growth is currently fatal.

**Combined fix (node-drop leak fix + `mallopt`), final measurement**
(`review/T127/rss_final/run1.log`, production binary, both fixes baked in,
no env var needed): **RSS 459.0MB -> 501.0MB, +42.0MB (1.09x) over the same
6-crossing tour.** Per-crossing breakdown (RSS at the end of each zone
visit): initial befallen settle 459.0->464.8, crossing 1 (->qeynos2, the
bigger zone) 464.8->500.9 (+36.1MB), crossings 2-6 (befallen<->qeynos2 x5
more) each add 0.0-0.1MB. **The remaining growth is a one-time cost of
visiting the larger zone once, not a per-crossing leak or fragmentation
trend** — this is the "flat profile" the plan named as the target.

### RSS growth per crossing — the primary number

| | RSS start | RSS end | Growth | Ratio |
|---|---|---|---|---|
| T120 baseline (log, cited in plan) | 549.6MB | 856.5MB | +306.9MB | 1.6x |
| This unit, re-measured, no fix | 499.4MB | 806.2MB | +306.8MB | 1.61x |
| Node-drop leak fix only | 516.0MB | 813.2MB | +297.2MB | 1.58x |
| `MALLOC_ARENA_MAX=1` only (env var, no node fix) | 468.4MB | 505.9MB | +37.5MB | 1.08x |
| **Both fixes, baked in (final)** | **459.0MB** | **501.0MB** | **+42.0MB** | **1.09x** |

The independently-re-measured no-fix baseline (+306.8MB) matches the plan's
cited T120 figure (+306.9MB) almost exactly, confirming a faithful repro
before attributing anything. The arena fix is the larger of the two levers
(8x vs the leak fix's ~3%) but they are additive and both real, distinct
bugs — the leak fix closes true unreachable memory the arena fix cannot
touch (arena consolidation doesn't free anything that's genuinely leaked),
and both are kept.

### Accounting updated to show this (part of the fix, not a nicety)

Added glibc `mallinfo2()`-based allocator accounting to `/pmem`: a new
`[Allocator] arena X | in-use Y | free-not-released Z` line
(`renderer_diagnostics.cpp`), sourced via `mallinfo2()` in
`runPmemDiagnostics()` (`eq_commands.cpp`) and threaded through
`DiagnosticsMemoryReportData`/`MemoryReportInput` the same way every other
external-input field already flows. This closes the exact gap T106 named
("no dedicated counter... the lever is `malloc_trim()` at quiet points, not
a report line item") — if arena fragmentation ever regresses (the fix is
undone, or a new source of thread churn is added), `/pmem`'s
`free-not-released` figure will show it directly, without needing to stand
up heaptrack from scratch again. Confirmed present and sane in the final
combined-fix run: arena settles at 434.8MB and stays flat across crossings
2-6 (`review/T127/rss_final/run1.log`), consistent with the RSS series
above.

### Verification

- x86 suite: **2284/2284** (excluding the documented ALSA
  `AudioManagerTest.PreloadCommonSounds`), matching the standing baseline
  exactly — includes `visual_gate` as a ctest target (passed).
- `bash scripts/ci/visual_gate.sh` run standalone: **PASS**, goldens
  untouched (memory figures reported-only, not gated, per T69).
- `bash scripts/build-arm-noble.sh`: succeeded, all binaries built
  including `willeq`.
- `scripts/refactor/check_file_sizes.sh fable`: 7 ok, 1 pre-existing WARN
  (`irrlicht_renderer.h`, 2298 lines — already over 1500 before this unit;
  this unit only added two `size_t` fields to it), 0 FAIL.
- Testchar restored to the documented resting position
  (zone_id=36, x=-77.9232, y=21.9086, z=3, h=190) after the tour's `/move`s
  moved it; confirmed via `SELECT` before final `down.sh`.

### Deviations

- **The plan's step 1/step 2 branching ("if LSAN is quiet, it's retention")
  didn't cleanly apply**: LSAN was NOT quiet (a real, fixable leak existed),
  but the leak it found explained only ~3% of the observed RSS growth. Per
  the plan's own spirit ("use an allocation profiler... regardless of
  reachability" if the picture isn't settled), escalated to heaptrack
  anyway rather than declaring the LSAN fix sufficient just because it
  found *something*. This is flagged because a shallower read of "LSAN
  found a leak, fixed it, done" would have shipped a fix that closes only
  3% of the actual problem.
- **The dominant cause (glibc per-thread-arena fragmentation from
  per-crossing `LoadingThread` creation) is neither a leak nor a
  cache-retention bug** in the sense the plan's two named categories
  describe — it's a third mechanism (allocator behavior, not application
  code holding memory). It has the identical practical effect (RSS growth
  invisible to `/pmem`, fatal on a 512MB device), so it's reported and
  fixed with the same rigor, but flagged as a genuine third shape rather
  than force-fit into "leak" or "cache."
- **The small residual leak (`RaceModelLoader::loadGlobalModels`,
  `EQAnimatedMesh::copyBaseMesh`, `SkeletalAnimator::computeBoneTransforms`,
  ~9MB net of the flat one-time audio leak across 6 crossings) was not
  chased to zero.** It's roughly two orders of magnitude smaller than the
  bug that was fixed and did not visibly move the RSS growth number (the
  arena fix, applied on top, brought growth to a flat per-crossing profile
  regardless). Named in `deferred.md` rather than silently dropped.
- **The one-time `EverQuest::InitializeAudio()` leak (~23MB, identical in
  both pre- and post-node-fix LSAN reports) is out of this unit's
  territory** (not zone-crossing-related, doesn't grow) but is real and
  named in `deferred.md` since it was found in the course of this work.
- No Pi run, per the plan's explicit instruction — the natural follow-up
  is named below.

## Board

**RSS growth per crossing: +306.8MB (1.61x, no fix) -> +42.0MB (1.09x, both
fixes), a 7.3x reduction. The remaining +42.0MB is a one-time cost of
visiting the larger of the two tour zones once (crossing 1 only); crossings
2-6 each add 0.0-0.1MB — a flat profile, the plan's stated target.**

Two additive, independent fixes, both committed:
1. A true unreachable leak (`RaceModelLoader` node-factory functions never
   balancing Irrlicht's own `addChild()` grab — missing `drop()`), LSAN-
   confirmed 89MB->33MB / 291K->34K allocations.
2. glibc per-thread-arena fragmentation from a fresh `LoadingThread` every
   zone crossing (`mallopt(M_ARENA_MAX, 1)`), heaptrack + direct
   `MALLOC_ARENA_MAX=1` A/B-confirmed as the dominant driver, 8x RSS-growth
   reduction on its own.

**Neither is a deliberate cache being bounded or gutted** — no design
tradeoff to put on the board here; both are straightforward bugs (a missing
refcount release, and an un-tuned allocator default) with no user-visible
behavior change and no STOP-condition territory touched (texture/mesh
ownership, zone load/unload lifecycle, and cache architecture are all
unchanged).

**Recommend as the next unit: re-run T125's Tour B and the Pi OOM
confirmation.** This closes the leg T125 could not complete — T125's Tour B
died to kernel OOM after repeated zone crossings on a 512MB device, and this
unit's fix directly targets that mechanism (a 7.3x cut in the RSS growth
that caused it). Also recommend, at lower priority: the small residual
entity-mesh leak and the one-time audio-init leak, both named above and in
`deferred.md`, neither currently RSS-significant enough to justify their
own unit ahead of the Pi confirmation.
