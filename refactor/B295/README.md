# B295 (#125) + B293 (#124): honour or remove, decided by reading

Two T407-census findings about members whose inputs are discarded. Decisions:

- **B295 — REMOVE.** `GraphicsArchiveIndex::buildIndex(eqPath, lazyMode, tick)`
  stored `lazyMode` into `lazyMode_` and never read it. The index is pure
  filename arithmetic: `scanArchives()` opens NO archive
  ("filename + size only, NO archive opening", `src/client/graphics/graphics_archive_index.cpp:191`),
  so the lazy/eager distinction the flag encodes ("closes archives after
  scanning" vs "keeps them open") has nothing to act on. Honoured is not an
  option because there is no behaviour to honour: the eager path is what every
  caller already gets.
- **B293 — HONOUR.** `ParticleManager::entityDirCallback_` was installed by two
  callers and invoked by none, but a real consumer exists: spray casts. The
  worker side of the seam was complete (require + consume); only the renderer
  half of the seam — the loop that resolves the worker's direction requests
  through the callback — was missing. The fix adds that loop, so the callback
  is now read on the production frame path.

## B295 — what lazy was meant to do, and why it is remove

`git log -S lazyMode -- include/client/graphics/graphics_archive_index.h` gives
exactly one commit: **aea2af12** (`aea2af12aba70f8dd0a96bc79a5629152dd52f35`),
*"Add progressive/deferred asset loading for constrained mode"*, 2026-02-19.
That commit introduced the `buildIndex(eqPath, bool lazyMode, ...)` parameter
and the `lazyMode_` member together. Reading the commit itself: its
`scanArchives()` was ALREADY pure filename arithmetic ("no archive parsing",
`git show aea2af12:src/client/graphics/graphics_archive_index.cpp`), so the
lazy/eager distinction the flag encodes ("closes archives after scanning" vs
"keeps them open") never had anything to act on — the member was stored at
`buildIndex` time and never read, from its first commit. Nothing was later
unwired; the flag was aspirational in the progressive-loading work that
carried it in, and by T407's census what remained of it was a write.

Note: `ConstrainedRendererConfig::lazyPfsLoading` itself is NOT in scope. It
is the constrained preset's own field and its other readers are intact — the
audio manager takes it as `setMemoryConstraints(..., lazyPfs)` and logs it
(`src/client/bridge/bridge_component_ui_audio.cpp:547-556`,
`src/client/audio/audio_manager.cpp:623,1083`), and the preset default
`lazyPfsLoading = true` lives at
`src/client/graphics/constrained_renderer_config.cpp:102`. Only the
GRAPHICS-ARCHIVE-INDEX half of the flag was discarded.

### What changed

- `include/client/graphics/graphics_archive_index.h`: the `lazyMode`
  parameter off `buildIndex()`, the `lazyMode_` member, and the stale
  "Lazy mode: closes archives after scanning; non-lazy: keeps them open for
  fast extraction" doc bullet. The class comment now states the index never
  opens an archive, which is why the flag went.
- `src/client/graphics/graphics_archive_index.cpp`: the definition drops the
  parameter and the `lazyMode_ = lazyMode;` store.
- `src/client/graphics/zone_load/renderer_zone_load.cpp` Step 5: the sole
  production call site now calls `buildIndex(ctx.eqPath)` for every preset.
- `tests/test_entity_model_sharing.cpp:199`: the one test call site (which
  spelled out the orangepi preset's `lazyPfsLoading` to pass through) now
  calls `buildIndex(clientPath())`.
- `scripts/refactor/check_write_only_fields.py`: the B295 `EXEMPT` row
  `("GraphicsArchiveIndex", "lazyMode_")` — struck, per the no-outlive rule
  (the checker fails on an exemption whose field is no longer write-only).

Tree-wide grep before the change: `buildIndex` on a `GraphicsArchiveIndex`
occurs at the two declarations, the one production call site and the one test
call site above; no tool or benchmark calls it. `lazyMode_` had no other
occurrence in the tree.

## B293 — the consumer exists; the missing link was the renderer arm

The mechanism, verified against the tree at this unit (all paths
`src/client/graphics/` unless noted):

1. **A spray cast is the production consumer.**
   `SpellVisualFX::isSprayCastStyle()` (spell_visual_fx.cpp:365) classifies
   Alteration/Abjuration spells as spray; `createCastGlow()` computes
   `bool spray = isSprayCastStyle(spell_id)` (spell_visual_fx.cpp:399) and the
   effect's emitters carry `useDynamicDirection` across to the worker
   (`sim_particles.cpp`, the `CreateSpellEffect` arm and the trigger arm).
2. **The worker REQUIRES the direction every frame.**
   `sim_particles.cpp:711-713`: an emitter with `useDynamicDirection` and an
   `attachEntityID` lands in
   `output.particleOutput.directionRequestEntities`.
3. **The worker CONSUMES the direction every frame.**
   `sim_particles.cpp:391-397`: `pi.entityDirections` →
   `emitter.dynamicDirection` → spawn velocity.
4. **The renderer feeds `pi.entityDirections` from a pending map**
   (`SimBridgeComponent`, `renderer_sim_bridge.cpp`).
5. **The missing half.** `SimBridgeComponent::applySimulationOutput()`
   resolved the PREVIOUS frame's `positionRequestEntities` into
   `pendingParticleEntityPositions_` (the position arm, `renderer_sim_bridge.cpp:932-937`)
   but had no direction arm: `pendingParticleEntityDirections_` was CLEARED
   each frame (line 930) and refilled with nothing, so
   `ParticleManager::entityDirCallback_` (installed by
   `src/client/graphics/renderer/renderer_entities.cpp` and
   `tools/model_viewer/model_viewer_spell_test.cpp`) was invoked by none and
   every spray cast fell back to the emitter's static `velocityBase`.
   T395's one-frame-behind rule (the emitter sees what the PREVIOUS frame's
   output named) applies to the position arm; the direction arm honours the
   same rule.

The card said "check `spell_visual_fx` and the emitter configs for a consumer
that was meant to use it" — steps 1-4 ARE that consumer, complete on the
worker side, which is why HONOUR (not delete) is the close. The two
installers keep their `setEntityDirectionCallback()` calls unchanged.

### What changed

- `include/client/graphics/environment/particle_manager.h`:
  `resolveEntityDirection(entityID, outDir)` beside `resolveEntityPosition()`,
  plus a doc note on the `EntityDirCallback` typedef naming the new arm.
- `src/client/graphics/environment/particle_manager.cpp`: the definition,
  a one-line read of `entityDirCallback_` — the member's first read in the
  tree, which is what takes the write-only ratchet's `entityDirCallback_`
  row from "write-only" to live.
- `src/client/graphics/sim/renderer_sim_bridge.cpp`: the direction arm beside
  the position arm — for each entity in
  `results->particleOutput.directionRequestEntities`, resolve through
  `ctx_.particleManager->resolveEntityDirection()` into
  `pendingParticleEntityDirections_`.
- `include/client/graphics/sim/sim_bridge_component.h`: three `friend class`
  lines for the new test's suite and its two cases — the hand-expanded
  FRIEND_TEST idiom the T116 lifetime fixtures use (no gtest include in a
  production header; the ARM cross-compile toolchain has no gtest).
- `include/client/graphics/irrlicht_renderer.h`:
  `Test_SetParticleManagerForSeam()` — a swap that hands the renderer a
  `ParticleManager` built by the test (production builds its own during
  init). It exists because `ctx_` holds a reference to the renderer's own
  `particleManager_` member, so the injected manager must live inside the
  renderer for the production body to see it.
- `tests/test_b293_entity_direction_resolution.cpp` (new) +
  `tests/cmake/spells.cmake`: the RED/GREEN net.
- `scripts/refactor/check_write_only_fields.py`: the `entityDirCallback_`
  EXEMPT row (carried as B342) — struck, per the no-outlive rule. The row's
  text ("registered callback the manager never invokes") stopped being true
  at the same commit as the B293 close.
- `refactor/BOARD.md`: B293 and B295 struck with the decisions and the
  evidence, one line each as found.

## Verification

All runs on this host (x86 dev VM, llvmpipe), base `06852f31`
(`origin/fable` at card cut). `origin/fable` advanced twice while this
branch was open and was merged in twice: first `ce7ecd66` (B760's code,
no overlap with this card's files), then `a1d14b1f` (B760's BOARD.md
strike — one conflict in the B760 entry region of `refactor/BOARD.md`,
resolved in fable's favour; this card's B295/B293 strike entries sit in
a different region and are intact).

### RED — the defect, on base

1. **B293 unit test red at base.** In a detached worktree at base
   `06852f31` carrying ONLY the test harness (the new test file, its
   `spells.cmake` registration, the three friend lines and the
   `Test_SetParticleManagerForSeam` seam — all additive), the test binary
   builds and the second case fails: `applySimulationOutput()` has no
   direction arm at base, so the counting callback never fires and the
   pending map stays empty. Exact output: see `red_test_run.log`
   (evidence dir `/home/user/kb-evidence/b295-b293-discarded-args/`).
   The first case (the worker names the caster in
   `directionRequestEntities`) passes at base — it is the sanity half.
2. **B295 write-only ratchet red at base.** In a detached worktree at base
   carrying ONLY the production fix (parameter/member deleted, the direction
   arm added — no ratchet row edits), `python3
   scripts/refactor/check_write_only_fields.py --repo . --check` exits 1:
   ```
   STALE exemption (no longer write-only): GraphicsArchiveIndex::lazyMode_ -- B295: buildIndex() stores its lazyMode argument and never reads it
   STALE exemption (no longer write-only): ParticleManager::entityDirCallback_ -- B342: registered callback the manager never invokes
   0 unlisted, 2 stale.
   ```
   (exact output in `ratchet_red.log`; `ratchet_base.log` is the same
   command at plain base: exit 0, 48 write-only fields, all exempt — so the
   stale failures are the fix's and nothing else's.)

### GREEN — at the head

- `python3 scripts/refactor/check_write_only_fields.py --repo . --check`:
  exit 0 — "write-only census clean: 45 fields, all 45 classified in
  EXEMPT". Base `06852f31` is 48; at the head that is 48 − 3: this card's
  two struck rows (`lazyMode_`, `entityDirCallback_`) plus
  `CameraController::mouseSensitivity_`, which B533 removed from
  `origin/fable` in the merge (not this card's line). Exact output in
  `ratchet_green.log`.
- `python3 scripts/refactor/check_zero_callers.py --repo . --check`: exit 0 —
  "zero-caller census clean: 364 functions, all classified in EXEMPT
  (boarded 364)" and "tests-only census clean: 292 functions, all classified
  in EXEMPT_TESTS (boarded 71, observer 169, seam 52)" (exact output in
  `zc_main_after.log`). The new `resolveEntityDirection` is NOT newly
  flagged: its production call through `ctx_.particleManager->` in
  `renderer_sim_bridge.cpp` counts as a use, exactly as the neighbouring
  `resolveEntityPosition`'s does (both are unlisted in the EXEMPT tables and
  both are swept as reached). The `Test_SetParticleManagerForSeam` and the
  test's three friends are test-only seams, and the tests-only census stayed
  clean — no new row needed.
