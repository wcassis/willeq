# P21 — Door/entity masked-RGBA alpha gap (setShaderMaterialTypes)

**Slot: slot1. OWNS EQEMU (sole eqemu owner this batch). x86 only; do
NOT touch the Pi.** Siblings: P22 (slot2, GLES2 driver territory), P23
(slot3, particles/stability territory) — file territories are disjoint;
respect them.

Same bug class P20b just fixed for trees, in the remaining two sites:
`door_manager.h` and `entity_renderer.h` `setShaderMaterialTypes()` are
still 2-argument (companion alpha-test + solid) with no RGBA alpha-test
variant. Any masked WLD material whose texture arrives via the RGBA/DDS
pipeline (not ETC1) gets the companion variant with nothing bound on
unit 1 → GLES2 opaque-white fallback → renders solid.

## Trusted background (do not re-derive)

- H02d Option A: RGBA-pipeline textures must read their OWN alpha
  (`uTexture.a`); companion `uAlphaTexture` (unit 1) is ETC1-only. The
  RGBA alpha-test shader variants ALREADY EXIST (`zone_shader.cpp`,
  `materialAlphaTestRGBA_`/`materialLWAlphaTestRGBA_`) — this unit is
  plumbing, not shader authoring.
- P20b's fix pattern (mirror it): per-material classification
  masked+ETC1 → companion bound / masked+RGBA → RGBA variant /
  non-masked → solid. See `renderer_zone_stream.cpp:560-624` and the
  P20b Review for the reference implementation.
- `citywlivy.bmp` class (WLD-masked but painted opaque) renders solid
  BY DESIGN — name, don't fix, if encountered.

## Work

1. Ground truth first: determine whether any masked-RGBA door or entity
   materials actually occur in the classic zones we test (grep the WLD
   material data via the s3d/wld tools for door + character archives in
   befallen/qeynos2/gfaydark; document what you find). Real occurrences
   → they're your evidence scenes. None found → the fix is
   latent-correctness; say so plainly and rely on code-level evidence
   (a forced test case is acceptable if cheap, e.g. a known masked
   texture on a door in some classic zone — search more zones before
   concluding "none").
2. Extend `setShaderMaterialTypes()` in `door_manager.h`/`.cpp` and
   `entity_renderer.h`/`.cpp` to carry the RGBA alpha-test variant
   (3-arg, matching `ZoneMeshBuilder`), classify per-material at the
   material-assignment sites, and wire the extra argument at all call
   sites (grep `setShaderMaterialTypes` across `src/client/graphics`).
3. Evidence: if a real in-game case exists, same-pose-by-construction
   before/after stills per the P20b evidence bar (identical script,
   same step, stills to `/mnt/projects/fable/videos/p20_live_visual/
   stills/` with `p21_` prefix, manifest updated). If latent-only:
   before/after of the forced case, clearly labeled as forced.

## Rules

- eqemu per EXECUTOR_PROTOCOL.md; restore Testchar (zone_id=36,
  x=-77.9232, y=21.9086, z=3, h=190); shut down cleanly.
- Territory: `src/client/graphics/door_manager.*`,
  `src/client/graphics/entity_renderer.*` (+ headers under
  `include/client/graphics/`), call-site wiring lines only in other
  files. Do NOT touch `zone_shader.cpp` (variants exist), the GLES2
  driver (P22's territory), or particle files (P23's).
- Goldens untouched (visual gate PASS); STOP if the gate moves.
- Composite-entity interaction: composite meshes bypass per-material
  texture swaps by design — if classification interacts with the
  composite path, STOP and report rather than special-casing.

## Verification

- Full 6-step bar (known ctest exceptions: AudioManagerTest
  .PreloadCommonSounds, RDPServerFrameTest.UpdateFrameMultipleTimes).
- Ground-truth finding documented either way (real vs latent).

## Review

### Ground truth (real vs latent, per-territory)

