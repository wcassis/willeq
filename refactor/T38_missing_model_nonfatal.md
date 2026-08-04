# T38 — One entity's missing model must not abort the whole zone load (race=415)

**Slot: t1. x86 only. No Pi. OWNS EQEMU (lock protocol applies) — the
acceptance check is a graphical live session.**
No sibling unit.

**Territory: `src/client/graphics/renderer_zone_load.cpp` (the step-8
per-entity chain and its caller loop), plus tests.** Do not change
`RaceModelLoader`'s lookup order or the race-data tables — this unit is
about failure handling, not model coverage.

Read `refactor/EXECUTOR_PROTOCOL.md`, then the deferred entry
**"`race=415` missing model data still crashes graphics mode (T32
re-test)"**, then `refactor/T32_loot_verification.md`'s Review section on
the race=415 leg — T32 has a committed repro script
(`benchmarks/audits/T32/run_race415_graphics.sh`).

## The defect

`loadZoneStep8a_BaseModelPrep()` (`renderer_zone_load.cpp:1397`) treats a
single entity's missing base model as a **fatal zone-load failure**:

```cpp
LOG_FATAL(MOD_GRAPHICS, "Failed to load race model: race={} gender={} (not found in any S3D archive)", ...);
pendingZoneComputations_.reset();
loadFailed_ = true;
return false;                       // caller: if (!...) return false;  -> whole zone load dies
```

So one NPC the client has no model for takes down the entire zone. That
is what blocks graphical live sessions today, and it is the same shape as
the long-recorded gfaydark `race=330` FATAL and the recorded
"one NPC with a missing `item_models.json` equipment entry ABORTS the
whole zone load (FATAL) — should be WARN+skip".

**The graceful path is already half-built**: `ctx.entitySuccess` exists,
is set by 8a, and `loadZoneStep8b_VariantModelPrep()` already guards on
it (`if (ctx.entitySuccess)`). But 8c/8d/8e do **not** guard, and 8a
aborts before any of that matters. A designed fallback also already
exists further down (`src/client/graphics/CLAUDE.md`: `getMeshForRace`
step 5, "Fallback: colored placeholder cubes") — this call site never
lets it be reached.

## Scope

### 1. Make a missing base model skip the ENTITY, not the ZONE

Change 8a so an unloadable model marks that entity as failed and lets the
zone continue, and have the caller loop skip the remaining per-entity
steps for it (8c/8d/8e do not currently guard on `entitySuccess`, so
simply returning `true` is NOT sufficient — check this yourself).

Downgrade `LOG_FATAL` to `LOG_WARN`. Keep it identifiable: the message
must still name the race, gender, and the entity, because this is how a
model-coverage gap gets noticed at all.

Reserve `loadFailed_` for failures that genuinely make the zone unusable.
If you find a case in 8a that really is zone-fatal, say which and why.

### 2. Make the skips visible, not silent

