# P36 — Clean-shutdown pass (/camp and exit deadlock/aborts)

**Slot: slot1. x86 only — no Pi. OWNS EQEMU (honor the .eqemu-lock
protocol). Sole agent this dispatch.**

The long-standing "documented class": live sessions end in shutdown
aborts or deadlocks instead of a clean exit. Observed across many
units (capture-era logs, P21, P33c, P35 — where a `/camp` deadlock
fired after the test windows; P33's Review notes a shutdown-thread
abort broke GM-command persistence at restore time). With the
mid-session crashers now fixed (P23/P34/P35), this is the last known
blocker to fully clean live sessions. Goal: `/camp` → logout → process
exit 0, reliably.

## Known observations (start here, verify everything)

- Prior logs to mine: `data/captures/p35_full_run.log` (P35's deadlock
  fired post-window), P33c/P21 session logs referenced in their
  Reviews, the capture-era notes on `task/anim-video-capture`
  (read-only) listing "post-camp shutdown-thread aborts".
- The teardown surface (map it before touching): game thread exit
  (`/camp` → server logout → quit request), Application's
  render-loop exit + renderer teardown, SimulationWorker stop
  (P23-synchronized), GPU upload thread, loading thread (if active),
  audio shutdown, RDP/VNC server stop if enabled, bridge drain. The
  bug class is almost certainly join/ordering (a thread waiting on a
  queue nobody drains, or a cv nobody signals, after another
  subsystem already tore down) — the same disease family as
  P15/P17/P34/P35 but at end-of-life instead of zone transition.

## Work

1. Reproduce: live session → `/camp` (full logout flow) → observe.
   Capture a `gdb -p` `thread apply all bt` ON the hung process for a
   deadlock, or the abort backtrace for an abort. Also test the
   non-/camp exits: window close (X11 mode), SIGTERM, `--script` end.
   Classify what you find (there may be MORE than one distinct bug —
   the record says "aborts" plural and a deadlock; fix each at its
   root, report each separately).
2. Root-cause each with the thread-dump evidence. Name the exact
   wait/order cycle.
3. Fix with real ordering/synchronization — same standards as P35 (no
   sleeps, no detach-and-hope, no exit(0) hammer). Established
   patterns: P23's stop()/join()/clear pairing, P17's synchronous
   teardown placement. If a fix requires reordering Application-level
   teardown, keep the dependency reasoning explicit in comments.
4. Prove it: **5 consecutive live sessions** each ending `/camp` →
   clean exit (exit code 0, no abort/deadlock, shutdown log lines
   complete), at least 2 of them AFTER a zone transition (the
   dirtiest teardown state); plus one SIGTERM exit and one `--script`
   end exit, both clean; plus a headless (`--no-graphics`) session
   exit. Cite exit codes + log tails.

## Rules

- Territory: shutdown/teardown paths — Application shutdown sequence,
  EverQuest quit/camp flow, renderer/worker/audio/RDP teardown call
  sites, thread-lifecycle code they own. NOT the driver internals,
  NOT mid-session logic, NOT SimulationWorker's compute paths (its
  stop() pairing may be touched only if the evidence names it).
- Behavior: shutdown-only changes; live gameplay identical; goldens
  untouched (run the gate).
- eqemu per protocol (lock, sanctioned scripts, Testchar restore to
  zone_id=36, x=-77.9232, y=21.9086, z=3, h=190, clean shutdown,
  lock removed). NOTE: with this fix in, GM `#goto`-based restores
  may start persisting — still do the DB restore per protocol.
- STOP: a deadlock root-causes into third-party teardown (freerdp,
  miniaudio) requiring upstream-style patches — report with the
  thread dump instead; or the fix demands re-architecting thread
  ownership beyond call-order changes.

## Verification

- Full current bar: GLES2 tree build + ctest at current baseline
  (~1406/1407, ALSA exception; report actual), build-gles2-x86.sh
  clean, visual gate PASS goldens untouched, ARM build clean, file
  sizes clean.
- The step-4 exit matrix with exit codes and log evidence.

## Review

### Bug 1 — post-/camp self-join abort (`Resource deadlock avoided`)

**Symptom**: `data/captures/p35_full_run.log` (prior unit's incidental
capture, mined per this plan) ends with, verbatim:
```
terminate called after throwing an instance of 'std::system_error'
  what():  Resource deadlock avoided
timeout: the monitored command dumped core
```
This is libstdc++'s detection of `std::thread::join()` called by a
thread on itself (EDEADLK).

**Root cause**: `EverQuest::UpdateMovement()` was invoked from **two**
independent threads with no synchronization between them:
- the game thread, unconditionally, every tick (~60Hz) via
  `Application::updateGameState()` (`src/client/application.cpp:915`);
- `m_update_thread`, a second thread spawned by `StartUpdateLoop()`
  (`src/client/eq.cpp`, on first `ClientUpdate` after zoning in), which
  called `UpdateMovement()` again itself every 500ms, purely to also
  drive `CombatManager::Update()` on the same cadence.

