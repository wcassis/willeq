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

---

## Review (slot3, 2026-09-02, branch `task/J03-entity-renderer-split`, commit `f6037efc`)

**Done as specified. Move-only. Zero deviations from the cluster plan beyond
the line-number drift T318 introduced (§1) and one comment edit in
`entity_first_person_draw.cpp`'s header (§5), which is boilerplate under the
verifier and is declared here.** Base tip verified `d3b2c995` at checkout.

### 1. How much T318 moved the ranges

`entity_renderer.cpp` was **3,244 lines at my tip, not 3,255** — T318 took 11
lines out of it by lifting the size-update path into the new
`src/client/graphics/entity_size.cpp` (58 lines, already listed in
`CMakeLists.txt:472`). **Nothing T318 added landed in this file at all**, so no
cluster of this plan inherited a non-obvious piece of the size path: the
`serverSize` member it promoted is declared in the header (untouched territory)
and read inside `registerEntity`, which was already going to
`entity_mesh_build.cpp`.

The drift is a uniform **-11 lines from `createEntity` onward** plus **-1 at the
top**, i.e. every plan boundary shifted, but the *cluster membership* is
identical to the plan's. Boundaries at my tip:

| plan range | actual range | → |
|---|---|---|
| 1-29 | **1-29** (unchanged) | preamble, replicated 6× |
| 30-274 | **30-274** | stays in `entity_renderer.cpp` (part A) |
| 275-860 | **275-855** | `entity_mesh_build.cpp` |
| 861-1330 | **856-1319** | `entity_build_step.cpp` |
| 1331-1944 | **1320-1933** | `entity_updates.cpp` |
| 1945-2285 | **1934-2274** | `entity_interpolation.cpp` |
| 2286-2705 | **2275-2694** | `entity_lifecycle.cpp` |
| 2706-2989 | **2695-2978** | appended to `entity_first_person_draw.cpp` |
| 2990-3253 | **2979-3242** | stays in `entity_renderer.cpp` (part B) |
| 3254-3255 | **3243-3244** | namespace close, replicated 6× |

The partition is contiguous and exhaustive over body lines 30-3242.

⚠ **No range had to be moved for a comment or `#if` boundary** — J02's failure
mode does not recur here, but I checked for it rather than assuming: a
character-by-character scanner (tracking `/* */`, `//` and string literals)
confirmed **every one of the eight cut points is at block-comment depth 0**, and
each boundary is a blank line preceded by a closing `}`, so no cluster inherits
an orphaned leading banner. **`grep -n '^#\|^// ='` returns only the 25 include
lines** — no preprocessor conditional and no `// ====` banner exists anywhere in
the body, as the plan claimed.

### 2. Hazards, re-checked at my tip

1. **`getGreenCutoff`** — `grep -rn 'getGreenCutoff' src/client/graphics/`
   returns exactly two hits, the definition at `:32` and its sole caller at
   `:52` inside `getConColor`. Both are in the 30-274 stays-cluster. **No
   internal header, nothing de-static-ed.** T318 added no second caller.
2. **No anonymous namespaces, no file-scope objects, no file-local statics
   other than `getGreenCutoff`.** `grep -n '^namespace\|^static\|^const '`
   returns only `namespace EQT {`, `namespace Graphics {` and that one static.
   Nothing new arrived with T318.
3. The two `updateFirstPersonBodyDrawRule()` call sites are unqualified member
   calls and needed no change; one now lives in `entity_updates.cpp` and one in
   `entity_first_person_draw.cpp` itself.

### 3. Final line counts

| file | lines | plan estimate |
|---|---|---|
| `src/client/graphics/entity_renderer.cpp` | **540** (was 3,244) | ~540 |
| `src/client/graphics/entity_updates.cpp` | **645** | ~645 |
| `src/client/graphics/entity_mesh_build.cpp` | **612** | ~617 |
| `src/client/graphics/entity_build_step.cpp` | **495** | ~501 |
| `src/client/graphics/entity_lifecycle.cpp` | **451** | ~451 |
| `src/client/graphics/entity_first_person_draw.cpp` | **401** (was 90) | ~374 |
| `src/client/graphics/entity_interpolation.cpp` | **372** | ~372 |
| `CMakeLists.txt` | 1,910 (+5) | — |

3,244 + 90 → 3,516 across seven TUs; the +182 is five extra copies of the
29-line preamble, the 26-line include block that replaced
`entity_first_person_draw.cpp`'s five-line one, and six extra header lines.

### 4. Both proofs

`BASE=$(git merge-base fable HEAD)` = **`d3b2c9953efea9e0a62ab6ccc38ea7f5b7c0d86f`**
(the tip named at dispatch; `fable` had not moved by the time I ran it).

```
$ python3 scripts/refactor/verify_move_only.py "$BASE..HEAD" --repo /mnt/projects/fable/wt/slot3
Non-C++ files changed (not verified, review separately):
  CMakeLists.txt

Files verified: 7
Boilerplate: +174 / -2 lines (allowed)

PASS: move-only (modulo boilerplate).
```

**Clean on the first try** — `--repo` was passed from the outset, per J02's
correction. The `-2` boilerplate removals are the two comment lines of the
header sentence rewritten in §5; the `+174` is the replicated preambles.

**Line-multiset check — both halves, both pass:**

```
  range 30-274      ->   245 lines
  range 275-855     ->   581 lines
  range 856-1319    ->   464 lines
  range 1320-1933   ->   614 lines
  range 1934-2274   ->   341 lines
  range 2275-2694   ->   420 lines
  range 2695-2978   ->   284 lines
  range 2979-3242   ->   264 lines
PROOF A byte-identical: OK (3213 vs 3213 lines)
PROOF B multiset: MULTISET-OK
```