**Doors — REAL, confirmed live.** WLD ground truth: a throwaway
`WldLoader`-based diagnostic (not committed; compiled ad hoc against
`pfs.cpp`/`wld_loader.cpp`/`zone_bsp.cpp`) found masked (Fragment 0x30
TransparentMasked) door-archive geometries in all three test zones —
`PAWHOOK_DMSPRITEDEF` (pchain.bmp) in both `qeynos2_obj.wld` and
`befallen_obj.wld`, plus `HPCHAIN`/`BONEPILE`/`BONES2` (befallen),
`TREE1`/`TREE6` (qeynos2), `HLOOM`/`NEKPINE1`/`NEKPINE2` (gfaydark).
Cross-referenced against the live `doors` table (eqemu DB, `peq`
schema): only `PAWHOOK` is an actual server-placed door — 6 real
instances in qeynos2 (ids 42,43,44,57,58,59 / DB ids 5972-5985,
opentype=58, size=100); the others are decorative WLD objects (trees,
bone piles) never referenced by a door row in any of the three zones.
Live client confirmation (`build-gles2/bin/willeq`, no `--eqe1-path`
so the RGBA/DDS decode path is exercised, `-d 5`): `pchain.bmp` decoded
via `[CONSTRAINED] texture 'pchain.bmp' decoded/uploaded` (never the
ETC1 path anywhere in the run log) and `ZoneShaderManager: RGBA
alpha-test shader compiled (alphaTestRGBA=258)`.

**Entities — LATENT-only, code evidence.** The same WLD-scan tool found
masked character-archive textures in all three `_chr.wld` files (e.g.
`zomch0001.bmp`/`zomlg0001.bmp` in befallen, `beahe0008.bmp` in
qeynos2). But an audit of every `MaterialType = ` assignment across
`src/client/graphics/eq/*.cpp` shows only `zone_geometry.cpp`
(`ZoneMeshBuilder`) ever assigns `EMT_TRANSPARENT_ALPHA_CHANNEL_REF`.
The character/equipment mesh-build paths — `mesh_building.cpp`
(`RaceModelLoader::buildMeshFromGeometry` AND
`buildCompositeMeshFromGeometry`, both the non-composite and composite
entity paths) and `equipment_model_loader.cpp` — unconditionally
hardcode `EMT_SOLID`, never consulting `textureMasked()`. This is
outside this unit's territory (door_manager.*/entity_renderer.* +
call-site wiring only). Consequence: `entity_renderer.cpp`'s new
`applyAlphaTestMaterialType()` classification is currently unreachable
dead code on the live entity path — no entity material is EVER assigned
`EMT_TRANSPARENT_ALPHA_CHANNEL_REF` today, composite or not, so no
masked entity currently renders opaque *specifically due to the
RGBA-vs-ETC1 gap this unit targets*. The fix is latent-correctness: it
wires the classification slot correctly for whichever future path (or
upstream fix to the separate `RaceModelLoader`-ignores-`textureMasked()`
gap) starts producing alpha-test entity materials, exactly mirroring
the pattern P20b used for trees.

