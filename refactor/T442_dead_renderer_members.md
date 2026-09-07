# T442 — dead renderer members and enumerators (B450, B451, B452)

Class B (structural; stale code = defect), slot2, no server. Read B450
(five dead `PopupType` enumerators), B451 (`checkCollisionIrrlicht()`
warn-and-return stub behind five unreachable ternaries — T440 deleted
its twin `findGroundZIrrlicht()`; follow the same recipe), B452
(`IrrlichtRenderer::fallbackMeshNode_` is never non-null yet nine sites
handle it), T440's §Review (the census that proved the ternaries dead;
the sky-zone HCMap path), and the census tests that pin enumerators /
members (`WriteOnlyFields`, reachability sweep, G05/G06).

1. B451: delete `checkCollisionIrrlicht()` and its five ternaries; prove
   the branch was unreachable the way T440 did (the `useBspFiltered`
   invariant) and pin it.
2. B452: delete `fallbackMeshNode_`, the `RendererContext` binding and
   the nine handling sites (scene breakdown count, material swap,
   re-parent, drop…); `createZoneMesh()`'s `zoneMeshNode` is the real
   fallback — say what each deleted site would have done and confirm
   `zoneMeshNode` already gets it where it matters (or board the gap).
3. B450: delete the five unused `PopupType` enumerators and every
   comment/doc naming them; the census test T439 wrote learns the new
   set.
4. Docs: `include/client/graphics/CLAUDE.md`, `module_map.md`, the
   renderer docs — strike every mention (DocClaims/DocCounts must stay
   green).

## Proof

Build (background), `ctest -j6` (no expected reds), pre-fix failing
census/reachability tests where a pin exists (the deletion itself is
the fix for a stale-doc/dead-code defect; a source census test in the
T436/T439 shape is the pre-fix red), G05/G06 in place (deletions only —
zero insertions, zero reorders), visual gate standalone + ARM (no image
golden should move; `fallbackMeshNode_` is null in every gate frame),
file sizes. Strike B450/B451/B452; new items B453+ (T441 in slot1
allocates from B453 too; say which you used).

STOP: an image golden moves; a `fallbackMeshNode_` site turns out to be
reachable with a non-null node (then report, do not delete).

## Review

Branch `task/T442-dead-renderer-members`, base `9825e098` (verified at
checkout). One commit, this one. No eqemu contact: T438 holds the lock and
nothing in this unit opens a connection.

### 1. B451 — checkCollisionIrrlicht(), its five ternaries, and the selector

It was a `LOG_WARN(... "CALLED — old meta selector path!")` plus
`return false` — "nothing blocks you" — and every one of its five call sites
was `useBspFiltered ? checkCollisionBspFiltered(...) : checkCollisionIrrlicht(...)`.

**The unreachability proof is T440's, re-taken.** `useBspFiltered` was
`ctx_.zoneBspTree && !ctx_.regionWorldTriangles.empty()`, declared inside
`resolveIrrlichtCollision()` — and `resolveMovementCollision()` enters that
body only under `hasCollision && useIrrlicht`, where `useIrrlicht` is
`!ctx_.regionWorldTriangles.empty()`
(`src/client/graphics/player/renderer_player_physics.cpp`, the
`regionWorldTriangles.empty()` guard the census now pins). The second
conjunct is therefore already true on entry, and the first cannot be false
with it: `regionWorldTriangles` is built from `regionMeshNodes`, which exist
only when a BSP tree was installed. **The false arm never ran.**

Deleted:

| site | what went |
|---|---|
| `src/client/graphics/player/renderer_player_collision.cpp:239` | the function body, replaced by a tombstone beside T440's |
| `include/client/graphics/player/player_camera_component.h:295` | the declaration |
| `renderer_player_physics.cpp:373-375` | horizontal check ternary |
| `renderer_player_physics.cpp:462-463` | swim ceiling check, clear path |
| `renderer_player_physics.cpp:565-566` | swim ceiling check, blocked path |
| `renderer_player_physics.cpp:591-592` | wall slide X |
| `renderer_player_physics.cpp:608-609` | wall slide Y |

**Beyond the plan's letter, and stated rather than absorbed: the
`useBspFiltered` selector went too.** T440 left it alive only because these
five ternaries still read it; with them gone it was a dead local and a dead
parameter on `resolveIrrlichtClearPath()` and `resolveIrrlichtBlockedPath()`
(both signatures shortened, in the TU and in the header). Keeping a dead
selector would have been the same defect class this unit exists to remove.
The census asserts `useBspFiltered` appears nowhere in the TU's code.

**Behaviour: none.** Every one of the five sites now calls the function the
selector always chose.

### 2. B452 — fallbackMeshNode_, a member nothing ever created

