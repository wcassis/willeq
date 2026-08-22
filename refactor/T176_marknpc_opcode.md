# T176 — Fix the wrong HC_OP_MarkRaidNPC opcode value (0x5191 → 0x5483) and rename

**Slot: orchestrator-direct on `fable`. x86, no server.**

## The bug

`include/client/eq.h` defined `HC_OP_MarkRaidNPC = 0x5191`. `0x5191` maps to **no
opcode** in eqemu's Titanium table — a grep of `patch_Titanium.conf` for `0x5191`
returns nothing. The real opcode is `OP_MarkNPC = 0x5483`
(`utils/patches/patch_Titanium.conf:528`). T169 found this while decoding raids,
annotated it in place ("found, not fixed — outside this unit's scope"), and left
it. This is the follow-up.

## The fix

- **Value** `0x5191` → `0x5483`, verified against `patch_Titanium.conf:528`.
- **Renamed** `HC_OP_MarkRaidNPC` → `HC_OP_MarkNPC`. Titanium has exactly one
  mark opcode, `OP_MarkNPC`; there is no distinct raid-mark opcode (the only
  neighbours are `OP_ClearNPCMarks` and `OP_RaidClearNPCMarks`, both for
  *clearing*). The "Raid" in the old name was a misnomer that would mislead the
  next reader, and the rename cost only two references, so the accuracy win is
  cheap. The load-bearing comment now records the correction and its authority
  instead of the old "found, not fixed" note.
- **Two references** on `fable`: the definition (`eq.h`) and the opcode-name
  string (`src/client/eq_strings.cpp:179`) — both updated. No sender, no dispatch
  arm; the opcode is referenced nowhere else.
- **Regression pin:** `tests/test_titanium_opcodes.cpp` gains
  `TitaniumOpcodesTest.MarkNpcOpcode` asserting `HC_OP_MarkNPC == 0x5483`.

## Acceptance

- `test_titanium_opcodes` **10/10**, including the new pin.
- The `willeq` main binary builds clean tree-wide (the rename touches a
  widely-included header; the whole tree recompiled without error), confirming
  no straggler reference to the old symbol.
- No opcode collision: `0x5483` was not otherwise used.

## Provenance / process note

This unit was first dispatched to a parallel executor, which **landed on the
wrong base branch** — the isolation worktree forked from `main` (an old ancestor
of `fable`, before the `eq_strings.cpp` split and the `refactor/` tree existed),
so its fix renamed the symbol in `main`'s file layout and could not merge onto
`fable` without breaking the build. Its *reasoning* (the rename decision, the
conf authority) was sound and is adopted here; the change itself was re-applied
directly against `fable`'s actual files and re-verified. See the session notes on
the base-branch issue.
