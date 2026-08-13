# T98 — Delete the 34 shadowed `CommandProcessor` commands

**Slot: whichever is free. Headless. Live spot-check at the end if the lock is
free — otherwise flag it outstanding.**

**Territory: `src/client/action/command_processor.cpp` and its header,
`tests/`, `refactor/`, `/mnt/projects/fable/review/T98/`. See "The cascade" —
`ActionDispatcher` is deliberately NOT in territory.**

Read T97's Review and `/mnt/projects/fable/review/T97/reachability_table.tsv`
first. **Will has ruled: delete the shadowed code.**

## The list is data, not prose — use the table

`/mnt/projects/fable/review/T97/reachability_table.tsv` is authoritative:
column 3 is `Shadowed` (34), `Reachable-via-alias` (1), or `Reachable` (30).

Delete **only** the 34 marked `Shadowed`:

```
say ooc auction tell reply gsay gu emote loc sit stand camp move moveto
follow stopfollow target cast interrupt invite disband decline who help
quit q debug timestamp skills gems mem forget pet filter
```

**`shout` must NOT be deleted.** It is `Reachable-via-alias` — its primary
name is shadowed but `/sh` reaches it, because the `game` registry's shout
alias is spelled `sho`. Deleting it would remove live behaviour.

Re-derive the list from the TSV rather than copying it from here. If your
derivation disagrees with the block above, **stop and report** — one of us is
wrong and finding out which matters more than proceeding.

## What "delete" means

For each of the 34: remove the `registerCommand(...)` registration **and** its
`cmdX()` handler, plus any helper that becomes unreferenced as a result.

**Verify before removing each helper** that no surviving command uses it.
`CommandProcessor` has 31 commands left (30 reachable + `shout`) and they may
share parsing or formatting helpers with the deleted ones. A helper still used
by a survivor stays.

## The cascade — report it, do not follow it

Deleting `cmdCast`, `cmdPet`, `cmdMem` and friends will leave methods on
`ActionDispatcher` and `EqActionHandler` with no remaining production caller —
including the `/pet` wrappers T96 just fixed and the gem-slot conversions T93
tested.

**Do not delete those.** `ActionDispatcher` is a deliberate action-layer API and
may be intended for a future scripting surface; that is Will's call, not a side
effect of this unit. **Produce a list of what becomes newly unreferenced** and
report it. That list is a deliverable.

## Tests

Many tests exercise the deleted commands. Handle them deliberately:

- A test that **only** covers a deleted command goes with it.
- A test that covers **shared logic** through a deleted command must be
  re-pointed at a surviving command, not deleted — otherwise this unit silently
  reduces coverage of live code.
- **State the count**: tests deleted, tests re-pointed, and the suite total
  before and after. A large drop that is not accounted for is a red flag.

Note T93's and T95's tests target `EqActionHandler` and `ActionDispatcher`
directly, not through `CommandProcessor`, so they should survive untouched —
confirm rather than assume.

## Do not bury the group-invite gap

T97 found that accepting a pending group invite via bare `/follow` is dead in
both registries: `game`'s Social `follow_cmd` (which carries that behaviour) is
overwritten by Movement's `follow`, and `action`'s fallback is shadowed.
Deleting `action`'s copy removes one of the two implementations.

**Before deleting it, record in `refactor/deferred.md` what the deleted code
did**, so the feature can be restored from the surviving `game`-side Social
registration rather than rediscovered. Also note whether
`eq_bridge.cpp`'s `GroupAcceptIntent` handler has a live producer — if it does,
a UI path still works and the gap is narrower than it looks.

## Verification — the bar is "nothing that worked stopped working"

- **Enumerate the surviving 31 commands and confirm each still dispatches**, by
  name and by every alias. This is the check that matters; a deletion that
  clips a survivor is the failure mode.
- Full build and suite. **Account for the test-count delta explicitly** against
  the current 2102/2103 baseline; the sole pre-existing failure is the
  documented ALSA `AudioManagerTest.PreloadCommonSounds`.
- Visual gate — goldens must not move.
- If the eqemu lock is free, spot-check a handful of survivors live and confirm
  they still log `registry=` as before. If it is held, say the live leg is
  outstanding.

## What you may not do

- **Do not delete `shout`.**
- Do not reorder the registries or touch the `game` registry at all.
- Do not delete `ActionDispatcher`/`EqActionHandler` methods — report them.
- Do not delete a helper without proving no survivor uses it.
- **STOP and report** if deletion would require changing `Application`'s
  dispatch logic. That is a different unit.

