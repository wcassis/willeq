# T377 — Owed live proofs 5

Measurement only; eqemu lock (atomic; must be ABSENT — if present read it
and STOP); the slot that frees after T370 releases the lock. Reuse the
T358/T361/T367 drivers under `review/T358|T361|T367/scripts/`; verify every
chat line against the client's own `Chat submit:` line. Binary rebuilt from
the slot at the current tip.

1. **B115 — official client key bindings screenshot.** Official client under
   Wine (T361's rig, character Oraclealpha): open Options → Keys, page to
   the UI window bindings, screenshot every page that names the group, pet
   and buff windows; record the default keys and compare with
   `hotkey_manager.cpp` (G / Alt+P, P, Alt+B). Also capture the empty group
   window (no group, window open) — T368 left its content UNVERIFIED. If
   the defaults differ: board it (B145+), do not rebind here.
2. **T361 vision residuals re-measured.** With T367's light-culling fix in
   the binary, repeat T361's table (oracle night no-buff / Ultravision /
   Infravision vs fable, same spot, same crop, video stepped). Record the
   new ratios against the oracle's 1.554 / 1.253 on the B067 strike.
3. **B036 corpse-loot coin row.** A clutter-free zone (T342 said qeynos2
   was too noisy — try `arena` or `befallen`), one `#npctypespawn`ed mob
   killed, loot the coin: which UI sound (the tone probe rig,
   `review/T328/scripts`) on the official client, and what fable plays.
   Fill the row in the B036 strike; board any gap.
4. **T375 `/report` live row.** From fable, `/bug test T377` once (eqemu
   allows ONE report per zone session — `CanUseReport`); SELECT from
   eqemu's `reports`: `name` = character, `reported` = '', `reported_text`
   = the text. Record the row on the B023 strike.
5. **B027 — who sets `Oraclerogue`'s `gm` flag.** Before starting any
   client: SELECT the flag; grep eqemu's source for writers of
   `character_data.gm` and the `#gm` handler; check `quest`/`lua` scripts
   and `login` for an account-status promotion; start the client, log in as
   Oraclerogue, SELECT again after login and after one zone. Name the
   writer or narrow it to a phase. Strike or annotate B027.
6. `review/T377/README.md` (one section per item, PASS/FAIL first). Restore
   every server value; `down.sh`; delete the lock.

STOP: lock present; a proof FAILS (record, board, continue).
