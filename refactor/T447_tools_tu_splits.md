# T447 — split the three largest tools TUs (structural)

Class B (structural), slot2, no server. `tools/hwcaps/gles2_shader_perpixel_benchmark.cpp`
(1,463), `tools/willeq_db_builder.cpp` (1,444), `tools/wld_dump.cpp`
(1,436) ride the 1,500 ceiling. Read T446's §Review (the split recipe:
contiguous line ranges, shared fixture/header, one target with several
sources; names preserved), T385 (tools under the ceiling), each tool's
CMake target, and any golden/census that pins a tool's output
(`PackReproducible`/`TexturesReproducible` for the db builder — its
OUTPUT must stay byte-identical: prove with the pinned sha256s).

1. Split each tool by functional phase into 2–3 TUs plus a header, one
   target each; no behaviour change. Largest resulting TU under 1,000.
2. `willeq_db_builder`: rebuild the db/pack outputs the tests pin and
   show the pinned hashes unchanged (do NOT install a new shared pack —
   build to a scratch path and compare).
3. Docs/nets: module_map (tools section, if the T444 COVERAGE table
   covers tools/ — check), README tool list, DocClaims/DocCitations.

## Proof

Build (background), `ctest -j6` (no expected reds), file sizes, the
hash comparisons, doc nets green. No board strikes; new items B465+
(T431/T445 allocate from B459+/B465+ — say which you used).

STOP: a tool's pinned output hash changes.
