# B339+B340+B341+B344+B345 (#133 #134 #135 #138 #139): the fourteen write-only members of T414 Leg C

T506. One mechanical card, five issues: each member is written and never read,
each issue says "closes when the member goes", and none has a consumer since
2026-09. The ratchet is `scripts/refactor/check_write_only_fields.py`
(`WriteOnlyFields` ctest, label `hygiene`): it fails STALE when an exempted
member disappears, so every deletion also deletes its `EXEMPT` row.

## Outcome table

| Member | Issue | Status |
|---|---|---|
| `WeatherEffectsController::targetIntensity_` (`weather_effects_controller.h`) | #133 B339 | DELETED -- the member, its two writes (`setWeather`, `resetWeatherState`), the EXEMPT row |
| `ZoneLifecycleComponent::entitiesLoaded_` (`zone_lifecycle_component.h`) | #134 B340 | DELETED -- the member, its two writes (`unloadZone`-side reset, `setNetworkReady(false)`), the EXEMPT row, the write-only header comment |
| `ZoneLifecycleComponent::expectedEntityCount_` (`zone_lifecycle_component.h`) | #134 B340 | DELETED -- the member, its two writes, the EXEMPT row; `setExpectedEntityCount()`'s body keeps the LOG_DEBUG count, which is the only remaining consumer of the forwarder |
| `OverlaysComponent::currentTargetLevel_` (`overlays_component.h`) | #135 B341 | **KEPT -- it is READ.** `src/client/graphics/overlays/target_ring_draw.cpp:48` reads it (`eqt::ConTierForLevels(ctx_.entityRenderer->getConPlayerLevel(), currentTargetLevel_)`, the B994 3D target ring tier colour). A member read anywhere is not write-only; the census agrees (it is not in the write-only list after this change). |
| `OverlaysComponent::currentTargetInfo_` (`overlays_component.h`) | #135 B341 | DELETED -- the member, its two writes (`clearCurrentTarget`, `setCurrentTargetInfo`), the EXEMPT row. The `TargetInfo` type STAYS: `irrlicht_bridge.cpp`'s `TargetChanged` arm builds one as its payload unit, and `setCurrentTargetInfo(info)` still maintains the legacy fields from it (its test `SetCurrentTargetInfoForwarderUpdatesTheLegacyFields` passes unchanged). |
| `BridgeComponent::m_audio_config_vendor_music` (`bridge_component.h`) | #138 B344 | DELETED -- the member, the store, the EXEMPT row. **The config key is user-facing and was NOT silently dropped**: `application_config.cpp` still parses `audio.vendor_music` (it keeps its LOG_INFO), and `SetVendorMusic()` (kept, same signature, still called from `Application::initEverQuestClient` and the G04 golden entry 91) now logs one `LOG_WARN(MOD_AUDIO, "audio.vendor_music is parsed but ignored: {}")` per run. `bridge_component.h` gained an explicit `#include "common/logging.h"` for it (the header had no logging before; every bridge TU includes it in its .cpp, none in the header). |
| `PlayerComponent::m_duel_target_id` (`player_component.h`) | #138 B344 | DELETED -- the member, its two writes (`SendDuelRequest`, `SendDuelAccept`), the EXEMPT row. The reply path (`m_is_dueling` / `m_has_pending_duel`) never consulted it |
| `SkillManager::m_player_race` (`skill_manager.h`) | #138 B344 | DELETED -- the member, its one write (`initialize()`), the EXEMPT row. `initialize()` keeps its `player_race` parameter (the caller `zone_entry.cpp:551` and the LOG_DEBUG line both keep it); no skill gate reads a race |
| `CameraController::orbitAngleInitialized_` (`camera_controller.h`) | #139 B345 | DELETED -- the member, its one write (`setOrbitAngle`), the EXEMPT row |
| `ConsoleRenderer::m_playerHeading` (`console_renderer.h`) | #139 B345 | DELETED -- the member, its one write (`setPlayerPosition`), the EXEMPT row. The `IOutputRenderer` signature keeps its `heading` parameter (`NullRenderer` and the graphics `setPlayerPosition` are untouched); the console body `(void)`s it |
| `ConsoleRenderer::m_curEnd` (`console_renderer.h`) | #139 B345 | DELETED -- the member, its one write (`updateCharacterStats`), the EXEMPT row |
| `ConsoleRenderer::m_maxEnd` (`console_renderer.h`) | #139 B345 | **DELETED -- also write-only, verified (the card asked to check, not assume).** The status print (`printStatus`) shows HP and Mana and location, but never END: the only `m_maxEnd` occurrence outside the declaration and its write is none (`rg -n '\bm_maxEnd\b'` tree-wide: declaration line + one write). `updateCharacterStats()` keeps its endurance parameters (the base signature; `NullRenderer` overrides the same one) and `(void)`s the pair. **It never had an EXEMPT row** (it sat on the combined declaration line beside `m_curEnd`'s row), which is why 14 deletions remove exactly 13 rows and the census goes 61 to 48 |
| `ConsoleRenderer::m_startTime` (`console_renderer.h`) | #139 B345 | DELETED -- the member, its one write (`initialize`), the EXEMPT row. Issue #139 names it; the card's summary line did not, but its "NOT m_maxEnd unless..." instruction applies per-member and the census lists it. `console_renderer.h`'s `#include <chrono>` stays (`system_clock::now()` at the status-line timestamp) |
| `ConflictInfo::binding1` / `binding2` (`hotkey_manager.h`) | #139 B345 | DELETED -- the two members, their two writes (`detectConflicts`), both EXEMPT rows. The report's `message` string already carries both actions and both bindings; `test_hotkey_manager.cpp`'s five conflict tests read `message` only and pass unchanged |

