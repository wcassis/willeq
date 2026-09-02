# J03 — Batch J Wave 1: split entity_renderer.cpp (3,255 → 7 TUs, largest ~645)

**Slot: assigned at dispatch. Branch `task/J03-entity-renderer-split` cut from
`fable`. x86 only. No Pi hardware. No eqemu.**

## ⚠⚠ DO NOT DISPATCH THIS UNIT UNTIL T318 HAS MERGED TO `fable`

`src/client/graphics/entity_renderer.cpp` — this unit's entire subject — is
**open and modified in slot3 right now** by T318 (the shrink/grow/illusion
size-update path, `refactor/T318_*.md`). T318 also has
`include/client/graphics/entity_renderer.h`, `renderer_entities.cpp`,
`irrlicht_bridge.cpp` and `event_bus.h` dirty. A split landing under it, or it
landing under a split, is a guaranteed and ugly conflict across a 3,000-line
file with **no** semantic help from git.

**Gate:** `git -C /mnt/projects/fable/main log --oneline fable | grep -i T318`
returns a merge before this unit is cut. J02 and J04 have no such dependency
and go first. If T318's own line-count constraint (its acceptance notes
`entity_renderer.cpp` is already over the ceiling) forces it to shrink the
file, **re-measure everything below from scratch** — this plan's ranges will be
wholesale wrong, not slightly wrong.

Wave 1 of `refactor/J_file_size_reclamation.md`, running alongside **J02** and
**J04**. Read, before editing anything: **`refactor/J_file_size_reclamation.md`
§Process per unit** (six corrections made after the J01 pilot; it **wins over
anything older**), `refactor/J01_inventory_manager_split.md` (the proven
template, and its Review), `refactor/F_file_split.md`,
`refactor/T166_split_renderer_zone_load.md` and
`src/client/graphics/CLAUDE.md`.

## MOVE-ONLY. No behavior changes, no interface changes.

Method definitions move **verbatim**. No change to
`include/client/graphics/entity_renderer.h`. **No method decomposition** —
`processOneEntityBuildStep` (470), `buildEntityMesh` (442) and `processUpdate`
(421) all move whole. No drive-by fixes.

## The split

Measured at `2eb34f3d`, re-confirmed byte-for-byte at `7e0c7823` after J01 merged (2026-09-02), i.e. **before T318**. **Re-verify every
range against the tree at your own branch tip before you cut** — see the gate
above; T318 lands in the middle of this file. Report drift in your Review
rather than silently adjusting.

Preamble is **lines 1-29**: 25 includes, a blank, `namespace EQT {`,
`namespace Graphics {`, a blank. The close is `} // namespace Graphics` /
`} // namespace EQT` at 3254-3255. Both replicate into every new TU. There are
**no `// ====` banners and no preprocessor conditionals** anywhere in this
file's body — verify with
`grep -n '^#\|^// =' src/client/graphics/entity_renderer.cpp`.

| Range | Cluster | Target TU | ≈ lines |
|---|---|---|---|
| 30-274 | **stays.** `getGreenCutoff` (file-local static) + `getConColor`, ctor, dtor, `setClientPath`, `loadGlobalCharacters`, `getColorForRace`, `getScaleForRace`, `applyAlphaTestMaterialType`, `createPlaceholderMesh`, `getMeshForRace` | **stays** `entity_renderer.cpp` | — |
| 275-860 | `registerEntity` (91), `buildEntityMesh` (**442**), `pollAndDistributePrepResults`, `getMeshBuilder`, `setCompositeEntityPath`, `setConstrainedTextureCache` | `entity_mesh_build.cpp` | ~617 |
| 861-1330 | `processOneEntityBuildStep` — **one method, 470 lines** | `entity_build_step.cpp` | ~501 |
| 1331-1944 | `createEntity`, `isEntityMeshBuilt`, `getUnbuiltEntities`, `updateEntity`, `flushPendingUpdates`, `drainPendingUpdates`, `flushPendingUpdatesForAnimations`, `updateMainThreadEntityState`, `processUpdate` (**421**) — the whole pending-update path, queue and apply | `entity_updates.cpp` | **~645** |
| 1945-2285 | `updateInterpolation` — **one method, 341 lines** | `entity_interpolation.cpp` | ~372 |
| 2286-2705 | `removeEntity`, `startCorpseDecay`, `updateEntityAppearance`, `clearEntities`, `hasEntity`, `setAllEntitiesVisible`, `getModeledEntityCount`, `getPerInstanceMemoryBytes`, `setNameTagsVisible`, `swapShaderMaterials`, `setLightingEnabled`, `setPlayerEntityVisible`, `setDebugTargetId`, `debugLogPlayerVisibility` | `entity_lifecycle.cpp` | ~451 |
| 2706-2989 | **the first-person family** (see below): `setPlayerFirstPersonMode`, `isPlayerInFirstPersonMode`, `updatePlayerEntityPosition`, `setPlayerEntityAnimation`, `setPlayerSpawnId`, `getPlayerModelYOffset`, `getPlayerCollisionZOffset`, `getPlayerFirstPersonEyeOffset` | **appended to the EXISTING** `entity_first_person_draw.cpp` | 90 → ~374 |
| 2990-3253 | **stays.** `setCurrentZone`, `loadNumberedGlobals`, `setEntityAnimation`, `markEntityAsCorpse`, `setEntityPoseState`, `getEntityPoseState`, `setEntityWeaponSkills`, `setEntityWeaponDelay`, `setEntityLight`, `setConstrainedConfig` | **stays** `entity_renderer.cpp` | — |