## Review

Executed in `/mnt/projects/fable/wt/slot1`, task branch
`task/T98-delete-shadowed`, at `fable` tip `fc934b93`.

### The list, re-derived

Filtered `review/T97/reachability_table.tsv` for column 3 == `Shadowed`:
`say ooc auction tell reply gsay gu emote loc sit stand camp move moveto
follow stopfollow target cast interrupt invite disband decline who help
quit q debug timestamp skills gems mem forget pet filter` — exactly 34,
**matching the plan's list exactly**. `shout` (`Reachable-via-alias`)
excluded, as instructed. No disagreement to report.

### What was deleted per command

For each of the 34: its `registerCommand(...)` block in
`registerBuiltinCommands()` and its `cmdX()` handler, in both
`command_processor.h` and `command_processor.cpp`. Left in place: `shout`
and the 30 `Reachable` survivors (`face`, `consider`, `hail`, `npcstate`,
`door`, `afk`, `anon`, `walk`, `run`, `sneak`, `crouch`, `feign`, `jump`,
`turn`, `list`, `dump`, `hunt`, `autoloot`, `listtargets`, `loot`,
`pathfinding`, `roleplay`, `wave`, `dance`, `cheer`, `laugh`, `logonly`,
`logexclude`, `logmodule`, `logclear`) — 31 total, confirmed by
`grep -c` on the surviving `registerCommand` calls and by the new
`T98_AllSurvivorsRegisteredAndDispatchByNameAndAlias` test (below). This
empties three categories entirely within `CommandProcessor` (`Group` —
invite/disband/decline were its only members; `Spells` — skills/gems/
mem/forget; `Pet` — pet alone); `game`'s registrations in those categories
are unaffected and still the only reachable ones. One caught-on-build
mistake: my first pass left the `cast`/`interrupt` `registerCommand(...)`
blocks in place after deleting their handler declarations, which failed to
compile (`'cmdCast' was not declared in this scope`) — fixed before any
test ran; full clean build follows below.

### Helper removed: `displayHelp()`

