# T370 — the zone tour under TSAN (B064)

Class B, slot1, eqemu lock (atomic; must be ABSENT — if present read it
and STOP). Measurement first; fix only what TSAN names. Read the B064
board text and `review/T352/README.md` (the TSAN build flags, the
two-thread driver, the positive control, and why the T121 tour was NOT
run there — it needs the server, which you now have).

1. Build the TSAN tree from T352's recipe (`review/T352/build_and_run.sh`
   shows the flags; T121's tour script is named in T352's Review). Bring
   the server up.
2. Run the T121 zone tour (login, six `#zone` commands) under TSAN with
   the EntityPrepWorker live — the run T352 said would confirm or clear
   B064's four containers (`globalTextures_`, `numberedGlobalTextures_`,
   `armorTextureCache_`, `zoneTextures_`) read in `getMergedTextures`
   while other loader paths mutate them. Three runs. Save every TSAN
   report verbatim in `review/T370/`.
3. For each distinct report: both stacks, the container, the two threads,
   and whether the T352 positive control still fires in this build (so a
   clean run is a finding and not a silent harness).
4. Fix: only what a report names, with the narrowest change that is
   deadlock-free by the same argument T352 made (no new lock ordering
   edge; the 12 call sites; nothing inside acquires a loader mutex). If a
   report needs the restructure B064 anticipates (the texture containers
   own by a different thread than the chr cache), STOP with the reports
   and the shape — do not add a lock that widens `otherChrCacheMutex_`
   over unrelated containers by argument.
5. Post-fix: three clean tours; the positive control still fires.
6. Strike B064 (or annotate it with the measured reports and the STOP).

## Proof

The TSAN runs are the proof; plus build, `ctest -j6`, visual gate
standalone + ARM if `src/client/graphics` changes, file sizes. Restore
server values; `down.sh`; delete the lock.

STOP: the TSAN tree no longer builds (report the first error — do not
fix the flags by guesswork); a report needs the restructure.

## Review

Branch `task/T370-tsan-zone-tour`, base `fable` tip `95b2e355`, slot1.
All artefacts in `/mnt/projects/fable/review/T370/`.

### Headline

**B064 is NOT a live race — CLOSED by measurement, no restructure.** The
T120/T121 amplified zone tour was run three times against a live eqemu under
ThreadSanitizer with the EntityPrepWorker doing real background prep, and
ThreadSanitizer reported **zero** races on `globalTextures_`,
`numberedGlobalTextures_`, `armorTextureCache_` or `zoneTextures_` — or on
anything else in `RaceModelLoader`, or anywhere in `src/client/graphics`.

The clean result is a finding rather than a silent harness because the same
three runs each reported **one** race, in the same binary, with byte-identical
stacks: `MixChannel::volume` written by the game thread and read by the
miniaudio audio thread. That is a genuine class-A defect the tour found while
looking for something else; it is fixed here and boarded as **B125**.

### The tour, and one deviation from the recipe

`review/T370/tour_zone_cross_tsan.txt` is T120's
`tour_zone_cross_x86_amplified.txt` with the **same command sequence,
unchanged** (login, `#set time 11 0`, `/pmem`, then six `#zone` crossings
Befallen<->Qeynos2 with a `/move` between each, then `/camp`) and every `wait`
after a `#zone` raised from **25 s to 150 s**.

**Deviation, flagged not absorbed.** The first attempt used T120's timings
verbatim and **stalled at script step 14 of 43**: under TSAN the client's own
log reports `Progressive loading complete: 57828ms total streaming time` for a
single zone, so the second `#zone` was issued while the first was still
loading and the command was held (`CMD outcome=held registry=pending
recognized=false message="waiting for connection"`). Kept as
`review/T370/aborted_run_short_waits/` rather than discarded. With 150 s waits
all six runs below reached script step 42 of 43 (`/camp`).

`review/T370/run_tsan_tour.sh` is the runner (TSAN flags, suppression file,
`setarch -R`, log_path); `review/T370/x86_config.json` is T120's config
verbatim.

