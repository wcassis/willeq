# T386 — Owed live proofs 6

Measurement only; slot2; eqemu lock (atomic; must be ABSENT — if present
read it and STOP). Reuse the T377 drivers (`review/T377/scripts/`,
`review/T361`, `review/T358`); verify every chat line against the client's
`Chat submit:` line; video for transients. Rebuild the slot's `build` and
`build-gles2` from the tip you are given (it carries T381/T382/T383; if
T384's willeq.db v7 has merged by then, rebuild all three trees).

1. **T382 corpse coin (B172/B036).** T377's recipe (`arena`,
   `#npctypespawn 113423`, `#damage 100000`, step back 20, F9 loot): on
   fable, open the loot window — expect the purse to move by the corpse's
   four denominations (SELECT `character_data` / the client's own currency
   line before and after), `BuyItem` (138) to play (the tone-probe rig
   from T328), and the chat line `You receive N platinum, N gold, N silver
   and N copper from the corpse.` Also engineer ONE zero denomination
   (`#give`/`#setcoin`-style GM command on the corpse or a mob whose loot
   table drops no platinum) on the OFFICIAL client and capture the line —
   T382 left the zero form UNMEASURED.
2. **T382 keys.** Press `Alt+P`, `Alt+F1`, `Alt+B` on fable: each toggles
   its window; `G` and `P` do nothing. Screenshot the empty group window
   (no group) on both clients side by side.
3. **T381 spell colours (B160).** Two-client pair: Testchar casts a Fire
   spell and a Cold spell (`#castspell` ids from T380's census — Frost
   Bolt 54 and a fire spell of similar level); the OBSERVING client's
   frame shows two different particle colours. Video both.
4. **T383 vision channels (B171).** Repeat T377 §2 exactly (same spot,
   crop, video-stepped): per-channel ratios for Ultravision and
   Infravision on fable vs the oracle table (R/G/B and whole-frame). The
   B×0.821 anomaly T383 could not explain from the source: does it
   reproduce? If yes, board it with the frames.
5. `review/T386/README.md` (PASS/FAIL first per item); strike the owed
   notes; restore every server value (snapshot the world clock);
   `down.sh`; delete the lock.

STOP: lock present; a proof FAILS (record, board B185+, continue).
