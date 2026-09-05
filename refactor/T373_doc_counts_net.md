# T373 — a regression net for hand-maintained doc counts (B130); DocClaims over the board's LIVE entries (B131)

Class P, slot2, no server. Read the B130 and B131 board text and T371's
§Review (the six drifted counts and the 39 legitimate struck-ledger hits).

## Leg A — B130

Every count an orientation doc states must be checked against the tree or
generated from it. Add `scripts/refactor/check_doc_counts.py` (ctest
`DocCounts`, label `hygiene`, next to `DocClaims`): a table of (doc, regex
capturing the stated number, a measuring function over the tree). Measuring
functions: enumerator count of `eqt::input::InputAction` and of
`HotkeyAction` (parse the enum body), `registerCommand({` call count in the
registering TU, `RendererIntent` variant count (parse the `std::variant<…>`),
component count per family (`include/client/<family>/` headers), the
zone-load step count (`loadZoneStep` functions). Do NOT net the suite total
or the `owner_` census — T371 already replaced those with the measuring
command; assert instead that the doc names the command and not a number.
Planted fault: change one enum count in a doc → FAIL naming the doc line,
the stated and the measured number; revert. Strike B130.

## Leg B — B131

`DocClaims` skips `refactor/BOARD.md` and the batch close-outs because
struck entries legitimately name retired things. Extend the scanner to the
board with a rule instead of an exemption: scan only LIVE entries (a bullet
that does not start with `- ~~`) and only their own lines (until the next
`- ` bullet or heading); struck entries and the close-outs stay out. Show
the pass line reporting how many live entries were scanned; plant a
retired name into a live entry → FAIL; revert. Strike B131.

## Proof

`ctest -R 'Doc'` + `ctest -j6`, file sizes on the two scripts.

STOP: none expected.
