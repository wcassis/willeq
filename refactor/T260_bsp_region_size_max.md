# T260 — "Player BSP region is SIZE_MAX" at zone-in (stale-zone-name race, T242 attribution)

Slot: slot2. x86. OWNS eqemu briefly (noclobber) for a zone-in repro if
needed; the benchmark/zone tests may suffice. Class A/B. Territory:
`EverQuest::CreateZoneLoadSnapshot` and the zone-load handoff it feeds
(eq_zone.cpp / eq.cpp snapshot only), renderer_zone_load* region lookup
for the player's start position, tests. Do NOT touch inventory (T259
slot3), willeq_db_builder / data/willeq.db (T258 slot1).

## Lead (deferred.md index; T242 Review)
Logs show `Player BSP region is SIZE_MAX` after zone-in — the player's
start position resolves to no BSP region. T242 attributed it to a
`CreateZoneLoadSnapshot` race: the snapshot is built with the PREVIOUS
zone's name/geometry (or before the new zone's position arrives), so the
region lookup runs against the wrong tree or a (0,0,0) position. Not the
benchmark's problem (it never calls it).

## Scope
1. Reproduce: zone from befallen → qeynos2 (or any two zones) with DEBUG
   logs; capture the SIZE_MAX line and the ordering of: NewZone received,
   PlayerProfile position applied, snapshot created, geometry loaded,
   region lookup. Identify which is stale at lookup time.
2. Fix the ordering (build the snapshot after the position for the NEW
   zone is known, or re-run the region lookup once geometry is ready —
   whichever the code's existing handoff supports; read application.cpp's
   zone-load comments ~1576 and T220/T227 notes first). No new threads,
   no locks.
3. Test: a zone-flow test (tests/test_zone_connection_flow.cpp or
   test_zoning_graphics_integration.cpp) that asserts the region is
   resolved (not SIZE_MAX) after a two-zone sequence. Red/green.
4. If the region genuinely is outside the BSP for a legitimate start
   position (some zones start outside the tree), say so and downgrade the
   log — but only with evidence from the geometry.

## Verification
Full ctest; visual gate if renderer_zone_load* changed; Review; evidence
review/T260/. Commit: `T260: player region resolved after zone-in` (or attributed form).

## Review (2026-08-26, slot2)

### What was stale at lookup time: the P17 tag, on every zone-in
The plan's lead ("snapshot built with the previous zone's name/geometry or
before the position arrives") is not what the code does. Ordering from a
real session (review/T260/prior_live_log_ordering_extract.txt, T83 block):

| t (s) | event | source |
|---|---|---|
| 0.000 | `LoadZoneSources: Loaded zone 'befallen'` -- tree built from befallen.wld, **tag left empty** | eq_zone.cpp (NewZone handler) |
| 0.181 | `RECEIVED OUR OWN ClientUpdate` -- position applied, OnGameStateComplete | eq_entities.cpp |
| 0.184 | `Game thread: zone load requested, creating snapshot` | application.cpp:817 |
| 0.185 | `Snapshot: m_zone_bsp_tree is stale (captured for '', current zone 'befallen') -- skipping region lookup` | eq_bridge.cpp |
| 10.9 | `graphicsCompleteReady=true, finalizing` -> SetZoneBspTree | application.cpp:831 |

Name and position were both the NEW zone's at snapshot time (P15 already
moved the request to OnGameStateComplete). The tree was the new zone's too.
What mismatched was `m_zone_bsp_tree_zone_name`: P17 added it but only
`SetZoneBspTree()` wrote it; `LoadZoneSources()` -- the path that builds
the tree on every zone-in -- never did. So since P17 the lookup was skipped
100% of the time with a misleading "stale" WARN (three sessions on record,
4/4 zone-ins, zero `Snapshot: player BSP region=` lines anywhere in
review/). The only tree the lookup could ever run against was the
finalization tree, and P17 tagged THAT with `m_current_zone_name` at call
time -- exactly the value that is wrong in the fast re-zone race.

