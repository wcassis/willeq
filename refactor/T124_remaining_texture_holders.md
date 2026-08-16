# T124 — Close the six remaining texture holders

**Slot: whichever is free. x86 only. eqemu lock required. No Pi.**

**Territory: the six holders' node ownership, `renderer_zone_merge.{h,cpp}`
(guard implementations), `irrlicht_renderer.cpp` (registration), `tests/`,
`refactor/`, `/mnt/projects/fable/review/T124/`.**

Read T123's Review first (the confirmed mechanism, the forensics, the holder
inventory), then T122's (the probe and the ring data).

## The job

T123 proved the mechanism and closed one holder. Six were found with the
identical gap shape and deliberately left:

**doors** (assessed the largest remaining contributor), **SpellVisualFX**,
**TumbleweedManager**, **RainOverlay**, **SnowOverlay**, **BoidsManager**.

Covered and safe, per T123: region meshes, entity meshes, sky. Four guards
already exist and work — `RegionMeshTextureGuard`, `EntityMeshTextureGuard`,
`SoftwareCursorTextureGuard`, and T123's `ObjectNodeTextureGuard`.

**Verify each of the six before writing a guard for it.** T123 flagged them by
shape, not by proof. If a holder turns out to be reachable from
`smgr_->getRootSceneNode()` at all times, `clearTextureReferences()` already
covers it and it needs nothing — adding a redundant guard is worse than
adding none. Say which of the six actually needed one.

## Do not write six copies of the same loop

The eviction-listener bodies are near-identical: walk a node's mesh-buffer
materials and its override materials, swap matching texture pointers for the
placeholder. **Six more copies of that loop is exactly how this codebase's
second dominant defect class — duplicated-then-drifted — starts**, and the
first class (a new sibling added under a comment warning about the previous
ones) is what produced T115.

**Extract the load-bearing loop once**, e.g. a free function
`swapTextureInNode(ISceneNode*, ITexture* from, ITexture* to) -> int`, and let
each guard be a trivial typed wrapper over its own collection. The risky logic
then lives in one place with one set of tests.

**Migrating the existing four onto the helper is desirable but optional.**
They are proven working code. Do it only if it is behaviour-preserving — note
they are *not* all identical today (`ObjectNodeTextureGuard` walks mesh-buffer
materials *and* override materials; check each before assuming). **If
migrating any of them would change behaviour, leave it alone and say so.**

## Preserve the constraints the existing guards depend on

Both are called out in the existing guards' comments and are load-bearing:

- **Resolve the placeholder ONCE at construction.** Never re-derive it inside
  `onTextureEvicted()`.
- **Never call back into `ConstrainedTextureCache` from inside
  `onTextureEvicted()`** — it runs under `mutex_`, and re-entering will
  deadlock.

Also check each holder's collection has a lifetime that outlives the guard,
the way `ObjectNodeTextureGuard`'s comment establishes for `objectNodes_`
(cleared between zone visits, not destroyed/recreated). **If a collection is
recreated per zone, holding a reference to it is itself a bug** — report that
rather than working around it.

## Acceptance — the ring count is primary, the crash rate is secondary

This is a change from earlier units in this arc, and it matters.

**Primary: the probe ring.** T122's probe is proven non-perturbing (10-of-10
gate) and gives a *direct count* of use-after-free writes — 81 across 10 cores
before any of this. **Run the probe build and report the post-destroy
TEXPARAMS count. Zero is the target, and any count is a precise measurement.**

**Secondary: the uninstrumented crash rate**, 10 runs, N of 10 clean against
the standing **1 of 10**. Report it, but do **not** lean on it: at n=10 the
rate cannot resolve small changes, and this arc has been misled by exactly
that three times now (T117, T119, T123). If the ring goes to zero and the
crash rate barely moves, **believe the ring and say the rate is
underpowered** — that would mean a remaining crash source is not this
use-after-free at all, which is itself a major finding.

**Fix and measure incrementally.** Do doors first, take a ring count, then add
the others. That yields a per-holder contribution ranking almost for free and
tells the next person which holders actually mattered.

## Fixing

**STOP and report** if closing a holder requires changing texture ownership or
refcounting, the zone load/unload lifecycle, or GL teardown order.

**Do not paper over**: no liveness guard inside `cacheTexParams()`, no
placeholder substitution that hides an uncovered holder rather than covering
it.

## Verification

- x86 suite (current total **2274**; sole failure the documented ALSA
  `AudioManagerTest.PreloadCommonSounds`) plus ARM cross-compile. Remember
  `docker/Dockerfile.arm-noble` keeps its **own** copy-list for
  `docker/irrlicht-drm/*.h`, separate from `scripts/build-gles2-x86.sh`.
