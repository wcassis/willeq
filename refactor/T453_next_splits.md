# T453 — the next TUs over 1,200 (structural)

Class B (structural), slot1, no server. Split, by the T446/T447/T451
recipe (contiguous ranges, shared header, no behaviour change, outputs
byte-identical, goldens/pins unmoved, G05/G06 zero insertions/reorders):
`src/client/graphics/door_manager.cpp` (1,365 — T438 already extracted
`door_manager_animation.cpp`; split the rest by create/mesh/state/
collision — T435's `door_manager_collision.cpp` exists) and every other
TU currently over 1,200 lines (measure at checkout with
`scripts/check_file_sizes.sh --all` or `wc -l`; list them in the
Review; T431 in slot3 may be editing camera TUs — skip any TU in
`src/client/graphics/player/` and say so). Largest resulting TU under
1,000.

Docs/nets: module_map TU table + COVERAGE rows, CLAUDE.md census,
DocClaims/DocCitations green. No board strikes; new items B470+
(T452/T431 allocate from B469+ — say which you used).

## Proof

Build (background), `ctest -j6` (no expected reds), visual gate
standalone + ARM (report host load), file sizes, doc nets.

STOP: an image golden or a memory pin moves; a G05/G06 reorder.