`CommandProcessor::displayHelp()` (private) had exactly one caller,
`cmdHelp` — verified by grepping the whole tree for `displayHelp` before
removal (two hits: its own definition and the `cmdHelp` call site, nothing
in `tests/` since it's private). Removed along with `cmdHelp`. The public
API it called internally (`getCommandInfo()`/`getCategories()`/
`getCommandsByCategory()`) is used elsewhere (e.g.
`Application::dispatchToActionOrRenderer()` calls `getCommandInfo()`
directly) and is untouched. No other `CommandProcessor` helper
(`toLower`, `parseCommandLine`, `splitArgs`, `displayMessage`,
`displayError`) became unreferenced — each still has at least one surviving
caller (`splitArgs`, the least obviously safe one, is still used by the
surviving `cmdFace`).

### The cascade — reported, not followed

Grepped the whole tree (excluding `command_processor.cpp` and `tests/`) for
every `ActionDispatcher`/`EqActionHandler` method the 34 deleted commands
called, to see which lost their last production caller. Full detail and the
two-call live-caller exceptions (`sendTell`/`moveToLocation`/`moveToEntity`/
`castSpell`/`sendChatMessage`/`sendAnimation` all still called from
`input_action_bridge.cpp`/`application.cpp`) are in `refactor/deferred.md`'s
T97 entry addendum. Newly production-caller-free (test-only now): `sit()`,
`stand()`, `startCamp()`, `replyToLastTell()`, `followEntity()`,
`acceptGroupInvite()`, `stopFollow()`, `targetNearest()` (its
`targetNearestPC`/`NPC` siblings stay live), `targetEntityByName()`,
`interruptCast()` at the dispatcher/handler layer (`SpellManager::
interruptCast()` itself stays live, called directly elsewhere),
`inviteTarget()`, `inviteToGroup()`, `leaveGroup()`, `declineGroupInvite()`,
`forgetSpell()` at the dispatcher/handler layer (`SpellManager::
forgetSpell()` stays live, called directly elsewhere), and all ten T96-fixed
pet wrappers (`petAttack`/`petBackOff`/`petFollow`/`petGuard`/`petSit`/
`petTaunt`/`petHold`/`petFocus`/`petHealth`/`dismissPet`). **Not deleted**,
per the plan — recorded as an open question for Will in
`refactor/deferred.md`.

### Tests: deleted vs re-pointed, with counts

`tests/test_command_processor.cpp` (53 tests before, 50 after this pass +
1 new permanent test = 51 net) and `tests/test_input_action_bridge.cpp` (1
test re-pointed, count unchanged):

- **Deleted (3, covered only a deleted command, no shared logic left
  uncovered)**: `TrailingArgumentCommand_TellMissingMessageFails`,
  `TrailingArgumentCommand_QuotesAreNotSpecial` (both pinned `cmdTell`'s own
  target+message-join/quote-handling, which no survivor replicates —
  `splitArgs` itself, the shared primitive, is re-pointed separately, see
  below), and `Timestamp_TogglesRendererSetting` (pinned `cmdTimestamp`'s
  own `IOutputRenderer::setShowTimestamps()` toggle; no survivor touches
  timestamp state).
- **Re-pointed (11, preserved shared-logic coverage on a surviving
  command)**: `ProcessCommand_AcceptsWithOrWithoutLeadingSlash` (/sit ->
  /consider), `ProcessInput_SlashPrefixDispatchesAsCommand` (/stand ->
  /jump), `TrailingArgumentCommand_TellSplitsTargetFromMessage` ->
  renamed `..._FaceUsesSplitArgsForThreeCoordinates` (/tell -> /face, since
  `splitArgs` — the shared primitive under test — is still called by
  `cmdFace`; pinned only that `faceLocation`'s `setHeading(...)` fires, not
  its numeric argument, to avoid a brittle floating-point match on player
  position), `CommandLookup_IsCaseInsensitive` (/SIT -> /CONSIDER),
  `CommandLookup_MixedCaseAlias` (/Tar -> /Sh, which doubles as a live check
  of `shout`'s escaping alias), `HasCommand_IsCaseInsensitive` (SIT ->
  FACE), `ArgumentCase_PreservedThroughDispatch` (/target -> /dump),
  `ArgumentParsing_NoArgs` (/sit -> /consider),
  `ArgumentParsing_LeadingWhitespaceTrimmed`/`_TrailingWhitespaceNotTrimmed`/
  `_MultipleInternalSpacesPreservedInMessage` (/say -> /shout, the only
  survivor still reaching `sendChatMessage()` by name),
  `EchoEnabled_DisplaysTypedCommand` (/sit -> /consider),
  `BuiltinCommands_RepresentativeSampleRegisteredWithExpectedCategory`
  (rewrote all 7 samples — every original sample, say/sit/target/invite/
  who/mem/pet, was itself one of the 34 deleted — to shout/face/consider/
  door, one per category that still has members), `GetCommandsByCategory_
  OnlyReturnsMatchingCategory` (target -> consider),
  `GetCompletions_ExactMatchIncluded` (sit -> face),
  `GetAllCommandNames_IncludesAliasesAndIsSorted` (s/say -> sh/shout), and
  `InputActionBridgeTest.ConsoleCommands_ForwardToCommandProcessorInOrder`
  (/sit,/stand -> /consider,/afk — /door was tried first but depends on
  world-state the test fixture doesn't set up, caught by the first test run
  and fixed).
- **Unaffected**: everything else (Hail/NpcState/Loot/registration-
  round-trip/T51-attack tests, `GetCompletions_PrefixMatch` — already used
  shout/"sh" — etc.). Confirmed T93's/T95's tests target `EqActionHandler`/
  `ActionDispatcher` directly, not through `CommandProcessor`
  (`grep -rL command_processor tests/test_eq_action_handler_transforms.cpp
  tests/test_eq_action_handler_enum_guards.cpp` — neither includes it) —
  survived untouched, confirmed rather than assumed.
- **1 new permanent test added**:
  `CommandProcessorTest.T98_AllSurvivorsRegisteredAndDispatchByNameAndAlias`
  — the plan's "enumerate the surviving 31 and confirm each still
  dispatches, by name and by every alias" verification, kept as a
  regression test rather than a throwaway check.

**Suite total**: 2103 baseline (2102 passing + 1 documented ALSA failure)
-> 2101 total (2100 passing + the same 1 ALSA failure). Delta = -2,
fully accounted for: -3 deleted +1 added = -2 (the one re-pointed test is
a modification, not a net add/remove).

### Surviving-31 dispatch confirmation

`CommandProcessorTest.T98_AllSurvivorsRegisteredAndDispatchByNameAndAlias`
(new, `tests/test_command_processor.cpp`) asserts, for all 31 survivors and
every one of their aliases (`sh`/`con`/`u`/`duck`/`fd`/`rp`):
`hasCommand()` true, `getCommandInfo()` resolves to the right primary name,
and `processCommand()`'s result is never "Unknown command" (some
legitimately fail for other reasons with no live GameState, e.g. `/turn`
with no args — that's fine, only "Unknown command" would mean a survivor
was actually clipped). Passed. Backed live by the eqemu spot-check below.