### Pre-fix: three runs

| run | script steps | EntityPrepWorker jobs completed | exit | distinct TSAN reports |
|---|---|---|---|---|
| 1 (`run1/`) | 42/43 | 112 | 66 | 1 |
| 2 (`run2/`) | 42/43 | 79 | 66 | 1 |
| 3 (`run3/`) | 42/43 | 107 | 66 | 1 |

Exit 66 is ThreadSanitizer's `exitcode` on a report, so the exit status alone
classifies a run. `ThreadSanitizer: reported 1 warnings` in each report file;
the three are identical down to the racing address `0x726800009a84` and the
build id.

**The one report — container, both stacks, both threads** (verbatim head of
`run1/tsan_report.1449301`; the other two differ only in pid):

```
WARNING: ThreadSanitizer: data race (pid=1449301)
  Write of size 4 at 0x726800009a84 by thread T72:
    #0 EQT::Audio::MusicPlayer::setVolume(float)          music_player.cpp:213
    #1 EQT::Audio::AudioManager::playMusic(...)           audio_manager.cpp:352
    #2 EQT::Audio::ZoneAudioManager::update(...)          zone_audio_manager.cpp:99
    #3 eqt::bridge::BridgeComponent::UpdateAudioListener  bridge_component_ui_audio.cpp:650
    #4 eqt::bridge::BridgeComponent::PostRenderTick       bridge_component.cpp:326
    #5 EverQuest::PostRenderTick(float)                   everquest.cpp:991
    #6 eqt::Application::updateGameState(float)           application_frame.cpp:115
    #7 eqt::Application::gameThreadLoop()                 application.cpp:945     [GAME THREAD]

  Previous read of size 4 at 0x726800009a84 by thread T71:
    #0 EQT::Audio::AudioMixer::render(float*, int)        audio_mixer.cpp:50
    #1 miniaudioDataCallback                              audio_backend.cpp:98
    #2..#8 ma_device__on_data ... ma_thread_entry_proxy   miniaudio.h            [AUDIO THREAD]

  Location is heap block of size 1392 at 0x726800009600 allocated by main thread:
    #1 std::make_unique<EQT::Audio::AudioMixer>()
    #2 EQT::Audio::AudioManager::initialize(...)          audio_manager.cpp:53
```

- **container**: `AudioMixer::channels_[]`, field `MixChannel::volume`
  (a plain `float`; the struct's `active` flag is already `std::atomic<bool>`,
  `volume` and `pan` were not).
- **threads**: T72 = the game thread created at `Application::run()`
  (`application.cpp:792`); T71 = miniaudio's own worker thread.
- **why every run**: `ZoneAudioManager::update` restarts the zone music on
  every zone change, so a six-crossing tour hits `setVolume` six times against
  a continuously-running audio callback.

**Zero reports naming `RaceModelLoader`, `mesh_building.cpp`, or any of B064's
four containers, in any of the three runs.**

### Was the window actually open? (a null result needs this)

Yes. `EntityPrepWorker: background pool-backed started` appears once per zone
load in every run, and the worker completed **112 / 79 / 107** prep jobs across
the six crossings — `processRequest()` -> `preloadModelData()` /
`preloadVariantModel()`, the exact background path T352's driver simulated,
running while the render thread built entity meshes through
`ensureModelDataLoaded()` -> `getMergedTextures()`. The tour reached the
concurrent window many times and TSAN saw no collision on the four containers.

Code-level, the B064 observation itself is accurate and stands: `loadGlobalModels`
(`model_loading.cpp:46`, writes `globalTextures_`), `loadZoneModels`
(`:156`/`:199`, clears and writes `zoneTextures_` **and**
`mergedTexturesCacheValid_`) and `getArmorTexture` (`:135`, writes
`armorTextureCache_`) all mutate outside `otherChrCacheMutex_`. What the
measurement adds is that on every path the tour reached, those mutations ran on
the same thread as the reader: `loadGlobalModels` early-outs on
`globalModelsLoaded_` after the main thread's first call, and `loadZoneModels`
runs during zone init while the previous zone's worker has already been
`stop()`ped and `reset()` by `unloadZone()`. **No lock was added and no
restructure was performed** — adding one on this evidence would be exactly the
"widen a mutex by argument" the plan forbids.

