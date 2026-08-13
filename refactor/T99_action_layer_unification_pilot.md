# T99 — Route commands through `ActionDispatcher`: the Group pilot

**Slot: whichever is free. Headless work, then a LIVE leg — this one needs the
server. eqemu lock normally.**

**Territory: `src/client/action/` (`command_processor.*`, `action_dispatcher.*`
if a method is genuinely missing), `src/client/eq_commands.cpp` (removals
only), `tests/`, `refactor/`, `/mnt/projects/fable/review/T99/`.**

Read T97's Review and `/mnt/projects/fable/review/T97/divergence_table.tsv`,
then T98's Review, then this plan.

**Will's direction: `ActionDispatcher` is to be the route commands take.** This
unit is the pilot that proves the migration recipe on one small family. It is
NOT the whole refactor — ~125 game-registry commands exist and this touches a
handful.

## The layering constraint — migrate, do not wrap

`Application` owns `ActionDispatcher`, `CommandProcessor` and `EverQuest`. The
dependency runs:

```
ActionDispatcher ──► IActionHandler ──► EqActionHandler ──► EverQuest
```

**`EverQuest` is below the action layer.** Making the game registry (which
lives inside `EverQuest`) call `ActionDispatcher` would create a cycle:
`EverQuest → ActionDispatcher → EqActionHandler → EverQuest`. **Do not do
that.** If you find yourself adding an `ActionDispatcher` include or pointer to
`eq.h` or any `eq_commands*.cpp`, stop — you have taken the wrong approach.

The correct shape is **migration upward**: the command's registration and logic
move *out* of the game registry and *into* `CommandProcessor`, which already
routes through `ActionDispatcher`.

## Port the better implementation, do not just pick a side

T97's divergence table is the input. Where the game-registry implementation is
better — which T97 found it usually is — **port that implementation** into the
action layer rather than reviving whatever `CommandProcessor` had.

T98 deliberately deleted the *worse* duplicates. This unit re-adds the *better*
ones in the right layer. That is the sequence working as intended, not churn.

## The pilot family: Group

Chosen because the action-layer methods already exist and are unreferenced —
T98 reported `acceptGroupInvite`, `declineGroupInvite`, `inviteTarget`,
`inviteToGroup` and `leaveGroup` as newly caller-free. Migrating Group gives
them callers instead of deleting them, and **fixes a real user-facing gap on
the way** (see below).

Scope: the game registry's Group-category commands — `invite`, `disband`,
`decline`, and the Social `follow_cmd` group-accept behaviour. Confirm the
exact membership from the registrations; do not trust this list.

## Atomicity — this is the part that will silently fail

The `game` registry is tried **before** `action`. So adding a command to
`CommandProcessor` while it still exists in the game registry changes nothing —
the migration would be invisible and you would be testing the old path.

**Each command must be added to `CommandProcessor` and removed from the game
registry in the same change.** Verify per command that dispatch now logs
`registry=action`, not `registry=game`. T82 established that
`Application::printCommandResult()` logs this.

## Fix the group-invite gap as part of the pilot

T97 and T98 established that accepting a pending group invite via bare
`/follow` is dead by every route: `game`'s Social `follow_cmd` is overwritten
by Movement's `follow`, `action`'s copy was shadowed and is now deleted, and
`eq_bridge.cpp`'s `GroupAcceptIntent` handler has **no live producer**.

`ActionDispatcher::acceptGroupInvite()` exists and is orphaned. Give it a
caller as part of this migration and **verify live that a pending invite can
actually be accepted.** Restoring a broken feature is the pilot's proof that
the migration delivers value rather than just moving code.

Decide and state where it belongs — reviving bare `/follow` conflicts with
Movement's `follow`, so an explicit spelling may be cleaner. Say what you chose
and why.

## Do NOT migrate renderer commands

Renderer commands must stay on the `RendererCommand` bridge queue and drain on
the render thread. Calling `processSlashCommand()` off the game thread runs GL
with no EGL context current — P10 hit exactly this and got deterministic
garbage screenshots rather than a crash, because Mesa fails silently. This is a
documented landing constraint in `docs/decisions.md`. None of the Group family
should be affected, but know the boundary before you touch anything else.

## Produce the recipe

The lasting deliverable is a **written migration recipe** for the remaining
~120 commands: the steps, the atomicity requirement, how to verify with
`registry=`, what to do when the action layer lacks a method, what must never
migrate, and how long the family took. The next unit should follow it rather
than rediscover it.

Include an honest estimate of the total effort implied.

## Tests

- Each migrated command dispatches through `CommandProcessor` and reaches the
  expected `ActionDispatcher` method.
- The group-accept path has a test.
- **Prove tests can fail in both directions** (the T95 standard): break the
  routing, show red; restore, show green. Report that you did it.

## Live leg — required

- Each migrated command works against the server, observed, not inferred from
  an ack. `outcome=applied` means a handler returned success and nothing more.
- **A real group invite, accepted.** Two accounts exist (`willeq_test`,
  `willeq_buddy`); T84 established two concurrent clients work and that
  concurrency is per-account.
- Confirm `registry=action` for every migrated command.

Session discipline: verify binary freshness against HEAD first; force daylight
`/say #set time 11 0`; pad scripts far longer than you think; `-d 4` plus
`--log-level=DEBUG` for wire lines; restore DB state and confirm by `SELECT`
**after logout**; clean teardown and delete the lock.

## What you may not do

- **No `ActionDispatcher` reference in `eq.h` or `eq_commands*.cpp`.** That is
  the cycle.
- Do not migrate renderer commands.
- Do not migrate beyond the Group family. **STOP and report** if the pilot
  reveals the recipe does not generalise — that is the most valuable thing this
  unit could find, and it must not be buried under a partial migration.
- Do not reorder the registries. Migration makes reordering unnecessary.

## Verification

Full build and suite (expect 2101 total plus your new tests; sole pre-existing
failure the documented ALSA `AudioManagerTest.PreloadCommonSounds`). Visual
gate — goldens must not move.

## Review

(executor fills: the confirmed Group membership; which implementation was
ported per command and why; proof of atomicity via `registry=action`; where
group-accept landed and the live evidence that an invite was accepted; the
migration recipe; the effort estimate for the remaining ~120; anything that
suggests the recipe will not generalise; deviations)
