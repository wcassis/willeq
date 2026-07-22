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

**Cluster boundaries (verified by grep of `IrrlichtRenderer::` definitions,
irrlicht_renderer.cpp @ merge-base commit
233a2dfd524c93b7315473726ef17816389e9634 with `fable`, 13,203 lines before
this branch):**

- `renderer_gpu_upload.cpp`: pre-move lines 1336–1755 — one contiguous
  cluster between `shutdown()` and `isRunning()`. 5 `IrrlichtRenderer::`
  methods:
  - `initGPUUploadThread()` (under `#ifdef EQT_HAS_GLES2`, nested
    `#ifdef EQT_HAS_DRM` / `#else` / `#endif` inside its body)
  - `processCompletedUploads()` (under `#ifdef EQT_HAS_GLES2`)
  - `drainGPUResults()` (under `#ifdef EQT_HAS_GLES2`)
  - `processOneGPUResult()` (under `#ifdef EQT_HAS_GLES2`; the
    `#endif // EQT_HAS_GLES2` closing the 4-method guard follows it)
  - `toggleGPUUploadThread()` — the "upload-thread toggle" named by the
    plan; sits *outside* the big guard, with its own internal
    `#ifdef EQT_HAS_GLES2` / `#else` / `#endif` (non-GLES2 builds get the
    "not available" log). Moved with its guard structure intact.

  No other contiguous upload-thread helpers exist (grep for
  `[Uu]pload[Tt]hread` found only these five definitions).

- `renderer_entities.cpp`: four ranges concatenated in file order (the
  entity façade cluster is non-contiguous — non-façade methods interleave
  and were left in place, itemized below):
  1. Pre-move 7330–7411: `createEntity`, `registerEntity`, `updateEntity`,
     `removeEntity`, `startCorpseDecay`.
  2. Pre-move 7413–7488: `setEntityLight` (player-light special case +
     entityRenderer_ forward), `clearEntities`.
  3. Pre-move 7681–7711: `playEntityDeathAnimation`, `setEntityAnimation`,
     `setEntityPoseState` (the entity-anim forwards).
  4. Pre-move 12948–13200: the `// --- Inventory UI Methods ---` block
     (`setInventoryManager`, `toggleInventory`, `openInventory`,
     `closeInventory`, `showNoteWindow`, `isInventoryOpen`,
     `setCharacterInfo`, `setCharacterDeity`, `setExpProgress`,
     `updateCharacterStats`, `updatePlayerAppearance`,
     `updateEntityAppearance`, `setChatSubmitCallback`,
     `setReadItemCallback` — exactly the 14 F15 left behind) immediately
     followed by the combat-anim façade F16 left behind
     (`setEntityWeaponSkills`, `getEntityPrimaryWeaponSkill`,
     `getEntitySecondaryWeaponSkill`, `queueCombatAnimation`,
     `hasEntityPendingCombatAnims`, `queueReceivedDamageAnimation`,
     `queueSkillAnimation`, `triggerFirstPersonAttack`). This range ran to
     the old EOF (only the file-scope `} // namespace` closers followed).

  **In-range but explicitly left in `irrlicht_renderer.cpp` (judgment
  calls — not entity/inventory/combat-anim façade methods):**
  - Between ranges 2 and 3 (pre-move 7490–7679): `setExpectedEntityCount`,
    `notifyEntityLoaded`, `setNetworkReady`, `setWeather`,
    `checkAndSetZoneReady` (zone-loading state management under its own
    `// Entity Loading State Management` header); `createDoor`,
    `registerDoor`, `setDoorState`, `clearDoors` (door façade — plan names
    only entity/inventory/combat-anim clusters); `addWorldObject`,
    `removeWorldObject`, `clearWorldObjects`, `getWorldObjectAtScreenPos`,
    `getNearestWorldObject` (world-object tracking, real logic not
    forwards).
  - Immediately after range 3: `setPlayerSpawnId`, `setPlayerRace`,
    `setVisionType`, `resetVisionToBase`, `updateObjectLightColors` —
    player/camera/vision state, not entityRenderer_ forwards.
    (`createEntity` and `setEntityLight` do *call* `setPlayerRace` /
    player-light members; those callees stay behind, cross-TU calls via
    the class — no code change.)

**Guard handling:**
- `renderer_gpu_upload.cpp`: the `#ifdef EQT_HAS_GLES2` /
  `#endif // EQT_HAS_GLES2` pair around the first four methods, the
  nested `#ifdef EQT_HAS_DRM` / `#else` / `#endif` inside
  `initGPUUploadThread`, and `toggleGPUUploadThread`'s internal
  `#ifdef EQT_HAS_GLES2` / `#else` / `#endif` all preserved verbatim.
- `renderer_entities.cpp`: one nested `#ifdef EQT_HAS_GLES2` … `#endif`
  block inside `setInventoryManager` (particleManager_ delegation
  lambdas) preserved verbatim in place.

