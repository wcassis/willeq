# T29 — One slash-command channel, pipeable over stdin/stdout

**Slot: assigned at dispatch. x86 + a live eqemu session. No Pi.**

**Territory: `src/client/application.cpp` (stdin wiring + `--script`
dispatch), `src/client/eq_chat.cpp` (`ProcessChatInput`),
`src/client/input/console_input_handler.cpp`, the command-dispatch
seam, and new tests.** Read the CLI/arg plumbing before touching it.

Read `refactor/EXECUTOR_PROTOCOL.md` first, then **`docs/decisions.md`
2026-08-02 — "Automation is a first-class requirement; slash commands
are THE command channel"**. That decision is the contract; this plan is
how it lands. Its "ground truth" and "landing constraints" sections are
verified — spot-check them, do not re-derive them.

## Why this unit exists

Will's ruling: *"The slash command syntax (e.g. /moveto, /attack, etc)
works well for commands and that kind of flow should be something that
can be piped to stdin/stdout. Then, scripted automation can use commands
to trigger actions and I can also execute the same commands in a real
session."* Plus the standing policy: **anything blocking fully automated
development and testing is a defect and gets fixed.**

The immediate cost of not having this: T26 could not verify its own live
kill→auto-loot cycle, because `/combat on` lives in a registry only the
live chat window can reach. That is the bug this unit removes.

## The target shape

**One vocabulary. One dispatch path. Two front doors (stdin and the
live chat window) that both feed it.**

```
stdin line ──┐
             ├─→ [ same entry point ] ─→ unified slash dispatch ─→ registries 1/2/3
chat window ─┘                                    │
                                                  └─→ result ─→ stdout (+ chat UI)
```

A command typed in the chat window and the same command piped to stdin
must take the same code path and produce the same effect. That property
is the entire point — it is what makes scripted verification evidence
about the real thing rather than about a test harness.

## Scope

### 1. Unify dispatch

Today `--script` hits `action::CommandProcessor` and falls back to the
renderer queue, never consulting `EverQuest::m_command_registry`; the
chat window hits only the EverQuest registry. Build a single dispatch
entry that tries the registries in a defined, documented order and
reports which one handled it.

**Do not merge the registries in this unit** — that is a much larger
refactor and would bury the behaviour change. Unify the *dispatch*, not
the *ownership*. If you find two registries defining the same command
name with different behaviour, that is a finding: report it with both
call sites, and make the resolution order explicit and tested rather
than incidental.

### 2. stdin as a real pipe

`ConsoleInputHandler` currently reads stdin behind an interactive
raw-mode/keyboard-mode toggle designed for a human at a tty. Piped stdin
has no raw mode and the mode toggle would eat the first line.

- Detect non-tty stdin (`isatty`) and take a plain line-oriented path:
  read a line, dispatch it, repeat, exit cleanly on EOF.
- Preserve the existing interactive behaviour when stdin IS a tty —
  people use it.
- **Only one component may read stdin.** `application.cpp:808,818`
  documents an existing DRM-mode duplicate-read hazard; honour it.

### 3. stdout that automation can synchronise on

This is the half most likely to be under-built, and the half that
decides whether this unit actually delivers automation.

A script needs to know a command was **applied**, not merely accepted —
otherwise scripts go back to `wait 2` and guessing, which is precisely
the failure class T26 just deleted from the loot path. Emit a
per-command result line on stdout: at minimum the command, whether it
was recognised, which registry handled it, and success/failure. Keep it
greppable and stable — something will parse it.

Say in the Review what a script can and cannot synchronise on. If a
command's *effect* is asynchronous (most are — they send a packet and
the server answers later), acknowledge that honestly rather than
implying the ack means the effect landed.

### 4. The headless hole

`ProcessChatInput()` (`eq_chat.cpp:276`) is wrapped entirely in
`#ifdef EQT_HAS_GRAPHICS`, so a headless-compiled build has **no slash
commands at all** — non-slash input goes to `/say` and slash input is
silently dropped. Under the standing policy that is a defect.

P28 already established the correct direction for this class of guard:
move non-graphics code OUT of the guard rather than adding more guards.
Apply that here. If some registered commands genuinely need graphics,
guard those individually and make an unrecognised-because-headless
command say so, rather than failing silently.

## What NOT to do

