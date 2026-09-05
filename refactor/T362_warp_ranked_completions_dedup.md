# T362 — /warp resolves ranked and the prefix resolver dies (B070); getCompletions de-dup shared (B069)

Class B, slot3. Two disjoint items; one branch, two commits. Read the
B070 and B069 board text.

## Leg A — B070

`CommandsComponent::RequestWarp` (`src/client/commands/commands_movement.cpp`)
is the LAST caller of `EntitiesComponent::FindEntityByName`, the unranked
case-insensitive prefix resolver (lowest spawn id wins). Resolve `/warp
<name>` through the ranked lookup (`EntityManager::findEntityByName(name,
px, py, pz)`) at the dispatcher if `/warp` passes through
`ActionDispatcher` (T356 shape), else at `RequestWarp` itself; then delete
`FindEntityByName` (K12 ff check, comment-filtered grep, the compiler).
Test, pre-fix failing: two same-named mobs, nearer spawned second, `/warp`
picks the nearer. Strike B070.

## Leg B — B069

`CommandProcessor::getCompletions` and `CommandRegistry::getCompletions` are
duplicated-then-drifted: the same prefix idiom over names then aliases,
the same private `toLower`, both sort; only `CommandProcessor` de-duplicates.
Extract ONE shared helper (a free function in a small header under
`include/client/action/` or wherever both can include without a new
dependency edge — say which and why) that both call; the helper sorts and
de-duplicates. Test, pre-fix failing: a `CommandRegistry` command
registered with its own name as an alias completes ONCE. Both existing
completion tests and T357's `ChatTabCompletionTest` stay green. Strike
B069.

## Proof

Build, `ctest -j6`, the new tests with pre-fix/post-fix runs, file sizes.
`command_registry.cpp` is under `src/client/graphics/ui` → visual gate
standalone + ARM (rebuild all three trees first; willeq.db v6). Check the
slot's CMakeCache points EQT_GLES2_IRRLICHT_DIR at the slot's own
third_party (protocol).

STOP: `/warp` name resolution is server-side and the client never resolves
(then Leg A is delete-only if the resolver has no other caller; say so).

## Review

Branch `task/T362-warp-ranked-completions-dedup`, base `5b863ec4`.
Leg A `4a06d2e2`, Leg B `465429b2`; the Review is the commit that adds this
section. No merge, no push. No eqemu lock taken (nothing here needed a
server).

### Leg A — where /warp resolves now

The plan's first question: does `/warp` pass through `ActionDispatcher`?
**Yes.** The full chain is

```
CommandProcessor::cmdWarp  ->  ActionDispatcher::warp(args)
                           ->  IActionHandler::warp(args)
                           ->  EqActionHandler::warp
                           ->  CommandsComponent::RequestWarp(args)
```

so the resolution moved to the dispatcher, the T356 shape. `/warp` is the one
command in this family whose argument is *polymorphic* — empty means "use the
current target", three floats mean literal coordinates, anything else is a
name — so the dispatcher now parses the arg string itself rather than pushing
it down whole:

| form | dispatcher | handler call |
|---|---|---|
| `/warp` | no resolution | `warp("")` — unchanged |
| `/warp 100 200 300` | no resolution | `warp("100 200 300")` — unchanged |
| `/warp a_fire_beetle` | `EntityManager::findEntityByName(name, px, py, pz)` | **`warpToEntity(277)`** — new |
| `/warp no_such_entity` | ranked lookup misses | **nothing** — `ActionResult::Failure` |

`IActionHandler::warpToEntity(uint16_t)` is net-new (the T230/T356 rule: ids
down, names at the dispatcher). Game side, `CommandsComponent::
RequestWarpToEntity(uint16_t)` is the entity form; `RequestWarp` keeps the
empty and coordinate forms. The teleport tail — the `SetPosLocal` store, the
entity-list update, `SendPositionUpdate()`, the hand-rolled `PlayerMoved`
push and the `Warped to (x, y, z)` ack — is byte-for-byte what it was and now
lives once, in a file-local `WarpToPosition()`, because all three destination
forms end in it.

