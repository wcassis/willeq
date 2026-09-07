# T455 — the zero-caller census, re-taken over the whole tree

Class B (the dominant defect class on this project is implemented-but-
unreached and duplicated-then-drifted; T366/T414/T429/T442/T448/T452
each found more by hand), slot1, no server. Read `tests/test_reachability_sweep.cpp`,
`scripts/refactor/check_write_only_fields.py` and the other census
scripts, T366's and T414's §Reviews (the census method: identifiers
counted tree-wide over `src/ include/ tests/ tools/ benchmarks/
docker/`, comments stripped, then each candidate re-grepped by hand),
T448's §Review (its 1.62M-occurrence identifier census).

1. Build the census as a SCRIPT (`scripts/refactor/check_zero_callers.py`):
   every free function and every public/protected member function
   declared under `include/` and `src/`, with its occurrence count
   outside its own declaration/definition, comments stripped. Report
   the zero-caller set. Exclude by rule, not by hand: entry points,
   gtest bodies, event-dispatch table targets (read the bridge intent
   table and the command registries — a function reached only through
   a table IS reached), virtual overrides, and operators.
2. For each zero-caller hit: delete it (with its tests if they only
   pin the dead code, docs struck), or if it is a feature that was
   never wired (the T39/B024 pattern), board it with the evidence and
   leave it — do not wire features in this unit. Say the split.
3. The script joins the hygiene label as `ZeroCallers` with an
   EXEMPT table (reason per row, as `WriteOnlyFields` does) so the
   count ratchets to zero and stays there.
4. Docs: strike every mention of deleted symbols (DocClaims/
   module_map/CLAUDE.md census numbers re-measured).

## Proof

Build (background), `ctest -j6` (no expected reds), the new net red on
the pre-fix tree (the hit list) and green after, G05/G06 in place
(deletions only), visual gate standalone + ARM (nothing moves), file
sizes, doc nets. New items B476+ (T449/T454 allocate from B474+/B476+ —
say which you used).

STOP: an image golden moves; a deleted symbol turns out reachable via a
table you did not model (then fix the rule, not the hit).
