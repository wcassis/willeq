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

(executor fills in)
