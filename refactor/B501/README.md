# B501 + B500 + B499 (#176, #175, #174): zero-caller deletions — common/, game modes, audio

One card, three T455 census groups, three `Closes` (see MR). Class B
(unreached): each function below had **zero callers** tree-wide at the
baseline (src, include, tests, tools, benchmarks, docker, scripts — the same
sweep the ratchet runs). The red/green pair is the zero-caller ratchet
itself (`ctest` `ZeroCallers`, label `hygiene`): with the functions deleted
and the EXEMPT rows still present, `check_zero_callers.py --check` fails with
30 `STALE` rows; after the rows go it exits 0. Evidence files (committed
under `scripts/ref/kb-evidence/` in this branch; copies attached to the
kanban task):

- `RED_zero_callers.txt` — the failing run, `exit=1`, "zero-caller: 0
  unlisted, 30 stale" (30 = 11 B499 (12 listed − 1 kept seam) + 10 B500 +
  9 B501; `loadFromPCM` is classified "seam" in the script's
  `EXEMPT_TESTS_GROUPS`, not under B499, so it never had a B499 row).
- `GREEN_zero_callers.txt` — the green run, `exit=0`, "zero-caller census
  clean: 334 functions, all classified in EXEMPT (boarded 334)" (baseline
  364 − 30 = 334).
- `writenonly_after.txt` — `check_write_only_fields.py --check` `exit=0`,
  "write-only census clean: 45 fields, all 45 classified in EXEMPT" and
  "unused census clean: 86 fields, all 86 classified in EXEMPT_UNUSED".

