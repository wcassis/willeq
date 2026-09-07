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

## Review

**Branch** `task/T450-willeq-db-reproducible`, base `35a14450` (fable tip,
verified at checkout). Slot2, `build/` RelWithDebInfo, GLES2 Irrlicht from the
slot's own `third_party/irrlicht-gles2-x86`. No server; T431 holds the eqemu
lock and nothing here opens a connection. Review evidence:
`/mnt/projects/fable/review/T450/README.md`.

### The consumer question (the STOP condition), answered first

**Nothing reads the wall-clock stamp, so the STOP did not fire.** A tree-wide
grep for `build_date` / `__DATE__` / `__TIME__` across `tools/ src/ include/
tests/ scripts/ docs/ benchmarks/` returns **one hit**: the line that writes
it. The client's only metadata read is
`SELECT value FROM metadata WHERE key='version'`
(`src/client/willeq_database.cpp:101`); `total_entries` has no reader either.
The key was therefore renamed rather than repurposed — a hash stored under a
key called `build_date` would be a lie.

### The change

| what | where |
|---|---|
| `setMeta("build_date", __DATE__ " " __TIME__)` → `setMeta("input_digest", …)` | `tools/willeq_db_builder.cpp` `writeMetadata()` |
| the digest itself: FNV-1a/64 over each input's LABEL + a NUL + `present`/`absent` + its contents | `computeInputDigest()`, same TU |
| the input set named ONCE, read by both the loaders and the digest | `std::vector<DbInput> inputs` + the `inputPath()` lookup in `main()` |
| the recipe script (`--check`, atomic install, refuses a missing EQ tree) | `scripts/build_willeq_db.sh` (new, 215 lines) |
| the pin | `data/willeq.db.sha256` (new) |
| the ratchet | `DbReproducible` in `tests/cmake/hygiene.cmake` |

Two deliberate properties of the digest, both written out in the TU:

- **It mixes the input's ROLE, never its path**, so `/mnt/projects/fable/main`,
  three slots and a staged install all produce the same database. Mixing the
  path would have made the file per-worktree and unpinnable — a new way to fail
  the same test.
- **A missing input still moves it** (`absent`), because a loader that cannot
  open its file WARNs and returns 0 rather than failing; without this a
  database built without `spells_us.txt` would claim the same provenance as one
  built with it.

FNV-1a rather than sha256 because the tool links nothing but sqlite3 (`dl`,
`pthread`) and this is a provenance stamp, not a security boundary. It is
64-bit and rendered as 16 hex characters.

The `inputs` table also killed the duplicated path expressions: `main()` used
to build each loader's path inline, so the eleven inputs existed only as
eleven `eqPath + "/…"` expressions. A new input cannot now be loaded without
appearing in the digest.

### Proof

**Two rebuilds of the TOOL, identical inputs, `touch`ed source between them**
(the `before` side produced by `git apply -R` of this unit's patch — never
`git stash`, which is shared across worktrees):

| | rebuild 1 | rebuild 2 | |
|---|---|---|---|
| before | `c5bc7e29…` | `cee904e8…` | **DIFFER** |
| after | `14f09de8…` | `14f09de8…` | **IDENTICAL** |

Both 3,788,800 bytes throughout.

**The new test, red then green** (same pin file on both runs, only the builder
reverted):

| run | result |
|---|---|
| `ctest -R DbReproducible` with the stamped builder | **FAILED** — "rebuild does not match the pinned sha256" + "the checked-in database differs from the rebuild" |
| `ctest -R DbReproducible` with the fix | **Passed**, 0.43 s |

**The shipped database WAS rebuilt**, once, and is committed together with its
pin in this unit's single commit. Its `sqlite3 .dump` differs from the previous
checked-in file by **exactly one line** — the metadata row,
`build_date` → `input_digest` — with all 32,414 data rows and the file size
unchanged. `metadata.version` is still 7; **`kWilleqDbExpectedVersion` was not
touched**, so no build tree is invalidated and steps 3–5 of the standing bar
are not owed (see below).

A useful by-product measured on the way: the checked-in database (built
2026-09-05) reproduced from today's inputs **byte-for-byte apart from that one
row**, so no input had drifted since it was last built by hand. The pin starts
honest.

### Verification