**The reachability proof.** `IrrlichtRenderer::fallbackMeshNode_` was
`= nullptr` at its declaration; a grep over `src/`, `include/`, `tests/`,
`tools/`, `benchmarks/` and `docker/` for `fallbackMeshNode[_]*\s*=` finds
exactly two assignments and both are `= nullptr`. It was bound into
`RendererContext` as a REFERENCE (`IMeshSceneNode*&`), so that binding is the
only alias, and the three functions it was passed to
(`deleteMeshHardwareBuffers`, `collectMeshBuffers`, the `swapNode` lambda) all
take `IMeshSceneNode*` **by value** — none could write it back. **No site was
reachable with a non-null node, so the STOP condition did not fire.**

`createZoneMesh()` is the real "single zone mesh" fallback the name referred
to: `ctx_.zoneMeshNode = ctx_.smgr->addOctreeSceneNode(mesh, nullptr, -1, 256)`
(`src/client/graphics/zone_lifecycle/renderer_zone_draw.cpp:183`), called from
the no-BSP/PVS install arm of `loadZoneStep2`
(`src/client/graphics/zone_load/renderer_zone_load.cpp:412`). Both are pinned
by the census.

Eleven sites deleted. What each would have done, and whether `zoneMeshNode`
covers it:

| site | would have done | covered by `zoneMeshNode`? |
|---|---|---|
| `zone_lifecycle/renderer_zone_lifecycle.cpp:177-181` | on zone unload: delete VBOs, `remove()`/`drop()`, null it | **yes** — the same block runs for `zoneMeshNode` eleven lines above |
| `zone_lifecycle/renderer_zone_draw.cpp:321` | keep `drawZoneGeometrySorted()` from early-returning on an empty sorted list | **n/a** — the term was always false, so the guard is unchanged in behaviour |
| `zone_lifecycle/renderer_zone_draw.cpp:599-601` | add the fallback's mesh buffers to the sorted draw list at max distance (drawn last) | **n/a** — this is the PVS manual-draw path; a no-BSP zone has `usePvsCulling == false` and draws `zoneMeshNode` through Irrlicht's own scene graph, not this list |
| `zone_lifecycle/renderer_zone_draw.cpp:716-724` | `drawRegionMesh()`'s `regionIdx == SIZE_MAX` branch, resolving the sentinel to the fallback node | **deleted with it** — that sentinel had exactly one caller (the row below), so the branch is unreachable without it; `drawRegionMesh()` now looks up `regionMeshNodes` only |
| `zone_lifecycle/renderer_zone_draw.cpp:840-843` | after the portal-stencil pass, draw the geometry in no BSP region | **n/a** — same reason as the sorted-list row; the portal path exists only in a BSP zone |
| `zone_load/renderer_zone_load.cpp:424-427` | pre-install cleanup of a previous zone's fallback node (log line reworded) | **yes** — `zoneMeshNode` is removed six lines above in the same block |
| `zone_load/renderer_zone_load.cpp:755-757` | `grab()`+`remove()` it from the scene graph when manual zone draw is enabled | **n/a** — the BSP path only; `zoneMeshNode` was already removed at install |
| `diagnostics/diagnostics_component.cpp:214` | subtract it from the `/dumpscene` `otherNodes` count and insert it into `knownNodes` | **NO — this is the gap, boarded as B453** (below) |
| `diagnostics/diagnostics_debug.cpp:282` | swap its material on a per-pixel/lightweight shader swap | **yes** — `swapNode(ctx_.zoneMeshNode)` is eight lines above |
| `diagnostics/diagnostics_debug.cpp:307` | report `fallback=<bool>` in the swap log line | **n/a** — always `false`; the field was dropped from the format |
| `diagnostics/diagnostics_debug.cpp:359` | swap it to the debug material | **yes** — `swapNodeToDebug(ctx_.zoneMeshNode)` is two lines above |
| `diagnostics/diagnostics_debug.cpp:458,477` | remove/re-add it from the scene graph on the `/manualdraw` toggle | **n/a** — `/manualdraw` refuses outright without PVS culling, so a no-BSP zone never reaches either arm |

Also deleted: the `RendererContext` binding (`renderer_context.h:163`) and the
matching constructor argument in `renderer/renderer_init.cpp:218`.

**The one gap, boarded not absorbed (B453).** The `/dumpscene` scene
breakdown subtracts every other zone-side node it can find but has never
subtracted `zoneMeshNode`. The deleted `fallbackMeshNode` subtraction was
itself guarded on `getParent()` and so never fired either, which is why the
breakdown has been blind to the real zone mesh for as long as both existed. In
Plane of Sky — T440 measured it as the ONE playable zone with no BSP tree and
no regions — `/dumpscene` therefore prints `unaccounted=1` and enumerates the
zone mesh as an unidentified `octree` node. Diagnostic only; nothing renders
differently. This unit did not fix it because fixing it is a behaviour
question about what the count should read in a BSP zone (where the node is
null), not a deletion.

