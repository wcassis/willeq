# T129 — Give the anti-stall rule a tool, not more words

**Slot: whichever is free. No Pi. No eqemu lock. Small unit.**

**Territory: `scripts/`, `refactor/EXECUTOR_PROTOCOL.md`, `tests/` if a test
fits, `/mnt/projects/fable/review/T129/`.**

## The problem is not a missing rule

`EXECUTOR_PROTOCOL.md`'s "Hard rules" already says, verbatim:

> NEVER end a turn waiting on a background task or monitor — poll output files
> in bounded foreground loops inside a single Bash call (e.g. 60 iterations x
> 10s). **This failure mode has stalled 5+ units; it is forbidden.**

**It is stated, it is marked forbidden, and agents violate it anyway.** T128's
executor stalled **three times in one unit** — on a build, then on a Pi batch,
then on a monitor it had spawned itself to avoid polling. That took the count
past 5+ to roughly 8.

**Do not "fix" this by rewording the rule or making it bolder.** That has been
tried implicitly for five units. This unit's premise is that the rule fails
because **it asks every agent to invent a polling loop from scratch, under time
pressure, deep in a long session** — and there is no helper in `scripts/` to
invoke instead. A rule without a tool is a rule that gets skipped.

## Build the tool

A small, dependency-free `scripts/wait_for.sh` that an agent can invoke as
**one foreground Bash call** in place of writing a loop.

**Hard constraint that shapes the design**: the Bash tool caps a single call at
**600000 ms (600 s)**. So the helper must be **bounded well under that and
re-invokable** — it reports state and exits, and the agent calls it again if
the work is still running. A helper that blocks for an hour is unusable here.

Suggested shape (adjust if you find better while implementing — say why):

```
scripts/wait_for.sh --exit-file <path> [--interval 10] [--deadline 540] [--label NAME]
scripts/wait_for.sh --file <path> --contains <text> [...]
```

- Polls at `--interval`, gives up at `--deadline` (default comfortably under
  600 s).
- **Exit 0** when the condition is met, **exit 2** on deadline (distinct from
  a real error so the agent can tell "still running" from "broken"), non-zero
  otherwise.
- On exit, print what it saw: elapsed time, current file size or tail, and for
  `--exit-file` the code it found. **The agent must be able to act on the
  output without a second command.**
- No dependencies beyond coreutils. Must work over the NFS paths this project
  uses for Pi logs.

Cover the two shapes that actually recur here: **waiting for a `.exit` file**
(the Pi batch pattern) and **waiting for a marker line in a growing log**
(the build/ctest pattern).

## Then make the protocol point at it

Edit `EXECUTOR_PROTOCOL.md` so the rule names the tool instead of describing a
pattern to reinvent — a concrete invocation an agent can copy. Keep the
existing prohibition; add the mechanism.

Consider whether the rule is in the right place. It currently sits as the
second bullet of "Hard rules", among shell-syntax items. **If you move or
promote it, say why**; a change that only shuffles text is not worth making,
so if you conclude placement is not the problem, leave it and say so.

## Explicitly out of scope

- Do not touch the hook system or try to enforce this mechanically.
- Do not rewrite unrelated parts of the protocol.
- No behaviour changes to `src/`.

## Verification

- **Actually exercise the helper**: both shapes, against a real background
  process — success, and the deadline path. Paste the invocations and output
  into the Review. A helper that has never been run is not a deliverable.
- Confirm it works against a path under `/mnt/projects/fable/review/` (NFS),
  since that is where Pi evidence lands.
- x86 suite (current total **2284**; sole failure the documented ALSA
  `AudioManagerTest.PreloadCommonSounds`) — you are not touching `src/`, so
  this is a sanity check only.
- `bash -n` at minimum; `shellcheck` if available.

## Review

### Interface and why

`scripts/wait_for.sh` (bash, coreutils only — `stat`, `tail`, `grep`, `sleep`,
`date`; no new dependency):

```
scripts/wait_for.sh --exit-file <path> [--interval 10] [--deadline 240] [--label NAME]
scripts/wait_for.sh --file <path> --contains <text> [--interval 10] [--deadline 240] [--label NAME]
```

- **Default deadline 240s, hard-rejected above 540s** (exit 1, before doing
  any work) — the Bash tool's single-call cap is 600000ms/600s; 540s leaves
  ~60s of slop for process startup/teardown so a caller can never construct
  an invocation that silently exceeds the cap. This directly encodes the
  plan's "bounded well under 600s and re-invokable" constraint rather than
  leaving it to each caller's judgment.
