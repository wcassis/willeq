# T91 — Every zone entity's mesh is built at load, with no distance gate

**Slot: slot2. Headless. NO EQEMU — slot1 owns it this round. Do not bring
the server up.**

**Territory: the zone-load entity-preparation path in
`src/client/graphics/`, `tests/`, `refactor/`,
`/mnt/projects/fable/review/T91/`.**

Read T82's Review (the Nektulos load failure), T83's Review, then this plan.

## The defect

`loadZoneSequential` logs `Sequential: preparing 452 entities inline
(chrCacheMaxEntries=8)` and then builds a mesh for **every entity in the
zone**, inline and blocking, at load time. The 40-entity cap
(`Entities 40/171 (max 40)`) is applied at **render** time — after all the
expensive work is already done.

Measured consequence: Nektulos took **~230 seconds and never finished** (still
87% when killed), thrashing an 8-entry character cache against `global*_chr.s3d`.
That destroyed T82's Part 1 capture set outright.

Receiving 452 spawns is correct — EQ's `ZoneSpawns` sends the whole zone.
Building 452 meshes to draw at most 40 is not.

## Why this one matters beyond itself

It gates iteration speed generally. Any future probe/harness mode that skips
login and world but still calls the zone-load path inherits the same 230
seconds, so this is effectively a prerequisite for faster live testing rather
than an isolated performance bug.

## Part 1 — Characterize before changing anything

- Find the unconditional prepare-all loop and name it precisely (file,
  function, line).
- Is there **any** distance, visibility or count gate at load time? State what
  exists rather than assuming there is none.
- What is `chrCacheMaxEntries=8` doing against 452 entities? Quantify the
  thrash: cache hits vs misses, and the cost distribution — is time going to
  S3D reads, mesh construction, texture upload, or cache eviction?
- **Measure a baseline** on at least two zones of different populations
  (Befallen ~171, Nektulos ~452) so any improvement is measured, not asserted.

## Part 2 — The design question, which is Will's to settle

Do **not** pick an approach unilaterally. Characterize the options and their
trade-offs, then **report**:

- **Distance-gated build at load** — build only what is within some radius,
  build the rest on demand. Risk: pop-in, and a stall when the player turns.
- **Lazy/on-demand build** — build nothing at load, build on first visibility.
  Risk: stutter during play rather than at load, which may be worse on the Pi.
- **Raise the cache to match the working set** — cheapest change, but does not
  address building 452 meshes to draw 40.
- **Background/incremental build** — keep building after the zone is playable.
  Risk: threading, and the GL context is owned by the loading thread.

For each, state the effect on the **Orange Pi** (Mali400, 512MB) specifically,
not just the x86 dev VM. A change that helps the VM and hurts the Pi is not an
improvement.

## Part 3 — Only implement if one option is unambiguously safe and small

If the characterization shows an obviously-correct narrow change — most likely
sizing the character cache to the actual working set — implement that and
measure the result.

**STOP and report** if the fix requires restructuring the zone-load pipeline,
touching the GL context handoff, or changing the loading-thread model. That is
its own unit with its own review. Do not begin a redesign inside a
characterization unit.

## What you may not do

- **Do not start the eqemu server.** slot1 owns it this round.
- Do not change rendering behaviour to make loading faster — pop-in traded for
  load time is a product decision, not an optimization.
- Do not assert an improvement without a before/after measurement on both
  zones.

## Verification

Full build and suite (expect 2070/2071; sole failure the documented ALSA
`AudioManagerTest.PreloadCommonSounds`). Visual gate — **if goldens change,
that is a real signal here**, since this path feeds what gets drawn: say so
explicitly and explain why.

Measurements and findings to `/mnt/projects/fable/review/T91/` with a README.
If you produce images, open them and describe what is actually visible.

## Review

