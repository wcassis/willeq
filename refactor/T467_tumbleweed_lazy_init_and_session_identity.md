# T467 — the lazily-created TumbleweedManager exits the process (B550); a second character inherits the first one's UI state (B560)

Class A (both unconfirmed live — confirm with a failing test first), **no
lock, no Pi**, slot3.

## B550 — `OverlaysComponent::applyEnvironmentalDisplaySettings()`

T463: it lazily creates a `TumbleweedManager` without
`setConstrainedTextureCache()`, and `TumbleweedManager::init()` then takes the
path whose pack miss is FATAL `exit(1)` (EQE1 all-or-nothing — the exit itself
is correct behaviour; reaching it through a missing wiring call is the defect).
Reachable from Step 11 of every zone load when `rollingObjects` was off at
renderer init and turned on later. Compare with the init-time construction of
the same manager and of every sibling environment manager in
`applyEnvironmentalDisplaySettings()` (boids, storm clouds, …): census each
lazy construction site for the SAME missing wiring (cache, protect list from
T463, eviction guards registered at init) and fix all of them through ONE
construction path so init-time and lazy creation cannot diverge again. Pre-fix
failing test: toggle the setting on after init on an EDT_NULL device with the
real cache and pack; the manager is wired and nothing exits (death test for the
pre-fix exit is acceptable as the red).

## B560 — second character in the same session inherits the first one's state

T464: with the Chase default moved to the identity load, window open states and
user-camera settings still carry over from character A to character B in one
process (camp → character select → B) when B has no ini or an ini missing
those keys. Establish from `FrameComponent::syncUiPreferences()` /
`setUiPreferencesIdentity()` and `ui_preferences.cpp` exactly which fields
carry over. Rule (docs/decisions.md 2026-09-07; T432): an ABSENT key keeps the
DEFAULT, i.e. `defaults.ini`'s value — not the previous character's. Fix: on an
identity change, reset every persisted field to its default before reading the
new file. Pre-fix failing test: A opens windows + sets User 1 rig values; switch
identity to B with no file → B has defaults and writes nothing.

## Proof

Standard verification bar (EXECUTOR_PROTOCOL): background builds with exit
files, `ctest -j6`, G-batch goldens for every TU touched
(`renderer_frame_render` is pinned), visual gate (no golden moves), ARM, file
sizes. Board B590–B599; strike B550/B560 with evidence.

File territory: `src/client/graphics/overlays/`, environment manager
construction, `src/client/graphics/frame/renderer_frame_preferences.cpp`,
`src/client/graphics/ui/ui_preferences.*`, tests. **Not**
`include/client/graphics/player/camera_rig.h`, camera tests,
`config/hotkeys.json`/`hotkey_manager.cpp` (T466, slot1), **not** the
HeadlessShutdownTest and its fixture (T465, slot2).

STOP: B560's fix requires changing what `defaults.ini` or the ini format means
(report); an image golden moves.

## Review

Slot3, `task/T467-tumbleweed-wiring-ui-identity`, base `bc99fb55`. No lock, no
Pi, no server contact. **STOP conditions:** none hit. `defaults.ini` and the
ini format mean what they meant, and no image golden moved.

### B550: confirmed in code, refuted as a live path, fixed

- **Confirmed** with a red test that goes through the renderer's own call.
  `tests/test_environment_manager_construction.cpp` binds a bare
  `IrrlichtRenderer`'s `RendererContext` to an EDT_NULL device and the real
  `ConstrainedTextureCache`. It then calls
  `OverlaysComponent::applyEnvironmentalDisplaySettings()` with the renderer's
  default `DisplaySettings`, where every toggle is on and no manager exists
  yet, i.e. the lazy branch. The unfixed binary **exited 1** inside that call
  and printed no gtest verdict. The log line was
  `[FATAL] [GRAPHICS] TumbleweedManager: Constrained texture cache not set`,
  from `TumbleweedManager::init()`.
- **Not reachable in a shipped build** (code evidence):
  - `IrrlichtRenderer::cachedDisplaySettings_` has exactly one writer. It is
    the `displaySettingsCached_`-guarded load in
    `IrrlichtRenderer::initLoadingScreen()`.
  - The same function then builds the tumbleweed manager from that same value,
    with the cache.
  - The only `tumbleweedManager_.reset()` is in `IrrlichtRenderer::shutdown()`.
  - `initLoadingScreen()` returns false before the build only on device or
    pack failure, and `BridgeComponent` then abandons graphics init.
  - So `rollingObjects` at every Step 11 equals its value at init, and the
    lazy branch never runs.

**Census of lazy construction sites.** Each row compares the init-time
construction with the lazy one.

