# T67 — Remove ConstrainedMeshCache from production

**Slot: t1. x86 only. No Pi. NO EQEMU.**

**Territory: `include/client/graphics/constrained_mesh_cache.h`,
`src/client/graphics/constrained_mesh_cache.cpp`,
`include/client/graphics/irrlicht_renderer.h`, the nine renderer TUs that
reference the pointer, `include/client/graphics/constrained_renderer_config.h`
+ `src/client/graphics/constrained_renderer_config.cpp` (the
`meshMemoryBytes` field), `src/client/graphics/CLAUDE.md`, and tests.**

Do not touch the GLES2 driver, the bone/equipment path, or the command
registries. `refactor/deferred.md` is append-only.

Read `refactor/EXECUTOR_PROTOCOL.md`, then `refactor/deferred.md`'s entry
**"ConstrainedMeshCache is never constructed (P10 finding, 2026-07-26)"**.

## The decision (Will, 2026-08-11)

Remove it from production, keep the code findable for reference.

`ConstrainedMeshCache` is a complete, working 385-line LRU mesh cache —
budget, eviction with protected regions, freeze/unfreeze, hit-rate and
eviction stats. **It has never been constructed.** The only statement in
the codebase that touches the `unique_ptr` as an lvalue is
`renderer_zone_lifecycle.cpp:177`'s `constrainedMeshCache_.reset()`, and
that sits inside `if (constrainedMeshCache_)`, so it never runs either.
The pointer is null for the entire process lifetime and every one of the
~40 call sites is a read behind a null guard.

Supporting facts, verified during scoping:
- There is **no way to turn it on**. `meshMemoryBytes` is computed per
  preset (24MB for `orangepi`, `constrained_renderer_config.cpp:306`) and
  consumed by nothing.
- The eviction pressure it exists to relieve **has never been reproduced**.
  P10b's "71 evictions" is the only such observation ever recorded; P13 and
  P13b then measured **zero** evictions across four independent
  configurations.
- It has been independently written **twice** — once on the now-deleted
  `mem` branch, once as `d442db12` (2026-02-18) — and wired **zero** times.

**"Keep a reference" does not mean an archive tag here.** Unlike the `mem`
branch (unmerged, whose commits would eventually be garbage-collected),
this code is in `fable`'s history and is permanently recoverable via
`git log --follow -- <path>`. The reference is a **pointer in the docs**:
record the last commit that contained the implementation and its file
paths, so a future reader can retrieve it in one command instead of
knowing to go looking.

## Step 1 — the one part that is NOT dead

**`ConstrainedMeshCache::estimateMeshSize()` is a static function and it
genuinely runs**, at `renderer_zone_stream.cpp:282` and
`renderer_zone_draw.cpp:1155`. The class is not link-dead.

Deal with this deliberately before deleting anything: keep that
calculation, relocated somewhere sensible (a free function in a mesh or
renderer utility header is the obvious shape), with its behaviour
unchanged. **Do not delete it and do not reimplement it from memory** —
move it verbatim and say where it went.

If you find any other static or otherwise-live member during the sweep,
treat it the same way and report it.

## Step 2 — establish what is genuinely dead, file by file

Roughly nine TUs reference `constrainedMeshCache_`:
`renderer_zone_stream.cpp` (the bulk), `renderer_zone_draw.cpp`,
`renderer_zone_lifecycle.cpp`, `renderer_zone_load.cpp`,
`renderer_diagnostics.cpp`, `renderer_sim_bridge.cpp`,
`renderer_player_physics.cpp`, `renderer_frame.cpp`, `renderer_debug.cpp`.

`renderer_zone_stream.cpp` is *largely* unreachable, but **not
necessarily entirely** — e.g. `:851` guards on
`constrainedMeshCache_ && !textureRebuildQueue_.empty()`, and the texture
rebuild queue may well be live on its own. **Do not delete whole files or
whole functions on the assumption that the guard implies death.** For each
site, determine whether removing the cache leaves live code that must be
preserved, dead code that goes with it, or a branch that now needs
simplifying rather than deleting.

Report the per-file accounting in the Review. This is the part of the unit
where a mistake is expensive and invisible.

## Step 3 — remove the config field