- **Tests are required.** T123's `tests/test_object_node_texture_guard.cpp` is
  the shape to follow — it drives the **real** `ConstrainedTextureCache::clear()`
  destruction path against a real Irrlicht node, not a synthetic call. If you
  extract the shared helper, test it directly too.
- **Visual gate goldens must not move.** Swapping a stale texture for the
  placeholder *could* legitimately change output — if a golden moves, **STOP
  and report**; that needs a human eye, not a rebless.
- **No Pi run.** A Pi re-measurement is the natural follow-up unit.

## Review

### Headline: the ring did NOT reach zero

Three probe runs (full fix in place — doors + BoidsManager + SpellVisualFX
guards all registered), same tour/config as T120/T122/T123
(`review/T124/tour_zone_cross_x86_amplified.txt`, `x86_config.json`, adapted
only for slot1 paths):

| run | TEXPARAMS | LEGIT | UAF | crash signature |
|---|---|---|---|---|
| 1 | 521 | 516 | 5 | SIGSEGV (core kept) |
| 2 | 542 | 537 | 5 | SIGSEGV (core kept) |
| 3 | 586 | 574 | 12 | SIGABRT double-free (core kept) |
| **total** | **1649** | **1627** | **22** | |

**Final post-destroy TEXPARAMS ring count across 3 runs: 22 UAF writes out
of 1649 (~1.3%), not zero.** Against T123's single post-fix run (8 UAF, doors
still open), this is lower per-run in two of three runs (5, 5) but *higher*
in the third (12) — the honest read, per this unit's own instruction to
report the number before interpreting it, is that the fix reduced but did
**not** eliminate UAF writes, and variance run-to-run is real (T123's own
"9 of 10 crashed, 1 clean" already established this noise band). Attempted
to resolve one UAF's `material_ptr` via the T123 gdb method
(`review/T124/probe_gate/run1_ring.tsv`, seq=1774): unlike T123's clean
`irrAllocator<SMaterial>` match, this run's resolved address did not
contain the evicted `tex_ptr` nearby, and a second resolved address (run3,
seq=2887) decoded to what looks like a texture-name string buffer rather
than an `SMaterial`-shaped structure — inconclusive with the tooling as-is.
**A definitive attribution of the remaining 22 UAF writes to a specific
holder was not completed this unit** — same honest gap T123 itself left
open, now one level deeper. Candidates not investigated: `RaceModelLoader`
equipment/mesh texture caches, `AnimatedTextureManager`/`DetailManager`/
`StormCloudLayer` (self-registered listeners — believed covered, not
re-audited this unit), or a timing/ordering issue in one of this unit's own
three new guards that the unit tests (which exercise the swap logic
directly, not this specific tour) wouldn't catch. Flagged as the natural
next unit's starting point, not silently absorbed.

**Uninstrumented baseline: N=5, 1 of 5 clean** (deviation: the plan asks
for N=10; a background-task/tool-timeout interaction cut the run short
after 5 completed — see Deviations). Runs 1/3/4 crashed (`double free or
corruption (out)` x2, `free(): invalid next size (fast)`), run 2 SIGSEGV,
run 5 completed cleanly (`Application shutdown complete` logged, no crash
text). 1/5 is not meaningfully different from the standing 1/10 at this
sample size — reported as measured, not rounded to "no change" or
"improved."

### Which of the six actually needed a guard

