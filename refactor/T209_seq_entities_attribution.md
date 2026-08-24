# T209 — where seq_entities goes: no redundant work, irreducible first-time model cost

Evidence: `review/T209/`. Instrumentation is permanent (two INFO lines at the end
of Step 8), same discipline as T207's archive-usage report.

## Why look
T208 established `seq_entities` is 62-70% of zone-in on both platforms (x86
2793ms, Pi 24842ms) and that nothing in the EQE1 arc touches it. The question,
per Will's standing bar, is correctness first: **is Step 8 building things
nothing needs, or redoing work it has already done?**

The setup invites that suspicion. Befallen zone-in prepares 226 entities drawn
from only **14 distinct race/gender pairs** — 211 of 225 entities (94%) are a
repeat of a pair an earlier entity already prepared.

## Answer: no. The caching is correct.

x86, cold befallen, orangepi preset:

    T209 Step8 split (ms): 8a_base=930.0 8b_variant=3.1 8c_vtex=23.6
      8d_equip=412.7 8e_distribute=0.2 8f_mesh=1213.6 promote=0.1 upload=77.0
      | 211 of 225 entities were a repeat race/gender (14 distinct pairs)

    T209 Step8 REPEAT-ONLY (ms): 8a=0.2 8b=0.0 8c=22.6 8d=382.8 8e=0.2
      8f=132.4 | 538.3 of 2583.2ms (20%) spent on entities whose race/gender
      was already fully prepared

| sub-step | total | on repeat entities | reading |
|---|---|---|---|
| `8a_base`  |  930.0ms | **0.2ms** | base model prep is cached essentially perfectly |
| `8f_mesh`  | 1213.6ms |  132.4ms | 0.63ms per repeat — a scene node per entity, which each entity genuinely needs |
| `8d_equip` |  412.7ms |  382.8ms | 93% on repeats, but equipment varies per entity — this is not repeat work, it is per-entity work |
| everything else | ~26ms | ~23ms | noise |

**Only 20% of Step 8 is spent on repeat race/gender entities, and the bulk of
that is equipment prep, which is legitimately per-entity.** `8a_base` — the
sub-step that would show duplicated model loading — costs 0.2ms across 211
repeat entities. The mesh cache (`variantMeshCache_`, keyed by
race/gender/head/body variant) and the model cache are both doing their job:
178 model cache hits against 12 disk loads.

So the hypothesis this unit set out to test is refuted. There is no
archives-style waste hiding in Step 8.

## What the time actually is

Strip the repeat portion and ~2045ms of the 2583ms is first-time construction
for 14 distinct race/gender pairs: ~66ms each in `8a_base` plus ~77ms each in
`8f_mesh`, roughly 143ms per distinct model on x86. Scaled by the Pi's measured
~8.9x CPU factor that is ~1.3s per model, and 14 models ≈ 18s — which accounts
for the bulk of the Pi's 24.8s.

**The Pi's zone-in is dominated by first-time construction of 14 distinct
character models.** Not i/o, not redundancy. Reducing it means making model
construction cheaper or making fewer distinct models necessary, not caching
harder — the caching is already right.

Note the T109 gate does not help here: 14 distinct pairs in-radius and 14
distinct deferred are the *same* 14, so deferring 140 entities saves zero model
loads. Its saving is per-entity loop cost only, which the table above shows is
the small half.

## NOT VERIFIED — deferred-entity pickup
140 entities are deferred by the T109 gate. T111 (`d21086be`) claims to have
fixed the pickup path that T109 shipped inert, and `refactor/deferred.md`'s T109
entry is stale on this point. **This unit did not confirm it.** The run holds
position for 55s and never moves, so no PVS region change occurs and no pickup
*should* fire — `EntityPrepWorker` starts and stops with no `queued prep` lines,
which is consistent with both a working and a broken pickup path. Confirming it
needs the movement-based reproduction T109 used: cross regions toward deferred
entities and assert the modeled count rises. Until then, "140 entities deferred"
is an unverified promise, and it is the one open correctness question in Step 8.
