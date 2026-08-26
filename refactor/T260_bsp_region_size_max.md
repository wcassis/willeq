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