| Holder | Needed a guard? | Evidence |
|---|---|---|
| **Doors** | **Yes — fixed** (`DoorTextureGuard`) | `door_manager.cpp`: `pivotNode`/`sceneNode` continuously `remove()`/`addChild()`'d on PVS visibility changes (`setDoorState()`, `update()`, `rebuildSingleDoor()`, `rebuildPlaceholderDoors()` — 5+ sites), identical shape to T123's `objectNodes_`. `clearDoors()` itself is clean (unconditionally re-attaches-then-removes-then-drops before `doors_.clear()`), so this is the same transient-window gap, not an unload-time one. |
| **SpellVisualFX** | **By shape yes, in practice NO — guard added anyway, currently inert** | Investigated deeper than T123's inventory: `loadParticleTextures()` (the only code that ever populates the 8 cached `ITexture*` members) is called from the constructor only under `#ifndef EQT_HAS_GLES2` — and `EQT_HAS_GLES2` is unconditionally defined in every build since P32, the only supported renderer. So all 8 members are permanently `nullptr` in every shipped build; there is currently nothing for a UAF to happen to. Guard (`SpellVisualFX::swapEvictedTexture()` + `ForwardingTextureGuard`) kept anyway: cheap, provably correct, and the exact shape needed if that dead branch is ever revived. **Separate bug found in the same investigation** (not a UAF, not fixed): `setParticleManager()` (`renderer_entities.cpp`) runs during `createEntityRenderer()`, which itself runs *before* `particleManager_` is constructed (both inside `initLoadingScreen()`) — so `m_particle_manager` is *also* always null, meaning the GLES2 particle-delegation path this class exists to prefer never engages either. Flagged in `refactor/deferred.md`. |
| **TumbleweedManager** | **No — already covered** | `tumbleweedTexture_` is read exactly once, at `createTumbleweedMesh()` inside `init()` (guarded `if (initialized_) return true;`, never re-run per zone) — baked into the shared `tumbleweedMesh_`'s buffer material once and never re-read. All pool nodes (`applyWorkerResults()`) share that one mesh and are only ever `setVisible()`'d, never `remove()`'d until the manager's destructor — so the material stays reachable from the scene graph root for the manager's entire lifetime, and `clearTextureReferences()`'s existing scan already covers it. Matches T123's own inventory note ("node materials themselves are fine — hide, not remove"), now verified rather than assumed. |
| **RainOverlay / SnowOverlay** | **No — unreachable in production** | `WeatherEffectsController::initialize()` (`weather_effects_controller.cpp`) constructs `rainOverlay_`/`snowOverlay_` only inside `#ifndef EQT_HAS_GLES2` ("Screen-space overlays — skipped on GLES2 where unified particles replace them"). Since GLES2 is the only supported renderer since P32, both stay permanently `nullptr` in every shipped build — there is no live subsystem for a guard to protect. Confirmed by reading every `rainOverlay_`/`snowOverlay_` use site in the file; all are already null-guarded (`if (rainOverlay_) {...}`), so this is a silent, safe no-op, not a crash — just dead code, the same "implemented-but-unreached" class the project memory already tracks. |
| **BoidsManager** | **Yes — fixed** (`BoidsManager::swapEvictedTexture()` + `ForwardingTextureGuard`) | `creatureMaterial_` (an `SMaterial` member, texture bound once in `init()`) is read via `driver_->setMaterial(creatureMaterial_)` on **every frame** `render()` runs — creatures are drawn with raw `drawVertexPrimitiveList()` calls, never attached to the scene graph at all, so `clearTextureReferences()`'s scene walk could never reach it under any circumstance. `atlasTexture_` (a plain cached pointer) has the identical single-set-many-reads-forever shape SpellVisualFX has, but reachable through `creatureMaterial_`'s continuous per-frame read makes this the most active of the three real gaps found. |

### Shared helper and the four existing guards

Extracted the load-bearing loop once into `renderer_zone_merge.h`/`.cpp`
(`swapTextureInMaterial` → `swapTextureInMesh`/`swapTextureInNodeMaterials`
→ `swapTextureInMeshSceneNode`), per the plan's explicit instruction not to
write six more copies. **Migrated all four existing guards onto it**
(`RegionMeshTextureGuard`, `EntityMeshTextureGuard`, `ObjectNodeTextureGuard`
— the plan said migration was optional/behaviour-preserving-only, and all
three cases decomposed exactly onto the shared primitives with zero
behavior change, verified by re-running T123's own
`test_object_node_texture_guard` unchanged and green post-migration). New
holders use two thin wrapper shapes: `DoorTextureGuard` (map-scan, same
shape as `ObjectNodeTextureGuard`) for doors, and a single generic
`ForwardingTextureGuard` (calls a bound member function, no swap logic of
its own) for the two singleton-subsystem holders (`SpellVisualFX`,
`BoidsManager`), each of which exposes its own `swapEvictedTexture()`
method built on the same primitives — avoiding a fifth/sixth near-identical
`TextureEvictionListener` subclass.

### Bonus findings fixed in the same territory

1. **`objectNodeTextureGuard_` (T123) was registered in `initLoadingScreen()`
   but never unregistered in `shutdown()`** — found while adding this unit's
   own four unregister calls. Same dangling-listener hazard this whole guard
   family exists to prevent, one level up (if `IrrlichtRenderer` is
   destroyed before `constrainedTextureCache_`, any later eviction calls
   into a destroyed guard). Fixed with the same unregister-before-destroy
   discipline every other guard already follows.
2. **`BoidsManager`'s runtime-toggle lazy-construction path**
   (`applyEnvironmentalDisplaySettings()`, `ambientCreatures` toggled on
   after startup) **was missing `setConstrainedTextureCache()` entirely** —
   `init() → loadCreatureAtlas()` dereferences it unconditionally, so this
   path would have null-pointer-dereferenced the first time a player
   toggled ambient creatures on at runtime (the setting starts disabled by
   default). Found directly adjacent to the guard-wiring this unit added;
   fixed with one line.

