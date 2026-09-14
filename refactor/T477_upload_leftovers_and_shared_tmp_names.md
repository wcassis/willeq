# T477 — hygiene: T475's leftovers (B690) and fixed /tmp names shared across slots in tests (B571)

Class B/P, **no lock, no Pi**, slot3. Read T475's Review
(`refactor/T475_delete_dead_texture_arrival_path.md`) and T465's
(`refactor/T465_headless_shutdown_load_flake.md`, section on the per-process
`mkdtemp()` config).

## B690 — leftovers no net sees (T475)

`AssetType::ZoneTexture` has no production user; `gpu_upload_thread.h`,
`work_priority.h` and fmt includes that only the deleted texture path needed
remain in the TUs T475 edited. Census each (prove zero users per the protocol's
grep rule), delete, cascade to a fixed point. If an enum value is persisted,
serialised or indexed by position anywhere (a table, a switch that must stay
exhaustive, a golden), say so before deleting it.

## B571 — 17 fixed `/tmp` names in 9 test files (T465)

Every slot — and parallel `ctest -j6` cases — writes the same paths. None fails
today, but it is the same latent race T465 removed from
`test_headless_shutdown.cpp`. Replace each with a per-process unique directory
(`mkdtemp()` or a `getpid()` suffix, removed in `TearDown()`), the T465 idiom.
Add a source census test that fails on a new literal `"/tmp/` path in `tests/`
without a unique component (allowlist only with a written reason).

## Proof

Standard verification bar, builds strictly in sequence; `ctest -j6` run TWICE
(the /tmp change is concurrency-facing); G-batch goldens for touched TUs;
`ZeroCallers`/`WriteOnlyFields`; visual gate; ARM; file sizes. Board B710–B719;
strike B690, B571.

File territory: the TUs/headers T475 left includes in, `AssetType`'s header and
users, the nine test files T465 named, the new census test. **Not** the camera
component / `camera_rig.h` (T474, slot1), **not** the equipment loader /
`bone_attachment.h` / `character_model_view.cpp` / held-item tests (T476,
slot2).

STOP: `AssetType::ZoneTexture` turns out to be positionally significant
(report); an image golden moves.

## Review

Slot3, branch `task/T477-upload-leftovers-tmp-names`, cut from `fable`
`3146fd44`. No lock, no Pi, no server contact. Every build and test run wrote
its own exit file under a unit-private scratch directory (`scratchpad/T477/`),
and every wait reported a non-zero elapsed time. No exit 143 anywhere.

**STOP conditions:** neither fired. `AssetType::ZoneTexture` is not
positionally significant (below), and no image golden moved.

### B690: census, deletions, cascade

Grepped `src/ include/ tests/ tools/ benchmarks/ docker/ scripts/ docs/` and
`tests/goldens/` for each symbol before deleting.

| remnant | users found | positional / persisted? | verdict |
|---|---|---|---|
| `AssetType::ZoneTexture` (`work_priority.h`) | `tests/test_work_priority.cpp` only | **No.** `WorkPriorityKey` is an in-process `uint32_t` compared with `<`; nothing persists, serialises, table-indexes, `switch`es over or decodes it. The only `>> 20` in `src/` is `door_manager_mesh.cpp`'s colour hash. Every enumerator has an explicit value, so deleting value 1 moves no other key. | **deleted**; value 1 is a commented gap, and `SameDepthAssetTypeOrdering` now chains `ZoneMesh < Door` |
| `gpu_upload_thread.h` + `work_priority.h` in the four `constrained_texture_cache*.cpp` TUs | none: no `GPUUpload*`, `Upload{Request,Result}*`, `WorkPriorityKey` or `AssetType` token in any of them | n/a | **deleted** (the whole `#ifdef EQT_HAS_GLES2` include block in each) |
| the fmt format header in `texture_atlas.cpp` | no `fmt::` token | n/a | **deleted** |

**Cascade:** a fixed point at once. The two headers are still included by
live users (`irrlicht_renderer.h`, `entity_prep_worker.cpp` and the zone TUs),
and no symbol lost its last user. The compile is the by-deletion proof: all
four cache TUs and `texture_atlas.cpp` built clean in step 1 and in the ARM
build, with no replacement include needed.

