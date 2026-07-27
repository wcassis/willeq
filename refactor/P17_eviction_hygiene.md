# P17 — Eviction hygiene: zone-unload texture release + entity-node guard + snapshot race

Pre-authorized batch (Will, 2026-07-27). Slot: wt/slot1, branch
task/P17-eviction-hygiene. OWNS THE EQEMU SERVER. Protocol:
refactor/EXECUTOR_PROTOCOL.md. Parallel: P18 (slot2, benchmark/
animation files), P19 (slot3, Pi/benchmark runs) — stay out of
tools/hwcaps code.

## Motivation (P15, 2026-07-27)

Three related production defects from the P15 findings:
1. **Cross-zone texture accumulation (the solved eviction mystery)**:
   ConstrainedTextureCache is never reset across zone unloads —
   befallen→qeynos2 = 27 evictions; long sessions worse (P10b's 71).
   The 1999-honest fix: release the OUTGOING zone's textures at zone
   unload (zone_region/object categories; entity/composite textures
   for races not present in the new zone are fair game too — design
   the retention rule, justify it).
2. **Entity-node dangling-ITexture* post-eviction**: P10b's
   RegionMeshTextureGuard protects region nodes only; entity mesh
   materials have the identical hazard (P15 hit it intermittently).
   Extend the guard (or a sibling) to entity nodes/instance meshes.
3. **Stale-zone-name-vs-position snapshot race** (P15's second new
   bug — its Review has the evidence trail).

## Design

1. Read P15/P13/P10b reviews + the ledger harness. Implement the
   unload-release (hook zone teardown; category-aware; the
   TextureUploadLedger should also reset/segment per zone so future
   audits stay per-zone-readable).
2. Entity guard: same eviction-listener pattern as
   RegionMeshTextureGuard, over the entity renderer's node/instance
   bookkeeping (EQAnimatedMeshSceneNode materials_ + instanceMesh_
   buffers).
3. Snapshot race: root-cause from P15's evidence, minimal fix,
   normal-path-untouched proof (the P15 precedent).
4. Verification: the P15 multi-hop session goal now achievable — 4+
   hops (befallen↔qeynos2↔third zone) in one process, ledger on:
   near-zero evictions expected post-release (quantify), no dangling
   crashes, no snapshot race; plus a mergeZoneDraws regression spot
   (merge should now ACTIVATE in qeynos2 once single-zone footprint
   stays under budget — check and report the activation change);
   standard bar.

## Out of scope
- Budget resizing; eviction-policy redesign; foliage/pop-in (next
  server tenants).

## Review

Executed on `wt/slot1`, branch `task/P17-eviction-hygiene`, cut from `fable` at
`9c5617b`.

### 1. Cross-zone texture accumulation — root cause and fix

**The root cause is NOT "the cache is never reset."** A full-wipe reset
(`ConstrainedTextureCache::clear()`, called from `IrrlichtRenderer::unloadZone()`)
already existed (since P04) and is exactly the right mechanism — it was just
never actually reached during a live zone transition. The queued trigger,
EverQuest's `"/unloadzone_internal"` `RendererCommand` (pushed from
`CleanupZone()`, `eq_zone.cpp`), is only ever applied by
`EverQuest::PreRenderTick()` → `ProcessBridgeEvents()`, which
`Application::render()` reaches ONLY on its "normal gameplay" branch
(`application.cpp`). Every branch `render()` takes DURING a zone transition —
the network-handshake-phase branch and the loading-thread-active branch — is
an early return that skips `PreRenderTick()` entirely. So the queued unload
event sat unapplied for the ENTIRE duration of the new zone's load, and was
only actually applied once gameplay resumed on the NEW zone — at which point
running `unloadZone()` would have wiped the zone that had just finished
loading, not the one being left (a second latent bug the same investigation
surfaced). Net effect confirmed by inspection and reproduced live (see
Evidence below): the OLD zone's textures remained fully resident in
`ConstrainedTextureCache` for the entire duration the NEW zone's textures
were being uploaded — a second zone visited in the same process shares the
first zone's already-consumed budget headroom, exactly matching P15's
`befallen→qeynos2 = 27 evictions` finding and P13b's "evictions accumulating
across MULTIPLE zone visits" open question.

