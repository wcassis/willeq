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

(executor fills: the six commands with their dispatcher/handler mapping;
what `bandage` needed and whether existing targeting was reused; atomic-commit
confirmation and what remains in `eq_commands.cpp`; where each guard ended up;
live evidence per command, with wire-effect vs dispatch-only stated explicitly;
**measured wall-clock** against T132's ~50 min and T134's ~100 min; deviations)

## Board

State the programme position: with Utility done, **only Merchant** (`vendor`,
`vendorlist`, `buy`, `sell`, `equip`) remains. T134's Board already flagged
Merchant as the family most likely to need T103's `GameStateSyncBridge`/
state-domain work — **if anything you touched here sharpens or contradicts
that read, say so**, since it is what the final unit gets scoped against.
