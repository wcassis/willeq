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

(executor fills: the helper's interface and why; the two shapes demonstrated
with real invocations and output, including the deadline path; the protocol
edit; whether the rule was moved and the reasoning either way; deviations)

## Board

Say plainly that this does not *guarantee* compliance — it removes one
friction. If the stall recurs after this, the next lever is mechanical
(a hook) rather than editorial, and that is worth recording now so the next
person does not reach for a third round of rewording.