The unresolvable-name path is a **deliberate small deviation**: it now fails
at the dispatcher with `Entity not found: <name>` (the moveTo/follow shape)
instead of reaching the game side. `RequestWarp` still prints
`Entity '<name>' not found` for a name that arrives there anyway, so
`EqActionHandlerMovementUtilityTest.Warp_UnknownEntityName_PrintsNotFound`,
which calls the handler directly, is unchanged and still green.

### Leg A — FindEntityByName is deleted

**Yes, deleted** — declaration (`include/client/entities/entities_component.h`)
and body (`src/client/entities/entities_component_appearance.cpp`), with a
tombstone comment where the body stood. Confirmations, in the order the plan
asked for them:

- **K12 `pushEvent` check: clean.** The deleted body was a pure lookup — one
  `std::transform`, one `std::replace`, a loop, `return &entity`. No bridge
  push, no state write. The nearest `pushEvent` is the `PlayerStatsChanged`
  block in `ZoneProcessHPUpdate`, thirteen lines above and untouched.
- **Comment-filtered grep:** after the delete, `FindEntityByName` appears
  nowhere in `src/`, `include/` or `tests/` except in comments, and every one
  of those is now past-tense. Three were still present-tense and were
  corrected (`movement_component.cpp`'s "still true" preamble list,
  `action_handler.h`'s ids-down rule, `command_processor_social.cpp`'s
  `cmdAssist` note).
- **The compiler:** the full `build/` tree links, and so do all 3,883 tests.

`EntityManager::findEntityByName` (the ranked one, two overloads over one
`findEntityByNameRanked` body) is now the **only** name resolver in the
client. `findEntityByExactName` remains, but that is an exact match, not a
resolution.

**Flagged behaviour narrowing.** The deleted resolver substituted `_` for
spaces in the search term, so `/warp a fire beetle` matched `a_fire_beetle`;
the ranked resolver does not. This is the same narrowing T350 and T356
accepted for `/face`, `/moveto` and `/follow`, and it is the point of the
item — `/warp` now picks exactly what `/target` picks. It is a narrowing, not
a regression of the defect, and it is recorded here rather than absorbed
silently.

### Leg A — tests, pre-fix and post-fix

Three new cases in `tests/test_action_dispatcher.cpp`, on the same fixture
`MoveToAndFollow_PickTheNearestMatch_NotTheLowestSpawnId` uses: two
`a_fire_beetle`s, the FAR one at spawn **68** (the lower id, so a first-match
resolver takes it), the NEAR one at spawn **277**, spawned second.

Pre-fix = the dispatcher body re-planted (`m_handler->warp(args)`, one `.cpp`
only; the new interface, the tests and the game side untouched) so the
failure isolates the resolution:

```
[ RUN      ] ActionDispatcherTest.Warp_ByName_PicksTheNearestMatch_NotTheLowestSpawnId
tests/test_action_dispatcher.cpp:212: Failure
Expected equality of these values:
  handler.calls[0]
    Which is: "warp(\"a_fire_beetle\")"
  "warpToEntity(277)"
[  FAILED  ] ActionDispatcherTest.Warp_ByName_PicksTheNearestMatch_NotTheLowestSpawnId

[ RUN      ] ActionDispatcherTest.Warp_UnknownName_FailsWithoutTouchingTheHandler
tests/test_action_dispatcher.cpp:235: Failure
Value of: dispatcher->warp("no_such_entity").success
  Actual: true
Expected: false
tests/test_action_dispatcher.cpp:236: Failure
Value of: handler.empty()
  Actual: false
Expected: true
[  FAILED  ] ActionDispatcherTest.Warp_UnknownName_FailsWithoutTouchingTheHandler

 2 FAILED TESTS
```

`Warp_CoordinatesAndEmptyStillGoDownAsTheRawArgString` passes both before and
after **by design** — it pins preserved behaviour, and a pre-fix failure there
would have meant the parse changed something it should not have.

