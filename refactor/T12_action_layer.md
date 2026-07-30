# T12 — Action layer tests (dispatcher + command processor)

**Slot: t1. x86 only — no Pi, no eqemu, no live server.** No sibling
unit; t2 idle.

**Territory: `tests/` ONLY.** No `src/`, no `include/`. No seam is
needed — the action layer already has a purpose-built injection
interface (see below). If you think you need a source change, STOP and
report.

Read `refactor/EXECUTOR_PROTOCOL.md` first and follow it exactly.

## Why this unit

The action layer is the thinnest-covered pure-logic area left from the
T01 survey — **0% coverage**, and it has grown since:

| file | lines |
|------|------:|
| `src/client/action/command_processor.cpp` | 1191 |
| `src/client/action/action_dispatcher.cpp` | 1027 |
| `src/client/action/input_action_bridge.cpp` | 391 |

2609 lines of `.cpp`, all reachable, none of it graphics.

## The test surface — already ideal, don't fight it

I checked all of this; treat it as given and verify rather than
rediscover.

**Dependencies are clean.** These TUs include only
`client/state/game_state.h`, `client/output/output_renderer.h`,
`common/logging.h`, and stdlib. **No Irrlicht, no `EverQuest`.**

**The injection seam already exists.** `IActionHandler`
(`include/client/action/action_dispatcher.h:67`) is a pure abstract
interface — ~50 pure virtuals, no default implementations. Production
supplies `EqActionHandler` (`include/client/eq_action_handler.h`), which
delegates to `EverQuest`. A test supplies its own fake. `ActionDispatcher`
exposes `setActionHandler()` and `isReady()`.

**Everything is constructible without the client:**

```cpp
ActionDispatcher(state::GameState& state);
CommandProcessor(state::GameState& state, ActionDispatcher& dispatcher);
InputActionBridge(state::GameState& state, ActionDispatcher& dispatcher);
```

**`ActionResult`** is `{bool success; std::string message;}` with
`Success()`/`Failure()` factories — every action returns something
checkable.

## What I already verified, so you don't spend the unit on it

`ActionDispatcher` has two guard helpers: `checkHandler()` (null handler
→ `Failure("No action handler set")`) and `checkZoneConnection()` (calls
`checkHandler()` first, then requires `m_state.world().isZoneConnected()`).

I checked **all 91** `ActionResult` methods that dereference `m_handler`:
**every one calls a guard first. Zero unguarded.**

So the no-handler sweep below is a **regression guard, not a bug hunt** —
expect it to pass. Its value is that method 92, added later without a
guard, will fail it. Don't oversell it as a finding, and don't be
disappointed when nothing breaks.

## What to test

### 1. `ActionDispatcher` routing — the core

Write a **recording fake** `IActionHandler` in a shared header
(`tests/action_test_fakes.h`) that implements every pure virtual by
appending a canonical string (e.g. `"startMoving(Forward)"`) to a
`std::vector<std::string>`. It must implement all of them or it won't
compile — that is a feature, since it forces the fake to stay in sync
with the interface.

Then, for a representative but broad set of the 91 actions:

- The action calls **the matching handler method exactly once**, with
  **the arguments passed through unchanged** (spawn ids, names, floats,
  gem slots, group indices).
- The action returns `Success`.

Cover every *family*, not every method: movement, heading/facing,
move-to/follow, targeting, auto-attack, casting, and whatever else the
header groups. Where a family has many near-identical members, test
each member's routing string — that is exactly where a copy-paste
mis-wiring hides (e.g. `targetNearestPC` calling `targetNearestNPC`).
**A mis-wired duplicate is the single most likely real bug in this file**,
so prefer breadth over depth here.

### 2. Guard behavior

- **No handler set**: `isReady()` is false, and every action returns
  `Failure` with a message, dispatches nothing to the fake, and does not
  crash. Sweep as many of the 91 as is practical — this is the
  regression guard described above.
- **Handler set but zone not connected**: the methods using
  `checkZoneConnection()` must fail with the zone message while the
  `checkHandler()`-only methods still succeed. That distinction is real
  behavior worth pinning; work out which methods are in which class by
  reading the source, and say in your Review how the split came out.
- Zone connected (`state.world().setZoneConnected(true)`) → gated
  methods succeed.

