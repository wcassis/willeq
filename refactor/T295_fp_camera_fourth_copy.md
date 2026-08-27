# T295 — the fourth first-person camera copy discards T76's fix whenever the player moves

Slot: slot2. x86. OWNS eqemu (noclobber) for the live proof. Class A.
Territory: `src/client/graphics/renderer_player_physics.cpp` (the
`updatePlayerMovement()` camera block ~690-740), the shared first-person
camera helper it should call instead, `entity_renderer.cpp`'s dead
`updateFirstPersonWeapons()` + its declaration, tests. Do NOT touch the near
plane or add a viewmodel — that is a separate decision (see below). Do NOT
touch ui_renderer / paperdoll (T294 slot1).

## Finding (audit 2026-08-27, verified at the tip)
T75's Review claims it removed "the last duplicate" of the first-person
camera formula, but that de-duplication was at `irrlicht_renderer.cpp:2683`
(the server-position path). **A FOURTH copy lives in
`renderer_player_physics.cpp:690-740`**, the per-frame LOCAL movement path.
It sets the camera from the head bone / bbox eye height directly and applies
**neither `kFirstPersonCameraStandoffUnits` (0.3) nor the 12° down-pitch** —
it uses `playerPitch_`. It runs whenever `positionChanged || headingChanged`.

**So T76's only shipped fix is overwritten the moment the player moves or
turns** — exactly the "while moving" case. Anyone re-testing first person in
motion is testing the pre-T76 camera without knowing it.

## Scope
1. Route that path through the SAME helper the other three use (one body —
   T230/T239 rule). If no helper exists, extract one; do not add a fifth
   formula. A test must fail if a fourth formula reappears (the T264
   source-parsing style is the precedent).
2. Prove it live: enter first person, stand still, capture; then move and
   turn, capture again — the camera origin/pitch must be identical in both
   (log the computed eye position and pitch each frame at DEBUG and diff
   them). Before the fix they differ; after, they do not. **This is a camera
   correctness proof, NOT a claim that first person looks right** — it does
   not, and that is T296's question.
3. **Delete `updateFirstPersonWeapons()`** (`entity_renderer.cpp:2831`, decl
   `entity_renderer.h:407`): zero callers anywhere in src/include/tests, and
   it is a near-no-op that re-calls `updateEquipmentTransforms` which the
   normal per-entity update already does at six sites. Reachability pattern
   #9's last surviving member — strike it on the board with the grep proof.
4. While you are there, confirm T76's head-bone eye-height lever actually
   engages live: its own captures logged the BBOX fallback (3.65), not the
   head bone (3.10), so the lever is proven only headlessly. Report which
   fires live; do not change it.

## Explicitly NOT in scope
The near clip plane and any viewmodel pass. T76 measured that arm geometry
straddles the 1.0 near plane in EVERY pose and that no camera-position lever
clears it; fixing that is an approach decision for Will (T296 will carry it).

## Verification
Full ctest (`-j6`); visual gate; Review; evidence review/T295/ with the
still-vs-moving camera logs. Commit:
`T295: one first-person camera formula; the movement path no longer discards it`.

---

# Review (executor, slot2, 2026-08-27)

**The plan's finding is CONFIRMED, and the live proof holds. The camera
origin and pitch are now identical standing still and moving/turning. Before
the fix they were not — the horizon jumped 88 pixel-rows the instant the
player moved, and did not come back.**

**This is a camera-correctness result and nothing more. First person does NOT
look correct** — the captures show a flat untextured ground plane, a band of
sky, and (standing still) an unidentifiable blurred brown slab. That question
is the near-plane/viewmodel decision, explicitly out of scope.

## 1. The fourth copy — routed through the one helper

`renderer_player_physics.cpp:690-740` is gone. `updatePlayerMovement()` now
ends with:

    if (cameraMode_ == CameraMode::FirstPerson &&
        (positionChanged || headingChanged || pitchChanged)) {
        repositionFirstPersonCamera();
    }

Two things had to move INTO the helper for that to be a true de-duplication
rather than a regression, and both are flagged here rather than absorbed:

