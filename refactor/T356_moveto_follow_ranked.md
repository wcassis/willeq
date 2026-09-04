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

## Review

Branch `task/T356-moveto-follow-ranked`, base `9b9840e0`.
Leg A `287ecbb7`, Leg B `fe812e8d`; the board strike + this Review are
the commit that adds this section.

### Leg A — what the handler's resolution turned out to be

**Not ranked, and by a third resolver.** The chain was:

| layer | function | resolver |
|---|---|---|
| `ActionDispatcher::moveToEntity/followEntity` | existence check only | `EntityManager::findEntityByName(name)` — ranked body, but called with **no position**, so tier-then-lowest-spawn-id |
| `EqActionHandler::moveToEntity/followEntity` | forwards the NAME | — |
| `MovementComponent::MoveToEntity/Follow` | the resolution that actually moved the player | `EntitiesComponent::FindEntityByName` (`src/client/entities/entities_component_appearance.cpp:326`) |

`EntitiesComponent::FindEntityByName` is a **different function from
`EntityManager::findEntityByName`** with a different rule: case-insensitive
**prefix** match (`find(...) == 0`, not substring), no corpse tiering, no
distance term, first hit over a `std::map<uint16_t, Entity>` — i.e. the
**lowest spawn id** among matches wins. So B003c was a live defect, and a
worse one than /face's: three resolvers on one command path, two of them
unranked, and the one the dispatcher validated against was not the one that
picked the destination.

Per the plan this is the "if not ranked" branch: the dispatcher now resolves
**once** with `findEntityByName(name, player.x(), player.y(), player.z())`
(the T244 nearest-first overload, exactly as `faceEntity` does since T350)
and passes the winner's **spawn id** down. `IActionHandler::moveToEntity` /
`followEntity`, `EqActionHandler`'s overrides and
`MovementComponent::MoveToEntity` / `Follow` all take `uint16_t` now — the
T230 rule ("the handler only ever receives a resolved spawn id") extended
from targeting to movement. The compiler found every caller; the only
production callers of the dispatcher methods are
`CommandProcessor::cmdMoveTo` / `cmdFollow` and `InputActionBridge`, both of
which keep their name-taking API unchanged, so **no bridge caller observes a
difference** other than the fixed pick. No STOP condition was hit.

Known residual, out of scope and unchanged by this unit: `Follow` stores the
follow target as a **name** (`PlayerState::m_followTarget`) and
`movement_update.cpp:183` re-matches it by exact string each tick, so
per-tick follow tracking of two identically-named mobs is still ambiguous.
This unit fixes which entity the follow *starts* on.

### Leg A — test, pre-fix and post-fix

`ActionDispatcherTest.MoveToAndFollow_PickTheNearestMatch_NotTheLowestSpawnId`
(`tests/test_action_dispatcher.cpp`), mirroring T350's face test: two
`a_fire_beetle*`s, the FAR one at spawn 68 (100 units east), the NEAR one at
spawn 277 (10 units north) spawned second with the higher id.

Pre-fix run — the dispatcher reverted to `findEntityByName(name)` (the exact
one-line defect), everything else as committed, so the failure isolates the
ranked lookup:

```
[ RUN      ] ActionDispatcherTest.MoveToAndFollow_PickTheNearestMatch_NotTheLowestSpawnId
tests/test_action_dispatcher.cpp:176: Failure
  handler.calls[0]  Which is: "moveToEntity(68)"   vs  "moveToEntity(277)"
tests/test_action_dispatcher.cpp:177: Failure
  handler.calls[1]  Which is: "followEntity(68)"   vs  "followEntity(277)"
[  FAILED  ] ActionDispatcherTest.MoveToAndFollow_PickTheNearestMatch_NotTheLowestSpawnId
1 FAILED TEST
```

Post-fix:

```
[  OK ] ActionDispatcherTest.MoveToAndFollow_PickTheNearestMatch_NotTheLowestSpawnId (0 ms)
[  OK ] ActionDispatcherTest.MoveToFamily_RequiresEntityAndRoutesCorrectly (0 ms)
[  PASSED  ] 2 tests.
```

