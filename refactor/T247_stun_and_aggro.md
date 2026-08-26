# T247 — instantaneous OP_Stun is dropped; CombatTarget::is_aggro is hard-coded false

Slot: slot1. x86. OWNS eqemu briefly (noclobber) for the live leg. Class A
(T245 + T246 findings). Territory: src/client/eq.cpp `OP_Stun` handler
(~1450), src/client/combat.cpp `is_aggro` / `CheckForAggro`, the
`OP_Action` / combat-message handlers that reveal an attacker
(eq_entities.cpp Damage/Action on SELF — read-only unless a one-line flag),
SpellManager's stun sub-check (spell_cast_checks.cpp — consume the flag),
tests. Do NOT touch audio/* (T248), inventory/* (T249).

## 1. OP_Stun
`eq.cpp:~1450` logs OP_Stun and stores nothing, so a duration stun (not a
buff) never blocks casting/movement locally. eqemu `Stun_Struct{duration}`
(verify in common/eq_packet_structs.h). Store `m_stunned_until` (steady
clock + duration ms), clear on expiry and on `OP_Stun{0}` if the server
sends it; `checkPlayerState` reads it (T246 left the hook). Test: stun
packet → cast refused 175 → after duration → allowed. Red/green.

## 2. is_aggro
`CombatTarget::is_aggro` is a hard-coded `false` TODO, so `CheckForAggro`
never fires — no auto-defence. Derive it from what the client already
receives: an `OP_Action`/`OP_Damage` with `target == our spawn id` from an
NPC marks that NPC aggro for N seconds (eqemu's hate is not sent; the
official client infers the same way). Wire the flag; `CheckForAggro`
engages only when `/combat on` (keep the existing gate). Test: damage
packet from spawn X on self → X is_aggro → `CheckForAggro` selects X.
Red/green.

## Live
befallen (first `#depopzone` + `#repop` to clear the leftover pups — T245
note), `/combat on`, let a pup hit Testchar → auto-target + swings without
`/attack`; `#stun 5000`-style GM command if one exists → `/cast` refused
"You are stunned" for 5 s.

## Verification
Full ctest; Review; evidence review/T247/. Commit:
`T247: stuns are tracked; attackers are aggro -- auto-defence works`.
