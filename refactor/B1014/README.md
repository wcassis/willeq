# B1014 (#335): release the custom SoundFont only on a real end, and never on
the audio thread

AUDIT-WAVE7 found B990's fix (release the custom orchestral SoundFont in
`MusicPlayer::stop()`) wrong twice over. Both defects reproduce at
origin/fable `06852f31` and both are closed here at the mechanism.

## The defects (from #335)

1. **`play()` frees the font the next song just selected.**
   `MusicPlayer::play()` calls `stop()` first
   (`src/client/audio/music_player.cpp:121` at the base), and
   `AudioManager::playMusic` selects the font BEFORE `play()`
   (`src/client/audio/audio_manager.cpp:385-396` at the base). So with ANY
   song playing, the Custom font `playMusic` had just selected was freed by
   the very next call and the custom song played on GM.
2. **The fade-completion release ran on the audio thread**
   (`music_player.cpp:299` at the base, via `midi_player.cpp:169-197`): a
   `tsf_close` racing the game thread's `selectSoundFont(Custom)` or
   `MidiPlayer::play()` (`tsf_reset`). Possible double-close /
   use-after-free [assumed from the code; not reproduced — the audit did not
   race the threads, and neither does this unit]. The T248 render barrier
   covers render passes, not game-thread mutators.
3. **The tests asserted a proxy.** The reload case started from silence and
   checked only `isCustomSoundFontResident()`, never
   `getActiveSoundFont()`; there was no failed-reload case.

## RED (on origin/fable `06852f31`, before the fix)

The two AUDIT-WAVE7 probes from #335 were added as
`SoundFontReleaseAudit.CustomSongStartedWhileGmSongPlays` and
`SoundFontReleaseAudit.CustomTrackSwitchWhileCustomPlays` (asserting BOTH
`isCustomSoundFontResident()` AND `getActiveSoundFont() == Custom`), built
and run against the UNMODIFIED audio sources at origin/fable. Output:

```
Running main() from ./googletest/src/gtest_main.cc
Note: Google Test filter = SoundFontReleaseAudit.*
[==========] Running 2 tests from 1 test suite.
[----------] Global test environment set-up.
[----------] 2 tests from SoundFontReleaseAudit
[ RUN      ] SoundFontReleaseAudit.CustomSongStartedWhileGmSongPlays
[audit] after GM->custom: resident=0 active=GM
/home/user/src/willeq-wt/kb-b1014-soundfont-release-order/tests/test_soundfont_release.cpp:293: Failure
Value of: m.midi.isCustomSoundFontResident()
  Actual: false
Expected: true

/home/user/src/willeq-wt/kb-b1014-soundfont-release-order/tests/test_soundfont_release.cpp:294: Failure
Expected equality of these values:
  m.midi.getActiveSoundFont()
    Which is: 4-byte object <00-00 00-00>
  EQT::Audio::SoundFontType::Custom
    Which is: 4-byte object <01-00 00-00>

[  FAILED  ] SoundFontReleaseAudit.CustomSongStartedWhileGmSongPlays (39 ms)
[ RUN      ] SoundFontReleaseAudit.CustomTrackSwitchWhileCustomPlays
[audit] after custom->custom: resident=0 active=GM
/home/user/src/willeq-wt/kb-b1014-soundfont-release-order/tests/test_soundfont_release.cpp:313: Failure
Value of: m.midi.isCustomSoundFontResident()
  Actual: false
Expected: true

/home/user/src/willeq-wt/kb-b1014-soundfont-release-order/tests/test_soundfont_release.cpp:314: Failure
Expected equality of these values:
  m.midi.getActiveSoundFont()
    Which is: 4-byte object <00-00 00-00>
  EQT::Audio::SoundFontType::Custom
    Which is: 4-byte object <01-00 00-00>

[  FAILED  ] SoundFontReleaseAudit.CustomTrackSwitchWhileCustomPlays (32 ms)
[----------] 2 tests from SoundFontReleaseAudit (72 ms total)

[----------] Global test environment tear-down
[==========] 2 tests from 1 test suite ran. (72 ms total)
[  PASSED  ] 0 tests.
[  FAILED  ] 2 tests, listed below:
[  FAILED  ] SoundFontReleaseAudit.CustomSongStartedWhileGmSongPlays
[  FAILED  ] SoundFontReleaseAudit.CustomTrackSwitchWhileCustomPlays

 2 FAILED TESTS
```

(full capture: `/home/user/kb-evidence/b1014-soundfont-release-order/red_probes_on_origin_fable.log`)

## GREEN (at the tip, after the fix)

Full `test_soundfont_release` (7 tests: the 3 B990 tests with
`getActiveSoundFont()` assertions added after every `play()`, the 2 AUDIT-WAVE7
probes, the failed-reload case, the GM-never-loads case, and the
2,976 B / 32-block residual census):

