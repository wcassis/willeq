# B977 — the `audio` ledger budget: measure on the Orange Pi (classic zones, sound ON), then ratchet

Issue #295. Branch `task/B977-audio-budget` from `gitlab/fable` @ `d3375b65`.
Will, in chat, 2026-10-01: **"4) perform the test run"** — the run `audio` was
held for on 2026-09-30 (**"lower the seven budgets, hold audio"**, B943).

## STATE AT HAND-OFF 2026-10-01

Handed off mid-unit on the coordinator's order (owner near his usage limit).
**No Pi session and no eqemu session was started: neither lock was taken, no
stack was brought up, no snapshot exists to restore.** Nothing has been
measured live by this unit yet. The branch carries a WIP commit with an
attribution fix, its regression test and a rig mode; it has NOT been built
(see "Build blocker").

### DONE (with evidence)

1. **Audio is ON on the Pi rig.** `scripts/pi_rendered_zone_sweep.sh` never
   passes `--no-audio`, and `WILLEQ_BASELINE=1` does not touch audio
   (`grep -rn Baseline src/client/audio` is empty). The B854 sweep's own logs
   show the backend up: e.g.
   `review/B854-rendered/sweep-full/zones/skyfire/client.log`:
   `Miniaudio backend initialized (22050Hz, stereo)`, `MidiPlayer: loaded GM
   SoundFont: .../synthusr.sf2`, `custom font deferred: .../synthus2.sf2`,
   `Preloaded 17/17 common sounds`, and `audio=1613736/2000912` at `startup`.

2. **FINDING — the `audio` row under-counts: it only sees what exists at
   startup.** Only `AudioManager::initialize()`, `ZoneAudioManager::loadZone()`
   and the RDP pump thread opened `MemoryScope(Audio)`. Everything loaded later
   is charged to the CALLER's scope, i.e. the game thread's `game_state`:
   - the custom SoundFont `synthus2.sf2` (12,036,844 B on disk; tsf holds float
     samples, ~24 MB). T504 deferred it from init to the first
     `selectSoundFont(Custom)`, which `AudioManager::playMusic()` makes for the
     31 Velious/Luclin XMI stems in `getSoundFontTypeForFile()`.
     **Pi evidence (B854, velketor,
     `review/B854-rendered/sweep-full/zones/velketor/client.log`):**
     `audio` = 1,625,664 B at `zone_load_complete`, `ready` AND `steady_60s`;
     the log shows `MidiPlayer: loaded custom SoundFont on demand` 1.6 s after
     `ready`; `game_state` went 4,850,304 (ready) -> 29,661,808 (steady_60s).
     Same load in `templeveeshan_retry`. No classic zone loads it (their XMI
     stems are not on the synthus2 list), so classic zones play on the GM font
     (`synthusr.sf2`, 498,320 B).
   - every WAV decoded on first play (`loadSound()` -> `bufferCache_`) and
     `SfxManager::preload()`'s second float copy of the same samples.
   **So the B854 "audio <= 1,862,784 B" figures are startup-only, and the
   Velious music zones' real audio is ~1.6 MB + ~24 MB.**

3. **The T496 25,725,824 B figure explained.** At T496 (2026-09-17, before
   T504) `MidiPlayer::init()` loaded BOTH fonts at startup under the Audio
   scope: ~24 MB synthus2 (float) + the rest (~1.6 MB) = 25.7 MB, in every zone
   (`review/T496/pi/raw/qeynos2_T496legD/ledger.txt`: `audio=25725824` from
   `zone_change_before_unload` on). T504 made the custom font on-demand, so the
   startup row fell to ~1.6 MB, and from then on the font (when a Velious/Luclin
   zone plays it) was charged to `game_state` — finding 2.

