# T251 — "You are missing <item>." for missing spell components (T246 leftover)

Slot: slot2. x86. NO eqemu. Class A. Territory: src/client/spell/
spell_cast_checks.cpp `checkReagents` (return WHICH component is missing),
include/client/spell/cast_refusal.h, src/client/eq_strings.cpp fallback
rows, the two refusal sites T244 unified (eq_action_handler
beginCastFromGem, eq_bridge CastSpellIntent — message rendering only),
tests. Do NOT touch `checkPlayerState` / the stun flag (T247 slot1),
inventory_manager.* (T250 slot3).

## Gap
T246 refuses with eqstr 272 ("You are missing some required components.")
but the official client uses **433 "You are missing %1."** with the item
name. Also `eq_strings.cpp`'s fallback for 272 says "…required spell
components." vs eqstr's "…required components." (T246 note).

## Scope
1. `checkReagents` reports the first missing component's item id;
   resolve the name via the item database the client already has (grep
   how item names are looked up for tooltips / `/equip` messages — the
   items table in willeq.db or the ItemPacket cache). If no name source is
   available offline for an item never seen, fall back to 272.
2. Both refusal sites render 433 with the name via `GetStringMessage`'s
   %1 substitution (verify it supports substitution; T178 did `%1` for
   WhoAll count).
3. Fix the 272 fallback text to match eqstr exactly.
4. Tests: missing reagent with a known name → "You are missing Bone Chips."
   (or whatever the test spell needs); unknown name → 272 text. Red/green.

## Verification
Full ctest; Review; evidence review/T251/. Commit:
`T251: missing spell components are named`.
