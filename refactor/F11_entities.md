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

(executor fills in)
