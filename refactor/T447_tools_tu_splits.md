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

## Review

**Branch** `task/T447-tools-tu-splits`, base `005c7e24` (fable tip, verified at
checkout). Slot2, `build/` RelWithDebInfo, GLES2 Irrlicht from the slot's own
`third_party/irrlicht-gles2-x86`. No server, no eqemu lock taken (T431 holds
it). Nothing outside `tools/`, `CMakeLists.txt`, three docs and this plan was
touched; no file under `src/`, `include/` or `tests/` is in the diff.

### The split table

All three follow T446's recipe: ONE target, several sources, a shared header,
every name preserved. Bodies are contiguous line ranges of the original — the
only edits are the ones a TU boundary forces, listed per tool below.

| old TU | lines | new TUs | lines |
|---|---|---|---|
| `tools/willeq_db_builder.cpp` | 1,444 | `tools/willeq_db/db_builder.h` (seam: the 4 helpers + all 11 loader declarations) | 55 |
| | | `tools/willeq_db_builder.cpp` — the 4 helpers, `writeMetadata()`, `main()` | 337 |
| | | `tools/willeq_db/db_loaders_text.cpp` — the client's `*.txt`: spells, eqstrings, dbstrings, sound assets, animation sounds, music index, login strings | 555 |
| | | `tools/willeq_db/db_loaders_json.cpp` — the WillEQ/config `*.json`: item models, equipment models, race models, s3d index | 607 |
| `tools/wld_dump.cpp` | 1,436 | `tools/wld_dump/wld_dump.h` (seam: `WldDumpFragments`, the two phase functions, `getFragmentTypeName`, `dumpHex`) | 48 |
| | | `tools/wld_dump.cpp` — usage, `.s3d` extraction, WLD selection, `WldLoader` analysis, `main()` | 310 |
| | | `tools/wld_dump/wld_dump_core.cpp` — fragment-type names, hex dumper, and `dumpWld()`'s first phase (header, string table, fragment index + details table) | 267 |
| | | `tools/wld_dump/wld_dump_geometry.cpp` — 0x10, 0x36, 0x1B, 0x1C, 0x28, 0x2A, 0x35 | 403 |
| | | `tools/wld_dump/wld_dump_animation.cpp` — 0x12, 0x13, 0x14, 0x15, 0x17, 0x18, 0x37, 0x2F, 0x2D + the hex fallback | 563 |
| `tools/hwcaps/gles2_shader_perpixel_benchmark.cpp` | 1,463 | `tools/hwcaps/shader_perpixel/shader_perpixel.h` (seam: `EGLState`, `Vertex`, every declaration) | 106 |
| | | `tools/hwcaps/gles2_shader_perpixel_benchmark.cpp` — `BenchResult`, `runBenchmark()`, `main()` | 442 |
| | | `.../shader_perpixel/shader_perpixel_egl.cpp` — DRM/GBM + X11 EGL context, `compileShader`, `linkProgram` | 285 |
| | | `.../shader_perpixel/shader_perpixel_shaders.cpp` — the 13 GLSL ES sources under test | 521 |
| | | `.../shader_perpixel/shader_perpixel_scene.cpp` — alley geometry, checker texture, FP16 attenuation LUT, 4x4 matrices | 223 |

Largest resulting TU **607** lines, against the plan's 1,000 bar. 4,343 lines
of ceiling-riding TU became fourteen files, none over 610.

### What the boundaries forced, and nothing else

A per-file `sort`ed line-multiset diff of the original against the union of its
replacements was run for all three tools. **Every original line survives**; the
only lines the original has that the split does not are:

- the 15 `static` keywords on the db builder's shared helpers and loaders, the
  28 on the benchmark's, and `dumpHex`'s default argument `= 0` (a default
  argument may be stated once, and it is now stated in the header);
- everything else in the diff is an addition: the three new headers, the new
  file banners, and per-TU `#include` lines.

`dumpWld()` was the one real decomposition — 1,073 lines in one function. Its
sections were already independent loops over the same four locals, so
`WldDumpFragments` carries `fragPtr` / `remaining` / `fragmentCount` /
`strings` (plus `typeCounts`) across the seam and each phase re-binds them to
the ORIGINAL local names at its top, which is why the moved bodies needed no
edit at all. `typeCounts` is bound as a copy, not a const reference, because
the moved bodies read it through `std::map::operator[]`; the map is ~30
entries.

### cmake changes