**This is not optional decoration.** Three separate checks in this repo
have recently been found capable of quietly not running (T36's golden
replay, T37's file-size script, the original golden skip) — the lesson
each time was that a silent non-event manufactures false confidence.
Do not replace a loud crash with a silent omission.

Count skipped entities and emit one summary line at the end of the zone
load (count + the distinct race/gender pairs). A zone that renders with
missing NPCs must say so plainly.

### 3. The equipment twin (`8d`) — fix if identical, report otherwise

`loadZoneStep8d_EquipmentPrep()` is called as
`if (!loadZoneStep8d_EquipmentPrep(ctx)) return false;` — the same
abort-the-zone shape, and deferred.md already records the same complaint
about a missing `item_models.json` entry. If it is the same defect with
the same fix, fix it here; leaving a known twin means the next missing
item model aborts the zone anyway. If it turns out to be genuinely
zone-fatal, say so and leave it.

### 4. Report on race 415's data gap — do NOT fix it here

For the record, established by the orchestrator: `race_models` has 185
rows with a maximum `race_id` of 367, so **415 is simply absent from the
mapping**, while `race_data` does carry an entry for it (model ids
1542-1547 / 3001-3002). That is a data-coverage question — related to the
recorded "race_codes.cpp mapping gaps" item from P08f — and it is
explicitly **out of scope**. Adding races to the table is a separate
decision; this unit makes the client survive their absence.

## STOP conditions

- Skipping the entity leaves the zone in a visibly broken state beyond a
  missing NPC (holes in geometry, crash later, entity list desync) → STOP
  and report; that means the skip needs more unwinding than a guard.
- The fix requires changing `RaceModelLoader`'s lookup or the race tables
  → STOP; that is scope item 4, deliberately excluded.
- 8d turns out to be genuinely zone-fatal → do not force it; report.
- Any behaviour change beyond missing-asset handling → STOP.

## Verification bar

1. `cmake --build <slot>/build -j24` clean.
2. `ctest --test-dir <slot>/build --output-on-failure` — known exception:
   `AudioManagerTest.PreloadCommonSounds` (ALSA). **The RDP family is no
   longer waived (T35) — if it fails, that is a real regression.**
3. Golden replay unchanged and **confirmed to have RUN** — since T36 it
   resolves against the repo root, so it should no longer skip; if it
   reports Skipped, something is wrong, investigate rather than accept.
4. **The acceptance check — the thing that has been blocked**: a
   graphical live session that previously died on `race=415` must now
   load the zone and stay up. Reuse T32's committed repro
   (`benchmarks/audits/T32/run_race415_graphics.sh`). Show: the WARN
   naming race 415, the skip-summary line, the zone loading to completion,
   and the client remaining usable afterwards (drive a couple of piped
   slash commands — T29's channel works and T31 makes the replies
   visible). eqemu LOCK FIRST; `up.sh` only; restore Testchar to
   zone_id=36, x=-77.9232, y=21.9086, z=3, h=190; `down.sh` at the end.
5. A regression test for the skip path if one can be written without a
   live server; if it cannot, say so plainly rather than inventing a
   fixture that does not exercise the real path.
6. `bash <slot>/scripts/refactor/check_file_sizes.sh --files <abs paths>`
   — the bare `... fable` form now says "nothing was checked" when the
   diff is empty (T37); that line is a failed verification, not a pass.
7. Evidence under `benchmarks/audits/T38/`.

## Review

### The skip mechanism and why the caller loop needed changing

`loadZoneStep8a_BaseModelPrep()` (`src/client/graphics/renderer_zone_load.cpp`)
no longer treats an unloadable base model as zone-fatal. On
`preloadModelData()` failure it now: downgrades `LOG_FATAL` to `LOG_WARN`
(still names the entity id/name, race, and gender), increments
`ctx.entitiesSkipped`, records `{raceId, gender}` in
`ctx.skippedRaceGenderPairs`, sets `ctx.entitySuccess = false`, and returns
`true` (no `loadFailed_`, no `pendingZoneComputations_.reset()`). Found no
other path inside 8a that is genuinely zone-fatal — `ctx.modelLoader` is
guaranteed non-null by the caller's precondition
(`entityRenderer_ && entityRenderer_->getRaceModelLoader()`), so 8a now
always returns `true` in practice; the `bool` return type is kept for
signature symmetry with the other guarded steps.

Verified (by reading, then confirmed live) that simply making 8a return
`true` would NOT have been sufficient: 8c (`loadZoneStep8c_VariantTextures`)
and 8d (`loadZoneStep8d_EquipmentPrep`) do not check `ctx.entitySuccess` at
all, and 8e (`loadZoneStep8e_DistributePrepResults`) distributes whatever
8b-8d staged. So the caller loop (`loadZoneStep8_Entities`) now wraps
8b/8c/8d/8e in `if (ctx.entitySuccess) { ... }` — those four steps all
depend on base model data that a skipped entity does not have. 8f
(`loadZoneStep8f_BuildEntityMesh`) and the `promotePreparedModels()`/
`uploadAllPending()` calls around it deliberately run unconditionally for
every entity, skipped or not: `EntityRenderer::buildEntityMesh()` doesn't
require `entityPrepComplete` (that's only consulted by the separate async
progressive-build state machine at `EntityBuildPhase::Placeholder`,
`entity_renderer.cpp:1043`), and its own `getMeshForRace()` already falls
back to a cached colored placeholder cube
(`entity_renderer.cpp:333-346`) when `raceModelLoader_->getMeshForRace()`
returns null for an uncached race — this is the designed fallback the plan
named (`src/client/graphics/CLAUDE.md`, "Model Loading Order" step 5) that
the old FATAL never let get reached. `buildEntityMesh()` also
unconditionally sets `visual.buildPhase = EntityBuildPhase::Built`
(`entity_renderer.cpp:861`) regardless of whether it built a real model or
a placeholder, which short-circuits the async rebuild-candidate scan
(`entity_renderer.cpp:955`, `if (vis.buildPhase == EntityBuildPhase::Built)
continue;`) before it would ever consult `entityPrepComplete` — so leaving
that flag `false` for skipped entities (since 8e never ran) is harmless,
not a desync.

### Visibility summary

Unconditional (not gated on `entitiesSkipped > 0`) log line at the end of
`loadZoneStep8_Entities`, once per zone load:
- `entitiesSkipped == 0`: `LOG_INFO` "0 entities skipped for missing base
  models — all N entities had loadable models" (so the log can be trusted
  to say "checked, found none" rather than staying silent the way T36's
  golden replay / T37's file-size script / the original golden skip did).
- `entitiesSkipped > 0`: `LOG_WARN` "SKIPPED N entities with no loadable
  base model (M distinct race/gender pair(s): race=R gender=G, ...) —
  rendered as placeholder cubes, zone load continued".

Live-confirmed exact line (befallen, race=415):
```
[WARN ] [GRAPHICS] Sequential: SKIPPED 2 entities with no loadable base model (1 distinct race/gender pair(s): race=415 gender=2) — rendered as placeholder cubes, zone load continued
```

### 8d verdict: already fixed, not a live twin

`loadZoneStep8d_EquipmentPrep()` still has the `if (!...) return false;`
caller shape and an internal `equipError`/`if (equipError) { ...
loadFailed_ = true; return false; }` block — but `equipError` is declared
`false` and is **never set to `true` anywhere in the function**. Both of
8d's actual failure points (`!modelRef` — item not in `item_models.json`;
`!equipData2` — extraction failure) already `LOG_WARN(...) + return;`
(skip just that one equipment slot, entity renders unequipped for it) per
a prior fix already in the code (the P10b comment at
`renderer_zone_load.cpp:1548-1561` documents this exact change, made for
the same reason this unit exists). So 8d's abort-the-zone path is dead
code today, not a live twin of 8a's old defect — there is currently no way
to reach it. Left as-is (removing genuinely-dead code was not asked for
and risked being a no-op change outside this unit's stated territory); not
"fixed" because there was nothing live to fix.

### Live acceptance check

eqemu lock: created `T38-missing-model-nonfatal 2026-08-04` (confirmed
absent first), removed after `down.sh` completed (confirmed absent after).
`up.sh` only. Testchar was already at canonical `zone_id=36,
x=-77.9232, y=21.9086, z=3, heading=190` before the run; after the run it
had drifted to `x=-77.875, y=21.875` (float rounding from
`/camp`-triggered position sync, same as T32 observed) and was explicitly
restored to the exact canonical values. No stray `willeq` processes
(`pgrep -a willeq` empty) before `down.sh`.

Reused T32's repro shape (`benchmarks/audits/T32/run_race415_graphics.sh`)
as the base for `benchmarks/audits/T38/run_race415_acceptance.sh`: zone
into befallen (Testchar's canonical zone, `zone_id=36`), spawn npc 351043
(`a_rat`, race=415 gender=2 — the exact asset T29/T32 found), then (new
vs. T32) additionally drive `/who` and `/say` afterward and camp cleanly,
instead of the client dying. Full transcript:
`benchmarks/audits/T38/transcript_race415_acceptance.txt` (+`.driver.txt`).

What it shows, in order:
1. `Zone connection complete! Player position confirmed` — zones in.
2. `#npctypespawn 351043` spawns the race=415 npc while the graphics
   loading thread's Step 8 entity loop is still mid-flight (same timing
   T32 found — the loading thread takes longer than the network handshake
   in befallen, so the freshly-spawned entity lands inside the same
   sequential Step 8 loop, exercising the exact code path this unit
   changed): `SEQ Step8a: entity 134 (a_rat000) race=415 gender=2 ...
   cached=false`.
3. The WARN (not FATAL) naming the race: `Skipping entity 134 (a_rat000):
   base race model not found: race=415 gender=2 (not found in any S3D
   archive) — will render as a placeholder cube, zone load continues`. A
   second instance of the same npc type (`a_rat001`, entity 136,
   auto-spawned as part of the same command) hits the identical WARN path.
4. The skip-summary line (quoted above): `SKIPPED 2 entities ... race=415
   gender=2 ... zone load continued`.
5. `SEQ: Step 8 complete, entering Step 9...` — the loader proceeds past
   the point that used to be fatal.
6. `loadZoneGraphicsOnThread: complete for zone 'befallen'` — the full
   zone load (not just Step 8) finishes. No `FATAL` anywhere in the
   transcript (`grep -c FATAL` → 0).
7. Client stays usable: `/who` → `CMD outcome=applied ...` and a reply
   (`[1] zone controller (Lvl 200)`); `/say hello after race415 skip` →
   echoed back as `[CHAT] Testchar (say): hello after race415 skip`. The
   performance report shows 2068 rendered frames (avg 16.1ms, ~62 FPS)
   after the skip, i.e. the render loop kept running, not just the
   process.
8. `/camp` → `You have begun to camp` → `You have camped` →
   `Application shutdown complete`, exit code 0 — clean voluntary
   shutdown, not a crash/quit.

This is the acceptance check the unit exists to unblock: a graphical live
session that previously died on `race=415` now loads the zone to
completion and stays usable afterward.

### Tests

1. `cmake --build build -j24` — clean.
2. `ctest --test-dir build --output-on-failure` — 1818/1819 passed; the
   sole failure is the documented `AudioManagerTest.PreloadCommonSounds`
   ALSA host exception. RDP family (23 tests, `RDPServer*`) — all passed,
   no regression. `SessionReplayGolden.MatchesRecordedSessionOrCreatesBaseline`
   confirmed to have RUN (not skipped) and passed.
3. `bash scripts/build-gles2-x86.sh` — clean.
4. `bash scripts/ci/visual_gate.sh` — `=== VISUAL GATE: PASS ===`, goldens
   untouched (qeynos2/gfaydark/befallen all verified).
5. `bash scripts/build-arm-noble.sh` — clean, ARM binary produced.
6. `scripts/refactor/check_file_sizes.sh --files
   src/client/graphics/renderer_zone_load.cpp` — pre-existing WARN (2951
   lines, file was already over the 1500-line threshold before this
   unit), no new FAIL.
7. Regression test for the skip path: **not written, and I'm saying so
   plainly rather than inventing one that doesn't exercise the real
   path.** The skip mechanism lives entirely inside `IrrlichtRenderer`'s
   private step methods and the `ZoneLoadContext` struct, which is defined
   inside `renderer_zone_load.cpp` itself (not the header) specifically as
   an implementation detail — there is no seam to drive
   `loadZoneStep8a_BaseModelPrep`/`loadZoneStep8_Entities` without a fully
   constructed `EntityRenderer` + `RaceModelLoader` + a real per-entity
   loop, which in turn needs a real (or realistically faked) zone entity
   list. The project's own precedent for this class of test
   (`test_zoning_graphics_integration.cpp`, documented in
   `src/client/graphics/CLAUDE.md`) already requires a running EQEmu
   server + `DISPLAY`, and is explicitly NOT auto-discovered by `ctest`
   for that reason. A unit test against `RaceModelLoader::preloadModelData`
   alone would only re-confirm the already-established, out-of-scope data
   gap (race=415 absent from `race_models`), not the caller-loop skip
   logic this unit actually changed — so it would not be evidence for this
   unit's fix and I did not write it. The live acceptance transcript above
   is the regression evidence for this path.
8. Evidence committed under `benchmarks/audits/T38/`: driver script
   (`run_race415_acceptance.sh`), config (`willeq_t38.json`), full
   transcript + driver log.

### Deviations from the plan

- None substantive. The plan anticipated needing to "check yourself"
  whether 8b-8e needed guarding (confirmed: 8c/8d/8e did, 8b already did)
  and whether 8d was a live twin (confirmed: no, already fixed by a prior
  unit — reported per the plan's own "if it turns out to be genuinely
  zone-fatal, say so and leave it" clause, adapted to "already non-fatal,
  say so and leave it").
- The acceptance driver spawns two `a_rat` instances (entities 134 and
  136) rather than exactly one — `#npctypespawn 351043` appears to spawn
  more than one copy in this server/data configuration. Left as-is: two
  independent hits of the same race/gender pair is if anything a stronger
  demonstration of the skip-and-continue path (and the summary line
  correctly reports `2 entities` / `1 distinct race/gender pair`).
