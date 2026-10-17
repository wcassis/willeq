# B1082 (GitLab #402) — Character-select keyboard: measured behavior

Verified against the official Titanium client on the oracle rig (lanes 1/4,
xdotool + xdotool stills + the session's `capture.pcap` decoded with
`scripts/eqemu/oracle/vendor/eqpcap.py`). The packet capture is the ground
truth for anything the server sees; the stills are ground truth for what the
screen shows.

Sessions (evidence under `/home/user/kb-evidence/kb-b1082-charselect-keys/`):

- `oracle5_final/` — logged in as `Testchar`, char-select open. Sequence:
  Down×5, Up×5, Backspace, Delete×2, Return. `capture.pcap` decoded by
  `pcap_dump.py` / `pcap_entry.py`.
- `oracle4_control/` — F9 (camera), Escape, Return — proves key delivery and
  the Escape behavior.
- `oracleC_empty/` — logged in as `Testmelee` (the LAST occupied row),
  Down×3, Up×3 — the boundary test.

## What the official client does

1. **Up/Down move the selection one row and wrap at the ends — within the
   occupied rows only.** The selected-character name line (read at 4× zoom,
   `name_big_*.png` / `CW_*.png`) follows the highlight: plan 5 started on
   `Testchar` and, after the net one-Down of the sequence, EnterWorld carried
   `Testmelee` (the next occupied row down). Plan C, from the last occupied
   row, one Down moved the highlight to an OCCUPIED row again (the 3D model
   stayed up, `C_grid.png`): the highlight never rests on an empty "Create
   a New Character" slot. With a 3-character account this reads as wrapping
   through the occupied rows; the client's highlight does cycle the full
   8-button list but the name line and the model only ever reflect an
   occupied row, so the observable/enterable selection is bounded by the
   occupied rows. Implemented as wrap within `s.rows`.

2. **Navigation sends nothing to the server.** Plan 5's pcap: login handshake
   (~t23–24 s), then nothing until `OP_EnterWorld` — no packet of any kind on
   any Down/Up press. Plan C's pcap: zero C→S packets in the entire
   Down/Up sequence. The selection is a purely client-side highlight until
   Enter.

3. **Enter/Return sends `OP_EnterWorld` for the finally-selected character.**
   Plan 5's pcap: a single `OP_EnterWorld name='Testmelee'` at t≈296 s —
   exactly the row the arrows had left the selection on. Same 72-byte payload
   as the Enter World button's click path.

4. **Delete prompt (CSW_Delete_Button → "Are you absolutely sure you want to
   delete %1?")**: ESC answers No (B998, already implemented). Enter/Return
   do NOT answer the prompt and no keys navigate or type while it is up —
   plan 5's pcap shows nothing on the wire during the Backspace/Delete-while-
   -prompt steps, and the prompt closed on its own (~10 s timeout, B998)
   before the Return that entered the world. The official prompt only
   answers Yes/No via its buttons, ESC (No), or the timeout.

5. **Escape (no prompt up) returns to the server-select screen** — no quit
   confirmation prompt (`oracle4_control/07_esc_select-assertion-failed.png`:
   the server list, not a dialog). This is the existing host behavior
   (Escape is spent and leaves pre-zone); unchanged.

6. **Everything else is inert** on the select screen (letters, Backspace,
   Delete, Tab) — nothing reaches the server and nothing on the screen
   changes; the host spends them so they do not replay in-zone (B1085).
   The one exception is the creation screen's page-1 name field, which
   already owned its keys through `applyTextEntryKey` (B888).

## Where it lives

- `include/client/graphics/ui/char_select_screen.h` /
  `src/client/graphics/ui/char_select_screen.cpp`:
  - `moveSelection(s, delta)` — new: one occupied row, wrap at the ends,
    gated on `charSelectBlocked`/prompt/create like every other press.
  - `applyCharSelectKey` now RETURNS the `CharSelectAction` (it was `void`)
    because Enter/Return posts an EnterWorld request exactly like the
    `EnterWorld` click; `KEY_DOWN`/`KEY_UP` → `moveSelection`;
    `KEY_RETURN` → `pressEnterWorld` (the numpad Enter maps to the same
    code in the receiver). The creation-field path is unchanged, now nested
    under `s.create.isOpen`.
- `src/client/graphics/ui/char_select_host.cpp`: the key loop now routes the
  returned action through `handle(..., channel)` (same body as the click
  path); the Enter/Escape/Slash spend-and-hold block is untouched (still
  prevents in-zone replay).
- `tests/test_character_select.cpp`: `CharacterSelectKeys.*` — wrap without
  sending, Enter→OP_EnterWorld for the arrow-chosen row, prompt ignores
  everything but Escape.

## What was NOT measured (scope)

- Left/Right and Tab: the oracle sequences exercised Up/Down/Enter/Escape/
  Backspace/Delete; Left/Right produced no still-visible change in plan 5
  (no name-line movement) and no packets — treated as inert, consistent with
  the 1-D list.
- The exact per-keystroke highlight frame for a rapid burst (xdotool timing
  can drop frames); the boundary behavior is settled by the single-press
  plan C and the name-line reads.