### Positive control — fires before and after, in and out of process

| control | pre-fix | post-fix |
|---|---|---|
| standalone `pc.cpp` (deliberate two-thread race, same TSAN flags, same `TSAN_OPTIONS`, same suppression file, same `setarch -R`) | fires (`poscontrol_pre.1435175`) | fires, exit 66 (`poscontrol_post.1694210`) |
| **in-process**, the willeq TSAN binary itself under the tour | fires 3/3 (the B125 race above) | n/a — that race is fixed |
| **in-process**, the POST-FIX willeq binary, same tour harness, suppression file emptied | n/a | **fires: 2 libgallium races reported** (`poscontrol_postfix/tsan_report.1694122`) |

The third row is the one that matters: it proves ThreadSanitizer is still live
*inside the post-fix binary during a real zone-loading session*, so the three
clean post-fix tours are silence with nothing to report, not a dead detector.
Those two reports are the Mesa/llvmpipe races the shipped suppression file
excludes by library — `race:libgallium*.so*`, copied verbatim from T114/T352,
**unchanged, with no willeq symbol suppressed anywhere and no new entry added**.

### The fix (B125), and its deadlock argument

`include/client/audio/audio_mixer.h`, two fields:

```
    std::atomic<float> volume{1.0f};
    std::atomic<float> pan{0.0f};
```

1. **No lock is taken**, so no lock-ordering edge can be created and
   `AudioMixer::render()` — documented "must be lock-free" — stays lock-free.
   This is strictly stronger than T352's argument, which had to enumerate 12
   call sites to show one mutex was safe.
2. Every touch site is a plain assignment or an implicit load and needs no
   edit: writers `music_player.cpp:213,268,414,415`, `sfx_manager.cpp:64,65`,
   `weather_audio.cpp:143,147,205,209`, `audio_mixer.cpp:118,119`; readers
   `audio_mixer.cpp:50,56`. `atomic<float>`'s `operator=` / `operator float()`
   cover all of them.
3. `MixChannel` was already non-copyable (`std::atomic<bool> active`) and
   `channels_` is a fixed C array, so no container operation is affected;
   `resetChannel()` already exists precisely because the struct cannot be
   copy-assigned.
4. **Deviation, flagged**: TSAN named only `volume`. `pan` is the same plain
   float in the same struct, written by the same non-audio threads
   (`sfx_manager.cpp:65`, `music_player.cpp:415`) and read on the same line
   range of the same audio callback (`audio_mixer.cpp:56`). It is fixed by the
   same argument, not by a measurement, and the header comment says so.

### Post-fix: three runs

| run | script steps | EntityPrepWorker jobs completed | exit | TSAN reports |
|---|---|---|---|---|
| postfix 1 (`runpostfix1/`) | 42/43 | 73 | **0** | **none** |
| postfix 2 (`runpostfix2/`) | 42/43 | 98 | **0** | **none** |
| postfix 3 (`runpostfix3/`) | 42/43 | 95 | **0** | **none** |

No `tsan_report.*` file was created in any of the three; exit 0 rather than 66.

### Verification