The four KSM heap helpers the card lists (`MarkHeapStart()`,
`MeasureHeapUsage()`, `CheckPageAlignment()`, `PageAlignVectorAligned()`)
were **already deleted by B941 (#259)**, which removed `ksm.h` when its last
user, `RaycastMesh`, moved to plain vectors — nothing to do for them here.

## B499 — audio (issue #174; ratchet `11 rows deleted, 1 kept as a seam`)

| function | verdict | reason |
|---|---|---|
| `AudioManager::isAutoAttackMusicPlaying()` | **deleted** | zero callers; the state it read (`autoAttackMusicActive_`, `src/client/audio/audio_manager.cpp:478-551`) is read by live auto-attack music code and stays |
| `AudioManager::isVendorBankMusicPlaying()` | **deleted** | same member family; state stays |
| `AudioManager::isUse3DAudio()` | **deleted** | zero callers; `use3DAudio_` is still read at `src/client/audio/audio_manager.cpp:342` and `setUse3DAudio()` (a writer) stays |
| `AudioMixer::getSfxVolume()` | **deleted** | zero callers; `sfxVolume_` is read at `src/client/audio/audio_mixer.cpp:33` and stays |
| `EffLoader::getMp3Index()` | **deleted** | zero callers; the static `mp3Index_` it returned had no other user and goes too. `mp3IndexLoaded_` stays (still set/checked by the loader) |
| `SoundBuffer::loadFromPCM()` | **kept — live test seam** | called by `tests/test_sfx_cache_eviction.cpp:52`, which builds deterministic raw PCM to pin the shared-buffer cache's LRU mechanics; classified "seam" in `check_zero_callers.py` `EXEMPT_TESTS` ("SoundBuffer","loadFromPCM") |
| `WeatherAudio::getThunderTimer()` | **deleted** | zero callers; `thunderTimer_` is read by the live tick path (`src/client/audio/weather_audio.cpp:155`) and stays |
| `ZoneSoundEmitter::getXmiIndex1()` / `getXmiIndex2()` | **deleted** | zero callers; `getXmiIndex(bool isDay)` (`include/client/audio/zone_sound_emitter.h:77`) reads the same two members and has callers, so the members stay |
| `MusicPlayer::enableSoftwareRendering()` / `isSoftwareRendering()` / `reinitializeOpenAL()` | **deleted** | OpenAL-era residue — the client uses miniaudio; all three were no-ops with zero callers |

## B500 — the `IGameMode` accessors (issue #175; ratchet `10 rows deleted`)

| function | verdict | reason |
|---|---|---|
| `AutomatedMode::getGameState()` | **deleted** | zero callers; cascade `m_gameState` |
| `AutomatedMode::queueCommand()` / `getNextCommand()` / `hasPendingCommands()` | **deleted** | the whole command queue T410's scenario lane replaced with `setAutomationCallback`; cascade `m_commandQueue`, `m_commandMutex` and the queue's definitions in `automated_mode.cpp` |
| `AutomatedMode::setEventCallback()` | **deleted** | the event callback nobody registered (T410 §1 said exactly this); cascade `m_eventCallback`, `m_listenerHandles`, `handleEvent()` and the `EventCallback` alias |
| `HeadlessMode::consoleInput()` / `consoleRenderer()` | **deleted** | zero callers; the underlying `m_inputHandler` / `m_renderer` are used by the mode's own lifecycle and stay |
| `HeadlessMode::getGameState()` | **deleted** | cascade `m_gameState` |
| `GraphicalMode::getGameState()` | **deleted** | cascade `m_gameState` |
| `GraphicalMode::getRendererType()` | **deleted** | cascade `m_rendererType`. `createMode()`'s `rendererType` parameter and the `GraphicalRendererType` enum **stay**: `Application::initGameMode` still logs the requested type (`src/client/application.cpp:431-434`) |

B345's three write-only `ConsoleRenderer` still owned were not touched;
nothing reintroduced.

## B501 — common/ (issue #176; ratchet `9 rows deleted, 4 already gone via B941`)

| function | verdict | reason |
|---|---|---|
| `PerformanceMetrics::getTimings()` | **deleted** | zero callers |
| `PerformanceMetrics::resetGameplay()` | **deleted** | zero callers |
| `PerformanceMetrics::getElapsedMs()` | **deleted** | zero callers; `programStart_` stays (`generateReport()` uses it) |
| `PerformanceMetrics::markZoneLoadStart()` / `markZoneLoadEnd()` | **deleted** | the zone-load timing channel nothing marks; cascade `currentZoneName_`, `zoneLoadStart_` (the private `getCurrentZoneName()` accessor was their only consumer and is gone with them) |
| `getCategoryName()` (free function in `performance_metrics.h`) | **deleted** | zero callers |
| `MarkHeapStart()` / `MeasureHeapUsage()` / `CheckPageAlignment()` / `PageAlignVectorAligned()` | already deleted by B941 (#259) | `ksm.h` is gone from the tree |
| `ClampLower()` (`data_verification.h`) | **deleted** | zero callers; `Clamp()` and `ClampUpper()` stay (live) |
| `Random::Shuffle()` (`random.h`) | **deleted** | zero callers; the `<algorithm>` / `<iterator>` / `<type_traits>` includes it alone justified go too |
| `LogManager::GetMutex()` (`logging.h`) | **deleted** | zero callers; cascade `m_mutex` (its only referent) |

## What stays that a quick grep might flag

- `setUse3DAudio()` — a writer; the getter was what died.
- `getXmiIndex(bool)` — live (`zone_audio_manager.cpp` plays zone music through it).
- Every data member behind a deleted getter: `autoAttackMusicActive_`,
  `vendorBankMusicActive_`, `use3DAudio_`, `sfxVolume_`, `thunderTimer_`,
  `xmiIndex1_` / `xmiIndex2_`, `mp3IndexLoaded_`, `programStart_` — each is
  read by live code (file:line in the tables above).
- `createMode(mode, rendererType)` and `GraphicalRendererType` — the
  requested-type log line in `Application::initGameMode` still reads it.

## Not verified / left open

- CI (full build + ctest + visual gate) runs on the GitLab runner per the
  card; local verification is the two ratchets above + the targeted build
  (`willeq`, `test_sfx_cache_eviction`, `test_eff_loader`,
  `test_data_verification` — all green after the last edit).
- No live-server or Pi leg exists for this card (offline by design).
