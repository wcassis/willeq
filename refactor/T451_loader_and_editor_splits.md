# T451 — split the next four TUs riding the ceiling (structural)

Class B (structural), slot1, no server. `src/client/graphics/eq/s3d_loader.cpp`
(1,336), `src/client/graphics/eq/animated_mesh_scene_node.cpp` (1,287),
`tools/zone_line_editor.cpp` (1,303), `include/client/state/event_bus.h`
(1,269). Read T446/T447's §Reviews (the split recipe: contiguous line
ranges, shared header, no behaviour change, outputs byte-identical),
Batch K/L conventions for component TUs, the census/golden pins that
name these TUs (`WriteOnlyFields`, `DirectSenderCensus`, G05/G06,
module_map COVERAGE), and `event_bus.h`'s structure (event type enum +
payload structs — a header split by payload family with an umbrella
header that keeps every existing include working).

1. Split each by functional phase (loader: archive/index, geometry,
   materials/textures; scene node: build, animate, render; editor:
   I/O, edit ops, UI; event_bus.h: enum + per-family payload headers
   + umbrella). Largest resulting TU under 1,000. Every include site
   keeps compiling unchanged (the umbrella header).
2. Byte-identical proof: the loader and scene node are exercised by
   the visual gate (goldens + `eqe1MappedBytes` must not move) and the
   `PackReproducible` chain; the editor's output on a fixture file is
   diffed before/after.
3. Docs/nets: module_map (TU table + COVERAGE rows), CLAUDE.md census
   numbers re-measured, DocClaims/DocCitations green.

## Proof

Build (background), `ctest -j6` (no expected reds), visual gate
standalone + ARM (nothing moves), file sizes, doc nets, G05/G06 zero
insertions/reorders. No board strikes; new items B468+ (T450/T431
allocate from B467+ — say which you used).

STOP: an image golden or a memory pin moves; a G05/G06 reorder.

## Review

**Branch** `task/T451-loader-editor-splits`, base `62c79e1e` (fable tip,
verified at checkout). Slot1, `build/` RelWithDebInfo, GLES2 Irrlicht from the
slot's own `third_party/irrlicht-gles2-x86` (cache checked). No server, no
eqemu lock taken (T431 holds it). `include/client/graphics/irrlicht_renderer.h`
was not touched — T448 owns it.

Full evidence, including the four verbatim-proof files and the editor's
before/after stdout, is in `/mnt/projects/fable/review/T451/`.

### The split table

All four follow T446/T447's recipe: bodies are contiguous line ranges of the
original, moved verbatim; the only edits are the ones a TU boundary forces.

| old file | lines | new files | lines |
|---|---|---|---|
| `src/client/graphics/eq/s3d_loader.cpp` | 1,336 | `s3d_loader.cpp` — the static empties, both `loadZone` overloads, `loadZoneFromPreParsed`, `extractZoneName`, `loadLights`/`loadLightsFromArchive` | 228 |
| | | `s3d_loader_characters.cpp` — `loadCharacters`, `flattenSkeleton`, `deriveChrArchivePath` | 436 |
| | | `s3d_loader_skinning.cpp` — the file-local `Mat4`, `applySkinning`, `buildAnimatedSkeleton` | 407 |
| | | `s3d_loader_objects.cpp` — `loadObjects` | 237 |
| | | `s3d_loader_textures.cpp` — the three texture loaders | 127 |
| `src/client/graphics/eq/animated_mesh_scene_node.cpp` | 1,287 | `animated_mesh_scene_node.cpp` — `EQAnimatedMeshSceneNode` lifecycle, animate, render, frame/loop API | 546 |
| | | `animated_mesh_skinning.cpp` — `applyBoneTransformsToMeshImpl` + the SIMD normal helper | 375 |
| | | `animated_mesh.cpp` — `EQAnimatedMesh` | 232 |
| | | `animated_mesh_bones.cpp` — bone world queries, hand-bone lookup | 209 |
| | | `include/client/graphics/eq/animated_mesh_skinning.h` (seam) | 26 |
| `tools/zone_line_editor.cpp` | 1,303 | `zone_line_editor.cpp` — ctor/dtor, `setDebugArea`, `init`, `run`, `printUsage`, `main` | 148 |
| | | `tools/zone_line_editor/zle_render.cpp` — the seven draw passes and the HUD | 323 |
| | | `tools/zone_line_editor/zle_io.cpp` — zone geometry, portal data, zone-line JSON load/save | 322 |
| | | `tools/zone_line_editor/zle_input.cpp` — `handleInput` + the two view transforms | 317 |
| | | `tools/zone_line_editor/zone_line_editor.h` (seam: `ZoneLineBounds`, `EditorState`, the event receiver, the class) | 251 |
| `include/client/state/event_bus.h` | 1,269 | `event_bus.h` — umbrella: the six includes, the `EventData` variant, `GameEvent`, `class EventBus` | 259 |
| | | `event_payloads_ui.h` — UI, hotbar, spell visuals, diagnostics | 245 |
| | | `event_payloads_social.h` — chat, combat, group, time of day, pet, windows | 240 |
| | | `event_payloads_entity.h` — player, entity, door, zone | 227 |
| | | `event_payloads_inventory.h` — inventory, audio volumes, loot, trade | 171 |
| | | `event_types.h` — `enum class GameEventType` | 170 |
| | | `event_payloads_spell.h` — spells, buffs, skills, weather, zone lifecycle | 136 |