Post-fix: `test_action_dispatcher --gtest_filter='*Warp*'` **3/3 pass**; the
whole binary is green in the suite run below.

Two existing test files were edited by the delete, not by the fix:

- `tests/test_entities_component.cpp` loses its two `FindEntityByName*` cases
  (the function is gone), with a section comment saying where name resolution
  is covered now.
- `tests/test_movement_component.cpp`'s T359 pre-condition
  `TheNameResolverStillPicksTheFarBeetle` called the deleted function. It is
  restated as `TheFarBeetleIsTheOneAFirstMatchResolverWouldPick` over the
  fixture it was always really about — the far beetle carries spawn 10, the
  near one spawn 20, and `GetEntities().begin()` is spawn 10 — so the two
  tracking cases below it still rest on a stated pre-condition.

### Leg B — where the shared helper lives, and why

`include/client/command_completion.h`, `namespace eqt::completion`, two free
functions: `ToLower(const std::string&)` and a `PrefixCompletions(partial,
commandsByLowerName, displayNameOf, aliasesByLowerAlias)` templated on the
command map's mapped type. Both `getCompletions()` bodies are now three lines
that call it, and both classes' private `toLower` bodies call
`eqt::completion::ToLower`.

**Why the client include root and not `include/client/action/`** (the plan's
first suggestion): the two callers sit on opposite sides of a boundary the
tree keeps clean. Registry 1 (`eqt::ui::CommandRegistry`) is UI; registry 2
(`action::CommandProcessor`) is the action layer; neither includes the
other's headers today. Putting the helper under `action/` would have made
`src/client/graphics/ui/command_registry.cpp` include an action-layer header
— a new directional edge bought for nine lines of string matching — and
putting it under `graphics/ui/` would do the same in reverse.

**Why it costs no dependency at all:** it is header-only, and it is templated
because the two maps hold *different* mapped types (`RegisteredCommand`, read
as `.info.name`, vs `ui::Command`, read as `.name`) — the caller passes a
lambda that extracts the display name. So there is no new TU, no CMake source
list change, and no library edge; `test_command_completion` links
`command_registry.cpp` and nothing else.

`include/client/help/` was the other real candidate — it is the established
neutral home for logic spanning both registries (`merged_help.h`, T143) — and
was rejected on two grounds: tab completion is not help, and `merged_help.h`
*includes both registries*, which is exactly the coupling this avoids.

The helper sorts **and** de-duplicates, so the drift is fixed by construction
rather than by a second copy of `std::unique` that could drift again.

### Leg B — tests, pre-fix and post-fix

New file `tests/test_command_completion.cpp`, new target
`test_command_completion` (7 cases: 4 on `CommandRegistry`, 3 on the helper).

Pre-fix = `CommandRegistry::getCompletions` re-planted with its old body
(sort, no `std::unique`; `.cpp` only):

```
[ RUN      ] CommandRegistryCompletionTest.ACommandRegisteredWithItsOwnNameAsAnAliasCompletesOnce
tests/test_command_completion.cpp:65: Failure
Expected equality of these values:
  CountOf(completions, "consider")
    Which is: 2
  1u
    Which is: 1
the primary name is also its own alias -- it must be offered ONCE
tests/test_command_completion.cpp:68: Failure
Expected equality of these values:
  completions.size()
    Which is: 3
  2u
[  FAILED  ] CommandRegistryCompletionTest.ACommandRegisteredWithItsOwnNameAsAnAliasCompletesOnce

 1 FAILED TEST
```

The other six pass pre-fix, as they should: they pin behaviour the old body
already had (sorting, case-insensitive partial, original-case output, empty
partial, no match) or exercise the new helper directly.

Post-fix: `test_command_completion` **7/7 pass**.

