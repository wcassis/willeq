# T385 — the ceiling really is tree-wide: `tools/` under the check, `model_viewer.cpp` decomposed (B167)

Class P (structural — Will's first-order rule). Two legs, one slot, no
server. Read B167, `refactor/M00_ceiling_closed.md`, the K/L mechanism
(`refactor/K_component_extraction.md` §Mechanism) and Batch G's method
decomposition shape.

## Leg A — the check

`scripts/refactor/check_file_sizes.sh --all` adds `tools/` (and
`benchmarks/` if it holds `*.cpp/*.h`). The FAIL list on the current tip
is the census: report it. Every file over 1,500 is decomposed in Leg B or,
if it is a pure data table (the `createDefaultConfig` precedent), listed
in the allowlist WITH its reason and a one-line comment — the allowlist
has been empty since M00 and re-opening it needs the reason in the file.

## Leg B — `tools/model_viewer.cpp` (3,246 lines)

Read it first; write the split plan into the Review BEFORE editing
(functional seams: window/input, model loading, spell FX exercise (the
B160 caller), rendering loop, CLI). Extract by function into
`tools/model_viewer/<part>.cpp` + headers, `main` stays tiny; no behaviour
change — prove with the tool's existing tests if any, else a golden of
its `--help` and one headless run's log (record before, compare after).
Each extracted part ≤ 1,500. One commit per part.

## Proof

Build (tools build with the main tree), `ctest -j6` (`FileSizeCeiling`
now covers tools), `check_file_sizes.sh --all` 0 FAIL, file sizes. Update
the M00 close-out and `CLAUDE.md`'s "tree-wide" sentence (DocClaims /
DocCounts may net the directory list — check).

STOP: a tool file is a generated table (say which; allowlist with reason).

---

## Review

Executed in `wt/slot1` on `task/T385-tools-ceiling-model-viewer`, base `fable`
= **`6c351a88`** (verified at checkout). No file under `src/` or `include/`
was touched — the diff is `scripts/`, `tools/`, `CMakeLists.txt` and four docs.

Commits (in order):

| hash | what |
|---|---|
| `4c28993c` | Leg A: `--all` covers `tools/` and `benchmarks/` |
| `1a643730` | Leg B: `model_viewer.cpp` 3,246 -> 155 |
| `892967db` | `model_viewer_spell_bar.h` 1,965 -> 14 |
| `8e9af9b0` | new `tools/model_viewer/` files to mode 644 |
| `8d8e3073` | `entity_composite_builder.cpp` 1,861 -> 484 |
| `99db5fc1` | `gles2_fog_volume_benchmark.cpp` 1,711 -> 1,220 |
| (this commit) | M00 close-out, `docs/decisions.md`, `CLAUDE.md`, `BOARD.md`, this Review |

### Leg A — the census

`--all` scanned 866 files before this unit and 971 after (989 once the split
adds its own TUs). `benchmarks/` holds no `*.cpp`/`*.h`/`*.hpp` at all today —
only `audits/`, `contact_sheets/`, `paths/`, `pi_baseline/`, `pi_runs/` — so it
is listed and skipped; a directory that does not exist or holds nothing costs
nothing. `tools/retired/` is deliberately NOT pruned: its largest file is 474
lines, so excluding it would only hide a future regression.

**FAIL census BEFORE (at `6c351a88`, 971 files):**

```
FAIL  tools/entity_composite_builder.cpp: 1861 lines (> 1500)
FAIL  tools/hwcaps/gles2_fog_volume_benchmark.cpp: 1711 lines (> 1500)
FAIL  tools/hwcaps/gles2_zone_benchmark.cpp: 3060 lines (> 3000)
FAIL  tools/hwcaps/zone_bench_loader.cpp: 3280 lines (> 3000)
FAIL  tools/model_viewer.cpp: 3246 lines (> 3000)
FAIL  tools/model_viewer_spell_bar.h: 1965 lines (> 1500)
checked 971 file(s): 965 ok, 0 warn, 6 fail          (exit 1)
```

**SIX, not the five B167 counted by hand.** The sixth is
`gles2_fog_volume_benchmark.cpp` at 1,711 — the difference between a scan and a
hand count, which is the argument for the change in one line. Three of the six
were over the **3,000-line hard cap**, not merely the 1,500 ceiling; the tree
had files that would have failed even under the pre-M00 rules, unseen.

**FAIL census AFTER (989 files):**

```
FAIL  tools/hwcaps/gles2_zone_benchmark.cpp: 3060 lines (> 3000)
FAIL  tools/hwcaps/zone_bench_loader.cpp: 3280 lines (> 3000)
checked 989 file(s): 987 ok, 0 warn, 2 fail          (exit 1)
```