Executed in `/mnt/projects/fable/wt/slot2`, task branch
`task/T91-entity-prepare`, at fable tip `1cc77307` (no eqemu used or
touched — slot1 owned it this round; all "live" numbers below come from
T82's own already-captured production logs, mined for this unit). Full
write-up, tables, and the reproduction commands:
`/mnt/projects/fable/review/T91/README.md`.

### The prepare-all loop, precisely

`IrrlichtRenderer::loadZoneStep8_Entities`
(`src/client/graphics/renderer_zone_load.cpp:1298`). Unconditionally loops
every entity in `entityRenderer_->getEntities()` and runs the full inline
8a-8f chain for each, at load time, on the loading thread, before the
zone is playable.

### What gating exists today — stated, not assumed

**None at Step 8.** No distance, visibility, or count check exists in the
loop. The 40-entity render cap is a separate, later, render-time filter
with no interaction with Step 8.

A gate for exactly this purpose DOES exist elsewhere in the codebase:
`entityPrepMaxPvsDepth` (`constrained_renderer_config.h:88`), consulted by
`queueEntityPrepRequests()` (`renderer_zone_stream.cpp:676`) — but that
function only runs on the render thread, post-load, throttled to every
~10 frames, for entities that spawn/come into view after the zone is
already playable. It is never called from Step 8, and both shipped
constrained presets set it to 255 (unbounded) regardless.

### Cache-thrash quantification (real T82 captures)

| Zone | Entities | Step 8 wall time | Failed preloads | Time in failed preloads | % of Step 8 |
|---|---|---|---|---|---|
| Befallen | 171 | 5.848s | 14 (8.2%) | 1,050ms | 18% |
| Nektulos | 452 | **246.065s** | **130 (28.8%)** | **230,749ms** | **93.8%** |

Nektulos's Step 8 DID finish (`prepped 452 entities, built 452 meshes` at
14:35:03.993) — T82's "killed at 87%" was the client being killed ~0.6s
*after* completion, not a true hang.

93.8% of Nektulos's Step 8 time went to 130 entities whose race matches no
model anywhere (placeholder-cube fallback, the same race>367-ceiling
GM-summon-debris class T83 identified). Each such determination walks the
7-file `global_chr.s3d`+`global2-7_chr.s3d` fallback chain from scratch,
and 6 of those 7 files were logged MISS 128–130 of 130 times — despite an
8-entry cap that comfortably fits the 7-file working set on its own.
Befallen shows no such thrash (one 934ms first-miss, then permanent fast
hits) — its smaller/less diverse population never triggers eviction.

### Root cause, found by source reading, fixed

`RaceModelLoader::preloadVariantModel()` (`variant_loading.cpp`, Step 8b's
callee) had five call sites that inserted into `otherChrCaches_` via a
bare `operator[]` assignment, bypassing the `chrCacheLruOrder_`
registration and `maxChrCacheEntries_` eviction accounting that
`preloadModelData()`'s own `tryLoadFromArchive` lambda correctly performs.
`config/race_models.json` confirms this branch is genuinely reachable for
Nektulos (races 22/36/54/69/74/77, all present in the captured sequence,
have a canonical archive from a different, non-"global" zone — exactly
the triggering condition). Entries inserted this way permanently occupy a
cache slot outside the tracked LRU budget, forcing the legitimately-
tracked global fallback chain to be evicted and reloaded far more often
than the cap should allow.

**Fixed**: new `RaceModelLoader::storeOtherChrCacheLocked()` is now the
only insertion path into `otherChrCaches_` — all 5 buggy sites in
`variant_loading.cpp`, plus the 2 previously-correct-but-duplicated sites
in `race_model_loader.cpp` (including two failure-path early returns that
used to leave permanently-unevictable "poisoned" empty entries), now
route through it. 3 files changed, no test/config/golden changes.

### Honesty check: fix magnitude NOT confirmed headlessly

