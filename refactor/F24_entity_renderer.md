# F24 — Split entity_renderer.cpp clean clusters into entity_* TUs

Lift the self-contained clusters out of
`src/client/graphics/entity_renderer.cpp` (4,329 lines), leaving the
entangled core (mesh building, update/interp, first-person/player). Pure
move — no behavior change, no interface change, no reformatting.

## Scope — four new files under `src/client/graphics/` (orig refs @ a0c02cd;
enumerate by NAME)

| File | Cluster |
|---|---|
| entity_castbars.cpp | casting bars (orig 3904–4136, ~230): startEntityCast, cancel/complete, isCasting, progress, updateEntityCastingBars, renderEntityCastingBars |
| entity_combat_anim.cpp | combat animation (orig 3492–3885, ~395): queueCombatAnimation, queueReceivedDamage, queueSkill, processCombatAnimationBufferForEntity, processExpiredCombatBuffers, pending queries; plus the adjacent animation-query/global-speed methods (3846–3903) if contiguous |
| entity_spatial.cpp | spatial grid + BSP occlusion (orig 4137–4268, ~130): updateEntityGridPosition, removeFromGrid, getEntitiesInRange, setBspTree, invalidateOcclusionCache, clearBspTree; plus boat support (4269–4329, ~60): registerBoat, unregisterBoat, findBoatDeckZ |
| entity_equipment.cpp | equipment (orig 2936–3247, ~310): setCurrentZone*, loadNumberedGlobals, loadEquipmentModels, attachEquipment, updateEquipmentTransforms — verify which of these are equipment vs zone-lifecycle; leave zone-lifecycle-only methods behind, documented |

The entangled core stays: registration/mesh building, update/interp,
remove/decay/visibility, first-person/player entity methods, animation
setters (setEntityAnimation, markAsCorpse, poseState, weapon skills, light).

## Steps

1. grep the full `EntityRenderer::` method list; assign each in-scope method
   to a file; put the mapping in the review BEFORE moving. When in doubt at a
   cluster boundary, leave the method in entity_renderer.cpp and document.
2. Replicate the namespace wrapper + include pattern of entity_renderer.cpp
   per new TU (see F14–F16, F23 reviews).
3. Move verbatim; preserve #ifdef guards exactly. File-local statics: usual
   rule (move if exclusive, else leave + document).
4. Add all four files to `GRAPHICS_SOURCES` in CMakeLists.txt.

## Verification

- `cmake --build build -j24` clean.
- `ctest --test-dir build` — green modulo documented exceptions in
  refactor/test_baseline.txt.
- `python3 scripts/refactor/verify_move_only.py BASE..HEAD` — PASS or
  documented necessary-include/de-static deviations only.
- `bash scripts/refactor/check_file_sizes.sh --files <four abs paths>` — each ok.
- entity_renderer.cpp shrinks by ~1,000+ lines.

## Review

**Cluster boundaries** were confirmed by a full `grep -n "EntityRenderer::"` pass
(95 methods) against `entity_renderer.cpp` @ `a0c02cd`/`fable` tip (4,329 lines),
cross-checked against the file's 3 section banners (`Entity Casting Management`
@3900, `Spatial Grid Functions...` @4133, `PVS-based Visibility Culling` @4210).
The combat-animation and equipment clusters have no banners of their own; their
line ranges were taken from contiguous method runs bounded by adjacent named
methods that stay in the core.

### Method-mapping table

