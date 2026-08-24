# T190 — Correction: the entity drain was never broken

**T188 and T189 reached a wrong conclusion, and this records the correction
before it hardens into project memory.**

## What was claimed

- T188: "deferred entities are never built … on the Pi, befallen entities
  beyond 400 units are permanently missing — a correctness bug in a shipped
  configuration", and recommended making `entityLoadRadius` inert.
- T189: root-caused it to `inSceneGraph` only being set inside
  `applySimulationOutput`'s `if (visual.sceneNode)` branch, and proposed a
  `wantsVisible` field to break the loop.

## What was actually happening

**The test character's saved position was outside the zone geometry, and I put
it there.** eqemu sends the last client-provided position back on login; the
`/move` teleports used in T186–T189 wrote out-of-bounds positions, which the
server saved. Every run afterwards started with the player outside all BSP
regions:

```
SimWorker apply: PVS=18446744073709551615     (SIZE_MAX)
```

`queueEntityPrepRequests()` bails with `if (!pvsValid && spawnId !=
playerSpawnId_) continue;`, so no non-player entity can be queued while that
holds. Nothing was wrong with the drain; the observations were taken from an
invalid vantage point.

## The measurement that settles it

Re-zone with `#zone` so the server supplies proper zone-in coordinates, then
run the identical script:

| | valid PVS frames | entity builds after Step 8 |
|---|---|---|
| **stock code** | 281 | **213** |
| T189 "fix" applied | 157 | **7** |

**Stock code drains fine.** 281 entities deferred at Step 8, 213 built during
the run, pending count moving throughout — against **zero** builds in every
earlier run, all of which had `PVS=SIZE_MAX`.

The T189 change is therefore not merely unnecessary but appears actively
harmful, plausibly by widening the queue/blocking predicates to
`inSceneGraph || wantsVisible`, flooding the prep queue and starving
`processOneEntityBuildStep()` of its per-frame budget. **`task/T189-entity-drain`
is kept unmerged** with its counter-evidence.

## What stands and what falls

**Falls:**
- "Deferred entities are never built" (T188) — false, an artifact of position.
- "Befallen entities beyond 400 units are permanently missing on the Pi" — not
  established.
- The recommendation to neutralise `entityLoadRadius` — withdrawn. It was wrong
  twice over: disabling a feature to dodge a bug, and the bug wasn't real.
- T189's `wantsVisible` diagnosis.

**Stands:**
- T186: the Pi zone-in time has not moved (~37.7s vs 37.3s baseline).
- T187: `seq_entities` is 64% of the Pi's cold zone-in, and the per-step
  `Progressive: seq_` instrumentation that showed it.
- T188's configuration finding: `orangepi` sets `entityLoadRadius = 400`
  (active) while the struct default is inert — so T109's "never returns true in
  any shipped configuration" comment is wrong about the preset.
- T188's measurement that the gate saves ~45ms on x86, so it is not the load-time
  lever — though that was x86-only and T109's Pi −12% may still hold.

## The process lesson

**Do not drive test characters with `/move` teleports.** The position is saved
server-side and silently poisons every later session in that zone. Use `#zone`
to re-zone (the server then supplies valid coordinates), or update
`character_data` directly — and only after the character has FULLY
disconnected, or the live session's save overwrites it.

Three separate units drew conclusions from a corrupted vantage point before
this was caught. The tell was there the whole time and was read as a symptom
rather than a cause: `PVS=18446744073709551615` from the very first frame.
