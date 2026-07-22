# F21 — Split GPU upload thread and façade clusters out of irrlicht_renderer.cpp

Lift the GPU-upload-thread cluster and the thin façade/forwarding clusters
out of `src/client/graphics/irrlicht_renderer.cpp` (13,203 lines). Pure move
— no behavior change, no interface change, no reformatting.

## Scope (orig refs @ a0c02cd; positions shifted by F14–F16. Enumerate by NAME.)

Two new files under `src/client/graphics/`:

**renderer_gpu_upload.cpp** (~420 lines) — the GPU upload thread cluster
(orig 1337–1756): `initGPUUploadThread`, `processCompletedUploads`,
`drainGPUResults`, `processOneGPUResult`, the upload-thread toggle, and any
contiguous upload-thread helpers.

**renderer_entities.cpp** (~735 lines) — the delegating façade clusters:
- Entity façade (orig 7330–7813): createEntity/registerEntity/updateEntity/
  removeEntity/entity-light/entity-anim forwards to entityRenderer_.
- Inventory/character-info façade (orig 14192–14387 region; F15's review
  lists them left behind): `setInventoryManager`, `toggleInventory`,
  `openInventory`, `closeInventory`, `showNoteWindow`, `isInventoryOpen`,
  `setCharacterInfo`, `setCharacterDeity`, `setExpProgress`,
  `updateCharacterStats`, `updatePlayerAppearance`, `updateEntityAppearance`,
  `setChatSubmitCallback`, `setReadItemCallback`.
- Combat-anim façade (orig 15296–15364): `setEntityWeaponSkills`,
  `queueCombatAnimation` family, `triggerFirstPersonAttack` — the methods
  F16 documented as left-behind.

## Steps

1. Locate clusters by grep; produce the mapping in the review BEFORE moving.
   When in doubt at a boundary, leave + document.
2. Replicate the namespace wrapper + include patterns (F14–F16 reviews).
3. Move verbatim; preserve #ifdef guards exactly. File-local statics: usual
   rule.
4. Add both files to `GRAPHICS_SOURCES` in CMakeLists.txt.

## Verification

- `cmake --build build -j24` clean.
- `ctest --test-dir build` — green modulo documented exceptions in
  refactor/test_baseline.txt.
- `python3 scripts/refactor/verify_move_only.py BASE..HEAD` — PASS or
  documented deviations only.
- `bash scripts/refactor/check_file_sizes.sh --files <two abs paths>` — each ok.
- irrlicht_renderer.cpp shrinks by ~1,100+ lines.

## Review

(executor fills in)