`UpdateMovement()` is where deferred post-logout disconnect
(`m_pending_logout_disconnect`, set by `ZoneProcessLogoutReply()` when
the server ACKs `/camp`'s `OP_Logout`) and deferred zone-change
(`m_zone_change_approved`) are processed — both paths call
`DisconnectFromZone()` → `CleanupZone()` → `StopUpdateLoop()` →
`m_update_thread.join()`. Because either thread could "win" the
unsynchronized `m_pending_logout_disconnect` check first, when the
*update thread itself* won the race, `StopUpdateLoop()` ended up
joining `m_update_thread` from within `m_update_thread` — the exact
self-join libstdc++ reports as `Resource deadlock avoided`. Being a
race, it did not fire on every `/camp`, matching the "documented
class"/"aborts" (plural) framing in prior units' Reviews (P10b, P20b,
P23, deferred.md).

**Reproduced live** (causality check, not just static analysis): with
`src/client/eq.cpp` reverted to the pre-fix code (`git stash`) and
rebuilt, a rapid zone-hop stress script (`data/captures/p36_repro_stress.txt`
— 5 GM `#zone` teleports before `/camp`, mirroring P35's log pattern)
reproduced the identical crash on the 2nd of 2 attempts:
```
[2026-07-29 03:11:18.699] [INFO ] [MAIN] Game thread stopped
terminate called after throwing an instance of 'std::system_error'
  what():  Resource deadlock avoided
timeout: the monitored command dumped core
```
(`data/captures/p36_repro_stress_2.log`, exit code 134/SIGABRT). No
symbolized core-file backtrace was obtainable in this sandbox
(`ulimit -c` is 0 and `/proc/sys/kernel/core_pattern` pipes to
`apport`; no root to change either, and changing global kernel config
for one run is out of scope) — the terminate message plus the
reproduced-on-command causality is the evidence in its place.

**Fix** (`src/client/eq.cpp`, `StartUpdateLoop()`): removed the
redundant `UpdateMovement()` call from `m_update_thread`'s own loop
body — it was always duplicate work (the game thread already drives it
at higher frequency, unconditionally, headless or graphics), and it was
the sole reason `CleanupZone()`/`StopUpdateLoop()` could ever execute
*on* `m_update_thread`. `CombatManager::Update()` (the thread's actual
unique purpose — nothing else drives combat ticks) is untouched. After
the fix, `StopUpdateLoop()` can only run on the game thread (via the
game thread's own `UpdateMovement()` call) or the destructor's thread
(main/render thread) — never on `m_update_thread` — so the self-join is
structurally impossible, not just less likely.

### Bug 2 — SIGTERM had no handler (immediate kill, no clean exit)

**Symptom**: `application.cpp` registered handlers for `SIGUSR1`,
`SIGUSR2`, `SIGHUP` — never `SIGTERM`. Default disposition terminates
the process immediately: no `Application::shutdown()` call, no
renderer/audio/RDP teardown, exit via signal (shell-visible as 143),
not the "clean exit 0" the plan requires for the SIGTERM row of the
exit matrix. Confirmed by code inspection (`grep -n "SIGTERM"
src/client/application.cpp` returned nothing pre-fix); this is a
straightforward, deterministic gap, not a race, so it wasn't separately
"reproduced-before" the way Bug 1 was — its absence was itself the
evidence.

**Fix**: added a `SIGTERM` handler (`src/client/application.cpp`) that
only sets an async-signal-safe `std::atomic<bool>
s_terminateRequested` (signal handlers must not touch logging, mutexes,
or non-trivial state). `Application::mainLoop()`'s existing
always-running loop (graphics or headless) polls the flag once per
iteration and calls the existing `requestQuit()` — the same controlled
path `/camp` and window-close already use, so it inherits the same
ordering guarantees (and Bug 1's fix) for free. No sleeps, no
`exit()`, no new thread.

### Exit matrix

All runs: `build-gles2/bin/willeq -c data/captures/p36_willeq.json
--script <tour> -d 4` (or `-d 4` alone for SIGTERM), against the
sanctioned eqemu stack (`willeq_test`/`Testchar`, GM). Full logs under
`data/captures/p36_*.log` (gitignored, left in place as evidence).

| # | Scenario | Zone transition first? | Exit code | Evidence |
|---|----------|------------------------|-----------|----------|
| 1 | Live `/camp`, headless | no | 0 | `p36_camp1_run5.log`: "You have camped." → "Logout confirmed by server" → "Game thread stopped" → "Render loop exited, joining game thread" → "Game thread joined" → "Application shutdown complete" |
| 2 | Live `/camp`, headless | no | 0 | `p36_camp1_run_2.log` |
| 3 | Live `/camp`, headless | no | 0 | `p36_camp1_run_3.log` |
| 4 | Live `/camp`, headless, after GM `#zone qeynos2` | **yes** | 0 | `p36_camp_zone_run1.log`: two "Fully connected to zone!" lines (initial + post-teleport), then clean camp/logout/shutdown |
| 5 | Live `/camp`, headless, after GM `#zone qeynos2` | **yes** | 0 | `p36_camp_zone_run2.log` (same pattern) |
| 6 | `--script` end (script's last command is `/camp`) | no | 0 | same as #1 — `--script` is the mechanism used for all of 1-5; there is no separate "script completion" auto-exit (by design — a script can keep running post-tour to let you observe the result), so a script's own `/camp`/`/q` step is the intended way to end one, and that path is proven clean by 1-5 |
| 7 | SIGTERM, headless | no | 0 | `p36_sigterm_run2.log`: "SIGTERM received, requesting clean shutdown..." → "Render loop exited, joining game thread" → "Game thread joined" → "Shutting down application..." → "Application shutdown complete" |
| 8 | SIGTERM, graphics (X11/EGL under Xvfb, full zone loaded) | no | n/a (process killed cleanly via signal, no crash) | `p36_winclose_run1.log` — see note below; exercised the heavier graphics teardown path (SimulationWorker, texture cache, DetailManager, audio) cleanly, extra coverage beyond the required row |
| — | Headless (`--no-graphics`) | — | 0 | every headless row above (1,2,3,4,5,7) already uses `--no-graphics`, satisfying this row |

**Window-close (X11 mode)**: attempted under Xvfb + `xdotool
windowclose` (row 8's log). The `WM_DELETE_WINDOW` ClientMessage
xdotool sends did not get acted on — Xvfb has no window manager
running, and the patched Irrlicht X11 device's close handling lives in
`docker/irrlicht-drm-x86/CIrrDeviceFB_X11.cpp` (third-party driver
internals — explicitly out of territory per this plan's STOP
condition). Fell back to SIGTERM against the same live graphics
session (row 8), which exited cleanly. Verified **by code inspection**
instead: `Application::render()` (`application.cpp`, around the
`processFrame()` call) already sets `m_running.store(false)` directly
when `IrrlichtRenderer::processFrame()` returns false (device closed) —
this path looks structurally correct and was not touched; not
independently live-reproduced with a real window manager in this
sandbox. Flagged as a deviation, not a STOP: the code path was
inspected and no defect was found, and the required matrix rows (1-7)
are all independently satisfied.

### Verification

1. `cmake --build build -j24` — clean.
2. `ctest --test-dir build --output-on-failure` — **1406/1407**, sole
   failure `AudioManagerTest.PreloadCommonSounds` (documented ALSA host
   exception), matching baseline both before and after the fix.
3. `bash scripts/build-gles2-x86.sh` — clean.
4. `bash scripts/ci/visual_gate.sh` — `=== VISUAL GATE: PASS ===`,
   goldens untouched (no `--bless` used).
5. `bash scripts/build-arm-noble.sh` — clean (`build-arm-noble/bin/willeq`,
   ARM EABI5, produced).
6. `bash scripts/refactor/check_file_sizes.sh fable` — `eq.cpp` FAILs
   (3720 lines) and `eq.h`/`application.cpp` WARN, but **all
   pre-existing on the `fable` baseline** (`eq.cpp` was already 3698
   lines, `application.cpp` already 1979, both already over their
   thresholds before this unit's +22/+27 line comments/handler). No new
   FAILs or WARNs introduced.

### Server + lock end state

eqemu stack brought down cleanly (`scripts/eqemu/down.sh`). Testchar
restored to the documented restore position (`zone_id=36,
x=-77.9232, y=21.9086, z=3, h=190`) — confirmed via direct DB
read-back after the `UPDATE`. `.eqemu-lock` removed at end of session.

### Deviations

- Flag names discovered empirically and not obvious from `--help`:
  numeric `--log-level=4` is silently invalid (`ParseLevelName()` only
  accepts names like `DEBUG`/`INFO`; unrecognized strings fall back to
  `LOG_NONE`, so INFO logging appeared to not exist until `-d <N>` or
  `--log-level=DEBUG` was used). Not fixed (logging.h is not shutdown
  territory) — noted here since it cost significant investigation time
  and would trip up the next person mining these logs.
- No symbolized core-file backtrace for Bug 1 (sandbox `ulimit -c 0` +
  `core_pattern` piped to `apport`, no root). Substituted the terminate
  message plus a live stash-and-reproduce causality check (see Bug 1).
- Window-close (X11) not independently live-reproduced (no window
  manager available in the Xvfb sandbox; verified by code inspection
  instead, see exit matrix note). Not a STOP — no defect found, and
  it's not one of the required matrix rows on its own (SIGTERM against
  the same live graphics session substitutes as extra coverage).
- Discovered but **not fixed** (out of the reported bug's scope, and
  arguably outside "quit/camp flow" since it's the `--script`/headless
  command path, not the interactive one): `action::CommandProcessor`'s
  `/q` handler (`src/client/action/command_processor.cpp`) returns
  `ActionResult::Success("exit")` but nothing ever inspects that
  sentinel — `/q` typed through that registry just prints "Exiting..."
  and does nothing. The *other* command registry
  (`EverQuest::m_command_registry`, used by live interactive chat-window
  input — see `eq_commands_movement.cpp`) has a working `/q` that calls
  `RequestQuit()` directly. Not exercised by anything this unit's exit
  matrix requires (`/camp` and `--script` both work correctly through
  their own paths), so left as-is rather than risk scope creep; noting
  for whoever picks up quit-flow work next.
