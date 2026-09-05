# T359 — Combat movement and Follow track a spawn id (B003d); loadSoundFont precondition (B015); setEntitySize limit (B011)

Class B, slot3. Three disjoint items; one branch, three commits.

## Leg A — B003d

T356 made moveTo/follow resolve ONCE (ranked, in the dispatcher) and pass a
spawn id down. The same unranked prefix resolver
(`EntitiesComponent::FindEntityByName`) still backs
`MoveToEntityWithinRange`, `StartCombatMovement(name)`, `UpdateCombatMovement`,
and `MovementComponent::Follow` re-matches its target NAME every tick
(`movement_update.cpp:183`).

1. Find every production caller of `FindEntityByName` (comment-filtered grep
   + the compiler by deleting the declaration). Record the list.
2. Combat movement: resolve once at the entry point (`StartCombatMovement`'s
   caller — find where the name comes from; if it is the current TARGET, the
   target's spawn id is already known and the name lookup is pure drift), pass
   the spawn id; `UpdateCombatMovement` reads the entity by id each tick.
3. Follow: store the spawn id; each tick look the entity up by id; if it is
   gone (despawned / zoned), stop following the same way a name miss does
   today (read what it does now and preserve it).
4. When `FindEntityByName` has zero callers, delete it (K12 ff check).
5. Tests, pre-fix failing: two identically-named mobs, the nearer spawned
   second; combat movement and Follow must track the nearer across two ticks
   (mirror T356's test). Movement TUs are in the G06 order golden: run
   `test_movement_order`; if a statement changes, the golden learns it IN
   PLACE (correction 32), both faults re-planted.
6. Strike B003d.

## Leg B — B015

`MidiPlayer::loadSoundFont` swaps a raw `tsf*` with no synchronisation; safe
only because its one call site runs before the audio thread exists. Assert
the precondition: a check that the audio thread is not running (an existing
flag / the thread handle) that LOGs and refuses the swap otherwise — do not
add a mutex to the audio callback. Test: call after the thread is up → refused
and logged; before → accepted. Strike B015.

## Leg C — B011

`EntityRenderer::setEntitySize` updates neither `collisionHeight` nor `deckZ`.
Nothing resizes a boat, so assert the limit: a test that `setEntitySize` on an
entity with a deck leaves `deckZ` unchanged AND a source-reader pin that no
production caller passes a boat — OR recompute both if the recompute is a
one-liner from the existing height formula (read it; decide; say which and
why). Strike B011.

## Proof

Build, `ctest -j6`, the new tests with pre-fix/post-fix runs, movement
golden, file size check; Leg C touches `src/client/graphics` → visual gate
standalone + ARM build. Tip carries willeq.db v6: rebuild all three trees
first.

STOP: `StartCombatMovement`'s name does not come from the target and has no
spawn id available (then report the shape and do Legs B and C).

## Review

Branch `task/T359-combat-movement-spawn-id`, base `d8338f65`.
Leg A `310725a4`, Leg B `c73f8745`, Leg C `23863718`; the Review is the
commit that adds this section.

### Leg A — the caller list, and where the name came from

`EntitiesComponent::FindEntityByName` had **five** production callers
(comment-filtered grep, then confirmed by the compiler when the four movement
ones were retyped):

| # | call site | where its name came from | outcome |
|---|---|---|---|
| 1 | `MovementComponent::MoveToEntityWithinRange` | `CombatManager::TickAwaitingCorpse` passed `it->second.name` from `entities.find(m_pending_corpse_id)`, and `StartCombatMovement(uint16_t)` passed `entity.name` from its own `entities.find(entity_id)` — **both already held the id** | now takes `uint16_t` |
| 2 | `MovementComponent::StartCombatMovement(const std::string&, float)` | **nothing — ZERO production callers** | deleted |
| 3 | `MovementComponent::UpdateCombatMovement` | `CombatState::combatTarget()`, i.e. the name the entry point stored | reads by `combatTargetId()` |
| 4 | `MovementComponent::FaceEntity` | `CombatManager::ProcessCombatRound` passed `entity.name` from inside `if (id == m_current_target_id)` — **already held the id** | now takes `uint16_t` |
| 5 | `CommandsComponent::RequestWarp` | `/warp <entity_name>` — a genuinely **user-typed** name | **unchanged** |

Plus the per-tick follow branch (`movement_update.cpp:183`), which used no
resolver at all: a linear scan re-matching `PlayerState::followTarget()` by
exact string over `ctx_.entities` every tick.

**Where `StartCombatMovement`'s name came from: nowhere.** The plan's STOP
condition asked whether the name reaches it from the target. It does not reach
it at all — the name-taking overload has had **no production caller** since it
was written. `CombatManager::ProcessCombatRound` has always called the
`uint16_t` overload (G03 co-located it) with `m_current_target_id`. So the entry
point was already id-resolved; the drift was entirely downstream, in what it
*stored* (a name) and what the tick then did with that name. The overload was
deleted rather than converted — it would have been a duplicate of the id one.

**`FindEntityByName` was NOT deleted.** Caller 5 survives: `/warp` takes a name
a human typed and genuinely needs a resolution. It uses the *unranked* one,
which is the same defect shape as B003b/B003c one command later — boarded as the
new **B070** (B, because `/warp` is a GM/debug command). Deleting the resolver
belongs to that item.

`FaceEntity` is a small deviation beyond the plan's four names, in the safe
direction: it is the same drift, in the same file, with the same caller holding
the same id, and converting it removed a fourth `FindEntityByName` call for a
one-token caller change. It is **not** the `/face` command — that goes
`ActionDispatcher::faceEntity` -> `EqActionHandler::setHeading` and was fixed by
T350; `MovementComponent::FaceEntity` has exactly one caller,
`ProcessCombatRound`.

### Leg A — the state change

`CombatState` and `PlayerState` now store the **spawn id** as the target
identity; the name rides along for the console/log lines only, written and
cleared with it so it can never disagree:

- `setCombatTarget(uint16_t, const std::string&)` / `combatTargetId()` /
  `hasCombatTarget()` tests the id / `clearCombatTarget()` and `reset()` zero both.
- `setFollowTarget(uint16_t, const std::string&)` / `followTargetId()` /
  `isFollowing()` tests the id / `clearFollowTarget()` zeroes both.

Consumers that only tested `followTarget().empty()` (`movement_update.cpp:376`
and `:440`, `zone_component.cpp:1033/1046`) are unaffected. `zone_component.cpp`'s
FixZ debug lookup, which scanned for the follow target by name, now matches on
`spawn_id` — the same ambiguity, in a log line.

A despawned follow target falls through and does nothing, which is exactly what
the no-match scan did before; that is pinned, not just asserted in prose.

### Leg A — tests, pre-fix and post-fix

Four new cases in `tests/test_movement_component.cpp`. Fixture: two
`a_fire_beetle`s, the FAR one at spawn **10** (entity x=100, the lowest id and so
the prefix matcher's pick), the NEAR one at spawn **20** (x=30) spawned second.
Both tracking cases run **two** `UpdateMovement()` ticks, because the per-tick
re-resolution is the defect.

Pre-fix = the three name resolutions re-planted in the bodies (`.cpp` only,
nothing else touched) so the failure isolates them:

```
[ RUN      ] MovementComponentTest.FollowTracksTheSpawnIdAcrossTicksNotTheNameMatch
tests/test_movement_component.cpp:238: Failure
The difference between ...targetX() and near_x is 70, which exceeds 5.0f, where
...targetX() evaluates to 100, near_x evaluates to 30
follow tick latched the far beetle (100) -- it re-matched the NAME
[  FAILED  ] MovementComponentTest.FollowTracksTheSpawnIdAcrossTicksNotTheNameMatch

[ RUN      ] MovementComponentTest.CombatMovementTracksTheSpawnIdAcrossTicksNotTheNameMatch
tests/test_movement_component.cpp:258: Failure
Expected: (...targetX()) < ((near_x + far_x) * 0.5f), actual: 96 vs 65
combat movement latched the far beetle (100) -- it re-resolved the NAME
tests/test_movement_component.cpp:260: Failure
The difference between ...targetX() and near_x is 66, which exceeds 10.0f
[  FAILED  ] MovementComponentTest.CombatMovementTracksTheSpawnIdAcrossTicksNotTheNameMatch
 2 FAILED TESTS
```

Post-fix: `test_movement_component` **12/12 pass**, including
`TheNameResolverStillPicksTheFarBeetle` (the precondition — the unranked
resolver really does return spawn 10, so tracking spawn 20 cannot be an accident
of iteration order) and `FollowTargetThatDespawnsJustStopsDrivingMovement`.
That last one passes both before and after by design: it pins *preserved*
behaviour, not a fix.

`test_state_modules` / `test_state_modules_secondary` learned the two-argument
setters and now assert the id round-trips and that clear/reset zero it.

### Leg A — the G06 movement golden

`test_movement_order` **4/4 pass, golden untouched, no statement learned.** The
G06 golden pins `src/client/graphics/player/renderer_player_physics.cpp` (the
renderer's per-frame `updatePlayerMovement`), not the game-side
`src/client/movement/` TUs this leg edits — the two are different movement
paths. Nothing to re-plant.

### Leg B — B015

The precondition is **asserted, not synchronised**. `MidiPlayer` gained
`setAudioThreadRunning(bool)` / `isAudioThreadRunning()` over an
`std::atomic<bool>`; `AudioManager::initialize` sets it at the single point all
three backend branches converge on (right after the started backend, before the
"Audio backend initialized" log) and `shutdown` clears it after
`backend_->stop()`. `loadSoundFont` checks it **first** and, if the backend is
up, emits a `LOG_ERROR` naming the hazard and returns false without loading a
file or touching a pointer. No lock was added to the audio callback — that was
explicitly rejected, it is the realtime render path.

Tests in `tests/test_midi_player_prealloc.cpp` (links `midi_player.cpp`
standalone — no device, no mixer). Pre-fix, the check compiled out with
`if (false && ...)`:

```
[ RUN      ] MidiPlayerPreallocTest.LoadSoundFontRefusedWhileTheAudioThreadIsRunning
tests/test_midi_player_prealloc.cpp:132: Failure
Value of: player.loadSoundFont(testSoundFontPath())
  Actual: true
Expected: false
the unsynchronised tsf* swap was allowed while the audio callback thread could be reading impl_->soundFont
[  FAILED  ] MidiPlayerPreallocTest.LoadSoundFontRefusedWhileTheAudioThreadIsRunning
 1 FAILED TEST
```

Post-fix, `test_midi_player_prealloc` **6/6 pass**, with the refusal line
visible in the run:

```
[ERROR] [AUDIO] MidiPlayer::loadSoundFont refused (.../data/1mgm.sf2): the audio
backend is running. The GM SoundFont swap is not synchronised against the audio
callback thread and would risk a use-after-free on the instance it replaces; it
may only run before the backend is created.
[       OK ] MidiPlayerPreallocTest.LoadSoundFontRefusedWhileTheAudioThreadIsRunning
```

The second case pins that a *failed load* (bad path) does not disturb the flag,
and the first pins that the refusal is reversible — after `shutdown` clears it,
the swap is accepted again.

### Leg C — B011: the limit, not the recompute

**Decision: assert the limit.** The plan offered "recompute both if the
recompute is a one-liner from the existing formula (read it; decide)". It read
as one line and is not one:

- the formula exists **twice already, with different inputs** —
  `entity_mesh_build.cpp:519` uses `mesh->getBoundingBox()` and a local `z`,
  `entity_build_step.cpp:388` uses `vis.meshNode->getMesh()->getBoundingBox()`
  and `vis.lastZ`. A third copy in `setEntitySize` would have to pick one, and
  whichever it picked would be wrong for entities built by the other path.
- **nothing can reach it.** eqemu's size changes are all `Mob::ChangeSize()` —
  SPA 89 ModelSize and SPA 58 Illusion, cast on players and NPCs. Boats (race
  72/73) are zone-owned static spawns nothing casts on. A recompute here would
  be untested-because-unreachable code, which is the exact defect class this
  board keeps finding.

So the limit is now stated in the source next to the code that has it, and
pinned by `EntityRescaleNodeTest.ResizingABoatLeavesItsDeckSurfaceWhereItWas` —
which builds a **real race-72 Ship mesh** from the client tree
(`collisionHeight` 338.78, `deckZ` 199.39), doubles the boat's server size,
checks the scene node genuinely rescaled 2x, and asserts both collision values
held. It `GTEST_SKIP`s only if that mesh does not build; on this host it does
not skip (2.3 s, real model load).

There is no "fix" here to fail before, so the red/green pair is the one that
matters for a limit pin — **is the assertion load-bearing?** With the recompute
planted in `setEntitySize`:

```
[ RUN      ] EntityRescaleNodeTest.ResizingABoatLeavesItsDeckSurfaceWhereItWas
tests/test_entity_size_change.cpp:429: Failure
  ...collisionHeight  Which is: 677.5625   vs  heightBefore  Which is: 338.78125
tests/test_entity_size_change.cpp:430: Failure
  ...deckZ            Which is: 368.78125  vs  deckBefore    Which is: 199.39062
[  FAILED  ] EntityRescaleNodeTest.ResizingABoatLeavesItsDeckSurfaceWhereItWas
```

With the plant removed: `[  OK ]`. If a future unit makes boats resizable, this
test fails and the comment tells it what to do.

### Verification

| step | result |
|---|---|
| `cmake --build build -j24` (GLES2 config, tests on) | EXIT=0 |
| `ctest --test-dir build -j6 --output-on-failure` | **3865 of 3867 passed.** Failures: `AudioManagerTest.PreloadCommonSounds` (standing ALSA host quirk) and `visual_gate` (below) |
| `test_movement_order` (G06 golden) | 4/4 pass, golden untouched |
| `bash scripts/build-gles2-x86.sh` | EXIT=0 |
| `visual_gate` standalone | **PASS** (21.1 s) |
| goldens | untouched; no golden or fixture file in the diff |
| `bash scripts/build-arm-noble.sh` | EXIT=0, `build-arm-noble/bin/willeq` = ELF 32-bit ARM EABI5 |
| `bash scripts/refactor/check_file_sizes.sh fable` | 32 files checked, **32 ok, 0 warn, 0 fail** (largest touched: `tests/test_state_modules.cpp` 1087) |

**`visual_gate` failed twice before it passed, for two different reasons, and
neither was this branch.** Inside the suite it failed with
`characterModelsBytes mismatch` in **all three** zones (qeynos2 expected
25737076 got 24701259, gfaydark 25368606/24332789, befallen 25496483/24460666) —
the identical-deficit signature of a stale `build-gles2` binary against the
tip's willeq.db v6 (T304/T355). Rebuilding `build-gles2` cleared it completely.
The next standalone run then failed on `befallen: p95 process-CPU ms 11.102
exceeds ceiling 10.948` — 1.4% over, immediately after the ARM cross-build
finished, i.e. the documented load artefact. Re-run clean: PASS, 0 failed. No
graphics behaviour changed in this branch (Leg C adds a comment and a test).

### Deviations

1. **`FaceEntity` converted too** (plan named four call sites; this is a fifth).
   Same drift, same file, same already-resolved caller. Flagged, not absorbed.
2. **`FindEntityByName` NOT deleted** — plan step 4 was conditional on zero
   callers and `RequestWarp` keeps one. Boarded as **B070** rather than scoped in.
3. **Leg C did not recompute** — the plan's own either/or, decided against with
   the reading above.
4. **`zone_component.cpp`'s FixZ debug scan** also learned the spawn id. Same
   name-match ambiguity in a log line; one token.

### Surprises

- `StartCombatMovement(const std::string&, float)` — the function the plan was
  built around — had **zero production callers**. The board entry (and T356's
  Review, which sourced it) named it as a live consumer of the unranked
  resolver; it never was one. The reachable defect was the *stored name*, not
  this entry point.
- The `ar` temp files `build/lib/st3rKOTI` and `stcc8pBm`, **3.1 GB each**, left
  behind by earlier timeout-killed builds in this slot (08:09 and 16:52,
  predating this unit). Found by the post-143 sweep and deleted; the real
  `libwilleq_core.a` was intact with 321 members. Worth a sweep of the other
  slots — that is 6 GB of a 93%-full volume.
