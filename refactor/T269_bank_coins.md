# T269 — bank coin deposit / withdraw / convert (T264 F3)

Slot: slot2. x86. OWNS eqemu (noclobber) for a live deposit/withdraw with
DB proof. Class A. Territory: the bank-coin intents/handlers T264 KEPT
(its Review names them), CommandProcessor `/bank` family, eq_merchant.cpp
bank senders (OP_MoveCoin), tests. Do NOT touch loot (T268 slot3), race
loader (T267 slot1).

## Gap
`/bank` opens the banker window (T136); the coin move handler exists with
no producer. Titanium: OP_MoveCoin {from_slot, to_slot, cointype1,
cointype2, amount} — verify in eqemu common/eq_packet_structs.h
MoveCoin_Struct and zone/client_packet.cpp Handle_OP_MoveCoin (slot ids
for inventory vs bank coin pools, and the convert rule: 10:1 across
denominations).

## Scope
1. Commands (T156 rule): `/bank deposit <amount> <pp|gp|sp|cp>`,
   `/bank withdraw <amount> <denom>`, `/bank convert <amount> <from> <to>`
   (if convert is a client-side MoveCoin with different cointypes — verify).
   Refuse locally when the pool lacks the coins (official string if one
   exists) — send nothing.
2. Route through the kept handler hop; update `test_reachability_sweep`'s
   expected set.
3. Tests: MoveCoin bytes per command; refusal sends nothing; the local
   currency projection (GameState player().currency + bank currency)
   updates from the server's echo, not optimistically. Red/green.
4. Live: `/bank deposit 5 pp` with the banker open → DB `character_currency`
   platinum -5 / platinum_bank +5; withdraw back; `/bank` closed → refused.
   Restore rows.

## Verification
Full ctest; Review; evidence review/T269/. Commit:
`T269: bank coin deposit, withdraw and convert reachable`.
