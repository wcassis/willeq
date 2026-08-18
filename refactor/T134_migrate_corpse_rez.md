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

Executed in `/mnt/projects/fable/wt/slot2`, task branch
`task/T134-corpse-rez`, off `fable` tip `3e87b2ca`. Full evidence:
`/mnt/projects/fable/review/T134/` (README + three filtered live-session
logs + the eqemu zone server's own log, including a hex dump proving the
struct bug below).

### The six commands: dispatcher/handler mapping

All net-new — `action::CommandProcessor` had zero registrations for either
family before this unit, same shape as Guild (T132). Each is a thin wrapper
over the pre-existing `EverQuest::Send*()` sender in `eq_player_info.cpp`:

| command (aliases) | `CommandProcessor` | `ActionDispatcher` | `EqActionHandler` | `EverQuest` sender |
|---|---|---|---|---|
| `corpsedrag` (`drag`) | `cmdCorpseDrag` | `corpseDrag(name)` | `corpseDrag(name)` | `SendCorpseDrag(name)` |
| `corpsedrop` (`drop`) | `cmdCorpseDrop` | `corpseDrop()` | `corpseDrop()` | `SendCorpseDrop()` |
| `consent` | `cmdConsent` | `consent(name)` | `consent(name)` | `SendConsent(name)` |
| `deny` | `cmdConsentDeny` | `consentDeny(name)` | `consentDeny(name)` | `SendConsentDeny(name)` |
| `rezaccept` (`acceptrez`) | `cmdRezAccept` | `rezAccept()` | `rezAccept()` | `SendRezzAnswer(true)` |
| `rezdecline` (`declinerez`) | `cmdRezDecline` | `rezDecline()` | `rezDecline()` | `SendRezzAnswer(false)` |

### The `SendRezzAnswer` flag decision

**Two thin dispatcher methods** (`rezAccept()`/`rezDecline()`), not one
method taking a `bool`. This matches the exact precedent already in the
codebase: Guild's `guildAcceptInvite()`/`guildDeclineInvite()` wrap
`SendGuildInviteAccept(bool)` — a sender with the identical shape — as two
separate `IActionHandler`/`ActionDispatcher` methods, not one flag-taking
method. Consistency with that precedent outweighed the minor duplication;
both route to `SendRezzAnswer(bool)`, and the test suite includes the flag
distinction the plan calls out
(`RezAccept_DispatchesRezAccept`/`RezDecline_DispatchesRezDecline`, each
asserting the correct one-argument call reached the handler).

