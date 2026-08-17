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

Executed in `/mnt/projects/fable/wt/slot2`, task branch
`task/T132-guild-family`, off `fable` tip `350144a4`. Full evidence:
`/mnt/projects/fable/review/T132/` (README + three filtered live-session
logs — a throwaway single-client run plus the full two-client session).

### The seven commands: dispatcher/handler mapping

All net-new — `action::CommandProcessor` had zero guild registrations
before this unit (T98 emptied the category and there was nothing
underneath it to revive). Each is a thin wrapper over the pre-existing
`EverQuest::SendGuild*()` sender in `src/client/eq_guild.cpp`:

| command (aliases) | `CommandProcessor` | `ActionDispatcher` | `EqActionHandler` | `EverQuest` sender |
|---|---|---|---|---|
| `guildinvite` (`ginvite`) | `cmdGuildInvite` | `guildInvite(name)` | `guildInvite(name)` | `SendGuildInvite(name)` |
| `guildaccept` (`acceptguild`) | `cmdGuildAccept` | `guildAcceptInvite()` | `guildAcceptInvite()` | `SendGuildInviteAccept(true)` |
| `guilddecline` (`declineguild`) | `cmdGuildDecline` | `guildDeclineInvite()` | `guildDeclineInvite()` | `SendGuildInviteAccept(false)` |
| `guildremove` (`gremove`, `gkick`) | `cmdGuildRemove` | `guildRemove(name)` | `guildRemove(name)` | `SendGuildRemove(name)` |
| `guilddemote` (`gdemote`) | `cmdGuildDemote` | `guildDemote(name)` | `guildDemote(name)` | `SendGuildDemote(name)` |
| `guildleader` (`gleader`) | `cmdGuildLeader` | `guildMakeLeader(name)` | `guildMakeLeader(name)` | `SendGuildLeader(name)` |
| `guildmotd` (`gmotd`) | `cmdGuildMotd` | `requestGuildMOTD()` / `setGuildMOTD(motd)` | same two methods | `SendGetGuildMOTD()` / `SendSetGuildMOTD(motd)` |