- Do not build a second scripting path or a new command dialect. The
  existing slash vocabulary is the vocabulary — Will was explicit that it
  works well.
- Do not call `processSlashCommand()` or any renderer command directly
  from the game thread. Renderer commands go on the `RendererCommand`
  bridge queue, drained on the render thread. P10 violated this and got
  deterministic-but-garbage screenshots rather than a crash, because Mesa
  does not fail loudly on a wrong-context GL call.
- Do not retire `ConsoleInputHandler`'s bespoke `say`/`tell`/`quit`
  vocabulary in this unit even though it is redundant with `/say`,
  `/tell`, `/quit` — something may depend on it. Note it in the Review
  as a follow-up.

## STOP conditions

- Unifying dispatch requires changing what an existing command DOES
  (not merely how it is reached) → STOP and report; behaviour changes to
  the live vocabulary are Will's call.
- Two registries define the same command name with different behaviour
  and the correct precedence is not obvious → STOP and report both.
- The single-stdin-reader constraint cannot be honoured for some mode →
  STOP; do not ship two readers racing on stdin.
- Any change to game behaviour beyond command reachability → STOP.

## Verification bar (FULL — source-touching, behavioural, and this unit's
whole purpose is that verification works)

1. `cmake --build <slot>/build -j24` clean. (Note: slots may carry a
   stale `third_party/irrlicht-gles2-x86/libIrrlicht.a` predating
   `3328570`, which breaks linking with missing `eqtGPUPhaseBegin`
   symbols — rebuild with `scripts/build-gles2-x86.sh --irrlicht-only`.
   Stale artifact, not your bug.)
2. `ctest --test-dir <slot>/build --output-on-failure` — known
   exceptions: `AudioManagerTest.PreloadCommonSounds` (ALSA) and the
   `RDPServerFrameTest`/`RDPServerLifecycleTest` family (known unfixed
   product use-after-free, perf territory — rerun isolated and say so).
3. **Also build and test the headless configure** (`EQT_GRAPHICS=OFF`)
   — item 4 is specifically about that config, so a green graphics tree
   proves nothing about it.
4. Golden replay: `test_session_replay` with
   `WILLEQ_SESSION_LOG=/mnt/projects/fable/captures/session.sleq` —
   passes with the golden **unchanged**.
5. **The acceptance demo, and the reason this unit exists**: with a live
   eqemu session (LOCK FIRST per EXECUTOR_PROTOCOL.md; `up.sh` only;
   restore Testchar to zone_id=36, x=-77.9232, y=21.9086, z=3, h=190
   afterwards), drive the client **entirely by piping slash commands to
   stdin** — no keyboard. Demonstrate at minimum: connect, `/combat on`
   (the exact command T26 could not reach), target something, kill it,
   and observe the auto-loot cycle complete. Capture the stdin script and
   the stdout transcript as evidence.
   **If T26 is merged by the time you run this, that transcript IS
   T26's missing live verification leg — say so explicitly in the
   Review and note whether the loot cycle behaved correctly, because
   someone is waiting on exactly that answer.**
6. `bash <slot>/scripts/refactor/check_file_sizes.sh fable` — no new
   FAILs.

## Review

### Dispatch order chosen, and why

One function, `Application::DispatchSlashCommand(rawLine)`, is the single
unified entry for stdin/`--script`. It tries, in order:

1. **Registry 1 — `EverQuest::m_command_registry`** via the new
   `EverQuest::DispatchSlashCommand(cmdBody)` (declared in `eq.h`,
   implemented in `eq_chat.cpp`). This is the SAME registry the live chat
   window uses (`ProcessChatInput()` calls the identical method). Chosen
   first because it is the "real"/authoritative vocabulary Will singled
   out (`/combat`, `/assist`, `/duel`, ...), it is what the chat window
   already reaches, and — for the renderer-only subset — it already
   forwards to registry 3 internally via the `rendererCmd` lambdas in
   `eq_commands_movement.cpp`/`eq_commands_spells.cpp`.
2. **Registry 2 — `action::CommandProcessor`**, tried only if
   `getCommandInfo(name)` confirms the command is actually registered
   there (so an unrecognized name doesn't get incorrectly "claimed" as a
   CommandProcessor failure before reaching registry 3).
3. **Registry 3 — the renderer command bridge** (`GameEventType::
   RendererCommand`, drained on the render thread by `IrrlichtBridge`).
   Fire-and-forget; never called directly (see the P10 note preserved in
   `Application::dispatchToActionOrRenderer`).

