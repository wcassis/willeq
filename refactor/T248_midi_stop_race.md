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
