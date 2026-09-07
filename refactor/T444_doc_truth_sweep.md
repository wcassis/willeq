# T444 — doc truth sweep after T428–T443 (stale docs are a first-class defect)

Class B (docs), slot1, no server. Read `CLAUDE.md` (the doc rules and the
truth nets: `DocClaims`, `DocCounts`, `DocCitations`, `check_doc_*.py`),
the §Reviews of T428, T430, T433–T443, `README.md`, `docs/module_map.md`,
`docs/decisions.md`, `docs/testing_progress.md` (or whatever the
progress/feature docs are called — list them), `docs/future/*.md`, the
per-directory `CLAUDE.md` files touched by those units
(`include/client/graphics/ui/CLAUDE.md` panel table: options, merchant,
bank, cursor rows; `src/client/graphics/CLAUDE.md`: GroundZ, door
collision, deleted fallback mesh node / Irrlicht stubs; audio: volume
steps; spell: colour source).

1. For each doc, every claim that T428–T443 changed is corrected in
   place with the unit cited (feature tables: options window, merchant/
   bank/cursor, item icons, door collision, night base, Froglok vision,
   volume round trip, guild list if T443 lands first — check `fable` at
   checkout and say what you saw). Counts (panels, event types, popup
   enumerators, vision levels, golden sizes G05/G06) re-measured, not
   copied.
2. Stale statements found that no unit fixed (the "implemented" claims
   the T363 rule forbids, features documented but unreached) are
   boarded, not silently rewritten into truth.
3. Extend the nets: every count you corrected by hand gets a row in
   `check_doc_counts.py`; every "X is implemented" claim you verified
   gets a `DocClaims` anchor if the mechanism supports it — so the next
   drift is a red, not a re-read.
4. `docs/future/` entries already delivered (T434 options, T436
   windows, T430 icons…) are struck with the unit, not deleted.

## Proof

`ctest -j6` (doc nets green pre- and post-), the new net rows red on the
pre-fix docs (show it with an exported patch), file sizes (docs are under
the ceiling too), no code changes except the checker tables. Board: new
items B457+ (T443 in slot2 allocates from B455+; say which you used).

STOP: none expected.