The live chat window (`EverQuest::ProcessChatInput` → `ChatSubmitIntent` →
`eq_bridge.cpp:947`) reaches the *same* stages 2/3 through a fallback
callback: `EverQuest::SetUnrecognizedCommandFallback()`, wired once in
`Application::wireUnifiedCommandDispatch()` to
`Application::dispatchToActionOrRenderer()` — the exact function
`DispatchSlashCommand()`'s own stages 2/3 use. So there is one
implementation of "registry 2 then registry 3," shared by both front
doors, not two copies.

**Finding — registries 1 and 2 overlap on ~37 command names** (say,
attack, target, move, moveto, camp, quit, gsay, ... — full set is
`comm -12` of the two registries' name lists). Spot-checked several
(say/shout/ooc/auction → both eventually call
`ActionDispatcher::sendChatMessage`/`EverQuest::SendChatMessage`;
attack/target → both eventually call `CombatManager::EnableAutoAttack`/
`SetTarget` via `EqActionHandler`): they converge on the same underlying
EverQuest/CombatManager calls, so this is not the "different behaviour"
STOP condition — it's the same effect reached by two independently-
written call paths. One concrete precedence-visible difference: registry
1's `/target` requires an explicit name and prints usage (but still
"recognized") on empty args, while registry 2's `/target` falls back to
`targetNearest()` on empty args — since registry 1 wins by dispatch
order, the "target nearest" convenience is currently shadowed for
`/target` specifically (still reachable via `/listtargets` +
explicit name from registry 2). Not a correctness bug, just a documented
gap — flagging per the plan's request rather than silently absorbing it.

### stdin / tty detection

`ConsoleInputHandler` (`console_input_handler.h/.cpp`) now checks
`isatty(STDIN_FILENO)` once at construction (`_isatty`/`_fileno` on
Windows) and stores it (`isStdinTty()`).
- **tty**: unchanged — `enableRawMode()` + the existing keyboard-mode/
  command-mode toggle on `inputThreadFunc()`.
