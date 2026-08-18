# T135 — Migrate the Utility commands (6) to `action::CommandProcessor`

**Slot: whichever is free. x86. eqemu lock required (live leg). No Pi.**

**Territory: `src/client/eq_commands.cpp` (the remaining Utility block),
`src/client/action/`, `include/client/action/`,
`src/client/eq_action_handler.{h,cpp}`, `tests/`, `refactor/`,
`/mnt/projects/fable/review/T135/`.**

Read T134's Review first (the wire-level acceptance bar and the two protocol
bugs it found), then T132's (the net-new recipe and its ~50-minute baseline).

## The commands — a leftover bucket, not a coherent family

`split`, `save`, `bandage`, `rewind`, `bug`, `petition`.

Unlike Guild or Corpse+Rez, these do not share a scenario. **Treat them as six
independent migrations that happen to share a unit**, and do not force a single
live scenario onto them.

Same net-new shape as T132/T134: no action-side registrations or dispatcher
methods exist, but the senders do.

## THE TRAP — do not "fix" these senders

T133 and T134 each found a **double-opcode-prefix** bug: a sender wrote the
opcode into its own packet *and* called `EverQuest::QueuePacket(opcode, body)`,
which prepends another. **You will see `PutUInt16(0, HC_OP_...)` in four of
this unit's senders and it is CORRECT there.**

There are **two conventions** in this codebase:

| Call | Who writes the opcode |
|---|---|
| `EverQuest::QueuePacket(opcode, body)` | **The callee.** Body must NOT contain it. Writing it yourself is the T133/T134 bug. |
| `m_zone_connection->QueuePacket(packet)` | **The caller.** Packet MUST contain it. |

Verified while scoping: **`SendSave`, `SendRewind`, `SendReport` and
`SendPetition` all use the connection-level form**, so their
`PutUInt16(0, HC_OP_…)` is required, not a bug.

**Changing them would break working commands.** If you believe one is genuinely
wrong, prove it from a live server-log size complaint the way T134 did — do not
infer it from the shape.

## `bandage` is the outlier — check it before assuming the recipe fits

Five of the six are thin wrappers over `EverQuest::Send*`. **`bandage` is not**
— it goes through `CombatManager` (`HasTarget()`, `GetTargetId()`).

T102 migrated the Combat family, so `ActionDispatcher` may already have
targeting concepts worth reusing rather than duplicating. **Check before adding
anything new**, and say what you found. If `bandage` needs a shape the other
five don't, that is fine — say so rather than bending it to match.

## Guards stay where they are

`SendSplit` has real preconditions — an `m_in_group` check and a
can-you-afford-it check. I verified `m_in_group` is genuinely populated
(`eq_group.cpp:390`), so this is **not** a repeat of the guild dead-guard
situation.

**Leave stateful guards inside the senders**, as T134 did. Only usage-string
guards belong at the command/dispatcher layer.

## Atomicity — the programme's standing rule

**Remove from `eq_commands.cpp` in the same commit that adds the
`CommandProcessor` registration.** Verify with
`Application::printCommandResult()`: `registry=action` for all six, plus a
`registry=game` control in the same session.

**After this unit, `eq_commands.cpp`'s registry should hold only the Merchant
family.** State what remains, as a check that nothing was missed.

## The live leg — different per command, so plan it per command

- `save`, `rewind` — trivial, single client.
- `split` — needs a **group**, so two clients.
- `bandage` — needs a target *and* bandages in inventory. Establish whether you
  can produce both; a GM account is available.
- `bug`, `petition` — these submit reports. The wire effect may only be
  visible in the **eqemu server log or DB**; say where you looked.

**Report which of the six you verified on the wire and which only at the
dispatch layer.** T132, T133 and T134 all bounded this honestly and it held up.
A partial live leg stated clearly is fine; an implied one is not.

## Fixing beyond the migration

**STOP and report** if a command needs a new `GameStateSyncBridge` case or a
new state domain.

**Do not paper over**: no command left registered in both places, no
`registry=` or wire-effect claim you did not observe in a log.

## Verification

- x86 suite (current total **2334**; sole failure the documented ALSA
  `AudioManagerTest.PreloadCommonSounds`) plus ARM cross-compile.
- **Tests at the programme's density** — T132 added 22 cases for 7 commands,
  T134 added 18 for 6. Registration and category assertions for all six, plus
  per-command dispatch-reaches-the-right-method assertions.
