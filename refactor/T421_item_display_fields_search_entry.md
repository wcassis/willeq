# T421 — the item display shows what it parses (B298, item half); the search box types (B365)

Class B ×2, slot3, no server. Read B298 (21 `ItemInstance` fields parsed
and never shown; the `SpellData`/`SkillData`/entity halves are separate —
board them as their own follow-ups with the census rows), B365, the
item display popup in `static_panels.cpp` (what it shows today), the
oracle `EQUI_ItemDisplay.xml` (+ `EQUI_Inspect.xml` if relevant), T413/T415's
XML-placement method, and the chat text-entry code (`ChatPanelState`,
the key routing T357/T363 documented) for Leg B.

## Leg A — B298 item fields

The official item display window lists the stats an item carries; fable
parses 21 of them and shows none. Table: field → the XML element/label
that shows it on the oracle (quote `EQUI_ItemDisplay.xml`; where the
window is free text, quote the official client's item-display text
format from an oracle screenshot under `review/T3xx/oracle/` if one
shows an item, else say "text layout unquotable" and follow the XML
element order). Draw every field the oracle shows, in its place;
fields the oracle does NOT show stay hidden (say which). Tests pre-fix
failing per field (static-panel seam: the text entries). Strike the
item half of B298; annotate the rest.

## Leg B — B365

The tradeskill search box needs text entry. Reuse the chat panel's
entry mechanism (focus + key routing + caret; T357/T363 documented the
focus gate) as a shared `TextEntryState` the chat AND the search box
use — do not write a second entry widget. Enter or the Search button
pushes the existing `SendRecipesSearch` intent with the text and the
trivial bounds; the favourites buttons push their existing senders.
`TradeskillButton` gains the enumerators (the hit test returns them).
Tests pre-fix failing: typing into the focused search box changes its
text; Enter publishes the search intent with that text; the chat entry
is unchanged (its existing tests stay green). Strike B365.

## Proof

Build (background), `ctest -j6` (expected red only if T420 (slot1) has
replaced the pack: `PackReproducible`/`eqe1MappedBytes`), pre-fix/post-fix,
visual gate standalone + ARM (no image golden should move), file sizes;
`ui_panel_capture` for the item display and the tradeskill window into
`review/T421/`.

STOP: none expected.