- `./build/bin/test_b293_entity_direction_resolution`: 2/2 PASSED
  (rebuilt at the head; exact output in `green_test_b293.log`).
- `ctest --test-dir build -L hygiene` green at the head (before push; see the
  completion summary for the run).

### Builds

`willeq`, `model_viewer` and `test_entity_model_sharing` built green at the
head (`g_willeq.log`, `g_mv.log`, `g_ems.log` in the evidence dir) —
`model_viewer_spell_test.cpp` is inside the `model_viewer` target, so both
B293 installers are covered there. The full suite + visual gate run in CI on
the push (card's rule: CI's job). The gate frames are identical by
construction: the diff touches no golden, no manifest, and no render path
that changes pixels — B295 removes a discarded argument (the index was
already pure filename arithmetic) and B293 adds a resolution arm that at the
gate's benchmark shape (no spray casts, no particle commands) iterates an
empty `directionRequestEntities` set; the position arm it mirrors is the
template that already ran there.

## Not verified / left open

- **No live session was run** (the card is OFFLINE: no server, no display).
  The end-to-end visual claim — a spray cast's particles now spawn along the
  caster's forward vector instead of the emitter's static
  `velocityBase` — follows from steps 1-4 of the mechanism (the worker's
  consume half at `sim_particles.cpp:391-397` was already live; only the
  feed was empty) but was not observed in a zone. A `/cast` of an
  Alteration/Abjuration spell in a live lane would be the observation.
- **The model_viewer harness** (`tools/model_viewer/model_viewer_spell_test.cpp`)
  installs its own direction callback for manual FX review; it exercises the
  same `resolveEntityDirection` read but was not run interactively on this
  host (no display). It compiled green as part of the `model_viewer` target.
- **B494's separate gap stays open:** `getCachedArchive()` /
  `cachedArchives_` on `GraphicsArchiveIndex` are zero-caller and
  never populated by `scanArchives()`. That is a different finding (boarded
  separately, `check_zero_callers.py`'s B494 row) and was left alone; the
  header comment on both now names it so the next census pass is not
  surprised.
- The `Test_SetParticleManagerForSeam` seam and the three friend lines are
  test-only surface in two production headers. They are the T116 idiom
  (never-Init()'d renderer fixtures) and carry no runtime cost; a future
  test refactor of that idiom could carry them.