4. **Code on the branch (WIP commit, unbuilt):**
   - `src/client/audio/audio_manager.cpp`: `MemoryScope(Audio)` at every public
     entry point that can allocate (`playSound`, `playSoundByName`,
     `playSoundInternal`, `playMusic`, `stopMusic`, `onZoneChange`,
     `restartZoneMusic`, auto-attack / vendor-bank music start/stop,
     `loadSound`, `preloadCommonSounds`, `enableLoopbackMode`,
     `setAudioOutputCallback`), plus `au.*` owner labels
     (`eqt::memory::StartupScope`, reported by `WILLEQ_LEDGER_OWNERS=audio`):
     `au.mixer_sfx`, `au.soundfont_gm`, `au.music`, `au.backend`,
     `au.pfs_index`, `au.sound_buffers`, `au.sfx_cache`, `au.soundfont_custom`.
   - `src/client/audio/zone_audio_manager.cpp`: Audio scope in `update()` and
     `setDayNight()`; `au.zone_emitters` label in `loadZone()`.
   - `tests/test_audio_ledger_attribution.cpp` (new, registered in
     `tests/cmake/audio.cmake`): two cases that open a GameState scope, call
     `playMusic(velketor.xmi)` / `playSoundByName(...)`, and assert the bytes
     land in `audio`, not `game_state`. **Not yet run red-before / green-after.**
   - `tests/cmake/audio.cmake`: `AUDIO_COMMON_TEST_SOURCES` now links
     `memory_ledger.cpp`, `memory_ledger_trace.cpp`, `gpu_buffer_census.cpp`
     (StartupScope is not header-only; every AudioManager test binary needs the
     symbol), and `test_audio_startup_peak` drops its now-duplicate copies.
   - `scripts/client_ledger_capture.sh`: new mode `audio` (x86 owner rows).

5. **Defect to file (not yet filed):** every played WAV is held TWICE as
   32-bit float — `AudioManager::bufferCache_` and `SfxManager::cache_`
   (`preload()` copies the samples), each capped separately at the preset's
   `soundCache` (8,192 KB on orangepi), so up to 16 MiB of duplicated decoded
   PCM. The custom SoundFont at ~24 MB float for a 12 MB file is the other
   1999-implausible owner (the original client did not decode its font to
   float). Both make any ratchet value provisional.

### REMAINS (numbered checklist)