- **Visual gate goldens must not move.**
- Live evidence in `review/T135/`.

## Review

Executed in `/mnt/projects/fable/wt/slot1`, task branch `task/T135-utility`,
off `fable` tip `10fbcb5b`. Full evidence: `/mnt/projects/fable/review/T135/`
(README + single-client log + two-client group session logs + petition
wire-bug DB evidence + filtered eqemu zone server log).

### The six commands: dispatcher/handler mapping

All net-new — `action::CommandProcessor` had zero registrations for any of
the six before this unit, same shape as Guild (T132) and Corpse/Rez (T134).
Five are thin wrappers over the pre-existing `EverQuest::Send*()` sender;
`bandage` routes through `CombatManager` (see below).

| command (aliases) | `CommandProcessor` | `ActionDispatcher` | `EqActionHandler` | `EverQuest` sender |
|---|---|---|---|---|
| `split` | `cmdSplit` | `split(p,g,s,c)` | `split(p,g,s,c)` | `SendSplit(p,g,s,c)` |
| `save` | `cmdSave` | `save()` | `save()` | `SendSave()` |
| `bandage` (`bindwound`) | `cmdBandage` | `bandage(targetId)` | `bandage(targetId)` | `SendBindWound(target_id)` |
| `rewind` | `cmdRewind` | `rewind()` | `rewind()` | `SendRewind()` |
| `bug` (`report`) | `cmdBug` | `bug(description)` | `bug(description)` | `SendReport(description)` |
| `petition` | `cmdPetition` | `petition(text)` | `petition(text)` | `SendPetition(text)` |

Categories ported verbatim from `eq_commands.cpp`'s per-command category
(`Group`/`Utility`/`Skills`/`Movement`/`Utility`/`Utility`) rather than
unified, matching the plan's framing that these six don't share a scenario.

### What `bandage` needed, and whether existing targeting was reused