**Largest resulting file 546 lines**, against the plan's 1,000 bar. 5,195
ceiling-riding lines became 22 files. `check_file_sizes.sh --all`: **1,181
files, 0 warn, 0 fail** (1,162 at T447 — +19 is exactly this unit's new files).

**Every include site still compiles unchanged.** `event_bus.h` includes all six
of its parts, and neither `s3d_loader.h` nor `animated_mesh_scene_node.h`
moved, so nothing outside the four families was edited for the split itself.

### What the boundaries forced, and nothing else

A sorted line-multiset diff of each original against the union of its
replacements. Files in `review/T451/*_lines_only_in_original.txt`.

- **`s3d_loader.cpp`: ZERO lines lost.** Every line survives.
- **`animated_mesh_scene_node.cpp`: one line** — `static void
  applyBoneTransformsToMeshImpl(`. It was a file-static and the split put its
  two callers (`EQAnimatedMesh::applyAnimation` and
  `EQAnimatedMeshSceneNode::applyAnimation`) in different TUs, so it lost the
  `static` and gained a declaration in the seam header. This is the only new
  header in the two `src/` splits and the only external-linkage change in the
  unit.
- **`event_bus.h`: three lines** — the `// Event data structures` banner and
  its two rules, superseded by a per-family banner in each new header.
- **`tools/zone_line_editor.cpp`: nineteen lines** — the eighteen method
  signature lines (each is now a declaration in the header plus a
  `ZoneLineEditor::`-qualified definition in a TU) and the one default argument
  `const std::string& maps_path = ""`, which may be stated once and is now
  stated in the header. The editor's comparison is whitespace-normalised
  because out-of-lining de-indented the bodies one level; nothing else in them
  changed.

The `Mat4` struct stayed with `applySkinning` rather than moving to a header:
its sixteen uses are all inside that one function's TU (checked by grep before
the split), so it needs no external linkage.

### cmake changes

No target was added, removed, renamed or relinked; no test registration
changed; no `GTEST_DISCOVER_TESTS` call moved.

- `CMakeLists.txt` — the nine `s3d_loader.cpp` entries and the five
  `animated_mesh_scene_node.cpp` entries each became the family list
  (targets: `willeq_core`, `model_viewer`, `entity_composite_builder`,
  `zone_line_editor`, the hwcaps benchmarks, and the rest).
- `tests/cmake/graphics.cmake` (2 + 2) and `tests/cmake/zone_integration.cmake`
  (1) — the same.
- `zone_line_editor` gained its three new sources and
  `${CMAKE_SOURCE_DIR}/tools` on its include path, so the seam header resolves
  as `"zone_line_editor/zone_line_editor.h"` — T447's convention.

### The behaviour proof

| target | comparison | result |
|---|---|---|
| loader + scene node | `scripts/ci/visual_gate.sh` (3 zones + 3 memory-coverage passes) | **=== VISUAL GATE: PASS ===**; no golden pixel and no memory pin moved |
| loader + scene node | `PackReproducible`, `TexturesReproducible` | **PASS**; `data/willeq.eqe1.sha256` and `data/textures.sha256` unchanged and not in the diff |
| `zone_line_editor` | usage/exit-1 path, base binary vs split binary | **BYTE-IDENTICAL** |
| `zone_line_editor` | full stdout on `befallen`, `qeynos2`, `gfaydark` under Xvfb | **all three BYTE-IDENTICAL** (21/21/23 lines) |

The editor's "before" is a real binary, not an argument: `git archive HEAD` of
the base commit into a scratch tree, configured against this slot's Irrlicht
and built with `--target zone_line_editor`.