```
[ RUN      ] SoundFontRelease.SoundFontCustomScopeFirstCallIsTheSynthus2NameSet
[measured] getSoundFontTypeForFile call: 2976 B / 32 blocks (B990 census residual: 2,976 B / 32 blocks)
[       OK ] SoundFontRelease.SoundFontCustomScopeFirstCallIsTheSynthus2NameSet (0 ms)
[ RUN      ] SoundFontRelease.ImmediateStopReleasesTheCustomFont
[measured] audio live while the custom-font song plays: 24225552 bytes (23.1033 MiB)
[measured] audio live after stop(0.0): 65680 bytes
[       OK ] SoundFontRelease.ImmediateStopReleasesTheCustomFont (72 ms)
[ RUN      ] SoundFontRelease.CustomSongStartedWhileGmSongPlays
[audit] after GM->custom: resident=1 active=Custom
[       OK ] SoundFontRelease.CustomSongStartedWhileGmSongPlays (20 ms)
[ RUN      ] SoundFontRelease.CustomTrackSwitchWhileCustomPlays
[audit] after custom->custom: resident=1 active=Custom
[       OK ] SoundFontRelease.CustomTrackSwitchWhileCustomPlays (271 ms)
[ RUN      ] SoundFontRelease.FailedReloadLeavesThePlayerOnGm
[       OK ] SoundFontRelease.FailedReloadLeavesThePlayerOnGm (21 ms)
[ RUN      ] SoundFontRelease.GmSongNeverLoadsTheCustomFont
[       OK ] SoundFontRelease.GmSongNeverLoadsTheCustomFont (3 ms)
[ RUN      ] SoundFontRelease.FadedStopReleasesTheCustomFontWhenTheFadeCompletes
[measured] audio live while the custom-font song plays: 24224160 bytes (23.102 MiB)
[measured] audio live after fade completion: 65632 bytes
[       OK ] SoundFontRelease.FadedStopReleasesTheCustomFontWhenTheFadeCompletes (19 ms)
[  PASSED  ] 7 tests.
```

(all 7 `OK`; full capture
`/home/user/kb-evidence/b1014-soundfont-release-order/green_final.log`; also
`ctest -R "^SoundFontRelease\."` 7/7 Passed, and `--gtest_repeat=5` all green)

The sibling suites stay green (rebuilt against the changed headers;
measured by binary run on this tree):
`test_midi_stop_race` 7/7, `test_music_player_soundfont_lifetime` 3/3,
`test_zone_music` 19/19 [measured, local x86].

## The fix

Release ordering (documented in one place:
`src/client/audio/music_player.cpp`, the comment above `stop()`):

- `play()` uses a new private `stopImmediateInternal()` — the stop mechanics
  without the release. A following custom song keeps its font resident.
- `stop(0.0)` (public) is a real end: `stopImmediateInternal()` then
  `drainPendingFontRelease()`.
- Fade completion in `mixerRenderCallback` (audio thread) calls
  `MidiPlayer::stop()` (thread-safe, re-entrant `waitQuiescent`) and sets
  `fontReleasePending_` — **the audio thread no longer frees.**
- New public `MusicPlayer::drainPendingFontRelease()`: the game thread's
  free site. Refuses while a song plays (so the flag can never free the font
  of a custom song that started in the meantime — the release rides on that
  song's own real end), then `releaseCustomSoundFont()`.
- Drained from `AudioManager::update()` (every game tick via
  `BridgeComponent::UpdateAudioListener` → `PostRenderTick`) and from the
  public `stop()`.

No timer, no second cache, `memoryLedgerBudgets.audio` untouched (32 MiB).

## Numbers

- `23.04 MiB = 24,162,944 / 1,048,576` (the old test header's "23.16 MiB"
  was the bytes/1048576 figure miscomputed; the header now states 23.04).
- The `au.soundfont_custom` residual **2,976 B / 32 blocks** is the
  first-call construction of `getSoundFontTypeForFile()`'s function-local
  `static const std::unordered_set<std::string> synthus2Files`
  (`audio_manager.cpp`, 31 Velious/Luclin names = 31 node blocks + 1 bucket
  array). **CONFIRMED by measurement**, not assumed:
  `SoundFontCustomScopeFirstCallIsTheSynthus2NameSet` measures the first
  call's allocation at exactly 2,976 B / 32 blocks (output above). This
  superseded the auditor's [assumed] identification in #335.
  (`getSoundFontTypeForFile()` was private; it is now public — a pure
  mapping, and the on-demand-load gate B977's `au.soundfont_custom` owner
  scope wraps — so the audit can reach it.)
- B990's "~35 ms" reload figure was an upper bound of a LOG GAP (the load
  sits between two unrelated log lines), not a measurement: now quoted as
  "<= ~35 ms (log gap)" (`refactor/B990/README.md`, `docs/decisions.md`
  2026-10-05). No new log line was added — the bound is what the evidence
  supports, and the card offered either.