- **Exit 0** = condition met. **Exit 2** = deadline reached, condition not
  met — distinct from a real error, so an agent can branch on it
  ("still running, call me again") without parsing text. **Exit 1** = usage
  error (bad/missing args, or a deadline too close to the cap). **Exit 3**
  = target unusable in a way that is not "still running" — currently, the
  target's parent directory doesn't exist at all, which is a caller mistake
  (typo, batch never launched), not a timing issue; distinguishing it from
  exit 2 stops an agent from burning repeated deadlines against a path that
  was never going to appear.
- Every exit (all four codes) prints: label, mode, target, elapsed time,
  file size or "absent", and (if present) a 10-line tail — so the agent can
  decide what to do next from this call's output alone, no second command.
  `--exit-file` additionally prints the code found inside the file on MET.
- Two shapes, chosen because they're the two patterns that actually recur
  in this project's plans/reviews: a Pi batch script writing a `.exit` file
  with its exit code, and a build/ctest log an agent watches for a
  completion marker/line.
- No mechanism for actually killing/managing the watched process — the plan
  scoped this as a poller, not a process supervisor, and adding kill
  semantics would need to know how the caller launched the job (PID, job
  control, ssh session, …), which varies per shape more than the polling
  logic does.

### Both shapes demonstrated, including the deadline path

All four demo runs below were executed for real against real background
`sleep`+write processes, over `/mnt/projects/fable/review/T129/` (confirmed
NFS via `df -T`: `10.0.30.10:/volume2/projects` on `/mnt/projects`). Full
transcripts and file evidence: `/mnt/projects/fable/review/T129/` (see its
README).

**Exit-file shape, success** (`batch1.exit`, background writer sleeps 8s):
```
$ bash scripts/wait_for.sh --exit-file .../batch1.exit --interval 3 --deadline 30 --label batch1-early-poll
wait_for[batch1-early-poll]: MET (exit-file appeared) after 6s (interval=3s deadline=30s)
  mode=exit-file target=/mnt/projects/fable/review/T129/batch1.exit
  file: present, size=2 bytes
  tail:
    0
  exit code found inside: 0
exit code: 0
```

**Exit-file shape, deadline reached then re-invoked** (`batch2.exit`,
background writer sleeps 60s — deliberately longer than the first
deadline):
```
$ bash scripts/wait_for.sh --exit-file .../batch2.exit --interval 5 --deadline 15 --label batch2-deadline
wait_for[batch2-deadline]: DEADLINE (condition not met — probably still running; re-invoke this same command) after 15s (interval=5s deadline=15s)
  mode=exit-file target=/mnt/projects/fable/review/T129/batch2.exit
  file: absent
exit code: 2

$ bash scripts/wait_for.sh --exit-file .../batch2.exit --interval 5 --deadline 60 --label batch2-reinvoke
wait_for[batch2-reinvoke]: MET (exit-file appeared) after 40s (interval=5s deadline=60s)
  ...
  exit code found inside: 0
exit code: 0
```

**Marker-line shape, success** (`build.log`, writer appends 5 lines then
`BUILD_DONE` over ~10s):
```
$ bash scripts/wait_for.sh --file .../build.log --contains 'BUILD_DONE' --interval 3 --deadline 30 --label build-marker
wait_for[build-marker]: MET (marker 'BUILD_DONE' found) after 9s (interval=3s deadline=30s)
  ...
exit code: 0
```

**Marker-line shape, deadline then re-invoked** (`build2.log`, writer
appends 10 lines over ~30s then the marker):
```
$ bash scripts/wait_for.sh --file .../build2.log --contains 'BUILD_DONE' --interval 4 --deadline 12 --label build2-deadline
wait_for[build2-deadline]: DEADLINE (condition not met — probably still running; re-invoke this same command) after 12s (interval=4s deadline=12s)
  file: present, size=114 bytes
  tail: (6 lines so far)
exit code: 2

$ bash scripts/wait_for.sh --file .../build2.log --contains 'BUILD_DONE' --interval 5 --deadline 30 --label build2-drain
wait_for[build2-drain]: MET (marker 'BUILD_DONE' found) after 5s (interval=5s deadline=30s)
exit code: 0
```

**Usage/error paths also exercised**: missing args → exit 1 with usage;
nonexistent parent directory → exit 3 ("FAIL directory does not exist");
`--deadline 595` → exit 1, rejected before doing any work (too close to the
600s cap).

