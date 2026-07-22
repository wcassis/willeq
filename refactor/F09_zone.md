# F09 — Split zone geometry and zone change/teardown into eq_zone.cpp

Move the zone-lifecycle clusters out of `src/client/eq.cpp` (10,819 lines).
HIGH entanglement (teardown touches broad state) but still a pure TU move —
all state stays in EverQuest members. No behavior change, no interface
change, no reformatting.

## Scope (orig refs @ a0c02cd. Enumerate by NAME.)

Move to new `src/client/eq_zone.cpp`:
1. **Zone change / teardown cluster** (orig 12929–13333, ~7 methods):
   `RequestClientZoneChange`-family, `ZoneChange` processor(s),
   `RequestZoneChange`, `CleanupZone`, `DisconnectFromZone`,
   `ProcessDeferredZoneChange`, and the LevelUpdate handler if it opens that
   contiguous block (verify — if it's an unrelated stat handler, leave it).
2. **Zone geometry cluster** (orig 13525–13912, ~6 methods):
   `LoadZoneMap`, zone-lines/zone-sources loading, `CheckZoneLine`,
   `GetBestZ`, `FixZ`.

The gap between them (pathfinding cluster, orig 13334–13524) belongs to F08
(movement) — do NOT move it; document it as the boundary.

## Steps

1. Locate both clusters; list every `EverQuest::` definition; document the
   pathfinding gap left for F08.
2. Create `src/client/eq_zone.cpp`: header comment + pruned includes
   (hc_map.h, zone_lines/zone_bsp headers likely needed).
3. Move verbatim; preserve #ifdef guards exactly.
4. File-local static helpers: usual rule.
5. Add to `CLIENT_CORE_SOURCES` in CMakeLists.txt.

## Verification

- `cmake --build build -j24` clean.
- `ctest --test-dir build` — green modulo documented exceptions in
  refactor/test_baseline.txt. Zone-related tests (ComprehensiveZoneTest,
  DoorLoading, zone line tests) MUST pass — they exercise this exact code.
- `python3 scripts/refactor/verify_move_only.py BASE..HEAD` — PASS or
  documented necessary-include/de-static deviations only.
- `bash scripts/refactor/check_file_sizes.sh --files <abs>/eq_zone.cpp` — ok.
- eq.cpp shrinks by ~750+ lines.

## Review

**Note on line numbers:** the "orig refs" in the Scope section above (12929–
13913 @ a0c02cd) are from before Batch F's earlier units (F00–F14, F12 most
recently at c53ec34) already shrank `eq.cpp` from 19,676 to 10,819 lines.
Clusters were re-located by grepping for the named methods in the current
tree rather than trusting the stale line numbers; the enumerated method
names all match.

### Moved-method list

**Zone change / teardown cluster** (6 methods, eq.cpp lines 6539–6905 pre-move):
- `ZoneProcessRequestClientZoneChange(const EQ::Net::Packet&)`
- `ZoneProcessZoneChange(const EQ::Net::Packet&)`
- `RequestZoneChange(uint16_t, float, float, float, float)`
- `CleanupZone()`
- `DisconnectFromZone()`
- `ProcessDeferredZoneChange()`

Note: `ZoneProcessLevelUpdate` (immediately preceding this cluster) was
checked per the plan's caveat and confirmed to be an unrelated player-level
packet handler (updates `m_level`, entity level, game state), not part of
zone lifecycle — left in `eq.cpp`.

**Zone geometry cluster** (6 methods, eq.cpp lines 7098–7484 pre-move):
- `LoadZoneMap(const std::string&)`
- `LoadZoneLines(const std::string&)`
- `LoadZoneSources(const std::string&)`
- `CheckZoneLine()`
- `GetBestZ(float, float, float)`
- `FixZ()`

### Pathfinding boundary (left for F08)

Between the two moved clusters, eq.cpp lines 6906–7097 contain the
pathfinding cluster (`LoadPathfinder`, `FindPath`, `MoveToWithPath`,
`FollowPath`), marked with a pre-existing `// Pathfinding implementation`
comment. This was **not moved** — it belongs to unit F08 (movement). A
boundary comment was added at the end of the moved teardown block in
`eq_zone.cpp` documenting that this gap is intentionally left in `eq.cpp`.

### Helper analysis

No file-local static helpers were introduced or needed; both clusters call
only `EverQuest::` member methods and externally-declared free functions
(e.g. `HCMap::LoadMapFile`) already visible via existing headers. No
de-static deviations were required.

### Includes

New `eq_zone.cpp` needed a pruned but distinct include set vs. `eq.cpp`:
`client/hc_map.h`, `client/pathfinder_interface.h` (for the complete
`IPathfinder` type behind `m_pathfinder`'s `unique_ptr::reset()` in
`CleanupZone`), `client/spell/buff_manager.h` (complete `BuffManager` type
for `m_buff_manager->clearPetBuffs()`), `client/zone_lines.h`,
`client/graphics/eq/wld_loader.h`, `client/graphics/eq/pfs.h` (complete
`PfsArchive` type for `LoadZoneSources`; `eq.cpp` gets this transitively
through a longer include chain that isn't worth reproducing),
`client/bridge/game_state_bridge.h`, `common/logging.h`,
`common/net/posix_udp_transport.h`, plus `<chrono>`, `<cmath>`, `<cstring>`.
These are the two documented necessary-include deviations (`pathfinder_interface.h`,
`spell/buff_manager.h`) required only because `eq.cpp` pulls in the complete
types transitively through headers not needed by this file's own code.

### Line counts

- `eq.cpp`: 10,819 → 10,064 lines (−755).
- `eq_zone.cpp`: 801 lines (new).

### Verifier / build / test summary

- `cmake --build build -j24`: clean, no errors (only pre-existing unrelated
  `-Wpointer-arith` warnings in untouched code).
- `ctest --test-dir build`: 1190/1192 passed. 2 failures, both pre-existing
  and documented in `refactor/test_baseline.txt`:
  `AudioManagerTest.PreloadCommonSounds` (ALSA card-0 resolution on this
  host) and `RDPServerFrameTest.UpdateFrameMultipleTimes` (known rare
  segfault flake). Zone-specific tests (`test_door_loading`,
  `test_zoning_integration`, `test_zoning_graphics_integration`, and all
  `Comprehensive`/door/zone-line coverage) passed.
- `verify_move_only.py 06e6f5f9..HEAD`: **PASS** — move-only modulo
  boilerplate (+41/-0 lines, all `#include`/comment/brace boilerplate).
  `CMakeLists.txt` flagged as non-C++ and reviewed separately (single
  source-list addition).
- `check_file_sizes.sh --files eq_zone.cpp`: **ok** (801 lines, under the
  1,500-line ceiling).

### Deviations

- Two necessary-include additions not present verbatim in `eq.cpp`'s own
  include list (`client/pathfinder_interface.h`, `client/spell/buff_manager.h`,
  and `client/graphics/eq/pfs.h`) — required because `eq.cpp` obtains these
  complete types transitively through other headers it includes for
  unrelated reasons; `eq_zone.cpp` needs them directly. No code logic was
  changed.
- No scope creep: no reformatting, no renamed symbols, no interface
  changes.