### P15's FATAL, attributed with geometry
`pos=(428,-74,3) zone=befallen`: probe (review/T260/bsp_point_probe_results.txt)
gives region **1161 in qeynos2** and **SIZE_MAX in befallen**. So it was
qeynos2's position against befallen's tree, i.e. the previous load's
finalization landing after the re-zone -- T242's attribution stands, and the
mechanism is the finalization overwriting the game-side tree (application
line 831 replaced whatever LoadZoneSources had built).

### Fix (no new threads, no locks)
1. `eq_zone.cpp LoadZoneSources()`: writes `m_zone_bsp_tree_zone_name =
   zone_name` after extracting the tree; clears it wherever it resets the
   tree. The normal zone-in path now runs the lookup.
2. `EverQuest::SetZoneBspTree(tree, builtForZone)` (now out-of-line in
   eq_bridge.cpp): takes the zone the tree was built FOR and **rejects** it
   (WARN, keeps the current zone's own tree) when that is not the current
   zone. Application carries the completed load's snapshot zoneName with
   the pending tree (`m_pendingBspTreeZone`, set at both
   `m_pendingBspTree` assignment sites under `m_zoneLoadMutex`), so the
   handoff cannot re-tag a stale tree with the new zone even if the next
   snapshot was created in the same game-loop iteration.
3. `CreateZoneLoadSnapshot()`: `LOG_FATAL + std::exit(1)` on SIZE_MAX
   downgraded to a WARN. Geometry backing (plan step 4): P44's befallen
   zone-in coordinate (-195,-982,3) is SIZE_MAX against befallen's **own**
   tree; `playerBspRegion` has **zero consumers** (grep: only
   zone_load_snapshot.h's default and eq_bridge.cpp's writer), and every
   renderer read of a camera region guards `!= SIZE_MAX`. The exit was the
   lookup's only effect.

Territory: eq.h / eq_zone.cpp / eq_bridge.cpp / application.{h,cpp} /
tests only. No renderer_zone_load* change, so no visual gate; goldens
untouched.

### Tests (tests/test_zone_connection_flow.cpp, suite `ZoneLoadSnapshot`)
Real befallen.s3d / arena.s3d via EQ_CLIENT_DATA_PATH (GTEST_SKIP if
absent). Expected indices are from the probe.

| test | red | green |
|---|---|---|
| PlayerRegionResolvedAfterZoneIn (befallen, Testchar restore coord -> 2220) | FAIL (SIZE_MAX) | OK |
| TwoZoneSequenceResolvesAgainstSecondZoneTree (befallen then arena; (0,0,0) -> 93, not befallen's 2216) | FAIL | OK |
| LateFinalizationTreeForPreviousZoneIsRejected (arena current, befallen tree finalized -> still 93) | FAIL | OK |
| MatchingFinalizationTreeIsInstalled | OK | OK |
| OutOfTreeStartPositionIsNotFatal ((-195,-982,3) -> SIZE_MAX, no quit) | OK (lookup skipped) | OK (lookup ran, WARN) |

Logs: review/T260/red_test_zone_load_snapshot.log, green_test_zone_connection_flow_full.log.

### Verification
| step | result |
|---|---|
| cmake --build slot2/build | exit 0 |
| ctest | 2961/2962 -- only AudioManagerTest.PreloadCommonSounds (documented ALSA) |
| build-gles2-x86.sh | exit 0 |
| visual gate | n/a (no renderer file changed) |
| build-arm-noble.sh | exit 0 |
| check_file_sizes.sh fable | 0 fail (4 pre-existing WARNs, all touched files already > 1500) |

### Deviations
- No live eqemu zone-in: `.eqemu-lock` held by T259 at start and at commit
  time. Pre-fix ordering evidence comes from three prior live sessions;
  post-fix behaviour is proven by replay tests over the same handler chain
  with real zone data. A follow-up live session should show
  `Snapshot: player BSP region=2220 pos=(-77.92,21.91,3.00) zone=befallen`
  at Testchar's restore coordinate and no "stale (captured for '')" WARN.
- Plan step 2 ("build the snapshot after the position is known") was
  already satisfied by P15; the fix is the tag, the finalization guard,
  and the downgrade, not a reordering.
