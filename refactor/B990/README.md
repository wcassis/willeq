# B990 (#309) — the custom SoundFont is released when a custom-font song stops

Class A: the custom orchestral SoundFont `synthus2.sf2` (12,036,844 B on disk)
loaded on the first `playMusic` of a Velious/Luclin XMI stem (T504 deferred it
from startup to `MidiPlayer::selectSoundFont(Custom)`) and was **never
released** — 24,160,712 B (Pi) / 24,162,944 B (x86) of float samples
(`tsf` holds float) resident for the rest of the session, 84 % of the audio
ledger's measured worst (B977, `refactor/B977/audio_ledger_census.tsv`,
docs/decisions.md 2026-10-05). The T504 free path existed
(`selectSoundFont(GM)` closes the font) but was driven only by a font SWITCH;
a song that simply ends never switches, so the free path was unreachable.

## The fix (e5acd72b)

The release is at the mechanism, not a timer: `MusicPlayer::stop()` is the one
place a song ends, and it now releases the resident custom font.

- `MidiPlayer::releaseCustomSoundFont()` (`src/client/audio/midi_player.cpp`):
  the new free path. It `stop()`s first — the T248 render-barrier handshake,
  so no render pass can still be inside the synth reading `impl_->soundFont` —
  points the active pointer back at GM (exactly what
  `selectSoundFont(GM)`'s release block does), then `tsf_close`s the instance.
- `MusicPlayer::stop(fadeSeconds)` (`src/client/audio/music_player.cpp`, the
  immediate-stop path): after freeing the mixer channel, calls
  `releaseCustomSoundFont()`. The freed channel already proves the audio
  thread is out of the synth; `releaseCustomSoundFont()`'s own `stop()` is the
  same handshake.
- The FADED path: `stop(fade>0)` returns with the song still fading, so the
  immediate path's release never ran (the pre-B990 gap); the fade-completion
  branch in the render callback now releases the font there, on the audio
  thread, which `stop()` already quiesced.
- `isCustomSoundFontResident()` (null-check on `impl_->customSoundFont`) is the
  predicate the tests assert.

**The stop() / playback-position interaction** (the one thing the pre-B990
comment defended by avoidance): `MidiPlayer::stop()` zeroes the playback
position; `releaseCustomSoundFont()` calls it. That is not observable by any
caller — the only readers of `getPlaybackTimeMs()` are
`startAutoAttackMusic()`/`startVendorBankMusic()`, both read it while the song
is still playing and stash it in `savedZoneMusicTimeMs_`; the resume uses the
saved value, never the live one. (Commented at the call site,
`music_player.cpp`.)

**The reload is cheap — no second cache added** (criterion 2). Measured from
the live leg's own log: `tsf_load_filename` for the 12 MB font completed in
**~35 ms** (`live_velketor/velketor/audio/client.log`: the `State snapshot
published` line at 22:20:55.120, the `loaded custom SoundFont on demand` line
at 22:20:55.155 — the load sits between them; B977's run shows 74 ms,
23:17:18.042→23:17:18.116). The font is page-cache-resident on every reload in
the same zone (same file, same session), so the measured cost is a page-cache
read, not a disk seek. A stutter of <50 ms at a music START (the song's first
note) is below perception; the card's own escape clause ("leave
streaming/caching as left open") is therefore NOT exercised — the reload is
fast, and a second cache would be a second 12 MB of the very memory the card
exists to free. The T504 on-demand load path (`selectSoundFont(Custom)`) is
unchanged: a following custom-font song reloads exactly as before, and a
GM-font song never loads the custom font (test 2, below, pins it).

**Thread safety** (criterion 1's keep-the-mutation-rules clause): the MidiPlayer
rules in `midi_player.h` are untouched — play/seekTo/selectSoundFont/
loadSoundFont still serialise on the same lock, and `releaseCustomSoundFont()`
goes through `stop()`'s render-barrier handshake exactly like them. The
fade-path release runs on the audio thread, which `stop()` already quiesced
and which is the `renderBarrier_` re-entrant owner (commented at
`music_player.cpp:285-299`). `test_midi_stop_race` stays green (re-run below).

## 1 — RED/GREEN ctest

New suite `SoundFontRelease` in `tests/test_soundfont_release.cpp` (a sibling
of `test_music_player_soundfont_lifetime.cpp`, which keeps its existing
lifetime pins and is not touched — the card says extend *or* a sibling).
Three tests: (a) immediate stop releases the custom font and a following
custom song reloads it; (b) a GM-font song never loads the custom font;
(c) a faded stop releases the font when the fade completes.

**RED** — base tip `16c9cf44` (pre-`origin/fable` merge), the new test added
and nothing else (`/tmp/b990-red`, worktree at the base commit).
`ctest -R SoundFontRelease`: 2 of 3 fail, the exact output
(`~/kb-evidence/b990-soundfont-release/red/ctest_red.log`):

```
[ RUN      ] SoundFontRelease.ImmediateStopReleasesTheCustomFont
[measured] audio live while the custom-font song plays: 24225520 bytes (23.1033 MiB)
[measured] audio live after stop(0.0): 24225520 bytes
tests/test_soundfont_release.cpp:172: Failure
Value of: m.midi.isCustomSoundFontResident()
  Actual: true
Expected: false
the custom SoundFont was not released when the song stopped -- 24225520 B (au.soundfont_custom) held for the rest of the session
tests/test_soundfont_release.cpp:175: Failure
Expected: (after) <= (kReleasedBound), actual: 24225520 vs 2097152
stopMusic leaked 24225520 B of the custom SoundFont (B977 measured 24,162,944 B at this point)
[  FAILED  ] SoundFontRelease.ImmediateStopReleasesTheCustomFont (43 ms)
...
[ RUN      ] SoundFontRelease.FadedStopReleasesTheCustomFontWhenTheFadeCompletes
[measured] audio live while the custom-font song plays: 24225520 bytes (23.1033 MiB)
[measured] audio live after fade completion: 24225472 bytes
tests/test_soundfont_release.cpp:272: Failure
Value of: m.midi.isCustomSoundFontResident()
  Actual: true
Expected: false
the custom SoundFont was not released when the faded song ended -- 24225472 B held
[  FAILED  ] SoundFontRelease.FadedStopReleasesTheCustomFontWhenTheFadeCompletes (37 ms)

33% tests passed, 2 tests failed out of 3
```

The 24,225,520 B held after stop is the defect: the same 24.16 MB B977
measured live, reproduced in-process.

**GREEN** — branch tip, `ctest -R 'SoundFontRelease|MidiStopRace|
SoundFontLifetime'`: **8/8 pass** (3 SoundFontRelease + 2 MidiStopRace + 3
MusicPlayerSoundFontLifetime, `build/ctest2.log`). Both runs use the
branch-tip `MidiPlayer` built fresh; the GREEN test binaries were REBUILT
after the last source edit (the card's stale-binary rule) — the build
reconfigure also re-linked the three test targets against the worktree's
own (fresh) `libIrrlicht.a`, which is the one that carries B997's
`gles2GetDrawCensusTotals`; the stale shared copy at
`/home/user/src/willeq/third_party/irrlicht-gles2-x86` predates it and the
`willeq` link failed against it until the build pointed at the worktree's
copy (`EQT_GLES2_IRRLICHT_DIR=$PWD/third_party/irrlicht-gles2-x86`).

## 3 — LIVE ledger leg (lane 3, x86)

`scripts/client_ledger_capture.sh` mode `audio` (WILLEQ_LEDGER_OWNERS=audio,
WITHOUT --no-audio), lane 3 (`EQEMU_LANE=3`), the binary built from the
branch tip. Both legs `rc=0` (`result.txt`: ready=1, steady_60s=1, landed=y,
watchdog=none, zone_link_lost=no), the rig's snapshot restored
byte-identically with the world down, the stack down, the lock free.

**The stop was driven by `/sound off` in the `--script` tour, not `/music
off`.** `/music off` calls `pauseMusic()` — it silences but KEEPS the font
(for a resume), so it would have shown the old 24 MB, not the fix.
`/sound off` → `AudioManager::setAudioEnabled(false)` →
`stopMusic(0.0f)` → the B990 release path, and with `audioEnabled_` false the
zone emitter's `playMusic` is a no-op, so the font stays released. (Tours:
`~/kb-evidence/b990-soundfont-release/live_velketor.tour`,
`live_qeynos2.tour`.)

Timing: the script releases at "Fully connected" (22:20:54.1), `ready` arms
`steady_60s` (22:20:55.1), the custom font loads at 22:20:55.155 with the
music start, `/sound off` fires at release+40 s = 22:21:31.77 (the log line
`released custom SoundFont on stop`), and `steady_60s` records at 22:21:55.12 —
**~18 s AFTER the release**, so steady_60s is the card's "music started, then
stopped" point. `WILLEQ_LEDGER_IDLE_EVERY=10` (now passed through the rig,
commit 954e89a7) gives the post-release idle points through idle_100
(22:22:35), all after the release.

**Result** — `refactor/B990/audio_ledger_census.tsv` (committed), the rows
re-derived here from the raw logs (`~/kb-evidence/b990-soundfont-release/
{live_velketor,live_qeynos2}/<zone>/audio/client.log`):

| zone | run | point | au.soundfont_custom live | audio live / peak |
|---|---|---|---:|---:|
| velketor | B990-live (stopped) | steady_60s | **2,976** | 4,386,288 / 28,850,592 |
| velketor | B990-live (stopped) | idle_100 | **2,976** | 4,386,288 / 28,850,592 |
| velketor | B977-base (playing) | steady_60s | 24,162,944 | 28,614,000 / 28,918,432 |
| qeynos2 | B990-live (stopped) | steady_60s | **2,976** | 3,908,640 / 5,654,592 |
| qeynos2 | B977-base (playing) | steady_60s | 2,976 | 3,910,144 / 5,652,480 |

- **velketor is the proving leg**: the peak (28,850,592) shows the font
  loaded in this run (the 24 MB is in it: 28,850,592 − 24,225,520 ≈ 4,625,072
  ≈ the non-font audio live 4,386,288); at steady_60s the font's 24,225,520 B
  are gone and the audio row is back to its non-font level. Against B977's
 24,162,944 B / 28,614,000 B at the same point: **−24,159,968 B** on the
 custom row, **−24,227,712 B** on the audio category (the extra ~67 KB over
 the custom delta is the music channel's own live bytes at the two runs'
 steady points — B977's music was still playing, mine had stopped).
- **The residual 2,976 B / 32 blocks is NOT font samples.** It is charged
  under the `au.soundfont_custom` owner scope by `playMusic`'s
  `selectSoundFont(...)` bookkeeping (audio_manager.cpp, the scope wraps the
  whole call for any `.xmi`), and it is **identical in the GM-only qeynos2
  legs, in B990's and in B977's, in both runs** (qeynos2 B977-base: 2,976/32
  at steady_60s with `loaded custom SoundFont` count = 0 in that log) — so it
  is the pre-existing selection overhead, present with or without the custom
  font, before and after the fix. The card's "0 B" bar is met in the only
  sense the ledger supports: the font's blocks are gone (136 → 32; the 32 are
  the same 32 a GM song always shows), i.e. the 24,162,944 B of float samples
  are released.
- **qeynos2 is the classic control**: no custom load ever (0 `loaded custom
  SoundFont` lines in its log), the custom row is the 2,976 B residual at
  every point, by construction.

**Not verified / left open**

- **The Pi figure is NOT part of this card** — its live re-run and the
  `memoryLedgerBudgets.audio` re-ratchet (32 MiB → Will's number, after B989
  lands too) are Will's bless. The Pi baseline row (24,160,712 B at velketor
  steady_60s, `refactor/B977/audio_ledger_census.tsv`) is the stand-in here;
  the fix is platform-neutral (it closes the `tsf` instance), so the Pi
  release is expected, but NOT measured on the Pi in this unit.
- `memoryLedgerBudgets.audio` is untouched (32 MiB), per the card.
- The 2,976 B residual (32 small blocks from the font-SELECTION path) could
  be scoped out of `au.soundfont_custom` into its own owner; it is below
  3 KB and identical with/without the custom font, so it is left alone —
  moving it would change the B977 baseline row's meaning.
- Font reload between two custom songs is the measured ~35 ms page-cache read
  (above); if a future font is far larger or the page cache is cold, a
  streaming/caching decision is still open — none is made here.

## Files

- `src/client/audio/midi_player.{h,cpp}` — `releaseCustomSoundFont()`,
  `isCustomSoundFontResident()`.
- `src/client/audio/music_player.cpp` — the release at the immediate-stop
  path and at fade completion.
- `tests/test_soundfont_release.cpp` (new) + `tests/cmake/audio.cmake`.
- `scripts/client_ledger_capture.sh` — `WILLEQ_LEDGER_IDLE_EVERY` pass-through
  (commit 954e89a7), so the post-release idle points record.
- `refactor/B990/audio_ledger_census.tsv` — this unit's live table.
- Raw logs + tours + rig logs: `~/kb-evidence/b990-soundfont-release/`
  (bulk evidence, not committed).
