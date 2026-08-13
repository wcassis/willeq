# T100 — Migrate the Chat family to the action layer

**Slot: slot1. Headless work, then a LIVE leg. eqemu lock normally — T101 is
running in parallel and also wants it, so do all headless work first and
contend for the lock at the end.**

**Territory: `src/client/action/`, `src/client/eq_commands*.cpp` (removals
only), `src/client/bridge/game_state_sync_bridge.*` if coverage is missing,
`tests/`, `refactor/`, `/mnt/projects/fable/review/T100/`.**

**Follow the migration recipe in T99's Review.** Read it first, in full,
along with T99's Review generally. This plan only adds what is specific to
Chat.

## Scope

The `game` registry's **Chat** category — 9 commands. Confirm exact membership
from the registrations; do not trust a count.

Will has directed the full migration, family by family. This is one batch.

## The recipe's own addendum — do this first

T99 learned it the hard way: **check `GameStateSyncBridge` coverage BEFORE the
live leg, not during it.** T99 found zero cases for Group events, meaning
`Application`'s `GameState` never saw a group change — the two-GameState trap.
Check whether Chat events are covered before you go live, and fix narrowly
following the file's existing pattern if not.

## Chat-specific cautions

- **Several of these were deleted by T98** (`say`, `ooc`, `auction`, `tell`,
  `reply`, `gsay`, `gu`, `emote`). You are porting the *game* implementation up
  into the action layer, not reviving what T98 removed. T97's
  `divergence_table.tsv` says which side was better — use it.
- **`shout` is a live `CommandProcessor` command already**, reachable via `/sh`
  because `game`'s shout alias is `sho`. Do not delete or duplicate it —
  reconcile it deliberately and say what you did.
- **`ConsoleInputHandler` has its own non-slash chat vocabulary** (`say`,
  `tell`, `shout`, `ooc`, `auction` — `console_input_handler.cpp:~373`) that
  builds `ChatMessage` structs directly and never reaches either registry.
  **Do not migrate or delete it**, but report whether it now diverges from the
  migrated path. A third chat implementation is exactly the kind of thing this
  programme exists to surface.
- Chat commands take free-text arguments including quotes and channel targets.
  **Preserve the argument parsing exactly** — `/tell Name multi word message`
  must still split identically. T98 deleted a `cmdTell`-quoting test; check
  whether the behaviour it covered still needs a test on the migrated path.

## Atomicity — the silent failure mode

`game` is tried before `action`. Add to `CommandProcessor` and remove from the
game registry **in the same change**, and verify per command that dispatch logs
`registry=action`. Use a control: leave one command unmigrated and confirm it
still logs `registry=game` in the same session, as T99 did with `/target`.

## Tests

Per T99 and the T95 standard: each migrated command reaches its expected
`ActionDispatcher` method, and **every new test proven able to fail in both
directions** — break the routing, show red, restore, show green. Report it.

## Live leg

Chat is directly observable, which makes this family unusually easy to verify
honestly: **send messages and confirm they arrive**, ideally with a second
client observing (T84 established two concurrent clients; concurrency is
per-account, `willeq_test` and `willeq_buddy`). Do not infer from an ack.

Session discipline: verify binary freshness against HEAD first; force daylight
`/say #set time 11 0`; pad scripts far longer than you think; `-d 4` plus
`--log-level=DEBUG`; restore DB state and confirm by `SELECT` **after logout**;
clean teardown, delete the lock.

## What you may not do

- **No `ActionDispatcher` reference in `eq.h` or `eq_commands*.cpp`** — that is
  the dependency cycle. Migration goes upward only.
- Do not migrate renderer commands (none should be in Chat, but know the line).
- Do not touch `ConsoleInputHandler`'s vocabulary — report divergence only.
- Do not migrate beyond Chat. **STOP and report if the recipe does not fit** —
  that is more valuable than a partial migration, and this batch exists partly
  to test the recipe on a second shape.

## Verification

Full build and suite (expect 2121 total plus new tests; sole pre-existing
failure the documented ALSA `AudioManagerTest.PreloadCommonSounds`). Visual
gate — goldens must not move.

## Review

(executor fills: confirmed membership; per-command source ported and why;
`GameStateSyncBridge` coverage before/after; how `shout` was reconciled;
whether `ConsoleInputHandler`'s vocabulary now diverges; atomicity proof with
the control; live evidence messages arrived; recipe refinements; actual hours
vs T99's estimate for a family this size; deviations)