**Two neighbours are boarded, not deleted**, because T475 did not make them
dead and they are outside B690's scope:
- **B710:** `renderer_types.h`, `renderer_draw_types.h` and
  `renderer_event_receiver.h` include both headers without using either, and
  `irrlicht_renderer.h` includes `work_priority.h` without using it. Two TUs
  reach `WorkPriorityKey` only through those, so this is a multi-header edit
  with added direct includes, not a one-line delete.
- **B711:** `AssetType::Door`, `EntityTexture` and `LightEffect` have no
  production user either. Only `ZoneMesh`, `EntityMesh` and `Icon` are live.

### B571: the /tmp paths replaced

A new RAII helper, `tests/scoped_temp_dir.h` (`willeq_test::ScopedTempDir`),
is T465's idiom made reusable. It calls `mkdtemp()` under
`temp_directory_path()`, so each name is unique per call rather than just per
process, and it runs `remove_all` when it goes out of scope.

| file | before | after |
|---|---|---|
| `test_click_senders_packet_sender.cpp` | `temp/t376_clicks_recorded.sleq` | `ScopedTempDir("t376_clicks")` |
| `test_op_report_shape.cpp` | `temp/t375_op_report_shape.sleq` | `ScopedTempDir("t375_op_report")` |
| `test_self_death_sequence.cpp` | `temp/t360_spawn_appearance_recorded.sleq`, `temp/t360_death_burst_recorded.sleq` | one `ScopedTempDir` per case |
| `test_login_world_senders_packet_sender.cpp` | `temp/t379_login_recorded.sleq`, `temp/t379_world_recorded.sleq` | one per case |
| `test_direct_senders_packet_sender.cpp` | `temp/<name>` in `WithRecordedSession` (the five `t378_*_recorded.sleq`) | one per call, in the helper |
| `test_inspect_answer.cpp` | `temp/willeq_t171_{roundtrip,coexist,missing}.json` | one per case |
| `test_spells_eff_reader.cpp` | `temp/t70_truncated_spells.eff` | one in the case |
| `test_entity_viewer_regression.cpp` | output DIR `temp/p47_entity_viewer_regression` | fixture-owned `unique_ptr<ScopedTempDir>`, made in `SetUp()` |
| `test_eqe1_composite_open.cpp` | output DIR `temp/t284_composite_open` | one in the case |
| **not in B571:** `test_eqe1_loader.cpp` | `"/tmp/test_bad_magic.eqe1"`, `"/tmp/test_truncated.eqe1"`, `"/tmp/test_v2_pack.eqe1"` | one `ScopedTempDir` per function |
| **not in B571:** `test_constrained_renderer.cpp` | 13 `"/tmp/test_constrained_presets_*.json"` + `"/tmp/nonexistent.json"` | a fixture member `tmp_` and `tmpPath(leaf)` |
| **not in B571:** `test_eqe1_alpha_companion_mip.cpp`, `test_alpha_blend_companion.cpp` | `"/tmp/" + name + ".eqe1"` | a fixture member `tmp_` in each |
| **not in B571:** `test_pcap_step_through.cpp` | `"/tmp/pcap_step_through.log"` | `"/tmp/pcap_step_through_<pid>.log"`, kept after the run (deviation 2) |

`temp/` above is `temp_directory_path()`. That makes 17 B571 names plus 20
bare-literal sites. B571's census had grepped only `temp_directory_path`, so
it could not see the literals. The census test below found them.

**The race is real on this host, not hypothetical.** After this unit's
standalone runs at 06:39, `/tmp/test_bad_magic.eqe1`,
`/tmp/test_truncated.eqe1` and `/tmp/test_v2_pack.eqe1` carried an mtime of
**06:32**, and `/tmp/pcap_step_through.log` one of **06:07**. This slot's runs
logged their own mkdtemp paths
(`/tmp/test_eqe1_loader_invalid_jEoU3C/test_bad_magic.eqe1`), so those writes
came from another checkout running the pre-T477 tests in the same hour. They
were left untouched. No `ScopedTempDir` directory from this slot's runs was
left behind.

### The census test: red under mutation, green without

`TmpPathCensus` (`tests/test_tmp_path_census.cpp`, registered in
`tests/cmake/core_utils.cmake` beside `test_data_path_routing`, no link
dependencies, `DISCOVERY_TIMEOUT 120`) has two cases.