### 3. `CommandProcessor`

Public API is rich; use it:

- `processCommand` / `processInput` — including the difference between
  them (read the header comments and pin the actual distinction).
- Unknown command → `Failure`, with a sensible message.
- `registerCommand` / `unregisterCommand` / `hasCommand` /
  `getCommandInfo` round-trips, including re-registering an existing
  name and unregistering something absent.
- `getAllCommands`, `getCommandsByCategory`, `getCategories` —
  consistency between them (every command's category appears in
  `getCategories`).
- `getCompletions` — prefix matching, empty prefix, no-match, exact
  match; and `getAllCommandNames`.
- Argument handling: no args, leading/trailing whitespace, multiple
  spaces, and a trailing-argument command like `/tell`. Check the source
  for whether quoting is supported before asserting anything about it.
- Case sensitivity — establish what it actually does, then pin it.

Built-in commands are registered by `registerBuiltinCommands()`; assert
a representative sample exists with the right category rather than
enumerating all of them.

### 4. `InputActionBridge` — only if there is room

Lowest priority (391 lines). If the first three sections fill the unit,
**stop and say so** — a follow-on unit is fine. Do not rush it in.

## File layout

Multiple TUs, each **≤800 lines**, per Will's 2026-07-29 direction that
files fit a coding model's context budget:

- `tests/action_test_fakes.h` — the recording fake handler + helpers.
- `tests/test_action_dispatcher.cpp`
- `tests/test_command_processor.cpp`
- `tests/test_input_action_bridge.cpp` (if reached)

Wire them into `tests/CMakeLists.txt` in the existing style. They will
need the three action TUs plus `game_state.cpp` and its state-module
dependencies plus `door_state_manager.cpp` — follow
`test_state_modules_secondary`'s target block, which already assembles
that set. **Link no Irrlicht.** If a target pulls it in, the wiring is
wrong.

## STOP conditions

- Any change under `src/` or `include/` → STOP and report.
- A real bug found (a mis-wired action, a missing guard, a parsing
  defect) → pin it with a test documenting current behavior, name it
  clearly, and report prominently. **Do not fix it.**
- The fake can't be written without changing `IActionHandler` → STOP;
  that would mean the interface isn't the seam I think it is.

## Acceptance criteria

1. The test files above exist and are wired in; none exceeds 800 lines.
2. Routing coverage across all action families, with mis-wiring
   specifically probed within families.
3. Guard behavior covered: no-handler, handler-without-zone,
   handler-with-zone.