`test_command_processor` `*MoveTo*:*Follow*` — 10/10 pass; the two
expectations there now read `moveToEntity(42)` / `followEntity(43)`, which is
the visible proof that the handler receives an id and never a name.

### Leg B — the eight names

Pre-fix, two `--gtest_list_tests` runs of the same binary differed on **all
eight** case lines (`diff` exit 1), e.g. `SetThenReadRoundTrips/0  #
GetParam() = 72-byte object <40-58 BA-99 62-5A ...>` vs `<40-78 ED-9C 06-61
...>`; `ctest -N` registered those hex dumps verbatim as the test names.

Post-fix `ctest -N -R CameraCommandRoundTrip`:

```
  Test #2973: ExactGetterStrings/CameraCommandRoundTripTest.SetThenReadRoundTrips/Free
  Test #2974: ExactGetterStrings/CameraCommandRoundTripTest.SetThenReadRoundTrips/Follow
  Test #2975: ExactGetterStrings/CameraCommandRoundTripTest.SetThenReadRoundTrips/First_Person
  Test #2976: CompactAliases/CameraCommandRoundTripTest.SetThenReadRoundTrips/free
  Test #2977: CompactAliases/CameraCommandRoundTripTest.SetThenReadRoundTrips/FOLLOW
  Test #2978: CompactAliases/CameraCommandRoundTripTest.SetThenReadRoundTrips/firstperson
  Test #2979: CompactAliases/CameraCommandRoundTripTest.SetThenReadRoundTrips/fp
  Test #2980: CompactAliases/CameraCommandRoundTripTest.SetThenReadRoundTrips/first_person
Total Tests: 8
```

Two runs of the built binary are now **byte-identical** (`diff` exit 0).

Deviation from the plan, in the safe direction: the plan offered "a name
generator ... **or** a `PrintTo`". A name generator alone left the trailing
`# GetParam() = 72-byte object <...>` comment still hex-dumping and still
varying run to run — the ctest *names* were stable but the listing was not,
so a listing diff still churned. Both were added; the whole listing is now
deterministic.

### Verification

| step | result |
|---|---|
| `cmake --build build -j24` (GLES2 config, tests on) | EXIT=0 |
| `ctest -j6 --output-on-failure` | **3852/3854 passed**. Failures: `AudioManagerTest.PreloadCommonSounds` (the standing ALSA host quirk) and `visual_gate` |
| `visual_gate` standalone | **PASS** (twice — before and after the rebuilt `build-gles2`) |
| `bash scripts/build-gles2-x86.sh` | EXIT=0 (see note below) |
| goldens | untouched; no golden or fixture file in the diff |
| `bash scripts/build-arm-noble.sh` | EXIT=0, `build-arm-noble/bin/willeq` = ELF 32-bit ARM EABI5 |
| `bash scripts/refactor/check_file_sizes.sh fable` | 11 files checked, **11 ok, 0 warn, 0 fail** (largest touched: `tests/test_action_dispatcher.cpp` 1158) |

`visual_gate`'s failure inside `ctest -j6` was a **load artifact, not a
regression**: the only gated line was `befallen: p95 process-CPU ms 19.739
exceeds ceiling 10.948`, i.e. the CPU-time budget measured while 3853 other
tests shared 24 cores. Run standalone it passes at 28–30 s with every zone
inside budget. No graphics file is in this branch's diff.

The first `scripts/build-gles2-x86.sh` invocation hit the tool's 600 s
ceiling (exit 143) and `gmake` **deleted** `build-gles2/lib/libwilleq_core.a`
mid-`ar` — the protocol's "absence, not corruption" shape. The post-143 sweep
found it (zero-byte scan: only ExternalProject stamps; `ar t` per archive;
`build-gles2/bin` inspected), the build was relaunched to completion
(EXIT=0), and the archive came back with 317 members. The visual gate was
then re-run against the freshly linked `willeq` (22:41) rather than the stale
one the in-suite run had used.
