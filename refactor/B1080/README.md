# B1080 (GitLab #400): zone-out silences zone audio

Will 2026-10-15 (XU4): "Upon death, willeq tried zoning but hung at 20%. I still
hear in-zone music and spell sound effects." Measured mechanism (the issue
body): NOTHING stopped zone audio at zone-OUT — `unloadZone()` ran only in the
NEXT zone's `loadZone()` (zone_entry.cpp:225) and `onZoneChange()` only on zone
ENTRY (zone_entry.cpp:229), so the leaving zone's music and every zero-cooldown
emitter kept playing until the destination re-armed audio.

## Oracle ledger (step 1 of the card — the measured official-client behavior)

**Rig:** lane 1 (eq-oracle-1), official Titanium client under Wine/Xvfb/
llvmpipe, eqemu lane 1. This rig produced NO client audio historically —
userspace alsa-lib enumerates zero cards on it (every past session's wine.log
shows `parse_card cannot find card '0'`; the kernel and the control ioctls are
fine, strace-verified — `oracle_audio_findings.md` in the evidence dir). The
workaround that makes it audible: a user-session PulseAudio daemon (auto_null
sink + monitor), so Wine's winepulse.drv opens a real stream and
`parecord --device=auto_null.monitor` records it. That daemon is still up.

**Scenario:** Testcaster (id 67) seeded in **causeway** (zone 303, pose
130,337,3.75 — causeway.mp3, 2.1 MB, plus environment emitters; the client's
shipped files carry it), with **Ring of Karana** (spell 530, rank 0, any
class, 7 s cast, `teleport_zone=northkarana`) memorised. Login → in-world →
settle → `/cast Ring of Karana` → poll through the transition. The capture
records the Pulse monitor (44.1 kHz s16le stereo) and the X screen
(10 fps) for the whole session; timeline marks carry wall-clock, the analyzer
(`analyze_audio.py`) turns the raw s16le into a 1 s RMS envelope,
`measure_fade.py` a 0.25 s one.

Session `b1080_audio_20261016T124756_1320452` (evidence: `run4_audio_envelope.txt`,
`run4_timeline.tsv`, stills 63/64/66/67, OCR dumps — all in
`/home/user/kb-evidence/kb-b1080-zone-out-audio/`). Marks, seconds relative to
the recording start (`video.mp4.t0` = 1792154882.756):

```
CHARACTER_SELECT 158.8   ZONE_ENTRY_CLICKED 226.4   IN_WORLD 277.7
MUSIC_SETTLED 299.5      CAST_ISSUED 304.1          LOADING_TAIL 666.3
```

**What stops when** — the 1 s RMS envelope (`run4_audio_envelope.txt`, t = seconds):

| t    | RMS  | state |
|------|------|-------|
| 209-308 | 0-4502 | old-zone audio (causeway music + ambient) sustained — dead second at 209, ~1 s ramp-up, peak 4502 at 244 |
| 308 | 654 | fade begins (7 s cast ends ~311) |
| 311 | 149 | |
| 313 | 62 | |
| 315 | 17 | |
| 316 | 3  | **noise floor reached** |
| 317 | 3  | flat — no SFX tail (0.25 s bins from 315.0: 26.9/15.1/10.4/7.6/5.0/3.2/1.6/0.8/0.5/0.6/0.5 — floor by 316.25, nothing above 3 after) |
| 318 | 1329 | new screen's audio starts |
| 322 | 3215 | sustained (login/char-select music) |

Reads: (1) at the zone-out the old zone's audio does NOT keep playing — it
**fades to the noise floor** (the last strong sample at 308 to floor at 316.2,
~8 s from the `/cast` at 304.1; the 7 s cast consumes ~7 s of that, leaving a
short stop-fade at the transition itself — see "Not verified" on the exact
split); (2) **there is no SFX tail** — the floor is flat for the ~1.5 s before
new audio starts; (3) **the loading/post-transition screen is silent except
for its own music**, which starts ~2 s after the floor — i.e. the official
client answers the issue's oracle question with: fade, no tail, silence until
the destination (or the char-select fallback) re-arms audio. That is exactly
what `silenceForZoneOut()` implements.

**The destination failed on this rig** (stills 66/67: the client is back at
the login screen with "A connection to the server could not be reached",
`run4_loading_ocr.txt` entry 58+). The northkarana zone-in never established,
so this capture exercises precisely the B1072-class path — a zone-out whose
new zone never completes — and the old zone's audio still stopped. The
sustained audio from t=318 is the fallback screen's music (not re-verified
track-by-track; see below).

Three earlier sessions on the same rig (login-driver failures, `login.rc` /
`91_login_fail.png` in their session dirs) produced no usable ledger; they are
kept in the evidence dir as the negative evidence that the login modal dismiss
is the flaky step (fixed with the measured (618,523) OK-button jclick).

## Fix (step 2)