The two survivors are **T387**'s (slot3, dispatched in the same wave;
mid-unit scope note from the orchestrator). They are boarded as **B185** so the
RED `FileSizeCeiling` has an owner and is not read as a T385 regression. They
are NOT allowlisted: the allowlist reopens only for a pure generated data
table, and neither is one.

### The split plan, per file, and what landed

**`tools/model_viewer.cpp` 3,246 -> 155.** Functional seams as the plan named
them, plus a decomposition of the 933-line `main()`:

| file | lines | holds |
|---|---|---|
| `tools/model_viewer.cpp` | 155 | `main()` only |
| `model_viewer/model_viewer_state.h` | 265 | the seam: `EntityData`, the two enums, `MyEventReceiver`, ~145 `extern` globals, every cross-TU declaration |
| `model_viewer/model_viewer_state.cpp` | 131 | the global definitions |
| `model_viewer/model_viewer_startup.cpp` | 417 | CLI, usage banner, direct race-code load, device creation, subsystem init |
| `model_viewer/model_viewer_loop.cpp` | 304 | the render loop |
| `model_viewer/model_viewer_input.cpp` | 371 | `MyEventReceiver::OnEvent` — the whole keyboard map |
| `model_viewer/model_viewer_mesh.cpp` | 499 | texture upload, mesh build, variant combining, animation cycling |
| `model_viewer/model_viewer_entity.cpp` | 580 | entity JSON, model load, texture reload |
| `model_viewer/model_viewer_equipment.cpp` | 353 | weapon/shield bone attachment |
| `model_viewer/model_viewer_spell_test.cpp` | 456 | spell-test mode (the B160/B166 caller) + `initSpellTestMode()` |

`main()` is now 43 statements. Bodies were MOVED; the only edits are the ones a
function boundary forces (a `ViewerArgs` struct for four of `main()`'s locals,
`character` as an out-parameter, four `return 1` -> `return false`,
`MyEventReceiver::OnEvent` out of line). Indentation was normalised from brace
depth in the three files whose bodies changed nesting level, each verified
`diff -w`-clean before building.

**`tools/model_viewer_spell_bar.h` 1,965 -> 14** (umbrella): →
`model_viewer/spell_effect_db.h` 471 (resist/school/category enums, EDD emitter
parser, EFF parser, the joining database), `model_viewer/spell_bar_ui.h` 657
(`SpellBarEntry`, `CastingState`, `CastingBar`, `SpellBar`),
`model_viewer/model_viewer_fx.h` 903 (`ModelViewerFX`). Declarations verbatim;
the original path survives as an umbrella so no includer changes.

**`tools/entity_composite_builder.cpp` 1,861 -> 484** (usage + `main()`): →
`entity_composite/composite_builder.h` 173,
`entity_composite/composite_texture_io.cpp` 528 (BMP codec, shelf packing, seam
gutter, ETC1, mips, `.eqe1` writer), `entity_composite/composite_model.cpp` 420
(per-model texture resolve + sheet build), `entity_composite/composite_archives.cpp`
396 (archive discovery, signature registry, index merge). Which of the 30
`static` functions had to lose internal linkage was **measured** — 19 cross a
boundary, 11 do not and stayed `static`. Everything is inside `namespace ecb`
because `toLower`/`toUpper`/`applyVariant` are generic names in a binary that
links most of `src/client/graphics`.

**`tools/hwcaps/gles2_fog_volume_benchmark.cpp` 1,711 -> 1,220:** the ten GLSL
ES sources → `hwcaps/fog_volume/fog_volume_shaders.cpp` 509 + `.h` 15.

**Left to T387 (B185), split plan offered, not executed:**
`tools/hwcaps/zone_bench_loader.cpp` 3,280 and
`tools/hwcaps/gles2_zone_benchmark.cpp` 3,060. Note for whoever takes them:
`gles2_zone_benchmark` is the binary `scripts/ci/visual_gate.sh` targets, so
that split needs a gate run, not just a byte-compared log — it is the one file
in the six where a log golden is not sufficient evidence.

### Behaviour proof

Every split was proved by byte-comparing the tool's own output before and
after. Each baseline was captured **twice first** to establish that the logs
are deterministic (they are; the pairs were identical).

| tool | comparison | result |
|---|---|---|
| `model_viewer /tmp/nonexistent_eq_path` | 29 lines, exit 1 | IDENTICAL |
| `model_viewer <EQP1999> HUM -d 2` | 1,750 lines, exit 124 | IDENTICAL |
| `model_viewer --spell-test --gles2 --client <EQP1999>` | 76 lines, exit 124 | IDENTICAL |
| `entity_composite_builder` (no args) | 41 lines, exit 1 | IDENTICAL |
| `entity_composite_builder --race HUM --all-variants --eqe1 --verbose` | 162 lines, exit 0 | IDENTICAL once the output directory name is normalised — the only diff |
| ... its 16 produced files | 5 sheets x `.bmp`/`.json`/`.eqe1` + `composite_index.json` | **byte-identical by md5** |
| `gles2_fog_volume_benchmark --help` | 3 lines, exit 1 | IDENTICAL |
| ... its linked binary's string table | `strings -n 4 \| sort` | identical but for the 10 symbol names (`_ZL13FS_FOG_LINEAR` -> `FS_FOG_LINEAR`) and the new TU filename; the other 1,065 strings, every shader line included, match |