- **`EveryTempPathInTestsHasAPerProcessComponent`**
  - Reads every `.cpp/.cc/.h/.hpp` under `tests/` and strips comments.
  - Finds every string literal starting with `/tmp` and every
    `temp_directory_path` call.
  - The C++ statement holding each site must carry `getpid`, `mkdtemp`,
    `mkstemp`, `XXXXXX` or `ScopedTempDir`. A test NAME does not count: the
    same test in another slot has the same name.
  - Five waivers, each with a written reason: `/tmp/capture` (a parsed CLI
    value), `k09-navmesh` (a setter/getter string), `deriveChrArchivePath`
    (pure string derivation), `test_hotkey_manager`'s `oss.str()` (its
    `getpid` is one statement earlier) and `test_willeq_database`'s TMPDIR
    fallback (its `getpid` is one statement later).
  - A stale waiver fails the case. So does a scan that sees too little: at
    most 200 sources or at most 15 unique sites.
  - The needles are spelled in pieces, so the file scans itself with no
    self-exemption.
- **`TheClassifierMatchesTheSitesT477Fixed`**
  - Pins both B571 shapes verbatim as flagged: a bare literal, and a join
    split across two lines, with its line number.
  - Pins the accepted shapes as accepted: a mkdtemp template, a getpid suffix
    split across lines, and a helper join.
  - Pins that a test-name suffix is still flagged, that comments are ignored,
    and that `"/tmpfoo"` is not `/tmp`.

| run | state | result |
|---|---|---|
| first run on the fixed tree | one unconsidered site: `test_willeq_database.cpp:24`, `tmp ? tmp : "/tmp"` | FAILED, naming it. It is a directory fallback whose file name carries `getpid()` one statement later, so it got a waiver. The "unique" floor was lowered from 25 to 15 against the measured 22. |
| fixed tree | clean | **2/2 OK**: `350 sources, 37 temp sites: 22 unique, 15 waived` |
| `tests/t477_mutation_probe_tmp.cpp` added (in no target). Line 2 is a comment naming a `/tmp` path, line 8 a fixed literal, lines 9-10 a fixed-name `temp_directory_path` join, line 11 a `getpid` literal. | half the bug back | **FAILED**, naming **lines 8 and 10 only**: the comment was ignored and the getpid site counted as unique (22 → 23). `351 sources, 40 temp sites` |
| probe deleted | clean | **2/2 OK**, back to 350 / 37 / 22 / 15 |

The probe is not in the commit.

### Standalone runs of every touched test binary (after step 1)

All green:

| binary | cases |
|---|---|
| `test_tmp_path_census` | 2 |
| `test_work_priority` | 13 |
| `test_click_senders_packet_sender` | 5 |
| `test_op_report_shape` | 6 |
| `test_self_death_sequence` (reads goldens G07 + G08) | 14 |
| `test_login_world_senders_packet_sender` | 7 |
| `test_direct_senders_packet_sender` | 17 |
| `test_inspect_answer` | 17 |
| `test_spells_eff_reader` | 25 |
| `test_constrained_renderer` | 74 |
| `test_eqe1_alpha_companion_mip` | 3 |
| `test_alpha_blend_companion` | 6 |
| `test_pcap_step_through` | 4 |
| `test_eqe1_composite_open` | 2 |
| `test_eqe1_loader` (not gtest) | 14/14 PASS |

None skipped where a temp path is written: `WriteDetailedLogToFile`,
`SingleRaceCompositeRunOpensOnePack` and `LoadFailsLoudlyOnWrongSize` all ran.
`test_entity_viewer_regression` ran inside the suite (below).

### Goldens

No order or statement golden pins a TU this unit touched: grepping
`tests/goldens/` and the golden tests for `constrained_texture_cache`,
`texture_atlas` and `work_priority` returns nothing. The one touched test that
reads goldens, `test_self_death_sequence` (G07, G08), ran 14/14 standalone.

### Verification (builds strictly in sequence)