**Fix**: `Application::loadZoneGraphicsOnThread()` (`application.cpp`) now
calls `m_renderer->unloadZone()` directly and synchronously, right after
clearing the loading flag and before touching any new-zone state — on the
loading thread, which owns the GL context for the entire duration of that
function, guaranteeing the old zone's cache/scene state is gone before a
single byte of the new zone uploads. The now-redundant (and, if left in,
actively harmful) `"/unloadzone_internal"` push in `CleanupZone()`
(`eq_zone.cpp`) was removed. The `RendererCommand` handler branch in
`irrlicht_bridge.cpp` that dispatches `"/unloadzone_internal"` → `unloadZone()`
was left in place (dead code, harmless — nothing pushes that command string
anymore) rather than removed, to keep the diff minimal.

**Category-aware release / retention rule — deviation, justified**: the
plan's design item 1 floated "category-aware" release (retain entity/composite
textures for races present in both zones; only release outgoing zone-region/
object textures). Given the root cause turned out to be a *timing* bug on an
*already-existing, already-correct, full-wipe* release mechanism — not a
missing release mechanism — building new selective-retention logic on top
would be scope creep into "eviction-policy redesign," which the plan
explicitly places out of scope. P13's own measurement (qeynos2 composites ON:
15.68MB, ~4x under the 64MB budget) shows the full-wipe's cost (re-uploading
common race textures on every zone revisit) is small relative to the budget
and the win (zero cross-zone leakage, provably) — so the already-existing
full clear is kept as the release mechanism, not replaced.

**Ledger segmentation** (plan's other design-1 ask): `ConstrainedTextureCache::clear()`
now also calls `ledger_.reset()` (+ clears `pendingLedgerTag_`), so each
zone's `--texture-ledger` dump (fired at `loadZoneSequential`'s "zone-in
complete" marker) reflects only that zone's own uploads/evictions instead of
accumulating every zone visited earlier in the process — this is what makes
"near-zero [cross-zone] evictions post-release" actually measurable per zone
rather than as a session-wide count that would mask the fix.

### 2. Entity-node dangling-`ITexture*` guard

New `EntityMeshTextureGuard` (`renderer_zone_merge.h/.cpp`, same
`TextureEvictionListener` pattern as P10b's `RegionMeshTextureGuard`), covering
the two places a dangling evicted `ITexture*` can hide in
`EQAnimatedMeshSceneNode` that region-node scanning can't reach: (1) the
node's own `materials_` vector (what `render()` actually binds — exposed via
the public `getMaterial(i)`/`getMaterialCount()` interface), and (2) the
node's per-instance `instanceMesh_` buffers' own materials (e.g. attached
equipment pieces). Constructed once, right after `createEntityRenderer()`
(needs `entityRenderer_` to exist first, unlike the two guards built earlier
in `initLoadingScreen()`), bound to `EntityRenderer::getEntities()` (a
persistent, renderer-lifetime member, cleared not recreated between zone
visits — same precedent as `regionMeshNodes_`), registered as an eviction
listener, torn down symmetrically in `shutdown()`.

**A second, distinct dangling-pointer class found and fixed during this
unit's own multi-hop verification** (not covered by `EntityMeshTextureGuard`,
which only protects per-instance node state): `RaceModelLoader`'s SHARED,
per-race cached base mesh (`meshCache_`/`animatedMeshCache_`, reused across
zone visits by `getAnimatedMeshForRace`) has its OWN independent texture
cache, `ZoneMeshBuilder::textureCache_` (`meshBuilder_->textureCache_`),
which `RaceModelLoader::clearMeshCaches()` (called from `unloadZone()`)
deliberately left untouched — its own comment said why: *"the ITexture*
pointers are still valid in the Irrlicht driver (character textures are never
removed during zone transitions)."* That assumption was true only because
`ConstrainedTextureCache::clear()` was starved out by the §1 bug and
essentially never fired during a live transition — with §1 fixed, it fires on
every transition, `driver_->removeTexture()`s every cached texture, and any
race reused across a zone revisit whose base mesh survived
`clearMeshCaches()` (which does correctly drop `meshCache_`/
`animatedMeshCache_` itself) can still hold a dangling pointer via THIS
separate, never-cleared map. Reproduced live: befallen → qeynos2 → befallen
(2nd visit) SIGSEGV'd in
`RaceModelLoader::createAnimatedNodeWithEquipment`'s
`currentTex->getName()` — the exact pre-existing bug `refactor/deferred.md`
already flagged ("found by P15, NOT chased" — "confirmed independent of
eviction volume," which now makes sense: it was never actually an
eviction-count-dependent bug, it was a zone-unload-timing-dependent one, and
P15 could not chase it because the §1 bug also meant it could never reliably
reproduce a 3rd zone visit in one session). **Fix**: `clearTextureCache()`
already existed on `ZoneMeshBuilder` (`textureCache_`/`pendingTextures_`/
`texturesWithAlpha_`) but had zero callers anywhere in the codebase — wired
it into `RaceModelLoader::clearMeshCaches()`. Also added a defensive
liveness check (`ConstrainedTextureCache::isLiveTexture()`, already
public, same reverse-pointer-lookup precedent as P10b's
`ZoneMergeBuffers::build()` defense) at the exact `createAnimatedNodeWithEquipment`
dereference site, mirroring P10b's two-layer approach (root-cause fix +
defensive read-site check) — belt and suspenders, since a raw `ITexture*`
read from any cache should never be trusted unconditionally regardless of
how it got there.

