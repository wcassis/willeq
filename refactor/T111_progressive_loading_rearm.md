# T111 — Deferred entities are never picked up; fix that, then enable the gate

**Slot: slot1. OWNS EQEMU AND THE PI. T110 is scoped and waiting on the Pi —
finish and release it.**

**Territory: `src/client/graphics/renderer_zone_stream.cpp`,
`renderer_entities.cpp`, `renderer_player_physics.cpp`,
`renderer_entity_load_gate.cpp`, `include/client/graphics/`,
`config/constrained_presets.json`, `tests/`, `refactor/`,
`/mnt/projects/fable/review/T111/`.**

Read T109's Review in full first — it found this, built the gate, and shipped
it inert because of it.

## The defect, located

T109 confirmed live that **entities deferred by the load gate are never picked
up**: modeled-entity count frozen at **82 of 178** across a 40-second idle hold
and after two large `/move`s (−400 and −700 units), with zero background-build
activity logged after Step 8.

Three sites set `progressiveLoadingActive_ = true`:

- `irrlicht_renderer.cpp:497` — zone load
- `renderer_player_physics.cpp:1033` — collision setup, also zone-load time
- `renderer_entities.cpp:71-72` — a **brand-new entity spawn**

**None of them fires on player movement or BSP region change.** And
`checkProgressiveLoadingComplete()` treats only the player's *current* region
as blocking, so once the initial region is satisfied, progressive loading
declares itself finished and never restarts.

So the streaming path works exactly once, for entities that were already
candidates at zone-in, and for later spawns. Walking toward something that was
deferred does nothing.

## What to fix — determine which, do not guess

Two candidate shapes, and they are not equivalent:

1. **Re-arm on region change / movement** — `progressiveLoadingActive_` becomes
   true again when the player's BSP region changes, or when movement brings
   new candidates into range.
2. **Fix the completion test** — `checkProgressiveLoadingComplete()` stops
   treating only the current region as blocking, so it does not declare
   completion while deferred entities remain anywhere relevant.

**Read both paths and decide which is correct**, or whether both are needed.
State the reasoning. A re-arm that fires constantly would defeat the throttle
that exists to protect frame time; a completion test that never completes would
be worse.

Note `renderer_entities.cpp:71` already re-arms on new spawns **only if**
`entityPrepReady_` — understand that guard before copying the pattern.

## Prove the pickup works — this gates everything else

**This is the hard part and the whole point of the unit.** T109 could not
enable the gate because deferred entities vanished permanently.

Required evidence, live, on a **populated** zone:

- Defer a known set of entities, then **walk toward them** and confirm each
  becomes modeled. Report counts before and after, not impressions.
- Confirm the count converges to the full population, not to a plateau —
  82 of 178 stuck at 82 is exactly the failure being fixed.
- Confirm this survives **multiple region changes**, not just the first.
- Confirm it works when the player moves **away and back**.

Nektulos (451 entities) is where a gating bug shows; Befallen (178) is what
T109 measured and is directly comparable.

## Then, and only then, enable the gate

If — and only if — the pickup proof is unambiguous, set `entityLoadRadius` in
the shipped presets to the value T109 justified: `entityRenderDistance + 100`
(400 on the orangepi/default preset). T109 ships it defaulted to
`1'000'000.0f`, i.e. inert.

**STOP and leave the gate inert if the pickup proof is anything less than
unambiguous.** A gate that loses NPCs in a rare case is worse than no gate,
and this project has now spent two units establishing that.

## Re-measure both machines after enabling

T109's characterisation, radius temporarily active, Befallen:

| | x86 | Pi |
|---|---|---|
| zone-in | 11.69s → 10.74s (−8.1%) | 35.62s → 33.27s (−6.6%) |
| Step 8 | 5.72s → 4.57s (−20.1%) | 22.00s → 19.44s (−11.6%) |

Confirm these hold with the pickup path working — they may move, since the
deferred work now actually happens afterwards. **Report the post-zone-in cost
too**: the gate trades load time for background work, and if that work causes
frame hitches during play, that is a finding.

## Also worth knowing

Step 8 is ~62% of the Pi's zone-in (22.0s of 35.6s). Even a working gate leaves
**~13.6s of non-Step-8 zone-in time** uncharacterised. Do not chase it here —
but note anything you observe about where it goes.

## What you may not do

- Do not change the 40-entity render cap.
- Do not touch `ConstrainedTextureCache` eviction.
- Do not enable the gate on a partial pickup proof.
- **STOP and report** if the fix requires restructuring the progressive-loading
  throttle or the frame-budget governor. Those protect frame time and are their
  own unit.

## Verification

Full build and suite (current total 2257; sole pre-existing failure the
documented ALSA `AudioManagerTest.PreloadCommonSounds`) plus ARM cross-compile.
Every new test **proven able to fail in both directions**.

Visual gate — goldens may legitimately move if the gate is enabled. Say so,
explain why, and show before/after rather than regenerating silently.

Session discipline: binary freshness against HEAD on **both** machines;
daylight `/say #set time 11 0`; pad scripts long — longer for the Pi; `-d 4`
plus `--log-level=DEBUG`; Pi `/tmp` is a 242MB tmpfs so pull captures off as
you go; restore DB state and confirm by `SELECT` **after logout**; kill clients
by real binary PID on both hosts; clean teardown, delete the lock. **Release
the Pi promptly — T110 is waiting on it.**

## Review

(executor fills: which fix shape and why; the live pickup proof with counts
converging to full population across multiple region changes and
away-and-back movement; whether the gate was enabled or left inert and why;
re-measured load times both machines; post-zone-in frame cost of the deferred
work; whether goldens moved; deviations)

## Board

If the gate goes live, record it — the distance gate has been the target of
T91, T105, T109 and this unit.
