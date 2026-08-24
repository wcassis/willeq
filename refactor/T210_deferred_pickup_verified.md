# T210 — deferred-entity pickup: VERIFIED WORKING

Evidence: `review/T210/`. Closes the one open correctness question T209 left.

## The question
T209 measured that the T109 load-distance gate defers 140 of 367 entities at a
befallen zone-in. T111 (`d21086be`) claims to have fixed the pickup path that
T109 had shipped inert, but `refactor/deferred.md`'s T109 entry still described
the gate as broken and warned that activating it "would produce permanently-
invisible NPCs". The `orangepi` preset ships `entityLoadRadius: 400.0` — the
gate IS active — so if the pickup were still broken, 140 NPCs in befallen would
never appear. T209 could not settle it: that run holds position for 55s, so no
PVS region change fires and the absence of pickup logs proved nothing.

## Method
Deliberately NOT `/move` — that teleport is what produced the wrong T188/T189
conclusion by saving out-of-zone positions. `/moveto <entity>` drives the
client's own pathfinding movement, so the server sees ordinary position updates.

Discovery pass at `--log-level=DEBUG` listed all 140 deferrals with names and
distances; the nearest was `a_greater_skeleton026` at 404.9 (just past the 400
radius). Repro script:

    wait 40 / dumpscene / moveto a_greater_skeleton026 / wait 100 / dumpscene

`/dumpscene` reports per-entity `inGraph=` and `placeholder=`, so the transition
is observable directly rather than inferred from a count.

## Result: the pickup works

The target entity, across the three scene dumps (zone-load end, pre-move,
post-move):

    id=228 'a_greater_skeleton026' pos=(-435.0,-150.0,-68.4) bspRegion=-1  inGraph=false placeholder=true
    id=228 'a_greater_skeleton026' pos=(-435.0,-150.0,-68.4) bspRegion=-1  inGraph=false placeholder=true
    id=228 'a_greater_skeleton026' pos=(-433.3,-149.1,-21.4) bspRegion=4004 inGraph=true  placeholder=false

Zone-wide, across the same dumps:

    | dump | placeholder cubes | real models | in-graph |
    |------|-------------------|-------------|----------|
    | zone-load end |    176 |    191 |  2 |
    | pre-move      |    176 |    191 |  3 |
    | post-move     |    168 |    199 | 15 |

Eight deferred entities were prepped and built during the walk, with the full
`EntityBuild: Placeholder -> NodeSetup -> MeshFinalize -> Built` transition
sequence firing well after Step 8 completed. Player ended at (-430.8, -147.8,
-67.6), having crossed into bspRegion 4004.

Only 8 of 140 were picked up because the player walked to one location; the
other 132 remain far away. That is the gate working as designed, not a shortfall.

## Consequence
`refactor/deferred.md`'s T109 entry is stale and has been annotated CLOSED with a
pointer here. The "permanently-invisible NPCs" risk did not survive T111.

Step 8 therefore has no known correctness defect: T209 showed it does no
redundant work, and T210 shows the entities it defers are genuinely picked up.
What remains on the Pi is cost, not incorrectness — first-time construction of 14
distinct character models, per T209.