The plan's "both existing completion tests and T357's `ChatTabCompletionTest`
stay green" is confirmed by the suite run: `CommandProcessorTest.
GetCompletions*` (4 cases, `test_command_processor`) and
`ChatTabCompletionTest` (`test_frame_component`) are all green with no edit.

### Verification bar

| step | result |
|---|---|
| `cmake --build build -j24` (GLES2-config tree) | **exit 0** |
| `ctest --test-dir build -j6 --output-on-failure` | **3,882 of 3,883**; sole failure `AudioManagerTest.PreloadCommonSounds` (the known ALSA host quirk) |
| `bash scripts/build-gles2-x86.sh` | **exit 0** |
| `bash scripts/ci/visual_gate.sh` | **`=== VISUAL GATE: PASS ===`**, goldens untouched (`git status` clean of `refactor/goldens`) |
| `bash scripts/build-arm-noble.sh` | **exit 0**, `build-arm-noble/bin/willeq` = ELF 32-bit ARM EABI5 |
| `bash scripts/refactor/check_file_sizes.sh fable` | **24 checked, 24 ok, 0 warn, 0 fail** |

Largest touched files after the unit: `tests/test_action_dispatcher.cpp` 1,217,
`src/client/action/eq_action_handler_extended.cpp` 964,
`src/client/action/command_processor_social.cpp` 809,
`src/client/action/action_dispatcher_guild_misc.cpp` 733. Nothing near the
1,500 ceiling; `src/client/action/command_processor.cpp` and
`src/client/graphics/ui/command_registry.cpp` both *shrank* (273 and 186).

The slot's `build/CMakeCache.txt` was checked at checkout and points
`EQT_GLES2_IRRLICHT_DIR` at `/mnt/projects/fable/wt/slot3/third_party/
irrlicht-gles2-x86` (its own), per protocol.

**Golden tests (merge rule, T337).** The order/coverage goldens for the
touched TUs — `command_processor_registration`, `application`,
`bridge_component_intents`, `zone_dispatch`, `renderer_frame_render`,
`renderer_player_physics` — all ran inside the suite and are green. None
learned a statement: this unit adds no `pushEvent` and no registration.

### Deviations and surprises

1. **The dispatcher had to learn `/warp`'s argument grammar.** Unlike
   `/moveto` and `/follow`, whose argument is always a name, `/warp` takes
   three different forms. Resolving "at the dispatcher, as T356 did" therefore
   meant moving the *coordinate parse* up as well (as a predicate — the
   coordinate string still goes down verbatim and is re-parsed by
   `RequestWarp`, so there is exactly one place that turns text into a
   destination for that form). The alternative — a `warpToEntity` that the
   game side chose between — would have put the polymorphism back on the
   handler.
2. **`RequestWarp`'s "not found" message survives with no resolver behind
   it.** A name reaching it is now by definition unresolvable, so the message
   is still correct; the alternative was deleting a directly-called handler
   path and its test for no behavioural gain.
3. **The two build trees behaved differently under the 600 s ceiling.** The
   first `build/` compile hit exit 143 mid-link. The post-143 sweep (zero-byte
   / non-ELF binaries, `ar t` member counts, `build*/lib/st??????` temp files)
   reported **bad=0** and both archives present with members, and the resumed
   build exited 0 — so this was a clean timeout kill, not the T293/J07
   truncation trap. Every subsequent long step was run detached with a
   unit-private exit file under `scratchpad/t362/` and waited on with
   `wait_for.sh`.
4. **`ctest` failed once at gtest DISCOVERY** on
   `test_vision_buff_fade_wire` ("Process terminated due to timeout"), a test
   this unit does not touch. `--gtest_list_tests` on that binary returned its
   cases immediately and exit 0, so the binary was intact — the J05/J07
   cold-cache discovery contention, not truncation. The re-run completed
   normally (420 s).
5. **The `RecordingActionHandler` quotes its string arguments.** The first
   `Warp_CoordinatesAndEmptyStillGoDownAsTheRawArgString` expectations were
   written as `warp(100 200 300)` and failed against `warp("100 200 300")`.
   Corrected in the test, not in the fake.