## Doc checklist

- [x] `docs/decisions.md` 2026-10-05 Will entry: the worker-written B990
  paragraph moved out of Will's entry (his ruling text is untouched) into a
  new dated worker entry "2026-10-11 — B1014: the custom-Font release moves
  to a real end, off the audio thread (worker, #335)" at the end of the file.
- [x] `include/client/audio/midi_player.h` T504 lifetime sentence rewritten
  for the fixed lifetime (resident from on-demand load until a real end;
  reloaded by a following custom song; never freed by the stop inside
  `play()`).
- [x] `include/client/audio/midi_player.h` + `src/client/audio/midi_player.cpp`
  — the false "saves beforehand and restores after" sentences deleted
  (no such save/restore exists); both now point at the
  `music_player.cpp` reasoning.
- [x] `src/client/audio/music_player.cpp` — the "deliberately still not
  called" paragraph rewritten: the release-ordering comment above `stop()`
  states what each path does now (the old paragraph's "deliberately not
  called" half is retained — it is still true and still the reason
  `stopImmediateInternal()` does not call bare `MidiPlayer::stop()`).
- [x] `refactor/B990/README.md` + `refactor/B990/audio_ledger_census.tsv`
  header — the 2,976 B / 32-block residual named (the static name set),
  confirmed by measurement (see Numbers).
- [x] `refactor/B990/README.md` + `docs/decisions.md` 2026-10-05 — "~35 ms"
  now "<= ~35 ms (log gap)".
- [x] `tests/test_soundfont_release.cpp` — root-level asset absence goes
  through `tests/asset_requirement.h` (`requireEqClientAssets`, the
  `WILLEQ_REQUIRE_ASSETS=1` hard-FAIL path in CI), not a bare `GTEST_SKIP`.
- [x] `tests/test_soundfont_release.cpp` header — no longer says the stop
  "then selects GM" (the header now describes the B1014 ordering).
- [x] `tests/test_soundfont_release.cpp` — 24,162,944 B is 23.04 MiB, not
  23.16.
- [x] the struck B990 entry in `refactor/BOARD.md` — corrected: the release
  is at a REAL end (public stop / fade completion, game thread), never at the
  stop inside `play()`; the 2,976 B residual re-attributed; the test count
  updated to 8.

## Not verified / left open

- The audio-thread double-close/UAF of defect 2 was never reproduced
  (neither by the audit nor here) — the fix removes the audio-thread free by
  construction, which is the card's requirement ("The audio thread no longer
  frees"), but no TSAN/A-B demonstration of the pre-fix race exists in this
  unit.
- No live (server/lane) leg was run for B1014: the card is OFFLINE, and the
  unit-level green plus the AUDIT-WAVE7 probes are the probes. The B990
  live-leg census (`refactor/B990/audio_ledger_census.tsv`) remains the
  live evidence for the release landing, and its numbers are unchanged by
  this fix (the release still happens at the same steady point; only the
  ordering of in-between releases changed, which the live tour never
  exercised in a way that would change the census rows).
- The Pi re-run and the `memoryLedgerBudgets.audio` re-ratchet remain Will's
  (per the B990 card, carried in the struck BOARD.md entry).
- `AudioManager::update()` had zero callers before this unit; it is now
  driven from `BridgeComponent::UpdateAudioListener` (every game tick). If a
  future tick path skips that body, the fade-completion release waits for the
  next public `MusicPlayer` call instead of a tick — still bounded, never a
  leak.

## Files

- `include/client/audio/music_player.h` — `drainPendingFontRelease()`,
  `stopImmediateInternal()`, `fontReleasePending_`.
- `src/client/audio/music_player.cpp` — the ordering (comment above
  `stop()`), `stopImmediateInternal()`, `drainPendingFontRelease()`, the
  fade-completion flag instead of the audio-thread `tsf_close`.
- `src/client/audio/audio_manager.cpp` — `update()` drains the flag.
- `src/client/bridge/bridge_component_ui_audio.cpp` —
  `UpdateAudioListener()` drives `AudioManager::update()` on the game tick.
- `include/client/audio/audio_manager.h` — `getSoundFontTypeForFile()`
  public (audit reachability, documented).
- `include/client/audio/midi_player.h` + `src/client/audio/midi_player.cpp`
  — lifetime docs rewritten; false save/restore sentences deleted;
  `setCustomSoundFontPath()` test hook (failed-reload case).
- `tests/test_soundfont_release.cpp` — the 8 tests above (RED/GREEN in this
  README).
- `docs/decisions.md` — the 2026-10-05 entry's worker paragraph moved to a
  new 2026-10-11 worker entry.
- `refactor/B990/README.md`, `refactor/B990/audio_ledger_census.tsv`,
  `refactor/BOARD.md` — corrections (residual naming, log-gap bound, release
  ordering, test count).