Files touched: `include/client/action/action_dispatcher.h` (+8
`IActionHandler` virtuals, +8 `ActionDispatcher` methods),
`src/client/action/action_dispatcher.cpp` (+8 implementations, all
`checkZoneConnection()` + empty-name guard where `game` had one, mirroring
`setSurname`'s established shape), `include/client/eq_action_handler.h` /
`src/client/eq_action_handler.cpp` (+8 pure-routing overrides),
`include/client/action/command_processor.h` /
`src/client/action/command_processor.cpp` (+7 `cmd*` declarations,
registrations, implementations), `src/client/eq_commands.cpp` (Guild block
removed), `tests/action_test_fakes.h` (`RecordingActionHandler` +8
recordings), `tests/test_command_processor.cpp` (+22 cases).

### Atomicity

The Guild block's seven registrations were removed from
`eq_commands.cpp`'s `RegisterSocialCommands()` in the **same commit** as
the seven new `CommandProcessor::registerCommand()` calls — no window
where both registries held a command. Confirmed structurally (single
commit, see below) and live: every migrated command shows `registry=action`
in the same session `/loc` (untouched, still `game`-registered) shows
`registry=game`.

### Live evidence

`review/T132/`, both clients (`willeq_test`/`Testmelee`,
`willeq_buddy`/`Testbuddy`, both already parked in `arena` from prior
units — no travel needed):

```
CMD outcome=applied registry=action recognized=true cmd="/guildmotd"
CMD outcome=applied registry=action recognized=true cmd="/guildmotd Welcome to TestGuild!"
CMD outcome=applied registry=action recognized=true cmd="/guildinvite Testbuddy"
CMD outcome=applied registry=action recognized=true cmd="/guilddemote Testbuddy"
CMD outcome=applied registry=action recognized=true cmd="/guildleader Testbuddy"
CMD outcome=applied registry=action recognized=true cmd="/guilddecline"
CMD outcome=applied registry=action recognized=true cmd="/guildaccept"
CMD outcome=applied registry=action recognized=true cmd="/guildremove Testmelee"
CMD outcome=applied registry=game   recognized=true cmd="/loc"
```

All seven commands dispatch through `registry=action`; the control
(`/loc`, untouched Movement-category command) still resolves `registry=game`
in the same session, proving dispatch order itself wasn't disturbed.

### The setup question, resolved — and a bigger finding underneath it

The plan asked whether forming a guild needs GM action, and to stop and
report honestly if the live leg couldn't be completed unaided. Checked: a
GM-status test account is available (`willeq_test`, account status 200,
above the `Steward` (10) gate `zone/gm_commands/guild.cpp`'s `#guild`
command requires) and `#guild create` is reachable — **so the live leg
does not need Will.**

But a throwaway single-client run (done *before* building the full
two-client session, per the protocol's "verify the mechanism cheaply
first" discipline) found something GM action can't fix: `/guildinvite`
dispatched `registry=action` correctly but printed "You are not in a
guild." and sent no packet. Traced to source:
**`EverQuest::m_guild_id` is declared, guarded on by every
`SendGuild*()` sender, and never assigned anywhere in the codebase** — the
zone-in `PlayerProfile` packet parses a guild id into a *different* field
(`Entity::guild_id`, self_entity's copy, used for rendering) that is never
copied into `EverQuest::m_guild_id`. So `/guildinvite`,
`/guildremove`, `/guilddemote`, `/guildleader`, and `/guildmotd`'s set-path
are permanently dead on the wire regardless of actual server-side guild
state, and `/guildaccept`/`/guilddecline` can never see a real pending
invite either, because the only way one arrives (a working
`/guildinvite`) is blocked by the same dead field. Only
`SendGetGuildMOTD()` (unguarded) got a real server round trip — confirmed
live, a real (empty, since Testmelee has no guild) `OP_GetGuildMOTDReply`.

**Not a T132 regression**: `game`'s original handlers called these exact
same `Send*()` methods with the exact same guard, so the migration
preserves this dead-on-arrival behavior identically, as the plan requires
("behaviour must be identical"). Full detail and the fix's shape (out of
this unit's territory — it's a zone-in/connection-state change in
`eq_guild.cpp`/`eq.cpp`, not an `action/` change) in
`refactor/deferred.md`'s T132 entry.

**What this means for "what was left unverified"**: registry-routing
(the plan's actual "what to establish" ask) is fully verified live for all
seven commands. A genuine end-to-end network effect is verified live only
for `/guildmotd`'s get-path; the other six are verified live as far as
"reaches the correct sender with the correct guard behavior," which is as
far as `game`'s original implementation could ever go either.

### Preserved from `game`'s handlers

- Empty-name usage messages ported verbatim: `/guildinvite`,
  `/guildremove`, `/guilddemote`, `/guildleader` all reject with the exact
  same `"Usage: /guildX <name>"` string `game` used, checked at the
  `CommandProcessor` layer (matching `cmdSurname`'s established shape) and
  again at the `ActionDispatcher` layer (belt-and-suspenders, also
  matching `setSurname`'s precedent — not new to this unit).
- `guildmotd`'s get-vs-set branch on `args.empty()` ported verbatim
  (`cmdGuildMotd`).
- All guard/argument logic (`m_guild_id == 0`,
  `m_has_pending_guild_invite`) lives inside the pre-existing `Send*()`
  senders themselves and was untouched — `EqActionHandler`'s methods are
  pure routing, identical to how T100's Chat family and T102's `combat`
  toggle wrappers were shaped once the underlying method already existed.
  Nothing was lost; there was no dispatcher-shape decision to make because
  game's own handlers did no argument/guard work the dispatcher would
  naturally omit, beyond the usage-string checks already ported above.

### Measured wall-clock, and what it implies

**~50 minutes**, start (reading the plan + protocol + T99-102 Reviews) to
this Review being written, inclusive of: reading four prior units' Reviews,
tracing all seven commands + the network layer, writing all six touched
files' changes, a full x86 build + 2311-test ctest run + GLES2 build +
visual gate + ARM cross-compile + file-size check, standing up eqemu,
resolving the live-leg setup question (including the throwaway run that
found the `m_guild_id` bug), the full two-client live session, and writing
this Review plus `review/T132/` and the `deferred.md` entry.

This is dramatically below the programme's 40-65h estimate for the
remaining families — but that estimate was explicitly derived from
reconcile-and-delete units (T99: ~1 session including a
`GameStateSyncBridge` fix; T100: ~2.5-3h; T102: a combined two-family
session). **The Guild family had zero reconciliation work**: no
divergence table to consult, no "which implementation is better" judgment,
no dead duplicate to delete — every method was new plumbing over an
already-correct, already-tested network layer. The real cost for a
net-new family without a `GameStateSyncBridge` gap (this family didn't
need one — no domain in `state::GameState` reads guild data, matching
T100's Chat finding that most families won't hit that class of cost) is
almost entirely mechanical: same eight-file shape, same registration
pattern, same test density, repeated seven times. ~50 minutes end-to-end,
including full verification and a live leg, is the number the next
net-new family should be planned against — call it **well under 1 hour
per net-new family with no `GameStateSyncBridge`/state-domain gap**, not
the 40-65h figure carried over from reconcile-and-delete work.

### Deviations

- The live leg could not produce a genuine end-to-end network effect for
  six of the seven commands, for the reason above (a real, pre-existing,
  out-of-territory client bug, not a live-leg execution failure). Reported
  per the plan's "say so plainly, don't paper over" instruction rather than
  treating `registry=action` alone as sufficient without flagging the gap.
- The originally-planned `#guild create` GM-command guild-formation step
  was run once as part of confirming the `m_guild_id` finding is real
  server-state-independent, then not repeated for the full two-client
  session — forming a guild would not have changed any of the seven
  commands' observable live behavior, so repeating it would have spent
  session time without adding evidence.

## Board

**Net-new vs. reconcile-and-delete now has a real cost comparison**:
T99/T100/T102 (reconcile-and-delete, with T99 also absorbing a
`GameStateSyncBridge` fix) measured ~1 session to ~3h each. T132
(net-new, no `GameStateSyncBridge` gap) measured **~50 minutes**. The
driver isn't "net-new is inherently cheaper" — it's that reconcile-and-delete
units spend real time on judgment calls (T97's divergence table, "which
implementation wins") that a net-new family with an already-correct network
layer simply doesn't have. A net-new family that *does* need a new
`GameStateSyncBridge` case (T99's Group finding) should be estimated closer
to T99's number, not T132's — that cost is real and separable, and the plan
correctly calls it out as its own unit when hit.

**Remaining families**: re-derive membership from `eq_commands*.cpp`
categories before planning each one (per T99's now-three-times-confirmed
"don't trust the prose list" recipe), and classify each as net-new
(check whether `ActionDispatcher`/`EqActionHandler` already have live,
reachable methods for it — if not, it's net-new like Guild) or
reconcile-and-delete (both a `game` and a shadowed `action` implementation
exist, per T97's audit) before estimating. Net-new families with no
`GameStateSyncBridge` gap: budget close to T132's ~50 minutes. Net-new
families that DO need a new `GameStateSyncBridge` case, or any family that
turns out to be reconcile-and-delete: budget closer to T99-T102's
~1-3h-per-family range.
