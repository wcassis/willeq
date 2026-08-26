# T288 — the seven remaining duplicate stores: position/heading, spawn id, zone name/id (T286 hand-back)

Slot: slot1. x86. OWNS eqemu (noclobber) for a live zone-in + movement
probe — these are the values most likely to break visibly. Class A.
Territory: `EverQuest`'s `m_x/m_y/m_z/m_heading`, `m_my_spawn_id`,
`m_current_zone_name`/`m_current_zone_id` and their `PlayerState`/
`WorldState` counterparts, every reader of each, tests. Do NOT touch the
composite/EQE1 path (T287's territory if it resumes) or inventory.

## Why these were kept
T286 deleted 99 of 107 duplicates but kept 8, and was explicit that 7 of
them are justified by **size and behaviour risk, not by a wire-path need**.
They interlock in the same functions (movement, zone transition, spawn
handling), which is why they belong in one unit rather than seven.
`m_character` is the ONE genuine exception (login-time name every packet
sender uses vs `PlayerState::m_name`, which does not exist until the
PlayerProfile arrives) — it must NOT be merged away.

## Traps established by earlier units — honour them
- **Heading conventions are load-bearing** (`docs/CLAUDE.md`, the memory
  note): players and NPCs do not share default rotations; the 90° offset is
  NOT drift to harmonise. T231 copied PlayerProfile's `90 - wire*360/512`
  rule deliberately rather than sharing a helper.
- **Coordinate order**: server order vs geometry order (INVERSEXY) burned
  T260 (its expected BSP value was in the wrong order) and T249/T282 (client
  slot ≠ server slot). State which order each store holds.
- T55/T58: `SetPosition`/`SetHeading` are the write path; T237 added the
  movement-mode writes beside them.

## Scope
1. One store per value, `PlayerState`/`WorldState` canonical, every reader
   routed. Keep `m_character`.
2. Update `tests/test_duplicate_state_stores.cpp`'s exception list (it
   asserts both directions, so it will fail until you do — that is the
   point).
3. Tests: position/heading round-trip through the real packet handlers;
   spawn id set once and read everywhere; zone name/id coherent across a
   zone change. Red/green measured.
4. Live: zone in, move, `/loc` and `/dumpscene` agree with the server's
   view (`/gmfind` self, T229's technique); zone to a second zone and back.

## Verification
Full ctest (use `-j6` — the visual gate's CPU ceiling flakes under heavy
parallelism, T286/T287); visual gate; Review; evidence review/T288/.
Commit: `T288: position, spawn id and zone identity have one store each`.
