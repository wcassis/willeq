# T463 — renderer textures evicted at zone unload and never reloaded (B522, B523, B524)

Class A (unconfirmed → confirm first), **no lock, no Pi**, slot2. Same defect
shape as B520 (the DRM software cursor, T460 — read its Review in
`refactor/T460_cursor_eviction_and_heading_units.md` and its test
`tests/test_software_cursor_eviction.cpp`, the model for this unit).

## The three candidates (T460's census, unconfirmed)

- **B522** `StormCloudLayer::cloudFrames_` — nulled by an eviction listener,
  never reloaded.
- **B523** `TumbleweedManager::tumbleweedTexture_` — no eviction listener, no
  protection, no reload: possibly a DANGLING pointer after
  `ConstrainedTextureCache::clear()` `removeTexture()`s it (use-after-free,
  the B482 class), not just a missing texture.
- **B524** `BoidsManager` `creature_atlas` — falls back to the placeholder
  after the first zone unload.

Known-good siblings for contrast: `ParticleManager` and `DetailManager`
reload in `onZoneEnter`.

## Tasks

1. For each: trace load → cache → zone unload (`clear()`) / LRU eviction →
   next zone's use. Confirm or refute with a pre-fix failing test driving the
   real `ConstrainedTextureCache` on an EDT_NULL device (T460 fixture shape):
   after `clear()` (and under budget pressure), the owner still draws a valid,
   live texture. For B523 prove whether the pointer dangles (the texture is
   removed from the driver while the member still holds it).
2. Fix per item, choosing with a written reason: reload in the owner's
   zone-enter hook (the `ParticleManager`/`DetailManager` idiom — preferred
   for zone-scoped effects, keeps them evictable) or `protectTexture()` (only
   for small process-lifetime assets). Every holder of a cache texture must
   either be protected, or be notified AND reload; add a guard where one is
   missing.
3. Close the class: a census test (source scan or registry) that fails when a
   renderer member holds a cache-loaded `ITexture*` with neither protection
   nor an eviction listener + reload path — or, if a generic net is not
   cheap, say so and pin each site.

## Proof

Standard verification bar (EXECUTOR_PROTOCOL): background builds with exit
files, `ctest -j6` (re-run load artefacts standalone —
`HeadlessShutdownTest` is one), G-batch goldens for every TU touched, visual
gate (no golden moves), ARM build, file sizes. Board B550–B559; strike
B522–B524 with evidence.

File territory: `src/client/graphics/environment/` (storm clouds,
tumbleweeds, boids), their headers, the texture cache only if a generic hook
is needed, new tests. **Not** `src/client/graphics/ui/` / frame timing
(T462, slot3), **not** camera/hotkeys/slash/player (T461, slot1).

STOP: a fix requires changing zone lifecycle order in
`renderer_zone_lifecycle.cpp` beyond adding a reload call (report the needed
change); an image golden moves.

## Review

Executor, slot2, branch `task/T463-evicted-texture-no-reload` off `fable`
`afbbb2dd`. No lock, no Pi, no server contact. Files: the three environment
owners and two of their headers, two new tests, one CMake fragment, the board.

### The three candidates -- all three CONFIRMED