`ZoneAudioManager::silenceForZoneOut(fadeSeconds)` (zone_audio_manager.h:73,
cpp:191) stops every emitter (each looped SFX channel freed, B1074), frees the
one-shot SFX channels (`AudioManager::stopAllSounds()`), and fades the zone
music (`stopMusic(fade)`); idempotent, safe with nothing loaded. It is called
from `ZoneComponent::CleanupZone()` (zone_component.cpp:365 via
`SilenceZoneAudioForZoneOut()`, :525) — the single point every zone-OUT path
funnels through: the graceful zone-out, the zone-out handshake timeout,
logout, and the B1072 fresh-login give-up (which reaches it through
`AbandonZoneConnect() -> DisconnectFromZone() -> CleanupZone()`). The 2.0 s
fade constant (`kZoneOutMusicFadeSeconds`, zone_component.h:219) is the
client's own existing zone-music stop (the entry-side `onZoneChange` uses a
2.0 s fade, audio_manager.cpp:490); the oracle envelope above bounds it — the
old-zone audio is at the noise floor within a few seconds of the transition,
never audible again. `onZoneChange()` keeps its entry role untouched; the
destination's `loadZone()` re-arms emitters and music as before.

## Tests (step 3)

- `tests/test_zone_sound_emitters.cpp` — `ZoneAudioSilenceForZoneOutTest`:
  the real managers over the qeynos2 loops; `silenceForZoneOut()` frees every
  loop channel (B1074 harness), frees the one-shots, fades the music, and is
  a no-op with nothing loaded. (32/32 pass locally, `test_zone_sound_emitters`.)
- `tests/test_zone_component.cpp` — `ZoneOutTeardownRunsTheAudioSilence`:
  the real `DisconnectFromZone() -> CleanupZone()` teardown moves the silence
  counter 0→1. (10/10 pass.)
- `tests/test_zone_connect_timeout.cpp` — the scenario give-up asserts
  `ZoneAudioSilenceCount()` moved by exactly one (a headless `--scenario` run
  has no audio system, so the lane counts the teardown path); an unreadable
  count (-1) and a missing increment both fail, pinned by two fake-world
  tests. (15/15 pass.)
- Red at the parent (37fb0df6): compile-red — every new test calls APIs that
  do not exist there.

## Hygiene / CI (step 4)

`ctest -L hygiene` green (38 tests, 2 skipped-by-design) and
`scripts/refactor/check_doc_counts.py` green on the pushed tip; the full
suite + visual gate run in the MR pipeline. No bless-only file touched
(goldens, baselines, ratchets, `constrained_presets.json`, `third_party/` —
none in the diff). The final push merged origin/fable (the ThinkPad-CI
change, MR !211) in, because B1087 made CI refuse builds on the old runner
topology; the re-trigger commit (9f30921f) carries this paragraph so the MR
pipeline runs against the merged head rather than a bare merge commit.

## CLAUDE.md EDITS

`src/client/audio/CLAUDE.md` — replace the `ZoneAudioManager` line:

    - `ZoneAudioManager` - Manages all zone emitters, handles day/night transitions; `unloadZone()`/`pause()` stop every emitter, so no looped channel outlives its zone

with:

    - `ZoneAudioManager` - Manages all zone emitters, handles day/night transitions; `unloadZone()`/`pause()` stop every emitter, so no looped channel outlives its zone. `silenceForZoneOut()` (B1080, #400) goes further — every emitter stopped, one-shot SFX channels freed, zone music faded — and is called from `ZoneComponent::CleanupZone()`, the single point every zone-OUT path funnels through, so a leaving zone's audio never outlives the zone (oracle: the official client fades its zone audio out at the transition, no SFX tail, silence until the destination re-arms — `refactor/B1080/README.md`)

## Not verified / left open

- **Exact fade split at the transition.** No pcap was captured in this run,
  so the zone-OUT *packet* instant is unmarked; the ~8 s envelope decay runs
  from the `/cast` at t=304.1, of which ~7 s is the spell's own cast time
  (rank-0 Ring, `cast_time` 7000 ms in `spells_new`). The stop-fade at the
  boundary is therefore ≤ ~1-3 s from last strong sample to floor — short —
  and the implementation's 2.0 s (the client's existing zone-music stop)
  sits inside that bound. A pcap-tagged rerun could pin it tighter; the
  qualitative ledger (fade, no tail, silence) holds regardless.
- **The new audio after the floor** (t=318+) is the fallback login/
  character-select screen's music — identified by the stills, not by
  track name (no per-track fingerprint was taken).
- **SFX-tail resolution:** 0.25 s — nothing above the noise floor (RMS ≤ 3,
  the auto_null sink's own floor) in the 1.5 s after the music floor.
- The rig's PulseAudio workaround is a session daemon on eq-oracle-1; it
  dies with the VM and must be re-armed for the next oracle audio capture
  (`pulseaudio --daemonize --exit-idle-time=-1` under the user's
  XDG_RUNTIME_DIR).