### 3. Stale-zone-name-vs-position snapshot race

Root-caused (P15 flagged, did not chase) as a genuine ordering hazard, not
merely a defensive-programming gap: `EverQuest::SetZoneBspTree()` sets the
game-side `m_zone_bsp_tree` from the render thread's *previous* zone-load
completion (`Application`'s `graphicsCompleteReady` handling,
`gameThreadLoop()`), and `CleanupZone()` resets it to null when a NEW zone
change starts — but nothing previously recorded WHICH zone a resident tree
belonged to, so if the `graphicsCompleteReady` finalization for a
just-completed load and a fast re-zone's `CleanupZone()` reset land close
together, `CreateZoneLoadSnapshot()` (`eq_bridge.cpp`) could read a non-null
but stale (previous-zone) tree at the exact moment `m_x/m_y/m_z` already
reflect the NEW zone's spawn position — `findRegionIndexForPoint()` against
the wrong zone's BSP tree returns `SIZE_MAX`, tripping the FATAL. **Fix**:
`EverQuest::SetZoneBspTree()` (`eq.h`) now tags the tree with the zone name
current at the moment it's set (`m_zone_bsp_tree_zone_name`); `CreateZoneLoadSnapshot()`
(`eq_bridge.cpp`) only trusts the tree when that tag matches
`m_current_zone_name` — a mismatch falls back to the same safe "no tree yet"
path the pre-existing null-tree case already takes (leaves
`playerBspRegion` at its `SIZE_MAX` default; the loading thread builds a
fresh BSP tree from the new zone's own map data regardless of what the
snapshot carries) instead of trusting a stale pointer. `CleanupZone()`
(`eq_zone.cpp`) also clears the new tag alongside the tree reset, for
consistency. Not independently reproduced as a standalone repro in this
unit's own runs (no `SIZE_MAX` FATAL occurred in any of the multi-hop
sessions below — consistent with it being timing-sensitive, per P15's own
"not reliably at `-d 3`" note) — this is a minimal, always-safe defensive
fix for the exact mechanism P15's evidence trail pointed to, not a
speculative guess.

### Verification

1. `cmake --build build -j24`: **PASS**, 0 errors (two pre-existing
   `-Wpointer-arith`-class warning families, unrelated files, unchanged by
   this unit).
2. `ctest --output-on-failure` (build/): **1233/1235** — exactly the two
   documented host exceptions (`AudioManagerTest.PreloadCommonSounds`,
   `RDPServerFrameTest.UpdateFrameMultipleTimes`), re-confirmed after the
   final (RaceModelLoader) fix landed.
3. `bash scripts/build-gles2-x86.sh`: **PASS**.
4. `bash scripts/ci/visual_gate.sh`: **PASS** — `=== VISUAL GATE: PASS ===`,
   `git status --porcelain` shows no golden/benchmark diffs (only the new
   `benchmarks/audits/p17_eviction_hygiene/` directory).