| manager | init site | lazy site | divergence before T467 |
|---|---|---|---|
| `TumbleweedManager` | `initLoadingScreen()`: cache + `init()` | `applyEnvironmentalDisplaySettings()`: `init()` only | **missing `setConstrainedTextureCache()`**, which means FATAL exit. T463's `protectTexture("tumbleweed")` lives inside `init()`, so it came along once the cache did |
| `BoidsManager` | cache + `init(eqClientPath)` + `ensureBoidsTextureGuard()` | same, cache null-checked | none. The lazy site adds `onZoneEnter()` for an already-loaded zone |
| `Detail::DetailManager` | surface-map path + cache (unconditional at init) | same | none |
| `WeatherEffectsController` | cache + `initialize()` + `weatherSystem->addListener()` | same, gated on `particleManager` | none. Storm clouds (T463) live inside `initialize()` |
| `ParticleManager` | init only | none | n/a |
| `AnimatedTextureManager` | `renderer_zone_load.cpp` Step 4 and `renderer_zone_draw.cpp` | n/a (two per-zone sites) | both pass the cache |
| `DoorManager` | `loadGlobalAssets()`: cache, BSP, PVS, frustum, shaders + `ensureDoorTextureGuard()` | `setupInstantScene()`: nothing; `loadZoneStep7_Doors()`: all but the guard | **`loadGlobalAssets()` has no caller** in `src/`, `include/`, `tests/`, `tools/`, `benchmarks/` or `docker/`, so the T124 door guard is never registered in production. Outside this plan's territory, boarded **B590** |

**Fix.** `src/client/graphics/overlays/overlays_env_managers.cpp` is new and
holds `OverlaysComponent::ensureBoidsManager()`, `ensureTumbleweedManager()`,
`ensureDetailManager()` and `ensureWeatherEffects()`.
- Each is idempotent and does all the construction-time wiring: the cache,
  `init()`/`initialize()`, the boids eviction guard and the weather listener.
- `initLoadingScreen()` and `applyEnvironmentalDisplaySettings()` now call
  those bodies and construct nothing themselves.
- The lazy site keeps only what depends on its moment, the `onZoneEnter()`
  for a loaded zone.
- `EnvironmentManagerConstructionSource.EachEnvironmentManagerHasOneConstructionSite`
  fails if any of the four is constructed anywhere else under `src/`. It was
  red before the fix: two sites for the tumbleweed manager,
  `overlays_hud_env.cpp:367` and `renderer_init.cpp:677`.

### B560: confirmed in `FrameComponent`, refuted as a live path, fixed

- **Confirmed** by red tests in `tests/test_ui_preferences_identity.cpp`,
  which use T464's bare-renderer seam and a private `WILLEQ_CACHE_DIR`.
- **Which fields carried over** (measured, unfixed). After Alpha changed
  everything and the identity switched to Beta, which has no file, Beta held
  Alpha's values for:
  - skills, combat-ability and Options windows open, and group, pet and buff
    closed. That covers all seven toggleable windows plus `[OptionsWindow]`;
  - the three `[Options]` sliders, as 3/4/17;
  - all eight `User1*`/`User2*` rig values.
- **Not carried over:** the camera MODE, which T464 had already reset.
- A Beta file that stated only `InventoryWindow` and `Music` kept Alpha's
  values for every other key.
- **Cause:** `FrameComponent::syncUiPreferences()` seeded the parse from
  `collect()`, i.e. from whatever the client held.
- **Not reachable today** (code evidence):
  - One process logs in one character. `ApplicationConfig::character` is
    passed once to the single `EverQuest` that
    `Application::initEverQuestClient()` builds.
  - `/camp` ends the process:
    `PlayerComponent::UpdateCampTimer()` -> `SendLogout()`, then
    `PlayerComponent::ZoneProcessLogoutReply()` -> `RequestQuit()`.
  - So there is no "camp to character select, then B" path in-process, and the
    board's confirmation step cannot be performed.
- **Fix.**
  - `FrameComponent::setUiPreferencesIdentity()` arms `uiPrefsResetPending_` on
    a change FROM a non-empty identity.
  - On the next load, `syncUiPreferences()` applies `UiPreferences{}` and the
    default rigs before it reads the new file. It then pushes the three Options
    settings when a file loaded OR a reset happened.
  - The reset runs at the sync rather than in the setter because the
    clip-plane apply is a renderer call that does nothing while a zone is
    loading.

### Tests, red before and green after

| test | unfixed tree | fixed tree |
|---|---|---|
| `EnvironmentManagerConstructionTest.TurningRollingObjectsOnAfterInitWiresTheTumbleweedManager` | **process exit 1** (FATAL above) | OK |
| `EnvironmentManagerConstructionTest.TurningAmbientCreaturesOnAfterInitWiresTheBoidsManager` | not reachable (the same call exits first) | OK |
| `EnvironmentManagerConstructionTest.ApplyingTheSettingsTwiceBuildsEachManagerOnce` | not reachable | OK |
| `EnvironmentManagerConstructionSource.EachEnvironmentManagerHasOneConstructionSite` | **FAILED** (2 sites) | OK |
| `UiPreferencesIdentityTest.ASecondCharacterWithNoFileGetsTheDefaults` | **FAILED** | OK |
| `UiPreferencesIdentityTest.AKeyTheSecondCharactersFileOmitsKeepsItsDefault` | **FAILED** | OK |
| `UiPreferencesIdentityTest.ReturningToTheFirstCharacterRestoresItsFile` | OK | OK |
| `UiPreferencesIdentityTest.AZoneLoadForTheSameCharacterKeepsItsSessionState` | OK | OK |
| `UiPreferencesIdentityTest.TheFirstCharacterOfARunKeepsWhatTheClientAlreadyHolds` | OK (pins unchanged behaviour) | OK |