| Method | Disposition |
|---|---|
| `loadEquipmentModels`, `attachEquipment`, `updateEquipmentTransforms` (orig 2948–3247) | entity_equipment.cpp (Equipment) |
| `extractBoneRotation` (static, orig 3058), `findBoneIndex` (static, orig 3071) | entity_equipment.cpp — file-local helpers used exclusively by `updateEquipmentTransforms`, moved with it |
| `setCurrentZone`, `loadNumberedGlobals` (orig 2936–2947) | **stays** in entity_renderer.cpp — both delegate only to `raceModelLoader_` (zone/race model lifecycle), not equipment; plan explicitly called for this split |
| `queueCombatAnimation`, `queueReceivedDamageAnimation`, `queueSkillAnimation`, `processCombatAnimationBufferForEntity`, `processExpiredCombatBuffers`, `hasEntityPendingCombatAnims`, `getEntityPrimaryWeaponSkill`, `getEntitySecondaryWeaponSkill`, `isEntityPlayingThrough`, `stopEntityAnimation`, `getEntityAnimation`, `hasEntityAnimation`, `getEntityAnimationList`, `setGlobalAnimationSpeed` (orig 3492–3896, one contiguous run, no banner) | entity_combat_anim.cpp (Combat Animation + adjacent animation-query/global-speed methods, confirmed contiguous per plan's conditional) |
| `startEntityCast`, `cancelEntityCast`, `completeEntityCast`, `isEntityCasting`, `getEntityCastProgress`, `updateEntityCastingBars`, `renderEntityCastingBars` (orig 3900–4131, banner "Entity Casting Management") | entity_castbars.cpp |
| `updateEntityGridPosition`, `removeEntityFromGrid`, `getEntitiesInRange` (orig 4133–4208, banner "Spatial Grid Functions") | entity_spatial.cpp |
| `setBspTree`, `invalidateOcclusionCache`, `clearBspTree` (orig 4210–4259, banner "PVS-based Visibility Culling") | entity_spatial.cpp |
| `setConstrainedConfig` (orig 4261–4267) | **stays** in entity_renderer.cpp — sits between `clearBspTree` and `registerBoat` but isn't named in the plan's spatial/boat list and isn't spatial/BSP/boat logic (just a config pointer setter); left behind per "leave method in core, document at boundary" rule, cutting a one-method gap inside the orig 4137–4268 range |
| `registerBoat`, `unregisterBoat`, `findBoatDeckZ` (orig 4269–4326) | entity_spatial.cpp (boat support) |

All other `EntityRenderer::` methods (registration/mesh building, update/interp,
remove/decay/visibility, first-person/player, animation setters
`setEntityAnimation`/`markEntityAsCorpse`/pose state/weapon skills/light, misc
setters) are entangled core and stay in `entity_renderer.cpp`, per plan.

### Static-helper disposition

Only 3 file-local `static` free functions exist in the original TU
(`grep "^static "`): `getGreenCutoff` (orig line 32, used only by
`getConColor`, a core method — stays in `entity_renderer.cpp`), and
`extractBoneRotation`/`findBoneIndex` (orig 3058/3071, used only by
`updateEquipmentTransforms` — moved to `entity_equipment.cpp` with their
sole caller, no de-static-ing needed).

### Include / namespace pattern

Followed the F14–F23 precedent: each new TU replicates the full original
top-of-file include block verbatim (`entity_renderer.h`,
`simulation_worker.h`, `entity_prep_worker.h`, `constrained_renderer_config.h`,
`constrained_texture_cache.h`, `light_source.h`, `eq/zone_geometry.h`,
`eq/race_model_loader.h`, `eq/race_codes.h`, `eq/dds_decoder.h`,
`eq/animated_mesh_scene_node.h`, `eq/skeletal_animator.h`,
`animation_constants.h`, `common/logging.h`, `common/name_utils.h`,
`common/performance_metrics.h`, plus the same 8 standard-library headers and
`<fmt/ranges.h>`) rather than hand-trimming per cluster, and opens
`namespace EQT { namespace Graphics { ... } }` once, matching the single
wrapper `entity_renderer.cpp` already used. No new includes were required —
every referenced type (`BoneMat4`, `CharacterSkeleton`, `EntityVisual`,
`BspTree`, `ConstrainedRendererConfig`, etc.) already comes in transitively
via the replicated includes. `verify_move_only.py` allowlists this boilerplate
(+116 lines across 4 files, ~29 each) as non-substantive.

### Boundary decisions

- Equipment cluster excludes `setCurrentZone`/`loadNumberedGlobals` (zone
  lifecycle delegation to `raceModelLoader_`, not equipment) — left in core.
- Spatial cluster excludes `setConstrainedConfig` (plain config setter,
  physically wedged between `clearBspTree` and `registerBoat` but not named
  in the plan and not spatial/BSP/boat logic) — left in core, gap documented
  above.
- No other ambiguous boundaries encountered; all four clusters' line ranges
  matched the plan's stated ranges closely (within a few lines, due to exact
  banner/blank-line placement).

### Whitespace cleanup at removal seams

Excising 5 non-adjacent ranges from `entity_renderer.cpp` left runs of 3–6
consecutive blank lines at two seams (around the `setEntityLight` →
`setConstrainedConfig` gap). Collapsed to single blank lines at both seams —
whitespace-only, no code lines touched; `verify_move_only.py` doesn't flag
blank-line changes as substantive.

### Line counts (final)

| File | Lines |
|---|---|
| `entity_renderer.cpp` (entangled core, was 4,329) | 3,201 |
| `entity_castbars.cpp` | 264 |
| `entity_combat_anim.cpp` | 437 |
| `entity_spatial.cpp` | 218 |
| `entity_equipment.cpp` | 332 |
| **Total (incl. boilerplate)** | 4,452 |

(Total exceeds the original 4,329 by 123 lines: +116 replicated
include/namespace boilerplate across the 4 new files, plus a handful of net
blank-line changes from seam cleanup — zero substantive lines added, per the
move-only verifier.)

### Verifier summary

- `cmake --build build -j24`: clean, no warnings from new/changed files.
- `ctest --test-dir build`: 1191/1192 passed; the 1 failure
  (`AudioManagerTest.PreloadCommonSounds`, ALSA card-0 resolution) is the
  documented pre-existing host-environment exception in
  `refactor/test_baseline.txt`. All graphics/entity-adjacent tests
  (`test_graphics`, `test_constrained_renderer`, `test_zoning_graphics_integration`,
  `test_zoning_integration`, etc.) passed.
- `verify_move_only.py` (BASE = `git merge-base fable HEAD` =
  `06e6f5f98d4ee8e5a7d4c29b97db6c981f7d75ab`): **PASS** — "Files verified: 5,
  Boilerplate: +116 / -0 lines (allowed)". `CMakeLists.txt` reported as
  non-C++ (reviewed separately: 4 new source lines added to
  `GRAPHICS_SOURCES`, no other changes).
- `check_file_sizes.sh` on all four new files: all `ok`
  (`entity_castbars.cpp` 264, `entity_combat_anim.cpp` 437,
  `entity_spatial.cpp` 218, `entity_equipment.cpp` 332 — largest well under
  the 1,500-line ceiling).

### Deviations

None beyond the documented include-replication boilerplate and blank-line
seam cleanup above (whitespace only, not a code-line change) — no line of
moved code was altered, reformatted, or behaviorally changed. A second commit
fixed file mode on the 4 new files (they were created 100755, inconsistent
with sibling sources at 100644); mode-only, no content change.
