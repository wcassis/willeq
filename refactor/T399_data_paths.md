# T399 — every shipped data file resolves through one path rule (B235)

Class B (reliability: the client works only when started from the repo
root), slot2, no server. Read B235 (the ~40-file census), T397's §Review
Leg A (`eqt::ResolveDataPath()` — CWD first, `$WILLEQ_DATA_ROOT`, the
executable's parents), `include/client/data_paths.h`.

1. Re-take the census on your tip (`grep -rnE '"(config|data|assets)/[^"]*"'
   src/client include/client tools`), one row per literal: file, loader,
   what happens on a miss today (silent default / WARN / fatal).
2. Route every load through `ResolveDataPath()` — including the two
   weather files T397 left (T396 has merged; they are yours now) and
   `data/willeq.db` (the version check stays). One commit per family
   (core/app, config/*.json loaders, textures, audio/index, others). The
   miss behaviour does not change in this unit unless it is SILENT — a
   silent default becomes an ERROR log (say which ones).
3. Tests: extend `CwdElsewhere` (T397) to a table over every routed
   literal: from a temp CWD the file resolves and its bytes equal the
   repo's (pre-fix failing for each family); plus a source-reader pin:
   no bare `"config/…"`/`"data/…"` literal outside `ResolveDataPath`
   call sites (grep-based, the census as a test).
4. `willeq --help` / a headless `--script` run started from `/tmp` must
   reach zone load with the same log as from the repo root (compare the
   two logs' data-file lines).
5. Strike B235; update `CLAUDE.md`'s startup notes if they say "run from
   the repo root" (DocClaims/DocCounts stay green).

## Proof

Build, `ctest -j6` (no expected failures), pre-fix/post-fix per family,
file sizes; textures/graphics loads change → visual gate standalone +
ARM (CWD-first keeps the gate byte-identical — a moved golden is a STOP).

STOP: a load site cannot take a resolved path (a library API that opens
by name itself — name it and board it).