| step | result |
|---|---|
| 1 `cmake --build build -j24` | **PASS**, exit 0 (about 900 s). This followed one `cmake -S -B` regeneration with no option change, needed to add the new target. No warning in any touched file. |
| 3 `build-gles2-x86.sh` (after step 1's exit file) | **PASS**, exit 0 (about 705 s). `build-gles2/bin/willeq` is dated 06:51, before either ctest run. |
| 2 `ctest -j6`, run 1 (06:51-06:56) | **4776/4777**, ctest exit 8. The one red was **DocCitations**, and this unit caused it: the B690 strike put the fmt format header's name in backticks, and the checker read it as a path that does not exist. The prose was reworded (as T475 had written it), and `check_doc_citations.py` then ran standalone at 1,317 ok, 0 fail, 44 skip. Everything else was green, including `visual_gate` (27.4 s), `ZeroCallers`, `WriteOnlyFields`, `FileSizeCeiling`, `DocClaims`, `DocCounts`, `CommentClaims`, both `HeadlessShutdownTest` cases and both `TmpPathCensus` cases. |
| 2 `ctest -j6`, run 2 (06:58-07:03, load 12-18 from other slots) | **PASS**: `100% tests passed, 0 tests failed out of 4777`, exit 0. It had the same binaries as run 1; only the doc prose changed in between. It includes everything above plus `DocCitations` and both `EntityViewerRegressionTest` legs (their output directory is now a `ScopedTempDir`). There were 18 skips in both runs, all environment-gated cases outside this unit's files. |
| 4 visual gate | **PASS** in both runs; `git diff 3146fd44 -- tests/goldens` is empty |
| 5 `build-arm-noble.sh` (after run 2) | **PASS**, exit 0 (about 1,000 s). `build-arm-noble/bin/willeq` is a fresh 32-bit ARM EABI5 ELF dated 07:20. The four cache TUs and `texture_atlas.cpp` compile there too without the deleted includes. |
| 6 `check_file_sizes.sh fable` | **PASS**: 21 ok, 0 warn, 0 fail. The two new untracked files went through `--files`: `scoped_temp_dir.h` 68 lines, `test_tmp_path_census.cpp` 286 lines, both ok. |
| G-batch goldens for touched TUs | none exist (see Goldens); `test_self_death_sequence` G07/G08 14/14 |
| `/tmp` after both suite runs | No `ScopedTempDir` directory from any touched test is left. Both runs' pcap cases wrote pid-suffixed logs (`_292388` at 06:53:33 inside run 1, `_310848` at 07:00:06 inside run 2), which this unit then deleted. The fixed `/tmp/pcap_step_through.log` (06:51:07) and the three fixed `test_*.eqe1` files predate run 1, so another checkout still running the pre-T477 tests wrote them. They were left alone. |

### Board

- **B571** struck. The strike names the twenty extra literal sites, the
  helper and the census.
- **B690** struck. The strike carries the positional-significance finding and
  the cascade result.
- **B710** new (B, hygiene): unused `work_priority.h` / `gpu_upload_thread.h`
  includes in four renderer headers.
- **B711** new (B, unreached): `AssetType::Door`, `EntityTexture` and
  `LightEffect` have no production user.
- B712–B719 unused.

### Deviations (flagged)

1. **The /tmp fix went past B571's nine files, into five more test files.**
   - B571's census grepped only the `temp_directory_path` shape.
   - The plan's census test (a literal `"/tmp/` without a unique component)
     fails on twenty sites in `test_eqe1_loader.cpp`,
     `test_constrained_renderer.cpp`, `test_pcap_step_through.cpp`,
     `test_eqe1_alpha_companion_mip.cpp` and `test_alpha_blend_companion.cpp`.
   - Fixing them was the only way to land the census green without
     waiving live fixed names.
   - Tests only; no production file outside B690's list changed.
2. **The pcap step-through log is a `getpid()` suffix that is NOT removed.**
   Reading that log afterwards is the whole point of
   `WriteDetailedLogToFile`, so deleting it in teardown would defeat the case.
   It only runs when a capture exists.
3. **The census also covers `temp_directory_path()`, not just literals.** The
   plan named the literal shape. The seventeen B571 names are all the other
   shape, and a census blind to them would not guard the bug it closes.
4. **Two output directories are now deleted after their test**
   (`p47_entity_viewer_regression`, `t284_composite_open`). They used to
   persist in `/tmp`, where a manual rerun could inspect them. Each failure
   message still prints the full command to rerun by hand.
5. **B710 and B711 are boarded, not deleted.** Neither was made dead by T475,
   and B710 is a multi-header edit in the renderer's shared headers.
6. **Run 1 was red on this unit's own prose (DocCitations), and it was fixed
   rather than waived.** Run 2 is the clean pass.
7. **`fable` moved during the unit** to `09c5472f` (T474, T476). The only file
   both changed is `refactor/BOARD.md`, and its new hunks start at line
   10,064, well below this unit's (9,379, 9,651, 9,670+), so a merge should be
   hunk-clean. B710–B719 are unused on the tip.
8. **Temporary file.** `tests/t477_mutation_probe_tmp.cpp` was created for the
   red run and deleted before both suite runs. It is not in the commit.
