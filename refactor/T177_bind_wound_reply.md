# T177 — Parse the inbound OP_Bind_Wound reply so the player sees the outcome

**Slot: orchestrator-direct on `fable`. x86.**

## The gap

The client SENDS `OP_Bind_Wound` to start a bandage (T162 wired `/bandage` →
`useSkill(9)`), but had **no parser for the server's reply** — there was no
`HC_OP_BindWound` case in the zone dispatch switch at all. So ~10 seconds after
starting a bind (or immediately, on failure), the server's answer was dropped on
the floor and the player was shown nothing: not success, not "target has left",
and not the common **"you have no bandages"** case, where the whole attempt fails
silently.

The reply reuses the send opcode (`OP_Bind_Wound = 0x601d`, verified against
`patch_Titanium.conf:146`) and carries a `BindWound_Struct` whose `type` field
(packet offset 6: 2 opcode + `type@4`) is the outcome.

## The fix

A new `ZoneProcessBindWound` parser, dispatched from the `HC_OP_BindWound` case,
reads `type` and shows the player the outcome. The `type → message` mapping is a
**pure, total function** `EQT::bindWoundOutcomeMessage` in its own
`bind_wound_outcome.{h,cpp}` (so it is unit-testable without spinning up
`EverQuest`), following the T169/T173 pattern of extracting decode logic.

Authority is eqemu `Client::BindWound` (`zone/client.cpp:3462`):

| type | meaning (eqemu) | shown to player |
|---|---|---|
| 1 | Done — wound bound | "You have bound your wounds." |
| 4 | No / dead target ("bindmob dead") | "You cannot bind wounds on that target." |
| 5 | Target left the zone before the timer finished | "Your bind wound target is no longer in the zone." |
| 7 | No bandage in inventory | "You have no bandages." |
| 0 | Client unlock / state reset (follows every real message) | — (silent) |
| 3 | Start control tick | — (silent, see below) |

**Type 3 is deliberately NOT surfaced.** It is a control tick sent *before* the
outcome is known — eqemu emits `type 3` at the top of both the normal start AND
the no-bandage failure path (which then sends `type 7`). Showing "you begin to
bandage" would immediately contradict the "you have no bandages" that may follow.
The start is already covered on the send side, which prints "You use Bind
Wound!" (T162). So the parser surfaces only the four definitive outcomes and logs
the rest.

## Acceptance

- `test_bind_wound_outcome`: **6/6** — the four outcome strings, the two control
  types returning empty, and unknown types returning empty (not garbage).
- `willeq` main binary builds clean (the new dispatch case, header decl, and
  parser compile tree-wide).
- Opcode and offset verified against eqemu: `OP_Bind_Wound=0x601d`
  (`patch_Titanium.conf:146`), `type` at struct offset 4 → packet offset 6.

## Owed

- **Live wire probe.** This is a receive-path change, the class where T155/T161
  found surprises, so it is worth confirming end to end: with eqemu up, remove
  the character's bandages and `/bandage` → expect **"You have no bandages."**;
  with a bandage, expect **"You have bound your wounds."** ~10 s later. Not run
  here (proportionate to a pure, opcode-verified mapping), but cheap when the
  server is up.

## Note — a stale board item closed without code

The todo list also carried "`getSkillUseError()` returns 'You must be behind
your target' unconditionally for `requires_behind`". That was **already fixed by
T159** — the `requires_behind` block in `getSkillUseError`
(`skill_manager.cpp:283`) is now an empty TODO pending facing support, and the
only remaining occurrence of the string is the explanatory comment. No code
change was needed; the board entry was stale and is removed.
