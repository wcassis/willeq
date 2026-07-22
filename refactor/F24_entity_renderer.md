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

(executor fills in)
