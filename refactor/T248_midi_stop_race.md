# T248 — MidiPlayer::stop() races the miniaudio render callback (TSAN, T220/T245)

Slot: slot2. x86. NO eqemu (TSAN over the audio test binaries / a headless
run with music). Class A. Territory: src/client/audio/midi_player.*,
audio_backend.* (only the callback handshake), tests. Do NOT touch
combat/eq.cpp (T247), inventory (T249).

## Defect
TSAN: `MidiPlayer::stop()` (game/main thread) frees/resets TinySoundFont
voices while the miniaudio render callback is still reading them — 4
reports in T220's tour, 15 in T245's (`review/T245/tsan_after_*`). T130/
T131 fixed the callback-allocation class; this is the teardown side.

## Scope
1. Reproduce under TSAN with the smallest driver: the audio unit tests or
   a headless run that starts and stops zone music (grep tests/ for
   MidiPlayer). Record BEFORE.
2. Fix with the audio subsystem's own discipline (T130/T131 write-ups):
   stop must hand off to the callback — e.g. set an atomic "draining" flag
   the callback honours, then wait for the callback to acknowledge
   (one-frame barrier) before touching voices; or route stop through the
   same command queue the callback drains. No lock inside the callback.
3. AFTER: same driver, zero MidiPlayer/TSF reports. Existing audio tests
   green; the miniaudio log callback wiring (T128 board item — `pLog`
   never set) may be done here if it is ≤10 lines, since it is the only
   way to see underruns the fix might cause.

## Verification
Full ctest; Review; evidence review/T248/. Commit:
`T248: MidiPlayer::stop synchronises with the render callback`.

## Review (executor, slot2, 2026-08-26)

Branch `task/T248-midi-stop-race` off fable `f461aaf3`. Evidence in
`/mnt/projects/fable/review/T248/` (README there). No eqemu, no audio
device, no EQ data needed: the driver owns its render thread.

### Driver (scope 1)

`tests/test_midi_stop_race.cpp` (new, `test_midi_stop_race` target, links
only `midi_player.cpp` + `audio_mixer.cpp`). A test-owned thread calls
`AudioMixer::render()` back-to-back with 512-frame periods -- the exact call
`miniaudioDataCallback` makes -- with a music channel whose callback
delegates to `MidiPlayer::render()` as `MusicPlayer::mixerRenderCallback`
does. MIDI data is a generated in-memory format-0 SMF (16 channels of
note-on/off pairs, looped) so every pass runs `tsf_channel_note_on/off` +
`tsf_render_float`. The game-thread side loops 300x over the four paths the
T245 stacks showed: `play()` (stop + `tml_free` + `tsf_reset`), bare
`stop()`, `selectSoundFont()` (stop + active-pointer swap), `seekTo()`.

**BEFORE** (same driver, untouched sources, TSAN tree `slot2/build-tsan`):
**139 reports**, process crashed inside the first test (`tsan_before.exit`
= 66, `ThreadSanitizer:DEADLYSIGNAL`). Classes (`tsan_before_summary.txt`):
tsf.h voice-state races (`tsf_reset` x54, `tsf_voice_envelope_nextsegment`
x40, `tsf_voice_endquick` x8, ...), **5 `tml_free` races** (play() freeing
the message list the callback is walking), **2 heap-use-after-free** and
**1 SEGV** at `MidiPlayer::render` lines 273/301 (the callback reading a
freed `tml_message`), plus `midi_player.cpp` field races (`currentMessage`,
`midiTimeMs`, `looping_`). This is the T245 class reproduced without a
server, and it is worse than a data race: the live client can crash.

### Fix (scope 2) -- the handshake

