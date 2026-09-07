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
