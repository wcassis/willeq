# G01 — Per-step decomposition of loadZoneSequential

Decompose the 2,673-line `loadZoneSequential` method
(src/client/graphics/renderer_zone_load.cpp) into per-step private methods.
The hardest unit so far: step bodies move verbatim, but locals that cross
step boundaries must be identified and threaded through a context struct.
Sequencing, early-return/abort paths, and LoadingStatus updates must be
byte-for-byte behavior-identical.

## Design

- **Forward-declared context:** `struct ZoneLoadContext;` in
  irrlicht_renderer.h (private section); the struct is DEFINED at the top of
  renderer_zone_load.cpp holding every local that crosses a step boundary.
  This keeps the header light — private method declarations take
  `ZoneLoadContext&` (incomplete type is fine for references).
- **One private method per step banner:** Steps 0, 1, 2, 4, 5, 6, 7, 8, 9,
  10, 11, 12, 12b, 13. Naming: `loadZoneStep0_OpenEQE1(ZoneLoadContext&)`,
  `loadZoneStep1_ParseS3D(...)`, etc. (match each banner's own title).
- **Step 8 is ~800+ lines with sub-banners 8a–8f:** decompose it one level
  further into `loadZoneStep8a_...` … `loadZoneStep8f_...` called from a
  short `loadZoneStep8_Entities`. No method over ~500 lines.
- **Abort/error handling:** steps that can fail return `bool`; the driver
  preserves the exact current control flow (early returns, cleanup,
  LoadingStatus writes). Analyze the current method for every early-exit
  path BEFORE restructuring and list them in the review.
- `loadZoneSequential` itself becomes a short driver: build context, call
  steps in order, handle failures exactly as before.

## Steps

1. **Analysis first (goes in the review BEFORE editing):**
   a. Table of every step's line range and every early-exit/abort path.
   b. Table of locals that cross step boundaries (name, type, defined-in
      step, consumed-in steps) — these become ZoneLoadContext members.
      Locals used within a single step stay local to that step's method.
2. Define ZoneLoadContext in renderer_zone_load.cpp; forward-declare in
   irrlicht_renderer.h; declare the step methods (private).
3. Cut each step's body verbatim into its method; replace cross-step local
   references with `ctx.` members (mechanical rename — the ONLY permitted
   textual change inside moved bodies, plus `return` adjustments at
   early-exit sites).
4. Rewrite loadZoneSequential as the driver.
5. Preserve all #ifdef guards and step banners (banners become the first
   comment of each step method).

## Verification

- `cmake --build build -j24` clean.
- `ctest --test-dir build` — green modulo documented exceptions; the zone
  tests (ComprehensiveZoneTest.*, DoorLoadingUnitTest.*,
  DoorManagerGraphicsTest.*) MUST pass — they exercise this exact code.
- `python3 scripts/refactor/verify_move_only.py BASE..HEAD` — expected
  output: additions limited to method signatures, ZoneLoadContext
  struct/members, `ctx.` renames, and driver calls; removals limited to the
  corresponding original local declarations. Paste the full verifier output
  in the review; the orchestrator signs off on every substantive line.
- `bash scripts/refactor/check_file_sizes.sh --files <renderer_zone_load.cpp abs path>`
  — file may grow slightly (signatures); still under 3,000.
- Report the largest resulting method size — target: none over ~500 lines.

## Review

(executor fills in — analysis tables first, then results)
