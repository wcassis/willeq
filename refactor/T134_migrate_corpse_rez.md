# T134 — Migrate the Corpse and Resurrection families (6 commands)

**Slot: whichever is free. x86. eqemu lock required (live leg). No Pi.**

**DISPATCH AFTER T133 RELEASES THE EQEMU LOCK.** T133 (`m_guild_id`) holds it
and its live leg cannot be shared.

**Territory: `src/client/eq_commands.cpp` (Corpse and Rez blocks),
`src/client/action/`, `include/client/action/`,
`src/client/eq_action_handler.{h,cpp}`, `tests/`, `refactor/`,
`/mnt/projects/fable/review/T134/`.**

**Stay out of T133's files** — `src/client/eq_entities.cpp`,
`src/client/eq_guild.cpp`, `include/client/eq.h`. You should not need any of
them; if you do, stop and say why.

Read T132's Review first (the net-new migration recipe and its ~50-minute
measured cost), then T102's (the batching precedent and two-client live-leg
method).

## The batch, and why these two families go together

Six commands, all currently `game`-only:

**Corpse**: `corpsedrag`, `corpsedrop`, `consent`, `deny`
**Resurrection**: `rezaccept`, `rezdecline`

They batch because they share **one coherent live scenario**: die → get a
corpse → `consent` another character → `corpsedrag`/`corpsedrop` → take a
resurrection → `rezaccept`/`rezdecline`. That is a single two-client session
covering all six, which is exactly the efficiency T102 demonstrated.

## Same shape as T132 — net-new action stack, existing senders

Checked while scoping:

- `action::CommandProcessor` registers **none** of the six.
- `ActionDispatcher` has **no** methods for them.
- But every sender exists: `SendCorpseDrag`, `SendCorpseDrop`, `SendConsent`,
  `SendConsentDeny`, `SendRezzAnswer` (`src/client/eq_player_info.cpp`).

So this is plumbing, as T132 was: per command an `ActionDispatcher` interface
method + implementation, an `EqActionHandler` implementation, a
`CommandProcessor` registration, and removal from `eq_commands.cpp`.

Note `rezaccept` and `rezdecline` both route to `SendRezzAnswer(bool)` — one
dispatcher method taking the flag, or two thin ones, your call; say which and
why.

## Unlike Guild, this family is ALIVE — verified while scoping

T132 found the guild commands were dead on the wire because `m_guild_id` is
never assigned. **Do not assume that pattern repeats here.** The guard state
for these is genuinely populated:

- `m_has_pending_rezz` — set `true` at `eq_player_info.cpp:954` (incoming rez
  packet), cleared at `:976`/`:1010`. `SendRezzAnswer` guards on it.
- `m_is_dragging_corpse` — set `true` at `:1204`, cleared at `:1231`.
  `SendCorpseDrop` guards on it.
- `consent`/`deny` guard only on `m_zone_connection && m_zone_connected`.

**So a real wire effect is expected here**, and the live leg should show it —
unlike T132, where six of seven could only be verified at the dispatch layer.
If a command turns out *not* to reach the wire, that is a finding, not an
acceptable outcome to shrug at.

## Preserve the guards

Those guards are correct — you cannot drop a corpse you are not dragging, or
answer a resurrection you were not offered. **Carry them through unchanged.**
If a guard currently lives in the `game` handler rather than the sender, make
sure the action path still applies it, and say so.

## Atomicity — the programme's standing rule

**Remove from `eq_commands.cpp` in the same commit that adds the
`CommandProcessor` registration.** A window where both registries hold a
command re-creates T97's shadowing bug — `game` is tried before `action`, so a
stale entry silently wins.

Verify with `Application::printCommandResult()`'s `registry=` output:
`registry=action` for all six, plus one **unmigrated** command as a
`registry=game` control in the same session.

## The live leg — resolve the resurrection setup early

Death and corpse handling are straightforward with a GM account
(`willeq_test` is status 200 per T132). **The risky one is getting a *pending
resurrection***: something has to cast rez on the corpse. Establish early
whether a GM command or a second character can produce one.

**If `rezaccept`/`rezdecline` cannot be driven live, say so and bound it** —
report which of the six you verified on the wire and which only at the
dispatch layer. T132 set that precedent and it held up. A partial live leg,
honestly bounded, is fine; an implied one is not.

## Fixing beyond the migration

**STOP and report** if a command needs a new `GameStateSyncBridge` case or a
new state domain — T103 proved that cost is real and it is its own unit.

**Do not paper over**: no command left registered in both places, no
`registry=` or wire-effect claim you did not observe in a log.

## Verification

- x86 suite (current total **2311** plus whatever T133 adds; sole failure the
  documented ALSA `AudioManagerTest.PreloadCommonSounds`) plus ARM
  cross-compile.
- **Tests at the programme's density** — T132 added 22 cases for 7 commands.
  Registration and category assertions for all six, plus per-command
  dispatch-reaches-the-right-method assertions, including the
  `rezaccept`/`rezdecline` flag distinction.
- **Visual gate goldens must not move.**
- Live evidence in `review/T134/`: the session log showing `registry=action`
  for each command, `registry=game` for the control, **and the wire effects**.

## Review

(executor fills: the six commands with their dispatcher/handler mapping and
the `SendRezzAnswer` flag decision; atomic-commit confirmation; where each
guard ended up; live log excerpts showing `registry=` and the wire effect for
each; how the pending resurrection was produced, or why it could not be;
**measured wall-clock** against T132's ~50 minutes; deviations)

## Board

State which of the six are verified **end-to-end on the wire** versus only at
the dispatch layer — T132's experience makes that distinction the one that
matters. Then give the running programme position: commands migrated, families
left (**Merchant**: `vendor`, `vendorlist`, `buy`, `sell`, `equip`;
**Utility**: `split`, `save`, `bandage`, `rewind`, `bug`, `petition`), and a
revised estimate using this unit's and T132's measured costs. Flag whether
Merchant looks likely to need the state-domain work T103 warned about, since
that is the one remaining family that could break the pattern.