`meshMemoryBytes` (`constrained_renderer_config.h:209`) is a derived limit
consumed by nothing once the cache is gone. Remove it, including the
per-preset assignments (`constrained_renderer_config.cpp:212, 232, 254,
306`), the `calculateMemoryLimits()` default
(`meshMemoryBytes = totalMemoryBudgetBytes / 5`, `:104-105`), the
validation that rejects zero when a total budget is set (`:72-73`), and
the JSON preset keys (`mesh_memory_mb` / `meshMemoryBytes`, `:527-529`).

**Check whether any committed preset JSON in `config/` sets those keys.**
If so, decide deliberately whether removing the field silently ignores an
existing key or errors on it, and say which you chose. A config key that
becomes silently meaningless is its own small trap.

Note Will's standing decision from the same conversation: **per-subsystem
budgets that are defined and enforced are the model** — no global
coordinator is wanted. Removing an unenforced, unconsumed budget is
consistent with that, not a retreat from it.

## Step 4 — the docs, which are actively misleading

`src/client/graphics/CLAUDE.md:121-124` describes the mesh cache as
shipping behaviour — the 24MB budget, on-demand region loading, eviction —
and then makes a consequential claim about lifecycle:

> Texture pixel data (`releaseTexturePixelData()`) is released only when
> the mesh cache is NOT active (all meshes pre-built). When the mesh cache
> IS active, texture pixel data must be retained for progressive region
> mesh rebuilds.

Since the cache is never active, that second branch has never executed.
Anyone reasoning about memory lifecycle from this document — including a
future Claude — is being told about a mechanism that does not run.

Correct it to describe what actually ships: regions are built eagerly at
zone load, and texture pixel data is always released. Add the reference
pointer (commit sha + paths) so the removed implementation stays findable.

## STOP conditions

- `estimateMeshSize()` (or any other live member) turning out to be
  entangled such that preserving it means keeping the class — report
  rather than improvising.
- Any call site where removing the cache would change rendering behaviour.
  There should be none; if you find one, the premise is wrong and I want
  to know before you proceed.
- The visual gate moving. This is dead-code removal — **rendering must be
  byte-identical**, and the gate is the proof.

## Verification

Full protocol bar. The visual gate matters more than usual here: it is the
evidence that removing ~40 guarded branches changed nothing. State the
line-count delta and the per-file accounting from Step 2.

## Review

**Verification bar**: PASS on all steps.
1. `cmake --build build -j24` (GLES2-config tree) — clean build, no errors.
2. `ctest --test-dir build --output-on-failure` — 1998/1999, sole failure
   `AudioManagerTest.PreloadCommonSounds` (documented ALSA host exception,
   protocol baseline). `visual_gate` PASSED inside the ctest run.
3. `scripts/build-gles2-x86.sh` — PASS, both `build-gles2/bin/willeq` and
   `gles2_zone_benchmark` (the latter statically links `zone_geometry.cpp`,
   exercising the relocated `estimateMeshSizeBytes()` in a second tree).
4. `scripts/ci/visual_gate.sh` — PASS (qeynos2, gfaydark, befallen). Goldens
   untouched (`git status` on `benchmarks/goldens` shows nothing — this unit
   never wrote there).
5. `scripts/build-arm-noble.sh` — PASS, `build-arm-noble/bin/willeq` built
   and linked (ELF 32-bit ARM, unstripped).
6. `scripts/refactor/check_file_sizes.sh fable` (run with `bash -c 'cd
   <slot> && ...'` — the script resolves its repo root via `git rev-parse
   --show-toplevel` with no `-C`, so it silently mis-resolves against the
   Bash tool's ambient cwd (`/mnt/projects/fable`, not a git repo) unless
   invoked from inside the worktree; `cd` inside a `bash -c` script body is
   the documented protocol exemption, not a violation): 19 files checked,
   11 ok, 6 warn, 0 fail. The 2 "file not found" lines are the two deleted
   files (`constrained_mesh_cache.h/.cpp`) showing up in `git diff
   --name-only` against the base rev — a pre-existing script gap (it has no
   handling for deletions in the diff, only checks-that-exist), not a size
   failure; worth a one-line fix in a future units-infra pass, not blocking
   here. All 6 WARNs (`irrlicht_renderer.h/.cpp`, `zone_geometry.cpp`,
   `renderer_frame.cpp`, `renderer_player_physics.cpp`,
   `renderer_zone_load.cpp`) were already over the 1500-line threshold at
   the `fable` base commit — confirmed by diffing line counts before/after;
   none newly crossed. Five of the six shrank; `zone_geometry.cpp` grew by
   19 lines (the relocated `estimateMeshSizeBytes()`), already WARN before.