**(a) mouse-look pitch (`playerPitch_`) was ONLY in the duplicate.** The
helper hard-coded the 12-degree bias and read `playerPitch_` nowhere; the
duplicate read `playerPitch_` and applied no bias. Deleting the duplicate
naively would have silently disabled vertical mouse-look in first person. The
helper now applies `playerPitch_ - kFirstPersonPitchDownRad` and builds the
look target from a unit direction instead of T75's small-angle `tan()` drop.
**At `playerPitch_ == 0` this is the SAME look ANGLE as T75's** — the drop
over the horizontal run is `tan(kFirstPersonPitchDownRad)` either way; only
the target's distance along the ray changes, which a look-at camera does not
care about. Verified live: every logged frame reports
`totalPitch - lookPitch == -12.0000deg` exactly.

**(b) the block's trigger condition.** It keyed off `positionChanged ||
headingChanged` only, so a PURE vertical mouse drag (pitch changes, heading
and position do not) never repositioned the camera at all. `pitchChanged` is
now part of the condition. **Deviation from the plan's literal scope**,
flagged: it is a one-line adjacent correctness fix in the same block, and
without it the pitch half of the proof cannot be exercised.

The entity-position update and the Follow-mode branch were left inside the
original `positionChanged || headingChanged` guard, unchanged.

## 2. A test fails if a fifth formula appears

`tests/test_first_person_camera_single_formula.cpp` (new TU, 4 tests, T264
source-parsing style — a compile-time check cannot see a duplicate, since
every copy compiles):

1. `OnlyTheHelperTUDrivesTheCameraInFirstPerson` — no file under
   `src/client/graphics/` (recursive, `ui/` included) may both mention
   `CameraMode::FirstPerson` and write `camera_->setPosition/setTarget`,
   except `irrlicht_renderer.cpp`. Line comments are stripped first, so the
   explanatory comments this unit left behind are not counted as copies.
2. `TunablesAreReferencedFromOneTUOnly` — `kFirstPersonCameraStandoffUnits`
   and `kFirstPersonPitchDownRad` are each read from exactly one TU.
3. `TheOneBodyAppliesBothTunablesAndMouseLookPitch` — parses the helper's own
   body by brace depth and asserts it still uses both tunables AND
   `playerPitch_`. Without this, 1 and 2 would both still pass if the helper
   quietly stopped applying a fix.
4. `DeadFirstPersonHelpersStayDeleted` — pins the two deletions below.

**Negative control run** (`review/T295/t295_test_negative_control.txt`): with
`renderer_player_physics.cpp` restored to its pre-T295 content, 2 of the 4
fail and name the file:line — `OnlyTheHelperTUDrivesTheCameraInFirstPerson`
and `DeadFirstPersonHelpersStayDeleted` (`renderer_player_physics.cpp:701`).
The source was then restored and all 4 pass.

## 3. Deletions — reachability pattern #9 is CLOSED

- **`EntityRenderer::updateFirstPersonWeapons()`** + decl — zero callers in
  `src/`, `include/`, `tests/` (grep proof; the only hits were the plan and
  historical `refactor/*.md` prose). A near-no-op that re-called
  `updateEquipmentTransforms()`, which the normal per-entity update already
  does at six sites.
- **`EntityRenderer::getPlayerHeadBonePosition()`** + decl — **this unit's
  change left it caller-less**, so deleting it is part of the same job rather
  than a fresh instance of the pattern. Since T75 it had not read a head bone
  at all: it returned `getPlayerEyeHeightFromFeet()` added to the player
  visual's last position, a second spelling of what the helper already
  computes from `playerZ_`.

`triggerFirstPersonAttack()` was already gone from the tree and
`setPlayerFirstPersonMode()` was wired by T74 (three production callers
today). With `updateFirstPersonWeapons()` deleted, **pattern #9 has no
surviving members**; `refactor/deferred.md` is annotated CLOSED at the
pattern's own entry with that accounting.

## 4. The live proof

Two legs, identical driver / script / zone / character / start position /
daylight command / key sequence, differing only in the binary (before built
from a `git stash` of this unit's changes, after from the fix; md5s differ
and are recorded). Testchar was reset in the DB to the canonical qeynos2
position between legs, and restored afterwards.

**Horizon row** (sky/ground boundary, centre column — a direct readout of
camera pitch; `review/T295/t295_horizon_rows.txt`):

| | BEFORE | AFTER |
|---|---|---|
| still (2 shots) | 202, 202 | 202, 202 |
| moving forward | **290** | **202** |
| turning | **290** | **203** |
| just after motion | 51 | 212 |

**Log invariants** (`review/T295/t295_camera_invariants.txt`): the helper now
logs one `[FP-CAMERA]` line per call at DEBUG, and being the only camera
writer, that log is the complete record. 445 lines — 1 from the mode switch
(still) and 444 from the per-frame movement path. In both phases `standoff`
is the single value 0.3, `totalPitch - lookPitch` the single value -12.0deg,
and `camZ - feet - eyeHeight - adjust` exactly 0.0. The moving phase spans
442 distinct camera X positions and 189 distinct headings, so it is genuinely
the movement path running, not a repeated frame.

The BEFORE log has exactly ONE camera line in the whole session (the mode
switch) — the duplicate logged nothing, which is part of why this survived
three de-duplication passes.

## 5. Plan item 4 — which eye-height lever fires live: the BBOX FALLBACK

    [FP-EYE] source=BBOX_FALLBACK headBoneIdx=-1 eyeHeight=3.6527

`getBoneIndexByName("HEAD")` returns -1 on Testchar's model (Human male), so
`getPlayerEyeHeightFromFeet()` takes T75's bbox-top path every frame. T76's
head-bone lever is therefore **proven headlessly only, and does not engage
live on this race** — which is exactly why T76's own captures logged 3.65 and
not 3.10. Per the plan this was NOT changed; T295 only added the throttled
DEBUG discriminator that lets a live run say which branch fired.

## 6. Verification

| step | result |
|---|---|
| `cmake --build build -j24` | exit 0 |
| `ctest -j6` | **3212/3213**; sole failure `AudioManagerTest.PreloadCommonSounds`, the documented ALSA host quirk. The 4 new tests pass. |
| `scripts/build-gles2-x86.sh` | exit 0 (run twice: before-binary and after-binary) |
| visual gate | **PASS**, goldens untouched (`git status` clean of `refactor/goldens`) |
| `scripts/build-arm-noble.sh` | exit 0 |
| `check_file_sizes.sh` | 2 FAILs, **both pre-existing**: `entity_renderer.cpp` 3295 (was 3340 at base — this unit REDUCED it by 45 and it is still over) and `irrlicht_renderer.cpp` 3074 (was 3059 at base — already FAIL before this unit; +15 lines here, all inside the one helper and its comment). 1 pre-existing WARN on `renderer_player_physics.cpp` 1527, reduced from 1558. No NEW FAIL. |

**Two infrastructure hazards hit and worth recording, neither a source
defect:**

1. **ctest died at discovery three times** with
   `Process terminated due to timeout` on a test binary — `test_wire_layout_t145`,
   then `test_loot_window_actions`. The binaries were INTACT (they ran and
   listed tests fine). Cause: the test binaries live on NFS and the tree's
   `libwilleq_core.a` is **1.9 GB**, so a freshly relinked binary takes ~8.5s
   to page in cold — over gtest's 5s DISCOVERY_TIMEOUT. Reading every binary
   once (`xargs -P12 dd`) before ctest fixes it. This is NOT the T293
   truncated-binary failure mode and should not be mistaken for it: there the
   binary is broken, here it is merely cold.
2. **`check_build_tree_orphans` failed on a CORRUPT CMake file-API reply.**
   `build/.cmake/api/v1/reply/target-<name>-<hash>.json` files contained a
   DIFFERENT target than their filename claimed (e.g.
   `target-test_packet_structs-*.json` described
   `test_eq_action_handler_movement_utility`), so only 160 of 197 artifacts
   were claimed and 37 live targets were reported as orphans. Left behind by
   a `cmake` generate that a 600s tool timeout killed mid-write. Deleting the
   reply directory and reconfiguring gives 197/197 PASS.

## 7. Deviations

- `pitchChanged` added to the camera trigger condition (section 1b) —
  adjacent one-line fix, flagged not absorbed.
- `getPlayerHeadBonePosition()` deleted in addition to the plan's named
  `updateFirstPersonWeapons()` — it became caller-less as a direct result of
  this unit's change (section 3).
- The helper's `LOG_INFO` became `LOG_DEBUG`. It now runs every frame the
  player moves; at INFO it would flood the log.
- The eqemu lock was held by T294 for the first ~3.5 hours of this unit. It
  was NEVER overwritten; a bounded background poller took it atomically
  (`set -o noclobber`) the moment T294 released. All non-server verification
  was done while waiting. Released, server shut down, Testchar restored to
  zone 36 / -77.9232, 21.9086, 3, h=190.

## 8. Not done, on purpose

The near clip plane and any viewmodel. T76 measured that arm geometry
straddles the 1.0 near plane in every pose and that no camera-position lever
clears it. Untouched here.