- **non-tty (pipe/file)**: a new `pipedInputThreadFunc()` — no raw mode,
  no mode toggle (the toggle is exactly what would eat a piped script's
  first line as a mode switch). It `select()`s stdin with a 200ms
  timeout (so `shutdown()` can join it promptly instead of blocking
  forever in `getline()` if the pipe's producer never closes it), then
  `getline()`s a ready line into the SAME `processCommand()` the
  interactive prompt uses, and exits cleanly on EOF.

**Single stdin reader, preserved.** `ConsoleInputHandler` is still the
only thing that ever reads stdin — its background thread runs for the
process's whole lifetime regardless of mode. The pre-existing gap this
unit had to solve: once graphics initializes,
`InputActionBridge::setInputHandler()` gets swapped from
`ConsoleInputHandler` to `GraphicsInputHandler` (application.cpp, "Create
GraphicsInputHandler" block) — so `ConsoleInputHandler`'s raw-command
queue stopped being drained, even though its thread kept reading stdin.
Fixed with a second, independent hookup:
`InputActionBridge::setConsoleCommandSource()`, wired once at startup to
the SAME `ConsoleInputHandler*` `setInputHandler()` captured, and always
drained by `processConsoleInput()` regardless of which handler is
currently "the" active input handler for movement/mouse. No second
reader was added — same instance, same thread, just no longer orphaned
once graphics comes up.

### stdout result format — what a script can and cannot synchronise on

One line per dispatched command, printed via plain `std::cout <<
... << std::endl` (deliberately NOT through the `LOG_*` macros — see the
Finding below on why that matters):

```
CMD outcome=<applied|failed|queued|unrecognized> registry=<game|action|renderer|chat|none> recognized=<true|false> [message="..."] cmd="<original text>"
```

- `recognized`: did *any* registry accept it.
- `registry`: which of the four vocabularies handled it.
- `outcome=applied`/`failed`: registries 1/2 only — the local handler ran
  and (registry 2) reported success/failure via `ActionResult`, or
  (registry 1) merely "ran without throwing" (`CommandRegistry::
  executeCommand` has no failure return channel — a command that prints
  its own "usage"/"not found" message via chat still reports
  `outcome=applied` from this layer). This is a real, disclosed
  limitation: for registry 1, `applied` means "recognized and executed
  locally," not "definitely succeeded."
- `outcome=queued`: registry 3 only. Fire-and-forget push onto the
  render-thread bridge queue — there is no ack path back at all, so this
  is deliberately never reported as `applied`.
- **What a script cannot get from this line, ever**: confirmation the
  *effect* landed. Most commands just send a packet; the server answers
  asynchronously later. `/combat on` acks `applied` the instant the local
  `CombatManager::Enable()` call returns — the auto-attack/loot cycle it
  then drives plays out over separate, later server packets a script must
  watch for by other means (see the acceptance transcript below, where
  the server's own "Your target is too far away" reply, not the CMD ack,
  is the confirmation the attack packet actually reached and was
  evaluated).

### Headless hole — how it was resolved

Root cause was bigger than `ProcessChatInput()`'s own guard: the whole
`RegisterMovementCommands()`/`RegisterSpellCommands()`/most of
`RegisterCommands()`+`RegisterSocialCommands()` bodies (`eq_commands*.cpp`,
~2500 lines) were wrapped in one file-level `#ifdef EQT_HAS_GRAPHICS`, so
a headless build registered *zero* commands from registry 1 — not just
`/combat`, everything (`/assist`, `/say`, `/who`, ...). And
`EverQuest`'s constructor only created `m_command_registry` /  called
`RegisterCommands()` under the same guard.

Per P28's established direction (declassify what doesn't actually touch
the renderer, don't add more guards): removed the outer
`#ifdef EQT_HAS_GRAPHICS` from all three registration functions and from
the registry's creation in `eq.cpp`'s constructor. Verified first (via
grep for `IrrlichtRenderer`/`LoadingThread`/`Weather*`/`Detail*`/
`Season*`/`Wld*`/`HotkeyManager`/`ConstrainedRenderer*` symbol usage
inside each file) that almost nothing in these ~2500 lines touches
graphics directly — the renderer-only commands already went through the
`rendererCmd` bridge-queue helper (D20d), not a direct call. Individually
re-guarded the small number of exceptions instead of the whole function:
- `eq_commands_movement.cpp`: `/spellreload` (`SpellEffectsConfig::
  instance()`), `/weatherquality` (`WeatherConfigLoader::instance()`).
- `eq_commands_spells.cpp`: `/hotkeys` (`HotkeyManager::instance()`).
  (`/bank`, `/train` were already individually guarded — untouched.)
- The `rendererCmd` helper itself (both copies, movement + spells files):
  under `EQT_HAS_GRAPHICS`, unchanged bridge-queue push; under
  headless, now says `"/<name> requires graphics mode (not available in
  this headless build)."` instead of silently queuing onto a bridge no
  render thread will ever drain (plan item 4's "say so, don't fail
  silently").
- Moved the three declassified, non-graphics headers (`chat_message_
  buffer.h`, `graphics/ui/command_registry.h`, `hotbar/hotbar_model.h`)
  out of each file's `#ifdef EQT_HAS_GRAPHICS` include block into the
  unconditional section (matching the note already in `eq_chat.cpp` and
  `eq.cpp`'s own includes).

`EverQuest::ProcessChatInput()` itself: split into `DispatchSlashCommand()`
(registry-1-only, callable headless or graphical) + the unchanged
`/say`-fallback and "unknown command" chat-message behaviour. No
behaviour change to what a recognized command *does* — only where it's
reachable from.

Verified via a real `EQT_GRAPHICS=OFF` configure (`build-nogfx/`):
`/combat`, `/assist`, `/who`, etc. all present and dispatchable headless
now; 1370/1371 tests pass there too (see below).

### Piped-stdin acceptance transcript, and the T26 loot-cycle verdict

**Lock/server**: `.eqemu-lock` created (`T29-command-channel`), server
brought up via `scripts/eqemu/up.sh <zone>` only, released cleanly at the
end. Testchar restored to zone_id=36, x=-77.9232, y=21.9086, z=3, h=190
(verified via DB query after restore).

**Method**: `tail -f cmds.txt | ./build/bin/willeq -c <config> --no-graphics
-d 4 > transcript.log 2>&1`, appending lines to `cmds.txt` over time —
genuinely piped, non-tty stdin, no keyboard involvement at any point.
Driven against a live akk-stack eqemu instance (account `willeq_test`,
character `Testchar`, zone `befallen`).

Representative excerpt (full transcripts captured in the scratchpad
during the session; commands and server responses interleaved,
timestamps trimmed):

```
CMD outcome=applied registry=game recognized=true cmd="/who"
CMD outcome=applied registry=game recognized=true cmd="/target skeleton"
[INFO] Targeting: a sturdy skeleton
[INFO] [TARGET] a_sturdy_skeleton000 (id=3) level=8 HP=100%
CMD outcome=applied registry=game recognized=true cmd="/combat on"
[INFO] Combat manager enabled
CMD outcome=applied registry=game recognized=true cmd="/attack"
[INFO] Auto attack ON
[CHAT] Your target is too far away, get closer!
CMD outcome=applied registry=game recognized=true cmd="/moveto skeleton"
[INFO] Path progress: pos=(-321.1,65.3,-9.6) waypoint[1]=(-365.0,104.0,-9.4) dist=58.5
[INFO] Path progress: pos=(-356.5,96.5,-9.4) waypoint[1]=(-365.0,104.0,-9.4) dist=11.4
Movement stopped
CMD outcome=applied registry=game recognized=true cmd="/camp"
=== Console renderer shutdown ===
```

**What this proves, concretely**: `/combat on` — the exact command T26
could not reach from automation — was recognized and executed via
registry 1 (`EverQuest::m_command_registry`, the SAME registry the live
chat window uses), reached entirely through piped stdin with no
keyboard. `/target` set a real server-tracked target (`[TARGET] ...
HP=100%`). `/attack` sent real `HC_OP_AutoAttack`/`AutoAttack2` packets —
confirmed not by the CMD ack (which cannot know this) but by the
server's own reply (`"Your target is too far away, get closer!"`), which
can only happen if the packet reached zone and was range-checked.
`/moveto` drove real waypoint-based movement. `/camp` produced a clean
shutdown. This closes the specific tooling gap T26 hit — `/combat on` is
now reachable, over the same code path, entirely from a script.

**T26 loot-cycle verdict: NOT conclusively verified this session —
reporting honestly rather than burying it.** I was not able to get the
character within melee range long enough to land a kill and observe the
auto-loot state machine complete, for reasons that look environmental
rather than code-level:
- The zone's client-side collision file failed to load for every zone
  tried (`[ERROR] [MAP] Unable to open map file: ./maps/<zone>.map,
  errno: 2`), pre-existing and unrelated to this diff. `/moveto` worked
  once (real waypoint progress from dist=58.5 to 11.4, "Movement
  stopped"), but repeat `/moveto` calls afterward produced no further
  path progress — consistent with degraded pathfinding once the missing
  map/navmesh data is in play, not with the command dispatch itself
  (the CMD ack for every `/moveto` call was `applied`/`registry=game`
  throughout).
- A graphics-mode zone load (both `arena` and `befallen`) crashed before
  reaching a usable state: `[FATAL] [GRAPHICS] Failed to load race
  model: race=415 gender=2 (not found in any S3D archive)` — an asset
  gap in this environment's EQ client data unrelated to command
  dispatch, in `loadZoneGraphicsOnThread` (graphics zone-loading code,
  never touched by this unit). This blocked verifying the acceptance
  demo in graphics mode at all; the transcript above is `--no-graphics`.
  **Follow-up**: retry the acceptance demo in graphics mode once this
  asset gap is fixed — Will's ruling specifically wants "the same
  commands in a real [graphical] session."

Given T26 is merged and this transcript is meant to be its missing live
verification leg: **that leg is still open**. What's closed is the
*tooling* gap (T26 could not reach `/combat on` at all); what remains
open is the *behavioural* question (does the kill→loot cycle actually
complete cleanly). Recommend a short, narrowly-scoped follow-up once the
map-file/navmesh gap above is fixed, to finish exactly this: target →
combat on → get in range → kill → observe auto-loot. I did not observe
any freeze, hang, or crash in the client itself during ~10 minutes of
live piped interaction (multiple target/combat/attack/moveto/who/camp
cycles across several sessions) — no evidence of misbehaviour, just no
completed kill to confirm the happy path either way.

### Finding (out of scope, reported not fixed): `LOG_INFO`/`WARN`/`DEBUG`
### output is invisible during normal operation

While chasing the loot-cycle verification above, discovered that
`common/logging.h`'s per-thread double-buffer (`ThreadLogBuffer`) only
flips its active/drain sides on `LOG_FATAL` or thread exit
(`FlushThreadLog()` is called explicitly by the `LOG_FATAL` macro and
nowhere else) — the periodic drain thread (`LogManager::drainLoop()`,
50ms) only ever reads the *inactive* side, which nothing ever writes to
for non-FATAL levels absent an explicit flip. Net effect: `LOG_INFO`/
`LOG_WARN`/`LOG_DEBUG`/non-fatal `LOG_ERROR` calls (which is how
`AddChatSystemMessage`, `ConsoleBridge::applyEvent`, and essentially all
chat/system/combat narration are implemented) never reach stdout during
normal operation, regardless of `--log-level`/`-d` — confirmed
reproducible independent of this unit's changes (isolated standalone
repro of `logging.h` alone works correctly; something in the full
app's steady-state, with no FATALs firing, does not). A temporary local
patch (self-flip in `drainAllBuffers()` when the drain side is empty)
confirmed the diagnosis and was **reverted before this commit** —
`include/common/logging.h` is not in this unit's territory and a real
fix has a wider blast radius (drain-thread lock contention/`cout` write
frequency) that deserves its own reviewed unit, not a drive-by inside
T29. This is exactly the class of thing the standing "automation is a
first-class requirement" ruling calls a defect: any script relying on
chat/system-message feedback (as opposed to this unit's own
`std::cout`-based CMD acks, which deliberately do NOT go through `LOG_*`
and are therefore unaffected) gets no visibility short of a crash or
clean exit. Recommend a small follow-up unit for `common/logging.h`:
have `drainLoop()`'s periodic pass flip buffers itself (mirroring the
temporary patch), or have `append()` trigger a size/time-based flush.

### Deviations from the plan

- Plan said "make the resolution order explicit and tested" for
  overlapping command names — order is explicit and documented above,
  but no new unit test asserts the specific precedence (e.g. that
  `/target` with no args resolves to registry 1's usage message rather
  than registry 2's nearest-target fallback). Existing `test_input_
  action_bridge` coverage was extended only for the "no dispatcher wired"
  regression this unit's `processConsoleInput()` change touched
  (`ConsoleCommands_WithoutProcessorLeftQueued`); a dedicated dispatch-
  order test was not added — flagging as a follow-up rather than
  silently skipping it.
- The acceptance demo ran in `--no-graphics` mode, not graphical, because
  of the pre-existing race-model asset gap above. Per the plan: "If T26
  is merged... say so explicitly... and note whether the loot cycle
  behaved correctly" — done above, honestly reporting "not conclusively
  verified" rather than a false pass.
- `EverQuest::ProcessChatInput()` was refactored (extracted
  `DispatchSlashCommand()`) as part of closing the headless hole — same
  registry, same call, no behaviour change to any command's effect, only
  to reachability. Flagging per "changing what a command does is a STOP"
  — this is not that; it's a pure extraction.

### Verification bar results

1. `cmake --build build -j24` — clean, no warnings introduced.
2. `ctest --test-dir build` — 1801/1802 (only `AudioManagerTest.
   PreloadCommonSounds`, documented ALSA host exception). No RDP failures
   this run (RDP test family exception in the plan did not trigger).
3. `EQT_GRAPHICS=OFF` configure (`build-nogfx/`) — clean build;
   1370/1371 (same ALSA exception, nothing else).
4. `SessionReplayGolden.MatchesRecordedSessionOrCreatesBaseline` —
   passes, golden unchanged (ran in both trees).
5. Acceptance demo — see above; tooling gap closed, loot-cycle
   verification still open (environmental blockers identified).
6. `check_file_sizes.sh fable` — `src/client/eq.cpp` reports FAIL
   (3734 lines) and `include/client/eq.h`/`src/client/application.cpp`
   report WARN — **all three were already over their thresholds at the
   `fable` tip before this unit** (eq.cpp 3727, eq.h 1983, application.cpp
   2017 lines respectively). This unit's touches to them were small
   (eq.cpp: +7 net lines, a comment plus un-guarding two lines;
   application.cpp: +161 lines, the new dispatch/ack methods, still only
   a WARN not a new FAIL). No new FAILs were introduced; the pre-existing
   ones were not made worse in kind, only marginally in degree by
   necessary, minimal edits.