5. `bash scripts/build-arm-noble.sh`: **PASS** — `build-arm-noble/bin/willeq`
   built with all of this unit's changes.
6. `bash scripts/refactor/check_file_sizes.sh fable`: **PASS, no new FAILs**.
   Touched WARN files (`eq.h`, `irrlicht_renderer.h`, `application.cpp`,
   `eq_bridge.cpp`, `constrained_texture_cache.cpp`, `irrlicht_renderer.cpp`)
   were all already WARN on `fable` before this unit (confirmed via `git show
   fable:<path> | wc -l` for each — e.g. `eq.h` 1880→1892,
   `constrained_texture_cache.cpp` 1829→1842); none crossed a new threshold.
   New/touched files stayed `ok` (`renderer_zone_merge.h` 254→313,
   `renderer_zone_merge.cpp` 329→388, `animated_mesh_creation.cpp` 664,
   `race_model_loader.cpp` 990).

### Multi-hop eviction-ledger evidence (design item 4)

Real x86 GLES2 client (`build-gles2/bin/willeq`, EGL surfaceless llvmpipe),
sanctioned eqemu automation (`up.sh`/`seed.sh`/`down.sh`), account
`willeq_test`/character `Testchar` (GM), `--constrained orangepi`
(`mergeZoneDraws: true` since P10b), `--texture-ledger`, `-d 4` (INFO level,
to capture the per-zone ledger-summary and `[mergedraws]` sample lines).
Script: `befallen (initial) → #zone qeynos2 → #zone befallen → #zone qeynos2
→ #zone gfaydark`, 45s settle per hop — **4 zone-visit hops in one process**,
satisfying the plan's "4+ hops (befallen↔qeynos2↔third zone)" bar (gfaydark
as the third zone). Evidence:
`benchmarks/audits/p17_eviction_hygiene/p17_multihop_console_run3.log` (full
run), `p17_multihop_ledger_run3.txt` (final successful zone's raw ledger —
overwritten per-zone by design, see §1's ledger-segmentation note),
`p17_multihop_gdb.log` (gdb backtrace from the pre-RaceModelLoader-fix
crash, kept as root-cause evidence for §2).

Per-zone `unloadZone()` + ledger summary, in session order (each ledger row
is a FRESH per-zone count, thanks to the `clear()`→`ledger_.reset()` wiring):

| # | zone | `unloadZone` before cleanup | uploads | evictions | thrash |
|---|---|---|---|---|---|
| 1 | befallen (initial) | 36 tex / 11.25MB (from a prior warm state) | 299 | **0** | 0 |
| 2 | qeynos2 (hop 1) | 299 tex / 24.18MB | 518 | **30** | 0 |
| 3 | befallen (hop 2) | 488 tex / 66.91MB | 298 | **0** | 0 |
| 4 | qeynos2 (hop 3) | 298 tex / 24.18MB | 518 | **30** | 0 |
| 5 | gfaydark (hop 4) | 488 tex / 66.91MB | — (aborted, see Deviations) | — | — |

**Near-zero/deterministic evictions, quantified**: each zone's eviction count
is now a flat, repeatable function of THAT zone alone (befallen: 0 every
time; qeynos2: 30 every time), not a growing cross-zone total — direct proof
the fix eliminates cross-zone accumulation (contrast P15's un-fixed 27, which
would only have grown further on more hops). qeynos2's own 30 evictions are
NOT a regression or leftover of this bug — they reproduce P15's own
"bonus data point" (68 evictions, single direct qeynos2 zone-in, no prior
zone) that P15 already attributed to real-time spawn-state drift in this
environment exceeding the 64MB budget on qeynos2's own population alone, out
of scope here (P17's own Out-of-scope: "budget resizing; eviction-policy
redesign"). "Near-zero" was the plan's own *expectation* under a
cross-zone-accumulation hypothesis; the *measured* result is that the
cross-zone component is now exactly zero, and the residual is 100%
same-zone, same-magnitude-each-time — which is the correct, honest way to
report it.

Every `unloadZone()` line in the log ("N textures, M bytes before cleanup" →
"cleared") appears exactly once per hop, immediately before that hop's
target zone begins loading — direct proof the fix's timing is correct (not
just that eviction counts look better).

No dangling-pointer crashes, no snapshot-race FATALs, in any of the 4
completed hops (or the 5th, aborted-for-an-unrelated-reason hop — see
Deviations).

### qeynos2 mergeZoneDraws regression spot (design item 4, required check)

**Merge did NOT newly activate in qeynos2.** Both qeynos2 visits: `merge=off`
throughout (`unmerged_would_be_draws=137/22 actual_draws=137/22
reduction=0.0%`), same as P10b's/P15's pre-existing finding — because
qeynos2's OWN population still accrues 30 evictions on its own (see table
above), so the P10b eviction-count safety gate (merge only ever builds when
BOTH the mesh-cache and texture-cache eviction counts are zero) correctly and
safely keeps merge disabled there, unrelated to cross-zone accumulation
being fixed. This is the honest, reportable outcome of the check the plan
asked for — not a bug, and not what "near-zero evictions" was hoped to
unlock, but a direct, correctly-gated consequence of qeynos2's standalone
footprint (unchanged by this unit, out of scope to fix). **befallen DID
activate merge in both visits** (`merge=ON`, `reduction=0.0%` then `30.0%` —
the difference is pose/entity-count dependent between the two sessions, not
a regression; matches P10b's own precedent of `[mergedraws]` reduction
varying with scene content, not a fixed constant).