A standalone repro (temporarily added to `tests/test_inventory_model_view.cpp`,
deleted before this commit — method preserved in `review/T91/README.md`)
replayed T82's exact captured race/gender call sequences directly through
the real `RaceModelLoader` against real local EQ client data. Plain replay
(`preloadModelData` only) showed clean, non-thrashing behavior even before
the fix (1.88s for the full 150-call Nektulos sequence) — it doesn't
exercise `preloadVariantModel`, which is what motivated the deeper source
read that found the bug above. An extended replay adding the six
foreign-archive races' `preloadVariantModel` calls, run against both
pre-fix and post-fix code (via `git stash`/`stash pop` + rebuild), showed
**no meaningful before/after difference** (~2.0-2.4s either way) — most
likely because this dev VM's OS page cache is now warm for the ~17MB of
archive files (T82, T83, and this unit's own exploration all touched them
repeatedly), collapsing the per-miss cost that dominated T82's original
cold-NFS capture, and because the exact live per-entity appearance bytes
(headVariant/bodyVariant) that fired this path were not captured by T82's
log and could not be replayed precisely. **The fix is correct and safe on
its own source-level merits — an LRU cache that doesn't track its own
insertions cannot enforce its configured bound — but its real-world
impact on the measured 230s is not confirmed by this unit.** A live eqemu
re-run of T82's Nektulos scenario is the only way to get a real
before/after; flagged as a follow-up, not performed here (no eqemu this
round, per the plan's explicit prohibition).

### Part 2 — design options (Will's call, not decided here)

See `review/T91/README.md` for the full write-up. Summary:
- **Distance/PVS-gated build at load**: infrastructure already exists
  (`entityPrepMaxPvsDepth`/`queueEntityPrepRequests`), needs wiring into
  Step 8. Pi risk: a stall when the player turns toward a previously-
  unbuilt entity could blow the ~27ms/frame CPU headroom budget.
- **Lazy/on-demand build**: fastest load, but risks a stutter on the very
  first frame after the loading screen clears, contradicting the Pi
  baseline (qeynos2, 40-54fps with entities *already built*).
- **Raise the cache**: this unit measured the true base-model working set
  at exactly 7 archives (~17.1MB) — already inside the current 8-entry
  cap once the accounting bug above is fixed. Not raised, for lack of
  evidence it's needed; `otherChrCaches_` persists for the whole zone
  visit, so any increase is a real, non-free RAM cost on the Pi's 512MB.
- **Background/incremental build**: the shape `EntityPrepWorker` already
  implements for post-load streaming; doing it for initial load safely
  needs GL-context-handoff or loading-thread restructuring — the Part 4
  STOP condition, correctly out of this unit's scope.

### Build/test

1. `cmake --build build -j24` — clean.
2. `ctest --output-on-failure` — **2070/2071**, sole failure the
   documented ALSA `AudioManagerTest.PreloadCommonSounds` — matches the
   plan's exact expected baseline.
3. `bash scripts/build-gles2-x86.sh` — clean.
4. Visual gate — **PASS**, goldens untouched (`git status` clean except
   the 3 fix files throughout). Stated explicitly since this path feeds
   what gets drawn: an unchanged golden here is a real signal — this fix
   is pure cache bookkeeping, it never changes what gets built or how
   anything looks.
5. `bash scripts/build-arm-noble.sh` — clean cross-compile.
6. `bash scripts/refactor/check_file_sizes.sh fable` — 4 files checked (3
   from this unit + 1 pre-existing on this branch from the already-
   committed T90 fix, `eq_action_handler.cpp`, not touched by T91): 3 ok,
   1 pre-existing WARN (`variant_loading.cpp`, 1614 lines, unchanged
   line count before/after — already over 1500 before this unit).

### Deviations

1. Part 3 was interpreted as permitting characterization-driven root-
   causing, not just the plan's suggested "raise the cache" candidate —
   the measured evidence pointed at a genuine accounting bug rather than
   an undersized cache (the true working set, 7 archives, already fit the
   configured 8), so that bug was fixed instead of blindly raising the
   cap. Flagged here as a deviation from the plan's literal suggestion,
   though within its stated latitude ("if the characterization shows an
   obviously-correct narrow change").
2. A throwaway standalone repro test was added to
   `tests/test_inventory_model_view.cpp` mid-unit to characterize and then
   validate the fix, and deleted before the final commit — see "Honesty
   check" above. It never shipped; `git diff` against this unit's final
   commit shows only the 3 production fix files.
3. Could not confirm the fix's real-world magnitude against T82's 230s
   measurement — headless reproduction was insufficient (see "Honesty
   check"). Reported as an open follow-up rather than asserted as solved,
   per the plan's explicit "do not assert an improvement without
   measurement" rule.
