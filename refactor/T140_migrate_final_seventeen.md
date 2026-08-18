# T140 — Migrate the 17 `migrate-action` commands

**Slot: whichever is free. x86. eqemu lock required (live leg). No Pi.**

**Territory: `src/client/eq_commands_movement.cpp`,
`src/client/eq_commands_spells.cpp`, `src/client/eq_commands.cpp`,
`src/client/action/`, `include/client/action/`,
`src/client/eq_action_handler.{h,cpp}`, `include/client/eq.h` (thin wrappers
only), `tests/`, `refactor/`, `/mnt/projects/fable/review/T140/`.**

Read T138's `review/T138/triage_table.tsv` (these are its 17 `migrate-action`
rows), then T132's Review (the net-new recipe, ~50 min baseline) and T136's
(the `Request*` wrapper recipe).

## The worklist

`loc`, `sit`, `stand`, `move`, `moveto`, `warp`, `zoning`, `follow`,
`stopfollow`, `quit`, `camp`, `debug`, `perf`, `time`, `bank`, `train` — plus
**`equip`**, treated separately below.

T138 traced each: **12 call an already-public `EverQuest` method**, **3
(`warp`, `time`, `bank`) need one thin wrapper each** on T136's `Request*`
recipe, and **2 (`debug`, `perf`) match existing action-registry diagnostic
precedent**.

## `equip` — do it LAST, and stop if it grows

`equip` is the one T136 and T138 **independently** concluded needs its own
design pass: it goes through `InventoryManager` (private, no `EqActionHandler`
accessor), is ~150 lines, and is categorised `Inventory` rather than sharing
these commands' shape.

**Do the other 16 first and commit them.** Then attempt `equip` **only if the
batch is green and time remains**. **STOP and report** the moment it needs more
than a thin accessor — pulling `InventoryManager` into the action layer is its
own unit, and two prior units already reached that conclusion. **Do not
re-litigate it under batch pressure.**

## The sequencing hazard — `camp` and `quit` end the session

`quit` → `RequestQuit()` sets `m_quit_requested`, polled by `Application`'s
main loop; the client exits. `camp` → sit + `StartCampTimer()`, which logs out
after its countdown.

**Anything scripted after them never runs.** A tour that puts `/camp` in the
middle will silently skip every later command while still looking like it
completed. **Put them last, and give each its own run** so one doesn't consume
the other's verification.

This is the highest-probability way to lose an afternoon on this unit.

## `warp` deserves care

`warp` **directly writes `m_x`/`m_y`/`m_z` and the entity's position** without
a server round trip — a debug/teleport path, not a normal movement command.
**Preserve its exact semantics**, including whatever it does about the
subsequent position update. If migrating it changes when or whether the server
is told, that is a behaviour change, not a refactor — **STOP and report**.

## Two things already checked, so you don't have to

- **`bank` and `train`'s guard state is genuinely populated** — `m_banker_npc_id`
  and `m_trainer_npc_id` are both written from server packets
  (`eq_merchant.cpp:1402`, `:1537`). **This is not a repeat of the
  `m_guild_id` dead-guard situation**; don't go hunting for one.
- **Guards stay where they are** (T134's rule): stateful guards belong in the
  layer owning the state; only usage-string guards move up.

## Check for wire rot — it has appeared in three consecutive units

`train` (`RequestTraining`) and `move` (position sends) reach the wire.
T133 and T134 each found a **double-opcode-prefix** bug — a sender writing its
own opcode *and* calling `EverQuest::QueuePacket(opcode, body)`, which prepends
another.

**But remember T135's correction**: `m_zone_connection->QueuePacket(packet)`
requires the caller to write the opcode, and four senders looked buggy and were
not. **Never infer the bug from shape — prove it from a live server-log size
complaint.**

Also: T135's petition bug and T136's buy/sell were visible **only in the DB**,
not the zone log. If a command's effect is persisted, **check the DB**.

## Atomicity and completeness

**Remove from the `eq_commands_*.cpp` files in the same commit that adds the
`CommandProcessor` registration.** Verify via
`Application::printCommandResult()`: `registry=action` per command, plus a
`registry=game` control from the 7 `stay-game` rows.

**After this unit the game registry should hold 11 commands** (27 − 16), or
**10** if `equip` also lands. State the number and list what remains as a
check.

## Verification

- x86 suite (current total **2361**; sole failure the documented ALSA
  `AudioManagerTest.PreloadCommonSounds`) plus ARM cross-compile.
- **Tests at the programme's density** — T132: 22/7, T134: 18/6, T135: 19/6,
  T136: 29/4.
- **Visual gate goldens must not move.**
- Live evidence in `review/T140/`, with `camp`/`quit` in their own runs.
- **Report which commands you verified on the wire versus dispatch-layer
  only.** Every unit in this programme has bounded that honestly; keep it.

## Review

(executor fills: the 16 with their dispatcher/handler mapping; the three thin
wrappers and why each was needed; what happened with `equip`; the
`camp`/`quit` run structure; `warp`'s semantics confirmed preserved; any wire
rot found, with live proof rather than inference; atomic-commit confirmation
and the remaining registry count; **measured wall-clock** against T132 ~50 /
T134 ~100 / T135 ~70 / T136 ~110 min; deviations)

## Board

Give the remaining registry contents and what is left of T138's worklist. With
the deletions done and these migrated, the only open items should be the 7
`stay-game` (no work) and the 3 `needs-decision` for Will — **say plainly
whether that is now the whole picture**, since I have mis-stated the
programme's endpoint once already.