4. `CommandProcessor` covered per section 3.
5. Build clean; each new binary under 5 seconds — report times.
6. Full suite ×3, documented exceptions only
   (`AudioManagerTest.PreloadCommonSounds` host ALSA;
   `RDPServerFrameTest.*` / `RDPServerLifecycleTest.*` = the known T03b
   use-after-free — name them, don't call the run clean). Report the new
   total test count.
7. `check_file_sizes.sh --files <each new file>` — no WARN/FAIL. (Its
   diff mode resolves the wrong repo root from a worktree; use
   `--files`.)

Visual gate and ARM build: not applicable (tests-only) — say so.

## Environment notes

- GLES2 config is the only tree; relative
  `-DEQT_GLES2_IRRLICHT_DIR=third_party/irrlicht-gles2-x86` works.
- **If you see an unresolved Irrlicht symbol** (e.g.
  `irr::video::g_glCounters`), your `third_party/irrlicht-gles2-x86` is
  a stale bootstrap, **not** repo breakage — re-run
  `bash <slot>/scripts/build-gles2-x86.sh --irrlicht-only`. A `git
  stash` test cannot tell those apart. This cost T11 a misdiagnosis.
- EQ assets: `data/eq_client` symlink → `/mnt/projects/EverQuestP1999`.

## Review

**Territory respected**: `tests/` only. No changes under `src/` or
`include/`. No seam was needed, as predicted.

**Files added** (all under the 800-line cap):
- `tests/action_test_fakes.h` (294 lines) — `RecordingActionHandler`, a
  recording fake `IActionHandler` implementing all ~50 pure virtuals by
  appending a canonical `"method(args)"` string to a call log. Also holds
  `Direction`/`ChatChannel` name helpers and a `Sym` wrapper so enum names
  render unquoted in the call log (distinct from `std::string` arguments,
  which are quoted).
- `tests/test_action_dispatcher.cpp` (730 lines, 33 tests).
- `tests/test_command_processor.cpp` (464 lines, 35 tests).
- Wired into `tests/CMakeLists.txt` following `test_state_modules_secondary`'s
  target block (shared `ACTION_LAYER_STATE_SOURCES` variable — event_bus,
  entity_manager, player_state, spell_state, inventory_state, group_state,
  pet_state, world_state, combat_state, tradeskill_state, game_state,
  door_state_manager — plus `action_dispatcher.cpp` and, for the
  command-processor target, `command_processor.cpp`). Neither target links
  `output_renderer.cpp`/`console_renderer.cpp`/`graphical_renderer.cpp` —
  `IOutputRenderer` is satisfied entirely by a local
  `RecordingOutputRenderer` fake in `test_command_processor.cpp`, so no
  Irrlicht is pulled in (confirmed: `nm`/link succeeded with zero Irrlicht
  symbols referenced).

**1. Routing coverage.** All families in `IActionHandler` are covered with
per-member routing assertions: movement, heading/facing (including
`setHeading` normalization and `faceLocation`/`faceEntity` heading-math),
move-to/follow, targeting (all 10 members individually, since this is
exactly the family the plan flagged as highest mis-wiring risk),
auto-attack, cast/interrupt/ability/skill, interaction (doors, corpses,
`interactNearest`'s door-vs-NPC distance comparison), chat, group, character
state, inventory, spellbook, trade, zone request, **pet** (all 9
near-identical `sendPetCommand` wrapper methods individually, verifying each
hardcoded command ID — `petAttack`=2, `petBackOff`=28, `petFollow`=4,
`petGuard`=5, `petSit`=6, `petTaunt`=12, `petHold`=15, `petFocus`=24,
`petHealth`=0 — this is the family structurally most similar to the
plan's `targetNearestPC`/`targetNearestNPC` mis-wiring example), tradeskill,
extended movement/character-state/combat, entity query, and utility.
**No mis-wired action was found** — every routing assertion passed on the
first correctly-written attempt (two early failures were test-authoring
bugs on my side: an enum-name quoting artifact in the fake's string
formatter, and a leftover placeholder assertion in an early draft of the
group-family test — both fixed before this report, see below).

**2. Guard behavior — the checkHandler-only vs checkZoneConnection split.**
Read every one of the 91 `ActionResult` methods in `action_dispatcher.cpp`
to classify the split (not inferred from names):

- **`checkHandler()`-only** (11 methods): `stopMoving`, `stopAllMovement`,
  `stopFollow`, `clearTarget`, `disableAutoAttack`, `interruptCast`,
  `openReplyToTell`, `declineGroupInvite`, `cancelCamp`, `closeSpellbook`,
  `cancelTrade`, `setPathfinding`.
- **`checkZoneConnection()`** (everything else, ~80 methods).

**Does it match what the names suggest?** Mostly yes, with one genuine
surprise pinned by a dedicated test
(`ExtendedCharacterStateFamily_RoleplayAndPathfinding`): `setRoleplay()` and
`setPathfinding()` read as symmetric toggles, but `setRoleplay` requires
zone connection while `setPathfinding` does not. The checkHandler-only set
is otherwise coherent: it's exactly the "stop/cancel/decline/interrupt"
class of actions — things that make sense to allow even when disconnecting
or not yet in zone (e.g. `stopAllMovement`, `cancelCamp`, `cancelTrade`,
`closeSpellbook`) — plus `clearTarget` and `openReplyToTell`, which are
pure client-side/UI-adjacent no-ops with respect to zone state.
`openReplyToTell` is a further curiosity: it's guarded by `checkHandler()`
but never actually dereferences `m_handler` on the success path (a
UI-layer stub per its own TODO comment) — pinned in its own test
(`OpenReplyToTell_SucceedsWithoutTouchingHandler`) so a future change that
starts calling into the handler there is visible as a diff, not a silent
behavior change.

Verified with three explicit states: handler unset (fails, `isReady()`
false), handler set + zone not connected (checkHandler-only methods
succeed and dispatch; checkZoneConnection methods fail without touching
the handler), handler set + zone connected (checkZoneConnection methods
now succeed).

**No mis-wired action found.** This is the headline result of the unit:
breadth-first per-member routing across every family, with the
highest-risk families (targeting, pet commands) checked member-by-member,
turned up zero wiring defects in `action_dispatcher.cpp`.

**3. `CommandProcessor` coverage.** `processCommand` vs `processInput`
(the latter routes non-`/`-prefixed text to the default chat channel, the
former always treats input as a command); unknown command failure with
the command name in the message; register/unregister/hasCommand/
getCommandInfo round-trips including re-registration (last-write-wins,
verified via handler + description swap) and unregistering an absent name
(no-op, doesn't throw); `getAllCommands`/`getCommandsByCategory`/
`getCategories` cross-consistency (every registered command's category is
asserted to appear in `getCategories()`); `getCompletions` (prefix match,
empty prefix reaches every registered name, no-match, exact match) and
`getAllCommandNames` (sorted, includes aliases); argument handling —
pinned the actual (asymmetric) whitespace behavior: `parseCommandLine`
trims only the *leading* run of whitespace between command and args,
trailing whitespace is passed through unchanged (two separate tests, one
per direction, so this doesn't read as an oversight); `/tell` as the
trailing-argument case (target/message split, missing-message failure);
confirmed via source read that `splitArgs` tokenizes on `istream >> token`
with **no quote handling** — a quoted phrase is still split on internal
whitespace, pinned with a dedicated test showing the literal quote
characters end up embedded in the reassembled message rather than being
treated as delimiters. Case sensitivity: command name lookup is
case-insensitive (`hasCommand`, alias resolution, direct dispatch all
verified), while command *arguments* preserve case exactly. Representative
built-in sample (`say`/Chat, `sit`/Movement, `target`/Combat, `invite`/Group,
`who`/Utility, `mem`/Spells, `pet`/Pet) checked against the actual
registered category rather than enumerating all ~60 built-ins.

**4. `InputActionBridge` — not reached.** Priorities 1–3 filled the unit as
scoped (68 new tests across two files, both under the line cap). Per the
plan's own priority order and stop condition, this is left for a follow-on
unit; nothing under `src/`/`include/` was touched to compensate.

**Visual gate / ARM build**: not applicable — tests-only unit, x86 only,
confirmed no Irrlicht link in either new binary.

**Build times**: `test_action_dispatcher` and `test_command_processor` each
built cleanly on the first attempt (no header/link errors) after the two
authoring bugs above were caught by the first local test run and fixed.

**Per-binary test runtimes**: `test_action_dispatcher` — 33 tests in ~5 ms
(process wall time ~14 ms). `test_command_processor` — 35 tests in ~18 ms
(process wall time ~28 ms). Both far under the 5-second-per-binary bar.

**Full suite ×3** (`ctest --test-dir build --output-on-failure`, GLES2-config
tree, x86 only):

| Run | Result | Failures |
|-----|--------|----------|
| 1 | 1482/1484 passed | `AudioManagerTest.PreloadCommonSounds` (documented host ALSA exception); `RDPServerFrameTest.UpdateFrameMultipleTimes` (SEGFAULT — the known T03b `RDPServer::stop()` use-after-free, intermittent, not a new regression) |
| 2 | 1483/1484 passed | `AudioManagerTest.PreloadCommonSounds` only |
| 3 | 1483/1484 passed | `AudioManagerTest.PreloadCommonSounds` only |

New total: **1484 tests** (up from the protocol's cited 1234/1235
baseline — the gap reflects everything added by T02–T11 since that number
was last recorded, not something introduced by this unit; 68 of the 1484
are new in T12). `check_file_sizes.sh --files` on all three new files:
`ok` for each, no WARN/FAIL.

**Deviations from the plan**:
- None in scope or territory. Two self-caught authoring bugs during
  development (an enum-name quoting artifact in the recording fake's
  string formatter, and a leftover placeholder assertion in an early draft
  of a group-family test) were fixed before running the acceptance suite;
  neither reached a committed/reported state as a "finding" — noted here
  only for transparency since the plan asks for deviations to be flagged
  rather than silently absorbed.
