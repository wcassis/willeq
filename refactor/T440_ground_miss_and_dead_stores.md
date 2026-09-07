# T440 — a total ray miss is NoFloor (B438); the diagnostic ground stub and two dead skill fields (B415, B416)

Class A/B, slot2, no server. Read B438, B415, B416; T437's §Review
(`GroundZ` type, `ShouldLandJump()`, the `findGroundZIrrlicht()` stub
with five ternary call sites that only warns), T429 Leg B's §Review
(the skill fields), `skill_data.h`, `skill_manager.cpp`,
`renderer_player_collision.cpp` / `renderer_player_physics.cpp`.

1. B438: `findGroundZBspFiltered()` answers `GroundZ::NoFloor()` on a
   total miss and when there is no BSP tree; every consumer already
   handles `NoFloor` since T437 — verify each (a falling player keeps
   falling; the walking step-height path and wall slides use
   `zOr(currentFeetZ)`), and say what the player does over a real hole
   (falls until the next floor / zone geometry; never lands in mid-air).
   Pre-fix failing test in the device-free `ground_query` seam and, if
   reachable, on the BSP filter with a hand-built tree.
2. `findGroundZIrrlicht()`: a `LOG_WARN` + return-feet stub referenced by
   five ternaries. Delete it and the ternaries (the BSP path is the only
   path); if a non-BSP zone exists in the era assets, say which and what
   happens there now (NoFloor). Census pin if one covers that TU.
3. B415: delete `SkillData::animation_code` (the one consumer calls the
   free function) — or make `SkillManager` read it; pick by which
   removes a duplicate store. B416: `usable_while_sitting` — read
   eqemu's per-opcode handlers (`/mnt/projects/akk-stack/code/zone/…`,
   quoted absolutely) for a sitting refusal as T325 read them for
   in-combat; wire the predicate where eqemu refuses, delete the field
   where it does not. No guessing: the handler text decides.

## Proof

Build (background), `ctest -j6` (no expected reds), pre-fix/post-fix,
G05/G06 in place (G06 pins the movement TU), visual gate standalone +
ARM (no image golden should move), file sizes, WriteOnlyFields/census
pins learn deletions. Strike B438/B415/B416; new items B445+ (T439 in
slot1 also allocates from B445 — say which you used).

STOP: an image golden moves.

## Review

Branch `task/T440-ground-miss-dead-stores`, base `9d646f42` (verified at
checkout). One commit, this one.

### 1. B438 — a total ray miss is NoFloor

`findGroundZBspFiltered()`'s verdict used to open with
`float groundZ = feetZ;` and, when neither the near nor the full raycast hit
anything at all, fall through to `return GroundZ::Floor(groundZ)` — "the floor
is exactly under your feet", whatever column the player is over.
`ShouldLandJump()` then read `feetZ <= feetZ`, agreed, and ended a fall in
mid-air.

The verdict now lives in a device-free free function beside T437's two,
`EQT::Graphics::ClassifyBspGroundRay(hit, hitZ, feetZ, maxStepUp)`
(`include/client/graphics/player/ground_query.h`,
`src/client/graphics/player/ground_query.cpp`), and it is used in all three
places the query spelled the predicate inline — phase 1's near hit, phase 2's
debug `validFloor`, and the final verdict:

| input | pre-T440 | now |
|---|---|---|
| ray hit nothing at all | `Floor(feetZ)` | **`NoFloor()`** |
| hit above `feetZ + maxStepUp + 0.1f` (a ceiling / overhang / platform underside) | `NoFloor()` (T437) | `NoFloor()`, same predicate |
| hit within reach | `Floor(hitZ)` | `Floor(hitZ)` |
| no BSP tree at all (`!ctx_.zoneBspTree`) | `Floor(currentZ - modelYOffset)` | **`NoFloor()`** |

Two smaller consequences, both stated rather than absorbed:

* **The boat check now runs against the verdict, not against a phantom.** It
  used to compare `boatDeckZ > groundZ` where `groundZ` was the phantom
  `feetZ`, so a deck BELOW the feet was discarded and the phantom won; and a
  ceiling hit returned before the boat check ran at all. It is now
  `!ground.hasFloor() || boatDeckZ > ground.z()` — the same shape
  `findGroundZ()` already used one function above
  (`groundZ == BEST_Z_INVALID || boatDeckZ > groundZ`). `findBoatDeckZ()`
  bounds its own answer to the player's z band
  (`src/client/graphics/entity_spatial.cpp:205`), so it cannot report a deck
  the player is nowhere near.
