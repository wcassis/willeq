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

---

## Review (executor, 2026-08-26)

Branch `task/T288-position-stores` off fable `25032f55`. x86, slot1, OWNED
eqemu (noclobber take, released + deleted, Testchar restored). Evidence:
`/mnt/projects/fable/review/T288/` (README.md there indexes it).

### Outcome: all seven merged. `m_character` kept.

| store | was | now |
|---|---|---|
| `m_x`/`m_y`/`m_z` | EverQuest member + `PlayerState::m_x/y/z`, hand-mirrored at ~6 of ~20 write sites | **`PlayerState` only.** `EverQuest::PosX()/PosY()/PosZ()` are accessors into it |
| `m_heading` | same | **`PlayerState::m_heading` only.** `HeadingDeg()` |
| `m_my_spawn_id` | EverQuest member + `PlayerState::m_spawnId`, mirrored at 4 of 5 writes | **`PlayerState::m_spawnId` only.** `MySpawnId()`/`SetMySpawnId()` |
| `m_current_zone_name`/`m_current_zone_id` | EverQuest members + `WorldState::m_zoneName/m_zoneId` | **`WorldState` only.** `CurrentZoneName()`/`CurrentZoneId()` |
| `m_character` | login-time name; `PlayerState::m_name` is the server-reported one | **KEPT**, unchanged, and it is now the ONLY entry in the pinning test's `kKnownRemaining` |

The accessor block sits at the top of `EverQuest`'s private section with the
coordinate-order and heading rules written next to it, so the next reader
finds the convention where the members used to be.

### The drift was real, and user-visible

T286 deferred these on size, not on evidence that they were coherent. They
were not. Measured against the **pre-T288** source with
`tests/test_position_store_coherence.cpp` (new, 9 tests): **5 FAILED**.

1. **`ZoneProcessClientUpdate`'s "this is us" branch wrote only EverQuest's
   copy.** That is the server's authoritative position for the player, every
   tick of a real session. So `PlayerState::x/y/z` never moved off the
   zone-in value, and with it every state-layer reader:
   `EqActionHandler::clickNearestDoor()` (picks the nearest door to the
   *zone-in point*), `EqActionHandler::targetNearestPlayer()` via
   `getNearestPlayer(player.x(), player.y(), player.z(), player.spawnId())`,
   and `GameState::playerPosition()`.
2. **`EverQuest::GetHeading()` already read `PlayerState`** — but the whole
   integrator, `Face()`, `MoveTo()`, the strafe/turn path and the
   self-ClientUpdate all wrote only `m_heading`. `GetHeading()` therefore
   returned whatever the last `SetHeading()`/PlayerProfile left, not where
   the player was facing. Red probe: packet sets 270 deg, `GetHeading()`
   returned 0.
3. **`ZoneProcessPlayerProfile` wrote `m_my_spawn_id` without mirroring** —
   the one spawn-id write site of five that did not. On an ordinary zone-in
   the profile IS the source of the id, so `player.spawnId()` stayed 0 and
   `getNearestPlayer()`'s "skip myself" argument could never exclude the
   player.
4. **`CleanupZone()` cleared EverQuest's zone name/id and left `WorldState`
   holding the departed zone**, so `GetCurrentZoneName()` (which already read
   `WorldState`) and `CreateZoneLoadSnapshot()` (which read the EverQuest
   copy) disagreed for the whole teardown window.

### Event behaviour is bit-for-bit unchanged — deliberately

The raw members fired nothing. `PlayerState::setPosition/setHeading` fire
`PlayerMoved` on the state-layer `EventBus`, and `AutomatedMode` subscribes
to **every** event on that bus. Routing the ~20 integrator writes through the
firing setters would have handed script mode a new per-tick event stream —
a behaviour change wearing a refactor's clothes. So:

- `PlayerState::setPositionNoEvent()` / `setHeadingNoEvent()` — same storage,
  no event. Used by the movement integrator, the self-ClientUpdate branch,
  the renderer's `PlayerPositionChanged` intent, jump/strafe/FixZ, `/warp`.
- `setPosition()` / `setHeading()` stay the **discrete-relocation** path
  (zone-in, bind respawn, `EverQuest::SetPosition/SetHeading`) and still fire,
  exactly as before.