**Line-count delta**: 21 files changed, 215 insertions(+), 1200
deletions(-), net **-985 lines** (`git diff --stat 46593f1f`, the branch
point). `constrained_mesh_cache.h` (102 lines) and `constrained_mesh_cache.cpp`
(283 lines) deleted outright.

**Where `estimateMeshSize()` went**: relocated verbatim as a free function
`estimateMeshSizeBytes(irr::scene::IMeshSceneNode*)` in
`include/client/graphics/eq/zone_geometry.h` (declaration, next to the
existing `heightToColor`/`normalToColor` helpers) and
`src/client/graphics/eq/zone_geometry.cpp` (implementation, byte-identical
math to the original). One live call site kept: `rebuildRegionMesh()`
(`renderer_zone_draw.cpp`) still computes and logs the built mesh's size —
just without the cache eviction/registration calls that used to consume
it. The `renderer_zone_stream.cpp:282` call site was inside a whole
`if (constrainedMeshCache_) { ... }` block that never ran (see per-file
accounting) and was removed with that block.

**Other live member found**: none beyond `estimateMeshSize()`. One
adjacent finding worth flagging explicitly: `rebuildRegionMesh()`
(`renderer_zone_draw.cpp`, pre-existing code, not something this unit
introduced) called `constrainedMeshCache_->evictUntilAvailable(...)` and
`->onLoaded(...)` **without a null-guard** — a latent null-pointer
dereference, since the pointer was always null. It appears never actually
reached in practice: `rebuildRegionMesh()`'s only production caller with
the cache guaranteed absent is `renderer_player_physics.cpp`'s
"already-built" fallback (`setupMinimalZoneCollision()`, called from SEQ
Step9, *after* SEQ Step4 has already eagerly built every region with
geometry into `regionMeshNodes_`), so `alreadyBuilt` is true for the
overwhelming majority of runs; the crash would only fire if Step4's mesh
build/node-creation failed for a region whose *second* attempt (via this
fallback) then succeeded — never observed, not chased further. Removing
the cache eliminates this latent crash path entirely (STOP condition
"call site where removing the cache would change behaviour" does not
apply: this is dead-code removal that happens to also delete an
unreachable-in-practice bug, not a rendering-affecting change).

**Config key decision**: `meshMemoryBytes`
(`constrained_renderer_config.h`) removed entirely — field, validation
(`:72-73`), `calculateMemoryLimits()` default (`:104-105` in the old
line numbers), all 4 preset assignments (voodoo1/voodoo2/tnt/orangepi),
and both JSON preset-key parses (`mesh_memory_mb`, `meshMemoryBytes`).
**No committed `config/` preset JSON sets either key** (verified by grep
before editing), so there is no shipped behavior change. A local,
uncommitted custom JSON that happened to set one of these keys will now
have it silently ignored — consistent with how every other unrecognized
preset key is already treated by this loader (no allowlist / no
reject-unknown-keys validation exists here at all), so this is not a new
kind of trap, just one more key added to the existing silent-ignore set.