Proof A re-concatenates the eight extracted ranges **out of the new files** in
original order and compares byte-for-byte against
`git show "$BASE:src/client/graphics/entity_renderer.cpp" | sed -n '30,3242p'`.
Proof B is the multiset form, and additionally folds in
`entity_first_person_draw.cpp`'s original body (lines 20-88 at `$BASE`) on both
sides, so the append is covered too. Script:
`scratchpad/proof.py`, reproducible from `$BASE`.

### 5. The first-person feature is now whole, in one TU

`entity_first_person_draw.cpp` (401 lines) now holds, in this order:
`updateFirstPersonBodyDrawRule` and `isPlayerBodyWithheldFromRenderList` (the
T309 per-frame driver, unmoved), then `setPlayerFirstPersonMode`,
`isPlayerInFirstPersonMode`, `updatePlayerEntityPosition`,
`setPlayerEntityAnimation`, `setPlayerSpawnId`, `getPlayerModelYOffset`,
`getPlayerCollisionZOffset` and `getPlayerFirstPersonEyeOffset`. A `grep -rn
first_person src/client/graphics/` now lands on the whole feature.

**Its header comment did not still read correctly for the enlarged file** — it
ended "This TU is only the per-frame driver," which the append makes false. I
replaced that clause (2 lines removed, 8 added) with a paragraph naming
everything the TU now carries and attributing the move to J03. The T307/T309
rule paragraph, the pointer to `first_person_body_draw.h`,
`refactor/T307_official_draw_rule.md` and `review/T307/README.md` are untouched.
This is the unit's **only** non-relocation edit; it is comment text, allowlisted
by the verifier as boilerplate, and is declared here rather than absorbed
silently. Per the plan the file took `entity_renderer.cpp`'s full 25-include
block on top and kept its own `first_person_body_draw.h` include.
`first_person_body_draw.h`, `first_person_eye.h`, `first_person_camera.h` and
`animated_mesh_scene_node.cpp` were not touched.

### CMake

`grep -n 'entity_renderer\.cpp\|entity_first_person_draw\.cpp' CMakeLists.txt
tests/CMakeLists.txt` → **exactly one site**, `CMakeLists.txt:468` in
`GRAPHICS_SOURCES` (the plan said `:463`; J02's merge moved it 5 lines in the
same afternoon — the process rule about line numbers earns its place again).
All five new TUs added directly beneath it. `entity_first_person_draw.cpp` was
already listed at `:471` and needed no entry. `tests/CMakeLists.txt` names
neither file.

### 6. Acceptance

| step | result |
|---|---|
| `cmake --build build -j24` | **EXIT=0** (first invocation hit the 600 s tool ceiling mid-build and was re-run; the classification is the second run's exit code) |
| `ctest -j6` | **3,477 / 3,478 pass**, 250 s. Sole failure `AudioManagerTest.PreloadCommonSounds` — the ALSA host quirk, line 8 of `refactor/test_baseline.txt`. **Zero new failures.** `HeadlessShutdownTest.NonLifecycleCommandIsHeldNotDroppedOrApplied` passed, no re-run needed |
| `verify_move_only.py "$BASE..HEAD" --repo <slot>` | **PASS**, first try |
| line-multiset check | **byte-identical + MULTISET-OK** |
| visual gate | **Passed, 31.88 s**, and `git status --porcelain` showed **no golden change** — only the 3 modified and 5 new sources |
| `scripts/build-gles2-x86.sh` | **EXIT=0** |
| `scripts/build-arm-noble.sh` | **EXIT=0** |
| `check_file_sizes.sh --files` (all 8) | **7 ok, 1 warn, 0 fail** — the warn is `CMakeLists.txt` at 1,910, the known pre-existing WARN §Process disclaims |
| `chmod 644` on created files | done in the cut script; `git diff --stat` records no mode change |

### 7. Wall clock and concurrency

**~40 min end to end.** Reading the contracts and surveying ~10 min; **the cut,
CMake and both proofs together under 4 min** (one off-by-one in my own extract
script, caught by proof B reporting a single missing blank line — the proof
earning its keep); the bar ~26 min. **The slowest step by far was
`cmake --build -j24`: it exceeded the 600 s tool ceiling on the first
invocation** and completed on the second — J01 measured this step at ~4 min
solo, so a three-slot wave stretched it past 10 min, a >2.5x stretch, worse
than J02's 2.3x on `ctest`. `ctest -j6` itself took **250 s (4.2 min)**,
between J01's 3.0 solo and J02's 6.8 under contention. **Budget ~40 min per
unit in a three-wide wave, and expect the x86 build, not `ctest`, to be the
step that needs a second invocation.**

### 8. What the template still gets wrong

1. ⚠ **A timed-out `cmake --build` is a routine event in a three-wide wave, not
   an anomaly.** The protocol warns that a mid-link kill can silently truncate a
   test binary; that is now the *expected* path for this batch's build step, and
   every unit should plan on two invocations. It cost 10 minutes here.
2. **The plan's per-TU line estimates were 1-6 lines high across the board**
   because they counted the namespace-close as part of each cluster. Harmless,
   but the arithmetic in future J plans is systematically off by the close.
3. **`--repo` and `merge-base` both worked exactly as J02 documented** — those
   two corrections are good and need no further change.
4. The §Process instruction to take ranges "from the leading `// ====` banner"
   has no purchase in this file (there are none), and here the honest rule is
   the one I used: **cut at a blank line whose predecessor closes a method, and
   verify comment depth 0 at the cut**. Worth generalising, since J02's hazard
   and this file's cleanliness are two faces of the same check.