Timestamps were stripped before comparing (`sed -E 's/^\[[0-9:. -]+\]//'`);
exit codes were compared unstripped. Exit 124 is the 60 s timeout — those two
`model_viewer` runs open a window and loop forever, and the 1,750-line one
covers S3D load, variant categorisation, the three-stage character texture
merge, device creation, equipment archive load and the animated-node path.

The 16 md5-identical build outputs are the strongest of these: they exercise
the ETC1 encoder, an 11-level mip chain, the shelf packer, the seam inset and
the JSON index writer end to end.

**Where the evidence is weak, and it is said out loud:**
`gles2_fog_volume_benchmark` drives raw EGL over DRM or X11 and cannot run on
this headless VM at all — before and after it prints the same three lines and
exits 1. That proves it starts, nothing more; the string-table comparison is
what actually carries the claim for that file, and it is a data-move proof, not
a behaviour proof. The two `model_viewer` window runs are likewise proofs of
everything up to the first frame, not of the render loop's steady state.

### Proof steps

- `cmake --build build -j24`: **exit 0** (full tree, after every split).
- `ctest -j6`: **4,011 of 4,014 passed**. Failures:
  `AudioManagerTest.PreloadCommonSounds` (the known ALSA host quirk),
  `FileSizeCeiling` (the two T387 files — B185), and `DocCitations`, which
  caught a citation this unit invalidated: `BOARD.md` B166 cited
  `tools/model_viewer.cpp:2750` and the file is now 155 lines. Fixed by
  re-anchoring B166 to `tools/model_viewer/model_viewer_spell_test.cpp` and
  saying that 2750 was a line in the 3,246-line TU as it then stood.
  `DocCitations`, `DocClaims` and `DocCounts` are **3/3 green** after that.
  Two earlier `ctest -j6` invocations died at gtest DISCOVERY on two different
  binaries (`test_eq_action_handler_transforms_real`, then `test_gm_summon_find`)
  — cold-cache contention, not truncation: `--gtest_list_tests` on the named
  binary exited 0 both times, and warming the page cache over `build/bin/*`
  made the run complete. Protocol's third discovery-failure cause, confirmed.
- `check_file_sizes.sh --all`: **987 ok, 0 warn, 2 fail** (exit 1) — the two
  T387 files, as above.
- Visual gate / ARM / `build-gles2-x86.sh`: **skipped with evidence.** The diff
  vs the merge base touches no file under `src/`, `include/` or `tests/` and no
  shader the client compiles; the only `CMakeLists.txt` edits are three tool
  targets' source lists and include paths. `willeq` cannot differ. (The one
  hwcaps target this unit does touch, `gles2_fog_volume_benchmark`, is not a
  gate target — `gles2_zone_benchmark` is, and this unit does not touch it.)

### Deviations

1. **One commit for the `model_viewer` extraction, not one per part.** The
   plan asked for one commit per extracted part. The parts share a single
   state header, so no subset of them compiles on its own — the extraction is
   atomic. The per-file split is the table above; the other three files did get
   a commit each.
2. **Scope changed mid-unit.** The orchestrator's note moved
   `zone_bench_loader.cpp` and `gles2_zone_benchmark.cpp` to T387. Neither had
   been started; nothing was discarded. `gles2_fog_volume_benchmark.cpp` was not
   in either unit's list and was taken here rather than left ownerless.
3. **`docs/decisions.md` gained a dated entry** (2026-09-05). The plan named
   the M00 close-out and `CLAUDE.md`; the decisions file states the rule those
   two point at, and leaving it saying "src/, include/ and tests/" would have
   left the same stale-claim defect one file over. Flagged rather than absorbed.

### Surprises

- The `tools/` gap was worse than B167 recorded on both axes: six files, not
  five, and **three over the 3,000 hard cap**, not one. `--all` had been
  reporting a clean tree over a directory holding a 3,280-line file.
- `tools/model_viewer.cpp`'s `main()` was 933 lines on its own — a Batch G
  oversize method hiding in a directory with no ceiling.
- Renaming a variable across a moved block edited a **string literal**
  (`" character models"` became `" outCharacter models"`), and the only thing
  that caught it was the byte-compare — the build was clean and the tool ran.
  A regex rename over moved code is not a safe mechanical edit; the golden is.
- Files created in this worktree land as mode 775, so nine new source files
  went in as `100755` and needed a separate mode commit. Worth knowing before
  the next unit adds files here.