**Result: 7 TUs** — `entity_renderer.cpp` ≈ 540 (29 preamble + 245 + 264 + 2
close), `entity_updates.cpp` ≈ 645, `entity_mesh_build.cpp` ≈ 617,
`entity_build_step.cpp` ≈ 501, `entity_lifecycle.cpp` ≈ 451,
`entity_first_person_draw.cpp` ≈ 374, `entity_interpolation.cpp` ≈ 372.
Every one comfortably under 1,500 with real room to grow — which matters here,
because this is the file T318 is currently growing.

## Hazards

1. **⚠ The T307/T309 first-person attack-animation draw rule must stay
   findable in ONE place.** This is the batch's named constraint for J03 and it
   is the reason the 2706-2989 cluster has an unusual destination.
   **`updateFirstPersonBodyDrawRule()` is ALREADY split out** — it lives in
   `src/client/graphics/entity_first_person_draw.cpp` (90 lines, T309), whose
   header comment carries the measured rule and points at
   `include/client/graphics/first_person_body_draw.h`,
   `refactor/T307_official_draw_rule.md` and `review/T307/README.md`. It is
   called from `entity_renderer.cpp:1521` (inside `updateMainThreadEntityState`,
   which this split sends to `entity_updates.cpp`) and `:2763`.
   **Append the 2706-2989 family to that existing TU** rather than creating a
   new `entity_player.cpp` — the mode toggle, the eye offset, the player
   animation entry point and the per-frame rule then all sit in one file, and
   an agent that greps `first_person` lands on the whole feature. Extend that
   file's five-line include block with whatever the moved methods need from
   `entity_renderer.cpp`'s block; the simplest correct move, and the one
   §Process asks for elsewhere, is to **adopt `entity_renderer.cpp`'s full
   25-include block** there.
   The two call sites are unqualified member calls and need no change — they
   resolve through the class declaration in the header. **Do not touch
   `first_person_body_draw.h`, `first_person_eye.h`, `first_person_camera.h`
   or `animated_mesh_scene_node.cpp`,** all of which reference the rule by
   name in comments and stay correct.

2. **`static int getGreenCutoff(int)` at `:32` is the file's only file-local
   static.** Its sole caller is `getConColor` at `:46`; both are inside the
   30-274 cluster and stay in `entity_renderer.cpp` together, so **no internal
   header is needed and nothing must be de-static-ed.** Confirm at your tip
   with `grep -n 'getGreenCutoff' src/client/graphics/`. If T318 has added a
   second caller elsewhere in the file, say so — that turns this into the
   T166 internal-header case.

3. **No anonymous namespaces, no file-scope objects, no preprocessor
   conditionals** in the body at `7e0c7823`. Re-check and state it in the
   Review; a new one arriving with T318 is the single most valuable thing this
   unit can report.

## Includes

**Each new TU carries `entity_renderer.cpp`'s full 25-include block, verbatim**,
plus the `namespace EQT { namespace Graphics {` open and the matching close.
Not "the includes it needs" — see §Process. Do not create a shared "everything"
header. The one exception is `entity_first_person_draw.cpp`, which already
exists and already has a header comment: keep its comment and its
`first_person_body_draw.h` include, and take the full block on top.