**Dogfood** (this unit's own required verification, done through the helper
per the "working style" instruction instead of a hand-rolled loop): kicked
off `cmake --build` and `ctest` as background processes each writing a
completion marker to a log, then polled each with `wait_for.sh --file
... --contains <MARKER>`. Build was already up to date (MET after 0s).
ctest took 260s wall-clock, well inside the 500s deadline used, MET with
exit 0. One incidental self-inflicted stall during this: the *outer* Bash
tool call wrapping the ctest poll used the default 120s timeout (my
mistake, not the helper's design) and got moved to background by the
harness; I did not wait on it — I re-invoked `wait_for.sh` in a fresh
foreground call with an explicit 540000ms timeout and got the result
directly, which is itself the correct anti-stall behavior the tool is
meant to support. See `/mnt/projects/fable/review/T129/README.md` for the
full transcript including this recovery.

### Protocol edit

`refactor/EXECUTOR_PROTOCOL.md`'s Hard Rules bullet 2 (the "run long
commands foreground" / "never end a turn waiting" bullet) now names
`scripts/wait_for.sh` directly with a copy-pasteable invocation block and
the exit-code contract, instead of describing "poll output files in
bounded foreground loops" as a pattern for the agent to build itself.

### Was the rule moved?

**No — left in place, deliberately.** It considered this: the bullet is
already the *second* item in Hard Rules (right after shell discipline),
already carries the strongest language in the document ("This failure mode
has stalled 5+ units; it is forbidden"), and T128 read and violated it
anyway, three times, in one unit, including inventing its own
purpose-built monitor specifically to route around it. That is direct
evidence that position-in-the-list and typographic emphasis were never the
limiting factor — an agent that ignores a bolded, near-top, explicitly
"stalled 5+ units" rule is not going to be caught by moving it to bullet 1.
Promoting or restructuring it now would be the same class of fix
("reword/re-emphasize") the plan explicitly says the last five units
already tried and that this unit was told not to repeat. The actual
change this unit makes is orthogonal to position: giving the existing
bullet, in its existing slot, something concrete to *point at*.

### Deviations from the plan

- None material. The suggested shape in the plan (`--exit-file`,
  `--file --contains`, `--interval`, `--deadline`, `--label`) was
  implemented as-is. Added on top of the plan's suggestion: an explicit
  exit-3 code for "target unusable" (distinct from exit-2 "still running")
  and a hard reject of `--deadline` values within 60s of the Bash-call cap
  — both are refinements of the plan's own stated requirements ("distinct
  exit codes", "bounded well under 600s"), not scope changes.
- `tests/` was named as in-territory "if a test fits"; no automated test
  was added. This is a bash script whose entire behavior is timing-based
  interaction with the filesystem and background processes — the real
  verification is the live demonstration above (which a mocked/fast-clock
  unit test would not actually exercise any more convincingly than this
  transcript does), not a `ctest` entry. Flagging this as a deliberate
  omission rather than an oversight.

### Verification

- `bash -n scripts/wait_for.sh` — OK.
- `shellcheck` — not available in this environment (checked, absent);
  noted rather than silently skipped.
- x86 build (`cmake --build build`) — already up to date, no changes to
  `src/`.
- x86 ctest — **2284 total, 2283 passed, 1 failed
  (`AudioManagerTest.PreloadCommonSounds`)** — matches the documented ALSA
  host-quirk baseline exactly. Full log: `/mnt/projects/fable/review/T129/x86_ctest.log`.
- No `src/` changes; no Pi; no eqemu.

## Board

This gives the anti-stall rule a tool; it does **not** guarantee
compliance. Nothing stops a future agent from writing an ad hoc loop again,
or from spawning a background Task/Monitor and waiting on it regardless (as
T128 did on its third stall) — `wait_for.sh` only makes the compliant path
cheaper than inventing one. **If the stall recurs after this unit, the next
lever is mechanical** (a hook that can actually flag/block a disallowed
tool-call shape, e.g. a bash call that starts `sleep` past some threshold,
or ending a turn with an unresolved background task id outstanding) **— not
a sixth round of protocol rewording.** Recorded here now, before a third
failure forces the point, per the plan's Board instruction.

## Board

Say plainly that this does not *guarantee* compliance — it removes one
friction. If the stall recurs after this, the next lever is mechanical
(a hook) rather than editorial, and that is worth recording now so the next
person does not reach for a third round of rewording.
