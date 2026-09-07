# T458 — functions reached only by tests (T455's second list), and the cascade to a fixed point (B503)

Class B, slot1, no server. Read T455's §Review and
`review/T455/tests_only_callers.tsv` (322 functions whose only callers
are under `tests/`), `scripts/refactor/check_zero_callers.py` (the
census, its exclusion rules, the EXEMPT/BOARD_REASONS tables), B503
(the census is one pass; deleting exposes more — iterate).

1. Extend the census with a `--tests-only` mode that reports functions
   with zero production callers (src/ include/ tools/ benchmarks/
   docker/) and ≥1 test caller, and iterate BOTH lists to a fixed
   point after each deletion round (B503).
2. For each tests-only function: it is production code nobody reaches
   — delete it WITH the tests that only exercise it (a test of dead
   code is a false net), or board it as a never-wired feature (the
   T39/B024 rule) with the evidence, and say the split. Test-support
   helpers that live in `src/`/`include/` only for tests move under
   `tests/` instead of being deleted.
3. The `ZeroCallers` net gains the tests-only ratchet (EXEMPT rows per
   boarded item), pre-fix red on the current tree, green after.
4. Docs: strike every deleted symbol; census numbers re-measured;
   G05/G06 deletions only.

## Proof

Build (background), `ctest -j6` (no expected reds; the case count
DROPS by exactly the deleted dead-code tests — list them), visual gate
standalone + ARM (nothing moves), file sizes, doc nets. New items
B504+ (T456/T457 allocate from B479+/B490+ — collisions renumbered at
merge; say which you used).

STOP: an image golden moves; a "tests-only" function turns out reached
through a table or a virtual the census does not model (fix the rule).
