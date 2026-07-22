# F11 — Split entity/spawn/world-object clusters into eq_entities.cpp

Move the entity-tracking core out of `src/client/eq.cpp` (9,569 lines). The
biggest remaining eq.cpp unit (~2,700 lines). HIGH entanglement but still a
pure TU move. No behavior change, no interface change, no reformatting.

**Size rule:** if the single file would exceed ~2,400 lines, split into TWO
files along a natural boundary — `eq_entities.cpp` (spawn/entity lifecycle,
position updates, health) and `eq_world_objects.cpp` (doors, ground spawns,
world objects, click handlers, appearance) — and document the partition.

## Scope (orig refs @ a0c02cd; enumerate by NAME)

1. Zone spawns / world entities (orig 5245–6609): `ZoneProcessZoneSpawns`,
   `NewSpawn` handling, `ZoneProcessSpawnDoor`, `SendZonepoints`,
   `ZoneProcessSpawnAppearance`, ground spawns, time-of-day and weather
   handlers if contiguous (judgment — they're world-state; document).
2. Exp / Raid / ClientUpdate (orig 6680–7154): `ZoneProcessExpUpdate`,
   `ZoneProcessRaidUpdate` (F12 left it here for this unit),
   `ZoneProcessClientUpdate` (~275 lines, entity position interp).
3. DeleteSpawn / MobHealth / HPUpdate (orig 7057–7287).
4. Appearance: `ZoneProcessWearChange`, `UpdatePlayerAppearanceFromInventory`,
   `ZoneProcessIllusion` (orig 11840–12085).
5. `ZoneProcessMoveDoor` (orig 12086–12149).
6. SpawnAppearance/Animation/ClickDoor/ClickObject/CloseContainer/WorldObject
   senders+handlers (orig 14004–14251). `SendTradeSkillCombine` in this range
   is item-economy — judgment: leave for consistency with F05 unless
   contiguity makes leaving it messier; document either way.
7. Entity lookup helpers left by F08's plan: `FindEntityByName`,
   `ListEntities` (orig ~11700s).

## Steps

1. Locate all clusters; produce the moved/left mapping in the review BEFORE
   moving. When in doubt, leave + document.
2. Create the file(s): header comment + pruned includes.
3. Move verbatim; preserve #ifdef guards exactly.
4. File-local static helpers: usual rule.
5. Add file(s) to `CLIENT_CORE_SOURCES` in CMakeLists.txt.

## Verification

- `cmake --build build -j24` clean.
- `ctest --test-dir build` — green modulo documented exceptions in
  refactor/test_baseline.txt.
- `python3 scripts/refactor/verify_move_only.py BASE..HEAD` — PASS or
  documented deviations only.
- `bash scripts/refactor/check_file_sizes.sh --files <abs paths>` — WARN
  accepted, must not FAIL.
- eq.cpp shrinks by ~2,400+ lines.

## Review

### Result: single file (`eq_entities.cpp`), no split needed

Total moved content came to 2,340 source lines (2,438 with header comment +
includes) — under the ~2,400-line size-rule threshold, so no
`eq_world_objects.cpp` split was needed. `check_file_sizes.sh` reports WARN
(>1,500) but not FAIL (>3,000) for the new file, as expected for a file of
this size.

### Moved/left mapping (by name, current `fable`-tip line numbers at start
of this unit — the plan's "orig refs @ a0c02cd" numbers were stale since
F01–F13 already landed on `fable` and renumbered eq.cpp; enumerated by NAME
per the plan's instruction)

**Cluster 1 — Zone spawns / world entities (moved):**
- `ZoneProcessZoneSpawns` (2415–2733)
- `ZoneProcessTimeOfDay` (2735–2765)
- `ZoneProcessSpawnDoor` (2767–2842)
- `ZoneProcessSendZonepoints` (2844–2916) — this is the function the plan
  calls "`SendZonepoints`"; the actual name is `ZoneProcessSendZonepoints`.
- `ZoneProcessSpawnAppearance` (2918–3169)
- `ZoneProcessGroundSpawn` (3460–3536)
- `ZoneProcessWeather` (3538–3568)
- `ZoneProcessNewSpawn` (3570–3778)

**Left (judgment call):** `ZoneProcessEmote` (3171–3458), sitting between
`ZoneProcessSpawnAppearance` and `ZoneProcessGroundSpawn`. Not named in the
plan's scope; it's chat/emote text processing, not spawn/entity/world-object
state. Left in `eq.cpp` and documented per "when in doubt, leave."

**Cluster 2 — Exp / Raid / ClientUpdate (moved):**
- `ZoneProcessExpUpdate` (3781–3807), plus its exclusive file-local helpers
  `EXP_FOR_LEVEL[]` / `MAX_EXP_LEVEL` / `CalculateExpProgress()` (orig lines
  93–184, near top of file) — verified single caller via grep, moved
  together per the file-local-helper rule.
- `ZoneProcessRaidUpdate` (3809–3856) — F12 explicitly left this here for
  this unit, per that plan's note; moved as expected.
- `ZoneProcessClientUpdate` (3882–4156)

**Left (judgment call):** `ZoneProcessGuildMOTD` (3858–3880), sitting
between `ZoneProcessRaidUpdate` and `ZoneProcessClientUpdate`. Not named in
scope — guild domain, already owned by F04 (`eq_guild.cpp`). Left in
`eq.cpp` and documented.

**Cluster 3 — DeleteSpawn / MobHealth / HPUpdate (moved):**
- `ZoneProcessDeleteSpawn` (4158–4254)
- `ZoneProcessMobHealth` (4256–4311)
- `ZoneProcessHPUpdate` (4313–4387)

**Cluster 7 — Entity lookup helpers left by F08 (moved):**
- `FindEntityByName` (5836–5855)
- `ListEntities` (5857–5926)
(F08's review confirms these were deliberately left in `eq.cpp` for this
unit; confirmed contiguous, moved as a pair.)

**Cluster 4 — Appearance (moved):**
- `ZoneProcessWearChange` (5928–5981)
- `UpdatePlayerAppearanceFromInventory` (5983–6059)
- `ZoneProcessIllusion` (6061–6129)

**Cluster 5 — Door move (moved):**
- `ZoneProcessMoveDoor` (6174–6236), plus its exclusive file-local helper
  `getDoorTypeFromName()` (orig 6131–6172, `#ifdef WITH_AUDIO` guard
  preserved exactly) — verified single caller, moved together.

**Left (not in scope):** `ZoneProcessPlayerStateAdd`, `ZoneProcessDeath`,
`ZoneProcessPlayerStateRemove`, `ZoneProcessStamina`,
`ZoneProcessZonePlayerToBind`, `ZoneProcessLevelUpdate` (immediately
following `ZoneProcessMoveDoor`) — none named in the plan's scope
(death/combat-state and level/stamina/bind belong to other domains). Left
in `eq.cpp` and documented.

**Cluster 6 — World-object / animation senders+handlers (moved):**
- `SendSpawnAppearance` (6822–6843)
- `SendAnimation` (6845–6865)
- `SendClickDoor` (6867–6904)
- `SendClickObject` (6906–6937, including its preceding
  "World Object / Tradeskill Methods" section-header comment)
- `SendCloseContainer` (6959–6989)
- `GetWorldObject` (6991–6998)
- `ClearWorldObjects` (7000–7005)
- `ZoneProcessClickObjectAction` (7007–7055)

**Judgment call — `SendTradeSkillCombine` / `ZoneProcessTradeSkillCombine`
(left):** Both sit nestled inside the world-object cluster (the sender
between `SendClickObject` and `SendCloseContainer`; the handler right after
`ZoneProcessClickObjectAction`, before `SendJump`). The plan explicitly
calls out `SendTradeSkillCombine` as a judgment call — item-economy,
default to leaving it for consistency with F05 (`eq_merchant.cpp`) unless
contiguity makes leaving it messier. Leaving a single function out of the
middle of an otherwise-contiguous run is not messier than moving it (it's
still just one function elided from the cut, no reordering required), so
both were left in `eq.cpp`, paired together since they're sender/handler
counterparts for the same tradeskill-combine flow.

### Helper analysis (file-local statics)

`eq.cpp` had exactly two file-scope `static` declarations outside class
methods:
1. `EXP_FOR_LEVEL[]` / `MAX_EXP_LEVEL` / `CalculateExpProgress()` — single
   caller `ZoneProcessExpUpdate`. Moved with it.
2. `getDoorTypeFromName()` — single caller `ZoneProcessMoveDoor`. Moved
   with it, `#ifdef WITH_AUDIO` guard preserved exactly.

No other file-local helpers were found (verified via
`grep -n "^static \|^namespace {"` across the whole file before the move).

### Line counts

- `eq.cpp`: 9,569 → 7,229 lines (−2,340, matches the sum of all moved
  ranges exactly).
- `eq_entities.cpp`: 2,438 lines (2,340 moved body + 98 header
  comment/includes).

### Verification results

- `cmake --build build -j24`: clean (one pre-existing `-Wpointer-arith`
  warning class, same as already present elsewhere in `eq.cpp`; no new
  warnings introduced by the move itself beyond that one instance in the
  moved `ZoneProcessClickObjectAction` code, which is verbatim).
- `ctest --test-dir build`: 1190/1192 run, 2 failed — both pre-documented
  in `refactor/test_baseline.txt` and unrelated to this change:
  `AudioManagerTest.PreloadCommonSounds` (host ALSA config) and
  `RDPServerFrameTest.UpdateFrameMultipleTimes` (pre-existing rare
  segfault flake).
- `verify_move_only.py fable..HEAD`: see committed run output below.
- `check_file_sizes.sh --files eq_entities.cpp`: WARN (2,438 > 1,500), not
  FAIL — accepted per plan.

### Deviations from plan text

- Plan step scope used stale "orig refs @ a0c02cd" line numbers (pre-dating
  F01–F13 landing on `fable`); re-derived all boundaries by NAME against
  the current `fable` tip, per the plan's own instruction to "enumerate by
  NAME."
- Plan item 1 names the sender as "`SendZonepoints`"; the actual symbol is
  `ZoneProcessSendZonepoints` (a receive-side handler, not a sender). Moved
  as the same function the plan clearly intended.
- No `eq_world_objects.cpp` split was needed (see "Result" above).
