# D08: Publish World/Environment + Zone Lifecycle Events

## Plan

### Overview

Add event publishing alongside existing direct renderer calls for weather, swimming,
time of day, zone change, zone loading progress, collision map, zone lines,
character info, world object spawns, and note window.

Note: ExpProgressChanged is already published (done in a prior session).

### Steps — Weather / Swimming / Time of Day

1. Publish WeatherChanged in `ZoneProcessWeather()` (line ~6626):
   - After `m_renderer->setWeather(...)`, push WeatherChanged
   - Include: type, intensity

2. Publish SwimmingStateChanged in `UpdatePlayer()` (line ~16060):
   - After `m_renderer->setSwimmingState(...)`, push SwimmingStateChanged
   - Include: isSwimming, swimSpeed, isLevitating

3. Publish TimeOfDayChanged in `ZoneProcessTimeOfDay()` (line ~5834):
   - After `m_game_state.world().setTimeOfDay(...)`, push TimeOfDayChanged
   - Include: hour, minute, day, month, year

### Steps — Zone Lifecycle

4. Publish ZoneChanged in `ZoneProcessNewZone()` (line ~3055):
   - After `m_game_state.world().setZone(...)`, push ZoneChanged
   - Include: zoneName, zoneId, x/y/z (safe coords), heading (0)

5. Publish ZoneLoading in `SetLoadingPhase()` (line ~1089):
   - After renderer loading progress update, push ZoneLoading
   - Include: zoneName, zoneId, progress, statusMessage

6. Publish CollisionMapChanged in `LoadZoneMap()` (line ~14349):
   - After `m_zone_map.reset(HCMap::LoadMapFile(...))`, push CollisionMapChanged
   - Include: map pointer (opaque)

7. Publish ZoneLineBoundingBoxes in `ZoneProcessSendZonepoints()` (line ~5972):
   - After `m_zone_lines->setServerZonePoints(...)`, push ZoneLineBoundingBoxes

### Steps — Character Info / World Objects / Notes

8. Publish CharacterInfoChanged in `ZoneProcessPlayerProfile()` (line ~3522):
   - After `m_renderer->setCharacterInfo(...)`, push CharacterInfoChanged
   - Include: name, level, className, deity

9. Publish WorldObjectSpawned in `ZoneProcessGroundSpawn()` (line ~6587):
   - After storing world object, push WorldObjectSpawned
   - Include: dropId, x/y/z, heading, modelName, objectType

10. Publish NoteWindowOpened in `ZoneProcessReadBook()` (line ~3937):
    - After `m_renderer->showNoteWindow(...)`, push NoteWindowOpened
    - Include: text, type

11. Build and verify compilation

## Acceptance Criteria

- All existing direct renderer calls remain unchanged
- Bridge receives weather, swimming, time, zone, character, world object, and note events
- All push calls guarded by `if (m_bridge)`
- No new warnings or errors in build
- ExpProgressChanged already exists — not duplicated

## Review

All steps completed as planned. No deviations.

Call sites covered:
- Weather: ZoneProcessWeather (WeatherChanged)
- Swimming: UpdatePlayer state transition block (SwimmingStateChanged — only on
  state change, not every frame)
- Time: ZoneProcessTimeOfDay (TimeOfDayChanged)
- Zone: ZoneProcessNewZone (ZoneChanged), SetLoadingPhase (ZoneLoading)
- Collision: LoadZoneMap (CollisionMapChanged)
- Zone lines: ZoneProcessSendZonepoints (ZoneLineBoundingBoxes)
- Character: ZoneProcessPlayerProfile (CharacterInfoChanged)
- World objects: ZoneProcessGroundSpawn (WorldObjectSpawned)
- Note: ZoneProcessReadBook (NoteWindowOpened)
- ExpProgressChanged: already existed, not duplicated

SwimmingStateChanged placed inside `if (newState != m_water_state)` block to
avoid per-frame bridge spam. All other events fire only on packet receipt or
discrete state changes.

All 10 push sites guarded by `if (m_bridge)`.

Build succeeds, 73 relevant tests pass. All acceptance criteria met.
