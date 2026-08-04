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

(Executor fills: the skip mechanism and why the caller loop needed
changing, the visibility summary, the 8d verdict, the live acceptance
transcript showing the zone loading with race 415 skipped, tests,
deviations.)
