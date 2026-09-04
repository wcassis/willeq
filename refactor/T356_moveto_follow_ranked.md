# T356 — moveTo/follow name resolution ranked by distance (B003c) + stable camera test names (B010)

Class B, slot3. Two disjoint items; one branch, two commits.

## Leg A — B003c

T350 found `/face` used the unranked `findEntityByName(name)` and turned the
player toward the wrong same-named entity. `ActionDispatcher::moveToEntity`
and `followEntity` (`src/client/action/action_dispatcher.cpp:165,178`) use the
same unranked lookup as an existence check, then hand the NAME to
`m_handler->moveToEntity(name)` / `followEntity(name)`. The question is how
the handler resolves the name.

1. Read the handler's resolution (`src/client/action/eq_action_handler*.cpp`
   and whatever it calls). Record it in the Review: ranked (by distance from
   the player, T350 shape) or not.
2. If not ranked: resolve ONCE in the dispatcher with the ranked overload
   (`findEntityByName(name, player.x(), player.y(), player.z())`, as
   `faceEntity` at line 110 does) and pass the resolved entity's id / the
   entity itself to the handler, so the handler cannot re-resolve
   differently. Change the handler signature if that is what it takes; the
   compiler finds the callers (bridge, commands, tests).
3. Test, pre-fix failing: two entities with the same name, the nearer one
   spawned SECOND (so an unranked first-match picks the far one); moveTo and
   follow must pick the nearer. Mirror T350's face test.
4. Strike B003c on `refactor/BOARD.md` with the commit hash.

## Leg B — B010

`tests/test_irrlicht_camera_command.cpp` `CameraCommandRoundTripTest`: gtest
names each parameterised case by hex-dumping a pointer. Give the cases stable
names — a name-generator lambda as the 4th `INSTANTIATE_TEST_SUITE_P` argument
returning an identifier built from the case's own fields (letters, digits,
underscore only), or a `PrintTo`. Show the eight names in `ctest -N` output
in the Review; run the test binary twice and show the names are identical.
Strike B010.

## Proof

Build, `ctest -j6`, the two new/changed tests named in the Review, file size
check. No graphics files change (unless the handler signature change touches
the bridge — then run the visual gate standalone too).

STOP: the handler already ranks by distance (then Leg A is a Review note +
an existence check swapped to the ranked overload for consistency, no
signature change) — that is not a STOP, do it. STOP only if resolving in the
dispatcher would change what a bridge caller observes in a way you cannot
test.
