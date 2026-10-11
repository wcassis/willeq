# B448 (#29): a race change on the local player reaches the vision system

**Status: defect half CLOSED by this unit** (`task/B448-self-race-vision`, cut
at `gitlab/fable` `1dc42487`, 2026-10-11). The oracle-ratio half of the issue's
close condition is NOT done -- see "Not verified / left open".

## The defect

`PlayerCameraComponent::setPlayerRace()` is the one writer of the racial vision
floor (`baseVision_`). Its only caller was `IrrlichtRenderer::registerEntity()`
(T348, B056), i.e. zone-in. A race change on a character already in the world --
an illusion spell, or a GM `#race N` -- arrives as `OP_Illusion`.
The game thread already handled it:
`EntitiesComponent::ZoneProcessIllusion()` decodes the packet and publishes
`EntityAppearanceChanged` with `isPlayer = (spawn_id == MySpawnId())`, and the
`IrrlichtBridge` arm forwards a player's event to
`IrrlichtRenderer::updatePlayerAppearance()`. That function's body was a
`LOG_DEBUG` and nothing else, so the race stopped there (MEASURED T428:
`#race 330` confirmed in chat twice, no `Player race 330 -> base vision:` line,
vision ratio 0.95-0.99).

## The fix

- `IrrlichtRenderer::updatePlayerAppearance()` (`renderer_entities.cpp`) calls
  `PlayerCameraComponent::setPlayerRace()` when the race differs from the one
  last applied (`PlayerCameraComponent::playerRace()`). The same arm carries
  every `OP_WearChange` / inventory re-publish of the race the player already
  has; those do not touch vision.
- `setPlayerRace()` (`renderer_camera.cpp`) now composes
  `current = max-rank(base, buff)` instead of `current = base`. The second
  caller can fire while a vision buff is up, and resetting to the base would
  wipe it. `setVisionType()` keeps the last absolute buff value
  (`buffVision_`) for this.
- No new `GameEventType`: `EntityAppearanceChanged` already carries the race as
  a typed payload field, and the renderer is reached only through the bridge
  (game thread publishes, render thread consumes). Nothing in `EverQuest` calls
  the renderer.

## Red / green

`tests/test_self_race_vision.cpp` (registered in `tests/cmake/renderer_guards.cmake`)
feeds a real `OP_Illusion` for the player's own spawn id through a real
`EverQuest` and applies what it publishes to a real `IrrlichtBridge` over a
bare `IrrlichtRenderer`. It also covers a peer's illusion (must not move our
vision), a race change under a vision buff (keeps the buff, then the fade lands
on the NEW floor), and a same-race appearance event (leaves a buff alone).

- RED at the fable cut: 5 of 7 fail, for example
  `RaceThreeThirtyOnSelfMovesTheBaseToFroglokVision`: base vision `0` (Normal)
  where `3` (FroglokVision) was expected. The two that pass are the negative guards.
- GREEN on the branch: 7 of 7. The neighbours `test_vision_buff_fade_wire`,
  `test_scene_ambient_recompute` and `test_player_camera_component` stay green.
  `ctest -L hygiene` is green (the two reproducibility rows needed their tool
  binaries built first).

## Live (local eqemu lane, dev-u24)

Testchar (Human) in `hole`, headless graphics (surfaceless EGL),
`--script` with `/say #race 330`, `#race 6` and `#race 1`, all after zone-in.
The full log and commands are in `/mnt/projects/fable/review/B448/`:

```
[16:22:37.768] [GRAPHICS] Player race 1 -> base vision: Normal          (zone-in)
[16:23:07.719] [ENTITY] Testchar (ID: 776) illusioned to race 330 ...
[16:23:07.742] [GRAPHICS] Player race 330 -> base vision: FroglokVision
[16:23:22.789] [GRAPHICS] Player race 6 -> base vision: Ultravision
[16:23:37.776] [GRAPHICS] Player race 1 -> base vision: Normal
```

## Not verified / left open

- **fable's own Froglok night ratio against the oracle's x2.6803 R / x1.9478 G /
  x2.1753 B** (T428 Review §4) is NOT measured here. It needs a T428-style
  session: forced 22:00, a pinned camera, frames from video, and the per-channel
  ratio over the session's own night base. The live run above proves that the
  floor now MOVES. It does not prove the pixels match the official client.
- `updatePlayerAppearance()` drops the event while `isLoading()` is true (the
  guard that was already there). A race change that lands during a zone load
  is applied by the next zone-in's `registerEntity()` from the spawn's race, so
  it is not lost unless the server's spawn race differs from the illusion.
  Not measured.
- Buff vision is now kept across a zone-in's `setPlayerRace()`, where it used
  to be reset to the base. That is the intended max(base, buff) rule, and the
  buff landings at zone-in republish `VisionChanged` in any case. Not measured live.
