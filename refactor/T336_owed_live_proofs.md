# T336 — The owed live proofs: navmesh overlay ON/OFF pair (B048), and the listener pan check (B045)

Measurement-only live session; slot1; needs the eqemu lock (free after
T332). No code change.

1. Lock atomically; `up.sh`; `build-gles2/bin/willeq` from the slot at the
   CURRENT tip (`cmake --build` first; the slot must contain T333's fix —
   `git log --oneline -20 | grep T333`), account willeq_test / `Testchar`
   (T331/T332's slot-2 selection script — read `review/T332/`), qeynos2,
   force daylight, `--eqe1-path`.
2. **B048 pair:** capture OFF, press Ctrl+N (T329's input findings: use
   the working input route from `review/T329/README.md`), capture ON; the
   ON frame must show navmesh lines AND the health/mana/stamina bars, FPS
   counter, chat panel and hotbar. Two PNGs + README to `review/T336/`
   naming what to look at. Then Ctrl+M (map overlay) ON/OFF the same way
   if T335 has merged by your tip (check `git log`); else note it owed.
3. **B045 check (optional, cheap):** with the audio backend the T328 rig
   used (`snd-aloop` capture), stand facing a positioned sound source
   (a door: open it with `/say #door open`-style GM or just a click; door
   sounds are positioned per `entities_component_world.cpp`), record 2 s,
   turn 180°, record 2 s; the left/right channel balance must swap. Two
   short WAVs + the channel RMS numbers in the README. If the rig cannot
   be set up in 20 minutes, skip it and say so — it is a listening test.
4. Restore the character as found (SELECT first); `down.sh`; delete the
   lock. Strike the "owed" notes on B048 (and B045 if step 3 ran) in
   `refactor/BOARD.md`; commit the board + plan Review only.

STOP: lock held; the client cannot log in; Ctrl+N does not toggle (then
the fix is wrong — report the log lines, do not touch code).