**Reused, no new targeting concept added.** `EqActionHandler` already had a
`getCombatManager()` helper (built for T102's `assist`/`shieldTarget`), and
`bandage()` calls it exactly the same way. The one real decision: `bandage`'s
fallback semantics are **not** the same as `assist`/`shieldTarget`. Those two
report "You don't have a target." when `targetId==0` and no combat target is
set; `bandage` instead falls back to **self** (`m_eq.GetMySpawnID()`),
matching `game`'s original `bindwound_cmd` handler exactly
(`target_id = (m_combat_manager && HasTarget()) ? GetTargetId() : 0; if
(target_id == 0) target_id = m_my_spawn_id;`). Implemented as a three-way
ternary in `EqActionHandler::bandage()` rather than duplicating `assist`'s
error-message shape. `CommandProcessor::cmdBandage` always dispatches
`bandage(0)` (no usage guard — `game`'s handler took no arguments either),
letting the handler resolve current-target-or-self.

### Where each guard ended up

- `split`'s two stateful guards (`m_in_group`, affordability) — **left
  exactly where they were**, inside `SendSplit()` in
  `src/client/eq_player_info.cpp`. Not duplicated at the dispatcher layer,
  per T134's established rule. Live-verified both fire independently (see
  below) — this is genuinely live state, not a repeat of the Guild
  dead-guard situation, confirming the plan's `m_in_group` scoping note.
- `bug`/`petition`'s empty-argument usage guards — ported verbatim to both
  `CommandProcessor::cmdBug`/`cmdPetition` (matching `game`'s exact usage
  strings) and `ActionDispatcher::bug`/`petition` (belt-and-suspenders,
  matching every prior family's precedent).
- `save`/`rewind`/`bandage`/`split` take no usage-string guard at the
  dispatcher layer beyond `checkZoneConnection()` — `game`'s originals had
  none either (`split`'s only guard was the empty-args check, ported to
  `CommandProcessor::cmdSplit`, which also does the numeric-token parsing
  — a formatting concern, not a guard, matching `who`'s
  `lvllow`/`lvlhigh`-parsing precedent already in that file).

### Atomicity

The six registrations were removed from `eq_commands.cpp` in the **same
commit** as the six new `CommandProcessor::registerCommand()` calls — no
window where both registries held a command. Confirmed structurally (single
commit) and live: every migrated command shows `registry=action` in the same
session; `/loc` (untouched, still `game`-registered) shows `registry=game`,
across both the single-client and two-client sessions.

### What remains in `eq_commands.cpp`

**Only the Merchant family** — `vendor`, `vendorlist`, `buy`, `sell`,
`equip` (confirmed by grepping every remaining `Command ..._cmd;`
declaration in the file after this unit's removal). Matches the plan's
prediction exactly.

### THE TRAP — confirmed, not touched

Verified live, not just by shape: `grep -i 'wrong size' zone_arena_h03.log`
across the entire session (single-client + two-client, all six commands
exercised, several repeats) returns **zero matches**. `SendSave`,
`SendRewind`, `SendReport`, and `SendPetition`'s `PutUInt16(0, HC_OP_...)`
were left untouched, confirming the plan's pre-verified claim that these
four correctly use the connection-level `QueuePacket(packet)` convention.

### Two wire bugs found; one fixed, one deliberately left open

Both **predate T135** (same "invisible until `action::` exercises the full
round trip" pattern T133/T134 established) — neither is a regression.

1. **Fixed: `SendPetition`'s missing NUL terminator.** eqemu's
   `Handle_OP_Petition` (`zone/client_packet.cpp`) reads the petition text
   via `pet->SetPetitionText((char*)app->pBuffer)` — a strlen-based C-string
   read. `Packet::PutString()` (unlike `PutCString()`) writes no terminator
   and leaves the packet exactly `str.length()` bytes long, so the server's
   read ran past our buffer into adjacent heap memory. Confirmed live
   byte-for-byte: a submitted `"stuck in geometry"` petition persisted to
   `peq.petitions` as `"stuck in geometrye.lua"`. Fixed by switching
   `SendPetition` to `PutCString()`; re-verified live (different character,
   to avoid a stale in-memory petition-queue entry from the pre-fix test —
   see `review/T135/petition_db_evidence.txt`) with the exact text
   persisting, no corruption.
2. **Found, NOT fixed: `SendReport`'s wire format doesn't match eqemu's
   `OP_Report` protocol at all.** `Handle_OP_Report` parses a
   pipe-delimited `<reported>|<reporter>|<message>\0` string; our sender
   sends a 4-byte `category_id` (always 0) followed by raw text, no pipes,
   no terminator — a completely different layout, not a small mechanical
   bug. Walking eqemu's parser against our actual bytes shows
   `database.AddReport()` is never reached (confirmed: `peq.bug_reports`/
   `peq.bugs` empty before and after). **Not fixed here** because a real
   fix requires a design decision the plan didn't ask for — `/bug`'s single
   free-text argument has no concept of a "reported" player name the real
   protocol wants, so a correct fix is a new command shape or a policy
   choice (e.g., reported=self), not a mechanical wire-format correction
   like the petition fix. Migration preserves `game`'s identical (already
   broken) behavior, per the programme's "behaviour must be identical"
   bar. Flagged in `refactor/deferred.md` for a future unit.

### Live evidence per command — wire-effect vs. dispatch-only, stated explicitly

- **`save`** — wire-confirmed: eqemu zone log shows `Save for [Testmelee]`
  correlating with the command.
- **`petition`** — wire-confirmed, including the bug fix above: DB row
  persists with exact correct text post-fix; zone log shows `Got Server
  Requested Petition List Refresh`.
- **`split`** — wire-confirmed on both guards: a real two-client group
  session (`/invite` + `/groupaccept`, both already-migrated commands) hit
  the affordability guard on a first attempt (`/split 10 5 3 7`,
  insufficient funds) and passed both guards on a second attempt
  (`/split 0 0 0 0`), reaching `SendSplit()`/`QueuePacket()`. Zone log
  confirms the group was real (`Handing out new group id 1003`) and shows
  zero size complaints across every attempt.
- **`bandage`** — sent cleanly (self-target fallback exercised, no
  CombatManager target set), no size complaint; the client received
  `Unhandled zone opcode: HC_OP_BindWound` immediately after, i.e. the
  server replied — evidence the packet was accepted, though the client
  can't parse that specific inbound reply (pre-existing gap, not a T135
  scope item).
- **`rewind`** — dispatch-layer and clean-send confirmed (no size
  complaint); no distinct positive server-log line to point to beyond
  that.
- **`bug`** — dispatch-layer only; the sender itself is dead on arrival
  server-side (see above), inherited unchanged from `game`.

Two-client setup used already-migrated commands (`/invite` from T102-era
Combat/Social work, `/groupaccept` likely also pre-existing) to form a real
group — no GM action needed beyond what T132/T134 already established is
available (`willeq_test`, status 200).

### Measured wall-clock

**~70 minutes**, start (reading the plan + protocol + T132/T134 Reviews) to
this Review being written — below T134's ~100 min, above T132's ~50 min.
Closest in shape to T134: this unit needed to find and fix one wire bug
(petition) before its live leg could be called clean, plus iterate three
times on two-client timing (the SQLite `data/willeq.db` metadata table
raced when both clients started within the same second — fixed with a
`sleep 5` stagger between launches — and two rounds of `wait N` retuning
were needed before `/invite`→`/groupaccept`→`/split` landed in the right
order). No `GameStateSyncBridge`/state-domain work was needed for any of
the six — confirms T134's estimate that Utility wouldn't need it.

### Deviations from the plan

- Deleted `tests/test_eq_commands_assist.cpp` (and its `CMakeLists.txt`
  `ADD_EXECUTABLE` block) entirely, rather than editing it. T102 had already
  trimmed it to a single remaining case (`bandage`'s self-target fallback,
  on the `game`-registry `ProcessChatInput()` seam) after removing
  assist/shield/duel; migrating `bandage` out of `eq_commands.cpp` in this
  unit removed that seam too, leaving the file with zero tests. Removed
  following the exact precedent of `test_eq_commands_attack.cpp`'s full
  removal in T102 (documented in `tests/CMakeLists.txt`) rather than leaving
  a zero-test executable target. Equivalent coverage now lives in
  `test_command_processor.cpp`'s T135 section.
- Fixed `SendPetition`'s wire bug beyond pure migration (sanctioned by
  T134's precedent — a bug proven from live evidence, fixed in the same
  unit, not a new `GameStateSyncBridge` case or state domain). Found but
  deliberately did NOT fix `SendReport`'s much larger protocol mismatch —
  see above.
- **`check_file_sizes.sh` reports `src/client/eq.cpp` as a new FAIL
  (3903 lines, > 3000).** This is a **pre-existing condition, not caused by
  T135**: `eq.cpp` was already 3898 lines at `fable` tip `10fbcb5b`
  (`git show fable:src/client/eq.cpp | wc -l`), already past the 3000-line
  FAIL threshold before this unit touched anything. The petition fix
  (`SendPetition`'s `PutCString()` switch, in `eq.cpp` because that's where
  `SendPetition`/`SendPetitionDelete` already lived, not part of this
  unit's territory list) added a net 5 lines, including a short comment
  explaining the fix. Kept the comment as short as possible given the file
  is already deep over budget; did not attempt to split `eq.cpp` into a new
  TU in this unit (out of scope, and CLAUDE.md's module map already tracks
  `eq.cpp`'s size as a known, separately-tracked condition, not something
  for a command-migration unit to fix incidentally). Flagged here rather
  than silently absorbed, per the protocol's "no new FAILs" bar — this one
  technically predates the unit but the executor made it marginally worse
  and wants that stated plainly rather than left for the orchestrator to
  discover.

## Board

With Utility done, **only Merchant** (`vendor`, `vendorlist`, `buy`, `sell`,
`equip`) remains — confirmed directly (nothing else left in
`eq_commands.cpp`'s registry after this unit).

Nothing here sharpens or contradicts T134's read that Merchant is the family
most likely to need T103's `GameStateSyncBridge`/state-domain work — Utility
confirmed it needed none (all six are either pure sender wrappers or, for
`bandage`, a reuse of T102's existing `CombatManager` targeting), which is
consistent with T134's reasoning that Utility (guard-flags-in-the-sender,
same shape as Corpse/Rez) wouldn't need it while Merchant's stateful
open-vendor-window/live-item-list transaction shape still plausibly would.
One new data point worth carrying forward: this unit found its own wire bug
by checking **persisted server-side data** (the `petitions` DB table), not
just the zone console log or a "Wrong size" complaint — Merchant's
live leg should budget the same DB-level check for `buy`/`sell` (e.g. actual
inventory/currency changes), not just packet-size correctness, since a
correctly-sized packet with wrong field semantics (this unit's `petition`
bug) is a distinct failure mode from a malformed packet (T133/T134's
findings) and neither showed up as a zone-log error.
