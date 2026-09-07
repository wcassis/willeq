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
