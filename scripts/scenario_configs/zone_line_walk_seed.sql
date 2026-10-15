-- B1068 (#388): zone_line_walk. Testchar starts in qeynos (zone 1) facing the
-- qeynos -> qeynos2 gate whose BSP zone-line region is region 4519
-- (DRNTP00002-00155-00007999999999, client-coord bounds x[468.8,482.8]
-- y[-447.8,-437.3] z[-1,20]; review/B1068/qeynos_bsp_zoneline_regions.txt).
-- The start is client (448,-442), 20 units short of the region, on the line
-- through the server's own zone point qeynos #1 (server -442,464 = client
-- 464,-442). character_data is SERVER order: x = client y, y = client x.
-- Heading 0 faces server +y = client +x, toward the line. Seeded after the
-- snapshot (scripts/scenario.sh 3b), so the restore moves Testchar back.
UPDATE character_data SET zone_id = 1, zone_instance = 0,
    x = -442, y = 448, z = 4, heading = 0
WHERE name = 'Testchar';
