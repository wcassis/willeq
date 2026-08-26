# T273 — read a book / note item (T264 F2)

Slot: slot3. x86. OWNS eqemu briefly (noclobber) for a live read. Class A.
Territory: the read-item intent/handler T264 KEPT (its Review names them),
the OP_ReadBook sender (T? — grep `SendReadBook`/`ReadBook`), the text
window/panel that shows the result, CommandProcessor (`/readitem` or
similar — T138's triage may have named it), tests. Do NOT touch race names
(T271 slot1), spellbook (T272 slot2).

## Gap
An item of type Book/Note can be read (eqemu `Handle_OP_ReadBook` returns
`BookText_Struct` with the text); the client has the hop but no producer.

## Scope
1. Verify the wire form in eqemu (`BookRequest_Struct` {window, type,
   invslot, txtfile[20]} → `BookText_Struct`), and that our sender exists
   and is correct (T149/T150 class — if the sender is wrong, fix it).
2. Command first (`/read <slot|name>`), routed through the kept hop;
   display the returned text (chat window if no book panel exists — say
   which). Update the reachability test's expected set.
3. Tests: request bytes for a book in a given slot; the reply parsed and
   surfaced. Red/green.
4. Live: `#summonitem` a book (find one in the items the server has —
   e.g. a note quest item), `/read` it, text appears; observer not needed.

## Verification
Full ctest; Review; evidence review/T273/. Commit:
`T273: books and notes can be read`.

---

## Review (executor, 2026-08-26)

Branch `task/T273-read-book` (slot3), off fable `0a379d8a`.
Evidence: `/mnt/projects/fable/review/T273/` (README.md indexes every file).

### 1. Wire form -- verified, sender was already correct

Checked against `/mnt/projects/akk-stack/code`, not assumed:

| claim | source | verdict |
|---|---|---|
| `OP_ReadBook = 0x1496` | `utils/patches/patch_Titanium.conf:157` | matches `HC_OP_ReadBook` |
| request = `window \| type \| txtfile[]` (NUL, variable) | `common/patches/titanium_structs.h:2069` | our sender matches exactly |
| minimum payload 3 bytes | `titanium.cpp DECODE(OP_ReadBook)` = `DECODE_LENGTH_ATLEAST(structs::BookRequest_Struct)` | our shortest payload is 3 |
| server-side `txtfile` is `char[20]` | `common/eq_packet_structs.h:2798` + `strn0cpy` in the DECODE | filenames must stay < 20 chars; real ones are |
| `Handle_OP_ReadBook` demands `size == sizeof(BookRequest_Struct)` (28) | `zone/client_packet.cpp:13095` | satisfied: `SETUP_DIRECT_DECODE` reallocates to 28 and zero-fills, so the SoF+-only `invslot`/`target_id` Titanium never sends are 0 |
| reply = `window \| type \| booktext[]` | `titanium_structs.h:2061` + `titanium.cpp ENCODE(OP_ReadBook)` (drops the rich internal fields) | our 3-field parse is right |
| text is keyed by `books`.`name` = the item's `Filename` | `SharedDatabase::GetBook`, `Client::ReadBook` | drove the design below |

**No sender fix was needed** -- `SendReadBookRequest` was already byte-correct.
This is the T149/T150 discipline applied and coming back clean; the defect was
one layer up (no producer) and one layer down (nothing displayed the reply).

### 2. What was actually broken

1. **No producer.** `ReadItemIntent` was on `kBlockedIntents` (T264 F2).
2. **Nothing displayed the reply.** `NoteWindowOpened`'s only consumer,
   `IrrlichtRenderer::showNoteWindow()`, is one `LOG_INFO` and a return --
   there is no book/note panel. Even a working producer would have shown the
   player nothing.
3. **The item fields the request is built from were mis-mapped.**
   `titanium.cpp SerializeItem` emits `Book`(100) `BookType`(101)
   `Filename`(102); `TitaniumItemParser` read 100 as the TYPE, dropped 101,
   and called 102 `bookText` -- a name asserting it was the text. Latent (no
   consumer existed), fixed here because `/read` is that consumer.

### 3. Design decisions

- **Command first, through the kept hop.** `/read` ->
  `CommandProcessor::cmdRead` -> `ActionDispatcher::readItem` ->
  `EqActionHandler::readItem` -> `EverQuest::RequestReadItem` -> pushes
  `ReadItemIntent`, drained by the same `ProcessBridgeIntents()` a UI gesture
  would use. Same shape as T136's vendor Request\* recipe and T141's `/equip`.
- **Argument form copied from `/equip` deliberately**, including the rule that
  a token starting with a digit or `-` is parsed strictly as a slot (a typo'd
  number stays a slot error rather than silently becoming a name search), and
  that multiple matches are ambiguous only when the NAMES differ.
- **Gate on `Filename`, not on the `Book` flag.** Real content disagrees with
  the flag: item 15005 "Tattered Note" has `book=0` but a valid `filename`,
  and `Client::ReadBook` keys only off `txtfile`. Gating on the flag would
  have refused a note the server would happily have served.
- **Surface into the CHAT window** (`SystemMessage`), from
  `ZoneProcessReadBook` on the game thread -- works headless and with
  graphics, and is unit-testable without a renderer. `showNoteWindow()` is
  left as the hook for a future book panel.
- **Split on both `` ` `` and `^`.** eqemu's struct comment documents `` ` ``;
  the shipped `books` table uses both (`EP2MAGNote1` uses both in one text).
  Verified in the live DB, not guessed. Empty runs are collapsed so a
  `` ```````` `` separator does not emit blank chat lines.

### 4. Red / green (measured, each RED a real reversion + rebuild)

| RED | result |
|---|---|
| `/read` registration removed | 5 passed, **7 FAILED** |
| `SurfaceBookText()` call removed | 8 passed, **4 FAILED** |
| pre-T273 item field mapping | 11 passed, **1 FAILED** |
| GREEN (T273 sources) | **12/12** |

Full ctest **3024/3025** (`AudioManagerTest.PreloadCommonSounds`, the
documented ALSA host quirk). `test_reachability_sweep` passes with
`ReadItemIntent` removed from `kBlockedIntents`; the T264 Review's blocked
table and F2 row are updated in the same commit.

### 5. Live

Two sessions (both exit 0), zone befallen. Name form, slot form, no-argument
refusal, bad-name refusal and a real non-book refusal all behave as designed;
`filename='EP2MAGNote1'` / `filename='TDPbook1'` go out on the wire and the
replies (92 and 177 bytes) come back parsed and land in the chat window.

**Trap found:** run 1's text was GARBLED. `Client::ReadBook` calls
`GarbleMessage()` when the reader's skill in the book's language is below max,
and Testchar had **no `character_languages` rows at all**. `#set language 0
100` in run 2 gives clean text. Not a client defect -- but any future
book/lore work will hit it.

**Item staging note:** `#summonitem` puts items on the CURSOR and Testchar
already had one there, so the books were written into `inventory` directly
(server slots 23/24 = client slots 22/23) and restored afterwards.

### 6. Deviations

- **`include/client/graphics/ui/CLAUDE.md` was deliberately NOT edited.**
  T271 (slot1) and T272 (slot2) were running in parallel and that file is the
  natural place all three would add a command line; `/read` is discoverable
  through its registered help text (`/help`, category Inventory). Worth one
  consolidated doc line when the three merge.
- **`check_file_sizes.sh` was run against `0a379d8a`, not `fable`.** `fable`
  advanced to `3cf0d399` (T271 merged) mid-unit; against that ref the script
  also reports T271's `entity_renderer.cpp` 3341-line FAIL, which is not
  T273's. Against this branch's own base: **0 FAIL**, 7 pre-existing WARNs.
- Renaming `ItemInstance::bookText` -> `bookFilename` (and adding `bookFlag`)
  is slightly wider than "the read-item hop", but the old name was actively
  misleading about what `/read` puts on the wire. Both old members had exactly
  one writer and zero readers.