Three `ADD_EXECUTABLE` source lists grew, and each of the three targets gained
`${CMAKE_SOURCE_DIR}/tools` on its include path so the seam header resolves as
`"willeq_db/db_builder.h"` / `"wld_dump/wld_dump.h"` /
`"hwcaps/shader_perpixel/shader_perpixel.h"`. No target added, removed,
renamed or relinked; no test registration changed.

### The output proof (STOP condition: a pinned hash moves)

Full evidence and the hash files are in `/mnt/projects/fable/review/T447/`.

| tool | comparison | result |
|---|---|---|
| `willeq_db_builder` | whole database, scratch path, same inputs incl. a synthetic `s3d_index.json` | **BYTE-IDENTICAL**, `e7a9a7fc…`, 3,805,184 bytes, after normalising the one `build_date` row |
| `willeq_db_builder` | `sqlite3 .dump`, 32,566 lines | one line differs: `metadata.build_date` |
| `willeq_db_builder` | stdout row counts (11 loaders, 32,417 entries) | identical, timings aside |
| `wld_dump` | stdout over 6 real archives + 2 error paths | **all 8 byte-identical**, exit codes included |
| `gles2_shader_perpixel_benchmark` | stdout, Xvfb/llvmpipe | every non-numeric line identical; whole log identical with numerics normalised |
| pack | `data/willeq.eqe1.sha256` | unchanged; `PackReproducible` **PASS** |
| textures | `data/textures.sha256` (67 files) | unchanged; `TexturesReproducible` **PASS** |

**Finding (flagged, not absorbed): `data/willeq.db` has never been
byte-reproducible, and it is not this unit's doing.** The plan asked for the
db builder's "pinned output hashes"; there are none — the two `.sha256` pins in
`data/` cover the pack and the textures, and no test, script or doc pins the
database. The reason is visible in the builder itself:
`writeMetadata()` stamps `setMeta("build_date", __DATE__ " " __TIME__)`, so
every recompilation of the tool changes the output file even with byte-identical
inputs and byte-identical code. That is why the proof above normalises that one
row rather than comparing raw sha256s, and it is the only reason it has to.
Worth a board item if anyone ever wants the shipped db pinned the way the pack
is; not opened here because nothing depends on it today.

### Verification

| step | result |
|---|---|
| `cmake --build build -j24` (background, exit file) | **exit 0** in 340 s — no exit 143, so no truncation scan was owed |
| `ctest --test-dir build -j6 --output-on-failure` | **4664/4664, 0 failed**, 187 s |
| `ctest -L hygiene` re-run after the doc edits | **7/7 PASS** (`FileSizeCeiling`, `DocCitations`, `DocClaims`, `DocCounts`, `TexturesReproducible`, `PackReproducible`, `WriteOnlyFields`) |
| `check_file_sizes.sh fable` | 14 files checked, **14 ok, 0 warn, 0 fail** |
| `check_file_sizes.sh --all` | **1,162 files, 0 warn, 0 fail** (1,129 at T444; +33 is this unit's 11 new files plus the tree's growth since) |
| goldens | untouched — no golden file is in the diff |

Steps 3–5 of the standing bar (`build-gles2-x86.sh`, the visual gate,
`build-arm-noble.sh`) were **not** run and are not owed: neither of those trees
builds any of these three targets (`build-gles2` builds `willeq` +
`gles2_zone_benchmark` only), no shipped asset changed, and
`kWilleqDbExpectedVersion` did not move.

### Docs and nets

- `docs/module_map.md` — new **`## Tools (tools/)`** section: the seven
  multi-TU tools (T385's three, T387's two, this unit's three) with their seam
  header and TU list, plus the `${CMAKE_SOURCE_DIR}/tools` include-path
  convention. The map had NO tools coverage at all before; `check_doc_counts.py`'s
  `COVERAGE` table does not reach `tools/` (checked — it lists eight
  `src/client/graphics/*` directories and nothing else), so this was a gap the
  nets could not have caught.
- `docs/module_map.md` + `CLAUDE.md` — the `--all` census re-measured
  1,129 → **1,162** files.
- `refactor/BOARD.md` — the one **DocCitations FAIL** the split created:
  the closed B245-era entry cited `tools/willeq_db_builder.cpp:1353`, which is
  now a 337-line file. Repointed to the call site (`:246`) and to the loader's
  new home (`tools/willeq_db/db_loaders_json.cpp:300`).
- `README.md` needed no change: its `tools/` line names the tools, not their
  TUs, and every tool keeps its name and its binary.

No board strikes. **No new board items were opened** (B465+ unused): the one
citation defect was fixed in place, and the `build_date` finding above is
recorded here rather than boarded because nothing in the tree depends on the
database being reproducible.
