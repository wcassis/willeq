# T132 — Migrate the Guild family (7 commands) to `action::CommandProcessor`

**Slot: whichever is free. x86. eqemu lock required (live leg). No Pi.**

**Territory: `src/client/eq_commands.cpp` (Guild block), `src/client/action/`,
`include/client/action/`, `src/client/eq_action_handler.cpp`, `tests/`,
`refactor/`, `/mnt/projects/fable/review/T132/`.**

Read T102's Review first (the two-family batching precedent and its live-leg
method), then T99's (the pilot recipe) and T100's.

## The remaining families are NOT the same job as T99–T102 — read this first

T99–T102 were **reconcile-and-delete**: both a `game` and an `action`
implementation existed, T97 rated their divergence, and the unit picked the
better one, moved the command, and deleted the loser.

**That is not the situation for what remains.** Checked while scoping, for the
Guild family:

- `action::CommandProcessor` registers **no** guild-management command. (The
  only match is `gu`/`guildsay`, which is *guild chat* and already migrated
  with the Chat family — do not confuse them.)
- `ActionDispatcher` has a `Guild` **category enum value** but **no guild
  methods**.
- `EqActionHandler` has **no** guild methods.

T98 deleted the shadowed action code, and for these families there was nothing
underneath it. **So migration here means building the action-side stack, not
reconciling two existing ones.**

**The mitigating fact, and it matters**: the network layer already exists —
`EverQuest::SendGuildInvite`, `GuildInviteAccept`, `GuildRemove`,
`GuildDemote`, `GuildLeader`, `GuildMOTD` are all present and are what the
current `game` handlers call. So this is **plumbing, not protocol**: per
command, an `ActionDispatcher` interface method + implementation, an
`EqActionHandler` implementation calling the existing `Send*`, a
`CommandProcessor` registration, and removal from `eq_commands.cpp`.

**Say in your Review how long this actually took**, because the programme
estimate (40–65h for the remaining families) was derived from
reconcile-and-delete units and is now on a different footing. A measured
number from a net-new family is what the next batch should be planned against.

## The family

Seven commands, all currently in `eq_commands.cpp`'s Guild block:
`guildinvite` (alias `ginvite`), `guildaccept`, `guilddecline`, `guildremove`,
`guilddemote`, `guildleader`, `guildmotd`.

Map each to its existing `EverQuest::Send*` and keep behaviour **identical** —
this is a migration, not a redesign. If a `game` handler does something the
obvious dispatcher shape would lose (argument parsing, a target fallback, a
guard), preserve it and note it.

## Atomicity — the rule this programme has held to

**Remove from `eq_commands.cpp` in the same commit that adds the
`CommandProcessor` registration.** A window where both registries hold the same
command re-creates exactly the shadowing bug T97 catalogued (game is tried
before action, so a stale `game` entry silently wins).

Verify with `Application::printCommandResult()`'s `registry=` output —
`registry=action` for all seven, and keep one **unmigrated** command as a
control showing `registry=game` in the same session, as T100 did.

## The live leg — and a setup question to resolve early

Guild invite/accept/decline is inherently **two-client**: one player invites,
another accepts or declines. T102 established the efficient pattern — one
session driving two clients through a written script.

**But forming a guild in the first place may need GM action or a pre-existing
guild.** Establish this early. If the live leg cannot be completed without
Will acting in-game, **say so plainly and stop at that boundary** — report
which commands you verified live and which you could not, rather than
skipping the leg silently or asserting behaviour you did not observe. A
partial live leg, honestly bounded, is acceptable; an implied one is not.

## What to establish

- All seven dispatch via `registry=action`, live, with the control.
- Behaviour identical to the `game` handlers, including argument handling.
- Anything the `game` handlers did that the action path would otherwise drop.
- **Measured wall-clock for a net-new family**, for re-estimating the rest.

## Fixing beyond the migration

**STOP and report** if a command needs a new `GameStateSyncBridge` case or a
new state domain — T103 showed that class of cost is real, and it is its own
unit.

**Do not paper over**: no command left registered in both places, no
`registry=` claim you did not observe in a log.

## Verification

- x86 suite (current total **2289**; sole failure the documented ALSA
  `AudioManagerTest.PreloadCommonSounds`) plus ARM cross-compile.
- **Tests are required** — the programme's established shape: registration and
  category assertions for all seven, plus per-command
  dispatch-reaches-the-right-`ActionDispatcher`-method assertions. T102 added
  70 cases for 23 commands; match that density.
- **Visual gate goldens must not move.**
- Live evidence in `review/T132/`: the session log showing `registry=action`
  for each command and `registry=game` for the control.

## Review

(executor fills: the seven commands with their dispatcher/handler mapping; the
atomic-commit confirmation; the live log excerpts with `registry=` for all
seven plus the control; anything preserved from the `game` handlers that the
naive shape would have lost; **measured wall-clock**, and what it implies for
the remaining families' estimate; whether the live leg needed Will and what
was left unverified; deviations)

## Board

State the revised per-family cost for **net-new** families against T102's
reconcile-and-delete numbers, and re-estimate the remainder on that basis.
List which families remain and which are net-new versus reconcile — that
distinction now drives the whole programme's shape.