| step | result |
|---|---|
| `cmake --build build -j24` (background, exit file) | **exit 0**, 315 s to the exit file; no exit 143, so no truncation scan owed |
| `ctest --test-dir build -j6 --output-on-failure` | **4670 / 4670, 0 failed** |
| `DbReproducible` inside that run | Passed, 0.62 s (test #4669) |
| `check_file_sizes.sh fable` | 11 files checked, **11 ok, 0 warn, 0 fail**; `tools/willeq_db_builder.cpp` 337 → **441** lines |
| `check_doc_counts.py` | OK: every stated count matches the tree |
| goldens | untouched — no golden file is in the diff |

Steps 3–5 (`build-gles2-x86.sh`, the visual gate, `build-arm-noble.sh`) were
NOT run and are not owed: no shipped asset those trees read changed in content
(the database's 32,414 data rows are byte-identical) and
`kWilleqDbExpectedVersion` did not move, which is the one thing that
invalidates a stale tree (T304/T355). The shared EQE1 pack was not rebuilt,
reinstalled or read.

### Docs and nets

- **`CLAUDE.md`** — "Two shipped assets are derived" → **three**; the recipe
  table gains the `data/willeq.db` row (recipe, pin, ctest); a new paragraph
  says why the pin could not have existed before, what `input_digest` is, and
  that a data change is a rebuild + a new pin line in one commit and **never**
  a version bump.
- **`docs/module_map.md`** — the `hygiene` fragment's test list was three units
  stale (it named only T371/T373's four). Now **9**, naming `CommentClaims`,
  `TexturesReproducible`, `PackReproducible`, `WriteOnlyFields` and
  `DbReproducible` too.
- **`scripts/refactor/check_doc_counts.py`** — new row **"hygiene ctest
  tests"** (`count_hygiene_tests`, counting `ADD_TEST(NAME …)` in
  `tests/cmake/hygiene.cmake`), netted at that module-map sentence. This is the
  net that would have caught the staleness above; it caught it during this
  unit, when the first draft wrote 8 and the measurement said 9.
- **`README.md`** — the `WillEQDatabase` bullet names the recipe script, the
  pin and `DbReproducible`, and says not to rebuild by hand.
- **`tools/willeq_db_builder.cpp` header** — its table list claimed a
  `zone_points` table that has **no loader and has never existed** (zone lines
  ship as `data/zone_lines/*.json` from `tools/generate_zone_lines.py`), and
  listed `s3d_assets` without saying it is not in the shipped file. Corrected;
  the four tables the list omitted are named too.

### Board

- **B467** (B) opened and **struck in this unit** with the proof above: the
  non-reproducible stamp, the recipe, the pin and the test. It is T447's
  flagged finding, boarded and closed.
- **B468** (A, unconfirmed live) opened — see below. **Numbers used: B467 and
  B468.** T448 and T431 allocate from B467+ as well, so a merge may have to
  renumber one of these.

### The finding that is NOT this unit's scope (B468)

Taking the census of what is actually in the shipped database turned up two SQL
queries against a table that is not: `getAssetLocation()` and
`getAssetsInArchive()` (`src/client/willeq_database.cpp`) select `FROM
s3d_assets`, and `sqlite3 data/willeq.db .tables` has never listed it. The
table is created only by `loadS3dIndex()`, which runs only under
`--s3d-index`, and **nothing in the tree passes that flag** — no script, no
CMake rule, and no `s3d_index.json` is checked in. The first of those two
queries is reached: `RaceModelLoader::getArmorTexture()` calls it, and
`src/client/graphics/eq/mesh_building.cpp:210` and `:273` call that while
building a character mesh. So every on-demand armor-texture lookup fails
against a missing table rather than missing a row.

Deliberately NOT fixed here. Adding the table to the shipped database would
change the bytes this unit just pinned, for a consumer nobody has shown a
player reaches (armor textures may be coming from the EQE1 path in practice);
deleting the three functions is a separate deletion unit. The board entry says
what measurement closes it.

### Deviations

None from the plan. One judgement call worth flagging: the plan offered "a hash
of the input file set, or the newest input mtime rendered as a fixed string"
and said to store the input hash if nothing reads it. Nothing reads it, so the
digest is what shipped — and the mtime option would have been wrong anyway,
since a `git checkout` rewrites mtimes and would have made the database
per-clone.