## CMake

**Do not trust a line number.** Find the sites yourself:

```bash
grep -n 'entity_renderer\.cpp\|entity_first_person_draw\.cpp' CMakeLists.txt tests/CMakeLists.txt
```

At `7e0c7823` `entity_renderer.cpp` appears exactly **once**, at
`CMakeLists.txt:463` in `GRAPHICS_SOURCES` (its sibling
`entity_first_person_draw.cpp` is at `:466` in the same list) — it was `:458`
four hours earlier, before J01 merged, which is §Process's point about line
numbers exactly, and `tests/CMakeLists.txt` names it nowhere. Add the five
new TUs to **every** list that names the original. `entity_first_person_draw.cpp`
is already listed and needs no new entry.

## Acceptance (the Batch J bar — every item, reported by EXIT CODE)

- build clean; `cmake --build build -j24` classified by **EXIT CODE**, never by
  grepping the log (`grep ' error '` never matches GCC, which prints `error:`)
- full **`ctest -j6`** (NOT -j24 — the visual gate's CPU ceiling flakes above
  that), zero new failures vs `refactor/test_baseline.txt`
- ⚠ `BASE=$(git merge-base fable HEAD)` then
  `python3 scripts/refactor/verify_move_only.py "$BASE..HEAD"` **clean** — the
  unit's central proof. **NOT `fable..HEAD`:** three units are merging into
  `fable` during this wave and `fable..HEAD` will report the other two units'
  work, in reverse, as a very loud spurious FAIL. Record `$BASE` and paste the
  output in the Review.
- ⚠ the **line-multiset check** (§Process): the seven files' bodies form a
  multiset identical to `entity_renderer.cpp`'s original lines 30-3253 **plus
  `entity_first_person_draw.cpp`'s original body**; and the eight extracted
  ranges re-concatenated in original order are byte-identical to
  `git show "$BASE:src/client/graphics/entity_renderer.cpp" | sed -n '30,3253p'`.
  Paste both results.
- **visual gate PASS with goldens untouched, and this is the unit where it
  bites.** Entity rendering, con colours, interpolation and the first-person
  draw rule are exactly what the gate photographs. A re-bless would mean the
  split changed behaviour — **STOP and report** rather than re-blessing.
  `git status --porcelain` must be empty of golden changes.
- `scripts/build-gles2-x86.sh` and `scripts/build-arm-noble.sh` both clean
- `bash scripts/refactor/check_file_sizes.sh --files <every changed and created
  file>` → **0 warn, 0 fail**. `CMakeLists.txt`'s pre-existing 1,899-line WARN
  is not yours (§Process).
- ⚠ `chmod 644` every created file before `git add`.

## Review must answer

1. **How much T318 moved the ranges**, and whether its size-update path landed
   in a cluster this plan sends somewhere non-obvious.
2. Line-range drift vs this plan, and the final line count of all seven TUs.
3. The verifier's base rev you used, and whether it came back clean first try.
4. Wall clock and slowest step, and whether concurrency with J02/J04 stretched
   the verification tail.
5. Confirmation that the whole first-person feature — mode, eye offset, player
   animation entry point and the per-frame draw rule — now lives in
   `entity_first_person_draw.cpp`, and that its header comment still reads
   correctly for the enlarged file.

## Territory

`src/client/graphics/entity_renderer.cpp`,
`src/client/graphics/entity_first_person_draw.cpp`, the five new
`src/client/graphics/entity_*.cpp` TUs beside them, `CMakeLists.txt`, this
file. **Nothing else** — not `include/client/graphics/entity_renderer.h`, not
`renderer_entities.cpp`, not the `first_person_*.h` headers, not
`docs/module_map.md`.

Concurrent at dispatch: **J02** (`eq.cpp` + new `eq_*.cpp`, `CMakeLists.txt`),
**J04** (`action/command_processor.cpp` + new `command_*.cpp`,
`CMakeLists.txt`, `tests/CMakeLists.txt`). No source overlap with either;
`CMakeLists.txt` is the only shared file, which is why merges are serialized
one at a time with a rebase. **T318 is a hard predecessor, not a peer — see
the gate at the top.**
