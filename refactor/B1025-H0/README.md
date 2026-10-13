# B1025-H0 — official Titanium hotbar oracle (partial capture)

Raw evidence: `/home/user/kb-evidence/kb-h0-hotbar-oracle/`. This is an incomplete oracle leg, not an implementation specification or a parity verdict. The official client ran on oracle lane 2 with paired eqemu lane 2 locked during capture. The temporary character-zone change was restored, the inventory snapshot matched byte-for-byte, the stack was shut down, and the lock was released (`db-before.tsv`, `db-after-restore.tsv`, `inventory-before.tsv`, `inventory-after-restore.tsv`).

## Measured facts

- A 0.117-second spell-gem press released without a cursor clone and attempted a cast, producing the character's level error (`gestures.tsv:2`, `spell-short-contact.png`). A 3.074-second hold produced a cloned gem icon by about 1.1 seconds after mouse-down at the capture's 10 fps resolution (`gestures.tsv:3`, `spell-hold-contact.png`). These observations bound the pickup threshold but do not determine its exact millisecond value.
- The gem clone was placed on a hotbar slot (`gestures.tsv:4`). A `/hotbutton H0 1 /say hi` command created a social button on the cursor and its placement replaced `Page1Button5=H2` with `Page1Button5=E12` (`gestures.tsv:5-6`, `persistence.md`). A later page-2 social became `Page2Button1=E13`; a cloned main-inventory item became `Page2Button2=F22` (`persistence.md`, `character-before-page2.ini`, `character-after-item2.ini`). The complete letter-code taxonomy is not established.
- The assignment keys are in the separate `Testcaster_WillEQ Private Server.ini`, **not** `UI_Testcaster_WillEQ Private Server.ini`. The latter contains window geometry and visibility (`persistence.md`, `UI-character-final.ini:129-157`, `character-final.ini:203-213`). This corrects the card's assumed INI location.
- PageRight and Shift+2 changed Hotbar 1's visible page label to 2 (`pages.tsv:2,7`). Official Options stills show Hotbar 1 default buttons `1`–`0`, pages `Shift+1`–`Shift+0`, and toggle `Alt+H`; all corresponding defaults in Hotbars 2–4 are unbound (`pages.tsv:3-6`).
- Pressing key 5 with the social on Hotbar 1 button 5 displayed “You say, 'hi'” (`activation.tsv:4`, `41_social_key5.png`). Right-clicking that social opened Edit Social (`gestures.tsv:8`, `42_social_rightclick.png`). The item hotbar key press had no established use effect (`activation.tsv:5`).
- A decoded packet trace was saved, but because the stream decoder began mid-session, no absence-of-network-traffic claim is made (`persistence.md`, `session2.opcodes.tsv`).
- All ledger video/still references exist: `ledger_check.txt` reports `broken_references=0`. This verifies references only, not the omitted acceptance scenarios.

## Not captured / not verified

- Combat-ability and Skills-row pickup; an actual item inside a bag; exact pickup thresholds for each kind; hotbar-slot move/swap/clear and world-drop semantics; reachable “Make hot button” context.
- Unambiguous opening and independent page behavior of Hotbars 2–4. A temporary F11 binding conflicted in red in Options, so its behavior is not an oracle result (`pages.tsv:8`). The original `eqclient.ini` was restored after the client exited.
- Click activation of spell/item/social/ability hot buttons, activation of a spell no longer memorized, and packet-level proof that assignment is local-only.

## Evidence and cleanup checks

- `python3 /home/user/kb-evidence/kb-h0-hotbar-oracle/check_ledgers.py` → `rows=23 broken_references=0` (`ledger_check.txt`).
- `diff -u db-before.tsv db-after-restore.tsv` and `diff -u inventory-before.tsv inventory-after-restore.tsv` produced no differences at lane cleanup; `scripts/eqemu/down.sh` exited zero; `eqemu_lock_show` returned empty after release.
- No code, goldens, budgets, baselines, or `CLAUDE.md` were modified by this leg.