## RED / GREEN (the ratchet)

RED -- 13 members deleted, EXEMPT rows still present (exit 1):

```
declared members: 8057 (5399 distinct names) in include/client, src/client
STALE exemption (no longer write-only): BridgeComponent::m_audio_config_vendor_music -- B344: the vendor-music config reaches no audio decision
STALE exemption (no longer write-only): CameraController::orbitAngleInitialized_ -- B345: orbit-camera latch with no reader
STALE exemption (no longer write-only): ConflictInfo::binding1 -- B345: hotkey conflict report fields nothing formats
STALE exemption (no longer write-only): ConflictInfo::binding2 -- B345: hotkey conflict report fields nothing formats
STALE exemption (no longer write-only): ConsoleRenderer::m_curEnd -- B345: headless console renderer state with no reader
STALE exemption (no longer write-only): ConsoleRenderer::m_playerHeading -- B345: headless console renderer state with no reader
STALE exemption (no longer write-only): ConsoleRenderer::m_startTime -- B345: headless console renderer state with no reader
STALE exemption (no longer write-only): OverlaysComponent::currentTargetInfo_ -- B341: target-overlay state cached per frame and never drawn from
STALE exemption (no longer write-only): PlayerComponent::m_duel_target_id -- B344: /duel stores the target id; the reply path never consults it
STALE exemption (no longer write-only): SkillManager::m_player_race -- B344: race stored for skill gating that does not gate on race
STALE exemption (no longer write-only): WeatherEffectsController::targetIntensity_ -- B339: the smooth weather transition it was for does not exist
STALE exemption (no longer write-only): ZoneLifecycleComponent::entitiesLoaded_ -- B340: L09 deleted the zero-caller accessor and left the member
STALE exemption (no longer write-only): ZoneLifecycleComponent::expectedEntityCount_ -- B340: L09 deleted the zero-caller accessor and left the member

0 unlisted, 13 stale.
An exemption whose field is no longer write-only must be removed from EXEMPT -- the table is not allowed to outlive its rows.
```

GREEN -- the 13 rows removed (exit 0; full output in `green_run.txt`):

