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

(executor fills in: per-bug backtrace/thread-dump + root cause + fix,
the exit matrix results, deviations)
