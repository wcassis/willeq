# B1025-H0 — official Titanium hotbar oracle (partial capture)

Raw evidence: `/home/user/kb-evidence/kb-h0-hotbar-oracle/`. This is an incomplete oracle leg, not an implementation specification or a parity verdict. The official client ran on oracle lane 2 with paired eqemu lane 2 locked during capture. The temporary character-zone change was restored, the inventory snapshot matched byte-for-byte, the stack was shut down, and the lock was released (`db-before.tsv`, `db-after-restore.tsv`, `inventory-before.tsv`, `inventory-after-restore.tsv`).

## Measured facts

- A 0.117-second spell-gem press released without a cursor clone and attempted a cast, producing the character's level error (`gestures.tsv:2`, `spell-short-contact.png`). A 3.074-second hold produced a cloned gem icon by about 1.1 seconds after mouse-down at the capture's 10 fps resolution (`gestures.tsv:3`, `spell-hold-contact.png`). These observations bound the pickup threshold but do not determine its exact millisecond value.
- The gem clone was placed on a hotbar slot (`gestures.tsv:4`). A `/hotbutton H0 1 /say hi` command created a social button on the cursor and its placement replaced `Page1Button5=H2` with `Page1Button5=E12` (`gestures.tsv:5-6`, `persistence.md`). A later page-2 social became `Page2Button1=E13`; a cloned main-inventory item became `Page2Button2=F22` (`persistence.md`, `character-before-page2.ini`, `character-after-item2.ini`). The complete letter-code taxonomy is not established.
- A 3.286-second hold on Actions–Melee Bash and a 4.045-second hold on occupied hotbar Bash each showed a clone about 1.0 seconds after mouse-down in the 10-fps video (`gestures.tsv:12,17`, `ability_crop_contact.jpg`, `hb_crop_contact.jpg`). The measured timings are frame-resolution bounds, not a common exact threshold.
- Actions–Melee Bash placed in button 1 as `J10`; Skills > Kick > Make Hotkey placed in button 2 as `J30` (`gestures.tsv:13-15`, `persistence.md`). A direct hold on the Skills row was not measured; the supported observed route used the Make Hotkey control (`103_skill_make_hotkey.png`).
- A held first bag-slot Rusty Dagger displayed a tooltip but no clone during 3.512 seconds; this specific hold was not supported in the observed session (`gestures.tsv:16`, `bag_crop_contact.jpg`). The later `F22` assignment came from the backpack itself in main inventory slot 22, not from that dagger (`gestures.tsv:16`, `persistence.md`, `130_item_source_bag.png`).
- Picking up a hotbar slot removed its INI key; placing into an empty slot added the key at its destination; placing Bash on occupied Kick displaced Kick to the cursor, and clicking the world dismissed Kick without altering the new Bash assignment (`gestures.tsv:17-20`, `persistence.md`). This is replacement-with-cursor, not a persisted two-slot swap.
- The assignment keys are in the separate `Testcaster_WillEQ Private Server.ini`, **not** `UI_Testcaster_WillEQ Private Server.ini`. The latter contains window geometry and visibility (`persistence.md`, `UI-character-final.ini:129-157`, `character-final.ini:203-213`). This corrects the card's assumed INI location.
- PageRight and Shift+2 changed Hotbar 1's visible page label to 2 (`pages.tsv:2,7`). Official Options stills show Hotbar 1 default buttons `1`–`0`, pages `Shift+1`–`Shift+0`, and toggle `Alt+H`; all corresponding defaults in Hotbars 2–4 are unbound (`pages.tsv:3-6`).
- Pressing key 5 with the social on Hotbar 1 button 5 displayed “You say, 'hi'” (`activation.tsv:4`, `41_social_key5.png`). Right-clicking that social opened Edit Social (`gestures.tsv:8`, `42_social_rightclick.png`). The item hotbar key press had no established use effect (`activation.tsv:5`).
- Pressing key 2 with Bash on Hotbar 1 button 2 produced the client's no-target attack error, not proof of a successful ability use (`activation.tsv:7`, `129_bash_key2_log.png`, `testchar-chat.log`).
- A decoded packet trace was saved, but because the stream decoder began mid-session, no absence-of-network-traffic claim is made (`persistence.md`, `session2.opcodes.tsv`).
- All ledger video/still references exist: `ledger_check.txt` reports `broken_references=0`. This verifies references only, not the omitted acceptance scenarios.

## Not captured / not verified

- Socials-window hold pickup; direct Skills-row hold pickup; a successful bag-item clone; exact millisecond thresholds; reachable “Make hot button” context outside the observed Skills > Make Hotkey control. The observed positive holds were 10-fps frame-bounded only.
- Unambiguous opening and independent page behavior of Hotbars 2–4. A temporary F11 binding conflicted in red in Options, so its behavior is not an oracle result (`pages.tsv:8`). The original `eqclient.ini` was restored after the client exited.
- Click activation of spell/item/social/ability hot buttons, a successful click/key activation of spell/item/ability, activation of a spell no longer memorized, and packet-level proof that assignment is local-only.

## Evidence and cleanup checks

- `python3 /home/user/kb-evidence/kb-h0-hotbar-oracle/check_ledgers.py` → `rows=34 broken_references=0` (`ledger_check.txt`).
- The second capture's lane-2 restore with the world down produced byte-identical character/currency, Testcaster inventory, and Testchar inventory snapshots (`db-before-second.tsv`, `db-after-second-restore.tsv`, `inventory-before-second.tsv`, `inventory-after-second-restore.tsv`, `inventory-testchar-before.tsv`, `inventory-testchar-after-restore.tsv`). The stack was shut down and the exact task-owned lock released (`cleanup_lane2.sh`).
- No code, goldens, budgets, baselines, or `CLAUDE.md` were modified by this leg.

## Current acceptance audit

| Criterion | Status | Evidence / remaining work |
|---|---|---|
| A — every source/clear gesture has a measured threshold or demonstrated unsupported result | Not met | Spell, main inventory, ability, occupied hotbar and bag-slot holds have video (`gestures.tsv:2-3,9,12,16-17`). Socials-window hold and direct Skills-row hold have not been attempted; the positive holds have only frame-resolution bounds. |
| B — default page and toggle keys from official Options stills | Met | Hotbar 1 and Hotbars 2–4 rows in `pages.tsv:3-6` and their cited stills. |
| C — assignment INI encoding from real diffs | Partly met | `persistence.md` and the captured per-character INIs show page/slot/source transitions. Independent window 2–4 assignments and their encoding remain unverified. |
| D — zero broken ledger references | Met | `ledger_check.txt` (`rows=34 broken_references=0`). |
| E — seeded DB restored | Met | Three byte-identical second-capture snapshot pairs listed above; lane 2 stack stopped and task-owned lock released. |
| F — docs-only MR | Pending publication | The proposed MR diff contains only this README and `refactor/BOARD.md`; publication and CI status must be checked separately. |