* **The `!ctx_.zoneBspTree` early return is unreachable**, and was changed for
  honesty, not for behaviour: `resolveIrrlichtCollision()` is entered only when
  `ctx_.regionWorldTriangles` is non-empty
  (`renderer_player_physics.cpp:330`), and those triangles are built from
  `regionMeshNodes`, which exist only when a BSP tree was installed.

**Every consumer verified** (all in `renderer_player_physics.cpp`; the table is
T437's, re-read against `NoFloor` arriving from a MISS rather than only from a
ceiling):

| consumer | with NoFloor | player-visible |
|---|---|---|
| jump landing, clear path (`ShouldLandJump`) | false | the fall continues |
| jump landing, blocked path | false | the fall continues |
| swim floor clamp ×2 (`ShouldClampSwimToFloor`) | false | no clamp to a phantom bottom |
| walking step height | `zOr(currentFeetZ)` → step 0, but both branches carry `groundZ.hasFloor() &&`, so neither fires | the step is refused: a walker does not stride out over a column with nothing under it |
| wall slide X / Y | same `hasFloor()` guard | no slide onto a phantom |
| `irrlicht_renderer.cpp:687` NPC ground snap | `zOr(currentZ)` — reaches `findGroundZ()`, not this function | unchanged |

**What a player does over a real hole now.** He falls. The full ray searches
500 units below the feet, so an ordinary pit, shaft or ledge answers with its
own floor and the fall ends there, at THAT floor — `newZ = ground.z() +
modelYOffset`. Only a column with nothing at all within that reach answers
NoFloor, and then the fall simply continues into the next frame's query, until
some geometry (or the server's own correction) is under him. He never lands in
mid-air, and he is never lifted by one model offset per frame. Walking into
such a column is refused instead of accepted — the move is not applied — so a
player cannot walk out over a void; he can still fall into it, because falling
is a jump and a jump keeps its horizontal movement.

**No live repro was attempted** and none is possible headless: the query needs
a live Irrlicht device, a loaded zone with region triangles and a BSP tree, and
the landing needs an event receiver, an entity renderer and a bridge. That is
**B437**, already on the board from T437, and it covers this fix too. This unit
held no eqemu lock (T428 has it) and opened no connection.

### 2. `findGroundZIrrlicht()` deleted, with its five ternaries

It was `LOG_WARN` + `return GroundZ::Floor(currentZ - modelYOffset)`. All five
call sites were `useBspFiltered ? findGroundZBspFiltered(...) :
findGroundZIrrlicht(...)`, and **`useBspFiltered` can never be false there**:
it is `ctx_.zoneBspTree && !ctx_.regionWorldTriangles.empty()`, evaluated
inside a body the caller only enters when `regionWorldTriangles` is already
non-empty. Gone from `renderer_player_collision.cpp`, from the five ternaries
in `renderer_player_physics.cpp`, and from `player_camera_component.h`.

**Is there a non-BSP zone in the era assets?** Measured, not assumed —
`review/T440/zone_bsp_pvs_census.cpp` parses every `<zone>.wld` in
`/mnt/projects/EverQuestP1999` through the real `WldLoader` and applies
`renderer_zone_load.cpp:406`'s own fallback condition
(`!bspTree || regions.empty() || !hasPvsData()`). **277 zone WLDs: 258 take the
BSP+PVS path, 19 take the fallback, and 18 of the 19 are `*equip*` item
archives plus `timorous_lit`, not playable zones.** The one real zone is
**`sky` (Plane of Sky): 0 regions, no BSP tree.** What happens there now:
nothing changes. With no region triangles the movement code never enters
`resolveIrrlichtCollision()` at all — it takes `resolveHCMapCollision()`, the
server `.map` path (`/mnt/projects/akk-stack/server/maps/base/sky.map` exists,
1,446,631 bytes), which this unit does not touch. Full output in
`review/T440/zone_bsp_pvs_census.txt`.

**Its sibling `checkCollisionIrrlicht()` is the same shape and was left
alone** — it is out of this plan's scope and is now boarded as **B445**.

### 3. B415 / B416 — both fields DELETED

**B415 `SkillData::animation_code`.** Deleted, because deleting is what removes
the duplicate store: `initializeSkillData()` copied
`getSkillAnimationCode(skill_id)` into a `std::string` field, and the one
consumer that wants a skill's skeletal code —
`SkillManager::playSkillAnimation()`,
`src/client/skill/skill_manager.cpp:719` — calls the free function directly.
Making `SkillManager` read the field instead would have kept a second copy of a
value the table already owns.

**B416 `SkillData::usable_while_sitting`. The handler text decided: DELETE.**
Read per T325's method, per opcode, quoting absolutely.

The whole zone server has exactly **two** sitting refusals for a player-initiated
action, both in `/mnt/projects/akk-stack/code/zone/special_attacks.cpp`, and
both are the same `IsSitting() ||` term inside a refusal list —
`Client::RangedAttack()` at line 976:

```
	if (!IsAttackAllowed(other) ||
		IsCasting() ||
		IsSitting() ||
		(DivineAura() && !GetGM()) ||
		IsStunned() ||
		IsFeared() ||
		IsMezzed() ||
		(GetAppearance() == eaDead)) {
		return false;
	}
```

and `Client::ThrowingAttack()` at line 1597 (identical but for
`(IsCasting() && GetClass() != Class::Bard)`).

**Neither is reachable from fable.** `Client::OPCombatAbility()`
(`special_attacks.cpp:325`) routes to them only inside
`if (ca_atk->m_atk == EQ::invslot::slotRange)` — and
`CombatManager::UseAbility()` (`src/client/combat.cpp:645`) writes that field
as the literal `100`:
`packet.PutUInt32(4, 100);  // attack value (real client sends 100)`.
Archery(7) and Throwing(51) are also not in `isActivatableSkill()`
(`include/client/skill/skill_constants.h:255`), so fable never asks for them as
skills at all; ranged attacks reach `RangedAttack()` on the server's own
auto-attack timer (`client_process.cpp:363`), not from a skill packet.

Beyond those two: **not one** of the seventeen dedicated skill opcode handlers
in `/mnt/projects/akk-stack/code/zone/client_packet.cpp` reads the appearance —
a `Sitting|eaSitting|IsSitting` grep over that file returns only pet
appearance-setting and `Handle_OP_ClientUpdate`'s own `SetAppearance`. The
only stand-up gate anywhere in the server is for **spells**, at
`/mnt/projects/akk-stack/code/zone/aa.cpp:1323`, and it stands the caster UP
rather than refusing:

```
	if (!IsCastNotStandingSpell(rank->spell)) {
		if (GetAppearance() == eaSitting) // we need to stand!
			SetAppearance(eaStanding, false);
```

And the field was **backwards as a gate**: it was true for Meditate(31) and
false for everything else, so a predicate over it would have refused every
OTHER skill while sitting — exactly the over-strictness that cost the rogue his
opener in T325. Both deletions carry a tombstone comment in `skill_data.h`
naming the evidence, so the fields cannot come back by guess.

The two `EXEMPT` rows went out of
`scripts/refactor/check_write_only_fields.py` with the fields — an exemption
may not outlive its row, which is the ratchet's own second half.

### Tests, pre-fix red and post-fix green

Five new cases in `tests/test_ground_sentinels.cpp`, suite `GroundMissT440`,
driving `ClassifyBspGroundRay()` together with T437's two decisions.

**The pre-fix red** is produced the way T437 produced its own: the pre-T440
tree has no seam this test could call — that absence is the defect — so the
DEFECT is restored in place while the scaffolding is kept. `if (!hit) return
GroundZ::NoFloor();` becomes `if (!hit) return GroundZ::Floor(feetZ);`, in a
copy of the TU (`review/T440/`'s recipe), linked ahead of
`build/lib/libwilleq_core.a` so the archive's own `ground_query.o` is not
pulled. No `git stash`, no `git checkout --`, nothing written into the tree.

| run | result |
|---|---|
| `test_ground_sentinels`, defect restored | **3 FAILED, 14 passed** — `ARayThatHitsNothingReportsNoFloor`, `AFallingPlayerOverAHoleKeepsFalling`, `AMissAndACeilingGiveTheSameAnswerAndItIsNotAFloor` |
| `test_ground_sentinels`, fix in place | **17 / 17 passed** |
| `WriteOnlyFields` census with the pre-T440 EXEMPT table against this tree | **exit 1** — `0 unlisted, 2 stale`, naming `SkillData::animation_code` and `SkillData::usable_while_sitting` |
| `WriteOnlyFields` census as committed | **exit 0** — `write-only census clean: 67 fields, all 67 classified` (was 69) |

The two ceiling cases in the same suite stay green under the restored defect,
which is the point: this is a DIFFERENT defect from B435 in the same function,
and the tests separate them.

### G06 — the movement golden learned the edit IN PLACE

`tests/goldens/movement_order_g06.txt`, **473 → 463**. Five three-line
ternaries each collapsed to the one statement that replaced them, at the same
position. **Zero insertions, zero reorderings, no other line touched** — the
learning was done by scripted group-substitution over the golden's own text,
not by regeneration from the tip, and the `kGoldenSize` comment in
`tests/test_movement_order.cpp` names the class of edit and why.

G05 pins `renderer_frame_render.cpp`, which this branch does not touch.

### Verification

| step | result |
|---|---|
| `cmake --build build -j24` (RelWithDebInfo, GLES2 config, background + exit file) | **exit 0** (no exit 143 anywhere in this unit, so no truncation scan was owed) |
| `ctest --test-dir build -j6` | **4562 / 4562, 0 failed** |
| `test_ground_sentinels` | **3 red pre-fix → 17 green post-fix** |
| `test_movement_order` | **4 / 4** after G06 learned the edit |
| `WriteOnlyFields` | **red with the stale table → green as committed** |
| `scripts/build-gles2-x86.sh` | **exit 0** |
| `scripts/ci/visual_gate.sh` (standalone) | **PASS** — qeynos2 3.924/10.108, gfaydark 5.556/10.672, befallen 6.316/10.948 |
| image goldens | **none moved** — `git status` shows only `movement_order_g06.txt` under `tests/goldens/` |
| `scripts/build-arm-noble.sh` | **exit 0** |
| `check_file_sizes.sh fable` | **17 ok, 0 warn, 0 fail**; `renderer_player_physics.cpp` 1,001 (it SHRANK by 10), `renderer_player_collision.cpp` 681, `skill_data.h` 303, `ground_query.h` 96, `ground_query.cpp` 43, `test_ground_sentinels.cpp` 358 |
| eqemu | never contacted — T428 holds the lock, this unit is offline |

### Board

| entry | action |
|---|---|
| **B438** | STRUCK — a total miss (and a null BSP tree) is NoFloor; every consumer verified, five ternaries and the stub deleted with it |
| **B415** | STRUCK — `animation_code` DELETED; the free function is the source |
| **B416** | STRUCK — `usable_while_sitting` DELETED; eqemu's only two sitting refusals are unreachable from fable, quoted above |
| **B445** | NEW (B) — `checkCollisionIrrlicht()` is `findGroundZIrrlicht()`'s surviving twin: a `LOG_WARN` + `return false` reached from five `useBspFiltered ? ... : ...` ternaries whose selector can never be false inside `resolveIrrlichtCollision()`. Out of this plan's scope; delete it and the ternaries the same way |
| **B446** | NEW (B) — `RendererContext::fallbackMeshNode` is **never assigned a non-null value anywhere in the tree**. The only two assignments are `= nullptr`; six other sites (zone draw, zone load, diagnostics ×4, lifecycle) test it, remove it, drop it, swap its material and re-parent it. A grep over `src/`, `include/`, `tests/`, `tools/`, `benchmarks/` and `docker/` finds no creator |

B445/B446 are the numbers this unit used. **T439 in slot1 also allocates from
B445** — renumber at merge if it collided.

### Deviations

1. **The pre-fix red restores the defect rather than running the new test
   against the old tree**, for T437's reason: the old tree has no seam. It is
   linked as a standalone binary against the built `libwilleq_core.a` instead
   of being patched into the worktree, so the checkout was never dirtied.
2. **The boat check's guard changed** (see §1). It is a behaviour change beyond
   "a miss is NoFloor", it makes a deck reachable in two cases where the
   phantom floor used to win, and it matches the sibling `findGroundZ()`'s
   existing shape. Stated, not absorbed.
3. **The `!ctx_.zoneBspTree` early return is unreachable** and was still
   changed, because the plan asked for it and because an unreachable branch
   that lies is how B438 got written in the first place.
4. **`checkCollisionIrrlicht()` was NOT deleted** although it is the same shape
   as the stub this unit removed. The plan named `findGroundZIrrlicht()`;
   boarded as B445 rather than folded in.
