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
