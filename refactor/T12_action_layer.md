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

Per the protocol, plus:
- How the checkHandler-only vs checkZoneConnection split came out, and
  whether it matches what the method names suggest.
- Any mis-wired action found, prominently.
- What you did not reach (InputActionBridge is an expected casualty).