### The group-invite gap

Recorded in `refactor/deferred.md`'s T97 entry addendum before deletion, per
the plan: the deleted `cmdFollow` code, what it called, and where the
surviving (but still shadowed, within `game` itself) copy of the same
behavior lives (`game`'s Social `follow_cmd`, `eq_commands.cpp:607-638`).
**`eq_bridge.cpp`'s `GroupAcceptIntent` handler
(`eq_bridge.cpp:1289-1291`) has NO live producer** — grepped the whole tree
for any construction site of `GroupAcceptIntent{}` outside its type
definition and its handler branch: zero. So the gap is not narrower than
T97 found; there is no UI path that still works.

### Verification

1. `cmake --build build -j24` — clean (after fixing the cast/interrupt
   leftover registration caught by the first build attempt).
2. `ctest --test-dir build` — **2100/2101 passed**, sole failure the
   documented ALSA `AudioManagerTest.PreloadCommonSounds`. Delta from
   baseline accounted for above.
3. `bash scripts/build-gles2-x86.sh` — folded into the ctest run above
   (same tree); `visual_gate` (test #2101) — PASS, 6.31s, goldens
   untouched (`git status` clean of any golden files throughout).
4. `bash scripts/build-arm-noble.sh` — clean cross-compile.
5. `scripts/refactor/check_file_sizes.sh fable` — 4/4 files ok (the two
   `command_processor.*` files shrank; the two test files grew modestly).
6. Live eqemu spot-check (lock free throughout, taken/released cleanly):
   `up.sh` -> `--script` tour of `/consider`, `/afk` x2, `/door`,
   `/face 0 0 0`, `/wave`, `/logclear`, `/camp` -> `down.sh`. All 6
   survivors show `registry=action recognized=true` (the same as before
   this unit, since none of them were ever shadowed — this proves the
   deletion didn't clip a survivor, not that any of them newly became
   reachable); `/door` fails cleanly ("No door nearby", zone has none) but
   still resolves through `registry=action`, not "Unknown command"; final
   `/camp` correctly shows `registry=game` (deleted from `CommandProcessor`
   by this unit; `game`'s own untouched `/camp` is what ran). Full log and
   README: `/mnt/projects/fable/review/T98/`.

### Deviations

1. First build attempt failed: I removed `cmdCast`/`cmdInterrupt`'s header
   declarations but initially left their `registerCommand(...)` blocks in
   `.cpp` intact, causing a compile error. Caught immediately by the first
   `cmake --build`, fixed before any test ran — not present in the final
   diff.
2. First `ConsoleCommands_ForwardToCommandProcessorInOrder` re-point picked
   `/door` as the second command; it depends on a nearby door existing in
   `GameState` (the test fixture has none), so `ActionDispatcher::
   clickNearestDoor()` fails before reaching the handler and the test's
   `handler.count()` assertion failed. Caught by the first `ctest` run,
   swapped for `/afk` (unconditional), re-verified passing.
3. No other deviations. Registries not reordered, `game` registry
   untouched, `shout` not deleted, no `ActionDispatcher`/`EqActionHandler`
   method deleted, no helper removed without proving no survivor used it,
   no change to `Application`'s dispatch logic.

## Board

T97's entry in `refactor/deferred.md` closed with the disposition taken
(delete, executed) — see the addendum appended to that entry rather than a
new top-level entry, to keep the full T97->T98 arc in one place. The
newly-unreferenced `ActionDispatcher`/`EqActionHandler` methods (the pet
wrappers plus 13 others) are recorded there as an open question for Will:
delete this now-fully-dead action-layer surface, or keep it as the
deliberate future-scripting-API the T97/T98 plans named it as? Not decided
by this unit.
