# T392 — sounds in the `snd*.pfs` archives never load (B200)

Class A, slot2, no server (the archives are on disk). Read the B200
board text (T386's narrowing: names ARE in `config/snd_index_cache.json`,
the lookup lowercases, the other failure exits of `loadSoundDataFromPfs`
log and none appears), `src/client/audio/audio_manager.cpp`, the PFS
reader it uses, T328's Review (the sound bank and how ids map to files),
and the audio tests (`tests/test_audio*`, `test_ui_sound*`).

1. Reproduce offline: a test that constructs `AudioManager` against the
   real client tree (`/mnt/projects/EverQuestP1999`) and calls
   `loadSound("BuyItem.WAV")` / `playSound(138)` headlessly (no device —
   whatever seam the existing audio tests use), asserting the PCM buffer
   is non-empty. It must FAIL today with the same "not found" line.
2. Instrument nothing permanent: find the branch that returns "not found"
   without logging — the index says archive X, so either the archive path
   the runtime opens differs from the builder's (`config/eqe1_archives`?
   `snd_index_cache` written with a different base path?), the entry name
   case differs inside the PFS directory, or the cache is consulted
   before the index is loaded. Say which, with the values.
3. Fix; the test goes green; add the same test over five more names from
   T386's list (SwordBlk, Swing, Spell_1, LevelUp, WaterIn) so a whole
   family is pinned, plus one negative (a name not in any archive still
   reports not found, once).
4. Strike B200; annotate B036/B172 (the sounds now audible are OWED a live
   ear — one T328 tone-probe row on fable for id 138, next lock unit).

## Proof

Build, `ctest -j6`, pre-fix/post-fix runs, file sizes. No graphics.

STOP: the archives on disk lack the files (then the index is wrong — say
where it came from).