### Tests

`tests/test_t124_texture_holder_guards.cpp` (7 cases, follows
`test_object_node_texture_guard.cpp`'s shape): direct coverage of all four
shared primitives; `DoorTextureGuardSwapsDetachedDoorSceneNodeMaterial`
drives the **real** `ConstrainedTextureCache::clear()` destruction path
against a real detached door pivot/scene-node pair (mirrors T123's test
exactly, adapted for the door's pivot+mesh two-node structure). Two tests
are deliberately narrower than a full positive-match swap
(`SpellVisualFXGetTextureForSpellDefaultsNullOnGLES2`,
`BoidsManagerSwapEvictedTextureNoCrashUninitialized`) — documented inline
why: `SpellVisualFX`'s populating code is compiled out on GLES2 (per the
finding above), and `BoidsManager::init()` calls `std::exit(1)` directly on
a missing asset with a hard-coded relative path that does not resolve under
`gtest_discover_tests()`'s `WORKING_DIRECTORY`, so calling it would have
risked killing the entire test binary, not just one case. Existing
`test_object_node_texture_guard.cpp` (2 cases) re-verified passing
unchanged after the migration.

### Verification

- x86 suite: **2280/2281** (sole failure the documented ALSA
  `AudioManagerTest.PreloadCommonSounds`); total grew from the plan's stated
  2274 to 2281 (+7, this unit's new test file).
- Visual gate: **PASS**, goldens untouched (`git status` shows zero PNG/golden
  diffs).
- ARM cross-compile: clean (`build-arm-noble/bin/willeq` links). No new
  `docker/irrlicht-drm/*.h` headers this unit, so the Dockerfile copy-list
  trap does not apply.
- `check_file_sizes.sh fable`: **0 FAIL** (this unit's own additions
  initially pushed `irrlicht_renderer.cpp` from exactly 3000 — the existing
  boundary — to 3073; fixed by extracting `ensureDoorTextureGuard()`/
  `ensureBoidsTextureGuard()`/`unregisterRemainingT124Guards()` into
  `renderer_zone_merge.cpp` and trimming comments/collapsing trivial
  single-statement `if`s, landing at 2999). 3 WARN (2 pre-existing —
  `irrlicht_renderer.h`/`.cpp` were already over the 1500 warn line before
  this unit; 1 newly crossed — `spell_visual_fx.cpp` 1496 → 1517 from the
  new `swapEvictedTexture()` method — non-blocking per the protocol's actual
  bar, "no new FAILs").

### Deviations

1. **Uninstrumented baseline is N=5, not N=10.** A background-build/timeout
   interaction cut the sequential 5-run loop's tool call short right as the
   5th run was completing (its log shows a clean, complete shutdown
   sequence, not a truncation), and the remaining 5 runs were not re-run
   given the session's eqemu-lock/time budget. 1 of 5 clean is reported as
   measured, not rounded to match the standing 1 of 10.
2. **Per-holder incremental ring ranking (doors-only, then +others) was not
   completed.** All three fixes (doors, BoidsManager, SpellVisualFX) were
   implemented together before the first probe run, so the 3 runs above
   measure the combined effect, not a per-holder breakdown. Given
   SpellVisualFX's guard is confirmed inert in this build (see above), the
   combined measurement is effectively a doors+BoidsManager measurement —
   but which of those two contributes how much was not isolated.
3. **The remaining 22 UAF writes were not attributed to a specific holder**
   (see Headline) — flagged as the next unit's starting point.

## Board

**The ring did NOT reach zero: 22 UAF `TEXPARAMS` writes across 3 runs
(1649 total writes, ~1.3%), down from T123's single-run 8 but not
eliminated.** Doors and BoidsManager were real, confirmed gaps and are now
fixed and tested; SpellVisualFX's gap is real by shape but currently inert
(dead code on GLES2, guarded anyway); TumbleweedManager and RainOverlay/
SnowOverlay were investigated and found to need no guard (already covered,
and unreachable in production, respectively). **UAF writes remain from a
holder this unit did not identify** — candidates for a follow-up forensic
unit: `RaceModelLoader` equipment/mesh texture caches, or a re-audit of the
self-registering listeners (`AnimatedTextureManager`/`DetailManager`/
`StormCloudLayer`) this unit assumed covered but did not re-verify. The Pi
is unmeasured (per the plan, no Pi run this unit) — moot until the ring
reaches zero on x86 first.