`include/client/audio/render_barrier.h` (new, header-only `RenderBarrier`):
an epoch counter the render side bumps to ODD on entry (relaxed RMW +
seq_cst fence) and back to EVEN on exit (release). The teardown side
publishes its stop condition (an atomic store), issues a seq_cst fence and
reads the epoch: EVEN -> return at once (the two fences are the
store-buffering pair, so a pass that starts later is guaranteed to see the
stop store); ODD -> poll until the epoch changes (the exit's release pairs
with the acquire load, ordering all of that pass's reads before the
teardown's writes). That is the one-frame barrier: the wait is bounded by
the remainder of ONE render pass; the callback's cost is two relaxed RMWs
and one fence; no lock, no allocation, no syscall on the render side.
Re-entrant teardown from the render thread itself (MusicPlayer's fade-out
completion calls `MidiPlayer::stop()` inside the mixer callback) returns
immediately (thread-id check). Bounded (500 ms default) -- a timeout is
logged and the caller proceeds instead of hanging shutdown.

Applied in two places:
- `MidiPlayer::render()` holds the barrier for its whole body, with the
  `playing_` check AFTER `enter()`. `stop()` = `playing_=false`; wait;
  `tsf_reset`. Every mutating path (`play`, `selectSoundFont`,
  `loadSoundFont`, `~MidiPlayer`) already went through `stop()` first and
  inherits it. `seekTo()` called mid-play (not a production path -- play()
  calls it before `playing_=true` -- but public API, and the driver's first
  AFTER run proved it was still racy: 41 reports, all on the direct
  `seekTo` at test line 158) now does the same pause/wait/seek/resume
  itself. `Impl::midiTimeMs` became `std::atomic<double>` (relaxed) because
  `getPlaybackTimeMs()` reads it on the game thread for the zone-music
  resume position while render advances it.
- `AudioMixer::render()` holds its own barrier; `freeChannel()` now waits
  after `active=false`, so on return the callback is not inside that
  channel's `renderCallback` and will not re-enter it -- the mixer half of
  the same class (T245's `audio_mixer.cpp:36/48/54/59` reports were
  `MusicPlayer::startMixerPlayback` rewriting a channel the callback was
  reading; `startMixerPlayback` frees before it rewrites, so it is covered).
  `waitForRenderQuiescent()` exposed for callers that mutate without
  freeing. The per-channel `active` load is now acquire (pairs with the
  producer's store after filling the fields -- previously relaxed, so the
  field writes were not formally published) and the PCM-end deactivation
  store is release (pairs with the allocator's load before `resetChannel`).
- `MusicPlayer::stop()`: comment only. Its "do NOT call midiPlayer_->stop()
  here, the audio thread may be inside render()" note described the defect
  as a rule; it now says the call would be safe and why it is still
  deliberately omitted (it would zero the resume position AudioManager reads
  via `getPlaybackTimeMs()`, and the next `play()` stops under the barrier
  anyway).

**AFTER**: same driver, same TSAN tree: **0 reports**, exit 0, 7/7 tests
(no `tsan_after_report.*` file was produced); repeated `--gtest_repeat=5`:
0 reports, 5/5 passes (`tsan_after_repeat5.*`).

### Log callback (scope 3, T128 board item)

Wired: `miniaudioLogCallback` in `audio_backend.cpp` (level -> LOG_ERROR/
WARN/INFO/DEBUG, trailing newline stripped) registered on
`ma_device_get_log(&device)` right after `ma_device_init`. miniaudio's ALSA
underrun-recovery messages now reach the willeq log. Caveat, stated in the
code comment: miniaudio posts from the thread that hit the condition; on
the underrun path that is the worker thread, but only after it has already
left real-time territory (inside `snd_pcm_recover`), so a `LOG_*` there is
accepted for those error paths. Nothing in the steady-state callback posts
a log. Not exercised here (no device on this host; the ALSA quirk test is
the only device-touching test and it fails for the documented reason).

### Verification bar

| step | result |
|---|---|
| `cmake --build slot2/build -j24` | exit 0, 0 warnings in touched files (`build_full.log`) |
| `ctest` (GLES2-config tree, `-j8`) | **2920/2921**; the 1 failure is `AudioManagerTest.PreloadCommonSounds` (documented ALSA quirk). 7 new cases pass. (`ctest_full.txt`) |
| `scripts/build-gles2-x86.sh` | exit 0 (`build_gles2_x86.log`) |
| visual gate | PASS, goldens untouched (`visual_gate.txt`; no renderer code changed) |
| `scripts/build-arm-noble.sh` | exit 0 (`build_arm_noble.log`) |
| `check_file_sizes.sh fable` | 10 files, 0 FAIL; the one WARN (`inventory_manager.cpp` 2171 lines) is not this branch's -- `fable` advanced past `f461aaf3` while this ran and the script diffs against the moving tip (`check_file_sizes.txt`) |

### Deviations / notes

- TSAN tree `slot2/build-tsan` was reconfigured with `-DEQT_BUILD_TESTS=ON`
  (it was tests-off since T114); only the driver target was built there.
  The first BEFORE attempt died with TSAN's "unexpected memory mapping" --
  `setarch x86_64 -R` (ASLR off, as T220/T245 did) fixed it.
- The driver is a unit test, not the T245 live tour: no eqemu (per
  dispatch). The class is the same (`MidiPlayer::stop` <-> `render` on tsf
  voices, `tml_free` <-> the message walk), and the driver is denser than
  the tour (300 stop/play cycles vs a handful), which is why it also
  surfaced the use-after-free and the SEGV the tours never did.
- `ctest` was run with `-j8` (the T35 port-race fix makes that safe);
  2921 total vs the 2746 baseline is fable's growth since T214, not this
  unit's (+7).
- Not done: making `MusicPlayer::stop()` reset the synth (see above --
  would break zone-music resume). Not done: any change to `loadSoundFont`'s
  raw-pointer publication (T131's caveat still stands for a caller that
  does not go through `play()` afterwards; comment updated to say so).
