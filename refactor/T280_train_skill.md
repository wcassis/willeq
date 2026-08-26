# T280 — train a skill at a trainer / close the trainer window (T264 F8, the last of the eight)

Slot: slot1. x86. OWNS eqemu (noclobber; T279 slot3 is asset-only, T278
slot2 may hold the lock — bounded wait). Class A. Territory: the
train/close-trainer intents + handler hops T264 KEPT (delist in
`tests/test_reachability_sweep.cpp` when wired), the OP_GMTrainSkill /
trainer senders, CommandProcessor `/train` family (T50 found the server's
max-distance rule), `EverQuest`'s trainer-window state (T136 opened it),
tests. Do NOT touch EQE1/builder/assets (T279 slot3), tradeskill (T278
slot2).

## Gap
`/train` opens the trainer window (T136); nothing can actually train a
skill or close the window. T50 already established the server's
max-distance refusal.

## Scope — verify before coding, three lessons apply
1. **Wire truth from eqemu, not from a member name** (T273/T277): read
   `Handle_OP_GMTrainSkill` / `Handle_OP_GMTrainSkillConfirm` (or whatever
   Titanium actually uses — `patch_Titanium.conf` decides; T277's plan
   named an Underfoot-only opcode and was wrong). Establish: which packet
   trains, what it costs, what the server validates (class, level, skill
   cap, distance, money), and what it sends back.
2. **Check the consumer arm's arguments and its local-state writes**
   (T272/T277 class): if the arm exists, verify it does not remove/spend
   locally and optimistically, and that its argument order matches the
   callee.
3. Command first (T156 rule): `/train <skill|id>` and `/trainclose`.
   Refuse locally only what the client can know; let the server refuse the
   rest. Route the click (if the trainer panel has one) through the same
   body (T230/T239 one-body rule) — if there is no panel hit-test, say so
   like T277 did.
4. Tests: train request bytes for a skill; close request; the skill value
   and the money change come from the server's reply, NOT optimistically.
   Red/green.
5. Live: a trainer NPC (`#npctypespawn` — T227's banker recipe shows the
   pattern; find a class-appropriate trainer for Testchar the warrior),
   `/train <skill>` → `character_skills` gains/raises the row and the coin
   is deducted server-side; `/trainclose`. Restore rows.

## Verification
Full ctest; visual gate if a panel changed; Review; evidence review/T280/.
Commit: `T280: skills can be trained at a trainer`.