### Verification

| step | result |
|---|---|
| 1 `cmake --build build -j24` | **PASS**, exit 0 (400 s, exit file removed at launch; no exit 143) |
| 2 `ctest -j6` | 4,803 tests: **4,801 passed, 2 failed, both mine and both fixed.** `DataPathRouting.TheWholeClientTreeIsRouted`: the `"data/detail/zones"` knob moved into `overlays_env_managers.cpp`, so its two per-file waivers in `tests/test_data_path_routing.cpp` became one. `DocCitations`: a board path was missing its `src/client/graphics/` prefix. After the fixes, the rebuilt `test_data_path_routing` ran 8/8 and `DocCitations` passed, re-run standalone. The full suite was not re-run after those two edits, which touched only that test TU and `BOARD.md` |
| G-batch goldens | **PASS**. No touched TU is golden-pinned (`renderer_frame_render` pins `renderer_frame.cpp`; this unit edited `renderer_frame_preferences.cpp`). All `FrameRenderOrder`, `MovementOrder`, `ApplicationInitOrderTest`, `CommandRegistrationOrderTest`, `ZoneDispatchTable` and `SelfDeathSequence` golden cases passed in the suite |
| 3 `build-gles2-x86.sh` | **PASS**, exit 0 |
| 4 visual gate | **PASS** (`visual_gate` in the `-j6` suite, 28 s, against the rebuilt `build-gles2`); `tests/goldens/` untouched |
| 5 `build-arm-noble.sh` | **PASS**, exit 0 |
| 6 `check_file_sizes.sh fable` | **PASS**. 6 ok, 0 warn, 0 fail on tracked files, plus `--files` on the three new ones: 3 ok. Largest touched file: `renderer_init.cpp` at 794 lines |

### Board

- **B550** and **B560** are struck, with the evidence above.
- **B590** is new (B, lead): the door guard's only registration is in the
  zero-caller `loadGlobalAssets()`, with the stale ParticleManager comment
  noted.
- B591–B599 are unused.

### Deviations (flagged)

1. **Both items are refuted as live defects** and were fixed as contract
   defects. Neither path is reachable in a shipped build (evidence above), so
   both were class B in effect, not A. They were fixed because the plan asks
   for it and because the fixes are cheap and pinned.
2. **B560's reset is narrower than the plan's wording** ("on an identity
   change, reset"). The FIRST identity of a run is not reset. Before it, the
   client already holds real values: `applyVolumesChanged()` writes the audio
   manager's volumes into the Options sliders. Resetting there would put
   default slider values over the real volumes on the only reachable path.
   `TheFirstCharacterOfARunKeepsWhatTheClientAlreadyHolds` pins this.
3. **`fable` moved during the unit.** T465 merged as `9d646a5a`, so
   `check_file_sizes.sh fable` also listed `tests/test_headless_shutdown.cpp`.
   That is T465's change seen against the moved ref; this branch never
   touched the file, and `git status` shows it unmodified.
4. **Files outside the named territory**, each required by the change:
   - `CMakeLists.txt` (the new TU);
   - `tests/test_data_path_routing.cpp` (the waiver moved with the literal);
   - `include/client/graphics/frame/frame_component.h` (the new member);
   - `tests/cmake/renderer_guards.cmake`: one block beside T463's, far from
     T466's camera-test entries in the same file;
   - `tests/cmake/ui_rendering.cmake`;
   - docs: `CLAUDE.md`, `include/client/graphics/ui/CLAUDE.md`,
     `docs/module_map.md`.

   Nothing in `camera_rig.h`, the camera tests, `config/hotkeys.json`,
   `hotkey_manager.cpp` or the HeadlessShutdownTest was touched.
5. **Log text changed.** The four managers now log one message each whichever
   caller built them. For example, "Tumbleweed manager initialized (rolling
   objects enabled in settings)" is now "Tumbleweed manager initialized". The
   only file in the tree quoting the old strings is the historical
   `benchmarks/audits/T32/transcript_race415_graphics.txt`.
6. **`build/` was regenerated** against its existing cache, because gmake did
   not know the two new targets (T463 deviation 3). No `-D` flag was passed:
   `CMAKE_BUILD_TYPE=RelWithDebInfo` and the slot's own
   `EQT_GLES2_IRRLICHT_DIR` were confirmed at checkout.
7. **No live proof**, since no lock was assigned. For B550 and B560 there is
   also no live path to prove.
