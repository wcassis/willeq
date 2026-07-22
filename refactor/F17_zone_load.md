# F17 — Split zone loading out of irrlicht_renderer.cpp

The final Batch F unit: move `loadZoneSequential` (~2,665 lines, the largest
single method in the codebase) and its zone-lifecycle siblings out of
`src/client/graphics/irrlicht_renderer.cpp`. Pure move — no behavior change,
no interface change, no reformatting.

**Explicitly NOT this unit:** decomposing loadZoneSequential's body into
per-step helper methods. That requires new private method declarations
(header changes) and breaks the move-only verifier guarantee. It is recorded
as a candidate follow-up (Batch G) in the review. The method's internal
`// ── Step N ──` banners (Steps 0–13) already provide navigation.

## Scope — two new files under `src/client/graphics/` (enumerate by NAME)

**renderer_zone_load.cpp** (~2,700 lines — WARN accepted, must stay under
the 3,000 FAIL ceiling):
- `loadZoneSequential` — moved verbatim, all step banners and #ifdef guards
  intact.

**renderer_zone_lifecycle.cpp** (~700 lines):
- `unloadZone`
- `setZoneEnvironment`
- `buildZonePlaceholder`
- `setupInstantScene` (and any other contiguous zone-lifecycle setup methods
  in that region — enumerate by reading the cluster; the loading-screen and
  camera/lighting clusters are NOT in scope)
- The zone-ready state methods immediately after loadZoneSequential
  (`setExpectedEntityCount`, `notifyEntityLoaded`, `setNetworkReady`,
  `checkAndSetZoneReady`) — these were left behind by F21 as zone-loading
  state management; they belong here. `setWeather` sits among them —
  judgment: weather is environment state, include it only if contiguity
  makes leaving it messier; document either way.

## Steps

1. Locate all boundaries by grep; produce the mapping in the review BEFORE
   moving. When in doubt, leave + document.
2. Replicate namespace wrapper + include patterns (F14–F22 reviews; expect
   many forward-declaration gaps — this method touches nearly every
   subsystem; budget for several build-fix iterations of missing includes).
3. Move verbatim; preserve every #ifdef guard and step banner exactly.
4. File-local statics / extern declarations: move with sole callers (F18
   pattern), duplicate only if genuinely shared (F21 pattern) — document.
5. Add both files to `GRAPHICS_SOURCES` in CMakeLists.txt.

## Verification

- `cmake --build build -j24` clean.
- `ctest --test-dir build` — green modulo documented exceptions in
  refactor/test_baseline.txt. Zone/graphics integration tests
  (test_zoning_graphics_integration, ComprehensiveZoneTest, DoorLoading)
  MUST pass — they exercise this exact path.
- `python3 scripts/refactor/verify_move_only.py BASE..HEAD` — PASS or
  documented deviations only.
- `bash scripts/refactor/check_file_sizes.sh --files <two abs paths>` —
  renderer_zone_load.cpp WARN accepted, must NOT reach FAIL (3,000);
  renderer_zone_lifecycle.cpp ok.
- irrlicht_renderer.cpp shrinks by ~3,300+ lines.

## Review

(executor fills in)
