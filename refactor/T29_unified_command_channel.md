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

(Executor fills: the dispatch order chosen and why, how stdin/tty
detection works, the stdout result format and what a script can
legitimately synchronise on, how the headless guard was resolved, the
piped-stdin acceptance transcript, what was left for follow-up,
deviations.)