Files touched: `include/client/action/action_dispatcher.h` (+6
`IActionHandler` virtuals, +6 `ActionDispatcher` methods),
`src/client/action/action_dispatcher.cpp` (+6 implementations, all
`checkZoneConnection()` + empty-name guard where `game` had one, mirroring
Guild's shape), `include/client/eq_action_handler.h` /
`src/client/eq_action_handler.cpp` (+6 pure-routing overrides),
`include/client/action/command_processor.h` /
`src/client/action/command_processor.cpp` (+6 `cmd*` declarations,
registrations, implementations), `src/client/eq_commands.cpp` (Corpse and
Resurrection blocks removed), `tests/action_test_fakes.h`
(`RecordingActionHandler` +6 recordings), `tests/test_command_processor.cpp`
(+21 cases), plus **`include/common/packet_structs.h`** and
`src/client/eq_player_info.cpp` for the two wire-layer bugs below (outside
the plan's listed territory strictly by file, but the same class of
"fixing beyond the migration" T133 did directly in `eq_guild.cpp` — not a
new packet handler, not a `GameStateSyncBridge`/state-domain change, so
covered here rather than reported as a STOP).

### Atomicity

The Corpse and Resurrection blocks' six registrations were removed from
`eq_commands.cpp` in the **same commit** as the six new
`CommandProcessor::registerCommand()` calls — no window where both
registries held a command. Confirmed structurally (single commit) and live:
every migrated command shows `registry=action` in the same session; `/loc`
(untouched, still `game`-registered) shows `registry=game`.

### Where each guard ended up

- `corpsedrag`/`consent`/`deny`'s empty-name usage guard: ported verbatim
  to both `CommandProcessor::cmd*` (matching `game`'s exact usage string)
  and `ActionDispatcher::*` (belt-and-suspenders, matching Guild's
  precedent) — neither layer needed a *new* guard, both duplicate the same
  check `game` already had.
- `m_is_dragging_corpse` (guards `SendCorpseDrop`) and `m_has_pending_rezz`
  (guards `SendRezzAnswer`): **left exactly where they were**, inside the
  pre-existing senders in `eq_player_info.cpp`. Not duplicated at the
  dispatcher layer — `EqActionHandler`'s methods are pure routing, per
  T101's established rule and Guild's identical precedent (its
  `m_guild_id`/`m_has_pending_guild_invite` guards stayed in
  `eq_guild.cpp`'s senders too).
- `consent`/`deny`'s `m_zone_connection && m_zone_connected` guard: also
  stayed in the senders; `ActionDispatcher::checkZoneConnection()` is an
  additional, earlier check at the dispatcher layer (same
  belt-and-suspenders shape as every other migrated family).

### Unlike Guild — this family really is alive, confirmed on the wire

All six commands got a **full, observed server round trip** in the live
session — not just `registry=action` with a guard message. See
`review/T134/README.md` for the complete evidence; summary:

```
# Client A (Testmelee)
CMD outcome=applied registry=action recognized=true cmd="/consent Testbuddy"
You have granted Testbuddy permission to drag your corpse.
CMD outcome=applied registry=action recognized=true cmd="/deny Testbuddy"
You have denied Testbuddy permission to drag your corpse.
CMD outcome=applied registry=action recognized=true cmd="/say #castspell 994"
Resurrection offer from Testmelee for corpse 'Testmelee's corpse34' (spell 994)
CMD outcome=applied registry=action recognized=true cmd="/rezdecline"
You have declined the resurrection.
CMD outcome=applied registry=action recognized=true cmd="/say #castspell 994"
Resurrection offer from Testmelee for corpse 'Testmelee's corpse34' (spell 994)
CMD outcome=applied registry=action recognized=true cmd="/rezaccept"
You have accepted the resurrection.
CMD outcome=applied registry=game   recognized=true cmd="/loc"

# Client B (Testbuddy)
CMD outcome=applied registry=action recognized=true cmd="/corpsedrag Testmelee's corpse"
You begin to drag Testmelee's corpse.
CMD outcome=applied registry=action recognized=true cmd="/corpsedrop"
You stop dragging Testmelee's corpse.
CMD outcome=applied registry=game   recognized=true cmd="/loc"
```

The throwaway log also shows the server actively confirming the accepted
resurrection with a teleport: `Server requests zone change: zone=77
instance=0 pos=(-1009.0, 156.0, 49.9) heading=0.0 type=1`.

### How the pending resurrection was produced

**A GM command, on the same single GM account already established
(`willeq_test`, status 200) — no second character and no Will action
needed for the rez itself.** The wrinkle the plan flagged as risky: `#kill`
doesn't leave the dead character standing next to its corpse as a ghost —
this eqemu config auto-teleports the dead character to its bind zone
(`tutorialb`) within seconds, while the corpse stays in the death zone
(`arena`). Resolved with the GM `#zone` command
(`#zone arena <x> <y> <z>`, coordinates from the pre-death zone-in log) to
teleport back, then `/target corpse` (substring match) and
`#castspell 994` ("Customer Service Resurrect" — found via
`SELECT ... FROM spells_new WHERE effectid*=81`, i.e.
`SpellEffect::Revive`; its `classes` columns identify it as the CS/GM-only
rez spell). `Corpse::CastRezz` (eqemu `zone/corpse.cpp`) is
target-and-caster-agnostic, so self-casting works identically to a second
character casting — confirmed by reading the server source before relying
on it, not guessed. Corpse drag/consent/deny used a genuine second
character (`willeq_buddy`/`Testbuddy`) since consent is meaningfully
two-party. Full detail and the exact command sequence: `review/T134/README.md`.

### Two independent, pre-existing wire bugs found and fixed

Both discovered via the plan's explicit warning to check the eqemu zone log
for a size complaint before assuming the migration itself was at fault —
and, in the second case, via the acceptance bar being set at "the offer
parses correctly," not just "a packet arrives."

1. **`SendRezzAnswer`'s double opcode prefix** — the exact bug class T133
   found in `eq_guild.cpp`'s senders: it built its own opcode-prefixed
   packet (`packet.Resize(2 + sizeof(Struct)); packet.PutUInt16(0,
   opcode); memcpy(...+2, ...)`) and then called `QueuePacket(opcode,
   &packet)`, which **also** prepends a 2-byte opcode. Found and fixed
   *before* the live leg ran (by re-checking every sender in
   `eq_player_info.cpp` against the correct calling convention used
   elsewhere, e.g. `combat.cpp`'s `ConsiderTarget`) — `corpsedrag`,
   `corpsedrop`, `consent`, and `denyConsent`'s senders call
   `m_zone_connection->QueuePacket(packet)` directly (a different, correct
   convention: the packet already carries its own opcode, sent raw with no
   second prefix added), so only `SendRezzAnswer` had this bug. Fixed by
   building a body-only packet before calling `QueuePacket()` once.
2. **`Resurrect_Struct` missing a 4-byte field** (`include/common/
   packet_structs.h`) — found live, *after* fix #1, when the first
   corrected `#castspell`-triggered `OP_RezzRequest` still parsed with an
   empty `rezzer_name`, a garbled `corpse_name`, and `spellid=0`. Traced to
   a missing `unknown020` field: eqemu's own `Resurrect_Struct`
   (`common/eq_packet_structs.h`) has a 4-byte unknown field right after
   `z` (offset 20) that ours lacked, shifting every field after `z` by 4
   bytes on both read (`OP_RezzRequest`) and write (`OP_RezzAnswer`, whose
   `action` field landed 4 bytes off, meaning accept/decline could not be
   reliably told apart server-side — the true cause of a first full-session
   attempt where a second cast on the same corpse, after a decline, failed
   with "You were unable to restore the corpse to life"). Confirmed
   byte-for-byte against a captured server packet's own hex dump in the
   eqemu zone log: `your_name` ("Testmelee") starts at wire offset 24, not
   the 20 our struct assumed. Fixed by adding the missing field and
   correcting every subsequent offset comment;
   `static_assert(sizeof(Resurrect_Struct) == 228, ...)`, was 224. Every
   use of the struct in the codebase goes through `sizeof()`
   (`eq_player_info.cpp`'s parse/build, `eq.h`'s `m_pending_rezz` member),
   so no other code needed a change.

Neither bug is a T134 regression: both predate this unit (the struct has
been wrong since it was written; `SendRezzAnswer`'s double-prefix bug is
the same class T133 already found once elsewhere, just not yet checked
here). Both were invisible before now because there was no `action::`
registration exercising these senders' full round trip previously.

### Measured wall-clock, and what it implies

**~100 minutes**, roughly double T132's ~50 minutes. The plumbing itself
(dispatcher/handler/registration/tests) took a comparable amount of time to
T132's — the difference is entirely the live leg: T132 needed zero
protocol-level debugging (its live leg *found* a bug but the fix was T133's
job, not T132's), while this unit needed to find **and fix** two independent
wire bugs before the live leg could succeed, including a full rebuild+retry
cycle for each (first the opcode-prefix fix, verified via a throwaway run;
then the struct-layout fix, found *from* that same throwaway run's garbled
output, requiring a second rebuild+retry before the real two-client
session could be attempted). This is the same shape as T133's "second,
independent bug" cost relative to T132 — a family being genuinely alive
means the acceptance bar (a correct value on the wire, not just a
correctly-sized packet) catches real bugs that a dispatch-layer-only bar
would have shipped past.

### Deviations

- The plan's live-leg risk ("the risky one is producing a pending
  resurrection") materialized differently than guessed — not blocked by
  account/GM-status, but by the dead character being auto-relocated away
  from its own corpse. Resolved with `#zone`, not flagged as needing Will.
- Territory: touched `include/common/packet_structs.h` and
  `src/client/eq_player_info.cpp` beyond the plan's listed file list, for
  the wire-bug fixes described above — same "fixing beyond the migration"
  allowance T133 used, not a new packet handler or state-domain addition,
  so not a STOP condition.

## Board

### End-to-end vs dispatch-layer-only

**All six verified end-to-end on the wire.** Unlike T132 (six of seven
dispatch-only, blocked by a dead `m_guild_id`), every command in this family
got a genuine, observed server round trip: `corpsedrag`, `corpsedrop`,
`consent`, `deny`, `rezaccept`, `rezdecline`. None are dispatch-layer-only.

### Running programme position

Commands migrated so far (T99-T102, T132, T134): Chat, Trade-adjacent
(accept/tradedecline/canceltrade), Combat (12), Social (10), Guild (7),
Corpse+Resurrection (6) — this unit closes the Corpse/Resurrection family
entirely.

Families left:
- **Merchant**: `vendor`, `vendorlist`, `buy`, `sell`, `equip`
- **Utility**: `split`, `save`, `bandage`, `rewind`, `bug`, `petition`

### Revised estimate

Two data points now: T132 (~50 min, net-new, zero wire bugs) and T134
(~100 min, net-new, two independent pre-existing wire bugs found and
fixed). The plumbing cost itself is consistent (~45-55 min for a
6-8-command net-new family: dispatcher/handler/registration/tests). The
swing factor is entirely **whether the family's senders have latent wire
bugs that only a real round trip surfaces** — both discovered bugs
(double-opcode-prefix, struct-layout) were invisible until something
actually exercised the full send→parse path, which only happens once
`action::` registers the command and a live session drives it. Estimate
for Utility (6 commands, likely simple senders, no obvious state-domain
need): ~50-90 min. Merchant is the one to watch — see below.

### Does Merchant look likely to need T103's state-domain work?

**Plausibly yes, more than any other remaining family.** Reasoning:
`vendor`/`vendorlist`/`buy`/`sell` are a stateful transaction sequence
(open a vendor window, see a live item list, buy/sell against it) much
closer in shape to T103's original state-domain trigger than anything in
Corpse/Rez (which is guard-flags-in-the-sender, not a `GameState` domain)
or Guild (no domain needed, confirmed by T132). If `state::GameState` has
no existing merchant/vendor domain for the UI to read current vendor
inventory from, wiring `buy`/`sell` through `action::` may need one — worth
checking `include/client/state/` for an existing vendor domain *before*
dispatching that unit, so it can be scoped correctly rather than
discovered mid-unit and STOPped.