```
declared members: 8057 (5399 distinct names) in include/client, src/client
write-only census clean: 48 fields, all 48 classified in EXEMPT.
unused census clean: 86 fields, all 86 classified in EXEMPT_UNUSED.
```

Baseline before the change: `write-only census clean: 61 fields, all 61
classified in EXEMPT` -- 61 - 13 = 48. The unused half (86) moved not at all.
The B342/B343 rows (six: the three `InventoryManager` callbacks,
`ParticleManager::entityDirCallback_`, `CharacterModelView::inventoryManager_`,
`EQAnimatedMeshSceneNode::sceneManager_`) were NOT touched by this card; an
earlier draft of the row removal had swept all six and the GREEN above is with
them restored (they are present in the file again).

## Issue handling

- #133, #134, #138, #139: every member the issue names was deleted (plus
  `m_maxEnd`, verified write-only). Closed by the MR's `Closes` lines; #139
  gets a note recording the `m_maxEnd` finding (posted 2026-10-13 by B1018,
  https://gitlab.goldfarm.local/willeq/willeq/issues/139#note_4233 — it also
  records the multi-declarator census fix that is WHY `m_maxEnd` never
  surfaced in an earlier census pass).
- #135: NOT closed by `Closes`. `currentTargetInfo_` went, but
  `currentTargetLevel_` is read (`target_ring_draw.cpp:48`) and stays, so a
  note is posted on the issue recording the split instead (posted 2026-10-13
  by B1018,
  https://gitlab.goldfarm.local/willeq/willeq/issues/135#note_4230) -- the
  card's rule is that an issue where a member was KEPT because it is read
  gets a note, not
  a close. The MR lists `Closes #133 / #134 / #138 / #139` (all of their
  members went); `#135` gets the note. The card's AC text said "the MR
  description lists `Closes #135`" but its own kept-member rule overrides that
  one line; the note + this README are the record of the split.

## Not verified / left open

- The full ctest suite and the visual gate run on CI (this host builds
  `willeq_core` + the affected test targets only, `-j3`, 4 GiB cap). Local
  runs: `WriteOnlyFields` + the full `hygiene` label (27/27, after building
  the asset-tool targets this fresh worktree lacked: `generate_textures`,
  `spell_sprite_atlas_builder`, `item_icon_atlas_builder`, `willeq_db_builder`,
  `eqe1_builder`, `ui_atlas_builder`), all thirteen rebuilt test targets run
  directly (bridge 9/9, hotkey_manager 63/63, hotkey_dispatch_census 4/4,
  zone_lifecycle 14/14, overlays 7/7, forage_fishing 12/12, skill_gates 19/19,
  command_output_routing 4/4, console_target_hp 3/3), and the three
  `*Reproducible` pins (Textures/Pack/Db all pass on the checked-in pins --
  this change touches no shipped asset input).
- **Local environment fix, not a code change:** this host's gitignored
  `third_party/irrlicht-gles2-x86/` prefix was stale -- its
  `COpenGLES2Driver.o` predated the draw-census additions, so linking any
  graphics test failed on `gles2GetGlRenderer` / `gles2GetDrawCensusTotals`
  (the stamp check in `scripts/ci/irrlicht_input_stamp.sh` also failed).
  Rebuilt that one TU from the current `docker/irrlicht-drm/` sources with
  the prefix's own flags and re-archived it (`ar rs`). CI builds its own
  prefix; nothing in the repo changed.
- No Pi run: every deletion is state a draw path never touched (or, for
  `currentTargetLevel_`, a draw path that keeps reading it), so the visual
  gate's pixel identity is expected to hold; that is the CI check, not an
  assumption I made.
- `setExpectedEntityCount`'s debug log line keeps its count; the forwarder
  (bridge → `IrrlichtRenderer` → `ZoneLifecycleComponent`) is now a log-only
  route. If Will wants the whole route gone it is a one-card follow-up, not
  part of the "closes when the member goes" bar.
