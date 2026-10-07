# The eqemu world database (peq) — join keys and the zone-id pitfall

Reference for anyone querying the eqemu `peq` database via
`scripts/eqemu/env.sh` (`mysql_exec`). Written after two separate incidents
(2026-10-07 floor-sweep route census) where a query silently returned the wrong
zone's data and the empty/garbage result was reported as a finding about the
world content. **The data was never wrong; the join was.**

## The trap: `zone` has TWO id columns

| column | meaning | gfaydark | qeynos2 | growthplane |
| --- | --- | --- | --- | --- |
| `id` | auto-increment PK | 138 | 303 | 143 |
| `zoneidnumber` | the classic EQ zone number | **54** | **2** | **127** |

**`grid.zoneid` and `grid_entries.zoneid` reference `zone.zoneidnumber`, NOT
`zone.id`.** The server's own loader confirms the key:
`GridEntriesRepository::GetZoneGridEntries` runs
`... WHERE zoneid = <zone_id> ORDER BY gridid, number` where the `zone_id`
passed in is the zone number (`code/common/repositories/grid_entries_repository.h`).

Filtering `grid.zoneid = <zone.id>` (138/303/143) queries whatever unrelated
zones happen to own those *numbers* — usually nothing, which reads as "this
zone has no roam paths" when the truth is hundreds of routes.

Rules that make the error impossible:

1. Resolve the number once, up front, and never mix the columns:
   `SELECT zoneidnumber FROM zone WHERE short_name = 'gfaydark'` → 54.
2. Verify any route/geometry result against the zone's own spawn box
   (`SELECT MIN(x),MAX(x),MIN(y),MAX(y) FROM spawn2 WHERE zone='gfaydark'`);
   waypoint coordinates that don't fall inside it mean the wrong zone.
3. An empty result that contradicts known ground truth (guards patrol
   gfaydark — that is observable in any login) is a **query bug until proven
   otherwise**. Re-check the join key before reporting; never confirm an empty
   result with a more elaborate query.
4. One-table counts (`SELECT COUNT(*) FROM grid WHERE zoneid = <number>`)
   come before multi-table joins.

## Roam/patrol route tables

| table | columns |
| --- | --- |
| `grid` | `id`, `zoneid` (= zoneidnumber), `type` (wander type), `type2` (pause type) |
| `grid_entries` | `gridid`, `zoneid`, `number`, `x`, `y`, `z`, `heading`, `pause`, `centerpoint` |
| `spawn2` | `... pathgrid ...` — references `grid.id`, **scoped to the spawn's own zone** (`spawn2.zone` short name ↔ `grid.zoneid`) |

Wander `grid.type` values (docs.eqemu.dev, server/npc/spawns/grids):
0 = circular, 1 = random-10, 2 = random, 3 = patrol (walk to end, backtrack),
4 = one-way repop, 5 = random-5-LoS, 6 = one-way depop, 7 = center point,
8 = random center point, 9 = random path.

`grid.id` is **NOT globally unique**: 31,556 grid rows but only 12,563
distinct ids (measured 2026-10-07) — each id belongs to ~2.5 zones. A bare
`JOIN grid g ON g.id = s.pathgrid` without the `g.zoneid = <number>` condition
is a cross-zone product and returns garbage (e.g. one "route" resolving to
butcher, cabeast, 280+ other zones).

## The correct census query (per zone)

```sql
-- 1. resolve the zone number
SELECT zoneidnumber FROM zone WHERE short_name = 'gfaydark';       -- 54

-- 2. routes this zone's spawns actually reference (the sweep's point set)
SELECT COUNT(DISTINCT s.pathgrid)
FROM spawn2 s
JOIN grid g ON g.id = s.pathgrid AND g.zoneid = 54
WHERE s.zone = 'gfaydark' AND s.pathgrid > 0;

-- 3. route geometry (waypoints in order; `number` is 1-based)
SELECT number, x, y, z, heading, pause
FROM grid_entries WHERE gridid = 81 AND zoneid = 54
ORDER BY number;
```

Measured route census (2026-10-07, live peq DB): qeynos2 = 102 roam grids
(100 circular), gfaydark = 239 (238 circular, 1 patrol), growthplane = 235
(233 patrol). Every non-tutorial zone has roaming NPCs.

## Lane

The stack lives on the eqemu VM; `scripts/eqemu/{up,down}.sh` manage it and
`mysql_exec` (env.sh) is the query path. The lane is managed by the
orchestrator as needed for card work — it is not a will-gated action.