- `WorldState::clearZone()` — clears name+id without firing `ZoneChanged`.
  This is precisely T286's stated reason for deferring the zone pair: a
  firing clear would make a console session print a zone change to the empty
  string on every teardown. `CleanupZoneClearsTheOneZoneIdentity` asserts
  zero `ZoneChanged` on the state bus across a teardown.
- `ZoneProcessNewZone` now parses the zone name/id into **locals** and lets
  the single existing `world().setZone(...)` call be the one write. Writing
  the name first via `setZoneName()` would have fired `ZoneChanged` before
  the safe coordinates were parsed, so the event would have carried the
  previous zone's arrival position.

### Traps

- **Heading (`docs/CLAUDE.md`, `heading-90-offset-load-bearing`).** The
  player's `90 - server_heading` flip is preserved verbatim at each site and
  is still NOT shared with NPCs. `SelfClientUpdate_HeadingFlipIsStillPlayerOnly`
  replays one `OP_ClientUpdate` heading to an NPC and to the player and
  asserts they land on **different** values (180 vs 270). Nothing was
  factored into a shared helper.
- **Coordinate order (T260/T249/T282).** No swap added, removed or moved.
  `PosX()/PosY()/PosZ()` hold CLIENT coordinates (INVERSEXY already applied);
  every outbound site still swaps back at exactly the line it did before.
  Proven at the wire, not by inspection — see the live table below.
- **T55/T58.** `SetPosition`/`SetHeading` remain the write path for forced
  relocation; T237's movement-mode writes are untouched.

### Live evidence (eqemu, Testchar, befallen)

| leg | client | server |
|---|---|---|
| zone-in (`live_probe.log`) | `/loc` -> `21.9, -77.9, 3.0` | `db_before.txt`: `x=-77.875 y=21.875 z=3` |
| `/warp 31.9 -67.9 3.0` (`live_move.log`) | `/loc` -> `31.9, -67.9, 3.0` | `db_after_move.txt` (post-`/camp`): `x=-67.9 y=31.9` |
| befallen -> arena -> befallen (`live_zone_roundtrip.log`) | `/loc` and `/dumpscene`'s `[PLAYER]` line agree at each step: `(31.9,-67.9,3.0)` / `(-1009.0,146.0,51.0)` / `(-82.0,35.0,3.0)` | spawn id moves 174 -> 31 -> 175; each scene dump lists that zone's own entities |

`/gmfind` was refused client-side ("not in GM mode when you logged in"), which
needs a relog to clear; the DB read is a stricter statement of the same thing
(it is the server's persisted view, not a chat echo), so the leg was taken
that way rather than burning a relog cycle. Flagged, not absorbed.

### The pinning test

`tests/test_duplicate_state_stores.cpp`'s `kKnownRemaining` now holds exactly
one entry, `m_character`. The test asserts both directions, so it went red on
the seven deletions until the list was updated — as designed.

### Verification

| step | result |
|---|---|
| `cmake --build build -j24` | PASS |
| `ctest --test-dir build -j6` | **3174/3175**; sole failure `AudioManagerTest.PreloadCommonSounds` (documented ALSA host quirk) |
| `bash scripts/build-gles2-x86.sh` | PASS |
| visual gate | **PASS**, goldens untouched |
| `bash scripts/build-arm-noble.sh` | PASS |
| `check_file_sizes.sh fable` | no NEW FAILs. `src/client/eq.cpp` was already over (3901 -> **3913**, +12) and `include/client/eq.h` already WARN (2489 -> **2538**, +49 — the documented accessor block replacing the seven member declarations). The other six WARNs (`eq_action_handler.cpp`, `eq_bridge.cpp`, `eq_entities.cpp`, `eq_merchant.cpp`, `eq_movement.cpp`, `eq_player_info.cpp`) are all pre-existing. |

### Deviations

1. `/gmfind` self was replaced by a direct read of `character_data` (above).
2. Two new no-event setters (`setPositionNoEvent`/`setHeadingNoEvent`) and one
   new `WorldState::clearZone()` were added rather than routing everything
   through the existing firing setters. Stated above with the reason; without
   them the merge would silently change what script mode observes.
