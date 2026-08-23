# T188 — "Tighten the gate" is the wrong lever, and the gate is losing entities

Asked to tighten T109's entity load-distance gate. Investigating it produced a
correction to T187, a measurement showing the gate cannot deliver what was
hoped, and a **user-visible defect in the shipped Pi configuration**. No code
change was merged; the experiments below were reverted.

## Correction 1 — the configuration is not what T187 assumed

| | `entityLoadRadius` | `entityPrepMaxPvsDepth` |
|---|---|---|
| struct default (`constrained_renderer_config.h`) | `1'000'000` — **inert** | `0` (same region only) |
| `orangepi` preset | **`400` — ACTIVE** | `255` (no limit) |
| `128x32x10` preset | (unset → inert) | `255` |

T109's source comment says the gate "ships at an inert default and this
function currently never returns true in any shipped configuration". **That is
true of the struct default and false of the `orangepi` preset**, which sets 400
and has done since the preset was written.

**So the Pi has been gating all along.** T186/T187 ran the Pi with
`--constrained orangepi` (gate ACTIVE) and x86 with no preset (gate INERT), so
**T187's Pi-vs-x86 `seq_entities` comparison was not like-for-like.** T187's
"the gate is active but only cut half" was right by accident; its follow-up
reasoning was not.

## Correction 2 — the gate cannot deliver the saving

x86, same zone and spot, with and without `--constrained orangepi`:

| | entities deferred | `seq_entities` | distinct (race,gender) loaded |
|---|---|---|---|
| no preset (gate inert) | 0 | **5413ms** | 12 |
| `orangepi` (gate at 400) | **116** | **5368ms** | 13 |

**Deferring 116 entities saved ~45ms — nothing.** The cost is not per-entity;
it is the distinct **race/gender model loads**, and those barely move because
**as long as ONE entity of a race is inside the radius, that model loads.**
T105's 6-9 "needed" pairs are not reachable by shrinking a radius — 12-13 pairs
have at least one representative nearby.

**Therefore: tightening the radius is not the lever.** The lever on
`seq_entities` (64% of the Pi's zone-in, T187) is making race-model loading
cheaper or rarer per *model*, not deferring more *entities*. The composite-atlas
fallback T187 found — 6 multi-buffer fallbacks per zone-in, identical on both
platforms — sits squarely in that path and is the better candidate.

## The defect — deferred entities are never built

Live on x86 under the shipped `orangepi` preset, walking across regions:

```
Progressive status: entities=2 ... progressive=true
Progressive status: entities=6 ... progressive=true      (after /move)
Progressive: entity_*                                     ZERO occurrences
```

Entities become pending as the player moves into range, progressive loading is
active — and **not one entity build step ever executes**. The 116 deferred
entities are never built.

T109 documented this failure at `entityPrepMaxPvsDepth = 0` and concluded the
completion check gives up outside the landing region. **It happens at 255 too**,
so the depth is not the cause and the re-arm-on-region-change fix I wrote for it
(reverted) does not address it. `entityPrepReady_` is set at the end of Step 8,
`skipEntityBuild` is unset in the preset — the stall is inside
`queueEntityPrepRequests()` / `processOneEntityBuildStep()` and was not chased
further.

**Consequence: on the Pi, befallen entities beyond 400 units are permanently
missing.** That is a correctness bug in a shipped configuration, not a
performance matter, and it is worth more than the ~45ms the gate saves. The
cheapest immediate mitigation is to set `entityLoadRadius` inert in the
`orangepi` preset — giving up nothing measurable and restoring the entities —
until the drain is fixed.

## What was reverted

An unconditional re-arm of `progressiveLoadingActive_` on PVS region change
(`renderer_sim_bridge.cpp`), and an experimental `entityLoadRadius = 300`
struct default. The re-arm targets the depth-0 completion behaviour, which no
shipped preset uses; shipping it would have been a fix for a configuration that
does not occur. Both reverted; `fable` is unchanged by this unit.

## Recommended next

1. **Fix or neutralise the drain.** Either chase why
   `processOneEntityBuildStep()` never fires for deferred entities, or set the
   `orangepi` preset's `entityLoadRadius` inert. The measurement above says the
   second costs nothing.
2. **Then attack race-model loading**, starting with the composite-atlas
   fallback (T187). That is where `seq_entities` actually spends its time.