### 3. B450 — PopupType shrank to {None, Inventory, Vendor}

The board offered two readings. **T434 and T436 had already taken the second
one in practice**, so the enum followed the code: `Spellbook`, `Bank`, `Loot`,
`Trade` and `Skills` are deleted, and each of those windows keeps drawing and
keeps tracking its own visibility on its own panel state
(`BankPanelState::isOpen`, `LootPanelState`, `SkillsPopupState`, …), exactly
as it already did. Nothing that worked stopped working — the census
round-trips the two enumerators fable drives.

Struck with it: the historical comment in `tests/test_loot_panel.cpp` ("a bare
enumerator with no render function" — it stayed bare after T290 gave the
window one), and the stale `enum PopupType { None, Inventory, Spellbook,
Vendor, ... }` sketch in `docs/future/ui_fixes.md`.
`B434Census.PopupTypeHasNoOptionsEnumerator` learned the new set in place: it
asserted `Skills` was present and last, and now asserts `Vendor` is present
and unique.

### 4. Docs

No orientation doc (root `CLAUDE.md`, any `*/CLAUDE.md`, `README.md`,
`docs/*.md`) named any of the three; a grep over `docs/`, `README.md`,
`src/client/graphics/CLAUDE.md` and `include/client/graphics/` for
`fallbackMeshNode` / `checkCollisionIrrlicht` / `useBspFiltered` returns only
this unit's own tombstone. Seven rows were added to
`scripts/refactor/check_doc_claims.py`'s `RETIRED` table so a future doc
cannot reintroduce any of the names.

**`DocCitations` was RED on the base tip**, and not because of anything this
unit did: T440's B452 entry cited `zone_lifecycle/renderer_zone_lifecycle.cpp:180`,
`diagnostics/diagnostics_debug.cpp:282` and three more as paths relative to
`src/client/graphics/`, which the checker rejects. Five failures. Every one of
those citations is now repo-relative (`src/client/graphics/…`), and the new
B453 entry uses the full form too. The orchestrator is fixing the same lines
on `fable`, so **expect a BOARD.md conflict on the B452 entry at merge**.

### Tests, pre-fix red and post-fix green

`tests/test_dead_renderer_members.cpp` (new, 260 lines), registered in
`tests/cmake/ui_rendering.cmake`. Nine cases in three suites. Every case is a
SOURCE CENSUS read through `WILLEQ_REPO_ROOT`, which is the pre-fix red for a
unit whose whole fix is a deletion: the same binary measures the pre-fix and
post-fix sources, so the recipe is build → run (red) → delete → rebuild → run
(green). No stash, no `git checkout --`, nothing reverted in the tree. It
excludes itself from its own tree walk (it names the deleted symbols in string
literals) and reads comment-stripped text, so a tombstone naming a deleted
symbol is not a false positive.

| run | result |
|---|---|
| `test_dead_renderer_members`, pre-fix (deletions not yet made) | **5 FAILED, 4 passed** — `B450Census.PopupTypeDeclaresOnlyTheThreeDrivenEnumerators`, `B451Census.CheckCollisionIrrlichtIsGoneFromTheWholeTree`, `B451Census.ThePhysicsTuCallsTheBspFilteredCheckUnconditionally`, `B452Census.FallbackMeshNodeIsGoneFromTheWholeTree`, `B452Census.DrawRegionMeshNoLongerCarriesTheFallbackSentinel` |
| `test_dead_renderer_members`, as committed | **9 / 9 passed** |
| `test_movement_order` (G06) | **4 / 4** after the golden learned the edit |
| `test_options_merchant_residue` (B434Census) | **13 / 13** after it learned the new enum set |

The four that pass in BOTH runs are the ones that must:
`B450Census.NothingInTheTreeNamesADeletedPopupEnumerator` (the enumerators
were named by nothing before the deletion — that is the whole finding),
`B450Census.TheTwoDrivenPopupsStillRoundTrip`,
`B451Census.ResolveIrrlichtCollisionIsEnteredOnlyWithRegionTriangles` (the
invariant is pre-existing and is pinned so a future edit cannot quietly
restore the branch) and `B452Census.CreateZoneMeshStillCreatesTheRealFallbackNode`.

### G06 — the movement golden learned the edit IN PLACE