**Namespace + include handling (same pattern as F14–F16):** both new
files wrap their bodies in `namespace EQT { namespace Graphics { … } }`.
Includes added:
- `renderer_gpu_upload.cpp`: `irrlicht_renderer.h` (pulls in
  `gpu_upload_thread.h`, `constrained_texture_cache.h` full types via the
  header), `common/logging.h`, `<algorithm>` (std::sort/std::find),
  `<utility>` (std::move), plus the same conditional
  `EQT_HAS_DRM`/`EQT_HAS_GLES2` EGL/GL header block the original TU
  carries at file scope (needed for `EGLDisplay`, `eglGetProcAddress`,
  `PFNEGLCLIENTWAITSYNCKHRPROC`, fence constants).
- `renderer_entities.cpp`: `irrlicht_renderer.h`,
  `client/graphics/spell_visual_fx.h` (`EQ::SpellVisualFX` is only
  forward-declared in the header; `setInventoryManager` constructs it and
  calls its statics), `client/graphics/light_source.h`
  (`lightsource::TypeToLevel` in `setEntityLight`; the header never
  includes it — caught by first build attempt), `common/logging.h`, and
  `<glm/glm.hpp>` under `#ifdef EQT_HAS_GLES2` (glm comes in via
  irrlicht_renderer.h anyway; kept explicit for the particle lambdas per
  F14–F16 explicit-include style).

**File-local statics / extern declarations:** no file-local
`static`/anonymous-namespace helpers were referenced by either cluster
(the two function-local `static auto eglClientWaitSyncKHR/eglDestroySyncKHR`
caches inside `processCompletedUploads`/`processOneGPUResult` moved as-is
with their functions). The GPU-upload cluster calls two of the file-scope
`extern` GLES2 bridge declarations (`gles2WrapTexture`,
`gles2RegisterExternalHWBuffer`, defined in COpenGLES2Driver.cpp);
`renderer_gpu_upload.cpp` duplicates those two declarations under the same
`#ifdef EQT_HAS_GLES2` guard. The original declarations stay in
irrlicht_renderer.cpp untouched (its remaining code still uses the other
five bridge functions in that block).

**CMakeLists.txt:** added `renderer_gpu_upload.cpp` and
`renderer_entities.cpp` to `GRAPHICS_SOURCES`, immediately after
`renderer_effects.cpp`.

**Line counts:**
- `irrlicht_renderer.cpp`: 13,203 → 12,340 lines (**−863**)
- `renderer_gpu_upload.cpp`: 455 lines (new; 421 moved + includes/extern
  decls/namespace wrapper)
- `renderer_entities.cpp`: 464 lines (new; 442 moved across 4 ranges +
  includes/namespace wrapper + 2 section-separator comment lines)
- Net: +921 / −863 = +58 lines. (Plan estimated ~420 + ~735 for the new
  files; the ~735 figure counted the plan's orig-ref ranges 7330–7813 and
  14192–14387 wholesale, but much of 7330–7813 is door/world-object/
  zone-state code the plan's own method list does not name — left behind
  per "when in doubt, LEAVE", itemized above.)

**Verification:**
- `cmake --build build -j24` — clean (one first-pass mechanical fix:
  missing `light_source.h` include in renderer_entities.cpp, noted above).
- `ctest --test-dir build` — 1191/1192 passed; the single failure
  (`AudioManagerTest.PreloadCommonSounds`) is the documented pre-existing
  ALSA environment gap in `refactor/test_baseline.txt`. 10 tests skipped
  (asset/graphics-dependent, pre-existing, unrelated).
- `python3 scripts/refactor/verify_move_only.py
  233a2dfd..HEAD --repo <slot3>` — 3 C++ files verified, boilerplate
  +35/−0 allowed, 7 substantive added lines reported: all seven are the
  duplicated `extern` declarations for `gles2WrapTexture` /
  `gles2RegisterExternalHWBuffer` in renderer_gpu_upload.cpp (documented
  deviation, same category as F16's duplicated GL header block — the
  moved code calls functions declared at file scope in the original TU,
  and the new TU needs its own declarations). `CMakeLists.txt` reported
  as non-C++/not-verified — its only change is the additive 2-line
  source-list entry. `git merge-base fable HEAD` = `233a2df` (branch
  point; `fable` had not moved).
- `bash scripts/refactor/check_file_sizes.sh --files <abs paths>` — both
  `ok` (455 and 464 lines, well under the 1,500 ceiling).

**Deviations:** none in moved code (verbatim, no reformatting). Two
documented additions beyond pure cut/paste: (1) the two duplicated
`extern` GLES2 bridge declarations in renderer_gpu_upload.cpp; (2) two
one-line `// ---` section comments in renderer_entities.cpp marking the
concatenation seams between the entity-façade ranges (counted as
boilerplate by the verifier). Scope clarification: the plan's
renderer_entities.cpp estimate (~735 lines, orig 7330–7813) included
door/world-object/zone-loading-state methods it never named; only the
named façade clusters moved, the rest left in place and itemized above.