**Deviation, flagged: the editor's SAVE path could not be exercised, and this
is a property of the tool, not of the split.** The plan asked for the editor's
output on a fixture file diffed before and after. `saveZoneLineData()` is
reachable only from `handleInput()`'s Ctrl+S arm, and the editor takes input
through the patched Irrlicht device's **evdev** reader rather than X11 events —
a run under Xvfb prints `evdev: No keyboard found`, so `xdotool` cannot reach
it and there is no non-interactive entry point. The three zone runs above do
cover `loadZoneGeometry()`, `loadPortalData()` and `loadZoneLineData()`, i.e.
all of `zle_io.cpp` bar that one method; the save path's proof is the
structural one (a verbatim contiguous move, in the multiset diff above).
Boarded as **B468**.

### Verification

Host load average was **22–30** throughout (three slots working); every gate
number below carries that.

| step | result |
|---|---|
| `cmake --build <slot>/build -j24` (background, exit file) | **exit 0**, ~27 min. No exit 143, so no truncation scan owed |
| `ctest --test-dir <slot>/build -j6 --output-on-failure` | **4667/4669**, 2 failed, 653 s — both resolved below |
| `ctest -L hygiene` re-run after the doc edits | **8/8 PASS** (`FileSizeCeiling`, `DocCitations`, `DocClaims`, `DocCounts`, `TexturesReproducible`, `PackReproducible`, `WriteOnlyFields`, `CommentClaims`) |
| `scripts/build-gles2-x86.sh` | **exit 0** |
| `scripts/ci/visual_gate.sh` (standalone) | **PASS** |
| `scripts/build-arm-noble.sh` | **exit 0** |
| `check_file_sizes.sh fable` | 17 checked, **17 ok, 0 warn, 0 fail** |
| `check_file_sizes.sh --all` | **1,181 files, 0 warn, 0 fail** |
| goldens | untouched — no golden, `.sha256` or `.eqe1` file is in the diff |
| G05/G06 | untouched — neither golden's TU is in the diff; zero insertions, zero reorders |

The two reds in the full suite:

- **`DocCitations`** — a real defect this split created, fixed in place.
  `refactor/BOARD.md:1011` cited `include/client/state/event_bus.h:702` for
  `SwimmingStateChangedData`, which is now `event_payloads_spell.h:113`. A
  second one appeared from this Review's own doc edit (a literal
  `#include "client/state/event_bus.h"` in a code span reads as a citation to a
  path that does not exist); rephrased. Green on re-run.
- **`HeadlessShutdownTest.NonLifecycleCommandIsHeldNotDroppedOrApplied`** — the
  T360 load-artefact class. It produced EMPTY client output under `ctest -j6`
  at load 22–30 and **passes standalone on the same binary** in 5.06 s. Not a
  regression: it does not touch the loader, the scene node, the editor or the
  event bus.

### Docs and nets

- **`scripts/refactor/check_doc_counts.py`** — the `GameEventType enumerators`
  net now counts from `include/client/state/event_types.h`. **This mattered**:
  the enum moving out of `event_bus.h` would have made `count_enumerators()`
  return 0 against an unchanged doc, i.e. a silent net rather than a red one.
- **`tests/test_reachability_sweep.cpp`** — `eventEnumerators()` reads
  `event_types.h`, and the "skip the file that DEFINES the enum" guard in
  `EveryEventIsPublishedAndConsumed` moved with it.
- **`tests/test_movement_component.cpp`** — the `SwimmingStateChangedData`
  field-list guard (T347/B029) reads its struct from `event_payloads_spell.h`.
  It reads the header text, not the type, so it would have gone VACUOUS
  (`ASSERT_NE(npos)`) rather than red had the path not moved; it is red-on-move
  by design and duly failed nothing because it was repointed.
- **`docs/module_map.md`** — two new sections (`## S3D loader`,
  `## Animated character mesh`) with their TU tables, a `zone_line_editor` row
  in the tools table, and the `--all` census 1,162 → **1,181**.
- **`include/client/state/CLAUDE.md`** — the EventBus section now carries the
  six-header table and the rule for adding an event (enumerator in
  `event_types.h`, payload in its family header, variant alternative in
  `event_bus.h`).
- **`CLAUDE.md`** — the M00 paragraph learned this unit and the re-measured
  census.
- **`refactor/BOARD.md`** — the one citation repointed; **B468** opened (the
  editor's untestable save path). No board strikes. **B468 is the only new
  item; B467 was left free for T450/T431.**

### What surprised me

`s3d_loader.cpp` appeared in **twelve** CMake source lists and
`animated_mesh_scene_node.cpp` in **seven** — the two files are compiled into
`willeq_core`, four tools, three benchmarks and four test targets. A split that
looks like five files is a nineteen-site cmake edit, and a missed site is a
link error rather than a quiet wrong build, which is the good failure mode.
