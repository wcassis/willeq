# T450 — willeq.db becomes reproducible and pinned (T447's finding)

Class B (hygiene; the pack and textures are pinned, the db is not and
cannot be), slot2, no server. Read T447's §Review (`writeMetadata()`
stamps `build_date = __DATE__ " " __TIME__`, so every recompile changes
`data/willeq.db` with identical inputs; the proof had to normalise one
row), `tools/willeq_db_builder.cpp` + `tools/willeq_db/`, the
`PackReproducible`/`TexturesReproducible` tests and
`scripts/build_willeq_pack.sh` (the pin recipe: sha256 in `data/`, one
commit), the db version validation (willeq.db v7; a version bump
invalidates every tree — do NOT bump), and any consumer of the
`build_date` metadata row (grep; if nothing reads it, say so).

1. Replace the compile-time stamp with a deterministic value derived
   from the INPUTS (e.g. a hash of the input file set, or the newest
   input mtime rendered as a fixed string — pick what a consumer could
   use; if nothing reads it, store the input hash). Two builds from the
   same inputs are byte-identical: prove with sha256 across two
   rebuilds of the tool.
2. Pin `data/willeq.db.sha256` the way the pack is pinned, with a
   `DbReproducible` hygiene test that rebuilds to a scratch path and
   compares (label `hygiene`; must run without the shared pack).
3. Docs: the reproducibility section names all three pins; the
   COVERAGE/DocCounts rows learn the new test.

## Proof

Build (background), `ctest -j6` (no expected reds), pre-fix red (the
new test against the stamped builder: two rebuilds differ), post-fix
green, file sizes, doc nets. Board B467 opened and struck in the same
unit with the proof; new items B468+ (T448/T431 allocate from B467+ —
say which you used).

STOP: a consumer depends on the wall-clock stamp.