Seam (T460's): the real `ConstrainedTextureCache` on an EDT_NULL device, the
real owners with their real `init()`, the real boids guard, and the renderer's
call order (`onZoneLeave()` -> `clear()` -> `onZoneEnter()`). "Drawable" never
dereferences a pointer. It means non-null, not the placeholder, a live cache
entry, AND still in the driver's own texture list
(`getTextureCount()`/`getTextureByIndex()`). That last condition is the
dangling proof: `removeTexture()` drops the driver's only reference.

**B524 `BoidsManager` creature atlas -- confirmed.**
- `BoidsManager::loadCreatureAtlas()` (boids_manager.cpp) runs only from
  `init()`, and nothing protects the texture.
- `ConstrainedTextureCache::clear()` notifies listeners and removes it.
  `ZoneStreamComponent::ensureBoidsTextureGuard()`'s `ForwardingTextureGuard`
  then calls `BoidsManager::swapEvictedTexture()`, which puts the placeholder in
  `atlasTexture_` and `creatureMaterial_`.
- `onZoneEnter()` only queues a worker command. `render()` binds
  `creatureMaterial_` every frame, so boids drew the placeholder for good.
- Pre-fix, both cases (two unloads; LRU) failed with `texture is the
  placeholder`.

**B523 `TumbleweedManager` texture -- confirmed, and it is a USE-AFTER-FREE.**
- `TumbleweedManager::init()` loads it with no listener, no protection and no
  reload. `createTumbleweedMesh()` bakes it into `tumbleweedMesh_`'s buffer
  material, and `applyWorkerResults()` copies that material into every new node
  (`addMeshSceneNode`).
- `clear()` -> `clearTextureReferences()` walks only the scene graph. With no
  tumbleweed spawned at that moment, nothing reaches the mesh, and
  `driver_->removeTexture()` frees the texture under it.
- Pre-fix, the mesh buffer, the next zone's first spawned node, and that node
  after a second unload all held `0x64d09ce50c90`, which was absent from the
  driver's texture list. So the texture was freed, and the next zone's spawn
  draws freed memory.
- Under LRU with a node alive, the scene walk swapped it to the placeholder
  instead.

**B522 `StormCloudLayer` frames -- confirmed.**
- `StormCloudLayer::onTextureEvicted()` nulls `cloudFrames_[i]`.
  `generateCloudTextures()`, from `initialize()`, is the only loader.
- `updateBlendedTexture()` returns on a null frame, and the scene walk had
  already moved the hidden dome to the placeholder.
- Pre-fix, after each of two unloads and under LRU, all 32 storm steps drew the
  placeholder: 1 distinct texture where a cycle shows 32.

### The fixes, and why each was chosen

| board | fix | why this, not the other |
|---|---|---|
| B524 | `protectTexture("creature_atlas")` in `loadCreatureAtlas()` | 64x64 (~22 KB accounted). Loaded once by a renderer-lifetime singleton and drawn in every zone. That is the "small process-lifetime asset" case. A reload would put a disk read and upload on a zone path for 22 KB. The T124 guard stays as the net. |
| B523 | `protectTexture("tumbleweed")` in `init()` | Same size and lifetime. It also removes the use-after-free structurally. A listener+reload would have to scrub the mesh buffer, every pool node, and the member from inside the cache mutex, then rebuild all three. |
| B522 | `loadFrame()` split out of `generateCloudTextures()`; new `reacquireFrame()` called from `updateBlendedTexture()` when the frame is null | 32 frames are ~11.2 MB accounted, not "small", so they stay evictable. **Deviation from the plan's preferred `onZoneEnter()` idiom, on purpose:** a zone-entry reload spends 11 MB of the orangepi's 64 MB in every zone whether it storms or not. It would also leave a mid-zone LRU eviction dark until the next zone. Re-acquiring at the point of use costs nothing until a storm is visible and covers both paths. It runs on the render thread, outside the cache mutex; the listener still only nulls. |

`renderer_zone_lifecycle.cpp` was not touched. No lifecycle order change was
needed, so the STOP condition was not approached.

### Closing the class -- a census, not just three fixes

`tests/test_texture_cache_holder_census.cpp`, `TextureCacheHolderCensus`:
- **`EveryGetOrLoadSiteDeclaresALifetimePolicy`.** Every `getOrLoad()` call site
  under `src/client/graphics` and `include/client/graphics` must be in a table
  with its call count and one policy:
  - Protected
  - ListenerReload
  - ZoneScoped
  - NotOnGles2

  There are 17 files and 21 sites; the cache's own TUs are excluded. A new
  file, or a changed count, fails until it declares one.
- **`EveryDeclaredPolicyHasItsMarker`.** Checks the literal each policy stands
  on. Examples: `protectTexture("tumbleweed")`, `reacquireFrame(`,
  ParticleManager's two `onZoneEnter` reloads, the region and entity guard
  registrations, `animatedTextureManager.reset()`.

Honest limits:
- It is a declaration net, not a proof of correct wiring. The behavioural tests
  do that for the owners T460 and T463 fixed.
- It covers `getOrLoad()` call sites only. A holder that gets its pointer from
  `uploadTexture()`, `getTexture()` or `registerTexture()` without calling
  `getOrLoad()` is not seen.
- `tools/hwcaps/p50_mask_plumbing_probe.cpp` (a standalone probe) is not
  scanned.
- The directory scan takes ~28 s on this NFS checkout. It passed inside
  `ctest -j6`.

### Tests, red before and green after

`tests/test_environment_texture_eviction.cpp` +
`tests/test_texture_cache_holder_census.cpp`, one target
`test_environment_texture_eviction` (`tests/cmake/renderer_guards.cmake`).
Red = built and run at `afbbb2dd` + tests only (plus the const
`BoidsManager::getCreatureMaterial()` accessor the test reads).

| test | pre-fix | post-fix |
|---|---|---|
| `BoidsDrawTheCreatureAtlasAfterEveryZoneUnload` | FAIL: placeholder, both unloads | PASS |
| `BoidsDrawTheCreatureAtlasUnderBudgetPressure` | FAIL: placeholder | PASS |
| `TumbleweedsNeverDrawAFreedTextureAfterAZoneUnload` | FAIL x3: `DANGLING (freed)` (mesh buffer, node, node after 2nd unload) | PASS |
| `TumbleweedsDrawTheirTextureUnderBudgetPressure` | FAIL: placeholder | PASS |
| `StormCloudsAnimateRealFramesAfterEveryZoneUnload` | FAIL: 64/64 steps placeholder, 1 of 32 frames seen, both unloads | PASS |
| `StormCloudsAnimateRealFramesUnderBudgetPressure` | FAIL: 32/32 placeholder, 1 of 32 frames | PASS |
| `TextureCacheHolderCensus.EveryGetOrLoadSiteDeclaresALifetimePolicy` | PASS (the table matched the tree; the fix changes no call count) | PASS |
| `TextureCacheHolderCensus.EveryDeclaredPolicyHasItsMarker` | FAIL: exactly boids, tumbleweed, storm markers missing | PASS |

### Board
- B522, B523 and B524 are struck with the evidence above.
- The census is recorded under B524.
- **B550** (A, lead, new): `OverlaysComponent::applyEnvironmentalDisplaySettings()`
  lazily creates a `TumbleweedManager` without `setConstrainedTextureCache()`,
  and `init()` then FATAL-exits the process.
  - It is reachable from Step11 of every zone load, but only when
    `rollingObjects` was off at renderer init and on later.
  - The fix is one statement, in `overlays/`, which is outside this plan's
    territory, so it is boarded, not fixed.

B551–B559 unused.

### Deviations
1. B522 is re-acquired at the point of use, not in `onZoneEnter()` (reason in
   the fixes table).
2. New public accessor `BoidsManager::getCreatureMaterial()` (const, test-only
   use). It is in territory, but it is an API addition.
3. The build script regenerated `build/` against its existing cache before
   building, because gmake did not yet know the new target. No flag or build
   type changed; `CMAKE_BUILD_TYPE=RelWithDebInfo` and the slot's own
   `EQT_GLES2_IRRLICHT_DIR` were confirmed at checkout.
4. No G-batch golden pins any touched TU (grepped `tests/goldens`). The golden
   tests ran green inside the full suite.

### Verification

| step | result |
|---|---|
| 1 `cmake --build build -j24` | PASS (exit 0, own exit file; no exit 143 in the unit) |
| 2 `ctest -j6` | PASS: `100% tests passed, 0 tests failed out of 4716`, exit 0 |
| 3 `build-gles2-x86.sh` | PASS (exit 0; `build-gles2/bin/willeq` rebuilt 19:17 UTC, valid x86-64 ELF) |
| 4 visual gate | PASS (`=== VISUAL GATE: PASS ===`, qeynos2/gfaydark/befallen, exit 0); `git status` shows no file under `tests/goldens/` changed |
| 5 `build-arm-noble.sh` | PASS (exit 0; `build-arm-noble/bin/willeq` rebuilt 19:21 UTC, valid ARM EABI5 ELF) |
| 6 file sizes (`--files`, 8 touched) | 8 ok, 0 warn, 0 fail |