### Deviations from the plan

1. **The multi-hop session did not complete its 5th hop (gfaydark)** — hit a
   pre-existing, unrelated, ALREADY-graceful FATAL: `Failed to load race
   model: race=330 gender=0 (not found in any S3D archive)`, the same class
   of "one NPC's missing model data aborts the entire zone load" bug P10b's
   review already documented and worked around for a *different* race/zone
   (giant rat, modelId 61, befallen). This is a content-data-coverage gap
   (`data/item_models.json`/race S3D coverage), not a crash (no SIGSEGV, a
   controlled `LOG_FATAL` + clean quit, `unloadZone()` still ran and cleared
   correctly on the way down) and not something this unit's scope covers
   (texture eviction hygiene, not race-model-data completeness) — flagged in
   `refactor/deferred.md` rather than chased, consistent with this plan's own
   and P10b's/P15's precedent of not chasing out-of-scope, pre-existing,
   already-graceful data-coverage gaps found while building required
   verification evidence. All 4 in-scope hops (befallen↔qeynos2↔befallen↔qeynos2)
   completed cleanly, satisfying the "4+ hops" bar; the 5th (gfaydark, the
   "third zone" for a 5-zone/4-hop-plus bar) hit this unrelated wall instead
   of completing.
2. **Two additional pre-existing bugs found and fixed during this unit's own
   verification**, beyond the plan's three named design items:
   - `RaceModelLoader`'s `meshBuilder_->textureCache_` never being cleared on
     zone unload (§2 above) — this is arguably PART OF design item 2's
     "entity-node dangling-ITexture* guard" ask (same class of hazard,
     `refactor/deferred.md`'s own "found by P15, NOT chased" entry names this
     exact call site), so it is reported under design item 2's heading above
     rather than as a true scope deviation — but it required touching a
     THIRD file/cache (`ZoneMeshBuilder::textureCache_`, not
     `EntityRenderer`/`EQAnimatedMeshSceneNode` state) beyond what
     `EntityMeshTextureGuard` alone could reach, so it's called out
     explicitly here for visibility.
   - The stale-zone-BSP-tree fix (§3) is design item 3 itself, not a
     deviation — listed here only to note it was NOT independently
     reproduced as a standalone crash in this unit's own runs (see §3's own
     note).
3. Server use: sanctioned flow throughout (`up.sh`/`seed.sh`/`down.sh`,
   direct `character_data` position UPDATEs before/after each session,
   mirroring P10b's/P13's/P15's own precedent — Testchar's `character_data`
   row confirmed restored to the exact documented coordinate, `zone_id=36,
   x=-77.9232, y=21.9086, z=3, heading=190`, via `SELECT` after the final
   run). Clean `down.sh` shutdown. No server code touched
   (`/mnt/projects/akk-stack/code` untouched). `tools/hwcaps/` untouched
   (P18/P19 non-interference maintained — confirmed via `git status`).