**Docs correction**: `src/client/graphics/CLAUDE.md`'s "Constrained Memory
Management" section rewritten. Old text claimed the mesh cache is shipping
behaviour (24MB budget, on-demand region loading, eviction) and that
texture pixel data is retained "when the mesh cache IS active" for
progressive rebuilds. Corrected to state plainly that regions are always
built eagerly at zone load, and that texture-pixel-data release is gated
on the (real, live) `releaseTextureDataAfterUpload` flag — which is
unconditional of anything mesh-cache-related and was **already** the
actual code path (the old doc's gating condition never matched what
`loadZoneStep13_Cleanup()` actually checks). Component list entry
removed. **Reference pointer recorded** in the new prose: last commit
containing the implementation is `49844c72` (2026-03-08, "PVS direct
index lookup, SimWorker copy elimination, DRM page flip fix, stderr
buffering"), paths `include/client/graphics/constrained_mesh_cache.h` +
`src/client/graphics/constrained_mesh_cache.cpp` — retrievable via `git
show 49844c72:src/client/graphics/constrained_mesh_cache.cpp` in one
command. Also updated: `constrained_renderer_config.h`'s `mergeZoneDraws`
doc comment and `renderer_zone_merge.h`'s design-memo header, both of
which described the merge-eviction gate in terms of
`ConstrainedMeshCache`; corrected to describe the surviving
texture-cache-only gate, with the same reference pointer.

**Per-file accounting (Step 2)**, all ~40 call sites across the 9 named
TUs plus one out-of-territory file the removal forced a touch in:

- **`constrained_mesh_cache.h`/`.cpp`** — deleted outright (385 lines).
  `estimateMeshSize()` relocated first (see above).
- **`renderer_zone_stream.cpp`** (the bulk, confirmed *not* entirely dead):
  - `processFrameLazyLoad()` (115 lines) — **fully dead, deleted**. Not
    merely cache-gated: grep confirmed it had **zero callers anywhere**
    (declared, defined, never invoked) — dead before this unit even
    touched it.
  - `drainMeshResults()`, `finalizeOneMeshResult()`,
    `updateRegionMaterials()`, `processOneMaterialSwap()`,
    `submitOneBgMeshBuild()` (~270 lines total) — **deleted**. This whole
    background-mesh-build subsystem existed solely to feed/drain the
    cache's async region loading. `submitOneBgMeshBuild()` was its sole
    producer and was itself gated `if (constrainedMeshCache_)` (never
    true), so `meshResultQueue_` was never populated in any shipped
    build, making `finalizeOneMeshResult()` (called *unconditionally*
    from `renderer_frame.cpp`) permanently return `false` at its first
    line, and `pendingTextureBuffers_` (populated only inside
    `finalizeOneMeshResult()`) permanently empty, which in turn made
    `materialSwapQueue_`'s sole producer (in `renderer_gpu_upload.cpp`,
    see below) permanently unreachable too. Traced the whole chain before
    deleting any of it — this is the "expensive and invisible mistake"
    zone the plan warned about, and the trace is why it's safe.
  - **`:851`'s guard** (`constrainedMeshCache_ && !textureRebuildQueue_.empty()`,
    "Priority 1.5" in `processFrameProgressiveLoad()`) — confirmed exactly
    the plan's predicted nuance: `textureRebuildQueue_` **is** independently
    live (populated by `renderer_gpu_upload.cpp`'s GPU-upload-complete
    callback, unrelated to the mesh cache), but this particular *consumer*
    of it was cache-gated and so never ran. Removed the consumer block;
    left the live producer and the queue's other genuinely-independent
    reader (`checkProgressiveLoadingComplete()`'s `gpuWorkPending` check)
    untouched. Net effect: `textureRebuildQueue_` is now drained only at
    zone unload — **identical to today's behavior**, since this was
    already the only mesh-cache-gated consumer of it in this
    file/`renderer_frame.cpp` (flagged as a pre-existing, not
    newly-introduced, unbounded-growth-until-unload pattern; not fixed
    here, noted for `deferred.md`).
  - `processFrameProgressiveLoad()`'s remaining cache-gated blocks
    (Priority "P1" player-region build, GREEN-gate diagnostic loop,
    Priority 2 PVS-neighbor-region build, end-of-function eviction sweep,
    `checkProgressiveLoadingComplete()`'s `unbuiltRegions` diagnostic) —
    all **deleted or simplified to their always-0/never-true residual**
    (each was gated solely on `constrainedMeshCache_`, confirmed never
    true).
  - Priority 3 (door build) and Priority 4 (PVS object build) in the same
    function are **NOT** gated on `constrainedMeshCache_` — **left
    untouched**, per the plan's instruction not to sweep beyond the
    pointer's own guards. (Side finding, not acted on: both read
    `protectedRegions_`, whose only writer — see `renderer_sim_bridge.cpp`
    below — was itself cache-gated, so `protectedRegions_` is
    permanently all-false and these two priorities are very likely
    already-dead in production for a *different*, unrelated reason. Out
    of this unit's scope; flagged for `deferred.md` rather than chased.)
  - `indexObjectMeshes()`, `buildDeferredObject()`,
    `processFrameProgressiveLoad()`'s entity-prep priorities,
    `rebuildPvsNeighborhood()`, `isInPvsNeighborhood()`,
    `onPvsRegionChanged()`, `queueEntityPrepRequests()`,
    `logAssetBuildTime()`, `sendLoadProgress()` — **fully live, untouched**
    (only stray comments mentioning the cache/`meshLoadQueue_` corrected).
- **`renderer_zone_draw.cpp`**:
  - `drawZoneGeometrySorted()`'s merge-eviction gate — **simplified**, not
    deleted: `meshEvictionCount` (always 0) dropped from the summed
    `evictionCount`, and the `(!constrainedMeshCache_ || ...)` "fully
    resident" OR-clause (always true) dropped from the build-trigger
    condition. `constrainedTextureCache_`'s real eviction count is the
    sole survivor — this is the P10b-documented live mechanism the old
    comment already called out as the actual SIGSEGV cause.
  - `rebuildRegionMesh()`'s cache-bookkeeping tail (`evictUntilAvailable`/
    `onLoaded`, unconditional, no null guard — see "other live member"
    finding above) — **deleted**; `estimateMeshSizeBytes()` call + a
    slimmed log line **preserved** (informational, no rendering effect).
    BEGIN-log's cache fields dropped.
- **`renderer_zone_lifecycle.cpp`** (`unloadZone()`): the
  `if (constrainedMeshCache_) { ...; .reset(); }` block (dead, guard
  never true) and all background-mesh-build-subsystem `.clear()` calls
  (members deleted) — **removed**. `protectedRegions_.clear()` **kept**
  (member survives, see below).
- **`renderer_player_physics.cpp`**: the `(!constrainedMeshCache_ ||
  !constrainedMeshCache_->isLoaded(playerRegion))` OR-clause (always
  true) in `setupMinimalZoneCollision()`'s player-region build condition
  — **dropped**, condition now just `playerRegion != SIZE_MAX &&
  !alreadyBuilt`. Provably identical: `!constrainedMeshCache_` was always
  `true`, so the clause never changed the outcome.
- **`renderer_debug.cpp`** (`toggleMergeZoneDraws()`): the ternary reading
  `constrainedMeshCache_->getEvictionCount()` (always 0) — **replaced
  with the literal `0`** it always evaluated to. Left a comment flagging
  a pre-existing, unrelated inconsistency (this reset never accounted for
  `constrainedTextureCache_`'s eviction count either, unlike
  `drawZoneGeometrySorted()`'s own gate) — not fixed here.
- **`renderer_diagnostics.cpp`**: the "Mesh Loading" frame-timing log line
  (dangling `if (constrainedMeshCache_)` with no braces, so gated only
  the one `LOG_INFO` beneath it) and the whole "Mesh Cache" section of
  `/pmem` — both dead (gate never true) — **deleted**.
- **`renderer_sim_bridge.cpp`** (`applySimulationResults()`): the
  `if (constrainedMeshCache_) { meshLoadQueue_...; protectedRegions_... }`
  block — **deleted**. This was `protectedRegions_`'s *only* writer
  anywhere in the codebase (confirmed by full-tree grep before deleting),
  so `protectedRegions_` has been permanently all-`false` in every
  shipped build already; removing this writer changes nothing (see the
  `renderer_zone_stream.cpp` Priority 3/4 side-finding above).
- **`renderer_frame.cpp`** (`processFrame()`): `drainMeshResults()`/
  `finalizeOneMeshResult()`/`processOneMaterialSwap()` calls (functions
  deleted, see above) and the cache-gated Priority 7 (texture rebuild)
  and Priority 10 (submit bg mesh build) blocks — **deleted**. The
  governor's `hasPendingWork` cache-gated loop over `meshLoadQueue_` —
  **deleted**, `hasPendingWork` now just `progressiveLoadingActive_`
  (provably identical: the loop's guard was always false).
- **`renderer_zone_load.cpp`**: the `--load-mem-audit` `sampleStep`
  lambda's `meshBytes` ternary (always 0) — **replaced with the literal
  `0`** it always evaluated to.
- **`irrlicht_renderer.h`/`.cpp`**: forward declaration, `unique_ptr`
  member, `getConstrainedMeshCache()` getter (confirmed zero external
  callers before removing), `processFrameLazyLoad()` declaration,
  `MeshLoadEntry`/`meshLoadQueue_` (no writers or readers survive
  anywhere after the above), `BackgroundMeshResult`/`meshResultMutex_`/
  `meshResultQueue_`/`localMeshResults_`/`pendingMeshBuilds_`,
  `MaterialSwapEntry`/`materialSwapQueue_`/`pendingTextureBuffers_`, and
  the 5 background-build function declarations — **all removed**.
  `protectedRegions_` **kept** (declared, resized in
  `renderer_zone_load.cpp`, cleared in `renderer_zone_lifecycle.cpp`) —
  it has live, cache-independent readers in
  `renderer_zone_stream.cpp`'s door/PVS-object progressive-load gating
  (Priority 3/4, see above); comment updated to state plainly that it is
  currently permanently all-false, rather than leave the stale "protected
  from eviction" framing now that there's no eviction concept left to
  protect against.
- **`renderer_gpu_upload.cpp`** (not in the plan's named 9 TUs, but a
  forced touch): the sole producer of `materialSwapQueue_`/reader of
  `pendingTextureBuffers_` (both deleted from `irrlicht_renderer.h`)
  lived here. Since `pendingTextureBuffers_` was *always* empty (its
  only writer was `finalizeOneMeshResult()`, deleted above), the
  `usedMaterialSwap` branch here was **already unreachable** — simplified
  to always take the "fall back to full rebuild" path it always fell
  into, dropping the now-impossible lookup. Behavior-preserving; noted
  as a deviation from the named territory list, forced by removing
  members this file referenced (there was no way to remove those members
  without touching every one of their referrers).

**Deviations from the plan**:
1. Touched `renderer_gpu_upload.cpp` (outside the named 9 TUs) — see
   above; unavoidable once `pendingTextureBuffers_`/`materialSwapQueue_`
   were removed, and behavior-preserving (the branch it simplifies was
   already unreachable).
2. Scope of "dead code that goes with it" ended up larger than a literal
   reading of "the nine TUs that reference the pointer" might suggest —
   the whole background-mesh-build subsystem (`drainMeshResults()` etc.)
   doesn't reference `constrainedMeshCache_` at every one of its own
   lines, but exists *only* in service of it (its sole producer was
   cache-gated). Traced the full producer/consumer chain before deleting
   any of it (documented above) rather than assuming; this is exactly
   the kind of thing the plan's per-file-accounting instruction asked
   for.
3. Found (not fixed, flagged for `deferred.md`): `protectedRegions_`
   being permanently all-false makes `renderer_zone_stream.cpp`'s
   Priority 3 (door build) and Priority 4 (PVS object build) in
   `processFrameProgressiveLoad()` very likely already-dead in
   production too — but doors/objects are also built eagerly elsewhere
   (SEQ Step6/7), so this is unconfirmed and orthogonal to
   `ConstrainedMeshCache` itself; out of this unit's territory, not
   chased further.
4. A stray uncommitted working-tree artifact (`refactor/
   T68_spell_effects_data_plumbing.md`, added by a later `fable` commit
   that landed on the shared branch while this unit was in flight) briefly
   got pulled into the staging area by an overbroad `git restore
   --source=fable`; caught and unstaged before committing — not part of
   this commit, left untracked in the worktree.

No STOP conditions were hit. No call site was found where removing the
cache would change rendering behaviour — every genuinely-reachable
call site was either purely cache-bookkeeping (removed) or an
always-true/always-false boolean term (simplified to its constant
value), never a branch whose *taken* path changed.

Board (`refactor/deferred.md`, append, never delete): re-title the
"ConstrainedMeshCache is never constructed" entry as resolved-by-removal,
recording Will's decision and the reference pointer. Note explicitly that
this closes the item **without** answering whether progressive region-mesh
streaming is ever wanted — if Pi memory pressure is later measured, the
implementation is one `git show` away, and the entry should say so.