| step | result |
|---|---|
| `cmake --build build-tsan --target willeq` | exit 0 (after an exit-143 sweep, below) |
| `cmake --build build -j12` (GLES2 test tree, slot's own Irrlicht) | exit 0 |
| `ctest --test-dir build -j6` | **3936 of 3937**, exit 8, sole failure `AudioManagerTest.PreloadCommonSounds` (the waived ALSA host quirk) |
| `scripts/build-gles2-x86.sh` | exit 0 |
| `scripts/ci/visual_gate.sh` (standalone, no other load) | **PASS**, goldens untouched. p95 CPU qeynos2 4.656 / gfaydark 5.453 / befallen 4.959 ms, all well under ceiling |
| `scripts/build-arm-noble.sh` | exit 0 |
| `scripts/refactor/check_file_sizes.sh fable` | 21 files checked, 21 ok, 0 warn, 0 fail |

`src/client/graphics` was **not** touched, so the gate and the ARM build were
not strictly required by this plan; both were run anyway because the change is
in a header linked into every binary.

**Exit-143 sweep (the T359/G03 trap, hit once).** The first
`cmake --build build-tsan` was killed at the 600 s ceiling *during
`Linking CXX static library lib/libwilleq_core.a`*. The sweep found the exact
recorded shape: a **3.0 GB `ar` temp file `build-tsan/lib/steyKq3P`** and a
2.0 GB `libwilleq_core.a` holding **137 members** where a complete archive holds
**322**. Both were deleted and the archive rebuilt; the rebuilt archive has 322
members and `build-tsan/bin/willeq` starts `7f 45 4c 46`. Note the archive was
present and non-empty — presence and size prove nothing, `ar t | wc -l` does.

Second, unrelated: `build-tsan/bin/willeq` **segfaults immediately without
`setarch -R`** (even for `--help`). Every run here goes through `setarch -R`,
as T352's recipe already did.

### Server

Lock `/mnt/projects/fable/.eqemu-lock` was absent, taken atomically with
`set -o noclobber` as `T370 2026-09-05`, and deleted at the end.

| value | as found (SELECT, before) | as left (SELECT, after) |
|---|---|---|
| `character_data` Testchar | `zone_id=2 x=-74 y=428.04 z=3.75 heading=160` | identical (`testchar_before.txt` / `testchar_restored.txt` diff clean) |

The tour left Testchar at `zone_id=36 x=35 y=-82 z=3 heading=0`
(`testchar_after_tours.txt`); it was restored by `UPDATE` and re-`SELECT`ed.
**Note the protocol's warning held again**: the as-found pose was *not* the
canonical `zone_id=36 …` line in `EXECUTOR_PROTOCOL.md`, it was T310/T311's
qeynos2 pose, and it was restored to what was found, not to the documented one.

Nothing else was written. `#set time 11 0` is in the inherited tour script and
moves the world clock (`eqtime`), which free-runs and was not snapshotted
before the first run — recorded here as a known, unrestored, self-advancing
value (`eqtime_after.txt`), not silently absorbed. `scripts/eqemu/up.sh
--launcher` reported `Launcher process already running` (the fleet had been up
since 01:28 under no lock); `down.sh` was run at the end regardless.

### Files in `review/T370/`

| file | what |
|---|---|
| `run_tsan_tour.sh`, `tour_zone_cross_tsan.txt`, `x86_config.json` | the runner, the tour, the client config |
| `run1/`, `run2/`, `run3/` | pre-fix: full client log + the verbatim `tsan_report.*` |
| `runpostfix1/`, `runpostfix2/`, `runpostfix3/` | post-fix: full client log, no report file |
| `aborted_run_short_waits/` | the T120-timed attempt that stalled at step 14 |
| `pc.cpp`, `pc`, `poscontrol_pre.*`, `poscontrol_post.*` | the standalone positive control, before and after |
| `run_poscontrol_inproc.sh`, `tour_control_short.txt`, `tsan_suppressions_empty.txt`, `poscontrol_postfix/` | the in-process positive control on the post-fix binary |
| `tsan_suppressions.txt` | T114/T352's file, byte-identical, one library-only entry |
| `ctest.log`, `gate.log`, `arm.log`, `gles2.log`, `build_tsan*.log` | verification logs |
| `testchar_before.txt`, `testchar_after_tours.txt`, `testchar_restored.txt`, `eqtime_after.txt` | server values, by SELECT |