**Composite-entity interaction (plan's STOP condition):** checked and
confirmed no interaction — composite meshes (`buildCompositeMeshFromGeometry`,
`mesh_building.cpp`) hardcode `EMT_SOLID` exactly like the non-composite
path (same audit above), so they never reach the classification branch
either. No special-casing needed; not a STOP.

### Fix

`door_manager.h`: `setShaderMaterialTypes()` extended 2->3 args
(`alphaTestRGBA = -1` default), new `shaderMaterialAlphaTestRGBA_`
member, forwarded unchanged to the per-call `ZoneMeshBuilder` in all 3
`findDoorMesh()` build sites (`door_manager.cpp`). `ZoneMeshBuilder`
already had the full H02d Option-A per-material classification (added
for trees/zone objects) — this unit is pure plumbing, as the plan
expected: no `zone_geometry.cpp` changes.

`entity_renderer.h`/`.cpp`: `setShaderMaterialTypes()` extended 2->3
args; new `applyAlphaTestMaterialType(SMaterial&)` private helper
(mirrors `ZoneMeshBuilder`'s H02d Option-A logic: `isETC1 ?
shaderMaterialAlphaTest_ (+ bind companion on unit 1) :
shaderMaterialAlphaTestRGBA_`) — the 3 material-assignment loop sites
that convert `EMT_TRANSPARENT_ALPHA_CHANNEL_REF` now call it instead of
unconditionally assigning `shaderMaterialAlphaTest_`. `swapShaderMaterials()`
extended with `oldAlphaRGBA`/`newAlphaRGBA` (both default -1) for the
runtime per-pixel/lightweight shader-swap path.

Call-site wiring (3rd arg added, all using the existing
`zoneShader_->getActiveAlphaTestRGBA()` accessor — no `zone_shader.cpp`
changes): `door_manager.cpp` (3x), `renderer_debug.cpp` (door + entity,
plus `MaterialMap` extended with `ppAlphaRGBA`/`lwAlphaRGBA` for the
entity swap calls), `irrlicht_renderer.cpp` (door + entity),
`renderer_zone_load.cpp` (door, 2x).

### Evidence

Before/after live capture (doors, PAWHOOK, qeynos2): same-pose-by-
construction (`#goto -352 184 3`, identical `--script` file, identical
wait timings between BEFORE/AFTER runs), stills at
`/mnt/projects/fable/videos/p20_live_visual/stills/p21_qeynos2_pawhook_00_BEFORE.png`
/ `_AFTER.png`. A temporary, uncommitted diagnostic (2 `LOG_INFO` lines
in `door_manager.cpp`, reverted before the final build — same pattern
as P20b's own uncommitted evidence workarounds) traced the actual
`MaterialType` applied to the PAWHOOK door mesh buffer:

| Build | `buf0.MaterialType` | Meaning |
|---|---|---|
| BEFORE (door_manager forced to drop the 3rd arg) | 14 | `EMT_TRANSPARENT_ALPHA_CHANNEL_REF` — Irrlicht fixed-function, NOT one of the GLES2 driver's 6 compiled programs |
| AFTER (fix restored) | 258 | The compiled GLES2 RGBA alpha-test shader (`alphaTestRGBA=258`), reads `uTexture.a` directly |

This numeric trace is the decisive evidence: the PAWHOOK door mesh
itself is small and not visually salient at the establishing-shot
vantage used (same wall/willow/pine scene P20b's qeynos2 evidence used)
— the stills are same-pose-by-construction but do not show an obvious
pixel-level delta on the door object itself, honestly noted rather than
overclaimed. Full evidence writeup in
`benchmarks/audits/p20_live_visual/EVIDENCE_MANIFEST.txt`.

Entity (latent) case: no forced live-game test was attempted — forcing
an entity material to `EMT_TRANSPARENT_ALPHA_CHANNEL_REF` requires
touching `RaceModelLoader`/`mesh_building.cpp`, outside this unit's
territory, and the plan's composite-interaction STOP condition made a
"cheap forced case" incompatible with staying in-territory. Reliance is
on the code-level evidence above (the `MaterialType = ` audit) plus the
full build/test suite passing.

### Verification bar

| Step | Result |
|---|---|
| 1. `cmake --build build -j24` | PASS |
| 2. `ctest --test-dir build --output-on-failure` | known host exceptions only (`AudioManagerTest.PreloadCommonSounds`; `RDPServerFrameTest.UpdateFrameMultipleTimes` intermittent, passed on the final run) |
| 3. `bash scripts/build-gles2-x86.sh` | PASS |
| 4. Visual gate | `bash scripts/ci/visual_gate.sh` PASS (qeynos2/gfaydark/befallen), goldens untouched, no re-bless; ctest's own `visual_gate` case also PASS |
| 5. `bash scripts/build-arm-noble.sh` | PASS |
| 6. `bash scripts/refactor/check_file_sizes.sh` | Run against the actual branch merge-base (`2fcc560`) rather than `fable` — `fable` advanced mid-session (P22/P23 merges landed), and diffing against the moving tip pulled in unrelated files. Against the correct baseline: `door_manager.h`/`.cpp` ok, `entity_renderer.h` ok, `renderer_debug.cpp` ok. `entity_renderer.cpp`: FAIL, 3296 lines — **pre-existing FAIL, not new**: 3263 lines on the branch-point baseline (already >3000, `deferred.md`'s "accepted core", exempted from splitting), grew by 33 lines from this unit's helper function + 3-arg extension. `irrlicht_renderer.cpp` (2811) and `renderer_zone_load.cpp` (2915): pre-existing WARNs, unchanged in kind (both already >1500 at the branch point; this unit's additions are small call-site edits, a few lines each). |

### eqemu

Sanctioned `scripts/eqemu/up.sh qeynos2` / `down.sh` only. Brought
up/down across the session (WLD ground-truth query needed only
mariadb+eqemu-server containers; full stack for the live evidence
capture). Host contention from the container's own auto-started
static-zone launcher fleet (same phenomenon P20b documented) caused two
transient client segfaults during evidence scouting — resolved by
retrying, not a code issue. One unrelated pre-existing crash found and
NOT investigated further (out of scope): passing `maps_path` in the
client config crashes the client during startup (before EGL/graphics
init) on this build — worked around by omitting `maps_path` (only
needed for `/move` pathfinding, not used in the final evidence script,
which uses `#goto` GM teleports only). Testchar restored to
zone_id=36 (befallen), x=-77.9232, y=21.9086, z=3, heading=190 per the
protocol's standard restore target — done via a direct DB write
(`UPDATE character_data SET zone_id=36, x=-77.9232, y=21.9086, z=3,
heading=190 WHERE name='Testchar'`, verified by re-`SELECT`), the same
technique P20b used after finding the in-game `#goto`/`#setheading` GM
commands don't reliably survive the post-`/camp` client teardown.
Server shut down cleanly (`down.sh`); confirmed no eqemu-related
containers/ports left (only pre-existing akk-stack fail2ban containers
untouched, per protocol never modified).

### Deviations

1. Ground-truth WLD scanning used a throwaway diagnostic tool (compiled
   ad hoc against `wld_loader.cpp`/`pfs.cpp`/`zone_bsp.cpp`, not
   committed, not part of the deliverable) rather than the existing
   `wld_dump`/`s3d_dump` tools — neither prints WLD Fragment 0x30
   `materialType`/masked-flag data, so a small purpose-built reader
   using `WldLoader::getGeometries()`/`textureMasked()` (the exact
   ground-truth signal `zone_geometry.cpp` itself uses) was faster than
   extending a dump tool for a one-time query.
2. The BEFORE/AFTER MaterialType evidence used a temporary, uncommitted
   2-line `LOG_INFO` diagnostic in `door_manager.cpp` (and a temporary
   one-line force-`-1` override in `door_manager.h`'s
   `setShaderMaterialTypes()`), both fully reverted (verified via `git
   diff` showing no trace) before the final build/verification pass and
   commit — same pattern as P20b's own uncommitted evidence
   workarounds.
3. `scripts/refactor/check_file_sizes.sh fable` was run against the
   actual branch merge-base instead of the literal `fable` argument,
   because `fable` moved mid-session (documented in the Verification
   bar table above) — a diff-baseline artifact of concurrent sibling
   units landing on `fable`, not a deviation in the fix itself.
4. Entity evidence relies on code-level audit only (no forced live test)
   per the "Evidence" section above — the plan allowed a forced case
   "if cheap"; forcing one here was not cheap without leaving this
   unit's territory, so it was not attempted.