`tests/goldens/movement_order_g06.txt`, **463 → 456**. Five two- and
three-line ternaries each collapsed to the one statement that replaced them,
at the same position, plus the deletion of the now-dead
`bool useBspFiltered = ...;` declaration. **Zero insertions, zero reorders, no
other line touched** — verified by stripping the line-number prefix from both
revisions and diffing: the content diff is exactly six hunks, all removals and
five one-line in-place collapses. The learning was scripted
group-substitution over the golden's own text, never regeneration from the
tip, and `kGoldenSize`'s comment names the class of edit and why.

G05 (`frame_render_order_g05.txt`) pins `renderer_frame_render.cpp`, which
this branch does not touch: **unchanged, 0 lines moved.** Its one `PopupType`
line reads `PopupType::Inventory`, a surviving enumerator.

### Verification

| step | result |
|---|---|
| `cmake --build build -j24` (RelWithDebInfo, GLES2 config, background + exit file) | **exit 0** ×2 (pre-fix and post-fix). No exit 143 anywhere in this unit, so no truncation scan was owed |
| `ctest --test-dir build -j6` (final) | **4593 / 4593, 0 failed** |
| `ctest -j6` (first post-fix run, before the board/doc edits) | 4591 / 4593 — the two reds were `DocCitations` (**red on the base tip**, see §4) and `DocClaims` (this unit's new RETIRED rows against the not-yet-struck B451/B452 entries). Both green once the board was struck |
| `test_dead_renderer_members` | **5 red pre-fix → 9 green post-fix** |
| `test_movement_order` | **4 / 4** |
| `DocClaims` / `DocCitations` / `DocCounts` / `FileSizeCeiling` | **4 / 4 passed** |
| `scripts/build-gles2-x86.sh` | **exit 0** |
| `scripts/ci/visual_gate.sh` (standalone) | **PASS** — qeynos2 5.274/10.108, gfaydark 8.897/10.672, befallen 7.974/10.948 |
| image goldens | **none moved** — `git status tests/goldens/` shows only `movement_order_g06.txt` |
| `scripts/build-arm-noble.sh` | **exit 0** |
| `check_file_sizes.sh fable` | **15 ok, 0 warn, 0 fail**; `irrlicht_renderer.h` 1,429, `renderer_player_physics.cpp` 997 (it SHRANK by 4), `renderer_player_collision.cpp` 675 (SHRANK by 6), `renderer_zone_draw.cpp` 1,143, `renderer_zone_load.cpp` 1,003, `diagnostics_component.cpp` 1,159, `diagnostics_debug.cpp` 582, `static_panels.h` 687. New file `test_dead_renderer_members.cpp` 260 |
| eqemu | never contacted — T438 holds the lock, this unit is offline |

### Board

| entry | action |
|---|---|
| **B450** | STRUCK — `PopupType` is `{None, Inventory, Vendor}`; the five that named nothing are deleted, and the second reading the entry offered is the one T434/T436 had already taken |
| **B451** | STRUCK — the stub, the five ternaries and the dead `useBspFiltered` selector (parameters included) are gone; G06 learned it in place |
| **B452** | STRUCK — closed by DELETION, not restoration: there is no creator to bring back, and `createZoneMesh()` is the real fallback. Eleven sites plus the `SIZE_MAX` sentinel branch |
| **B453** | NEW (B) — `/dumpscene`'s scene breakdown never subtracts `ctx_.zoneMeshNode`, so a no-BSP zone (Plane of Sky) reports the one real zone mesh as `unaccounted`. Found while removing the dead sibling that WAS subtracted there |

**B453 is the number this unit used.** T441 in slot1 also allocates from B453 —
renumber at merge if it collided.

### Deviations

1. **The `useBspFiltered` selector and its two function parameters were
   deleted**, which the plan did not name. With the five ternaries gone it was
   a dead local and two dead parameters; leaving them would be the defect
   class this unit removes. It costs two extra in-place edits in the G06
   golden's neighbourhood — the phase-call statements it appears in are G06
   split plumbing and are not in the golden at all, so the golden edit stayed
   pure.
2. **`drawRegionMesh()`'s `regionIdx == SIZE_MAX` branch was deleted too.**
   The sentinel resolved only to the fallback node and had exactly one caller,
   the fallback draw. Keeping it would have left a branch that silently
   returns.
3. **The scene-breakdown site is a real gap and was boarded (B453), not
   fixed.** It is the one deleted site `zoneMeshNode` does not cover, and
   covering it is a behaviour question, not a deletion.
4. **`DocCitations` was already red on the base tip** (five failures, all from
   T440's B452 entry's short relative paths). Fixed here as part of striking
   that entry, which means a BOARD.md merge conflict with the orchestrator's
   parallel fix on `fable`.
5. **No live or visual evidence beyond the gate.** Nothing this unit deleted
   was reachable, so there is no user-visible before/after to capture; the
   gate's three zones are the proof that the deletions did not disturb the
   paths that do run. `review/T442/` holds no media for that reason.