1. **Build blocker.** The worktree's `third_party/irrlicht-gles2-x86` was
   copied from `main/`, whose Irrlicht predates B959: the link fails with
   `undefined reference to eqtSetShaderBinaryCacheDir(char const*)`
   (`gles2_alpha_test_probe`, `gles2_player_light_probe`). Rebuild the slot's
   own Irrlicht first (needs downloads.sourceforge.net — see B970 #288 if it is
   still down):
   `bash /mnt/projects/fable/wt/audit-audio/scripts/build-gles2-x86.sh --irrlicht-only`
   then
   `cmake --build /mnt/projects/fable/wt/audit-audio/build -j24 > <log> 2>&1; echo EXIT=$?`
   (background + `scripts/wait_for.sh --exit-file`; the tree is configured
   RelWithDebInfo against the slot's own Irrlicht dir).
2. **Red/green the attribution test.** On this branch:
   `ctest --test-dir /mnt/projects/fable/wt/audit-audio/build -R AudioLedgerAttribution --output-on-failure`
   must pass; with the two `src/client/audio/*.cpp` hunks reverted
   (`git diff ... > x.patch; git apply -R`; never `git stash`) the
   `CustomSoundFontLoadedByPlayMusicIsChargedToAudio` case must fail with
   `game_state` +~24 MB. Record both numbers.
3. **ARM build of the branch:** push, then
   `glab ci trigger build-arm -b task/B977-audio-budget -R https://gitlab.goldfarm.local/willeq/willeq`
   -> `/mnt/projects/fable/ci-arm/<sha>/` (must contain `bin/willeq-nollvm`
   and `mesa/prefix/.willeq-mesa-pin`).
4. **Pi run (locks: the rig takes eqemu `local` then the Pi, releases both,
   restores the snapshot with the world down):** a tour file with
   `/loc`, `wait 75`, `/say #npctypespawn 351043`, `wait 2`, `/target`,
   `wait 1`, `/attack`, `wait 10`, `/stopattack`, `wait 20`, `/loc`, `/q`, then
   ```
   EQEMU_HOST=local bash scripts/pi_rendered_zone_sweep.sh \
     --pi-dir /mnt/projects/fable/ci-arm/<sha> --owner B977 \
     --zones qeynos2,qeynos,freportw,gfaydark,befallen,halas,erudnint,rivervale,neriaka,akanon,velketor,thurgadina,kael \
     --tour <tour> --drive-keys --stop-point never \
     --env WILLEQ_LEDGER_OWNERS=audio --env WILLEQ_LEDGER_IDLE_EVERY=15 \
     --out /mnt/projects/fable/review/B977/pi
   ```
   (the three Velious zones are there because they are the only ones that load
   the custom font — they are the real worst; kael may need `--char-level`).
   Per zone read `audio=<live>/<peak>` at `zone_load_complete`, `ready`,
   `steady_60s`, the `idle_*` points after the combat, and the
   `[memledger-startup] ... owner=startup.au.*` rows. Confirm in each
   `client.log`: `Miniaudio backend initialized`, the zone's music started
   (`MidiPlayer`/music lines; for velketor `loaded custom SoundFont on
   demand`), and `audio` non-zero.
5. **x86 client, same zones:**
   `EQEMU_HOST=local bash scripts/client_ledger_capture.sh --zones <same> --modes audio --owner B977 --tour <tour> --out /mnt/projects/fable/review/B977/x86`
   (needs `build-gles2/bin/willeq` built from this branch:
   `bash scripts/build-gles2-x86.sh`, AFTER step 1's build finishes). Benchmark
   census column: 0 — `gles2_zone_benchmark` never constructs `AudioManager`.
6. **Table + ratchet** (`refactor/B943/README.md` is the model): per zone
   audio at ready / steady / peak, Pi and x86, owner rows of the worst. Worst x
   1.25, rounded UP to a whole MiB. If that is above 32 MiB (likely, if the
   velketor figure is ~26 MB with the font now charged to `audio`: 26e6 x 1.25
   = 32.5e6 B = 31.0 MiB -> 31 MiB; above ~26.8 MB it rounds to 33 MiB = a
   raise, which the ratchet rule forbids — then HOLD 32 MiB and say the
   soundfont defect is what has to move). Files to change, exactly as B943:
   `config/constrained_presets.json` (`memoryLedgerBudgets.audio`),
   `src/client/graphics/constrained_renderer_config.cpp` (`fromPreset(OrangePi)`
   + derivation comment), `include/client/graphics/constrained_renderer_config.h`
   (field comment), `tests/test_constrained_renderer.cpp` (compiled-preset and
   shipped-JSON `audio` pins), `docs/decisions.md` (2026-10-01 entry quoting
   "4) perform the test run" and "lower the seven budgets, hold audio"),
   `refactor/BOARD.md`, this README ("Not verified").
7. **Gate:** the benchmark's `audio` row is 0, so the five gate zones pass any
   audio budget trivially; the CLIENT figure is what the budget bounds and the
   gate does not see it (B923 #242). Run `visual_gate.py` once on the branch
   anyway (`docs/ci.md`).
8. **File the defects** (next free B number = max `B###:` GitLab title + 1;
   re-check before filing): (a) duplicated float SFX caches; (b) the custom
   SoundFont held as ~24 MB of float samples. Name them in the README and the
   MR.
9. Full suite + hygiene (`ctest -L hygiene`) + `check_file_sizes.sh fable`
   from the slot; commit (trailer
   `Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>`); push; take the
   MR out of draft with `Closes #295`; issue label `status::review`. Do not
   merge.

### Locks and stacks touched

None. Pi lock and the eqemu `local` lock were read FREE at ~11:04 UTC and
never taken; no stack was brought up; no character row was changed.
